#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib-unix.h>
#include <gtk/gtk.h>

#include "lib/ipc.h"
#include "lib/log.h"

#define MAX_NUM_USERS    100
#define MAX_USERNAME_LEN 256

typedef struct {
    char username[MAX_USERNAME_LEN];
} user_entry;

/* Global variables - avoid passing context around */
static ipc_channel *g_ch;
static GtkStack    *g_stack;
static GtkLabel    *g_user_label;
static GtkEntry    *g_password_entry;
static user_entry  *g_selected_user;
static user_entry   g_users[MAX_NUM_USERS];
static int          g_num_users;

static int enumerate_users(user_entry *users, int max) {
    int            count = 0;
    struct passwd *pw;

    setpwent();
    while ((pw = getpwent()) != NULL && count < max) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 65534)
            continue;
        if (pw->pw_shell == NULL || pw->pw_shell[0] == '\0' || strstr(pw->pw_shell, "nologin") ||
            strcmp(pw->pw_shell, "/bin/false") == 0) {
            continue;
        }
        snprintf(users[count].username, sizeof(users[count].username), "%s", pw->pw_name);
        count++;
    }
    endpwent();

#ifdef ATRIUM_DEBUG
    /* For testing only: add more users */
    snprintf(users[count++].username, sizeof(users[0].username), "Alice");
    snprintf(users[count++].username, sizeof(users[0].username), "Bob");
#endif

    if (count == max)
        log_warn("user list truncated at %d entries", max);

    return count;
}

/* Callback for IPC response from the daemon */
static gboolean on_ipc_response_ready(gint fd, GIOCondition condition, gpointer user_data) {
    (void)fd;
    GtkWidget *widget = GTK_WIDGET(user_data);

    if (condition & (G_IO_ERR | G_IO_HUP)) {
        log_error("greeter: IPC fd error or hangup");
        goto err;
    }

    char    buf[512];
    ssize_t n = ipc_recv(g_ch, buf, sizeof(buf) - 1);
    if (n <= 0) {
        log_error("greeter: failed to receive response from daemon");
        goto err;
    }

    buf[n] = '\0';
    log_debug("greeter: received response: %s", buf);
    if (strcmp(buf, "ok\n") == 0) {
        log_info("greeter: authentication successful for user '%s'; exiting",
                 g_selected_user->username);
        g_application_quit(g_application_get_default());
        return G_SOURCE_REMOVE;
    }

    log_info("greeter: authentication failed for user '%s': %s", g_selected_user->username, buf);

err:
    /* TODO: show error message to user */
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_widget_get_root(widget)), TRUE);
    return G_SOURCE_REMOVE;
}

static void on_user_clicked(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    g_selected_user = user_data;

    char title[MAX_USERNAME_LEN + 16];
    snprintf(title, sizeof(title), "Log in as %s", g_selected_user->username);
    gtk_label_set_text(g_user_label, title);
    gtk_editable_set_text(GTK_EDITABLE(g_password_entry), "");
    gtk_stack_set_visible_child_name(g_stack, "password");
    gtk_widget_grab_focus(GTK_WIDGET(g_password_entry));
}

/* Construct credentials string for daemon: "<username>\0<password>\0" */
static int build_credentials_str(char *buf, size_t buflen, const char *username,
                                 const char *password) {
    size_t ulen = strlen(username) + 1; /* include the \0 */
    size_t plen = strlen(password) + 1;
    if (ulen + plen > buflen) {
        log_error("greeter: credentials too long to send");
        return -1;
    }
    memcpy(buf, username, ulen);
    memcpy(buf + ulen, password, plen);
    return ulen + plen;
}

static void on_login_clicked(GtkWidget *widget, gpointer user_data) {
    (void)user_data;

    if (!g_selected_user) {
        log_warn("greeter: login clicked but no user selected");
        return;
    }
    log_debug("greeter: login clicked for user '%s'", g_selected_user->username);

    GtkRoot *root = gtk_widget_get_root(widget);
    gtk_root_set_focus(root, NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(root), FALSE);

    /* Send credentials to daemon */
    char        buf[512];
    const char *password = gtk_editable_get_text(GTK_EDITABLE(g_password_entry));
    int credlen = build_credentials_str(buf, sizeof(buf), g_selected_user->username, password);
    if (credlen < 0)
        goto err;
    if (ipc_send(g_ch, buf, credlen) < 0) {
        log_syserr("greeter: failed to send credentials");
        goto err;
    }

    /* Activate fd watcher for ipc channel */
    g_unix_fd_add(ipc_get_read_fd(g_ch), G_IO_IN | G_IO_ERR | G_IO_HUP, on_ipc_response_ready,
                  widget);
    log_info("greeter: sent credentials for user '%s'; waiting for response",
             g_selected_user->username);
    return;

err:
    /* TODO: show error message to user */
    gtk_widget_set_sensitive(GTK_WIDGET(root), TRUE);
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "atrium");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    gtk_widget_set_size_request(window, 400, 300); /* hard minimum */
    /* gtk_window_fullscreen(GTK_WINDOW(window)); */
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

    /* Stack consisting of user selection page and password entry page */

    GtkWidget *stack = gtk_stack_new();
    gtk_window_set_child(GTK_WINDOW(window), stack);
    g_stack = GTK_STACK(stack);

    /* User selection page */

    GtkWidget *users_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(users_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(users_box, GTK_ALIGN_CENTER);
    gtk_stack_add_named(g_stack, users_box, "users");

    for (int i = 0; i < g_num_users; i++) {
        GtkWidget *btn = gtk_button_new_with_label(g_users[i].username);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_user_clicked), &g_users[i]);
        gtk_box_append(GTK_BOX(users_box), btn);
    }

    /* Password entry page */

    GtkWidget *password_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(password_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(password_box, GTK_ALIGN_CENTER);
    gtk_stack_add_named(g_stack, password_box, "password");

    GtkWidget *password_title = gtk_label_new("");
    gtk_box_append(GTK_BOX(password_box), password_title);
    g_user_label = GTK_LABEL(password_title);

    gtk_box_append(GTK_BOX(password_box), gtk_label_new("Password:"));

    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(password_entry), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_box_append(GTK_BOX(password_box), password_entry);
    g_password_entry = GTK_ENTRY(password_entry);

    GtkWidget *login_btn = gtk_button_new_with_label("Log In");
    g_signal_connect(login_btn, "clicked", G_CALLBACK(on_login_clicked), NULL);
    g_signal_connect(password_entry, "activate", G_CALLBACK(on_login_clicked), NULL);
    gtk_box_append(GTK_BOX(password_box), login_btn);

    /* Show the window */

    gtk_stack_set_visible_child_name(GTK_STACK(stack), "users");
    gtk_window_present(GTK_WINDOW(window));
}

int main(void) {
    g_num_users = enumerate_users(g_users, MAX_NUM_USERS);

    if (ipc_create_from_env(&g_ch) != 0) {
        log_error("failed to create IPC channel from environment");
        return EXIT_FAILURE;
    }

    GtkApplication *app = gtk_application_new("com.kavau.atrium", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);

    log_info("greeter exiting with status %d", status);
    ipc_close(g_ch);
    return status;
}
