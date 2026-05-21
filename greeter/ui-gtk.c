#include "ui-gtk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib-unix.h>
#include <gtk/gtk.h>

#include "ipc.h"
#include "lib/log.h"

/* Global variables - avoid passing context around */
static ipc_channel        *g_ch;
static GtkStack           *g_stack;
static GtkLabel           *g_user_label;
static GtkEntry           *g_password_entry;
static greeter_user const *g_selected_user;
static greeter_user const *g_users;
static int                 g_num_users;

/* Callback for IPC response from the daemon */
static gboolean on_ipc_response_ready(gint fd, GIOCondition condition, gpointer user_data) {
    (void)fd;
    GtkWidget *widget = GTK_WIDGET(user_data);

    if (condition & (G_IO_ERR | G_IO_HUP)) {
        log_error("greeter: IPC fd error or hangup");
        goto err;
    }

    ipc_status status = ipc_read_result(g_ch);
    /* TODO: add separate failure states for auth/ipc failures. */
    if (status == IPC_FAIL) {
        log_info("greeter: authentication failed for user '%s'", g_selected_user->username);
        goto err;
    }

    log_info("greeter: authentication successful for user '%s'; exiting",
             g_selected_user->username);
    g_application_quit(g_application_get_default());
    return G_SOURCE_REMOVE;

err:
    /* TODO: show error message to user */
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_widget_get_root(widget)), TRUE);
    return G_SOURCE_REMOVE;
}

static void on_user_clicked(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    g_selected_user = user_data;

    char title[MAX_DISPLAY_NAME_LEN + 16];
    snprintf(title, sizeof(title), "Log in as %s", g_selected_user->username);
    gtk_label_set_text(g_user_label, title);
    gtk_editable_set_text(GTK_EDITABLE(g_password_entry), "");
    gtk_stack_set_visible_child_name(g_stack, "password");
    gtk_widget_grab_focus(GTK_WIDGET(g_password_entry));
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
    const char *password = gtk_editable_get_text(GTK_EDITABLE(g_password_entry));
    if (ipc_send_credentials(g_ch, g_selected_user->username, password) != 0)
        goto err;

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
        g_signal_connect(btn, "clicked", G_CALLBACK(on_user_clicked), (void *)&g_users[i]);
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

int run_ui(const greeter_user *users, int num_users, ipc_channel *ch) {
    g_users = users;
    g_num_users = num_users;
    g_ch = ch;

    GtkApplication *app = gtk_application_new("com.kavau.atrium", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);

    return status;
}
