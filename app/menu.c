/*
 *  @(#) $Id$
 *  Copyright (C) 2003-2004 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111 USA
 */

/* FIXME: most of this file belongs to gwyddion, not libgwyapp.
 * - menu constructors should be in gwyddion
 * - the menu sensitivity stuff should be in libgwyapp
 * - last-run function stuff should be in ???
 */

#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwydgets/gwytoolbox.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwymodule/gwymodule-file.h>
#include <libgwymodule/gwymodulebrowser.h>
#include "app.h"
#include "file.h"
#include "menu.h"

#define set_sensitive(item, flags) \
    g_object_set_qdata(G_OBJECT(item), sensitive_key, \
                       GUINT_TO_POINTER(flags))
#define set_sensitive_state(item, state) \
    g_object_set_qdata(G_OBJECT(item), sensitive_state_key, \
                       GUINT_TO_POINTER(state))
#define set_sensitive_both(item, flags, state) \
    do { \
        set_sensitive(item, flags); \
        set_sensitive_state(item, state); \
    } while (0)

enum {
    THUMBNAIL_SIZE = 16
};

static void       gwy_app_update_last_process_func  (GtkWidget *menu,
                                                     const gchar *name);
static void       setup_sensitivity_keys            (void);
static gchar*     fix_recent_file_underscores       (gchar *s);
static void       gwy_option_menu_data_window_append(GwyDataWindow *data_window,
                                                     GtkWidget *menu);
static GtkWidget* find_label_of_repeat_last_item    (GtkWidget *menu,
                                                     const gchar *key);

static GQuark sensitive_key = 0;
static GQuark sensitive_state_key = 0;
static GQuark omenu_data_window_key = 0;
static GQuark omenu_data_window_id_key = 0;

static GtkWidget *recent_files_menu = NULL;

/**
 * gwy_app_menu_set_sensitive_array:
 * @item_factory: A item factory to obtain menu items from.
 * @root: Menu root, without "<" and ">".
 * @items: %NULL-terminated array of item paths in the menu (without the
 *         root).
 * @flags: Sensitivity bits describing when the item should be sensitive.
 *
 * Sets sensitivity flags for a list of menu items.
 **/
void
gwy_app_menu_set_sensitive_array(GtkItemFactory *item_factory,
                                 const gchar *root,
                                 const gchar **items,
                                 GwyMenuSensFlags flags)
{
    GtkWidget *item;
    gsize i, len, maxlen;
    gchar *path;

    setup_sensitivity_keys();

    g_return_if_fail(GTK_IS_ITEM_FACTORY(item_factory));
    g_return_if_fail(root);
    g_return_if_fail(items);

    maxlen = 0;
    for (i = 0; items[i]; i++) {
        len = strlen(items[i]);
        if (len > maxlen)
            maxlen = len;
    }

    len = strlen(root);
    path = g_new(gchar, maxlen + len + 3);
    strcpy(path + 1, root);
    path[0] = '<';
    path[len+1] = '>';
    for (i = 0; items[i]; i++) {
        strcpy(path + len + 2, items[i]);
        item = gtk_item_factory_get_item(item_factory, path);
        set_sensitive(item, flags);
    }
    g_free(path);
}

/**
 * gwy_app_menu_set_sensitive_recursive:
 * @widget: A menu widget (a menu bar, menu, or an item).
 * @data: Sensitivity data.
 *
 * Sets sensitivity bits and current state of a menu subtree at @widget
 * according @data.
 **/
void
gwy_app_menu_set_sensitive_recursive(GtkWidget *widget,
                                     GwyMenuSensData *data)
{
    GObject *obj;
    guint i, j;

    setup_sensitivity_keys();

    obj = G_OBJECT(widget);
    /*gwy_debug("%s", g_type_name(G_TYPE_FROM_INSTANCE(obj)));*/
    i = GPOINTER_TO_UINT(g_object_get_qdata(obj, sensitive_key));
    /* if there are any relevant flags */
    if (i & data->flags) {
        j = GPOINTER_TO_UINT(g_object_get_qdata(obj, sensitive_state_key));
        /* clear all data->flags bits in state
         * and set all data->set_to bits in state */
        j = (j & ~data->flags) | (data->set_to & data->flags);
        set_sensitive_state(obj, j);
        /* make widget sensitive if all conditions are met */
        gtk_widget_set_sensitive(widget, (j & i) == i);

    }
    if (GTK_IS_ALIGNMENT(widget)
        || GTK_IS_MENU_BAR(widget)
        || GWY_IS_TOOLBOX(widget)) {
        gtk_container_foreach(GTK_CONTAINER(widget),
                              (GtkCallback)gwy_app_menu_set_sensitive_recursive,
                              data);
    }
    else if (GTK_IS_MENU_ITEM(widget)
             && (widget = gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget))))
        gtk_container_foreach(GTK_CONTAINER(widget),
                              (GtkCallback)gwy_app_menu_set_sensitive_recursive,
                              data);
}

