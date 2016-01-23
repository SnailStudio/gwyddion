/*
 *  @(#) $Id$
 *  Copyright (C) 2016 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/correct.h>
#include <libprocess/spline.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define STRAIGHTEN_RUN_MODES (GWY_RUN_INTERACTIVE)

#define PointXY GwyTriangulationPointXY

enum {
    RESPONSE_RESET = 1,
};

typedef struct {
    gint thickness;
    GwyInterpolationType interp;
    gdouble slackness;
    gboolean closed;
} StraightenArgs;

typedef struct {
    StraightenArgs *args;
    GtkWidget *dialogue;
    GtkWidget *view;
    GwyVectorLayer *vlayer;
    GwySelection *selection;
    GwyContainer *mydata;
    GtkObject *thickness;
    GtkObject *slackness;
    GtkWidget *closed;
    gdouble zoom;
} StraightenControls;

static gboolean      module_register         (void);
static void          straighten_path         (GwyContainer *data,
                                              GwyRunType run);
static gint          straighten_dialogue     (StraightenArgs *args,
                                              GwyContainer *data,
                                              GwyDataField *dfield,
                                              gint id,
                                              gint maxthickness);
static void          init_selection          (GwySelection *selection,
                                              GwyDataField *dfield,
                                              const StraightenArgs *args);
static void          path_selection_changed  (StraightenControls *controls);
static void          thickness_changed       (StraightenControls *controls,
                                              GtkAdjustment *adj);
static void          slackness_changed       (StraightenControls *controls,
                                              GtkAdjustment *adj);
static void          closed_changed          (StraightenControls *controls,
                                              GtkToggleButton *toggle);
static GwyDataField* straighten_do           (GwyDataField *dfield,
                                              GwyDataField *result,
                                              GwySelection *selection,
                                              const StraightenArgs *args,
                                              gboolean realsquare);
static void          straighten_load_args    (GwyContainer *container,
                                              StraightenArgs *args);
static void          straighten_save_args    (GwyContainer *container,
                                              StraightenArgs *args);
static void          straighten_sanitize_args(StraightenArgs *args);

static const StraightenArgs straighten_defaults = {
    1, GWY_INTERPOLATION_LINEAR,
    1.0/G_SQRT2, FALSE,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Extracts a straightened part of image along a curve."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_process_func_register("straighten_path",
                              (GwyProcessFunc)&straighten_path,
                              N_("/_Correct Data/Straighten _Path..."),
                              NULL,
                              STRAIGHTEN_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Straighten along a path"));

    return TRUE;
}

static void
straighten_path(GwyContainer *data, GwyRunType run)
{
    StraightenArgs args;
    GwyDataField *dfield;
    gint id, newid, maxthickness;

    g_return_if_fail(run & STRAIGHTEN_RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPath"));
    straighten_load_args(gwy_app_settings_get(), &args);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(dfield);

    maxthickness = MAX(gwy_data_field_get_xres(dfield),
                       gwy_data_field_get_yres(dfield))/2;
    maxthickness = MAX(maxthickness, 3);

    newid = straighten_dialogue(&args, data, dfield, id, maxthickness);
    straighten_save_args(gwy_app_settings_get(), &args);
    if (newid != -1)
        gwy_app_channel_log_add_proc(data, id, newid);
}

static gint
straighten_dialogue(StraightenArgs *args,
                    GwyContainer *data,
                    GwyDataField *dfield,
                    gint id,
                    gint maxthickness)
{
    GtkWidget *hbox, *alignment;
    GtkDialog *dialogue;
    GtkTable *table;
    StraightenControls controls;
    GwyDataField *result, *mask;
    gint response, row, newid = -1;
    GObject *selection;
    gchar selkey[40];

    gwy_clear(&controls, 1);
    controls.args = args;

    controls.dialogue = gtk_dialog_new_with_buttons(_("Straighten Path"),
                                                    NULL, 0, NULL);
    dialogue = GTK_DIALOG(controls.dialogue);
    gtk_dialog_add_button(dialogue, _("_Reset"), RESPONSE_RESET);
    gtk_dialog_add_button(dialogue, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(dialogue, GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(dialogue, GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialogue), GWY_HELP_DEFAULT);

    hbox = gtk_hbox_new(FALSE, 2);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialogue)->vbox), hbox,
                       FALSE, FALSE, 4);

    controls.mydata = gwy_container_new();
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    alignment = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 4);

    controls.view = create_preview(controls.mydata, 0, PREVIEW_SIZE, FALSE);
    controls.zoom = gwy_data_view_get_real_zoom(GWY_DATA_VIEW(controls.view));
    controls.selection = create_vector_layer(GWY_DATA_VIEW(controls.view),
                                             0, "Path", TRUE);
    g_object_ref(controls.selection);
    gwy_selection_set_max_objects(controls.selection, 1024);
    controls.vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(controls.view));
    g_signal_connect_swapped(controls.selection, "changed",
                             G_CALLBACK(path_selection_changed), &controls);

    g_snprintf(selkey, sizeof(selkey), "/%d/select/path", id);
    if (gwy_container_gis_object_by_name(data, selkey, &selection)
        && gwy_selection_get_data(GWY_SELECTION(selection), NULL) > 1) {
        gwy_serializable_clone(selection, G_OBJECT(controls.selection));
        g_object_get(selection,
                     "slackness", &args->slackness,
                     "closed", &args->closed,
                     NULL);
    }
    else
        init_selection(controls.selection, dfield, args);

    gtk_container_add(GTK_CONTAINER(alignment), controls.view);

    table = GTK_TABLE(gtk_table_new(3, 4, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_end(GTK_BOX(hbox), GTK_WIDGET(table), FALSE, FALSE, 0);
    row = 0;

    controls.thickness = gtk_adjustment_new(args->thickness, 3.0, maxthickness,
                                            1.0, 10.0, 0.0);
    gwy_table_attach_hscale(GTK_WIDGET(table), row, _("_Thickness:"), "px",
                            controls.thickness, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls.thickness, "value-changed",
                             G_CALLBACK(thickness_changed), &controls);
    row++;

    controls.slackness = gtk_adjustment_new(args->slackness, 0.0, G_SQRT2,
                                            0.001, 0.1, 0.0);
    gwy_table_attach_hscale(GTK_WIDGET(table), row, _("_Slackness:"), NULL,
                            controls.slackness, GWY_HSCALE_DEFAULT);
    g_signal_connect_swapped(controls.slackness, "value-changed",
                             G_CALLBACK(slackness_changed), &controls);
    row++;

    controls.closed = gtk_check_button_new_with_mnemonic(_("C_losed curve"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.closed),
                                 args->closed);
    gtk_table_attach(table, controls.closed,
                     0, 4, row, row+1, GTK_FILL, 0, 0, 0);
    g_signal_connect_swapped(controls.closed, "toggled",
                             G_CALLBACK(closed_changed), &controls);
    row++;

    gtk_widget_show_all(controls.dialogue);

    /* We do not get the right value before the data view is shown. */
    controls.zoom = gwy_data_view_get_real_zoom(GWY_DATA_VIEW(controls.view));
    g_object_set(controls.vlayer, "thickness",
                 GWY_ROUND(controls.zoom*args->thickness), NULL);

    do {
        response = gtk_dialog_run(dialogue);
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(controls.dialogue);
            case GTK_RESPONSE_NONE:
            goto finalize;
            break;

            case GTK_RESPONSE_OK:
            break;

            case RESPONSE_RESET:
            init_selection(controls.selection, dfield, NULL);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    result = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    mask = straighten_do(dfield, result, controls.selection, args, FALSE);

    newid = gwy_app_data_browser_add_data_field(result, data, TRUE);
    gwy_app_set_data_field_title(data, newid, _("Straightened"));
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    if (mask) {
        gwy_container_set_object(data,
                                 gwy_app_get_mask_key_for_id(newid), mask);
        g_object_unref(mask);
    }

    gtk_widget_destroy(controls.dialogue);

finalize:
    g_snprintf(selkey, sizeof(selkey), "/%d/select/path", id);
    selection = gwy_serializable_duplicate(G_OBJECT(controls.selection));
    gwy_container_set_object_by_name(data, selkey, selection);
    g_object_unref(selection);
    g_object_unref(controls.selection);
    g_object_unref(controls.mydata);

    return newid;
}

