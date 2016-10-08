/*
 *  @(#) $Id$
 *  Copyright (C) 2003 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libprocess/simplefft.h>
#include <libprocess/inttrans.h>
#include <libprocess/filters.h>
#include <libprocess/correlation.h>

/* Correlation iterator */
typedef struct {
    GwyComputationState cs;
    GwyDataField *data_field;
    GwyDataField *kernel_field;
    GwyDataField *score;
    GwyDataField *avg;
    GwyDataField *rms;
    gdouble kavg;
    gdouble krms;
    gint i;
    gint j;
} GwyCorrelationState;

/* Cross-correlation iterator */
typedef struct {
    GwyComputationState cs;
    GwyDataField *data_field1;
    GwyDataField *data_field2;
    GwyDataField *x_dist;
    GwyDataField *y_dist;
    GwyDataField *score;
    GwyDataField *weights;
    gint search_width;
    gint search_height;
    gint window_width;
    gint window_height;
    gint i;
    gint j;
} GwyCrossCorrelationState;

/**
 * gwy_data_field_get_correlation_score:
 * @data_field: A data field.
 * @kernel_field: Kernel to correlate data field with.
 * @col: Upper-left column position in the data field.
 * @row: Upper-left row position in the data field.
 * @kernel_col: Upper-left column position in kernel field.
 * @kernel_row: Upper-left row position in kernel field.
 * @kernel_width: Width of kernel field area.
 * @kernel_height: Heigh of kernel field area.
 *
 * Calculates a correlation score in one point.
 *
 * Correlation window size is given
 * by @kernel_col, @kernel_row, @kernel_width, @kernel_height,
 * postion of the correlation window on data is given by
 * @col, @row.
 *
 * If anything fails (data too close to boundary, etc.),
 * function returns -1.0 (none correlation)..
 *
 * Returns: Correlation score (between -1.0 and 1.0). Value 1.0 denotes
 *          maximum correlation, -1.0 none correlation.
 **/
gdouble
gwy_data_field_get_correlation_score(GwyDataField *data_field,
                                     GwyDataField *kernel_field,
                                     gint col,
                                     gint row,
                                     gint kernel_col,
                                     gint kernel_row,
                                     gint kernel_width,
                                     gint kernel_height)
{
    gint xres, yres, kxres, kyres, i, j;
    gdouble rms1, rms2, avg1, avg2, sumpoints, score;
    gdouble *data, *kdata;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), -1.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(kernel_field), -1.0);

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel_field->xres;
    kyres = kernel_field->yres;
    kernel_width = kernel_width;
    kernel_height = kernel_height;

    /* correlation request outside kernel */
    if (kernel_col > kxres || kernel_row > kyres)
        return -1;

    /* correlation request outside data field */
    if (col < 0 || row < 0
        || col + kernel_width > xres
        || row + kernel_height > yres)
        return -1;
    if (kernel_col < 0
        || kernel_row < 0
        || kernel_col + kernel_width > kxres
        || kernel_row + kernel_height > kyres)
        return -1;

    avg1 = gwy_data_field_area_get_avg(data_field, NULL,
                                       col, row, kernel_width, kernel_height);
    avg2 = gwy_data_field_area_get_avg(kernel_field, NULL,
                                       kernel_col, kernel_row,
                                       kernel_width, kernel_height);

    rms1 = gwy_data_field_area_get_rms(data_field, NULL,
                                       col, row, kernel_width, kernel_height);
    if (rms1 == 0.0)
        return 0.0;
    rms2 = gwy_data_field_area_get_rms(kernel_field, NULL,
                                       kernel_col, kernel_row,
                                       kernel_width, kernel_height);
    if (rms2 == 0.0)
        return 0.0;

    score = 0;
    sumpoints = kernel_width * kernel_height;
    data = data_field->data;
    kdata = kernel_field->data;
    for (j = 0; j < kernel_height; j++) {   /* row */
        for (i = 0; i < kernel_width; i++) {   /* col */
            score += (data[(i + col) + xres*(j + row)] - avg1)
                      * (kdata[(i + kernel_col) + kxres*(j + kernel_row)]
                         - avg2);
        }
    }
    score /= rms1 * rms2 * sumpoints;

    return score;
}
/**
 * gwy_data_field_get_weighted_correlation_score:
 * @data_field: A data field.
 * @kernel_field: Kernel to correlate data field with.
 * @weight_field: data field of same size as kernel window size
 * @col: Upper-left column position in the data field.
 * @row: Upper-left row position in the data field.
 * @kernel_col: Upper-left column position in kernel field.
 * @kernel_row: Upper-left row position in kernel field.
 * @kernel_width: Width of kernel field area.
 * @kernel_height: Heigh of kernel field area.
 *
 * Calculates a correlation score in one point using weights to center
 * the used information to the center of kernel.
 *
 * Correlation window size is given
 * by @kernel_col, @kernel_row, @kernel_width, @kernel_height,
 * postion of the correlation window on data is given by
 * @col, @row.
 *
 * If anything fails (data too close to boundary, etc.),
 * function returns -1.0 (none correlation)..
 *
 * Returns: Correlation score (between -1.0 and 1.0). Value 1.0 denotes
 *          maximum correlation, -1.0 none correlation.
 **/
