/*
 * greeter/ui-gtk4.c — GTK4 implementation of the greeter UI.
 *
 * Implements greeter_run_ui() declared in ui.h.  The entire toolkit dependency
 * is contained in this file: to swap to a different toolkit, replace this file
 * and update the meson.build source list.  Neither greeter/main.c nor the
 * daemon need any changes.
 *
 * Fullscreen window with a dark background; a centered card holds a username
 * entry (pre-filled from ATRIUM_USERNAME), a password entry, and a "Log In"
 * button.
 *
 * On submit, the credentials are written to credentials_fd as
 * "<username>\0<password>\0" and the UI waits for a response on result_fd.
 * The daemon (or greeter-test) replies with "ok\n" on success or
 * "fail:<reason>\n" on failure; the former exits cleanly, the latter shows
 * an error label and allows retry.  When either fd is -1 (standalone dev run
 * without a daemon), the button quits the app directly.
 */

#include "ui.h"
#include "log.h"

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <string.h>
#include <unistd.h>

/*
 * CSS for the greeter window.
 *
 * The window itself carries the dark background.  The .card class gives the
 * inner widget a rounded, lighter panel so it reads as a distinct surface
 * floating over the background.
 */
static const char CSS[] =
    "window {"
    "  background-color: #1e1e2e;"
    "}"
    ".card {"
    "  background-color: #313244;"
    "  border-radius: 12px;"
    "  padding: 40px 48px;"
    "  min-width: 360px;"
    "}"
    ".card .heading {"
    "  color: #cdd6f4;"
    "  font-size: 20px;"
    "  font-weight: bold;"
    "  margin-bottom: 24px;"
    "}"
    ".card entry {"
    "  background-color: #1e1e2e;"
    "  color: #cdd6f4;"
    "  border: 1px solid #45475a;"
    "  border-radius: 6px;"
    "  padding: 8px 12px;"
    "  margin-bottom: 12px;"
    "  caret-color: #cdd6f4;"
    "  outline: none;"
    "}"
    ".card entry:focus-within {"
    "  border-color: #89b4fa;"
    "  box-shadow: 0 0 0 1px #89b4fa;"
    "  outline: none;"
    "}"
    ".card entry text selection {"
    "  background-color: #89b4fa;"
    "  color: #1e1e2e;"
    "}"
    ".card button.suggested-action {"
    "  background-image: none;"
    "  background-color: #89b4fa;"
    "  color: #1e1e2e;"
    "  border: none;"
    "  box-shadow: none;"
    "  border-radius: 6px;"
    "  padding: 10px 24px;"
    "  margin-top: 8px;"
    "}"
    ".card button.suggested-action:hover {"
    "  background-image: none;"
    "  background-color: #b4d0ff;"
    "}"
    ".card button.suggested-action:focus {"
    "  outline: none;"
    "  box-shadow: 0 0 0 2px #cdd6f4;"
    "}"
    ".card .spinner {"
    "  color: #cdd6f4;"
    "  margin-bottom: 8px;"
    "}"
    ".error-label {"
    "  color: #f38ba8;"
    "  font-size: 13px;"
    "  margin-bottom: 8px;"
    "}";

/*
 * activate_ctx — arguments forwarded from greeter_run_ui() to on_activate().
 *
 * GLib's activate signal passes a single user_data pointer, so we bundle
 * the relevant fields here.  Allocated on the stack in greeter_run_ui();
 * g_application_run() blocks until the app quits so the lifetime is safe.
 */
typedef struct {
    const char *username;
    int         credentials_fd;
    int         result_fd;
} activate_ctx;

/*
 * login_ctx — data shared between the on_login callback and on_result_ready.
 *
 * Passed as user_data to the button's "clicked" signal and the password
 * entry's "activate" signal (fired when the user presses Enter).  Allocated
 * with g_new() in on_activate(); intentionally not freed — the process exits
 * immediately after g_application_quit() returns control to greeter_run_ui().
 */
typedef struct {
    GtkApplication *app;
    GtkWidget      *window;
    GtkWidget      *username_entry;
    GtkWidget      *password_entry;
    GtkWidget      *button;
    GtkWidget      *spinner;
    GtkWidget      *error_label;
    int             credentials_fd;
    int             result_fd;
} login_ctx;

