/*
 *  @(#) $Id$
 *  Copyright (C) 2003-2016 David Necas (Yeti), Petr Klapetek.
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/datafield.h>
#include <libprocess/stats.h>
#include <libprocess/stats_uncertainty.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define ENTROPY_NORMAL 1.41893853320467274178l

#define GWY_TYPE_TOOL_STATS            (gwy_tool_stats_get_type())
#define GWY_TOOL_STATS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_STATS, GwyToolStats))
#define GWY_IS_TOOL_STATS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_STATS))
#define GWY_TOOL_STATS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_STATS, GwyToolStatsClass))

typedef struct _GwyToolStats      GwyToolStats;
typedef struct _GwyToolStatsClass GwyToolStatsClass;

typedef struct {
    GwyMaskingType masking;
    gboolean instant_update;
} ToolArgs;

typedef struct {
    gdouble sel[4];
    gint isel[4];

    gdouble min;
    gdouble max;
    gdouble avg;
    gdouble median;
    gdouble ra;
    gdouble rms;
    gdouble rms_gw;
    gdouble skew;
    gdouble kurtosis;
    gdouble area;
    gdouble projarea;
    gdouble var;
    gdouble entropy;
    gdouble entropydef;
    /* These two are in degrees as we use need only for the user. */
    gdouble theta;
    gdouble phi;

    gdouble umin;
    gdouble umax;
    gdouble uavg;
    gdouble umedian;
    gdouble ura;
    gdouble urms;
    gdouble uskew;
    gdouble ukurtosis;
    gdouble uprojarea;
    gdouble utheta;
    gdouble uphi;
} ToolResults;

typedef struct {
    ToolResults results;
    gboolean masking;
    gboolean same_units;
    GwyContainer *container;
    GwyDataField *data_field;
    GwySIValueFormat *angle_format;
    gint id;
} ToolReportData;

struct _GwyToolStats {
    GwyPlainTool parent_instance;

    ToolArgs args;
    ToolResults results;
    gboolean results_up_to_date;

    GwyRectSelectionLabels *rlabels;
    GtkWidget *update;
    GtkBox *aux_box;
    GtkWidget *copy;
    GtkWidget *save;

    GtkWidget *min;
    GtkWidget *max;
    GtkWidget *avg;
    GtkWidget *median;
    GtkWidget *ra;
    GtkWidget *rms;
    GtkWidget *rms_gw;
    GtkWidget *skew;
    GtkWidget *kurtosis;
    GtkWidget *area;
    GtkWidget *projarea;
    GtkWidget *var;
    GtkWidget *entropy;
    GtkWidget *entropydef;
    GtkWidget *theta;
    GtkWidget *phi;

    GSList *masking;
    GtkWidget *instant_update;

    GwySIValueFormat *zrange_format;
    GwySIValueFormat *rms_format;
    GwySIValueFormat *parea_format;
    GwySIValueFormat *sarea_format;
    GwySIValueFormat *var_format;
    GwySIValueFormat *angle_format;

    gboolean same_units;

    gboolean has_calibration;
    GwyDataField *xunc;
    GwyDataField *yunc;
    GwyDataField *zunc;

    /* potential class data */
    GType layer_type_rect;
};

struct _GwyToolStatsClass {
    GwyPlainToolClass parent_class;
};

static gboolean module_register(void);

static GType      gwy_tool_stats_get_type              (void)                      G_GNUC_CONST;
static void       gwy_tool_stats_finalize              (GObject *object);
static void       gwy_tool_stats_init_dialog           (GwyToolStats *tool);
static GtkWidget* gwy_tool_stats_add_aux_button        (GwyToolStats *tool,
                                                        const gchar *stock_id,
                                                        const gchar *tooltip);
static void       gwy_tool_stats_data_switched         (GwyTool *gwytool,
                                                        GwyDataView *data_view);
static void       gwy_tool_stats_data_changed          (GwyPlainTool *plain_tool);
static void       gwy_tool_stats_mask_changed          (GwyPlainTool *plain_tool);
static void       gwy_tool_stats_response              (GwyTool *tool,
                                                        gint response_id);
static void       gwy_tool_stats_selection_changed     (GwyPlainTool *plain_tool,
                                                        gint hint);
static void       gwy_tool_stats_update_labels         (GwyToolStats *tool);
static gboolean   gwy_tool_stats_calculate             (GwyToolStats *tool);
static void       calculate_uncertainties              (GwyToolStats *tool,
                                                        GwyDataField *field,
                                                        GwyDataField *mask,
                                                        GwyMaskingType masking,
                                                        guint nn,
                                                        const gint *isel,
                                                        gdouble w,
                                                        gdouble h);
static void       gwy_tool_stats_update_units          (GwyToolStats *tool);
static void       update_label                         (GwySIValueFormat *units,
                                                        GtkWidget *label,
                                                        gdouble value);
static void       update_label_unc                     (GwySIValueFormat *units,
                                                        GtkWidget *label,
                                                        gdouble value,
                                                        gdouble uncertainty);
static void       gwy_tool_stats_masking_changed       (GtkWidget *button,
                                                        GwyToolStats *tool);
static void       gwy_tool_stats_instant_update_changed(GtkToggleButton *check,
                                                        GwyToolStats *tool);
static void       gwy_tool_stats_save                  (GwyToolStats *tool);
static void       gwy_tool_stats_copy                  (GwyToolStats *tool);
static gchar*     gwy_tool_stats_create_report         (gpointer user_data,
                                                        gssize *data_len);

