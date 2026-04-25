/*
 * greeter/ui-gtk4.c — GTK4 implementation of the greeter UI.
 *
 * Implements greeter_run_ui() declared in ui.h.  The entire toolkit dependency
 * is contained in this file: to swap to a different toolkit, replace this file
 * and update the meson.build source list.  Neither greeter/main.c nor the
 * daemon need any changes.
 *
 * The UI has two screens inside a centered card:
 *
 *   1. User selection — one button per eligible local user.  The user list
 *      is provided by the caller (main.c); this file has no passwd dependency.
 *      Clicking a button moves to screen 2.
 *
 *   2. Password entry — shows "Log in as <name>", password field, Log In
 *      button, and a Back link to return to screen 1.
 *
 * On submit, the credentials are written to credentials_fd as
 * "<username>\0<password>\0" and the UI waits for a response on result_fd.
 * The daemon replies with "ok\n" on success or "fail:<reason>\n" on failure;
 * the former exits cleanly, the latter shows an error label and allows retry.
 * When credentials_fd is -1 (standalone dev run), the button quits directly.
 */

#include "ui.h"
#include "config.h"
#include "log.h"

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <string.h>
#include <unistd.h>

/* SHORTCUT: check whether a username is in CONFIG_PASSWORDLESS_USERS. */
static int is_passwordless(const char *username)
{
    static const char *list[] = CONFIG_PASSWORDLESS_USERS;
    for (size_t i = 0; i < sizeof(list)/sizeof(list[0]); i++) {
        if (list[i] && strcmp(username, list[i]) == 0)
            return 1;
    }
    return 0;
}

/* ── CSS ───────────────────────────────────────────────────────────────────── */

/* CSS template for the greeter window.  Sizes are supplied at runtime
 * from CONFIG_BASE_FONT_SIZE; the nine %d placeholders correspond to:
 *   1. card padding vertical   (base * 2)
 *   2. card padding horizontal (base * 12/5)
 *   3. card min-width          (base * 18)
 *   4. heading font-size       (base * 3/2)
 *   5. entry font-size         (base)
 *   6. login button font-size  (base)
 *   7. user button font-size   (base * 6/5)
 *   8. back button font-size   (base)
 *   9. error label font-size   (base) */
static const char CSS_TEMPLATE[] =
    "window {"
    "  background-color: #1e1e2e;"
    "}"
    ".card {"
    "  background-color: #313244;"
    "  border-radius: 12px;"
    "  padding: %dpx %dpx;"
    "  min-width: %dpx;"
    "}"
    ".card .heading {"
    "  color: #cdd6f4;"
    "  font-size: %dpx;"
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
    "  font-size: %dpx;"
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
    "  font-size: %dpx;"
    "}"
    ".card button.suggested-action:hover {"
    "  background-image: none;"
    "  background-color: #b4d0ff;"
    "}"
    ".card button.suggested-action:focus {"
    "  outline: none;"
    "  box-shadow: 0 0 0 2px #cdd6f4;"
    "}"
    ".user-button {"
    "  background-image: none;"
    "  background-color: transparent;"
    "  border: 1px solid #45475a;"
    "  border-radius: 8px;"
    "  padding: 12px 20px;"
    "  margin-bottom: 8px;"
    "  color: #cdd6f4;"
    "  font-size: %dpx;"
    "}"
    ".user-button:hover {"
    "  background-image: none;"
    "  background-color: #45475a;"
    "}"
    ".user-button:focus {"
    "  outline: none;"
    "  box-shadow: 0 0 0 2px #89b4fa;"
    "}"
    ".back-button {"
    "  background: none;"
    "  border: none;"
    "  box-shadow: none;"
    "  color: #a6adc8;"
    "  font-size: %dpx;"
    "  padding: 4px 0;"
    "  margin-top: 4px;"
    "}"
    ".back-button:hover {"
    "  color: #cdd6f4;"
    "}"
    ".card .spinner {"
    "  color: #cdd6f4;"
    "  margin-bottom: 8px;"
    "}"
    ".error-label {"
    "  color: #f38ba8;"
    "  font-size: %dpx;"
    "  margin-bottom: 8px;"
    "}";

