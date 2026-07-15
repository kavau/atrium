#include "glass.h"

#include <stdio.h>

#define GLASS_TYPE_CARD_BG glass_card_bg_get_type()
G_DECLARE_FINAL_TYPE(GlassCardBg, glass_card_bg, GLASS, CARD_BG, GtkWidget)

/*
 * GlassCardBg: private widget that renders the wallpaper slice
 */

struct _GlassCardBg {
    GtkWidget   parent_instance;
    GdkTexture *texture;
    GtkWidget  *picture; /* no ref - lives in the same widget tree */
};

G_DEFINE_TYPE(GlassCardBg, glass_card_bg, GTK_TYPE_WIDGET)

static void glass_card_bg_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
    GlassCardBg *self = GLASS_CARD_BG(widget);
    if (!self->texture || !self->picture)
        return;

    /* Compute the scale and translation parameters for the card background
    slice, so that it lines up exactly with the desktop background. When clipped
    and blurred, this creates the frosted-glass effect. */

    /* Get the dimensions of the background texture, and of the widget holding it. */
    int tex_w = gdk_texture_get_width(self->texture);
    int tex_h = gdk_texture_get_height(self->texture);
    int win_w = gtk_widget_get_width(self->picture);
    int win_h = gtk_widget_get_height(self->picture);
    if (win_w <= 0 || win_h <= 0)
        return;

    /* Apply the same scaling as applied to the desktop background by GTK_CONTENT_FIT_COVER. */
    double scale = MAX((double)win_w / tex_w, (double)win_h / tex_h);
    double offset_x = (win_w - tex_w * scale) / 2.0; /* horizontal overflow */
    double offset_y = (win_h - tex_h * scale) / 2.0; /* vertical overflow */

    /* Compute the widget position (top left corner) in picture coordinates. */
    graphene_point_t origin = GRAPHENE_POINT_INIT(0.0f, 0.0f);
    graphene_point_t widget_pos;
    if (!gtk_widget_compute_point(widget, self->picture, &origin, &widget_pos))
        return;

    /* Destination rect for the whole wallpaper (the origin is negative, so that
    the region behind the card lands in our box). */
    graphene_rect_t dest =
        GRAPHENE_RECT_INIT((float)(offset_x - widget_pos.x), (float)(offset_y - widget_pos.y),
                           (float)(tex_w * scale), (float)(tex_h * scale));
    gtk_snapshot_append_texture(snapshot, self->texture, &dest);
}

static void glass_card_bg_dispose(GObject *object) {
    GlassCardBg *self = GLASS_CARD_BG(object);
    g_clear_object(&self->texture);
    G_OBJECT_CLASS(glass_card_bg_parent_class)->dispose(object);
}

static void glass_card_bg_class_init(GlassCardBgClass *klass) {
    G_OBJECT_CLASS(klass)->dispose = glass_card_bg_dispose;
    GTK_WIDGET_CLASS(klass)->snapshot = glass_card_bg_snapshot;
}

static void glass_card_bg_init(GlassCardBg *self) { (void)self; }

static GtkWidget *glass_card_bg_new(GdkTexture *texture, GtkWidget *picture) {
    GlassCardBg *self = g_object_new(GLASS_TYPE_CARD_BG, NULL);
    self->texture = texture ? g_object_ref(texture) : NULL;
    self->picture = picture;
    return GTK_WIDGET(self);
}

/*
 * GlassCard
 */

struct _GlassCard {
    GtkWidget  parent_instance;
    GtkWidget *bg;    /* GlassCardBg: blurred wallpaper slice */
    GtkWidget *tint;  /* color overlay (styled via .card-tint) */
    GtkWidget *child; /* single content widget */
};

G_DEFINE_TYPE(GlassCard, glass_card, GTK_TYPE_WIDGET)