/* NB: The order of the values is important for create_report()! */
static struct {
    const gchar *name;
    gsize offset;
}
const values[] = {
    { N_("Minimum:"),          G_STRUCT_OFFSET(GwyToolStats, min),        },
    { N_("Maximum:"),          G_STRUCT_OFFSET(GwyToolStats, max),        },
    { N_("Average value:"),    G_STRUCT_OFFSET(GwyToolStats, avg),        },
    { N_("Median:"),           G_STRUCT_OFFSET(GwyToolStats, median),     },
    { N_("Ra (Sa):"),          G_STRUCT_OFFSET(GwyToolStats, ra),         },
    { N_("Rms (Sq):"),         G_STRUCT_OFFSET(GwyToolStats, rms),        },
    { N_("Rms (grain-wise):"), G_STRUCT_OFFSET(GwyToolStats, rms_gw),     },
    { N_("Skew:"),             G_STRUCT_OFFSET(GwyToolStats, skew),       },
    { N_("Kurtosis:"),         G_STRUCT_OFFSET(GwyToolStats, kurtosis),   },
    { N_("Surface area:"),     G_STRUCT_OFFSET(GwyToolStats, area),       },
    { N_("Projected area:"),   G_STRUCT_OFFSET(GwyToolStats, projarea),   },
    { N_("Variation:"),        G_STRUCT_OFFSET(GwyToolStats, var),        },
    { N_("Entropy:"),          G_STRUCT_OFFSET(GwyToolStats, entropy),    },
    { N_("Entropy deficit:"),  G_STRUCT_OFFSET(GwyToolStats, entropydef), },
    { N_("Inclination θ:"),    G_STRUCT_OFFSET(GwyToolStats, theta),      },
    { N_("Inclination φ:"),    G_STRUCT_OFFSET(GwyToolStats, phi),        },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Statistics tool."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.18",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

static const gchar instant_update_key[] = "/module/stats/instant_update";
static const gchar masking_key[]        = "/module/stats/masking";

static const ToolArgs default_args = {
    FALSE,
    GWY_MASK_IGNORE,
};

GWY_MODULE_QUERY(module_info)

G_DEFINE_TYPE(GwyToolStats, gwy_tool_stats, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_STATS);

    return TRUE;
}

static void
gwy_tool_stats_class_init(GwyToolStatsClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_stats_finalize;

    tool_class->stock_id = GWY_STOCK_STAT_QUANTITIES;
    tool_class->title = _("Statistical Quantities");
    tool_class->tooltip = _("Statistical quantities");
    tool_class->prefix = "/module/stats";
    tool_class->data_switched = gwy_tool_stats_data_switched;
    tool_class->response = gwy_tool_stats_response;

    ptool_class->data_changed = gwy_tool_stats_data_changed;
    ptool_class->mask_changed = gwy_tool_stats_mask_changed;
    ptool_class->selection_changed = gwy_tool_stats_selection_changed;
}

static void
gwy_tool_stats_finalize(GObject *object)
{
    GwyToolStats *tool;
    GwyContainer *settings;

    tool = GWY_TOOL_STATS(object);

    settings = gwy_app_settings_get();
    gwy_container_set_enum_by_name(settings, masking_key, tool->args.masking);
    gwy_container_set_boolean_by_name(settings, instant_update_key,
                                      tool->args.instant_update);

    GWY_SI_VALUE_FORMAT_FREE(tool->zrange_format);
    GWY_SI_VALUE_FORMAT_FREE(tool->rms_format);
    GWY_SI_VALUE_FORMAT_FREE(tool->angle_format);
    GWY_SI_VALUE_FORMAT_FREE(tool->parea_format);
    GWY_SI_VALUE_FORMAT_FREE(tool->sarea_format);
    GWY_SI_VALUE_FORMAT_FREE(tool->var_format);

    G_OBJECT_CLASS(gwy_tool_stats_parent_class)->finalize(object);
}

static void
gwy_tool_stats_init(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool;
    GwyContainer *settings;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->layer_type_rect = gwy_plain_tool_check_layer_type(plain_tool,
                                                           "GwyLayerRectangle");
    if (!tool->layer_type_rect)
        return;

    plain_tool->lazy_updates = TRUE;
    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;

    settings = gwy_app_settings_get();
    tool->args = default_args;
    gwy_container_gis_enum_by_name(settings, masking_key, &tool->args.masking);
    gwy_container_gis_boolean_by_name(settings, instant_update_key,
                                      &tool->args.instant_update);

    tool->args.masking = gwy_enum_sanitize_value(tool->args.masking,
                                                 GWY_TYPE_MASKING_TYPE);

    tool->angle_format = gwy_si_unit_value_format_new(1.0, 1, "deg");
    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_rect,
                                     "rectangle");

    gwy_tool_stats_init_dialog(tool);
}