gdouble
gwy_data_field_get_weighted_correlation_score(GwyDataField *data_field,
                                     GwyDataField *kernel_field,
                                     GwyDataField *weight_field,
                                     gint col,
                                     gint row,
                                     gint kernel_col,
                                     gint kernel_row,
                                     gint kernel_width,
                                     gint kernel_height)
{
    gint xres, yres, kxres, kyres, wxres, i, j;
    gdouble rms1, rms2, avg1, avg2, score;
    gdouble *data, *kdata, *wdata, weightsum;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), -10.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(kernel_field), -11.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(weight_field), -12.0);
    g_return_val_if_fail(kernel_width == weight_field->xres && 
                     kernel_height == weight_field->yres, -13);

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel_field->xres;
    kyres = kernel_field->yres;
    wxres = weight_field->xres;

    /* correlation request outside kernel */
    if (kernel_col > kxres || kernel_row > kyres)
        return -2;

    /* correlation request outside data field */
    if (col < 0 || row < 0
        || col + kernel_width > xres
        || row + kernel_height > yres) {
     
        printf("error: %d %d %d  %d %d %d\n", col, kernel_width, xres, row, kernel_height, yres);
        return -3;
    }

    if (kernel_col < 0
        || kernel_row < 0
        || kernel_col + kernel_width > kxres
        || kernel_row + kernel_height > kyres)
        return -4;

    data = data_field->data;
    kdata = kernel_field->data;
    wdata = weight_field->data;
    weightsum = gwy_data_field_get_sum(weight_field);

    avg1 = avg2 = 0;
    for (j = 0; j < kernel_height; j++) {   /* row */
        for (i = 0; i < kernel_width; i++) {   /* col */
            avg1 += data[(i + col) + xres*(j + row)]*wdata[i + wxres*j];
            avg2 += kdata[(i + kernel_col) + kxres*(j + kernel_row)]*wdata[i + wxres*j];
        }
    }
    avg1 /= gwy_data_field_get_sum(weight_field);
    avg2 /= gwy_data_field_get_sum(weight_field);

    rms1 = rms2 = 0;
    for (j = 0; j < kernel_height; j++) {   /* row */
        for (i = 0; i < kernel_width; i++) {   /* col */
            rms1 += wdata[i + wxres*j]*(data[(i + col) + xres*(j + row)] - avg1)
                      *(data[(i + col) + xres*(j + row)] - avg1);
            rms2 += wdata[i + wxres*j]*(kdata[(i + kernel_col) + kxres*(j + kernel_row)] - avg2)
                      *(kdata[(i + kernel_col) + kxres*(j + kernel_row)] - avg2);
        }
    }
    rms1 /= gwy_data_field_get_sum(weight_field);
    rms2 /= gwy_data_field_get_sum(weight_field);

    rms1 = sqrt(rms1);
    rms2 = sqrt(rms2);

    if (rms1 == 0.0)
        return 0.0;
    if (rms2 == 0.0)
        return 0.0;


    score = 0;
    data = data_field->data;
    kdata = kernel_field->data;
    for (j = 0; j < kernel_height; j++) {   /* row */
        for (i = 0; i < kernel_width; i++) {   /* col */
            score += wdata[i + wxres*j]*(data[(i + col) + xres*(j + row)] - avg1)
                      * (kdata[(i + kernel_col) + kxres*(j + kernel_row)]
                         - avg2);
        }
    }
    //printf("%g %g %g %g  = %g\n", rms1, rms2, score, weightsum, score/(rms1 * rms2 * weightsum));

    score /= rms1 * rms2 * weightsum;

    return score;
}

/* Note: rms and avg must be identical and contain a copy of the original data
 * field */
