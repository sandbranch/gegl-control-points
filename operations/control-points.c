/*
 * Control points: local adjustments at points placed on the image, as a
 * GEGL operation
 *
 * control-points.c
 * Copyright 2026 by David
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Up to eight points, each with a position, a size, a color selectivity
 * and five adjustments (brightness, contrast, saturation, warmth and
 * structure). A point acts on the pixels near it that look like the
 * pixel under it: its weight at a pixel is a smooth spatial falloff
 * (zero beyond its size) times a Gaussian of the pixel's CIE Lab
 * distance to the color sampled under the point. The adjustments of the
 * points are mixed per pixel by their normalized weights, with an
 * implicit "no change" weight, so that one point alone acts with exactly
 * its own weight and overlapping points blend without seams. The idea is
 * Nik's U Point (US 6,728,421 and its family, all expired); the formulas
 * are this file's own, see README.md.
 *
 * The color under each point is read from the input at the point
 * whatever region is rendered (get_required_for_output asks for it), so
 * the result is the same for any tiling. Alpha is passed through; pixels
 * that are not finite are passed through unchanged.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

/* the number of points: GEGL properties are fixed per operation, see
 * README.md for why eight */
#define CP_N_POINTS 8

enum_start (cp_point)
  enum_value (CP_POINT_1, "p1", N_("Point 1"))
  enum_value (CP_POINT_2, "p2", N_("Point 2"))
  enum_value (CP_POINT_3, "p3", N_("Point 3"))
  enum_value (CP_POINT_4, "p4", N_("Point 4"))
  enum_value (CP_POINT_5, "p5", N_("Point 5"))
  enum_value (CP_POINT_6, "p6", N_("Point 6"))
  enum_value (CP_POINT_7, "p7", N_("Point 7"))
  enum_value (CP_POINT_8, "p8", N_("Point 8"))
enum_end (CpChosen)

enum_start (cp_view)
  enum_value (CP_VIEW_RESULT, "result", N_("Result"))
  enum_value (CP_VIEW_MASK,   "mask",   N_("Mask of this point"))
  enum_value (CP_VIEW_ALL,    "all",    N_("Mask of all points"))
enum_end (CpView)

property_enum (point, _("Point"), CpChosen, cp_point, CP_POINT_1)
  description (_("The point whose settings are shown below. It only "
                 "chooses what the dialog shows, and whose mask "
                 "\"Show\" shows; every point that is on acts."))

/* the properties of point n, shown in the dialog when it is the chosen
 * point and editable when it is on */
#define CP_POINT_PROPS(n, on, dx, dy)                                        \
  property_boolean (p##n##_enabled, _("Use this point"), on)                 \
    description (_("The point acts only when this is on."))                  \
    ui_meta ("visible", "point {p" #n "}")                                   \
                                                                             \
  property_double (p##n##_x, _("X"), dx)                                     \
    description (_("Where the point is, from the left edge (0) to the "     \
                   "right edge (1). Pick it on the image with the "          \
                   "button."))                                               \
    value_range (0.0, 1.0)                                                   \
    ui_meta ("unit", "relative-coordinate")                                  \
    ui_meta ("axis", "x")                                                    \
    ui_meta ("visible", "point {p" #n "}")                                   \
    ui_meta ("sensitive", "p" #n "-enabled")                                 \
                                                                             \
  property_double (p##n##_y, _("Y"), dy)                                     \
    description (_("Where the point is, from the top edge (0) to the "      \
                   "bottom edge (1)."))                                      \
    value_range (0.0, 1.0)                                                   \
    ui_meta ("unit", "relative-coordinate")                                  \
    ui_meta ("axis", "y")                                                    \
    ui_meta ("visible", "point {p" #n "}")                                   \
    ui_meta ("sensitive", "p" #n "-enabled")                                 \
                                                                             \
  property_double (p##n##_size, _("Size"), 20.0)                             \
    description (_("How far the point reaches, in percent of the longer "   \
                   "side of the image. Its influence falls smoothly to "     \
                   "nothing at this distance."))                             \
    value_range (0.5, 150.0)                                                 \
    ui_range (0.5, 100.0)                                                    \
    ui_gamma (1.5)                                                           \
    ui_digits (1)                                                            \
    ui_meta ("visible", "point {p" #n "}")                                   \
    ui_meta ("sensitive", "p" #n "-enabled")                                 \
                                                                             \
  property_double (p##n##_selectivity, _("Color selectivity"), 50.0)         \
    description (_("How much the point keeps to pixels of the color and "   \
                   "brightness under it: 0 acts on everything in reach, "    \
                   "100 only on nearly the same color."))                    \
    value_range (0.0, 100.0)                                                 \
    ui_digits (0)                                                            \
    ui_meta ("visible", "point {p" #n "}")                                   \
    ui_meta ("sensitive", "p" #n "-enabled")                                 \
                                                                             \
  property_double (p##n##_brightness, _("Brightness"), 0.0)                  \
    description (_("Brighter or darker, most in the midtones."))            \
    value_range (-100.0, 100.0)                                              \
    ui_digits (0)                                                            \
    ui_meta ("visible", "point {p" #n "}")                                   \
    ui_meta ("sensitive", "p" #n "-enabled")                                 \
                                                                             \
  property_double (p##n##_contrast, _("Contrast"), 0.0)                      \
    description (_("More or less contrast of the tones, around the "        \
                   "middle gray."))                                          \
    value_range (-100.0, 100.0)                                              \
    ui_digits (0)                                                            \
    ui_meta ("visible", "point {p" #n "}")                                   \
    ui_meta ("sensitive", "p" #n "-enabled")                                 \
                                                                             \
  property_double (p##n##_saturation, _("Saturation"), 0.0)                  \
    description (_("More or less colorful; -100 is gray."))                 \
    value_range (-100.0, 100.0)                                              \
    ui_digits (0)                                                            \
    ui_meta ("visible", "point {p" #n "}")                                   \
    ui_meta ("sensitive", "p" #n "-enabled")                                 \
                                                                             \
  property_double (p##n##_warmth, _("Warmth"), 0.0)                          \
    description (_("Warmer (towards yellow and red) or cooler (towards "    \
                   "blue); the brightness stays."))                          \
    value_range (-100.0, 100.0)                                              \
    ui_digits (0)                                                            \
    ui_meta ("visible", "point {p" #n "}")                                   \
    ui_meta ("sensitive", "p" #n "-enabled")                                 \
                                                                             \
  property_double (p##n##_structure, _("Structure"), 0.0)                    \
    description (_("More or less fine detail and texture (the size of "     \
                   "the detail is \"Structure size\")."))                    \
    value_range (-100.0, 100.0)                                              \
    ui_digits (0)                                                            \
    ui_meta ("visible", "point {p" #n "}")                                   \
    ui_meta ("sensitive", "p" #n "-enabled")

CP_POINT_PROPS (1, TRUE,  0.50, 0.50)
CP_POINT_PROPS (2, FALSE, 0.25, 0.25)
CP_POINT_PROPS (3, FALSE, 0.75, 0.25)
CP_POINT_PROPS (4, FALSE, 0.25, 0.75)
CP_POINT_PROPS (5, FALSE, 0.75, 0.75)
CP_POINT_PROPS (6, FALSE, 0.50, 0.25)
CP_POINT_PROPS (7, FALSE, 0.50, 0.75)
CP_POINT_PROPS (8, FALSE, 0.25, 0.50)

property_enum (view, _("Show"), CpView, cp_view, CP_VIEW_RESULT)
  description (_("The result, or where the points act (white: fully, "
                 "black: not at all), of the chosen point or of all "
                 "points, to set size and selectivity."))

property_double (structure_size, _("Structure size"), 4.0)
  description (_("The size (radius) in pixels of the detail that "
                 "Structure changes, for all points."))
  value_range (1.0, 100.0)
  ui_range (1.0, 30.0)
  ui_gamma (1.5)
  ui_digits (1)
  ui_meta ("unit", "pixel-distance")

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     control_points
#define GEGL_OP_C_SOURCE control-points.c

#include "gegl-op.h"
#include <math.h>
#include <string.h>

/* the method's constants (README.md) -------------------------------------- */

/* the color under a point: the mean of the finite pixels of a square of
 * this half width (5 x 5 pixels), so that noise does not decide */
#define CP_SAMPLE_HALF   2
/* selectivity s: a Gaussian in the color distance of width
 * CP_TAU_50 * 50 / s (in CIE Lab units, L from 0 to 100) */
#define CP_TAU_50        25.0f
/* lightness counts 0.4 in the color distance: the parts of one object
 * differ more in light and shade than in hue */
#define CP_L_WEIGHT      0.4f
/* warmth at 100: red gain 1 + this, blue gain 1 - this, luminance kept */
#define CP_WARMTH        0.3f
/* brightness b (-1 to 1): lightness L to L^(2^(-CP_BRIGHT b)) */
#define CP_BRIGHT        0.75f
/* contrast at 100 and -100: the slope at middle gray becomes 1 + this */
#define CP_CONTRAST_UP   0.45f
#define CP_CONTRAST_DOWN 0.6f
/* structure at 100: the detail (L minus its blur) times 1 + this; the
 * detail is soft limited to about CP_DETAIL_LIMIT in L so that strong
 * edges do not get halos */
#define CP_STRUCTURE     1.5f
#define CP_DETAIL_LIMIT  0.15f

#define CP_N 8

typedef struct
{
  /* linear RGB of the working space to CIE XYZ (D50), rows X, Y, Z */
  gfloat m[9];
} CpPriv;

/* one point that is on, in pixel coordinates of the input */
typedef struct
{
  gint    index;              /* 0 to 7 */
  gdouble cx, cy;             /* center, from the image's top left corner */
  gdouble radius;             /* reach, > 0 */
  gfloat  inv_r2;             /* 1 / radius^2 */
  gfloat  inv_2tau2;          /* 1 / (2 tau^2), 0 without selectivity */
  gfloat  lab[3];             /* the sampled color, CIE Lab */
  gboolean sampled;           /* lab is valid */
  gfloat  bright, contrast, sat, warm, structure;   /* -1 to 1 */
  GeglRectangle reach;        /* pixels with a weight > 0 */
  GeglRectangle sample;       /* the pixels the color is read from */
} CpPoint;

typedef struct
{
  CpPoint p[CP_N];
  gint    n;
  gdouble structure_sigma;
  gboolean any_structure;     /* a point on has structure != 0 */
} CpPoints;

/* the settings of point i (0 to 7), from the properties */
typedef struct
{
  gboolean on;
  gdouble  x, y, size, sel, bright, contrast, sat, warm, structure;
} CpSettings;

static void
cp_settings (GeglProperties *o,
             gint            i,
             CpSettings     *s)
{
#define CP_GET(n)                                                         \
  case n - 1:                                                             \
    s->on = o->p##n##_enabled; s->x = o->p##n##_x; s->y = o->p##n##_y;    \
    s->size = o->p##n##_size; s->sel = o->p##n##_selectivity;             \
    s->bright = o->p##n##_brightness; s->contrast = o->p##n##_contrast;   \
    s->sat = o->p##n##_saturation; s->warm = o->p##n##_warmth;            \
    s->structure = o->p##n##_structure;                                   \
    break;

  switch (i)
    {
      CP_GET (1) CP_GET (2) CP_GET (3) CP_GET (4)
      CP_GET (5) CP_GET (6) CP_GET (7) CP_GET (8)
    default:
      memset (s, 0, sizeof (*s));
    }
#undef CP_GET
}

static gboolean
cp_adjusts (const CpSettings *s)
{
  return s->bright != 0.0 || s->contrast != 0.0 || s->sat != 0.0 ||
         s->warm != 0.0 || s->structure != 0.0;
}

/* lightness from luminance and back, both 0 to 1 (L* / 100) */
static inline gfloat
cp_Y_to_L (gfloat Y)
{
  return Y > 216.0f / 24389.0f ? 1.16f * cbrtf (Y) - 0.16f : Y * (24389.0f / 2700.0f);
}

static inline gfloat
cp_L_to_Y (gfloat L)
{
  if (L > 0.08f)
    {
      gfloat t = (L + 0.16f) / 1.16f;
      return t * t * t;
    }
  return L * (2700.0f / 24389.0f);
}

static inline gfloat
cp_lab_f (gfloat t)
{
  return t > 216.0f / 24389.0f ? cbrtf (t) : t * (24389.0f / 27.0f / 116.0f) + 16.0f / 116.0f;
}

/* linear RGB to CIE Lab (L 0 to 100), D50 */
static inline void
cp_lab (const gfloat *m,
        const gfloat *rgb,
        gfloat       *lab)
{
  gfloat X  = m[0] * rgb[0] + m[1] * rgb[1] + m[2] * rgb[2];
  gfloat Y  = m[3] * rgb[0] + m[4] * rgb[1] + m[5] * rgb[2];
  gfloat Z  = m[6] * rgb[0] + m[7] * rgb[1] + m[8] * rgb[2];
  gfloat fx = cp_lab_f (X / 0.96420f);
  gfloat fy = cp_lab_f (Y);
  gfloat fz = cp_lab_f (Z / 0.82491f);

  lab[0] = 116.0f * fy - 16.0f;
  lab[1] = 500.0f * (fx - fy);
  lab[2] = 200.0f * (fy - fz);
}

static inline gboolean
cp_finite3 (const gfloat *p)
{
  return isfinite (p[0]) && isfinite (p[1]) && isfinite (p[2]);
}

/* the input's extent, or FALSE for none or an infinite one */
static gboolean
cp_extent (GeglOperation *operation,
           GeglRectangle *extent)
{
  const GeglRectangle *r = gegl_operation_source_get_bounding_box (operation, "input");

  if (! r || r->width <= 0 || r->height <= 0 || gegl_rectangle_is_infinite_plane (r))
    return FALSE;
  *extent = *r;
  return TRUE;
}

/* the points that are on, in pixels of the input; with need_view, also
 * the chosen point when the mask of it is shown although it has no
 * adjustments. Returns the number. */
static gint
cp_points (GeglOperation *operation,
           CpPoints      *pts)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  GeglRectangle   ext;
  gdouble         longer;
  gint            i;

  memset (pts, 0, sizeof (*pts));
  pts->structure_sigma = o->structure_size;
  if (! cp_extent (operation, &ext))
    return 0;
  longer = MAX (ext.width, ext.height);
  for (i = 0; i < CP_N; i++)
    {
      CpSettings s;
      CpPoint   *p = &pts->p[pts->n];
      gdouble    sel;
      gint       sx, sy;

      cp_settings (o, i, &s);
      if (! s.on)
        continue;
      p->index  = i;
      /* from the top left corner of the image, so that the pixels are
       * the same wherever the image is */
      p->cx     = CLAMP (s.x, 0.0, 1.0) * ext.width;
      p->cy     = CLAMP (s.y, 0.0, 1.0) * ext.height;
      p->radius = MAX (0.5, CLAMP (s.size, 0.5, 150.0) / 100.0 * longer);
      p->inv_r2 = (gfloat) (1.0 / (p->radius * p->radius));
      sel       = CLAMP (s.sel, 0.0, 100.0);
      if (sel > 0.0)
        {
          gdouble tau = CP_TAU_50 * 50.0 / sel;
          p->inv_2tau2 = (gfloat) (1.0 / (2.0 * tau * tau));
        }
      p->bright    = CLAMP (s.bright, -100.0, 100.0) / 100.0;
      p->contrast  = CLAMP (s.contrast, -100.0, 100.0) / 100.0;
      p->sat       = CLAMP (s.sat, -100.0, 100.0) / 100.0;
      p->warm      = CLAMP (s.warm, -100.0, 100.0) / 100.0;
      p->structure = CLAMP (s.structure, -100.0, 100.0) / 100.0;
      if (p->structure != 0.0f)
        pts->any_structure = TRUE;
      /* the pixels within the radius: centers at x + 0.5 */
      p->reach.x      = (gint) floor (p->cx - p->radius);
      p->reach.y      = (gint) floor (p->cy - p->radius);
      p->reach.width  = (gint) ceil (p->cx + p->radius) - p->reach.x + 1;
      p->reach.height = (gint) ceil (p->cy + p->radius) - p->reach.y + 1;
      p->reach.x     += ext.x;
      p->reach.y     += ext.y;
      /* the pixel under the point, inside the image, and its square */
      sx = ext.x + CLAMP ((gint) floor (p->cx), 0, ext.width - 1);
      sy = ext.y + CLAMP ((gint) floor (p->cy), 0, ext.height - 1);
      p->sample.x = sx - CP_SAMPLE_HALF;
      p->sample.y = sy - CP_SAMPLE_HALF;
      p->sample.width = p->sample.height = 2 * CP_SAMPLE_HALF + 1;
      gegl_rectangle_intersect (&p->sample, &p->sample, &ext);
      pts->n++;
    }
  return pts->n;
}

/* the margin the structure blur needs */
static gint
cp_margin (const CpPoints *pts)
{
  return pts->any_structure ? (gint) ceil (3.0 * pts->structure_sigma) : 0;
}

/* nothing to do: no point changes anything and only the result is shown */
static gboolean
cp_is_identity (GeglOperation *operation)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  gint            i;

  if (o->view != CP_VIEW_RESULT)
    return FALSE;
  for (i = 0; i < CP_N; i++)
    {
      CpSettings s;

      cp_settings (o, i, &s);
      if (s.on && cp_adjusts (&s))
        return FALSE;
    }
  return TRUE;
}

static void
prepare (GeglOperation *operation)
{
  GeglProperties *o      = GEGL_PROPERTIES (operation);
  const Babl     *space  = gegl_operation_get_source_space (operation, "input");
  const Babl     *format = babl_format_with_space ("RGBA float", space);
  CpPriv         *priv   = o->user_data;

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);

  if (! priv)
    priv = o->user_data = g_new0 (CpPriv, 1);
  {
    /* the matrix from linear RGB of the space to XYZ, from babl */
    static const gfloat unit[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    gfloat              xyz[9];
    const Babl         *fish = babl_fish (babl_format_with_space ("RGB float", space),
                                          babl_format ("CIE XYZ float"));
    gint                c;

    babl_process (fish, unit, xyz, 3);
    for (c = 0; c < 3; c++)
      {
        priv->m[0 + c] = xyz[c * 3 + 0];
        priv->m[3 + c] = xyz[c * 3 + 1];
        priv->m[6 + c] = xyz[c * 3 + 2];
      }
  }
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  CpPoints      pts;
  GeglRectangle r = *roi;
  gint          m, i;

  (void) input_pad;
  if (cp_is_identity (operation) || ! cp_points (operation, &pts))
    return r;
  m = cp_margin (&pts);
  r.x -= m;
  r.y -= m;
  r.width += 2 * m;
  r.height += 2 * m;
  /* the color under each point that reaches into roi */
  for (i = 0; i < pts.n; i++)
    if (gegl_rectangle_intersect (NULL, &pts.p[i].reach, roi) && pts.p[i].sample.width > 0)
      gegl_rectangle_bounding_box (&r, &r, &pts.p[i].sample);
  return r;
}

static GeglRectangle
get_invalidated_by_change (GeglOperation       *operation,
                           const gchar         *input_pad,
                           const GeglRectangle *input_region)
{
  CpPoints      pts;
  GeglRectangle r = *input_region;
  gint          m, i;

  (void) input_pad;
  if (! cp_points (operation, &pts))
    return r;
  m = cp_margin (&pts);
  r.x -= m;
  r.y -= m;
  r.width += 2 * m;
  r.height += 2 * m;
  /* a change under a point changes all it reaches */
  for (i = 0; i < pts.n; i++)
    if (gegl_rectangle_intersect (NULL, &pts.p[i].sample, input_region))
      gegl_rectangle_bounding_box (&r, &r, &pts.p[i].reach);
  return r;
}

/* the color under each point, from the input */
static void
cp_sample (const CpPriv *priv,
           GeglBuffer   *input,
           const Babl   *format,
           CpPoints     *pts)
{
  gint i;

  for (i = 0; i < pts->n; i++)
    {
      CpPoint *p = &pts->p[i];
      gfloat   buf[(2 * CP_SAMPLE_HALF + 1) * (2 * CP_SAMPLE_HALF + 1) * 4];
      gdouble  sum[3] = { 0, 0, 0 };
      gint     k, n = 0, count = p->sample.width * p->sample.height;

      p->sampled = FALSE;
      if (count <= 0)
        continue;
      gegl_buffer_get (input, &p->sample, 1.0, format, buf, GEGL_AUTO_ROWSTRIDE,
                       GEGL_ABYSS_NONE);
      for (k = 0; k < count; k++)
        if (cp_finite3 (buf + k * 4))
          {
            sum[0] += buf[k * 4 + 0];
            sum[1] += buf[k * 4 + 1];
            sum[2] += buf[k * 4 + 2];
            n++;
          }
      if (n)
        {
          gfloat mean[3] = { sum[0] / n, sum[1] / n, sum[2] / n };

          cp_lab (priv->m, mean, p->lab);
          p->sampled = isfinite (p->lab[0]) && isfinite (p->lab[1]) && isfinite (p->lab[2]);
        }
    }
}

/* the Gaussian blur of the lightness plane (w x h, in place), with the
 * edges of the plane clamped: the plane covers the region plus the
 * margin, cut to the image, so the result of a pixel does not depend on
 * the region */
static void
cp_blur (gfloat *plane,
         gint    w,
         gint    h,
         gdouble sigma)
{
  gint    rad = (gint) ceil (3.0 * sigma);
  gfloat *k   = g_new (gfloat, 2 * rad + 1);
  gfloat *tmp = g_new (gfloat, (gsize) MAX (w, h));
  gdouble sum = 0;
  gint    i, x, y;

  for (i = -rad; i <= rad; i++)
    sum += k[i + rad] = (gfloat) exp (-0.5 * i * i / (sigma * sigma));
  for (i = 0; i <= 2 * rad; i++)
    k[i] = (gfloat) (k[i] / sum);

  for (y = 0; y < h; y++)
    {
      gfloat *row = plane + (gsize) y * w;

      for (x = 0; x < w; x++)
        {
          gfloat acc = 0;
          for (i = -rad; i <= rad; i++)
            acc += k[i + rad] * row[CLAMP (x + i, 0, w - 1)];
          tmp[x] = acc;
        }
      memcpy (row, tmp, sizeof (gfloat) * w);
    }
  for (x = 0; x < w; x++)
    {
      for (y = 0; y < h; y++)
        {
          gfloat acc = 0;
          for (i = -rad; i <= rad; i++)
            acc += k[i + rad] * plane[(gsize) CLAMP (y + i, 0, h - 1) * w + x];
          tmp[y] = acc;
        }
      for (y = 0; y < h; y++)
        plane[(gsize) y * w + x] = tmp[y];
    }
  g_free (k);
  g_free (tmp);
}

/* the adjustments at one pixel: warmth, saturation, then the tone
 * (brightness, contrast, structure) on lightness, the chromaticity kept */
static inline void
cp_adjust (const CpPriv *priv,
           gfloat       *px,
           gfloat        bright,
           gfloat        contrast,
           gfloat        sat,
           gfloat        warm,
           gfloat        detail_gain,
           gfloat        detail)
{
  const gfloat *m = priv->m;
  gfloat        Y = m[3] * px[0] + m[4] * px[1] + m[5] * px[2];
  gint          c;

  if (warm != 0.0f)
    {
      gfloat Y2;

      px[0] *= 1.0f + CP_WARMTH * warm;
      px[2] *= 1.0f - CP_WARMTH * warm;
      Y2 = m[3] * px[0] + m[4] * px[1] + m[5] * px[2];
      if (Y2 > 1e-9f && Y > 0.0f)
        for (c = 0; c < 3; c++)
          px[c] *= Y / Y2;
    }

  if (sat != 0.0f)
    {
      gfloat k = 1.0f + sat;

      if (k > 1.0f)
        {
          /* no channel below 0 or above the brightest of 1 and the
           * pixel's own: more saturation only as far as the color can go */
          gfloat top  = MAX (1.0f, MAX (px[0], MAX (px[1], px[2])));
          gfloat kmax = G_MAXFLOAT;

          for (c = 0; c < 3; c++)
            {
              gfloat d = px[c] - Y;

              if (d < 0.0f && Y >= 0.0f)
                kmax = MIN (kmax, Y / -d);
              else if (d > 0.0f)
                kmax = MIN (kmax, (top - Y) / d);
            }
          k = MIN (k, MAX (1.0f, kmax));
        }
      for (c = 0; c < 3; c++)
        px[c] = Y + (px[c] - Y) * k;
    }

  if (bright != 0.0f || contrast != 0.0f || detail_gain != 0.0f)
    {
      gfloat L  = cp_Y_to_L (Y);
      gfloat L2 = L, Y2;

      if (bright != 0.0f && L > 0.0f)
        {
          gfloat e = exp2f (-CP_BRIGHT * bright);

          L2 = L2 <= 1.0f ? powf (L2, e) : 1.0f + e * (L2 - 1.0f);
        }
      if (contrast != 0.0f)
        {
          gfloat a = contrast > 0.0f ? CP_CONTRAST_UP * contrast : CP_CONTRAST_DOWN * contrast;
          gfloat t = CLAMP (L2, 0.0f, 1.0f);

          L2 += a * 2.0f * (2.0f * t - 1.0f) * t * (1.0f - t);
        }
      if (detail_gain != 0.0f)
        {
          gfloat d = detail / (1.0f + fabsf (detail) / CP_DETAIL_LIMIT);

          L2 += detail_gain * d;
        }
      if (L >= 0.0f && L2 < 0.0f)
        L2 = 0.0f;
      Y2 = cp_L_to_Y (L2);
      if (Y > 1e-6f)
        {
          gfloat f = Y2 / Y;
          for (c = 0; c < 3; c++)
            px[c] *= f;
        }
      else
        {
          for (c = 0; c < 3; c++)
            px[c] += Y2 - Y;
        }
    }
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o      = GEGL_PROPERTIES (operation);
  CpPriv         *priv   = o->user_data;
  const Babl     *format = gegl_operation_get_format (operation, "output");
  CpPoints        pts;
  CpPoint        *act[CP_N];
  gint            n_act = 0, i, x, y;
  gint            chosen = -1;
  gboolean        need_detail = FALSE;
  GeglRectangle   ext, prect = { 0, };
  gfloat         *buf, *plane = NULL;
  gfloat          detail_gain[CP_N];

  (void) level;
  cp_points (operation, &pts);
  cp_extent (operation, &ext);

  /* the points that reach into roi */
  for (i = 0; i < pts.n; i++)
    if (gegl_rectangle_intersect (NULL, &pts.p[i].reach, roi))
      {
        act[n_act++] = &pts.p[i];
        if (pts.p[i].index == (gint) o->point)
          chosen = n_act - 1;
      }

  if (n_act == 0 && o->view == CP_VIEW_RESULT)
    {
      gegl_buffer_copy (input, roi, GEGL_ABYSS_NONE, output, roi);
      return TRUE;
    }

  cp_sample (priv, input, format, &pts);
  for (i = 0; i < n_act; i++)
    {
      detail_gain[i] = act[i]->structure > 0.0f ? CP_STRUCTURE * act[i]->structure
                                                : act[i]->structure;
      if (act[i]->structure != 0.0f && o->view == CP_VIEW_RESULT)
        need_detail = TRUE;
    }

  buf = g_new (gfloat, (gsize) roi->width * roi->height * 4);
  gegl_buffer_get (input, roi, 1.0, format, buf, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

  if (need_detail)
    {
      /* lightness of roi plus the margin, cut to the image; then its
       * detail, L minus the blur, in the plane */
      gint    mg = cp_margin (&pts);
      gfloat *pb;
      gint    pw, ph;

      prect.x = roi->x - mg;
      prect.y = roi->y - mg;
      prect.width = roi->width + 2 * mg;
      prect.height = roi->height + 2 * mg;
      gegl_rectangle_intersect (&prect, &prect, &ext);
      pw = prect.width;
      ph = prect.height;
      pb = g_new (gfloat, (gsize) pw * ph * 4);
      plane = g_new (gfloat, (gsize) pw * ph);
      gegl_buffer_get (input, &prect, 1.0, format, pb, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
      for (i = 0; i < pw * ph; i++)
        {
          const gfloat *p = pb + (gsize) i * 4;
          gfloat        L = cp_finite3 (p)
                            ? cp_Y_to_L (priv->m[3] * p[0] + priv->m[4] * p[1] + priv->m[5] * p[2])
                            : 0.0f;
          plane[i] = isfinite (L) ? L : 0.0f;
        }
      g_free (pb);
      cp_blur (plane, pw, ph, pts.structure_sigma);
    }

  for (y = 0; y < roi->height; y++)
    {
      gint    gy = roi->y + y;
      gfloat  fy = (gy - ext.y) + 0.5f;
      CpPoint *row[CP_N];
      gfloat  row_dy2[CP_N];
      gint    row_x0[CP_N], row_x1[CP_N], row_k[CP_N];
      gint    n_row = 0;

      /* the points that reach this row, and where */
      for (i = 0; i < n_act; i++)
        {
          CpPoint *p  = act[i];
          gdouble  dy = fy - p->cy;
          gdouble  hw;

          if (dy * dy >= p->radius * p->radius)
            continue;
          hw = sqrt (p->radius * p->radius - dy * dy);
          row_x0[n_row] = MAX (roi->x, ext.x + (gint) floor (p->cx - hw - 0.5));
          row_x1[n_row] = MIN (roi->x + roi->width - 1, ext.x + (gint) ceil (p->cx + hw - 0.5));
          if (row_x0[n_row] > row_x1[n_row])
            continue;
          row_dy2[n_row] = (gfloat) (dy * dy);
          row_k[n_row]   = i;
          row[n_row++]   = p;
        }
      if (n_row == 0 && o->view == CP_VIEW_RESULT)
        continue;

      for (x = 0; x < roi->width; x++)
        {
          gfloat *px = buf + ((gsize) y * roi->width + x) * 4;
          gint    gx = roi->x + x;
          gfloat  fx = (gx - ext.x) + 0.5f;
          gfloat  w[CP_N];
          gfloat  lab[3];
          gboolean have_lab = FALSE;
          gfloat  sum = 0.0f, keep = 1.0f, alpha, scale;
          gint    j;

          if (! cp_finite3 (px))
            continue;
          for (j = 0; j < n_row; j++)
            {
              CpPoint *p = row[j];
              gfloat   ws, d2;

              w[j] = 0.0f;
              if (gx < row_x0[j] || gx > row_x1[j])
                continue;
              d2 = ((fx - (gfloat) p->cx) * (fx - (gfloat) p->cx) + row_dy2[j]) * p->inv_r2;
              if (d2 >= 1.0f)
                continue;
              ws = (1.0f - d2) * (1.0f - d2);
              if (p->inv_2tau2 > 0.0f && p->sampled)
                {
                  gfloat dl, da, db;

                  if (! have_lab)
                    {
                      cp_lab (priv->m, px, lab);
                      have_lab = TRUE;
                    }
                  dl = CP_L_WEIGHT * (lab[0] - p->lab[0]);
                  da = lab[1] - p->lab[1];
                  db = lab[2] - p->lab[2];
                  ws *= expf (-(dl * dl + da * da + db * db) * p->inv_2tau2);
                  if (! isfinite (ws))
                    ws = 0.0f;
                }
              w[j] = ws;
              sum += ws;
              keep *= 1.0f - ws;
            }

          /* the points' share: their union 1 - prod (1 - w), divided among
           * them by their weights (the rest is the implicit "no change") */
          alpha = 1.0f - keep;
          scale = sum > 0.0f ? alpha / sum : 0.0f;

          if (o->view != CP_VIEW_RESULT)
            {
              gfloat mval = 0.0f, Ym;

              if (o->view == CP_VIEW_ALL)
                mval = alpha;
              else
                for (j = 0; j < n_row; j++)
                  if (row_k[j] == chosen)
                    mval = w[j] * scale;
              Ym = cp_L_to_Y (CLAMP (mval, 0.0f, 1.0f));
              px[0] = px[1] = px[2] = Ym;
              continue;
            }
          if (sum <= 0.0f)
            continue;

          {
            gfloat br = 0, co = 0, sa = 0, wa = 0, dg = 0, detail = 0;

            for (j = 0; j < n_row; j++)
              {
                gfloat  b = w[j] * scale;
                CpPoint *p = row[j];

                br += b * p->bright;
                co += b * p->contrast;
                sa += b * p->sat;
                wa += b * p->warm;
                dg += b * detail_gain[row_k[j]];
              }
            if (dg != 0.0f && plane)
              {
                gfloat L = cp_Y_to_L (priv->m[3] * px[0] + priv->m[4] * px[1] +
                                      priv->m[5] * px[2]);
                detail = L - plane[(gsize) (gy - prect.y) * prect.width + (gx - prect.x)];
                if (! isfinite (detail))
                  detail = 0.0f;
              }
            cp_adjust (priv, px, br, co, sa, wa, dg, detail);
          }
        }
    }

  gegl_buffer_set (output, roi, 0, format, buf, GEGL_AUTO_ROWSTRIDE);
  g_free (buf);
  g_free (plane);
  return TRUE;
}

static gboolean
operation_process (GeglOperation        *operation,
                   GeglOperationContext *context,
                   const gchar          *output_prop,
                   const GeglRectangle  *result,
                   gint                  level)
{
  GeglRectangle ext;

  if (cp_is_identity (operation) || ! cp_extent (operation, &ext))
    {
      /* nothing changes: the input itself, bit for bit */
      GObject *input = gegl_operation_context_get_object (context, "input");

      gegl_operation_context_set_object (context, "output", input);
      return TRUE;
    }
  return GEGL_OPERATION_CLASS (gegl_op_parent_class)->process (operation, context, output_prop,
                                                               result, level);
}

static void
finalize (GObject *object)
{
  GeglProperties *o = GEGL_PROPERTIES (object);

  g_clear_pointer (&o->user_data, g_free);
  G_OBJECT_CLASS (gegl_op_parent_class)->finalize (object);
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GObjectClass             *object_class    = G_OBJECT_CLASS (klass);
  GeglOperationClass       *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationFilterClass *filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  object_class->finalize                     = finalize;
  operation_class->prepare                   = prepare;
  operation_class->process                   = operation_process;
  operation_class->get_required_for_output   = get_required_for_output;
  operation_class->get_invalidated_by_change = get_invalidated_by_change;
  operation_class->threaded                  = TRUE;
  filter_class->process                      = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "control-points:adjust",
    "title",           _("Control Points"),
    "categories",      "color",
    "description",     _("Brightness, contrast, saturation, warmth and "
                         "structure at up to eight points on the image; "
                         "each point acts on the pixels near it that look "
                         "like the pixel under it (U Point style)."),
    "gimp:menu-path",  "<Image>/Colors",
    "gimp:menu-label", _("Control Points..."),
    NULL);
}

#endif