/* ── UI state ──────────────────────────────────────────────────────────────── */

/* Arguments forwarded from greeter_run_ui() to on_activate(). */
typedef struct {
    const greeter_user *users;
    int         user_count;
    int         credentials_fd;
    int         result_fd;
    const char *message;
} activate_ctx;

/* Shared state between callbacks.  Allocated with g_new0() in on_activate();
 * intentionally not freed — the process exits after g_application_quit(). */
typedef struct {
    GtkApplication *app;
    GtkWidget      *window;
    GtkWidget      *stack;
    GtkWidget      *users_box;        /* user selection page container */
    GtkWidget      *users_spinner;    /* spinner shown during user selection submit */
    GtkWidget      *users_error_label; /* error shown on users page after passwordless failure */
    GtkWidget      *login_heading;
    GtkWidget      *password_entry;
    GtkWidget      *button;
    GtkWidget      *spinner;
    GtkWidget      *error_label;
    char            selected_user[CONFIG_MAX_USERNAME_LEN];
    int             credentials_fd;
    int             result_fd;

    /* SHORTCUT: screen blanking via black overlay window (not true DPMS). */
    GtkWidget      *blank_window;     /* fullscreen black window */
    guint           idle_timeout_id;  /* g_timeout_add_seconds() source ID */
    int             blank_timeout;    /* seconds; 0 = disabled             */
} login_ctx;

/* Forward declaration for idle timer reset. */
static void reset_idle_timer(login_ctx *ctx);

/* ── Callbacks ─────────────────────────────────────────────────────────────── */

/*
 * Handle the daemon's authentication response ("ok\n" or "fail:...\n").
 * Returns G_SOURCE_REMOVE so this watch fires only once per submission;
 * on_login() registers a new watch for each retry.
 */
static gboolean on_result_ready(gint fd, GIOCondition condition,
                                gpointer user_data)
{
    (void)condition;
    login_ctx *ctx = user_data;

    /*
     * FRAGILE: assumes the reply fits in a single read().  This holds because
     * the daemon writes ≤ PIPE_BUF bytes atomically; if messages ever exceed
     * PIPE_BUF, this needs a read loop.
     */
    char    buf[256] = {0};
    ssize_t n        = read(fd, buf, sizeof(buf) - 1);
    log_debug("result_fd ready: n=%zd buf='%.*s'",
              n, (int)(n > 0 ? n : 0), buf);

    if (n >= 3 && memcmp(buf, "ok\n", 3) == 0) {
        /* Clear the password field before destroying the window. */
        gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), "");

        /* Stop the idle timer to prevent blank window from appearing during shutdown. */
        if (ctx->idle_timeout_id) {
            g_source_remove(ctx->idle_timeout_id);
            ctx->idle_timeout_id = 0;
        }

        /* Destroy both windows to trigger application shutdown. Order matters:
         * destroy blank_window first to avoid leaving it visible during teardown. */
        if (ctx->blank_window)
            gtk_window_destroy(GTK_WINDOW(ctx->blank_window));
        gtk_window_destroy(GTK_WINDOW(ctx->window));
        return G_SOURCE_REMOVE;
    }

    /* "fail:<reason>\n" — strip trailing newline, show error, allow retry. */
    const char *msg = "Authentication failed.";
    if (n >= 6 && strncmp(buf, "fail:", 5) == 0) {
        buf[n - 1] = '\0';
        msg = buf + 5;
    }

    const char *page = gtk_stack_get_visible_child_name(GTK_STACK(ctx->stack));
    if (page && strcmp(page, "users") == 0) {
        /* Passwordless path failed — reset users page. */
        gtk_spinner_stop(GTK_SPINNER(ctx->users_spinner));
        gtk_widget_set_visible(ctx->users_spinner, FALSE);
        GtkWidget *child = gtk_widget_get_first_child(ctx->users_box);
        while (child) {
            if (GTK_IS_BUTTON(child))
                gtk_widget_set_sensitive(child, TRUE);
            child = gtk_widget_get_next_sibling(child);
        }
        gtk_label_set_text(GTK_LABEL(ctx->users_error_label), msg);
        gtk_widget_set_visible(ctx->users_error_label, TRUE);
    } else {
        /* Password path failed — reset login page. */
        gtk_label_set_text(GTK_LABEL(ctx->error_label), msg);
        gtk_widget_set_visible(ctx->error_label, TRUE);
        gtk_spinner_stop(GTK_SPINNER(ctx->spinner));
        gtk_widget_set_visible(ctx->spinner, FALSE);
        gtk_widget_set_sensitive(ctx->password_entry, TRUE);
        gtk_widget_set_sensitive(ctx->button, TRUE);
        gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), "");
        gtk_widget_grab_focus(ctx->password_entry);
    }
    return G_SOURCE_REMOVE;
}

