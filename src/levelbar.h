#ifndef __LEVEL_BAR_H__
#define __LEVEL_BAR_H__

// A simple level bar widget.

#include <gtk/gtk.h>

typedef enum BAR_VALUE {VALUE_NONE, VALUE_0_1/*0 - 1.0*/, VALUE_PERCENT/*0 - 100%*/} BAR_VALUE;
typedef enum BAR_SHAPE {SHAPE_LEVELBAR, SHAPE_LINE, SHAPE_LINE2, SHAPE_CIRCLE} BAR_SHAPE;

G_BEGIN_DECLS

#define TYPE_LEVEL_BAR            (level_bar_get_type ())
#define LEVEL_BAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_LEVEL_BAR, LevelBar))
#define LEVEL_BAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_LEVEL_BAR, LevelBarClass))
#define IS_LEVEL_BAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_LEVEL_BAR))
#define IS_LEVEL_BAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_LEVEL_BAR))
#define LEVEL_BAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_LEVEL_BAR, LevelBarClass))

typedef struct _LevelBar              LevelBar;
typedef struct _LevelBarPrivate       LevelBarPrivate;
typedef struct _LevelBarClass         LevelBarClass;

struct _LevelBar {
    GtkWidget parent;

    /*< private >*/
    LevelBarPrivate *priv;
};

struct _LevelBarClass {
    GtkWidgetClass parent_class;

    /* Padding for future expansion */
    void (*_gtk_reserved1) (void);
    void (*_gtk_reserved2) (void);
    void (*_gtk_reserved3) (void);
    void (*_gtk_reserved4) (void);
};

GType level_bar_get_type(void) G_GNUC_CONST;
GtkWidget* level_bar_new(void);

void level_bar_set_bar_height(LevelBar *pbar, guint height);
void level_bar_set_fraction(LevelBar *pbar, gdouble fraction);

guint level_bar_get_bar_height(LevelBar *pbar);
gdouble level_bar_get_fraction(LevelBar *pbar);

void level_bar_set_value_type(LevelBar *pbar, enum BAR_VALUE bar_value);
enum BAR_VALUE level_bar_get_value_type(LevelBar *pbar);

void level_bar_set_shape(LevelBar *pbar, enum BAR_SHAPE bar_shape);
enum BAR_SHAPE level_bar_get_shape(LevelBar *pbar);

G_END_DECLS

#endif


