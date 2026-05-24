#include "ui-gtk.h"

#include <stdio.h>
#include <string.h>

#include <glib-unix.h>
#include <gtk/gtk.h>

#include "ipc.h"
#include "lib/log.h"
#include "theme.h"

/* Global variables - avoid passing context around */
static ipc_channel        *g_ch;
static GtkStack           *g_stack;
static GtkLabel           *g_user_label;
static GtkEntry           *g_password_entry;
static greeter_user const *g_selected_user;
static greeter_user const *g_users;
static int                 g_num_users;
static GtkSpinner         *g_users_spinner;
static GtkLabel           *g_users_error_label;
static GtkSpinner         *g_password_spinner;
static GtkLabel           *g_password_error_label;

static gboolean on_users_page(void) {
    return strcmp(gtk_stack_get_visible_child_name(g_stack), "users") == 0;
}

/* Reset the current page to its idle state: hide its spinner and error label. */
static void reset_page(void) {
    if (on_users_page()) {
        gtk_spinner_stop(g_users_spinner);
        gtk_widget_set_visible(GTK_WIDGET(g_users_spinner), FALSE);
        gtk_label_set_text(g_users_error_label, "");
        gtk_widget_set_visible(GTK_WIDGET(g_users_error_label), FALSE);
    } else {
        gtk_spinner_stop(g_password_spinner);
        gtk_widget_set_visible(GTK_WIDGET(g_password_spinner), FALSE);
        gtk_label_set_text(g_password_error_label, "");
        gtk_widget_set_visible(GTK_WIDGET(g_password_error_label), FALSE);
    }
}

/* Start the spinner for the current page. */
static void start_spinner(void) {
    if (on_users_page()) {
        gtk_spinner_start(g_users_spinner);
        gtk_widget_set_visible(GTK_WIDGET(g_users_spinner), TRUE);
    } else {
        gtk_spinner_start(g_password_spinner);
        gtk_widget_set_visible(GTK_WIDGET(g_password_spinner), TRUE);
    }
}

/* Stop the spinner and show an error message on the current page. */
static void show_error(const char *message) {
    if (on_users_page()) {
        gtk_spinner_stop(g_users_spinner);
        gtk_widget_set_visible(GTK_WIDGET(g_users_spinner), FALSE);
        gtk_label_set_text(g_users_error_label, message);
        gtk_widget_set_visible(GTK_WIDGET(g_users_error_label), TRUE);
    } else {
        gtk_spinner_stop(g_password_spinner);
        gtk_widget_set_visible(GTK_WIDGET(g_password_spinner), FALSE);
        gtk_label_set_text(g_password_error_label, message);
        gtk_widget_set_visible(GTK_WIDGET(g_password_error_label), TRUE);
    }
}

/* Callback for IPC response from the daemon */
static gboolean on_ipc_response_ready(gint fd, GIOCondition condition, gpointer user_data) {
    (void)fd;
    GtkWidget *widget = GTK_WIDGET(user_data);

    if (condition & (G_IO_ERR | G_IO_HUP)) {
        log_error("greeter: IPC fd error or hangup");
        show_error(IPC_ERROR_INTERNAL);
        goto err;
    }

    char       reason[256];
    ipc_status status = ipc_read_result(g_ch, reason, sizeof(reason));
    if (status == IPC_FAIL) {
        log_info("greeter: authentication failed for user '%s': %s", g_selected_user->username,
                 reason);
        show_error(reason[0] ? reason : IPC_ERROR_INTERNAL);
        goto err;
    }

    /* Quit greeter on successful authentication - no need to stop spinner. */
    log_info("greeter: authentication successful for user '%s'; exiting",
             g_selected_user->username);
    g_application_quit(g_application_get_default());
    return G_SOURCE_REMOVE;

err:
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_widget_get_root(widget)), TRUE);
    return G_SOURCE_REMOVE;
}

/* Disable the window, send credentials, and register the IPC response watcher.
 * Re-enables the window on send failure. */
static void submit_credentials(GtkWidget *widget, const char *password) {
    reset_page();
    start_spinner();

    GtkRoot *root = gtk_widget_get_root(widget);
    gtk_widget_set_sensitive(GTK_WIDGET(root), FALSE);
    if (ipc_send_credentials(g_ch, g_selected_user->username, password) != 0) {
        show_error(IPC_ERROR_INTERNAL);
        gtk_widget_set_sensitive(GTK_WIDGET(root), TRUE);
        return;
    }
    g_unix_fd_add(ipc_get_read_fd(g_ch), G_IO_IN | G_IO_ERR | G_IO_HUP, on_ipc_response_ready,
                  widget);
    log_info("greeter: sent credentials for user '%s'; waiting for response",
             g_selected_user->username);
}

