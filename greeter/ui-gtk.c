#include "ui-gtk.h"

#include <stdio.h>
#include <string.h>

#include <glib-unix.h>
#include <gtk/gtk.h>

#include "config.h"
#include "defs.h"
#include "ipc.h"
#include "lib/log.h"
#include "theme.h"

/* Layout constants */
#define CARD_SPACING_USERS     16           /* vertical gap between items on the users page */
#define CARD_SPACING_PASSWORD  20           /* vertical gap between items on the password page */
#define USER_BUTTONS_SPACING   8            /* vertical gap between user buttons */
#define ACTION_BUTTONS_SPACING 4            /* vertical gap between login and back buttons */
#define BLANK_MOTION_GUARD_US  (200 * 1000) /* ignore motion events for 200 ms after blanking */

/* The different pages in the UI stack. */
typedef enum {
    PAGE_USERS,
    PAGE_PASSWORD,
} greeter_page;

/* Global variables - avoid passing context around */
static ipc_channel           *g_ch;
static GtkFixed              *g_root;
static GtkStack              *g_stack;
static GtkWindow             *g_window;
static GtkLabel              *g_user_label;
static GtkEntry              *g_password_entry;
static greeter_user const    *g_selected_user;
static greeter_user const    *g_users;
static int                    g_num_users;
static greeter_session const *g_sessions;
static int                    g_num_sessions;
static GtkDropDown           *g_session_dropdown; /* NULL when hidden (<= 1 session) */
static GtkSpinner            *g_users_spinner;
static GtkLabel              *g_users_error_label;
static GtkSpinner            *g_password_spinner;
static GtkLabel              *g_password_error_label;
static GtkWidget             *g_blank_screen;
static guint                  g_blank_timer_id;
static gint64                 g_blank_start_us; /* monotonic time when blanking started */
static guint                  g_auth_fd_source_id;
static guint                  g_auth_timeout_id;

static greeter_page current_page(void) {
    const char *name = gtk_stack_get_visible_child_name(g_stack);
    return strcmp(name, "password") == 0 ? PAGE_PASSWORD : PAGE_USERS;
}

static void switch_page(greeter_page page) {
    const char *name = page == PAGE_PASSWORD ? "password" : "users";
    gtk_stack_set_visible_child_name(g_stack, name);
}

/* Reset the current page to its idle state: hide its spinner and error label. */
static void reset_page(void) {
    if (current_page() == PAGE_USERS) {
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
    if (current_page() == PAGE_USERS) {
        gtk_spinner_start(g_users_spinner);
        gtk_widget_set_visible(GTK_WIDGET(g_users_spinner), TRUE);
    } else {
        gtk_spinner_start(g_password_spinner);
        gtk_widget_set_visible(GTK_WIDGET(g_password_spinner), TRUE);
    }
}

/* Stop the spinner and show an error message on the current page. */
static void show_error(const char *message) {
    if (current_page() == PAGE_USERS) {
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

/* GTK fires a synthetic motion event immediately after blanking, in order to
re-evaluate which widget is under the cursor. Therefore we ignore motion events
within this window after blanking starts. */
static gboolean on_blank_timeout(gpointer user_data) {
    (void)user_data;
    g_blank_timer_id = 0;
    g_blank_start_us = g_get_monotonic_time();
    gtk_root_set_focus(GTK_ROOT(g_window), NULL);
    gtk_widget_set_visible(g_blank_screen, TRUE);
    GdkCursor *none_cursor = gdk_cursor_new_from_name("none", NULL);
    gdk_surface_set_cursor(gtk_native_get_surface(GTK_NATIVE(g_window)), none_cursor);
    g_object_unref(none_cursor);
    log_info("greeter: screen blanked");
    return G_SOURCE_REMOVE;
}

static void reset_blank_timer(void) {
    if (g_blank_timer_id != 0)
        g_source_remove(g_blank_timer_id);
    int timeout = greeter_config_blank_timeout();
    if (timeout <= 0) {
        g_blank_timer_id = 0;
        return;
    }
    g_blank_timer_id = g_timeout_add_seconds((guint)timeout, on_blank_timeout, NULL);
}

static void unblank_screen(void) {
    if (!gtk_widget_get_visible(g_blank_screen))
        return;
    log_info("greeter: screen unblanked");
    gtk_widget_set_visible(g_blank_screen, FALSE);
    gdk_surface_set_cursor(gtk_native_get_surface(GTK_NATIVE(g_window)), NULL);
    reset_blank_timer();
}

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
                               GdkModifierType state, gpointer user_data) {
    (void)controller;
    (void)keyval;
    (void)keycode;
    (void)state;
    (void)user_data;
    if (gtk_widget_get_visible(g_blank_screen)) {
        log_debug("greeter: key pressed while blank");
        unblank_screen();
    } else {
        reset_blank_timer();
    }
    return FALSE; /* don't consume - let the event reach its target widget */
}

static void on_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y,
                      gpointer user_data) {
    (void)controller;
    (void)x;
    (void)y;
    (void)user_data;
    if (gtk_widget_get_visible(g_blank_screen)) {
        gint64 elapsed = g_get_monotonic_time() - g_blank_start_us;
        log_debug("greeter: motion while blank, elapsed=%ldms", (long)(elapsed / 1000));
        if (elapsed < BLANK_MOTION_GUARD_US)
            return;
        unblank_screen();
    } else {
        reset_blank_timer();
    }
}

static gboolean on_auth_timeout(gpointer user_data) {
    GtkWidget *widget = GTK_WIDGET(user_data);
    log_error("greeter: timed out waiting for daemon response");
    g_auth_timeout_id = 0;
    if (g_auth_fd_source_id != 0) {
        g_source_remove(g_auth_fd_source_id);
        g_auth_fd_source_id = 0;
    }
    unblank_screen();
    show_error(IPC_ERROR_INTERNAL);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_widget_get_root(widget)), TRUE);
    return G_SOURCE_REMOVE;
}