/*
 * Submit credentials and wait for the daemon's response.
 * Connected to both the "Log In" button's "clicked" signal and the password
 * entry's "activate" signal (Enter key), so either interaction submits.
 * When credentials_fd is -1 (standalone run without a daemon), falls back
 * to quitting the application directly.
 */
static void on_login(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    login_ctx *ctx = user_data;

    /* Reset idle timer on user interaction. */
    reset_idle_timer(ctx);

    if (ctx->credentials_fd == -1) {
        log_debug("no credentials_fd, quitting directly");
        g_application_quit(G_APPLICATION(ctx->app));
        return;
    }

    const char *u = ctx->selected_user;
    const char *p = gtk_editable_get_text(GTK_EDITABLE(ctx->password_entry));
    log_debug("submitting credentials for user '%s'", u);

    /* Validate password length before submission. */
    size_t plen = strlen(p);
    if (plen >= CONFIG_MAX_PASSWORD_LEN) {
        log_error("password too long (%zu bytes, max %d)", plen, CONFIG_MAX_PASSWORD_LEN - 1);
        gtk_label_set_text(GTK_LABEL(ctx->error_label), "Password too long");
        gtk_widget_set_visible(ctx->error_label, TRUE);
        return;
    }

    /* Combine username and password into a single buffer for atomic write.
     * Wire format: "<username>\0<password>\0"
     * Single write() under PIPE_BUF (4096 bytes) is guaranteed atomic by POSIX,
     * preventing the credential-read race where poll() wakes between two writes. */
    char credentials[CONFIG_MAX_USERNAME_LEN + CONFIG_MAX_PASSWORD_LEN];
    size_t ulen = strlen(u);
    memcpy(credentials, u, ulen + 1);           /* username + NUL */
    memcpy(credentials + ulen + 1, p, plen + 1); /* password + NUL */
    size_t total_len = ulen + 1 + plen + 1;

    /*
     * MINOR: write() return value is not checked.  This write is well under
     * PIPE_BUF (4096 bytes), so partial writes cannot occur.  If the pipe is
     * broken (daemon crashed), SIGPIPE will terminate the greeter, which is
     * the desired behaviour since the daemon will restart it.
     */
    write(ctx->credentials_fd, credentials, total_len);

    /* Hide any previous error and disable inputs while waiting. */
    gtk_widget_set_visible(ctx->error_label, FALSE);
    gtk_widget_set_sensitive(ctx->password_entry, FALSE);
    gtk_widget_set_sensitive(ctx->button, FALSE);
    gtk_widget_set_visible(ctx->spinner, TRUE);
    gtk_spinner_start(GTK_SPINNER(ctx->spinner));

    /* Register a one-shot watcher for the daemon's response. */
    g_unix_fd_add(ctx->result_fd, G_IO_IN, on_result_ready, ctx);
}