static void glass_card_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
                               int *minimum, int *natural, int *min_baseline, int *nat_baseline) {
    GlassCard *self = GLASS_CARD(widget);
    if (!self->child) {
        *minimum = *natural = 0;
        if (min_baseline)
            *min_baseline = -1;
        if (nat_baseline)
            *nat_baseline = -1;
        return;
    }

    if (orientation == GTK_ORIENTATION_VERTICAL && for_size < 0) {
        /* Measure the child's height for unconstrained width. This needs a
        two-pass approach, because wrapping text (i.e. an error message) reports
        its height at minimum width, making the card unnaturally tall.
        The fix: first ask the child for its natural (preferred) width, then ask
        for its height at that width. */
        int nat_w;
        gtk_widget_measure(self->child, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &nat_w, NULL, NULL);
        int min_w = -1;
        gtk_widget_get_size_request(widget, &min_w, NULL);
        /* effective width is the larger of natural width and requested minimum width */
        int effective_w = MAX(nat_w, min_w);
        gtk_widget_measure(self->child, GTK_ORIENTATION_VERTICAL, effective_w, minimum, natural,
                           min_baseline, nat_baseline);
        return;
    }

    gtk_widget_measure(self->child, orientation, for_size, minimum, natural, min_baseline,
                       nat_baseline);
}

/* Forward the child's request mode (GtkWidget defaults to CONSTANT_SIZE). */
static GtkSizeRequestMode glass_card_get_request_mode(GtkWidget *widget) {
    GlassCard *self = GLASS_CARD(widget);
    return self->child ? gtk_widget_get_request_mode(self->child) : GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void glass_card_size_allocate(GtkWidget *widget, int width, int height, int baseline) {
    GlassCard *self = GLASS_CARD(widget);
    if (self->bg)
        gtk_widget_allocate(self->bg, width, height, baseline, NULL);
    if (self->tint)
        gtk_widget_allocate(self->tint, width, height, baseline, NULL);
    if (self->child)
        gtk_widget_allocate(self->child, width, height, baseline, NULL);
}

static void glass_card_dispose(GObject *object) {
    GlassCard *self = GLASS_CARD(object);
    g_clear_pointer(&self->bg, gtk_widget_unparent);
    g_clear_pointer(&self->tint, gtk_widget_unparent);
    g_clear_pointer(&self->child, gtk_widget_unparent);
    G_OBJECT_CLASS(glass_card_parent_class)->dispose(object);
}

static void glass_card_class_init(GlassCardClass *klass) {
    G_OBJECT_CLASS(klass)->dispose = glass_card_dispose;
    GTK_WIDGET_CLASS(klass)->get_request_mode = glass_card_get_request_mode;
    GTK_WIDGET_CLASS(klass)->measure = glass_card_measure;
    GTK_WIDGET_CLASS(klass)->size_allocate = glass_card_size_allocate;
}

static void glass_card_init(GlassCard *self) {
    gtk_widget_add_css_class(GTK_WIDGET(self), "card");
    gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_HIDDEN);
}

GtkWidget *glass_card_new(GFile *file, GtkWidget *picture, GtkWidget *child) {
    GlassCard  *self = g_object_new(GLASS_TYPE_CARD, NULL);
    GdkTexture *texture = NULL;

    if (file) {
        GError *err = NULL;
        texture = gdk_texture_new_from_file(file, &err);
        if (!texture) {
            fprintf(stderr, "GlassCard: failed to load texture: %s\n", err->message);
            g_error_free(err);
        }
    }

    self->bg = glass_card_bg_new(texture, picture);
    if (texture)
        g_object_unref(texture); /* bg keeps its own ref */
    gtk_widget_add_css_class(self->bg, "card-background");
    gtk_widget_set_parent(self->bg, GTK_WIDGET(self));

    self->tint = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(self->tint, "card-tint");
    gtk_widget_set_parent(self->tint, GTK_WIDGET(self));

    if (child) {
        self->child = child;
        gtk_widget_set_parent(child, GTK_WIDGET(self));
    }

    return GTK_WIDGET(self);
}