/* Changed in rev 1.52: use logical OR with existing flags.  There is no
 * way to clear existing flags. */
void
gwy_app_menu_set_flags_recursive(GtkWidget *widget,
                                 GwyMenuSensData *data)
{
    setup_sensitivity_keys();

    if (!GTK_IS_TEAROFF_MENU_ITEM(widget)) {
        GwyMenuSensFlags flags, state;
        GObject *obj;

        obj = G_OBJECT(widget);
        flags = GPOINTER_TO_UINT(g_object_get_qdata(obj, sensitive_key));
        state = GPOINTER_TO_UINT(g_object_get_qdata(obj, sensitive_state_key));
        set_sensitive_both(widget, data->flags | flags, data->set_to | state);
    }

    if (GTK_IS_ALIGNMENT(widget)
        || GTK_IS_MENU_BAR(widget)
        || GWY_IS_TOOLBOX(widget)) {
        gtk_container_foreach(GTK_CONTAINER(widget),
                              (GtkCallback)gwy_app_menu_set_flags_recursive,
                              data);
    }
    else if (GTK_IS_MENU_ITEM(widget)) {
        if ((widget = gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget))))
            gtk_container_foreach(GTK_CONTAINER(widget),
                                  (GtkCallback)gwy_app_menu_set_flags_recursive,
                                  data);
    }
}

/**
 * gwy_app_menu_set_sensitive_both:
 * @item: A menu item.
 * @flags: Sensitivity bits describing when the item should be sensitive.
 * @state: Current state bits determining whether it's actually sensitive
 *         or not.
 *
 * Sets both senstitivity data and current state for a menu item.
 **/
void
gwy_app_menu_set_sensitive_both(GtkWidget *item,
                                GwyMenuSensFlags flags,
                                GwyMenuSensFlags state)
{
    set_sensitive_both(item, flags, state);
}


static void
setup_sensitivity_keys(void)
{
    if (!sensitive_key)
        sensitive_key = g_quark_from_static_string("sensitive");
    if (!sensitive_state_key)
        sensitive_state_key = g_quark_from_static_string("sensitive-state");
}

void
gwy_app_run_process_func_cb(gchar *name)
{
    GwyRunType run_types[] = {
        GWY_RUN_INTERACTIVE, GWY_RUN_MODAL,
        GWY_RUN_NONINTERACTIVE, GWY_RUN_WITH_DEFAULTS,
    };
    GwyMenuSensData sens_data = {
        GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_LAST_PROC
            | GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA_SHOW,
        GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_LAST_PROC
    };
    GwyRunType run;
    GwyDataWindow *data_window;
    GtkWidget *data_view, *menu;
    GwyContainer *data;
    gsize i;
    gboolean ok;

    gwy_debug("`%s'", name);
    data_window = gwy_app_data_window_get_current();
    g_return_if_fail(data_window);
    data_view = gwy_data_window_get_data_view(data_window);
    data = gwy_data_view_get_data(GWY_DATA_VIEW(data_view));
    g_return_if_fail(data);
    run = gwy_process_func_get_run_types(name);
    for (i = 0; i < G_N_ELEMENTS(run_types); i++) {
        if (!(run & run_types[i]))
            continue;
        ok = gwy_process_func_run(name, data, run_types[i]);

        /* update the menus regardless the function returns TRUE or not.
         * functions changing nothing would never appear in the last-used
         * menu and/or it would never become sensitive */
        menu = GTK_WIDGET(g_object_get_data(G_OBJECT(gwy_app_main_window_get()),
                                            "<proc>"));
        gwy_app_update_last_process_func(menu, name);
        /* FIXME: the ugliest hack! */
        if (ok)
            gwy_app_data_view_update(data_view);

        /* re-get current data window, it may have changed */
        data_window = gwy_app_data_window_get_current();
        data = gwy_data_window_get_data(GWY_DATA_WINDOW(data_window));
        if (gwy_container_contains_by_name(data, "/0/mask"))
            sens_data.set_to |= GWY_MENU_FLAG_DATA_MASK;
        if (gwy_container_contains_by_name(data, "/0/show"))
            sens_data.set_to |= GWY_MENU_FLAG_DATA_SHOW;
        gwy_app_menu_set_sensitive_recursive(menu, &sens_data);

        return;
    }
    g_critical("Trying to run `%s', but no run mode found (%d)", name, run);
}