/* User button clicked — switch to the password screen. */
static void on_user_selected(GtkWidget *widget, gpointer user_data)
{
    login_ctx  *ctx      = user_data;
    const char *username = g_object_get_data(G_OBJECT(widget), "username");
    const char *display  = g_object_get_data(G_OBJECT(widget), "display");

    /* Reset idle timer on user interaction. */
    reset_idle_timer(ctx);

    snprintf(ctx->selected_user, sizeof(ctx->selected_user), "%s", username);
    log_debug("selected user '%s' (%s)", username, display);

    /* SHORTCUT: passwordless users skip the password screen entirely.
     * Send empty credentials and wait for the daemon on the users page,
     * showing a spinner to indicate the request is in flight. */
    if (ctx->credentials_fd != -1 && is_passwordless(username)) {
        log_debug("passwordless login for '%s'", username);
        /* Disable buttons and show spinner while waiting for auth result.
         * Hide any error from a previous failed attempt. */
        gtk_widget_set_visible(ctx->users_error_label, FALSE);
        GtkWidget *child = gtk_widget_get_first_child(ctx->users_box);
        while (child) {
            if (GTK_IS_BUTTON(child))
                gtk_widget_set_sensitive(child, FALSE);
            child = gtk_widget_get_next_sibling(child);
        }
        gtk_widget_set_visible(ctx->users_spinner, TRUE);
        gtk_spinner_start(GTK_SPINNER(ctx->users_spinner));

        /* Combine username and empty password into a single buffer for atomic write.
         * Wire format: "<username>\0<password>\0" where password is empty.
         * Single write() under PIPE_BUF (4096 bytes) prevents credential-read race. */
        char credentials[CONFIG_MAX_USERNAME_LEN + CONFIG_MAX_PASSWORD_LEN];
        size_t ulen = strlen(username);
        memcpy(credentials, username, ulen + 1);  /* username + NUL */
        credentials[ulen + 1] = '\0';              /* empty password NUL */
        size_t total_len = ulen + 1 + 1;

        write(ctx->credentials_fd, credentials, total_len);
        g_unix_fd_add(ctx->result_fd, G_IO_IN, on_result_ready, ctx);
        return;
    }

    /* Password user — switch directly to the login page, no spinner needed. */
    char heading[256];
    snprintf(heading, sizeof(heading), "Log in as %s", display);
    gtk_label_set_text(GTK_LABEL(ctx->login_heading), heading);

    gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), "");
    gtk_widget_set_visible(ctx->error_label, FALSE);
    gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "login");
    gtk_widget_grab_focus(ctx->password_entry);
}

/* Back button — return to the user selection screen. */
static void on_back(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    login_ctx *ctx = user_data;

    /* Reset idle timer on user interaction. */
    reset_idle_timer(ctx);

    /* Reset login page state. */
    gtk_editable_set_text(GTK_EDITABLE(ctx->password_entry), "");
    gtk_widget_set_visible(ctx->error_label, FALSE);
    gtk_widget_set_sensitive(ctx->password_entry, TRUE);
    gtk_widget_set_sensitive(ctx->button, TRUE);
    ctx->selected_user[0] = '\0';

    /* Clear any error from a previous passwordless failure on the users page. */
    gtk_widget_set_visible(ctx->users_error_label, FALSE);

    gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "users");
}

/* ── Screen blanking (SHORTCUT) ────────────────────────────────────────────── */

/*
 * SHORTCUT: idle screen blanking via black overlay window.
 *
 * After ctx->blank_timeout seconds of idle time, show a fullscreen black
 * window over the greeter.  Hide it on any input (keyboard/mouse) event.
 *
 * This does NOT power down the display — LCD backlights stay on, saving only
 * burn-in, not power.  True DPMS blanking would require daemon-side DRM
 * control (drmModeConnectorSetProperty DPMS=Off), which is significantly more
 * complex and fragile (needs per-seat connector discovery, privilege handling,
 * coordination with cage's DRM master).
 */