static void
init_selection(GwySelection *selection,
               GwyDataField *dfield,
               const StraightenArgs *args)
{
    gdouble xy[6];

    if (!args)
        args = &straighten_defaults;

    xy[0] = xy[2] = xy[4] = 0.5*dfield->xreal;
    xy[1] = 0.2*dfield->yreal;
    xy[3] = 0.5*dfield->yreal;
    xy[5] = 0.8*dfield->yreal;
    gwy_selection_set_data(selection, 3, xy);

    g_object_set(selection,
                 "slackness", args->slackness,
                 "closed", args->closed,
                 NULL);
}

static void
path_selection_changed(StraightenControls *controls)
{
    gint n;

    n = gwy_selection_get_data(controls->selection, NULL);
    g_print("Curve has %d points now.\n", n);
}

static void
thickness_changed(StraightenControls *controls, GtkAdjustment *adj)
{
    StraightenArgs *args = controls->args;

    args->thickness = gwy_adjustment_get_int(adj);
    g_object_set(controls->vlayer, "thickness",
                 GWY_ROUND(controls->zoom*args->thickness), NULL);
}

static void
slackness_changed(StraightenControls *controls, GtkAdjustment *adj)
{
    StraightenArgs *args = controls->args;
    gdouble slackness;

    args->slackness = gtk_adjustment_get_value(adj);
    g_object_get(controls->selection, "slackness", &slackness, NULL);
    if (args->slackness != slackness)
        g_object_set(controls->selection, "slackness", args->slackness, NULL);
}

