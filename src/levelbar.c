/*
 * Copyright (c) Team audio-recorder.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License (GPL3), or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Library General Public License 3 for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License 3 along with this program; if not, see /usr/share/common-licenses/GPL file
 * or <http://www.gnu.org/licenses/>.
*/
#include "levelbar.h"
#include <math.h> // round()

// A simple level bar widget.
// By Osmo Antero.
//
// Sample call:
// GtkWidget *lb = level_bar_new();
// gtk_widget_show(lb);
//
// Set value [0.0, 1.0].
// level_bar_set_fraction(LEVEL_BAR(lb), 0.8);
//
// Show %-value [0 - 100%] or plain value [0 - 1.0] on the level bar. See BAR_VALUE enum.
// level_bar_set_value_type(LEVEL_BAR(lb), VALUE_PRECENT);
//

struct _LevelBarPrivate {
    gdouble fraction;
    guint bar_height;
    enum BAR_VALUE bar_value;
    enum BAR_SHAPE bar_shape;
};

static void level_bar_get_preferred_width(GtkWidget *widget, gint *minimum, gint *natural);
static void level_bar_get_preferred_height (GtkWidget *widget,gint *minimum, gint *natural);

static void level_bar_real_update(LevelBar *progress);
static gboolean level_bar_draw(GtkWidget *widget, cairo_t *cr);

static void level_bar_finalize(GObject *object);

G_DEFINE_TYPE(LevelBar, level_bar, GTK_TYPE_WIDGET);

static void level_bar_class_init(LevelBarClass *class) {
    GObjectClass *gobject_class;
    GtkWidgetClass *widget_class;

    gobject_class = G_OBJECT_CLASS (class);
    widget_class = (GtkWidgetClass *)class;

    gobject_class->set_property = NULL;
    gobject_class->get_property = NULL;
    gobject_class->finalize = level_bar_finalize;

    widget_class->draw = level_bar_draw;
    widget_class->get_preferred_width = level_bar_get_preferred_width;
    widget_class->get_preferred_height = level_bar_get_preferred_height;

    g_type_class_add_private(class, sizeof (LevelBarPrivate));
}

static void level_bar_init(LevelBar *pbar) {
    LevelBarPrivate *priv;

    pbar->priv = G_TYPE_INSTANCE_GET_PRIVATE(pbar, TYPE_LEVEL_BAR, LevelBarPrivate);
    priv = pbar->priv;

    priv->fraction = 0.0;
    priv->bar_height = 8;
    priv->bar_value = VALUE_NONE;
    priv->bar_shape = SHAPE_CIRCLE; // pulsing line with circle at end.

    gtk_widget_set_has_window(GTK_WIDGET (pbar), FALSE);
}

GtkWidget *level_bar_new(void) {
    GtkWidget *pbar;
    pbar = g_object_new(TYPE_LEVEL_BAR, NULL);
    return pbar;
}

static void level_bar_real_update (LevelBar *pbar) {
    GtkWidget *widget;

    g_return_if_fail (IS_LEVEL_BAR (pbar));

    LevelBarPrivate __attribute__ ((unused)) *priv = pbar->priv;

    widget = GTK_WIDGET(pbar);

    gtk_widget_queue_draw(widget);
}

static void level_bar_finalize (GObject *object) {
    G_OBJECT_CLASS(level_bar_parent_class)->finalize (object);
}

static void level_bar_get_preferred_width (GtkWidget *widget,gint *minimum, gint *natural) {
    *minimum = 50;
    *natural = 160;
}

static void level_bar_get_preferred_height(GtkWidget *widget, gint *minimum, gint *natural) {
    *minimum = 6;
    *natural = 8;
}