static void
calculate_normalization(GwyDataField *avg,
                        GwyDataField *rms,
                        gint kernel_width,
                        gint kernel_height)
{
    GwyDataField *buffer;
    gint xres, yres, i;

    g_return_if_fail(rms->xres == avg->xres && rms->yres == avg->yres);
    xres = avg->xres;
    yres = avg->yres;

    for (i = 0; i < xres*yres; i++)
        rms->data[i] *= rms->data[i];

    buffer = gwy_data_field_new_alike(avg, FALSE);
    gwy_data_field_area_gather(rms, rms, buffer,
                               kernel_width, kernel_height, TRUE,
                               0, 0, xres, yres);
    gwy_data_field_area_gather(avg, avg, buffer,
                               kernel_width, kernel_height, TRUE,
                               0, 0, xres, yres);
    g_object_unref(buffer);

    for (i = 0; i < xres*yres; i++) {
        rms->data[i] -= avg->data[i]*avg->data[i];
        rms->data[i] = sqrt(MAX(rms->data[i], 0.0));
    }
}

/**
 * gwy_data_field_get_raw_correlation_score:
 * @data_field: A data field.
 * @kernel_field: Kernel to correlate data field with.
 * @col: Upper-left column position in the data field.
 * @row: Upper-left row position in the data field.
 * @kernel_col: Upper-left column position in kernel field.
 * @kernel_row: Upper-left row position in kernel field.
 * @kernel_width: Width of kernel field area.
 * @kernel_height: Heigh of kernel field area.
 * @data_avg: Mean value of the effective data field area.
 * @data_rms: Mean value of the effective kernel field area.
 *
 * Calculates a raw correlation score in one point.
 *
 * See gwy_data_field_get_correlation_score() for description.  This function
 * is useful if you know the mean values and rms.
 *
 * To obtain the score, divide the returned value with the product of rms of
 * data field area and rms of the kernel.
 *
 * Returns: Correlation score (normalized to multiple of kernel and data
 *          area rms).
 **/
static gdouble
gwy_data_field_get_raw_correlation_score(GwyDataField *data_field,
                                         GwyDataField *kernel_field,
                                         gint col,
                                         gint row,
                                         gint kernel_col,
                                         gint kernel_row,
                                         gint kernel_width,
                                         gint kernel_height,
                                         gdouble data_avg,
                                         gdouble kernel_avg)
{
    gint xres, yres, kxres, kyres, i, j;
    gdouble sumpoints, score;
    gdouble *data, *kdata;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), -1.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(kernel_field), -1.0);

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel_field->xres;
    kyres = kernel_field->yres;
    kernel_width = kernel_width;
    kernel_height = kernel_height;

    /* correlation request outside kernel */
    if (kernel_col > kxres || kernel_row > kyres)
        return -1;

    /* correlation request outside data field */
    if (col < 0 || row < 0
        || col + kernel_width > xres
        || row + kernel_height > yres)
        return -1;
    if (kernel_col < 0
        || kernel_row < 0
        || kernel_col + kernel_width > kxres
        || kernel_row + kernel_height > kyres)
        return -1;

    score = 0;
    sumpoints = kernel_width * kernel_height;
    data = data_field->data;
    kdata = kernel_field->data;
    for (j = 0; j < kernel_height; j++) {   /* row */
        for (i = 0; i < kernel_width; i++) {   /* col */
            score += (data[(i + col) + xres*(j + row)] - data_avg)
                      * (kdata[(i + kernel_col) + kxres*(j + kernel_row)]
                         - kernel_avg);
        }
    }
    score /= sumpoints;

    return score;
}

/**
 * gwy_data_field_correlate:
 * @data_field: A data field.
 * @kernel_field: Correlation kernel.
 * @score: Data field to store correlation scores to.
 * @method: Correlation score calculation method.
 *
 * Computes correlation score for all positions in a data field.
 *
 * Correlation score is compute for all points in data field @data_field
 * and full size of correlation kernel @kernel_field.
 *
 * The points in @score correspond to centers of kernel.  More precisely, the
 * point ((@kxres-1)/2, (@kyres-1)/2) in @score corresponds to kernel field
 * top left corner coincident with data field top left corner.  Points outside
 * the area where the kernel field fits into the data field completely are
 * set to -1 for %GWY_CORRELATION_NORMAL.
 **/