/*
 * on_result_ready — handle the daemon's authentication response.
 *
 * Called by the GLib main loop when data arrives on result_fd.  Reads one
 * reply ("ok\n" or "fail:<reason>\n"), updates the UI accordingly, and
 * returns G_SOURCE_REMOVE so this watch fires only once per submission.
 * on_login() registers a new watch for each retry.
 */
static gboolean on_result_ready(gint fd, GIOCondition condition, gpointer user_data)
{
    (void)condition;
    login_ctx *ctx = user_data;

    /*
     * FRAGILE: assumes the daemon's reply fits in a single read().  This
     * holds because the daemon writes "ok\n" or "fail:<reason>\n" as a
     * single write() to a pipe, and pipe writes ≤ PIPE_BUF (4096) are
     * atomic.  If messages ever exceed PIPE_BUF, this needs a read loop.
     */
    char    buf[256] = {0};
    ssize_t n        = read(fd, buf, sizeof(buf) - 1);
    log_debug("result_fd ready: n=%zd buf='%.*s'", n, (int)(n > 0 ? n : 0), buf);

    if (n > 0 && strncmp(buf, "ok", 2) == 0) {
        /* Clear the password field before destroying the window. */
        gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), "");
        gtk_window_destroy(GTK_WINDOW(ctx->window));
        return G_SOURCE_REMOVE;
    }

    /* "fail:<reason>\n" — strip trailing newline, show error, allow retry. */
    const char *msg = "Authentication failed.";
    if (n > 5 && strncmp(buf, "fail:", 5) == 0) {
        buf[n - 1] = '\0';
        msg = buf + 5;
    }
    gtk_label_set_text(GTK_LABEL(ctx->error_label), msg);
    gtk_widget_set_visible(ctx->error_label, TRUE);
    gtk_spinner_stop(GTK_SPINNER(ctx->spinner));
    gtk_widget_set_visible(ctx->spinner, FALSE);
    gtk_widget_set_sensitive(ctx->username_entry, TRUE);
    gtk_widget_set_sensitive(ctx->password_entry, TRUE);
    gtk_widget_set_sensitive(ctx->button, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), "");
    gtk_widget_grab_focus(ctx->password_entry);
    return G_SOURCE_REMOVE;
}

/*
 * on_login — submit the credentials and wait for the daemon's response.
 *
 * Connected to both the "Log In" button's "clicked" signal and the password
 * entry's "activate" signal (Enter key), so either interaction submits.
 *
 * If credentials_fd is -1 (standalone run without a daemon), falls back to
 * quitting the application directly.
 */
static void on_login(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    login_ctx *ctx = user_data;

    if (ctx->credentials_fd == -1) {
        log_debug("no credentials_fd, quitting directly");
        g_application_quit(G_APPLICATION(ctx->app));
        return;
    }

    const char *u = gtk_editable_get_text(GTK_EDITABLE(ctx->username_entry));
    const char *p = gtk_editable_get_text(GTK_EDITABLE(ctx->password_entry));
    log_debug("submitting credentials for user '%s'", u);

    /*
     * MINOR: write() return values are not checked.  These are small writes
     * (username + NUL, password + NUL) to a pipe, well under PIPE_BUF, so
     * partial writes cannot occur.  If the pipe is broken (daemon crashed),
     * SIGPIPE will terminate the greeter, which is the desired behaviour
     * since the daemon will restart it.
     */
    write(ctx->credentials_fd, u, strlen(u) + 1);
    write(ctx->credentials_fd, p, strlen(p) + 1);

    /* Hide any previous error and disable all inputs to prevent interaction. */
    gtk_widget_set_visible(ctx->error_label, FALSE);
    gtk_widget_set_sensitive(ctx->username_entry, FALSE);
    gtk_widget_set_sensitive(ctx->password_entry, FALSE);
    gtk_widget_set_sensitive(ctx->button, FALSE);
    gtk_widget_set_visible(ctx->spinner, TRUE);
    gtk_spinner_start(GTK_SPINNER(ctx->spinner));

    /* Register a one-shot watcher for the daemon's response. */
    g_unix_fd_add(ctx->result_fd, G_IO_IN, on_result_ready, ctx);
}

/*
 * on_activate — build the window when the GtkApplication becomes active.
 */