static void
gwy_app_update_last_process_func(GtkWidget *menu,
                                 const gchar *name)
{
    static GtkWidget *repeat_label = NULL;
    static GtkWidget *reshow_label = NULL;
    const gchar *menu_path;
    gsize len;
    gchar *s, *mp;

    g_object_set_data(G_OBJECT(menu), "last-func", (gpointer)name);
    if (!repeat_label)
        repeat_label = find_label_of_repeat_last_item(menu, "run-last-item");
    if (!reshow_label)
        reshow_label = find_label_of_repeat_last_item(menu, "show-last-item");

    menu_path = gwy_process_func_get_menu_path(name);
    menu_path = strrchr(menu_path, '/');
    g_assert(menu_path);
    menu_path++;
    len = strlen(menu_path);
    if (g_str_has_suffix(menu_path, "..."))
        len -= 3;
    mp = gwy_strkill(g_strndup(menu_path, len), "_");

    if (repeat_label) {
        s = g_strconcat(_("Repeat Last"), " (", mp, ")", NULL);
        gtk_label_set_text_with_mnemonic(GTK_LABEL(repeat_label), s);
        g_free(s);
    }

    if (reshow_label) {
        s = g_strconcat(_("Re-show Last"), " (", mp, ")", NULL);
        gtk_label_set_text_with_mnemonic(GTK_LABEL(reshow_label), s);
        g_free(s);
    }

    g_free(mp);
}

/* Find the "run-last-item" menu item
 * FIXME: this is fragile */
static GtkWidget*
find_label_of_repeat_last_item(GtkWidget *menu,
                               const gchar *key)
{
    GtkWidget *item;
    GQuark quark;
    GList *l;

    quark = g_quark_from_string(key);
    while (GTK_IS_BIN(menu))
        menu = GTK_BIN(menu)->child;
    item = GTK_WIDGET(GTK_MENU_SHELL(menu)->children->data);
    menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
    for (l = GTK_MENU_SHELL(menu)->children; l; l = g_list_next(l)) {
        if (g_object_get_qdata(G_OBJECT(l->data), quark))
            break;
    }
    if (!l) {
        g_warning("Cannot find `%s' menu item", key);
        return NULL;
    }

    item = GTK_WIDGET(l->data);
    return GTK_BIN(item)->child;
}

void
gwy_app_run_graph_func_cb(gchar *name)
{
    GtkWidget *graph_window, *graph;

    gwy_debug("`%s'", name);
    graph_window = gwy_app_graph_window_get_current();
    g_return_if_fail(graph_window);
    graph = GTK_BIN(graph_window)->child;
    g_return_if_fail(GWY_IS_GRAPH(graph));
    gwy_graph_func_run(name, GWY_GRAPH(graph));
    /* FIXME TODO: some equivalent of this:
    gwy_app_data_view_update(data_view);
    */
}

void
gwy_app_menu_recent_files_update(GList *recent_files)
{
    GtkWidget *item;
    GQuark quark;
    GList *l, *child;
    gchar *s, *label, *filename;
    gint i;

    g_return_if_fail(GTK_IS_MENU(recent_files_menu));
    child = GTK_MENU_SHELL(recent_files_menu)->children;
    if (GTK_IS_TEAROFF_MENU_ITEM(child->data))
        child = g_list_next(child);

    quark = g_quark_from_string("filename");
    for (i = 0, l = recent_files;
         l && i < gwy_app_n_recent_files;
         l = g_list_next(l), i++) {
        filename = (gchar*)l->data;
        s = fix_recent_file_underscores(g_path_get_basename(filename));
        label = g_strdup_printf("%s%d. %s", i < 10 ? "_" : "", i, s);
        if (child) {
            item = GTK_BIN(child->data)->child;
            gwy_debug("reusing item %p for <%s> [#%d]", item, s, i);
            gtk_label_set_text_with_mnemonic(GTK_LABEL(item), label);
            g_object_set_qdata_full(G_OBJECT(child->data), quark,
                                    g_strdup(filename), g_free);
            child = g_list_next(child);
        }
        else {
            item = gtk_menu_item_new_with_mnemonic(label);
            gwy_debug("creating item %p for <%s> [#%d]", item, s, i);
            g_object_set_qdata_full(G_OBJECT(item), quark, g_strdup(filename),
                                    g_free);
            gtk_menu_shell_append(GTK_MENU_SHELL(recent_files_menu), item);
            g_signal_connect(item, "activate",
                             G_CALLBACK(gwy_app_file_open_recent_cb), NULL);
            gtk_widget_show(item);
        }
        g_free(label);
        g_free(s);
    }
}