void
gwy_data_field_correlate(GwyDataField *data_field, GwyDataField *kernel_field,
                         GwyDataField *score, GwyCorrelationType method)
{

    gint xres, yres, kxres, kyres, i, j, k;
    GwyDataField *data_in_re, *data_out_re, *data_out_im;
    GwyDataField *kernel_in_re, *kernel_out_re, *kernel_out_im;
    gdouble norm;

    g_return_if_fail(data_field != NULL && kernel_field != NULL);

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel_field->xres;
    kyres = kernel_field->yres;

    if (kxres <= 0 || kyres <= 0) {
        g_warning("Correlation kernel has nonpositive size.");
        return;
    }

    switch (method) {
        case GWY_CORRELATION_NORMAL:
        gwy_data_field_fill(score, -1);
        /*correlation request outside kernel */
        if (kxres > xres || kyres > yres) {
            return;
        }

        {
            GwyDataField *avg, *rms;
            gdouble s, davg, drms, kavg, krms;
            gint xoff, yoff;

            /* The number of pixels the correlation kernel extends to the
             * negative direction */
            xoff = (kxres - 1)/2;
            yoff = (kyres - 1)/2;
            kavg = gwy_data_field_get_avg(kernel_field);
            krms = gwy_data_field_get_rms(kernel_field);
            avg = gwy_data_field_duplicate(data_field);
            rms = gwy_data_field_duplicate(data_field);
            calculate_normalization(avg, rms, kxres, kyres);
            for (i = yoff; i + kyres - yoff <= yres; i++) {
                for (j = xoff; j + kxres - xoff <= xres; j++) {
                    k = i*xres + j;
                    davg = avg->data[k];
                    drms = rms->data[k];
                    if (!krms || !drms) {
                        score->data[k] = 0.0;
                        continue;
                    }
                    s = gwy_data_field_get_raw_correlation_score(data_field,
                                                                 kernel_field,
                                                                 j - xoff,
                                                                 i - yoff,
                                                                 0, 0,
                                                                 kxres, kyres,
                                                                 davg, kavg);
                    score->data[k] = s/(drms*krms);
                }
            }
            g_object_unref(avg);
            g_object_unref(rms);
        }
        break;

        case GWY_CORRELATION_FFT:
        case GWY_CORRELATION_POC:
        data_in_re = gwy_data_field_duplicate(data_field);
        kernel_in_re = gwy_data_field_new_alike(data_field, TRUE);
        gwy_data_field_area_copy(kernel_field, kernel_in_re,
                                 0, 0, kernel_field->xres, kernel_field->yres,
                                 kernel_in_re->xres/2 - kernel_field->xres/2,
                                 kernel_in_re->yres/2 - kernel_field->yres/2);
        gwy_data_field_resample(score, xres, yres,
                                GWY_INTERPOLATION_NONE);

        data_out_re = gwy_data_field_new_alike(data_in_re, TRUE);
        data_out_im = gwy_data_field_new_alike(data_in_re, TRUE);
        kernel_out_re = gwy_data_field_new_alike(data_in_re, TRUE);
        kernel_out_im = gwy_data_field_new_alike(data_in_re, TRUE);

        gwy_data_field_2dfft(data_in_re, NULL, data_out_re, data_out_im,
                             GWY_WINDOWING_NONE,
                             GWY_TRANSFORM_DIRECTION_FORWARD,
                             GWY_INTERPOLATION_BILINEAR, FALSE, FALSE);
        gwy_data_field_2dfft(kernel_in_re, NULL, kernel_out_re, kernel_out_im,
                             GWY_WINDOWING_NONE,
                             GWY_TRANSFORM_DIRECTION_FORWARD,
                             GWY_INTERPOLATION_BILINEAR, FALSE, FALSE);

        for (i = 0; i < xres*yres; i++) {
            /*NOTE: now we construct new "complex field" from data
             * and kernel fields, just to save memory*/
            data_in_re->data[i] = data_out_re->data[i]*kernel_out_re->data[i]
                + data_out_im->data[i]*kernel_out_im->data[i];
            kernel_in_re->data[i] = -data_out_re->data[i]*kernel_out_im->data[i]
                + data_out_im->data[i]*kernel_out_re->data[i];
            if (method == GWY_CORRELATION_POC) {
                norm = hypot(data_in_re->data[i], kernel_in_re->data[i]);
                data_in_re->data[i] /= norm;
                kernel_in_re->data[i] /= norm;
            }
        }
        gwy_data_field_2dfft(data_in_re, kernel_in_re, score, data_out_im,
                             GWY_WINDOWING_NONE,
                             GWY_TRANSFORM_DIRECTION_BACKWARD,
                             GWY_INTERPOLATION_BILINEAR, FALSE, FALSE);
        gwy_data_field_2dfft_humanize(score);

        /*TODO compute it and put to score field*/
        g_object_unref(data_in_re);
        g_object_unref(data_out_re);
        g_object_unref(data_out_im);
        g_object_unref(kernel_in_re);
        g_object_unref(kernel_out_re);
        g_object_unref(kernel_out_im);
        break;
    }

    gwy_data_field_invalidate(score);
}