static GtkWidget*
gwy_tool_stats_add_aux_button(GwyToolStats *tool,
                              const gchar *stock_id,
                              const gchar *tooltip)
{
    GtkTooltips *tips;
    GtkWidget *button;

    tips = gwy_app_get_tooltips();
    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_tooltips_set_tip(tips, button, tooltip, NULL);
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(stock_id,
                                               GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_box_pack_end(tool->aux_box, button, FALSE, FALSE, 0);
    gtk_widget_set_sensitive(button, FALSE);

    return button;
}

static void
gwy_tool_stats_rect_updated(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool;

    plain_tool = GWY_PLAIN_TOOL(tool);
    gwy_rect_selection_labels_select(tool->rlabels,
                                     plain_tool->selection,
                                     plain_tool->data_field);
}

static void
gwy_tool_stats_init_dialog(GwyToolStats *tool)
{
    GtkDialog *dialog;
    GtkWidget *hbox, *vbox, *image, *label, **plabel;
    GtkTable *table;
    gint i, row;

    dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);

    hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, FALSE, 0);

    /* Selection info */
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    tool->rlabels = gwy_rect_selection_labels_new
                         (TRUE, G_CALLBACK(gwy_tool_stats_rect_updated), tool);
    gtk_box_pack_start(GTK_BOX(vbox),
                       gwy_rect_selection_labels_get_table(tool->rlabels),
                       FALSE, FALSE, 0);

    /* Options */
    table = GTK_TABLE(gtk_table_new(6, 3, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(table), FALSE, FALSE, 0);
    row = 0;

    label = gwy_label_new_header(_("Masking Mode"));
    gtk_table_attach(table, label,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    tool->masking
        = gwy_radio_buttons_create(gwy_masking_type_get_enum(), -1,
                                   G_CALLBACK(gwy_tool_stats_masking_changed),
                                   tool,
                                   tool->args.masking);
    row = gwy_radio_buttons_attach_to_table(tool->masking, table, 3, row);
    gtk_table_set_row_spacing(table, row-1, 8);

    label = gwy_label_new_header(_("Options"));
    gtk_table_attach(table, label,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    tool->instant_update
        = gtk_check_button_new_with_mnemonic(_("_Instant updates"));
    gtk_table_attach(table, tool->instant_update,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->instant_update),
                                 tool->args.instant_update);
    g_signal_connect(tool->instant_update, "toggled",
                     G_CALLBACK(gwy_tool_stats_instant_update_changed), tool);
    row++;

    /* Parameters */
    table = GTK_TABLE(gtk_table_new(16, 2, FALSE));
    gtk_table_set_col_spacings(table, 6);
    gtk_table_set_row_spacings(table, 2);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(table), TRUE, TRUE, 0);
    row = 0;

    gtk_table_attach(table, gwy_label_new_header(_("Parameters")),
                     0, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    for (i = 0; i < G_N_ELEMENTS(values); i++) {
        label = gtk_label_new(_(values[i].name));
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        gtk_table_attach(table, label,
                         0, 1, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

        plabel = (GtkWidget**)G_STRUCT_MEMBER_P(tool, values[i].offset);
        *plabel = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(*plabel), 1.0, 0.5);
        gtk_label_set_selectable(GTK_LABEL(*plabel), TRUE);
        gtk_table_attach(table, *plabel,
                         1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

        row++;
    }

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, FALSE, FALSE, 0);
    tool->aux_box = GTK_BOX(hbox);

    tool->save = gwy_tool_stats_add_aux_button(tool, GTK_STOCK_SAVE,
                                               _("Save table to a file"));
    g_signal_connect_swapped(tool->save, "clicked",
                             G_CALLBACK(gwy_tool_stats_save), tool);

    tool->copy = gwy_tool_stats_add_aux_button(tool, GTK_STOCK_COPY,
                                               _("Copy table to clipboard"));
    g_signal_connect_swapped(tool->copy, "clicked",
                             G_CALLBACK(gwy_tool_stats_copy), tool);

    tool->update = gtk_dialog_add_button(dialog, _("_Update"),
                                         GWY_TOOL_RESPONSE_UPDATE);
    image = gtk_image_new_from_stock(GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(tool->update), image);
    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), TRUE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_NO_BUTTON);

    gtk_widget_set_sensitive(tool->update, !tool->args.instant_update);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_stats_data_switched(GwyTool *gwytool,
                             GwyDataView *data_view)
{
    GwyPlainTool *plain_tool;
    GwyContainer *container;
    GwyToolStats *tool;
    gboolean ignore;
    gchar xukey[24];
    gchar yukey[24];
    gchar zukey[24];

    plain_tool = GWY_PLAIN_TOOL(gwytool);
    tool = GWY_TOOL_STATS(gwytool);
    ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_stats_parent_class)->data_switched(gwytool,
                                                               data_view);
    if (ignore || plain_tool->init_failed)
        return;

    if (data_view) {
        container = plain_tool->container;
        gwy_object_set_or_reset(plain_tool->layer,
                                tool->layer_type_rect,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);

        g_snprintf(xukey, sizeof(xukey), "/%d/data/cal_xunc", plain_tool->id);
        g_snprintf(yukey, sizeof(yukey), "/%d/data/cal_yunc", plain_tool->id);
        g_snprintf(zukey, sizeof(zukey), "/%d/data/cal_zunc", plain_tool->id);

        tool->has_calibration = FALSE;
        if (gwy_container_gis_object_by_name(container, xukey, &tool->xunc)
            && gwy_container_gis_object_by_name(container, yukey, &tool->yunc)
            && gwy_container_gis_object_by_name(container, zukey, &tool->zunc))
            tool->has_calibration = TRUE;
        gwy_tool_stats_update_labels(tool);
    }

    gtk_widget_set_sensitive(tool->copy, data_view != NULL);
    gtk_widget_set_sensitive(tool->save, data_view != NULL);

}