static gboolean level_bar_draw(GtkWidget *widget, cairo_t *cr) {
    // Draw level bar and optional text
    LevelBar *pbar = LEVEL_BAR (widget);
    LevelBarPrivate *priv = pbar->priv;
    GtkStyleContext *context;
    int width, height;

    context = gtk_widget_get_style_context(widget);

    width = gtk_widget_get_allocated_width(widget);
    height = gtk_widget_get_allocated_height(widget);

    // Bar thickness
    gdouble bar_height = MIN(height , priv->bar_height);

    // Vertical pos
    gdouble y = (height - bar_height)/2;

    // Pulse width
    gdouble w = priv->fraction/(1.00/width);

    // Debug:
    // LOG_DEBUG("width=%d height=%d bar_height=%2.1f y=%2.1f w=%2.1f  fraction=%2.1f\n", width, height, bar_height, y, w, priv->fraction);

    gtk_style_context_save(context);
    gtk_render_background(context, cr, 0, 0, width, height);
    gtk_render_frame(context, cr, 0, 0, width, height);

    // Render level bar with current theme and color.

    // Progressbar style
    gtk_style_context_add_class(context, GTK_STYLE_CLASS_PROGRESSBAR);

    if (priv->fraction > 0.001) {

        switch (priv->bar_shape) {

        case SHAPE_LINE:
            // Render a single line
            if (priv->bar_value == VALUE_NONE) {
                // No value (text) shown. Draw a line on the middle.
                gtk_render_line(context, cr, 0, y + (bar_height / 2), w, y + (bar_height / 2));

            } else {
                // Draw a line under text.
                gtk_render_line(context, cr, 0, y + (bar_height ), w, y + (bar_height ));
            }

            break;

        case SHAPE_LINE2:
            // Render two horizontal lines + close the end.
            gtk_render_line(context, cr, 0, y-1 , w, y-1);
            gtk_render_line(context, cr, 0, y + (bar_height ), w, y + (bar_height ));
            gtk_render_line(context, cr, w, y - 1, w, y + (bar_height ));
            break;

        case SHAPE_CIRCLE:
            // Draw a line on the middle + circle at the end.
            gtk_render_line(context, cr, 0, y + (bar_height / 2), w, y + (bar_height / 2));
            gtk_style_context_set_state(context, GTK_STATE_FLAG_CHECKED);
            gtk_render_option(context, cr, w, y, bar_height+1, bar_height+1);
            break;

        default:
            // case SHAPE_LEVELBAR:

            // EDIT: gtk_render_activity() does not work in GTK 3.14+
            // gtk_style_context_set_state(context, GTK_STATE_FLAG_ACTIVE);
            // gtk_render_activity(context, cr, 0, y, w, bar_height);

            // Render a filled frame (this is a typical levelbar).
            gtk_render_frame(context, cr, 0, y, w, bar_height);
            break;

        }
    }

    gtk_style_context_restore(context);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, priv->bar_height);

    GdkRGBA color;
    
// Check:
// $ pkg-config --modversion gtk+-3.0
//           
#if GTK_CHECK_VERSION(3,16,0)    
    gtk_style_context_get_color(context, GTK_STATE_FLAG_NORMAL, &color);
#else
    gtk_style_context_get_border_color(context, GTK_STATE_NORMAL, &color);
#endif

    gdk_cairo_set_source_rgba(cr, &color);
    color.alpha = 0.9;

    // Calculate total width of scale
    cairo_text_extents_t extents;
    cairo_text_extents(cr, "0.0", &extents);
    gint total_w = 9 * (extents.x_advance + extents.width);

    // Debug:
    // g_print("Bar width=%d  total_w=%d  bearing=%3.1f advance=%3.1f char.width=%3.1f\n", width, total_w,
    //          extents.x_bearing, extents.x_advance, extents.width);

    // Draw values
    gboolean draw_all = (total_w - extents.width) < width;

    // Show normalized value [0 - 1.0]?
    if (priv->bar_value == VALUE_0_1) {
        // Value: 0.1  0.2  0.3  0.4...0.9

        gint i = 0;
        for (i=0; i < 10; i++) {
            gchar *s = NULL;

            // Draw all or draw only each second value?
            if (draw_all || (i % 2 == 0))
                s = g_strdup_printf("%2.1f", (gdouble)i/10.0);

            if (!s) continue;

            cairo_text_extents_t extents;
            cairo_text_extents(cr, s, &extents);

            gdouble xx = (width/10) * i;
            gdouble yy = (height/2)-(extents.height/2 + extents.y_bearing) + 0.2;

            cairo_move_to(cr, xx, yy);
            cairo_show_text(cr, s);

            g_free(s);
        }

        // Show percentage value?
    } else if (priv->bar_value == VALUE_PERCENT) {
        // Value: 10% . 20% . 30% . 40% . 50% ... 90%
        gint i = 0;
        for (i=0; i < 10; i++) {
            gchar *s = NULL;
            if (i % 2 == 0)
                s = g_strdup_printf("%2.0f%%", (gdouble)i*10.0);
            else
                s = g_strdup_printf("%3s", ".");

            cairo_text_extents_t extents;
            cairo_text_extents(cr, s, &extents);

            gdouble xx = (width/10) * i;
            gdouble yy = (height/2)-(extents.height/2 + extents.y_bearing);

            cairo_move_to(cr, xx, yy);
            cairo_show_text(cr, s);

            g_free(s);
        }
    }