/**
 * gwy_data_field_correlate_init:
 * @data_field: A data field.
 * @kernel_field: Kernel to correlate data field with.
 * @score: Data field to store correlation scores to.
 *
 * Creates a new correlation iterator.
 *
 * This iterator reports its state as #GwyComputationStateType.
 *
 * Returns: A new correlation iterator.
 **/
GwyComputationState*
gwy_data_field_correlate_init(GwyDataField *data_field,
                              GwyDataField *kernel_field,
                              GwyDataField *score)
{
    GwyCorrelationState *state;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), NULL);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(kernel_field), NULL);
    g_return_val_if_fail(kernel_field->xres <= data_field->xres
                         && kernel_field->yres <= data_field->yres, NULL);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(score), NULL);

    state = g_new0(GwyCorrelationState, 1);
    state->cs.state = GWY_COMPUTATION_STATE_INIT;
    state->cs.fraction = 0.0;
    state->data_field = g_object_ref(data_field);
    state->kernel_field = g_object_ref(kernel_field);
    state->score = g_object_ref(score);

    return (GwyComputationState*)state;
}

/**
 * gwy_data_field_correlate_iteration:
 * @state: Correlation iterator.
 *
 * Performs one iteration of correlation.
 *
 * An iterator can be created with gwy_data_field_correlate_init().
 * When iteration ends, either by finishing or being aborted,
 * gwy_data_field_correlate_finalize() must be called to release allocated
 * resources.
 **/
void
gwy_data_field_correlate_iteration(GwyComputationState *cstate)
{
    GwyCorrelationState *state = (GwyCorrelationState*)cstate;
    gint xres, yres, kxres, kyres, k, xoff, yoff;
    gdouble s, davg, drms;

    xres = state->data_field->xres;
    yres = state->data_field->yres;
    kxres = state->kernel_field->xres;
    kyres = state->kernel_field->yres;
    xoff = (kxres - 1)/2;
    yoff = (kyres - 1)/2;

    if (state->cs.state == GWY_COMPUTATION_STATE_INIT) {
        gwy_data_field_fill(state->score, -1);
        state->kavg = gwy_data_field_get_avg(state->kernel_field);
        state->krms = gwy_data_field_get_rms(state->kernel_field);
        state->avg = gwy_data_field_duplicate(state->data_field);
        state->rms = gwy_data_field_duplicate(state->data_field);
        calculate_normalization(state->avg, state->rms, kxres, kyres);
        state->cs.state = GWY_COMPUTATION_STATE_ITERATE;
        state->cs.fraction = 0.0;
        state->i = yoff;
        state->j = xoff;
    }
    else if (state->cs.state == GWY_COMPUTATION_STATE_ITERATE) {
        k = state->i*xres + state->j;
        davg = state->avg->data[k];
        drms = state->rms->data[k];
        if (drms && state->krms) {
            s = gwy_data_field_get_raw_correlation_score(state->data_field,
                                                         state->kernel_field,
                                                         state->j - xoff,
                                                         state->i - yoff,
                                                         0, 0,
                                                         kxres, kyres,
                                                         davg, state->kavg);
            state->score->data[k] = s/(drms*state->krms);
        }
        else
            state->score->data[k] = 0.0;

        state->j++;
        if (state->j + kxres - xoff > xres) {
            state->j = xoff;
            state->i++;
            if (state->i + kyres - yoff > yres)
                state->cs.state = GWY_COMPUTATION_STATE_FINISHED;
        }
        state->cs.fraction += 1.0/((xres - kxres + 1)*(yres - kyres + 1));
        state->cs.fraction = MIN(state->cs.fraction, 1.0);
    }
    else if (state->cs.state == GWY_COMPUTATION_STATE_FINISHED)
        return;

    gwy_data_field_invalidate(state->score);
}

/**
 * gwy_data_field_correlate_finalize:
 * @state: Correlation iterator.
 *
 * Destroys a correlation iterator, freeing all resources.
 **/
void
gwy_data_field_correlate_finalize(GwyComputationState *cstate)
{
    GwyCorrelationState *state = (GwyCorrelationState*)cstate;

    GWY_OBJECT_UNREF(state->data_field);
    GWY_OBJECT_UNREF(state->kernel_field);
    GWY_OBJECT_UNREF(state->score);
    GWY_OBJECT_UNREF(state->avg);
    GWY_OBJECT_UNREF(state->rms);
    g_free(state);
}

