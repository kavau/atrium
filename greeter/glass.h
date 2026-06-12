#pragma once

/*
 * glass.h: a GTK4 widget that renders a scaled and translated slice of a
 * desktop background image as its own background. In conjunction with a css
 * blur filter, it can be used to create a frosted-glass effect.
 */

#include <gtk/gtk.h>

#define GLASS_TYPE_CARD glass_card_get_type()
G_DECLARE_FINAL_TYPE(GlassCard, glass_card, GLASS, CARD, GtkWidget)

/* Create a new GlassCard. file is the background image file, picture is the
same GtkPicture used for the desktop background (GlassCard reads its allocated
size in order to scale and translate the image accordingly), and child is the
content widget placed on top. */
GtkWidget *glass_card_new(GFile *file, GtkWidget *picture, GtkWidget *child);