/* Idle timeout callback — show the black overlay window. */
static gboolean on_idle_timeout(gpointer user_data)
{
    login_ctx *ctx = user_data;
    if (ctx->blank_window) {
        gtk_window_present(GTK_WINDOW(ctx->blank_window));
        log_debug("screen blanked after %d seconds idle", ctx->blank_timeout);
    }
    ctx->idle_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

/* Reset the idle timer — cancel existing timer and start a new one. */
static void reset_idle_timer(login_ctx *ctx)
{
    if (ctx->blank_timeout == 0)
        return;

    if (ctx->idle_timeout_id) {
        g_source_remove(ctx->idle_timeout_id);
        ctx->idle_timeout_id = 0;
    }

    ctx->idle_timeout_id = g_timeout_add_seconds(ctx->blank_timeout,
                                                   on_idle_timeout, ctx);
}

/* Key press handler — hide blank window if visible, reset idle timer. */
static gboolean on_key_press(GtkEventControllerKey *controller,
                               guint keyval, guint keycode,
                               GdkModifierType state, gpointer user_data)
{
    (void)controller;
    (void)keyval;
    (void)keycode;
    (void)state;
    login_ctx *ctx = user_data;

    if (ctx->blank_window && gtk_widget_get_visible(ctx->blank_window)) {
        gtk_widget_set_visible(ctx->blank_window, FALSE);
        log_debug("screen unblanked on key press");
    }

    reset_idle_timer(ctx);
    /* Return FALSE to propagate the event to the focused widget (e.g. the
     * password entry). Returning TRUE would swallow the keystroke. */
    return FALSE;
}

/* Motion handler — hide blank window if visible, reset idle timer. */
static void on_motion(GtkEventControllerMotion *controller,
                       gdouble x, gdouble y, gpointer user_data)
{
    (void)controller;
    (void)x;
    (void)y;
    login_ctx *ctx = user_data;

    if (ctx->blank_window && gtk_widget_get_visible(ctx->blank_window)) {
        gtk_widget_set_visible(ctx->blank_window, FALSE);
        log_debug("screen unblanked on motion");
    }

    reset_idle_timer(ctx);
}

/* GtkEditable::changed wrapper — any keystroke in the password entry counts as
 * user activity and resets the idle timer. */
static void on_keystroke(GtkEditable *editable, gpointer user_data)
{
    (void)editable;
    reset_idle_timer((login_ctx *)user_data);
}

/* ── Window construction ───────────────────────────────────────────────────── */

static void on_activate(GtkApplication *app, gpointer user_data)
{
    activate_ctx *actx = user_data;

    /* Set cursor theme and size — atriumdm has no home dir so there is no
     * user-level GTK config to inherit these from.  Both must be set: GTK4
     * under cage only takes ownership of cursor rendering when a valid theme
     * name is provided; the size has no effect without it. */
    g_object_set(gtk_settings_get_default(),
                 "gtk-cursor-theme-name", CONFIG_CURSOR_THEME,
                 "gtk-cursor-theme-size", CONFIG_CURSOR_SIZE,
                 NULL);

    /* Build CSS from template, filling in sizes derived from
     * CONFIG_BASE_FONT_SIZE.  Placeholders in order: card padding vertical,
     * card padding horizontal, card min-width, heading, entry, login button,
     * user button, back button, error label. */
    int fs = CONFIG_BASE_FONT_SIZE;
    char *css = g_strdup_printf(CSS_TEMPLATE,
                                fs * 2,       /* card padding vertical */
                                fs * 12 / 5,  /* card padding horizontal */
                                fs * 18,      /* card min-width */
                                fs * 3 / 2,   /* heading */
                                fs,           /* entry */
                                fs,           /* login button */
                                fs * 6 / 5,   /* user button */
                                fs,           /* back button */
                                fs);          /* error label */

    /* Register CSS before creating any widgets. */
    GtkCssProvider *provider = gtk_css_provider_new();
    /*
     * gtk_css_provider_load_from_string was added in GTK 4.12.
     * Use the older gtk_css_provider_load_from_data on earlier versions
     * so the greeter builds on both the target and the dev machine.
     */
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
#endif
    g_free(css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    const greeter_user *users = actx->users;
    int user_count = actx->user_count;

    login_ctx *ctx    = g_new0(login_ctx, 1);
    ctx->app          = app;
    ctx->credentials_fd = actx->credentials_fd;
    ctx->result_fd      = actx->result_fd;
    const char *bt_env = getenv("ATRIUM_BLANK_TIMEOUT");
    ctx->blank_timeout = bt_env ? atoi(bt_env) : 300;

    /* ── Users page ──────────────────────────────────────────────────────── */

    GtkWidget *users_heading = gtk_label_new("Log in");
    gtk_widget_add_css_class(users_heading, "heading");
    gtk_widget_set_halign(users_heading, GTK_ALIGN_START);

    GtkWidget *users_spinner = gtk_spinner_new();
    gtk_widget_add_css_class(users_spinner, "spinner");
    gtk_widget_set_halign(users_spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(users_spinner, FALSE);
    ctx->users_spinner = users_spinner;

    GtkWidget *users_error_label = gtk_label_new(actx->message ? actx->message : "");
    gtk_widget_add_css_class(users_error_label, "error-label");
    gtk_widget_set_halign(users_error_label, GTK_ALIGN_START);
    gtk_widget_set_visible(users_error_label, actx->message != NULL);
    ctx->users_error_label = users_error_label;

    GtkWidget *users_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    ctx->users_box = users_box;
    gtk_box_append(GTK_BOX(users_box), users_heading);
    gtk_box_append(GTK_BOX(users_box), users_error_label);
    gtk_box_append(GTK_BOX(users_box), users_spinner);

    for (int i = 0; i < user_count; i++) {
        GtkWidget *btn = gtk_button_new_with_label(users[i].display);
        gtk_widget_add_css_class(btn, "user-button");
        g_object_set_data_full(G_OBJECT(btn), "username",
                               g_strdup(users[i].name), g_free);
        g_object_set_data_full(G_OBJECT(btn), "display",
                               g_strdup(users[i].display), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_user_selected), ctx);
        gtk_box_append(GTK_BOX(users_box), btn);
    }

    if (user_count == 0) {
        GtkWidget *empty = gtk_label_new("No login users found.");
        gtk_widget_set_halign(empty, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(users_box), empty);
    }

    /* ── Login page ──────────────────────────────────────────────────────── */

    GtkWidget *login_heading = gtk_label_new("Log in");
    gtk_widget_add_css_class(login_heading, "heading");
    gtk_widget_set_halign(login_heading, GTK_ALIGN_START);
    ctx->login_heading = login_heading;

    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_entry_set_max_length(GTK_ENTRY(password_entry), CONFIG_MAX_PASSWORD_LEN - 1);
    ctx->password_entry = password_entry;

    GtkWidget *button = gtk_button_new_with_label("Log In");
    gtk_widget_add_css_class(button, "suggested-action");
    ctx->button = button;

    GtkWidget *error_label = gtk_label_new("");
    gtk_widget_add_css_class(error_label, "error-label");
    gtk_widget_set_halign(error_label, GTK_ALIGN_START);
    gtk_widget_set_visible(error_label, FALSE);
    ctx->error_label = error_label;

    GtkWidget *spinner = gtk_spinner_new();
    gtk_widget_add_css_class(spinner, "spinner");
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(spinner, FALSE);
    ctx->spinner = spinner;

    GtkWidget *back_button = gtk_button_new_with_label("\xe2\x86\x90 Back");
    gtk_widget_add_css_class(back_button, "back-button");
    g_signal_connect(back_button, "clicked", G_CALLBACK(on_back), ctx);

    g_signal_connect(button,         "clicked",  G_CALLBACK(on_login), ctx);
    g_signal_connect(password_entry, "activate", G_CALLBACK(on_login), ctx);
    /* Any keystroke in the password field counts as activity — reset the
     * idle timer so the screen doesn't blank while the user is typing. */
    g_signal_connect(password_entry, "changed",  G_CALLBACK(on_keystroke), ctx);

    GtkWidget *login_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(login_box), login_heading);
    gtk_box_append(GTK_BOX(login_box), password_entry);
    gtk_box_append(GTK_BOX(login_box), error_label);
    gtk_box_append(GTK_BOX(login_box), spinner);
    gtk_box_append(GTK_BOX(login_box), button);
    gtk_box_append(GTK_BOX(login_box), back_button);

    /* ── Stack (users → login) ───────────────────────────────────────────── */

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack),
                                 GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_add_named(GTK_STACK(stack), users_box, "users");
    gtk_stack_add_named(GTK_STACK(stack), login_box, "login");
    ctx->stack = stack;

    /* ── Card + window ───────────────────────────────────────────────────── */

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
    /* KNOWN ISSUE: after navigating user → login → back, the left/right borders
     * of the user buttons appear clipped.  The card shrinks to the login page's
     * natural width while that page is shown; on return the button borders
     * overflow the card and are clipped by its border-radius.  Several layout
     * workarounds were attempted (hhomogeneous, queue_resize, size_request,
     * CSS min-width) without success.  Accept for now; revisit when the greeter
     * UI is redesigned. */
    gtk_box_append(GTK_BOX(card), stack);

    GtkWidget *window = gtk_application_window_new(app);
    ctx->window = window;
    gtk_window_set_title(GTK_WINDOW(window), "atrium");
    gtk_window_set_child(GTK_WINDOW(window), card);

    /*
     * Go fullscreen when ATRIUM_FULLSCREEN=1 is set.  The daemon sets this
     * before exec inside cage, so production runs are always fullscreen.
     * Without it the window opens at natural size — convenient for dev.
     */
    const char *fullscreen_env = getenv("ATRIUM_FULLSCREEN");
    if (fullscreen_env && fullscreen_env[0] == '1')
        gtk_window_fullscreen(GTK_WINDOW(window));
    else
        gtk_window_set_default_size(GTK_WINDOW(window), 480, 360);

    gtk_window_present(GTK_WINDOW(window));
    log_debug("window presented with %d users", user_count);

    /* ── Screen blanking setup (SHORTCUT) ────────────────────────────────── */

    if (ctx->blank_timeout > 0) {
        /* Create a separate fullscreen black window for blanking.
         * Use GtkWindow (not GtkApplicationWindow) to avoid lifecycle issues
         * when destroying multiple application windows during shutdown. */
        GtkWidget *blank_window = gtk_window_new();
        ctx->blank_window = blank_window;
        gtk_window_set_title(GTK_WINDOW(blank_window), "atrium-blank");

        /* Black background via CSS.  Scope the rule to the "atrium-blank"
         * CSS class so it does not bleed onto the main greeter window. */
        gtk_widget_add_css_class(blank_window, "atrium-blank");
        GtkCssProvider *black_css = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
        gtk_css_provider_load_from_string(black_css,
            ".atrium-blank { background-color: #000000; }");
#else
        gtk_css_provider_load_from_data(black_css,
            ".atrium-blank { background-color: #000000; }", -1);
#endif
        gtk_style_context_add_provider_for_display(
            gtk_widget_get_display(blank_window),
            GTK_STYLE_PROVIDER(black_css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(black_css);

        /* Fullscreen but hidden — don't present() until timeout fires. */
        gtk_window_fullscreen(GTK_WINDOW(blank_window));
        gtk_widget_set_visible(blank_window, FALSE);
        
        /* Hide the cursor on the blank screen by setting it to "none". */
        gtk_widget_set_cursor_from_name(blank_window, "none");

        /* Event controllers ONLY on blank window to dismiss it. */
        GtkEventController *blank_key = gtk_event_controller_key_new();
        g_signal_connect(blank_key, "key-pressed",
                         G_CALLBACK(on_key_press), ctx);
        gtk_widget_add_controller(blank_window, blank_key);

        GtkEventController *blank_motion = gtk_event_controller_motion_new();
        g_signal_connect(blank_motion, "motion",
                         G_CALLBACK(on_motion), ctx);
        gtk_widget_add_controller(blank_window, blank_motion);

        /* Start the idle timer. */
        reset_idle_timer(ctx);
        log_debug("screen blanking enabled: %d second timeout",
                  ctx->blank_timeout);
    }
}

void greeter_run_ui(const greeter_user *users, int user_count,
                    int credentials_fd, int result_fd, const char *message)
{
    log_debug("greeter_run_ui: %d users, cfd=%d rfd=%d",
              user_count, credentials_fd, result_fd);

    activate_ctx actx = {
        .users          = users,
        .user_count     = user_count,
        .credentials_fd = credentials_fd,
        .result_fd      = result_fd,
        .message        = message,
    };
    GtkApplication *app = gtk_application_new("org.kavau.atrium.greeter",
                                              G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &actx);
    g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
}