static void on_user_clicked(GtkWidget *widget, gpointer user_data) {
    g_selected_user = user_data;

    if (g_selected_user->passwordless) {
        log_info("greeter: passwordless user '%s', skipping password page",
                 g_selected_user->username);
        submit_credentials(widget, "");
        return;
    }

    char title[MAX_DISPLAY_NAME_LEN + 16];
    snprintf(title, sizeof(title), "Log in as %s", g_selected_user->display_name);
    gtk_label_set_text(g_user_label, title);
    gtk_editable_set_text(GTK_EDITABLE(g_password_entry), "");
    gtk_stack_set_visible_child_name(g_stack, "password");
    reset_page();
    gtk_widget_grab_focus(GTK_WIDGET(g_password_entry));
}

static void on_back_clicked(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
    g_selected_user = NULL;
    gtk_editable_set_text(GTK_EDITABLE(g_password_entry), "");
    gtk_stack_set_visible_child_name(g_stack, "users");
    reset_page();
}

static void on_login_clicked(GtkWidget *widget, gpointer user_data) {
    (void)user_data;

    if (!g_selected_user) {
        log_warn("greeter: login clicked but no user selected");
        return;
    }
    log_debug("greeter: login clicked for user '%s'", g_selected_user->username);

    gtk_root_set_focus(gtk_widget_get_root(widget), NULL);
    submit_credentials(widget, gtk_editable_get_text(GTK_EDITABLE(g_password_entry)));
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    theme_apply();

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "atrium");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_fullscreen(GTK_WINDOW(window));

    /* Stack consisting of user selection page and password entry page */

    GtkWidget *stack = gtk_stack_new();
    gtk_window_set_child(GTK_WINDOW(window), stack);
    g_stack = GTK_STACK(stack);

    /* User selection page */

    GtkWidget *users_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(users_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(users_box, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(users_box, "card");
    gtk_stack_add_named(g_stack, users_box, "users");

    GtkWidget *heading = gtk_label_new("Log in");
    gtk_widget_add_css_class(heading, "heading");
    gtk_box_append(GTK_BOX(users_box), heading);

    for (int i = 0; i < g_num_users; i++) {
        GtkWidget *btn = gtk_button_new_with_label(g_users[i].display_name);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_user_clicked), (void *)&g_users[i]);
        gtk_box_append(GTK_BOX(users_box), btn);
    }

    GtkWidget *users_spinner = gtk_spinner_new();
    gtk_widget_set_visible(users_spinner, FALSE);
    gtk_box_append(GTK_BOX(users_box), users_spinner);
    g_users_spinner = GTK_SPINNER(users_spinner);

    GtkWidget *users_error_label = gtk_label_new("");
    gtk_widget_add_css_class(users_error_label, "error-label");
    gtk_widget_set_visible(users_error_label, FALSE);
    gtk_box_append(GTK_BOX(users_box), users_error_label);
    g_users_error_label = GTK_LABEL(users_error_label);

    /* Password entry page */

    GtkWidget *password_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(password_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(password_box, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(password_box, "card");
    gtk_stack_add_named(g_stack, password_box, "password");

    GtkWidget *password_title = gtk_label_new("");
    gtk_box_append(GTK_BOX(password_box), password_title);
    g_user_label = GTK_LABEL(password_title);

    gtk_box_append(GTK_BOX(password_box), gtk_label_new("Password:"));

    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(password_entry), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_widget_add_css_class(password_entry, "password-entry");
    gtk_box_append(GTK_BOX(password_box), password_entry);
    g_password_entry = GTK_ENTRY(password_entry);

    GtkWidget *password_spinner = gtk_spinner_new();
    gtk_widget_set_visible(password_spinner, FALSE);
    gtk_box_append(GTK_BOX(password_box), password_spinner);
    g_password_spinner = GTK_SPINNER(password_spinner);

    GtkWidget *login_btn = gtk_button_new_with_label("Log In");
    g_signal_connect(login_btn, "clicked", G_CALLBACK(on_login_clicked), NULL);
    g_signal_connect(password_entry, "activate", G_CALLBACK(on_login_clicked), NULL);
    gtk_box_append(GTK_BOX(password_box), login_btn);

    GtkWidget *back_btn = gtk_button_new_with_label("Back");
    gtk_widget_add_css_class(back_btn, "back-button");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_clicked), NULL);
    gtk_box_append(GTK_BOX(password_box), back_btn);

    GtkWidget *password_error_label = gtk_label_new("");
    gtk_widget_add_css_class(password_error_label, "error-label");
    gtk_widget_set_visible(password_error_label, FALSE);
    gtk_box_append(GTK_BOX(password_box), password_error_label);
    g_password_error_label = GTK_LABEL(password_error_label);

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
