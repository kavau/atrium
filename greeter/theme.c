#include "theme.h"

#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include "config.h"
#include "lib/log.h"

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

/* Read path into a heap-allocated NULL-terminated string.
Returns NULL on failure. Caller must g_free() the result. */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_syserr("theme: cannot open '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size <= 0) {
        fclose(f);
        return g_strdup("");
    }
    char *buf = g_malloc(size + 1);
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

void theme_apply(void) {
    /* Get the built-in CSS as a NULL-terminated string. */
    GBytes     *bytes = g_resources_lookup_data("/com/kavau/atrium/theme.css",
                                                 G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    const char *base_css = bytes ? (const char *)g_bytes_get_data(bytes, NULL) : "";

    GtkCssProvider *provider = gtk_css_provider_new();
    const char     *theme_path = greeter_config_theme();

    if (*theme_path) {
        char *overrides = read_file(theme_path);
        if (overrides) {
            /* Append the theme file to the base CSS. @define-color declarations
            in the theme override the defaults; direct CSS rules win via normal
            cascade. */
            char *combined = g_strconcat(base_css, "\n", overrides, NULL);
            load_css_string(provider, combined);
            g_free(combined);
            g_free(overrides);
        } else {
            load_css_string(provider, base_css);
        }
    } else {
        load_css_string(provider, base_css);
    }

    if (bytes)
        g_bytes_unref(bytes);
    add_provider(provider);

    /* Inject base font size as a separate provider so it takes precedence over
    any font-size set in the theme. */
    char font_css[48];
    snprintf(font_css, sizeof(font_css), "window { font-size: %dpx; }",
             greeter_config_base_font_size());
    GtkCssProvider *font_provider = gtk_css_provider_new();
    load_css_string(font_provider, font_css);
    add_provider(font_provider);
}