static void
gwy_tool_stats_update_units(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataField *field = plain_tool->data_field;
    GwySIUnit *siunitxy, *siunitz, *siunitarea, *siunitvar;
    const ToolResults *res = &tool->results;

    siunitxy = gwy_data_field_get_si_unit_xy(field);
    siunitz = gwy_data_field_get_si_unit_z(field);
    tool->same_units = gwy_si_unit_equal(siunitxy, siunitz);

    tool->zrange_format
         = gwy_si_unit_get_format_with_digits(siunitz,
                                              GWY_SI_UNIT_FORMAT_VFMARKUP,
                                              res->max - res->min, 3,
                                              tool->zrange_format);
    /* If the data variation is zero just copy the zrange format. */
    if (res->rms) {
        tool->rms_format
            = gwy_si_unit_get_format_with_digits(siunitz,
                                                 GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                 res->rms, 3, tool->rms_format);
    }
    else {
        tool->rms_format = gwy_si_unit_value_format_clone(tool->zrange_format,
                                                          tool->rms_format);
    }

    siunitarea = gwy_si_unit_power(siunitxy, 2, NULL);
    tool->parea_format
        = gwy_si_unit_get_format_with_digits(siunitarea,
                                             GWY_SI_UNIT_FORMAT_VFMARKUP,
                                             res->projarea, 3,
                                             tool->parea_format);
    /* If the surface and projected area are similar use the same value format.
     * But if surface area is something strange use a separate format. */
    if (res->area <= 120.0*res->projarea) {
        tool->sarea_format = gwy_si_unit_value_format_clone(tool->parea_format,
                                                            tool->sarea_format);
    }
    else {
        tool->sarea_format
            = gwy_si_unit_get_format_with_digits(siunitarea,
                                                 GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                 res->area, 3,
                                                 tool->sarea_format);
    }
    g_object_unref(siunitarea);

    siunitvar = gwy_si_unit_multiply(siunitxy, siunitz, NULL);
    tool->var_format
        = gwy_si_unit_get_format_with_digits(siunitvar,
                                             GWY_SI_UNIT_FORMAT_VFMARKUP,
                                             res->var, 3, tool->var_format);
    g_object_unref(siunitvar);
}

static void
gwy_tool_stats_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolStats *tool = GWY_TOOL_STATS(plain_tool);
    GwyContainer *container = plain_tool->container;
    gchar xukey[24];
    gchar yukey[24];
    gchar zukey[24];

    g_snprintf(xukey, sizeof(xukey), "/%d/data/cal_xunc", plain_tool->id);
    g_snprintf(yukey, sizeof(yukey), "/%d/data/cal_yunc", plain_tool->id);
    g_snprintf(zukey, sizeof(zukey), "/%d/data/cal_zunc", plain_tool->id);

    tool->has_calibration = FALSE;
    if (gwy_container_gis_object_by_name(container, xukey, &tool->xunc)
        && gwy_container_gis_object_by_name(container, yukey, &tool->yunc)
        && gwy_container_gis_object_by_name(container, zukey, &tool->zunc))
        GWY_TOOL_STATS(plain_tool)->has_calibration = TRUE;

    gwy_rect_selection_labels_fill(tool->rlabels, plain_tool->selection,
                                   plain_tool->data_field, NULL, NULL);
    gwy_tool_stats_update_labels(tool);
}

static void
gwy_tool_stats_mask_changed(GwyPlainTool *plain_tool)
{
    if (GWY_TOOL_STATS(plain_tool)->args.masking != GWY_MASK_IGNORE)
        gwy_tool_stats_update_labels(GWY_TOOL_STATS(plain_tool));
}

static void
gwy_tool_stats_response(GwyTool *tool,
                        gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_stats_parent_class)->response(tool, response_id);

    if (response_id == GWY_TOOL_RESPONSE_UPDATE)
        gwy_tool_stats_update_labels(GWY_TOOL_STATS(tool));
}

static void
gwy_tool_stats_selection_changed(GwyPlainTool *plain_tool,
                                 gint hint)
{
    GwyToolStats *tool;
    gint n;

    tool = GWY_TOOL_STATS(plain_tool);
    g_return_if_fail(hint <= 0);

    if (plain_tool->selection) {
        n = gwy_selection_get_data(plain_tool->selection, NULL);
        g_return_if_fail(n == 0 || n == 1);
        gwy_rect_selection_labels_fill(tool->rlabels,
                                       plain_tool->selection,
                                       plain_tool->data_field,
                                       NULL, NULL);
    }
    else
        gwy_rect_selection_labels_fill(tool->rlabels, NULL, NULL, NULL, NULL);

    tool->results_up_to_date = FALSE;
    if (tool->args.instant_update)
        gwy_tool_stats_update_labels(tool);
}

