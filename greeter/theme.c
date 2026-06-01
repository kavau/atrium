#include "theme.h"

#include <stdio.h>

#include <gtk/gtk.h>

#include "config.h"

static void load_css_string(GtkCssProvider *provider, const char *css) {
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
#endif
}

static void add_provider(GtkCssProvider *provider) {
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void theme_apply(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/kavau/atrium/theme.css");
    add_provider(provider);

    /* Inject base font size from compile-time constant. Loaded after the main
     * theme so it wins at the same priority level. */
    char font_css[48];
    snprintf(font_css, sizeof(font_css), "window { font-size: %dpx; }", greeter_config_base_font_size());
    GtkCssProvider *font_provider = gtk_css_provider_new();
    load_css_string(font_provider, font_css);
    add_provider(font_provider);
}