static void on_activate(GtkApplication *app, gpointer user_data)
{
    activate_ctx *actx     = user_data;
    const char   *username = actx->username;

    /* Register CSS before creating any widgets. */
    GtkCssProvider *provider = gtk_css_provider_new();
    /*
     * gtk_css_provider_load_from_string was added in GTK 4.12.
     * Use the older gtk_css_provider_load_from_data on earlier versions
     * so the greeter builds on both the target (GTK 4.14) and the
     * development machine (which may have an older GTK).
     */
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, CSS);
#else
    gtk_css_provider_load_from_data(provider, CSS, -1);
#endif
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* Heading */
    GtkWidget *heading = gtk_label_new("Log in");
    gtk_widget_add_css_class(heading, "heading");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);

    /*
     * Username entry: pre-filled with the account name the daemon provided
     * via ATRIUM_USERNAME.  The user may edit it.
     */
    GtkWidget *username_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(username_entry), "Username");
    gtk_editable_set_text(GTK_EDITABLE(username_entry), username);

    /* Password entry: input is hidden. */
    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);

    /* Log In button */
    GtkWidget *button = gtk_button_new_with_label("Log In");
    gtk_widget_add_css_class(button, "suggested-action");

    /* Error label: hidden until a "fail:" response arrives from the daemon. */
    GtkWidget *error_label = gtk_label_new("");
    gtk_widget_add_css_class(error_label, "error-label");
    gtk_widget_set_halign(error_label, GTK_ALIGN_START);
    gtk_widget_set_visible(error_label, FALSE);

    /* Spinner: shown while waiting for the daemon's auth response. */
    GtkWidget *spinner = gtk_spinner_new();
    gtk_widget_add_css_class(spinner, "spinner");
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(spinner, FALSE);

    login_ctx *ctx           = g_new(login_ctx, 1);
    ctx->app                 = app;
    ctx->window              = NULL; /* filled in after window creation */
    ctx->username_entry      = username_entry;
    ctx->password_entry      = password_entry;
    ctx->button              = button;
    ctx->spinner             = spinner;
    ctx->error_label         = error_label;
    ctx->credentials_fd      = actx->credentials_fd;
    ctx->result_fd           = actx->result_fd;

    g_signal_connect(button,         "clicked",  G_CALLBACK(on_login), ctx);
    g_signal_connect(password_entry, "activate", G_CALLBACK(on_login), ctx);

    /*
     * When the user presses Enter in the username field, move focus to the
     * password field rather than submitting (the password is still empty).
     */
    g_signal_connect_swapped(username_entry, "activate",
                             G_CALLBACK(gtk_widget_grab_focus), password_entry);

    /*
     * Card: a small box centered in the fullscreen window.
     * halign/valign CENTER causes GTK to give it only its natural size and
     * position it in the middle of the window's content area.
     */
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(card), heading);
    gtk_box_append(GTK_BOX(card), username_entry);
    gtk_box_append(GTK_BOX(card), password_entry);
    gtk_box_append(GTK_BOX(card), error_label);
    gtk_box_append(GTK_BOX(card), spinner);
    gtk_box_append(GTK_BOX(card), button);

    GtkWidget *window = gtk_application_window_new(app);
    ctx->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "atrium");
    gtk_window_set_child(GTK_WINDOW(window), card);

    /*
     * Go fullscreen only when ATRIUM_FULLSCREEN=1 is set in the environment.
     * The daemon sets this variable before execing the greeter inside cage, so
     * production runs under cage are always fullscreen.  Without the variable
     * the window opens at its natural size, which is convenient for standalone
     * development and testing.
     */
    const char *fullscreen_env = getenv("ATRIUM_FULLSCREEN");
    if (fullscreen_env && fullscreen_env[0] == '1')
        gtk_window_fullscreen(GTK_WINDOW(window));
    else
        gtk_window_set_default_size(GTK_WINDOW(window), 480, 360);

    /* Place focus on the username field so the user can verify or edit it. */
    gtk_widget_grab_focus(username_entry);

    gtk_window_present(GTK_WINDOW(window));
    log_debug("window presented");
}

void greeter_run_ui(const char *username, int credentials_fd, int result_fd)
{
    log_debug("greeter_run_ui: username='%s' cfd=%d rfd=%d",
             username, credentials_fd, result_fd);
    activate_ctx actx = {
        .username       = username,
        .credentials_fd = credentials_fd,
        .result_fd      = result_fd,
    };
    GtkApplication *app = gtk_application_new("org.kavau.atrium.greeter",
                                              G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &actx);
    g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
}