static gchar*
fix_recent_file_underscores(gchar *s)
{
    gchar *s2;

    s2 = gwy_strreplace(s, "_", "__", (gsize)-1);
    g_free(s);

    return s2;
}

void
gwy_app_menu_set_recent_files_menu(GtkWidget *menu)
{
    g_return_if_fail(GTK_IS_MENU(menu));
    g_return_if_fail(!recent_files_menu);

    recent_files_menu = menu;
}

/**
 * gwy_app_toolbox_update_state:
 * @sens_data: Menu sensitivity data.
 *
 * Updates menus and toolbox sensititivity to reflect @sens_data.
 **/
void
gwy_app_toolbox_update_state(GwyMenuSensData *sens_data)
{
    GSList *l;
    GObject *obj;

    gwy_debug("{%d, %d}", sens_data->flags, sens_data->set_to);

    /* FIXME: this actually belongs to toolbox.c; however
    * gwy_app_toolbox_update_state() is called from gwy_app_data_view_update()
    * so libgwyapp would depend on gwyddion instead the other way around */
    obj = G_OBJECT(gwy_app_main_window_get());

    for (l = g_object_get_data(obj, "menus"); l; l = g_slist_next(l))
        gtk_container_foreach(GTK_CONTAINER(l->data),
                              (GtkCallback)gwy_app_menu_set_sensitive_recursive,
                              sens_data);

    for (l = g_object_get_data(obj, "toolbars"); l; l = g_slist_next(l))
        gwy_app_menu_set_sensitive_recursive(GTK_WIDGET(l->data),
                                             sens_data);
}

/**
 * gwy_option_menu_data_window:
 * @callback: A callback called when a menu item is activated (or %NULL for
 *            no callback).
 * @cbdata: User data passed to the callback.
 * @none_label: Label to use for `none' menu item.  If it is %NULL, no `none'
 *              item is created, if it is empty, a default label is used.
 * @current: Data window to be shown as currently selected.
 *
 * Creates an option menu of existing data windows, with thumbnails.
 *
 * It sets object data "data-window" to data window for each menu item.
 * Note the menu is static and does NOT react to creation or closing of
 * data windows.
 *
 * Returns: The newly created option menu as a #GtkWidget.
 *
 * Since: 1.2.
 **/