/**
 * gwy_data_field_crosscorrelate:
 * @data_field1: A data field.
 * @data_field2: A data field.
 * @x_dist: A data field to store x-distances to.
 * @y_dist: A data field to store y-distances to.
 * @score: Data field to store correlation scores to.
 * @search_width: Search area width.
 * @search_height: Search area height.
 * @window_width: Correlation window width.  This parameter is not actually
 *                used.  Pass zero.
 * @window_height: Correlation window height.  This parameter is not actually
 *                 used.  Pass zero.
 *
 * Algorithm for matching two different images of the same object under changes.
 *
 * It does not use any special features
 * for matching. It simply searches for all points (with their neighbourhood)
 * of @data_field1 within @data_field2. Parameters @search_width and
 * @search_height
 * determine maimum area where to search for points. The area is cenetered
 * in the @data_field2 at former position of points at @data_field1.
 **/
void
gwy_data_field_crosscorrelate(GwyDataField *data_field1,
                              GwyDataField *data_field2, GwyDataField *x_dist,
                              GwyDataField *y_dist, GwyDataField *score,
                              gint search_width, gint search_height,
                              G_GNUC_UNUSED gint window_width,
                              G_GNUC_UNUSED gint window_height)
{
    gint xres, yres, i, j, m, n;
    gint imax, jmax;
    gdouble cormax, lscore;
    gdouble zm, zp, z0, ipos, jpos;

    g_return_if_fail(data_field1 != NULL && data_field2 != NULL);

    xres = data_field1->xres;
    yres = data_field1->yres;

    g_return_if_fail(xres == data_field2->xres && yres == data_field2->yres);

    gwy_data_field_clear(x_dist);
    gwy_data_field_clear(y_dist);
    gwy_data_field_clear(score);

    /*iterate over all the points */
    for (i = (search_width/2); i < (xres - search_width/2); i++) {
        for (j = (search_height/2); j < (yres - search_height/2); j++) {
            /*iterate over search area in the second datafield */
            imax = i;
            jmax = j;
            cormax = -1;
            for (m = (i - search_width); m < i; m++) {
                for (n = (j - search_height); n < j; n++) {
                    lscore =
                        gwy_data_field_get_correlation_score(data_field1,
                                                             data_field2,
                                                             i-search_width/2,
                                                             j-search_height/2,
                                                             m, n,
                                                             m + search_width,
                                                             n + search_height);

                    /* add a little to score at exactly same point
                     * - to prevent problems on flat data */
                    if (m == (i - search_width/2)
                        && n == (j - search_height/2))
                        lscore *= 1.0001;

                    if (cormax < lscore) {
                        cormax = lscore;
                        imax = m + search_width/2;
                        jmax = n + search_height/2;
                    }
                }
            }
            score->data[i + xres * j] = cormax;

            z0 = cormax;
            zm = gwy_data_field_get_correlation_score(data_field1,
                                                      data_field2,
                                                      i-search_width/2,
                                                      j-search_height/2,
                                                      imax - search_width/2 - 1,
                                                      jmax - search_height/2,
                                                      imax + search_width/2 - 1,
                                                      jmax + search_height/2);
            zp = gwy_data_field_get_correlation_score(data_field1,
                                                      data_field2,
                                                      i-search_width/2,
                                                      j-search_height/2,
                                                      imax - search_width/2 + 1,
                                                      jmax - search_height/2,
                                                      imax + search_width/2 + 1,
                                                      jmax + search_height/2);


            ipos = imax + (zm - zp)/(zm + zp - 2*z0)/2.0;
            x_dist->data[i + xres * j]
                = (gdouble)(ipos - i)*data_field1->xreal/data_field1->xres;

            zm = gwy_data_field_get_correlation_score(data_field1,
                                                      data_field2,
                                                      i-search_width/2,
                                                      j-search_height/2,
                                                      imax - search_width/2,
                                                      jmax - search_height/2 - 1,
                                                      imax + search_width/2,
                                                      jmax + search_height/2 - 1);
            zp = gwy_data_field_get_correlation_score(data_field1,
                                                      data_field2,
                                                      i-search_width/2,
                                                      j-search_height/2,
                                                      imax - search_width/2,
                                                      jmax - search_height/2 + 1,
                                                      imax + search_width/2,
                                                      jmax + search_height/2 + 1);


            jpos = jmax + (zm - zp)/(zm + zp - 2*z0)/2.0;
             y_dist->data[i + xres * j]
                = (gdouble)(jpos - j)*data_field1->yreal/data_field1->yres;
        }
    }

    gwy_data_field_invalidate(score);
    gwy_data_field_invalidate(x_dist);
    gwy_data_field_invalidate(y_dist);
}