static void
gwy_tool_stats_update_labels(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    ToolResults *results = &tool->results;
    GwySIValueFormat *vf;
    gboolean mask_in_use;
    gchar buffer[64];

    plain_tool = GWY_PLAIN_TOOL(tool);

    if (!plain_tool->data_field) {
        gtk_label_set_text(GTK_LABEL(tool->min), "");
        gtk_label_set_text(GTK_LABEL(tool->max), "");
        gtk_label_set_text(GTK_LABEL(tool->avg), "");
        gtk_label_set_text(GTK_LABEL(tool->median), "");
        gtk_label_set_text(GTK_LABEL(tool->ra), "");
        gtk_label_set_text(GTK_LABEL(tool->rms), "");
        gtk_label_set_text(GTK_LABEL(tool->rms_gw), "");
        gtk_label_set_text(GTK_LABEL(tool->skew), "");
        gtk_label_set_text(GTK_LABEL(tool->kurtosis), "");
        gtk_label_set_text(GTK_LABEL(tool->area), "");
        gtk_label_set_text(GTK_LABEL(tool->projarea), "");
        gtk_label_set_text(GTK_LABEL(tool->var), "");
        gtk_label_set_text(GTK_LABEL(tool->entropy), "");
        gtk_label_set_text(GTK_LABEL(tool->entropydef), "");
        gtk_label_set_text(GTK_LABEL(tool->theta), "");
        gtk_label_set_text(GTK_LABEL(tool->phi), "");
        return;
    }

    mask_in_use = gwy_tool_stats_calculate(tool);
    if (!tool->results_up_to_date)
        return;

    gwy_tool_stats_update_units(tool);

    if (tool->has_calibration) {
        vf = tool->zrange_format;
        update_label_unc(vf, tool->min, results->min, results->umin);
        update_label_unc(vf, tool->max, results->max, results->umax);
        update_label_unc(vf, tool->avg, results->avg, results->uavg);
        update_label_unc(vf, tool->median, results->median, results->umedian);
        vf = tool->rms_format;
        update_label_unc(vf, tool->ra, results->ra, results->ura);
        update_label_unc(vf, tool->rms, results->rms, results->urms);
        g_snprintf(buffer, sizeof(buffer), "%.3g±%.3g", results->skew,
                   results->uskew);
        gtk_label_set_text(GTK_LABEL(tool->skew), buffer);
        g_snprintf(buffer, sizeof(buffer), "%.3g±%.3g",
                   results->kurtosis, results->ukurtosis);
        gtk_label_set_text(GTK_LABEL(tool->kurtosis), buffer);
        update_label_unc(tool->parea_format, tool->projarea,
                         results->projarea, results->uprojarea);
    }
    else {
        vf = tool->zrange_format;
        update_label(vf, tool->min, results->min);
        update_label(vf, tool->max, results->max);
        update_label(vf, tool->avg, results->avg);
        update_label(vf, tool->median, results->median);
        vf = tool->rms_format;
        update_label(vf, tool->ra, results->ra);
        update_label(vf, tool->rms, results->rms);
        g_snprintf(buffer, sizeof(buffer), "%.3g", results->skew);
        gtk_label_set_text(GTK_LABEL(tool->skew), buffer);
        g_snprintf(buffer, sizeof(buffer), "%.3g", results->kurtosis);
        gtk_label_set_text(GTK_LABEL(tool->kurtosis), buffer);
        update_label(tool->parea_format, tool->projarea, results->projarea);
    }

    /* This has no calibration yet. */
    update_label(tool->rms_format, tool->rms_gw, results->rms_gw);
    update_label(tool->var_format, tool->var, results->var);
    g_snprintf(buffer, sizeof(buffer), "%.4g", results->entropy);
    gtk_label_set_text(GTK_LABEL(tool->entropy), buffer);
    g_snprintf(buffer, sizeof(buffer), "%.4g", results->entropydef);
    gtk_label_set_text(GTK_LABEL(tool->entropydef), buffer);

    /* This has no calibration yet. */
    if (tool->same_units)
        update_label(tool->sarea_format, tool->area, results->area);
    else
        gtk_label_set_text(GTK_LABEL(tool->area), _("N.A."));

    if (tool->same_units && !mask_in_use) {
        update_label(tool->angle_format, tool->theta, results->theta);
        update_label(tool->angle_format, tool->phi, results->phi);
        if (tool->has_calibration) {
            update_label_unc(tool->angle_format, tool->theta,
                             results->theta, results->utheta);
            update_label_unc(tool->angle_format, tool->phi, results->phi,
                             results->uphi);
        }
    }
    else {
        gtk_label_set_text(GTK_LABEL(tool->theta), _("N.A."));
        gtk_label_set_text(GTK_LABEL(tool->phi), _("N.A."));
    }
}