GtkWidget*
gwy_option_menu_data_window(GCallback callback,
                            gpointer cbdata,
                            const gchar *none_label,
                            GtkWidget *current)
{
    GtkWidget *omenu, *menu, *item;
    GList *c;

    if (!omenu_data_window_key)
        omenu_data_window_key = g_quark_from_static_string("data-window");
    if (!omenu_data_window_id_key)
        omenu_data_window_id_key
            = g_quark_from_static_string("gwy-option-menu-data-window");

    omenu = gtk_option_menu_new();
    g_object_set_qdata(G_OBJECT(omenu), omenu_data_window_id_key,
                       GINT_TO_POINTER(TRUE));
    menu = gtk_menu_new();
    gwy_app_data_window_foreach((GFunc)gwy_option_menu_data_window_append,
                                menu);
    if (none_label) {
        if (!*none_label)
            none_label = _("(none)");
        item = gtk_menu_item_new_with_label(none_label);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_option_menu_set_menu(GTK_OPTION_MENU(omenu), menu);
    if (current || none_label)
        gwy_option_menu_data_window_set_history(omenu, current);

    if (callback) {
        for (c = GTK_MENU_SHELL(menu)->children; c; c = g_list_next(c))
            g_signal_connect(c->data, "activate", callback, cbdata);
    }

    return omenu;
}

static void
gwy_option_menu_data_window_append(GwyDataWindow *data_window,
                                   GtkWidget *menu)
{
    GtkWidget *item, *data_view, *image;
    GdkPixbuf *pixbuf;
    gchar *filename;

    data_view = gwy_data_window_get_data_view(data_window);
    filename = gwy_data_window_get_base_name(data_window);

    pixbuf = gwy_data_view_get_thumbnail(GWY_DATA_VIEW(data_view),
                                         THUMBNAIL_SIZE);
    image = gtk_image_new_from_pixbuf(pixbuf);
    gwy_object_unref(pixbuf);
    item = gtk_image_menu_item_new_with_label(filename);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_object_set_qdata(G_OBJECT(item), omenu_data_window_key, data_window);
    g_free(filename);
}

/**
 * gwy_option_menu_data_window_set_history:
 * @option_menu: An option menu created by gwy_option_menu_data_window().
 * @current: Data window to be shown as currently selected.
 *
 * Sets data window option menu history to a specific data window.
 *
 * Returns: %TRUE if the history was set, %FALSE if @current was not found.
 *
 * Since: 1.2.
 **/
gboolean
gwy_option_menu_data_window_set_history(GtkWidget *option_menu,
                                        GtkWidget *current)
{
    GtkWidget *menu;
    GtkOptionMenu *omenu;
    GList *c;
    gint i;

    g_return_val_if_fail(GTK_IS_OPTION_MENU(option_menu), FALSE);
    g_return_val_if_fail(g_object_get_qdata(G_OBJECT(option_menu),
                                            omenu_data_window_id_key), FALSE);

    omenu = GTK_OPTION_MENU(option_menu);
    g_assert(omenu);
    menu = gtk_option_menu_get_menu(omenu);
    i = 0;
    for (c = GTK_MENU_SHELL(menu)->children; c; c = g_list_next(c)) {
        if (g_object_get_qdata(G_OBJECT(c->data), omenu_data_window_key)
            == current) {
            gtk_option_menu_set_history(omenu, i);
            return TRUE;
        }
        i++;
    }

    return FALSE;
}

/**
 * gwy_option_menu_data_window_get_history:
 * @option_menu: An option menu created by gwy_option_menu_data_window().
 *
 * Gets the currently selected data window in a data window option menu.
 *
 * Returns: The currently selected data window (may be %NULL if `none' is
 *          selected).
 *
 * Since: 1.2.
 **/
GtkWidget*
gwy_option_menu_data_window_get_history(GtkWidget *option_menu)
{
    GList *c;
    GtkWidget *menu, *item;
    GtkOptionMenu *omenu;
    gint idx;

    g_return_val_if_fail(GTK_IS_OPTION_MENU(option_menu), NULL);
    g_return_val_if_fail(g_object_get_qdata(G_OBJECT(option_menu),
                                            omenu_data_window_id_key), NULL);

    omenu = GTK_OPTION_MENU(option_menu);
    g_assert(omenu);
    idx = gtk_option_menu_get_history(omenu);
    if (idx < 0)
        return NULL;
    menu = gtk_option_menu_get_menu(omenu);
    c = g_list_nth(GTK_MENU_SHELL(menu)->children, (guint)idx);
    g_return_val_if_fail(c, NULL);
    item = GTK_WIDGET(c->data);

    return (GtkWidget*)g_object_get_qdata(G_OBJECT(item),
                                          omenu_data_window_key);
}

/***** Documentation *******************************************************/

/**
 * GwyMenuSensitivityFlags:
 * @GWY_MENU_FLAG_DATA: There's at least a one data window present.
 * @GWY_MENU_FLAG_UNDO: There's something to undo (for current data window).
 * @GWY_MENU_FLAG_REDO: There's something to redo (for current data window).
 * @GWY_MENU_FLAG_GRAPH: There's at least a one graph window present.
 * @GWY_MENU_FLAG_LAST_PROC: There is a last-run data processing function
 *                           to rerun.
 * @GWY_MENU_FLAG_LAST_GRAPH: There is a last-run graph function to rerun.
 * @GWY_MENU_FLAG_DATA_MASK: There is a mask on the data.
 * @GWY_MENU_FLAG_DATA_SHOW: There is a presentation on the data.
 * @GWY_MENU_FLAG_MASK: All the bits combined.
 *
 * Menu sensitivity flags.
 *
 * They represent various application states that may be preconditions for
 * some menu item (or other widget) to become sensitive.
 **/

/**
 * GwyMenuSensData:
 * @flags: The flags that have to be set for a widget to become sensitive.
 * @set_to: The actually set flags.
 *
 * Sensitivity flags and their current state in one struct.
 *
 * All widget bits have to be set to make it sensitive.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