/**
 * gwy_data_field_crosscorrelate_init:
 * @data_field1: A data field.
 * @data_field2: A data field.
 * @x_dist: A data field to store x-distances to, or %NULL.
 * @y_dist: A data field to store y-distances to, or %NULL.
 * @score: Data field to store correlation scores to, or %NULL.
 * @search_width: Search area width.
 * @search_height: Search area height.
 * @window_width: Correlation window width.
 * @window_height: Correlation window height.
 *
 * Initializes a cross-correlation iterator.
 *
 * This iterator reports its state as #GwyComputationStateType.
 *
 * Returns: A new cross-correlation iterator.
 **/
GwyComputationState*
gwy_data_field_crosscorrelate_init(GwyDataField *data_field1,
                                   GwyDataField *data_field2,
                                   GwyDataField *x_dist,
                                   GwyDataField *y_dist,
                                   GwyDataField *score,
                                   gint search_width,
                                   gint search_height,
                                   gint window_width,
                                   gint window_height)
{
    GwyCrossCorrelationState *state;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field1), NULL);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field2), NULL);
    g_return_val_if_fail(data_field1->xres == data_field2->xres
                     && data_field1->yres == data_field2->yres, NULL);
    g_return_val_if_fail(!x_dist || GWY_IS_DATA_FIELD(x_dist), NULL);
    g_return_val_if_fail(!y_dist || GWY_IS_DATA_FIELD(y_dist), NULL);
    g_return_val_if_fail(!score || GWY_IS_DATA_FIELD(score), NULL);

    state = g_new0(GwyCrossCorrelationState, 1);

    state->cs.state = GWY_COMPUTATION_STATE_INIT;
    state->cs.fraction = 0.0;
    state->data_field1 = g_object_ref(data_field1);
    state->data_field2 = g_object_ref(data_field2);

    if (x_dist)
        state->x_dist = g_object_ref(x_dist);
    if (y_dist)
        state->y_dist = g_object_ref(y_dist);
    if (score)
        state->score = g_object_ref(score);

    state->search_width = search_width;
    state->search_height = search_height;
    state->window_width = window_width;
    state->window_height = window_height;

    state->weights = gwy_data_field_new(window_width, window_height, 
                                        window_width, window_height, 1);
    gwy_data_field_fill(state->weights, 1);

    return (GwyComputationState*)state;
}

/**
 * gwy_data_field_crosscorrelate_set_weights:
 * @state: Cross-correlation iterator.
 * @type: Set windowing type to be set as correlation weight, see #GwyWindowingType for details.
 *
 * Sets the weight function to be used within iterative cross-correlation algorithm. By default (not setting it),
 * rectangular windowing is used. This function should be called before running first iteration to get consistent results.
*/
 
void 
gwy_data_field_crosscorrelate_set_weights(GwyComputationState *state, 
                                          GwyWindowingType type)
{
    GwyCrossCorrelationState *cstate = (GwyCrossCorrelationState*)state;
    g_return_if_fail(cstate->cs.state == GWY_COMPUTATION_STATE_INIT);

    gwy_data_field_fill(cstate->weights, 1);
    gwy_fft_window_data_field(cstate->weights, GWY_ORIENTATION_HORIZONTAL, type);
    gwy_fft_window_data_field(cstate->weights, GWY_ORIENTATION_VERTICAL, type);

}


/**
 * gwy_data_field_crosscorrelate_iteration:
 * @state: Cross-correlation iterator.
 *
 * Performs one iteration of cross-correlation.
 *
 * Cross-correlation matches two different images of the same object under
 * changes.
 *
 * It does not use any special features
 * for matching. It simply searches for all points (with their neighbourhood)
 * of @data_field1 within @data_field2. Parameters @search_width and
 * @search_height determine maimum area where to search for points.
 * The area is cenetered in the @data_field2 at former position of points at
 * @data_field1.
 *
 * A cross-correlation iterator can be created with
 * gwy_data_field_crosscorrelate_init().  When iteration ends, either
 * by finishing or being aborted, gwy_data_field_crosscorrelate_finalize()
 * must be called to release allocated resources.
 **/

void
gwy_data_field_crosscorrelate_iteration(GwyComputationState *cstate)
{
    GwyCrossCorrelationState *state = (GwyCrossCorrelationState*)cstate;
    gint xres, yres, m, n, k, col, row, colmax, rowmax;
    gdouble cormax, lscore;
    gdouble ipos, jpos, scores[9];
    gdouble xmaximum, ymaximum;
    


    xres = state->data_field1->xres;
    yres = state->data_field1->yres;

    if (state->cs.state == GWY_COMPUTATION_STATE_INIT) {
        if (state->x_dist)
            gwy_data_field_clear(state->x_dist);
        if (state->y_dist)
            gwy_data_field_clear(state->y_dist);
        if (state->score)
            gwy_data_field_clear(state->score);
        state->cs.state = GWY_COMPUTATION_STATE_ITERATE;
        state->cs.fraction = 0.0;
        state->i = state->window_width/2 + 1;
        state->j = state->window_height/2 + 1;

    }
    else if (state->cs.state == GWY_COMPUTATION_STATE_ITERATE) {
        //iterate over search area in the second datafield 
        col = colmax = state->i;
        row = rowmax = state->j;
        cormax = -G_MAXDOUBLE;
        for (m = MAX(0, row - state->search_height/2 - state->window_height/2); m < MIN(yres-state->window_height, (row + state->search_height/2 - state->window_height/2)); m++) {
            for (n = MAX(0, col - state->search_width/2 - state->window_width/2); n < MIN(xres-state->window_width, (col + state->search_width/2 - state->window_width/2)); n++) {


                lscore = gwy_data_field_get_weighted_correlation_score
                                                (state->data_field1,
                                                 state->data_field2,
                                                 state->weights,
                                                 col-state->window_width/2,
                                                 row-state->window_height/2,
                                                 n, 
                                                 m,
                                                 state->window_width,
                                                 state->window_height);
 


                // add a little to score at exactly same point - to prevent problems on flat data 
                if (m == 0 && n == 0) lscore *= 1.0001;

                if (lscore > cormax) {
                    cormax = lscore;
                    colmax = n + state->window_width/2;
                    rowmax = m + state->window_height/2;
                }
      

            }
        }

        if (state->score)
            state->score->data[col + xres * row] = cormax;

        if (state->x_dist || state->x_dist) {
            k = 0;
            for (m=-1; m<=1; m++) {
               for (n=-1; n<=1; n++) {
    
                  if (m==0 && n==0) scores[k++] = cormax;
                  else scores[k++] = gwy_data_field_get_weighted_correlation_score(state->data_field1,
                                                      state->data_field2,
                                                      state->weights,
                                                      col - state->window_width/2,
                                                      row - state->window_height/2,
                                                      colmax - state->window_width/2 + n,
                                                      rowmax - state->window_height/2 + m,
                                                      state->window_width,
                                                      state->window_height);
                  }
            }


            if (gwy_math_refine_maximum(scores, &xmaximum, &ymaximum))
            {
                ipos = colmax + xmaximum;
                jpos = rowmax + ymaximum;
            }
            else {
                ipos = colmax;
                jpos = rowmax;
            }


            state->x_dist->data[col + xres * row]
                = (ipos - col)*state->data_field1->xreal/state->data_field1->xres;

            state->y_dist->data[col + xres * row]
                = (jpos - row)*state->data_field1->yreal/state->data_field1->yres;
        }

        state->i++;
        if (state->i == (xres - state->window_width/2 - 1)) {
            state->i = state->window_width/2 + 1;
            state->j++;
            if (state->j == (yres - state->window_height/2 - 1)) {
                state->cs.state = GWY_COMPUTATION_STATE_FINISHED;
            }
        }
        state->cs.fraction
            += 1.0/(xres - state->window_width - 2)
                  /(yres - state->window_height - 2);
        state->cs.fraction = MIN(state->cs.fraction, 1.0);
    }
    else if (state->cs.state == GWY_COMPUTATION_STATE_FINISHED)
        return;

    if (state->score)
        gwy_data_field_invalidate(state->score);
    if (state->x_dist)
        gwy_data_field_invalidate(state->x_dist);
    if (state->y_dist)
        gwy_data_field_invalidate(state->y_dist);
}

/**
 * gwy_data_field_crosscorrelate_finalize:
 * @state: Cross-correlation iterator.
 *
 * Destroys a cross-correlation iterator, freeing all resources.
 **/
void
gwy_data_field_crosscorrelate_finalize(GwyComputationState *cstate)
{
    GwyCrossCorrelationState *state = (GwyCrossCorrelationState*)cstate;

    GWY_OBJECT_UNREF(state->data_field1);
    GWY_OBJECT_UNREF(state->data_field2);
    GWY_OBJECT_UNREF(state->x_dist);
    GWY_OBJECT_UNREF(state->y_dist);
    GWY_OBJECT_UNREF(state->score);
    GWY_OBJECT_UNREF(state->weights);
    g_free(state);
}

/************************** Documentation ****************************/

/**
 * SECTION:correlation
 * @title: correlation
 * @short_description: Correlation and crosscorrelation
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