static gboolean
gwy_tool_stats_calculate(GwyToolStats * tool)
{
    GwyPlainTool *plain_tool;
    GwyDataField *field, *mask;
    GwyMaskingType masking;
    ToolResults *results = &tool->results;
    gdouble q;
    gint nn, w, h;
    gdouble sel[4];
    gint isel[4];

    plain_tool = GWY_PLAIN_TOOL(tool);
    field = plain_tool->data_field;

    tool->results_up_to_date = FALSE;
    if (!gwy_selection_get_object(plain_tool->selection, 0, sel)
        || sel[0] == sel[2] || sel[1] == sel[3]) {
        isel[0] = isel[1] = 0;
        isel[2] = w = gwy_data_field_get_xres(field);
        isel[3] = h = gwy_data_field_get_yres(field);
        sel[0] = sel[1] = 0.0;
        sel[2] = gwy_data_field_get_xreal(field);
        sel[3] = gwy_data_field_get_yreal(field);
    }
    else {
        isel[0] = floor(gwy_data_field_rtoj(field, sel[0]));
        isel[1] = floor(gwy_data_field_rtoi(field, sel[1]));
        isel[2] = floor(gwy_data_field_rtoj(field, sel[2]));
        isel[3] = floor(gwy_data_field_rtoi(field, sel[3]));
        w = ABS(isel[2] - isel[0]) + 1;
        h = ABS(isel[3] - isel[1]) + 1;
        isel[0] = MIN(isel[0], isel[2]);
        isel[1] = MIN(isel[1], isel[3]);
    }

    if (!w || !h)
        return FALSE;

    masking = tool->args.masking;
    mask = plain_tool->mask_field;
    if (!mask || masking == GWY_MASK_IGNORE) {
        /* If one says masking is not used, set the other accordingly. */
        masking = GWY_MASK_IGNORE;
        mask = NULL;
    }

    q = gwy_data_field_get_xmeasure(field) * gwy_data_field_get_ymeasure(field);
    if (mask) {
        if (masking == GWY_MASK_INCLUDE)
            gwy_data_field_area_count_in_range(mask, NULL,
                                               isel[0], isel[1], w, h,
                                               0.0, 0.0, &nn, NULL);
        else
            gwy_data_field_area_count_in_range(mask, NULL,
                                               isel[0], isel[1], w, h,
                                               1.0, 1.0, NULL, &nn);
        nn = w * h - nn;
    }
    else
        nn = w * h;
    results->projarea = nn * q;
    /* TODO: do something more reasonable when nn == 0 */

    gwy_data_field_area_get_min_max_mask(field, mask, masking,
                                         isel[0], isel[1], w, h,
                                         &results->min, &results->max);
    gwy_data_field_area_get_stats_mask(field, mask, masking,
                                       isel[0], isel[1], w, h,
                                       &results->avg,
                                       &results->ra, &results->rms,
                                       &results->skew, &results->kurtosis);
    results->rms_gw
        = gwy_data_field_area_get_grainwise_rms(field, mask, masking,
                                                isel[0], isel[1], w, h);
    results->median
        = gwy_data_field_area_get_median_mask(field, mask, masking,
                                              isel[0], isel[1], w, h);
    results->var
        = gwy_data_field_area_get_variation(field, mask, masking,
                                            isel[0], isel[1], w, h);
    results->entropy
        = gwy_data_field_area_get_entropy(field, mask, masking,
                                          isel[0], isel[1], w, h);
    /* Consider δ-function a limit of Gaussian and report deficit of zero. */
    if (results->rms > 0.0 && results->entropy < 0.1*G_MAXDOUBLE) {
        results->entropydef = (ENTROPY_NORMAL + log(results->rms)
                               - results->entropy);
    }
    else
        results->entropydef = 0.0;

    if (tool->same_units)
        results->area
            = gwy_data_field_area_get_surface_area_mask(field, mask, masking,
                                                        isel[0], isel[1], w, h);

    if (tool->same_units && !mask) {
        gwy_data_field_area_get_inclination(field, isel[0], isel[1], w, h,
                                            &results->theta, &results->phi);
        results->theta *= 180.0/G_PI;
        results->phi *= 180.0/G_PI;
    }

    calculate_uncertainties(tool, field, mask, masking, nn, isel, w, h);

    gwy_assign(results->isel, isel, 4);
    gwy_assign(results->sel, sel, 4);
    sel[2] += gwy_data_field_get_xoffset(field);
    sel[3] += gwy_data_field_get_yoffset(field);
    tool->results_up_to_date = TRUE;

    return mask != NULL;
}

static void
calculate_uncertainties(GwyToolStats *tool,
                        GwyDataField *field, GwyDataField *mask,
                        GwyMaskingType masking, guint nn,
                        const gint *isel, gdouble w, gdouble h)
{
    ToolResults *results = &tool->results;
    gint xres, yres, oldx, oldy;

    if (!tool->has_calibration)
        return;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    oldx = gwy_data_field_get_xres(tool->xunc);
    oldy = gwy_data_field_get_yres(tool->xunc);
    //FIXME, functions should work with data of any size
    gwy_data_field_resample(tool->xunc, xres, yres, GWY_INTERPOLATION_BILINEAR);
    gwy_data_field_resample(tool->yunc, xres, yres, GWY_INTERPOLATION_BILINEAR);
    gwy_data_field_resample(tool->zunc, xres, yres, GWY_INTERPOLATION_BILINEAR);

    results->uprojarea
        = gwy_data_field_area_get_projected_area_uncertainty(nn, tool->xunc,
                                                             tool->yunc);

    gwy_data_field_area_get_min_max_uncertainty_mask(field, tool->zunc,
                                                     mask, masking,
                                                     isel[0], isel[1], w, h,
                                                     &results->umin,
                                                     &results->umax);
    gwy_data_field_area_get_stats_uncertainties_mask(field, tool->zunc,
                                                     mask, masking,
                                                     isel[0], isel[1], w, h,
                                                     &results->uavg,
                                                     &results->ura,
                                                     &results->urms,
                                                     &results->uskew,
                                                     &results->ukurtosis);

    results->umedian
        = gwy_data_field_area_get_median_uncertainty_mask(field, tool->zunc,
                                                          mask, masking,
                                                          isel[0], isel[1],
                                                          w, h);
    if (tool->same_units && !mask) {
        gwy_data_field_area_get_inclination_uncertainty(field,
                                                        tool->zunc,
                                                        tool->xunc,
                                                        tool->yunc,
                                                        isel[0], isel[1], w, h,
                                                        &results->  utheta,
                                                        &results->  uphi);
        results->utheta *= 180.0/G_PI;
        results->uphi *= 180.0/G_PI;
    }

    gwy_data_field_resample(tool->xunc, oldx, oldy, GWY_INTERPOLATION_BILINEAR);
    gwy_data_field_resample(tool->yunc, oldx, oldy, GWY_INTERPOLATION_BILINEAR);
    gwy_data_field_resample(tool->zunc, oldx, oldy, GWY_INTERPOLATION_BILINEAR);
}

