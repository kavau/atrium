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

static void add_provider_at(GtkCssProvider *provider, guint priority) {
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               priority);
    g_object_unref(provider);
}

static void add_provider(GtkCssProvider *provider) {
    add_provider_at(provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* Read path into a heap-allocated NUL-terminated string.
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

/* Resolves path to a single image file. If path is a directory, pick a random 
image from it. Returns a g_malloc'd path to a background image, or NULL. */
static char *resolve_background_path(const char *path) {
    if (!path || !*path)
        return NULL;

    if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return g_strdup(path);

    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        log_warn("theme: background-image path not found: %s", path);
        return NULL;
    }

    static const char * const img_exts[] = {
        ".jpg", ".jpeg", ".png", ".webp", ".gif", ".bmp", ".svg", ".tiff", ".tif", NULL
    };

    GPtrArray *images = g_ptr_array_new_with_free_func(g_free);
    GDir      *dir    = g_dir_open(path, 0, NULL);
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir))) {
            const char *dot = strrchr(name, '.');
            if (!dot)
                continue;
            for (int i = 0; img_exts[i]; i++) {
                if (g_ascii_strcasecmp(dot, img_exts[i]) == 0) {
                    g_ptr_array_add(images, g_build_filename(path, name, NULL));
                    break;
                }
            }
        }
        g_dir_close(dir);
    }

    char *chosen = NULL;
    if (images->len > 0) {
        guint idx = (guint)g_random_int_range(0, (gint32)images->len);
        chosen = g_strdup(images->pdata[idx]);
    } else {
        log_warn("theme: no images found in background directory: %s", path);
    }
    g_ptr_array_free(images, TRUE);
    return chosen;
}

void theme_apply(void) {
    /* Get the built-in CSS as a NUL-terminated string. */
    GBytes *bytes = g_resources_lookup_data("/com/kavau/atrium/theme.css",
                                            G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    const char *base_css = bytes ? (const char *)g_bytes_get_data(bytes, NULL) : "";

    GtkCssProvider *provider   = gtk_css_provider_new();
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

    /* Apply background image if configured. Loaded at APPLICATION+2 so it
    takes precedence over any background set in theme files. */
    char *bg_path = resolve_background_path(greeter_config_background_image());
    if (bg_path) {
        char *uri    = g_filename_to_uri(bg_path, NULL, NULL);
        char *bg_css = g_strdup_printf(
            "window { background-image: url(\"%s\");"
            " background-size: cover;"
            " background-position: center;"
            " background-repeat: no-repeat; }",
            uri ? uri : bg_path);
        g_free(uri);
        GtkCssProvider *bg_provider = gtk_css_provider_new();
        load_css_string(bg_provider, bg_css);
        g_free(bg_css);
        add_provider_at(bg_provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
        log_info("theme: background image: %s", bg_path);
        g_free(bg_path);
    }
}
