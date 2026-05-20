#include <pwd.h>
#include <string.h>

#include <gtk/gtk.h>

#include "lib/ipc.h"
#include "lib/log.h"

#define MAX_NUM_USERS 100
#define MAX_USERNAME_LEN 256

typedef struct {
    char username[MAX_USERNAME_LEN];
} user_entry;

typedef struct {
    user_entry *users;
    int num_users;
    ipc_channel *ch;
} activate_ctx;

typedef struct {
    user_entry *user;
    ipc_channel *ch;
} login_ctx;

static int enumerate_users(user_entry *users, int max) {
    int count = 0;
    struct passwd *pw;

    setpwent();
    while ((pw = getpwent()) != NULL && count < max) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 65534)
            continue;
        if (pw->pw_shell) {
            if (strstr(pw->pw_shell, "nologin") || strcmp(pw->pw_shell, "/bin/false") == 0)
                continue;
        }
        snprintf(users[count].username, sizeof(users[count].username), "%s", pw->pw_name);
        count++;
    }
    endpwent();

    if (count == max)
        log_warn("user list truncated at %d entries", max);

    return count;
}

static void free_login_ctx(gpointer data, GClosure *closure) {
    (void)closure;
    g_free(data);
}

static void on_user_selected(GtkWidget *widget, gpointer user_data) {
    login_ctx *lctx = user_data;

    log_debug("greeter: user '%s' selected", lctx->user->username);
    const char password[] = "password"; /* TODO: prompt for password */

    gtk_widget_set_sensitive(GTK_WIDGET(gtk_widget_get_root(widget)), FALSE);

    /* Send credentials to daemon */
    char buf[512];
    size_t ulen = strlen(lctx->user->username) + 1; /* include the \0 */
    size_t plen = strlen(password) + 1;
    memcpy(buf, lctx->user->username, ulen);
    memcpy(buf + ulen, password, plen);
    if (ipc_send(lctx->ch, buf, ulen + plen) < 0) {
        log_syserr("greeter: failed to send credentials");
        gtk_widget_set_sensitive(GTK_WIDGET(gtk_widget_get_root(widget)), TRUE);
        return; /* TODO: show error message */
    }
    log_info("greeter: sent credentials for user '%s'; waiting for response", lctx->user->username);

    /* Block waiting for auth result */
    ssize_t n = ipc_recv(lctx->ch, buf, sizeof(buf) - 1);
    if (n <= 0) {
        log_error("greeter: failed to receive response from daemon");
        return; /* TODO: show error message */
    }

    buf[n] = '\0';
    log_debug("greeter: received response: %s", buf);
    if (strcmp(buf, "ok\n") == 0) {
        log_info("greeter: authentication successful for user '%s'", lctx->user->username);
        g_application_quit(g_application_get_default()); /* SHORTCUT */
        return;
    }

    log_info("greeter: authentication failed for user '%s': %s", lctx->user->username, buf);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_widget_get_root(widget)), TRUE);
}

static void activate(GtkApplication *app, gpointer user_data) {
    activate_ctx *actx = user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "atrium");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    gtk_widget_set_size_request(window, 400, 300); /* hard minimum */
    /* gtk_window_fullscreen(GTK_WINDOW(window)); */
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

    GtkWidget *users_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(users_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(users_box, GTK_ALIGN_CENTER);
    gtk_window_set_child(GTK_WINDOW(window), users_box);

    for (int i = 0; i < actx->num_users; i++) {
        GtkWidget *btn = gtk_button_new_with_label(actx->users[i].username);

        login_ctx *lctx = g_new(login_ctx, 1);
        lctx->user = &actx->users[i];
        lctx->ch = actx->ch;
        g_signal_connect_data(btn, "clicked", G_CALLBACK(on_user_selected), lctx, free_login_ctx,
                              0);

        gtk_box_append(GTK_BOX(users_box), btn);
    }

    gtk_window_present(GTK_WINDOW(window));
}

static int run_ui(int num_users, user_entry *users, ipc_channel *ch) {
    activate_ctx actx = {.num_users = num_users, .users = users, .ch = ch};

    GtkApplication *app = gtk_application_new("com.kavau.atrium", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &actx);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    return status;
}

int main(void) {
    user_entry users[MAX_NUM_USERS];
    int num_users = enumerate_users(users, MAX_NUM_USERS);
    ipc_channel *ch;
    if (ipc_create_from_env(&ch) != 0) {
        log_error("failed to create IPC channel from environment");
        return EXIT_FAILURE;
    }

    int status = run_ui(num_users, users, ch);

    log_info("greeter exiting with status %d", status);
    ipc_close(ch);
    return status;
}