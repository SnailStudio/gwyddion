/* @(#) $Id$ */

#ifndef __GWY_APP_APP_H__
#define __GWY_APP_APP_H__

#include <gtk/gtkwidget.h>
#include <libgwyddion/gwycontainer.h>
#include <libgwydgets/gwydatawindow.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

GtkWidget *gwy_app_main_window;

GwyContainer*   gwy_app_get_current_data          (void);
GwyDataWindow*  gwy_app_data_window_get_current   (void);
void            gwy_app_data_window_set_current   (GwyDataWindow *data_window);
void            gwy_app_data_window_remove        (GwyDataWindow *data_window);
void            gwy_app_data_window_foreach       (GFunc func,
                                                   gpointer user_data);
GtkWidget*      gwy_app_data_window_create        (GwyContainer *data);
gint            gwy_app_data_window_set_untitled  (GwyDataWindow *data_window,
                                                   const gchar *templ);
void            gwy_app_quit                      (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GWY_APP_APP_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