static void
update_label(GwySIValueFormat *units,
             GtkWidget *label,
             gdouble value)
{
    static gchar buffer[64];

    g_return_if_fail(units);
    g_return_if_fail(GTK_IS_LABEL(label));

    g_snprintf(buffer, sizeof(buffer), "%.*f%s%s",
               units->precision, value/units->magnitude,
               *units->units ? " " : "", units->units);
    gtk_label_set_markup(GTK_LABEL(label), buffer);
}

static void
update_label_unc(GwySIValueFormat *units,
                 GtkWidget *label,
                 gdouble value, gdouble uncertainty)
{
    static gchar buffer[64];

    g_return_if_fail(units);
    g_return_if_fail(GTK_IS_LABEL(label));

    g_snprintf(buffer, sizeof(buffer), "(%.*f±%.*f) %s%s",
               units->precision, value/units->magnitude,
               units->precision, uncertainty/units->magnitude,
               *units->units ? " " : "", units->units);
    gtk_label_set_markup(GTK_LABEL(label), buffer);
}

static void
gwy_tool_stats_masking_changed(GtkWidget *button,
                               GwyToolStats *tool)
{
    GwyPlainTool *plain_tool;

    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
        return;

    plain_tool = GWY_PLAIN_TOOL(tool);
    tool->args.masking = gwy_radio_button_get_value(button);
    if (plain_tool->data_field && plain_tool->mask_field)
        gwy_tool_stats_update_labels(tool);
}

static void
gwy_tool_stats_instant_update_changed(GtkToggleButton *check,
                                      GwyToolStats *tool)
{
    tool->args.instant_update = gtk_toggle_button_get_active(check);
    gtk_widget_set_sensitive(tool->update, !tool->args.instant_update);
    if (tool->args.instant_update)
        gwy_tool_stats_selection_changed(GWY_PLAIN_TOOL(tool), -1);
}

static void
gwy_tool_stats_save(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool;
    ToolReportData report_data;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->container);
    if (!tool->results_up_to_date)
        gwy_tool_stats_update_labels(tool);

    /* Copy everything as user can switch data during the Save dialog (though
     * he cannot destroy them, so references are not necessary) */
    report_data.results = tool->results;
    report_data.masking = tool->args.masking;
    if (!plain_tool->mask_field)
        report_data.masking = GWY_MASK_IGNORE;
    report_data.same_units = tool->same_units;
    report_data.angle_format = tool->angle_format;
    report_data.container = plain_tool->container;
    report_data.data_field = plain_tool->data_field;
    report_data.id = plain_tool->id;

    gwy_save_auxiliary_with_callback(_("Save Statistical Quantities"),
                                     GTK_WINDOW(GWY_TOOL(tool)->dialog),
                                     &gwy_tool_stats_create_report,
                                     (GwySaveAuxiliaryDestroy)g_free,
                                     &report_data);
}

static void
gwy_tool_stats_copy(GwyToolStats *tool)
{
    GwyPlainTool *plain_tool;
    ToolReportData report_data;
    GtkClipboard *clipboard;
    GdkDisplay *display;
    gssize len;
    gchar *text;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->container);
    if (!tool->results_up_to_date)
        gwy_tool_stats_update_labels(tool);

    report_data.results = tool->results;
    report_data.masking = tool->args.masking;
    if (!plain_tool->mask_field)
        report_data.masking = GWY_MASK_IGNORE;
    report_data.same_units = tool->same_units;
    report_data.angle_format = tool->angle_format;
    report_data.container = plain_tool->container;
    report_data.data_field = plain_tool->data_field;
    report_data.id = plain_tool->id;

    text = gwy_tool_stats_create_report(&report_data, &len);
    display = gtk_widget_get_display(GTK_WIDGET(GWY_TOOL(tool)->dialog));
    clipboard = gtk_clipboard_get_for_display(display, GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    g_free(text);
}

static void
append_report_line_label(GString *report, guint labelid, guint width)
{
    const gchar *label = _(values[labelid].name);
    guint len = g_utf8_strlen(label, -1);

    g_string_append(report, label);
    while (len++ < width)
        g_string_append_c(report, ' ');
}

static void
append_report_line_vf(GString *report, guint labelid, gdouble value,
                      GwySIValueFormat *vf, guint width)
{
    append_report_line_label(report, labelid, width);
    g_string_append_printf(report, "%.*f%s%s\n",
                           vf->precision + 1, value/vf->magnitude,
                           *vf->units ? " " : "", vf->units);
}

static void
append_report_line_plain(GString *report, guint labelid, gdouble value,
                         const gchar *format, guint width)
{
    append_report_line_label(report, labelid, width);
    g_string_append_printf(report, format, value);
    g_string_append_c(report, '\n');
}

static gchar*
gwy_tool_stats_create_report(gpointer user_data,
                             gssize *data_len)
{
    const ToolReportData *report_data = (const ToolReportData*)user_data;
    const ToolResults *results = &report_data->results;
    GwyDataField *field = report_data->data_field;
    GwyContainer *container = report_data->container;
    GwySIUnit *siunitxy, *siunitz, *siunitarea, *siunitvar;
    GwySIValueFormat *vf = NULL;
    const guchar *title;
    gboolean mask_in_use, same_units = report_data->same_units;
    GString *report;
    gchar *ix, *iy, *iw, *ih, *rx, *ry, *rw, *rh, *muse, *uni;
    gchar *key, *retval;
    guint i, maxw;

    siunitxy = gwy_data_field_get_si_unit_xy(field);
    siunitz = gwy_data_field_get_si_unit_z(field);

    mask_in_use = (report_data->masking != GWY_MASK_IGNORE);
    *data_len = -1;
    report = g_string_sized_new(4096);

    g_string_append(report, _("Statistical Quantities"));
    g_string_append(report, "\n\n");

    /* Channel information */
    if (gwy_container_gis_string_by_name(container, "/filename", &title))
        g_string_append_printf(report, _("File:         %s\n"), title);

    key = g_strdup_printf("/%d/data/title", report_data->id);
    if (gwy_container_gis_string_by_name(container, key, &title))
        g_string_append_printf(report, _("Data channel: %s\n"), title);
    g_free(key);

    g_string_append_c(report, '\n');

    iw = g_strdup_printf("%d", results->isel[2]);
    ih = g_strdup_printf("%d", results->isel[3]);
    ix = g_strdup_printf("%d", results->isel[0]);
    iy = g_strdup_printf("%d", results->isel[1]);

    vf = gwy_data_field_get_value_format_xy(field,
                                            GWY_SI_UNIT_FORMAT_PLAIN, vf);
    rw = g_strdup_printf("%.*f", vf->precision, results->sel[2]/vf->magnitude);
    rh = g_strdup_printf("%.*f", vf->precision, results->sel[3]/vf->magnitude);
    rx = g_strdup_printf("%.*f", vf->precision, results->sel[0]/vf->magnitude);
    ry = g_strdup_printf("%.*f", vf->precision, results->sel[1]/vf->magnitude);
    uni = g_strdup(vf->units);

    muse = g_strdup(mask_in_use ? _("Yes") : _("No"));

    maxw = 0;
    for (i = 0; i < G_N_ELEMENTS(values); i++) {
        guint len = g_utf8_strlen(_(values[i].name), -1);
        maxw = MAX(maxw, len);
    }
    maxw++;

    g_string_append_printf(report,
                           _("Selected area: %s × %s at (%s, %s) px\n"
                             "               %s × %s at (%s, %s) %s\n"
                             "Mask in use:   %s\n"),
                           iw, ih, ix, iy,
                           rw, rh, rx, ry, uni,
                           muse);
    g_string_append_c(report, '\n');

    vf = gwy_si_unit_get_format_with_digits(siunitz, GWY_SI_UNIT_FORMAT_PLAIN,
                                            results->max - results->min, 4, vf);
    append_report_line_vf(report, 0, results->min, vf, maxw);
    append_report_line_vf(report, 1, results->max, vf, maxw);
    append_report_line_vf(report, 2, results->avg, vf, maxw);
    append_report_line_vf(report, 3, results->median, vf, maxw);
    if (results->rms) {
        vf = gwy_si_unit_get_format_with_digits(siunitz,
                                                GWY_SI_UNIT_FORMAT_PLAIN,
                                                results->rms, 4, vf);
    }
    append_report_line_vf(report, 4, results->ra, vf, maxw);
    append_report_line_vf(report, 5, results->rms, vf, maxw);
    append_report_line_vf(report, 6, results->rms_gw, vf, maxw);

    append_report_line_plain(report, 7, results->skew, "%.4g", maxw);
    append_report_line_plain(report, 8, results->kurtosis, "%.4g", maxw);

    siunitarea = gwy_si_unit_power(siunitxy, 2, NULL);
    if (same_units) {
        vf = gwy_si_unit_get_format_with_digits(siunitarea,
                                                GWY_SI_UNIT_FORMAT_PLAIN,
                                                results->area, 4, vf);
        append_report_line_vf(report, 9, results->area, vf, maxw);
    }
    vf = gwy_si_unit_get_format_with_digits(siunitarea,
                                            GWY_SI_UNIT_FORMAT_PLAIN,
                                            results->projarea, 4, vf);
    append_report_line_vf(report, 10, results->projarea, vf, maxw);
    g_object_unref(siunitarea);

    siunitvar = gwy_si_unit_multiply(siunitxy, siunitz, NULL);
    vf = gwy_si_unit_get_format_with_digits(siunitvar, GWY_SI_UNIT_FORMAT_PLAIN,
                                            results->var, 4, vf);
    append_report_line_vf(report, 11, results->var, vf, maxw);
    g_object_unref(siunitvar);
    GWY_SI_VALUE_FORMAT_FREE(vf);

    append_report_line_plain(report, 12, results->entropy, "%.5g", maxw);
    append_report_line_plain(report, 13, results->entropydef, "%.5g", maxw);

    if (same_units && !mask_in_use) {
        vf = report_data->angle_format;
        append_report_line_vf(report, 14, results->theta, vf, maxw);
        append_report_line_vf(report, 15, results->phi, vf, maxw);
    }

    g_free(ix);
    g_free(iy);
    g_free(iw);
    g_free(ih);
    g_free(rx);
    g_free(ry);
    g_free(rw);
    g_free(rh);

    retval = report->str;
    g_string_free(report, FALSE);

    return retval;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