static void
closed_changed(StraightenControls *controls, GtkToggleButton *toggle)
{
    StraightenArgs *args = controls->args;
    gboolean closed;

    args->closed = gtk_toggle_button_get_active(toggle);
    g_object_get(controls->selection, "closed", &closed, NULL);
    if (!args->closed != !closed)
        g_object_set(controls->selection, "closed", args->closed, NULL);
}

static GwyDataField*
straighten_do(GwyDataField *dfield, GwyDataField *result,
              GwySelection *selection,
              const StraightenArgs *args, gboolean realsquare)
{
    GwyDataField *mask;
    GwySpline *spline;
    PointXY *points, *tangents;
    GwyInterpolationType interp;
    gdouble dx, dy, h, length;
    guint n, i, j, thickness;
    gboolean have_exterior = FALSE;
    gint xres, yres;
    gdouble *d, *m;

    n = gwy_selection_get_data(selection, NULL);
    if (n < 2)
        return NULL;

    /* TODO: Actually support realsquare. */
    dx = gwy_data_field_get_xmeasure(dfield);
    dy = gwy_data_field_get_ymeasure(dfield);
    h = MIN(dx, dy);

    points = g_new(PointXY, n);
    for (i = 0; i < n; i++) {
        gdouble xy[2];

        gwy_selection_get_object(selection, i, xy);
        points[i].x = xy[0]/dx;
        points[i].y = xy[1]/dy;
    }
    spline = gwy_spline_new_from_points(points, n);
    /* Assume args and selection agree on the parameters... */
    gwy_spline_set_closed(spline, args->closed);
    gwy_spline_set_slackness(spline, args->slackness);
    g_free(points);

    length = gwy_spline_length(spline);
    /* This would give natural sampling for a straight line along some axis. */
    n = GWY_ROUND(length + 1.0);
    points = g_new(PointXY, n);
    tangents = g_new(PointXY, n);

    thickness = args->thickness;
    interp = args->interp; // TODO: Fix non-interpolating bases.
    gwy_data_field_resample(result, thickness, n, GWY_INTERPOLATION_NONE);
    gwy_data_field_set_xreal(result, h*thickness);
    gwy_data_field_set_yreal(result, h*n);
    gwy_data_field_set_xoffset(result, 0.0);
    gwy_data_field_set_yoffset(result, 0.0);
    gwy_serializable_clone(G_OBJECT(gwy_data_field_get_si_unit_xy(dfield)),
                           G_OBJECT(gwy_data_field_get_si_unit_xy(result)));
    gwy_serializable_clone(G_OBJECT(gwy_data_field_get_si_unit_z(dfield)),
                           G_OBJECT(gwy_data_field_get_si_unit_z(result)));
    d = gwy_data_field_get_data(result);

    mask = gwy_data_field_new_alike(result, TRUE);
    m = gwy_data_field_get_data(mask);

    gwy_spline_sample_uniformly(spline, points, tangents, n);
    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    for (i = 0; i < n; i++) {
        gdouble xc = points[i].x, yc = points[i].y;
        gdouble vx = tangents[i].y, vy = -tangents[i].x;
        /* TODO: Handle zero (undefined) tangents. */

        for (j = 0; j < thickness; j++) {
            gdouble x = xc + (j + 0.5 - 0.5*thickness)*vx;
            gdouble y = yc + (j + 0.5 - 0.5*thickness)*vy;

            if (x >= 0.0 && x <= xres && y >= 0.0 && y <= yres) {
                d[i*thickness + j] = gwy_data_field_get_dval(dfield, x, y,
                                                             interp);
            }
            else {
                m[i*thickness + j] = 1.0;
                have_exterior = TRUE;
            }
        }
    }
    g_free(points);
    g_free(tangents);

    if (have_exterior)
        gwy_data_field_correct_average_unmasked(result, mask);
    else
        gwy_object_unref(mask);

    return mask;
}

static const gchar closed_key[]    = "/module/straighten_path/closed";
static const gchar slackness_key[] = "/module/straighten_path/slackness";
static const gchar thickness_key[] = "/module/straighten_path/thickness";

static void
straighten_sanitize_args(StraightenArgs *args)
{
    /* Upper limit is set based on image dimensions. */
    args->thickness = MAX(args->thickness, 3);
    args->slackness = CLAMP(args->slackness, 0.0, G_SQRT2);
    args->closed = !!args->closed;
}

static void
straighten_load_args(GwyContainer *container,
                     StraightenArgs *args)
{
    *args = straighten_defaults;

    gwy_container_gis_int32_by_name(container, thickness_key, &args->thickness);
    gwy_container_gis_double_by_name(container, slackness_key,
                                     &args->slackness);
    gwy_container_gis_boolean_by_name(container, closed_key, &args->closed);

    straighten_sanitize_args(args);
}

static void
straighten_save_args(GwyContainer *container,
                     StraightenArgs *args)
{
    gwy_container_set_int32_by_name(container, thickness_key, args->thickness);
    gwy_container_set_double_by_name(container, slackness_key, args->slackness);
    gwy_container_set_boolean_by_name(container, closed_key, args->closed);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */