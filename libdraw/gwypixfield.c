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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111 USA
 */

#include <stdio.h>
#include <math.h>

#define DEBUG 1
#include <libgwyddion/gwymacros.h>
#include "gwypixfield.h"


/**
 * gwy_pixfield_do:
 * @pixbuf: A Gdk pixbuf to draw to.
 * @data_field: A data to draw.
 * @palette: A palette to draw with.
 *
 * Paints a pixbuf @pixbuf with data from @data_field using false color
 * palette @palette.
 *
 * FIXME: This is a provisory function, probably to be renamed, moved,
 * changed, etc.
 **/
void
gwy_pixfield_do(GdkPixbuf *pixbuf,
                GwyDataField *data_field,
                GwyPalette *palette)
{
    int xres, yres, i, j, palsize, rowstride, dval;
    guchar *pixels, *line;
    const guchar *samples, *s;
    gdouble maximum, minimum, cor;
    gdouble *row, *data;

    g_return_if_fail(GDK_IS_PIXBUF(pixbuf));
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(GWY_IS_PALETTE(palette));

    xres = gwy_data_field_get_xres(data_field);
    yres = gwy_data_field_get_yres(data_field);;
    data = gwy_data_field_get_data(data_field);
    maximum = gwy_data_field_get_max(data_field);
    minimum = gwy_data_field_get_min(data_field);
    if (minimum == maximum)
        maximum = 1e100;

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    samples = gwy_palette_get_samples(palette, &palsize);
    cor = (palsize-1.0)/(maximum-minimum);

    for (i = 0; i < yres; i++) {
        line = pixels + i*rowstride;
        row = data + i*xres;
        for (j = 0; j < xres; j++) {
            dval = (gint)((*(row++) - minimum)*cor + 0.5);
            /* simply index to the guchar samples, it's faster and no one
             * can tell the difference... */
            s = samples + 4*dval;
            *(line++) = *(s++);
            *(line++) = *(s++);
            *(line++) = *s;
        }
    }
}

/**
 * gwy_pixfield_do_with_range:
 * @pixbuf: A Gdk pixbuf to draw to.
 * @data_field: A data to draw.
 * @palette: A palette to draw with.
 * @minimum: The value corresponding to palette start.
 * @maximum: The value corresponding to palette end.
 *
 * Paints a pixbuf @pixbuf with data from @data_field using false color
 * palette @palette, stretched over given range (outliers get the edge
 * colors).
 *
 * FIXME: This is a provisory function, probably to be renamed, moved,
 * changed, etc.
 **/
void
gwy_pixfield_do_with_range(GdkPixbuf *pixbuf,
                           GwyDataField *data_field,
                           GwyPalette *palette,
                           gdouble minimum,
                           gdouble maximum)
{
    int xres, yres, i, j, palsize, rowstride, dval;
    guchar *pixels, *line;
    const guchar *samples, *s;
    gdouble cor;
    gdouble *row, *data;

    g_return_if_fail(GDK_IS_PIXBUF(pixbuf));
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(GWY_IS_PALETTE(palette));
    if (minimum == maximum)
        maximum = 1e100;

    xres = gwy_data_field_get_xres(data_field);
    yres = gwy_data_field_get_yres(data_field);;
    data = gwy_data_field_get_data(data_field);

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    samples = gwy_palette_get_samples(palette, &palsize);
    cor = (palsize-1.0)/(maximum-minimum);

    for (i = 0; i < yres; i++) {
        line = pixels + i*rowstride;
        row = data + i*xres;
        for (j = 0; j < xres; j++) {
            dval = (gint)((*(row++) - minimum)*cor + 0.5);
            dval = CLAMP(dval, 0, palsize-0.000001);
            /* simply index to the guchar samples, it's faster and no one
             * can tell the difference... */
            s = samples + 4*dval;
            *(line++) = *(s++);
            *(line++) = *(s++);
            *(line++) = *s;
        }
    }
}

/**
 * gwy_pixfield_do_mask:
 * @pixbuf: A Gdk pixbuf to draw to.
 * @data_field: A data to draw.
 * @color: A color to use.
 *
 * Paints a pixbuf @pixbuf with data from @data_field using a signle color
 * @color with opacity varying with data value.  The data range is assumed
 * to be [0,1).
 *
 * FIXME: This is a provisory function, probably to be renamed, moved,
 * changed, etc.
 **/
void
gwy_pixfield_do_mask(GdkPixbuf *pixbuf,
                     GwyDataField *data_field,
                     GwyRGBA *color)
{
    int xres, yres, i, j, rowstride;
    guchar *pixels, *line;
    guint32 pixel;
    gdouble *row, *data;
    gdouble cor;

    g_return_if_fail(GDK_IS_PIXBUF(pixbuf));
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(color);

    pixel = 0xff
            | ((guint32)(guchar)floor(255.99999*color->b) << 8)
            | ((guint32)(guchar)floor(255.99999*color->g) << 16)
            | ((guint32)(guchar)floor(255.99999*color->r) << 24);
    gdk_pixbuf_fill(pixbuf, pixel);
    if (!gdk_pixbuf_get_has_alpha(pixbuf))
        return;

    xres = gwy_data_field_get_xres(data_field);
    yres = gwy_data_field_get_yres(data_field);
    data = gwy_data_field_get_data(data_field);

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    cor = 255*color->a + 0.99999;
    gwy_debug("cor = %g", cor);

    for (i = 0; i < yres; i++) {
        line = pixels + i*rowstride + 3;
        row = data + i*xres;
        for (j = 0; j < xres; j++, row++) {
            gdouble val = CLAMP(*row, 0.0, 1.0);

            *line = (guchar)(cor*val);
            line += 4;
        }
    }
}



/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