/* Callback for IPC response from the daemon */
static gboolean on_ipc_response_ready(gint fd, GIOCondition condition, gpointer user_data) {
    (void)fd;
    GtkWidget *widget = GTK_WIDGET(user_data);

    if (g_auth_timeout_id != 0) {
        g_source_remove(g_auth_timeout_id);
        g_auth_timeout_id = 0;
    }
    g_auth_fd_source_id = 0;

    unblank_screen();

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
    if (current_page() == PAGE_PASSWORD) {
        /* Clear and refocus the password field so user can retype it. */
        gtk_editable_set_text(GTK_EDITABLE(g_password_entry), "");
        gtk_widget_grab_focus(GTK_WIDGET(g_password_entry));
    }
    return G_SOURCE_REMOVE;
}

/* Return the id of the currently selected session, or "" if none. */
static const char *get_selected_session_id(void) {
    if (g_session_dropdown) {
        guint idx = gtk_drop_down_get_selected(g_session_dropdown);
        if ((int)idx < g_num_sessions)
            return g_sessions[idx].id;
    } else if (g_num_sessions >= 1) {
        return g_sessions[0].id;
    }
    return "";
}

/* Disable the window, send credentials, and register the IPC response watcher.
Re-enables the window on send failure. */
static void submit_credentials(GtkWidget *widget, const char *password) {
    reset_page();
    start_spinner();

    GtkRoot *root = gtk_widget_get_root(widget);
    gtk_widget_set_sensitive(GTK_WIDGET(root), FALSE);
    if (ipc_send_credentials(g_ch, g_selected_user->username, password,
                             get_selected_session_id()) != 0) {
        show_error(IPC_ERROR_INTERNAL);
        gtk_widget_set_sensitive(GTK_WIDGET(root), TRUE);
        return;
    }
    g_auth_fd_source_id = g_unix_fd_add(ipc_get_read_fd(g_ch), G_IO_IN | G_IO_ERR | G_IO_HUP,
                                        on_ipc_response_ready, widget);
    g_auth_timeout_id = g_timeout_add_seconds(GREETER_AUTH_TIMEOUT, on_auth_timeout, widget);
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
    switch_page(PAGE_PASSWORD);
    reset_page();
    gtk_widget_grab_focus(GTK_WIDGET(g_password_entry));
}