#if 0
    // Commented out by moma 30.sep.2012.

    // Set text
    if (priv->text) {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

        cairo_set_font_size(cr, 0.5*height);
        cairo_text_extents_t extents;
        cairo_text_extents(cr, priv->text, &extents);

        // Ref: https://www.cairographics.org/manual/cairo-cairo-scaled-font-t.html#cairo-text-extents-t
        gdouble xx = width-(extents.width + extents.x_bearing)-2;
        gdouble yy = height/2-(extents.height/2 + extents.y_bearing);

        GdkRGBA color;
        gtk_style_context_get_border_color(context, GTK_STATE_NORMAL, &color);
        gdk_cairo_set_source_rgba(cr, &color);
        color.alpha = 0.9;

        cairo_move_to(cr, xx, yy);
        cairo_show_text(cr, priv->text);
    }
#endif

    return FALSE;
}

void level_bar_set_fraction(LevelBar *pbar, gdouble fraction) {
    // Set fraction [0.0, 1.0]
    LevelBarPrivate* priv;
    g_return_if_fail (IS_LEVEL_BAR (pbar));
    priv = pbar->priv;

    priv->fraction = CLAMP(fraction, 0.0, 1.0);
    level_bar_real_update (pbar);
}

gdouble level_bar_get_fraction(LevelBar *pbar) {
    // Get fraction
    g_return_val_if_fail(IS_LEVEL_BAR (pbar), 0);
    return pbar->priv->fraction;
}

void level_bar_set_bar_height(LevelBar *pbar, guint height) {
    // Set bar height (thickness). Normally 8 pixels.
    g_return_if_fail(IS_LEVEL_BAR (pbar));
    LevelBarPrivate* priv = pbar->priv;
    priv->bar_height = height;
    // Redraw
    level_bar_real_update(pbar);
}

guint level_bar_get_bar_height(LevelBar *pbar) {
    // Get bar thickness
    g_return_val_if_fail(IS_LEVEL_BAR(pbar), 0);
    return pbar->priv->bar_height;
}

void level_bar_set_value_type(LevelBar *pbar, enum BAR_VALUE bar_value) {
    // Set BAR_VALUE
    g_return_if_fail(IS_LEVEL_BAR(pbar));
    LevelBarPrivate* priv = pbar->priv;
    priv->bar_value = bar_value;
    // Redraw
    level_bar_real_update(pbar);
}

enum BAR_VALUE level_bar_get_scale(LevelBar *pbar) {
    // Get BAR_VALUE
    g_return_val_if_fail(IS_LEVEL_BAR(pbar), VALUE_NONE);
    LevelBarPrivate* priv = pbar->priv;
    return priv->bar_value;
}

void level_bar_set_shape(LevelBar *pbar, enum BAR_SHAPE bar_shape) {
    // Set BAR_SHAPE
    g_return_if_fail(IS_LEVEL_BAR(pbar));
    LevelBarPrivate* priv = pbar->priv;
    priv->bar_shape = bar_shape;
    // Redraw
    level_bar_real_update(pbar);
}

enum BAR_SHAPE level_bar_get_shape(LevelBar *pbar) {
    // Get BAR_SHAPE
    g_return_val_if_fail(IS_LEVEL_BAR(pbar), SHAPE_LEVELBAR);
    LevelBarPrivate* priv = pbar->priv;
    return priv->bar_shape;
}