static void on_back_clicked(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
    g_selected_user = NULL;
    gtk_editable_set_text(GTK_EDITABLE(g_password_entry), "");
    switch_page(PAGE_USERS);
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

static void card_create(GdkRectangle geo) {
    /* Stack consisting of user selection page and password entry page. */
    GtkWidget *stack = gtk_stack_new();
    gtk_widget_set_size_request(stack, geo.width, geo.height);
    gtk_fixed_put(g_root, stack, geo.x, geo.y);
    g_stack = GTK_STACK(stack);

    /* User selection page */

    GtkWidget *users_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, CARD_SPACING_USERS);
    gtk_widget_set_halign(users_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(users_box, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(users_box, "card");
    gtk_stack_add_named(g_stack, users_box, "users");

    GtkWidget *heading = gtk_label_new(greeter_config_login_label());
    gtk_widget_add_css_class(heading, "heading");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(users_box), heading);

    GtkWidget *users_spinner = gtk_spinner_new();
    gtk_widget_set_visible(users_spinner, FALSE);
    gtk_box_append(GTK_BOX(users_box), users_spinner);
    g_users_spinner = GTK_SPINNER(users_spinner);

    GtkWidget *users_error_label = gtk_label_new("");
    gtk_widget_add_css_class(users_error_label, "error-label");
    gtk_widget_set_visible(users_error_label, FALSE);
    gtk_label_set_wrap(GTK_LABEL(users_error_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(users_error_label), 0);
    gtk_label_set_justify(GTK_LABEL(users_error_label), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(users_box), users_error_label);
    g_users_error_label = GTK_LABEL(users_error_label);

    /* Session dropdown - only shown when there are two or more sessions. */
    if (g_num_sessions > 1) {
        GtkWidget *session_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(session_row, "session-row");
        gtk_box_append(GTK_BOX(users_box), session_row);

        GtkWidget *session_label = gtk_label_new("Session:");
        gtk_box_append(GTK_BOX(session_row), session_label);

        const char *names[MAX_NUM_SESSIONS + 1];
        for (int i = 0; i < g_num_sessions; i++)
            names[i] = g_sessions[i].name;
        names[g_num_sessions] = NULL;
        GtkWidget *dropdown = gtk_drop_down_new_from_strings(names);
        gtk_widget_add_css_class(dropdown, "session-dropdown");
        gtk_box_append(GTK_BOX(session_row), dropdown);
        g_session_dropdown = GTK_DROP_DOWN(dropdown);

        /* Preselect the last-used session if available. */
        const char *preselect = getenv("ATRIUM_SESSION_PRESELECT");
        if (preselect && *preselect) {
            for (int i = 0; i < g_num_sessions; i++) {
                if (strcmp(g_sessions[i].id, preselect) == 0) {
                    gtk_drop_down_set_selected(g_session_dropdown, (guint)i);
                    break;
                }
            }
        }
    }

    GtkWidget *user_buttons_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, USER_BUTTONS_SPACING);
    gtk_box_append(GTK_BOX(users_box), user_buttons_box);

    for (int i = 0; i < g_num_users; i++) {
        GtkWidget *btn = gtk_button_new_with_label(g_users[i].display_name);
        gtk_widget_add_css_class(btn, "user-button");
        g_signal_connect(btn, "clicked", G_CALLBACK(on_user_clicked), (void *)&g_users[i]);
        gtk_box_append(GTK_BOX(user_buttons_box), btn);
    }

    /* Password entry page */

    GtkWidget *password_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, CARD_SPACING_PASSWORD);
    gtk_widget_set_halign(password_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(password_box, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(password_box, "card");
    gtk_stack_add_named(g_stack, password_box, "password");

    GtkWidget *password_title = gtk_label_new("");
    gtk_widget_add_css_class(password_title, "heading");
    gtk_widget_set_halign(password_title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(password_box), password_title);
    g_user_label = GTK_LABEL(password_title);

    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(password_entry), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
    gtk_widget_add_css_class(password_entry, "password-field");
    gtk_box_append(GTK_BOX(password_box), password_entry);
    g_password_entry = GTK_ENTRY(password_entry);

    GtkWidget *password_spinner = gtk_spinner_new();
    gtk_widget_set_visible(password_spinner, FALSE);
    gtk_box_append(GTK_BOX(password_box), password_spinner);
    g_password_spinner = GTK_SPINNER(password_spinner);

    GtkWidget *password_error_label = gtk_label_new("");
    gtk_widget_add_css_class(password_error_label, "error-label");
    gtk_widget_set_visible(password_error_label, FALSE);
    gtk_label_set_wrap(GTK_LABEL(password_error_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(password_error_label), 0);
    gtk_label_set_justify(GTK_LABEL(password_error_label), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(password_box), password_error_label);
    g_password_error_label = GTK_LABEL(password_error_label);

    GtkWidget *action_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, ACTION_BUTTONS_SPACING);
    gtk_box_append(GTK_BOX(password_box), action_box);

    GtkWidget *login_btn = gtk_button_new_with_label("Log In");
    gtk_widget_add_css_class(login_btn, "suggested-action");
    g_signal_connect(login_btn, "clicked", G_CALLBACK(on_login_clicked), NULL);
    g_signal_connect(password_entry, "activate", G_CALLBACK(on_login_clicked), NULL);
    gtk_box_append(GTK_BOX(action_box), login_btn);

    GtkWidget *back_btn = gtk_button_new_with_label("← Back");
    gtk_widget_add_css_class(back_btn, "back-button");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_clicked), NULL);
    gtk_box_append(GTK_BOX(action_box), back_btn);
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    theme_apply();

    GtkSettings *gtk_settings = gtk_settings_get_default();
    g_object_set(gtk_settings, "gtk-cursor-theme-name", greeter_config_cursor_theme(),
                 "gtk-cursor-theme-size", greeter_config_cursor_size(), NULL);

    /* Detect number of displays on this seat, so we can center the card correctly. */
    GdkDisplay *display = gdk_display_get_default();
    GListModel *monitors = gdk_display_get_monitors(display);
    guint       n_monitors = g_list_model_get_n_items(monitors);
    log_info("greeter: %u monitor(s) detected", n_monitors);

    g_window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(g_window, "atrium");
    gtk_window_set_decorated(g_window, FALSE);
    gtk_window_fullscreen(g_window);

    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), NULL);
    gtk_widget_add_controller(GTK_WIDGET(g_window), key_controller);

    GtkEventController *motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(motion_controller, "motion", G_CALLBACK(on_motion), NULL);
    gtk_widget_add_controller(GTK_WIDGET(g_window), motion_controller);

    /* Overlay for screen blanking. */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_window_set_child(g_window, overlay);

    /* Build everything on top of a GtkFixed so we can handle multi-monitor geometries. */
    g_root = GTK_FIXED(gtk_fixed_new());
    gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(g_root));

    /* Get the geometry of the default monitor. */
    GdkRectangle default_monitor_geo = {.width = 800, .height = 600};
    if (n_monitors > 0) {
        GdkMonitor *mon = GDK_MONITOR(g_list_model_get_item(monitors, 0));
        gdk_monitor_get_geometry(mon, &default_monitor_geo);
        g_object_unref(mon);
        log_debug("greeter: monitor[%u] geometry: %dx%d+%d+%d", 0, default_monitor_geo.width,
                  default_monitor_geo.height, default_monitor_geo.x, default_monitor_geo.y);
    }

    /* Build the login card */
    card_create(default_monitor_geo);

    /* SHORTCUT - show a black screen instead of real DPMS. */
    GtkWidget *blank_screen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(blank_screen, TRUE);
    gtk_widget_set_vexpand(blank_screen, TRUE);
    gtk_widget_add_css_class(blank_screen, "blank-page");
    gtk_widget_set_visible(blank_screen, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), blank_screen);
    g_blank_screen = blank_screen;

    /* Show the window and start the blank timer */
    switch_page(PAGE_USERS);
    gtk_window_present(g_window);
    reset_blank_timer();
}

int run_ui(const greeter_user *users, int num_users, const greeter_session *sessions,
           int num_sessions, ipc_channel *ch) {
    g_users = users;
    g_num_users = num_users;
    g_sessions = sessions;
    g_num_sessions = num_sessions;
    g_ch = ch;

    GtkApplication *app = gtk_application_new("com.kavau.atrium", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);

    return status;
}
