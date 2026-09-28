/*
 * Automated checks for control-points:adjust
 *
 * check.c
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
 * Runs the operation on generated images and measures what it must do:
 * a point on a red flower among green leaves changes the flower and not
 * the leaves at the same distance (for each selectivity), the falloff
 * with distance (the mask against its formula, the effect monotonic and
 * zero beyond the size), two points blend without seams and two points
 * with the same settings never add up to more than one, a neutral point
 * protects, each adjustment does what it says, identity (bit exact) when
 * no point changes anything, alpha, broken pixels (NaN, infinity) and
 * huge values, buffer formats and RGB spaces, the masks, the region a
 * change invalidates, and the same result for any tile size, number of
 * threads and requested region (also regions far from the points, whose
 * color must still be read under the points). With the photos of
 * tests/images (tests/images/fetch.py), the flower selectivity on real
 * photos too.
 *
 *   check <build folder>
 *
 * Prints PASS, FAIL or NOTE per case and exits with 1 if any failed.
 */

#include <gegl.h>
#include <gegl-plugin.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modules.h"

#define OP  "control-points:adjust"
#define LIN "RGBA float"

static gint n_failed   = 0;
static gint n_passed   = 0;
static gint n_messages = 0;

static void
log_handler (const gchar    *domain,
             GLogLevelFlags  level,
             const gchar    *message,
             gpointer        data)
{
  (void) data;
  if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING))
    n_messages++;
  g_log_default_handler (domain, level, message, NULL);
}

static void report (const gchar *name, gboolean ok, const gchar *format, ...) G_GNUC_PRINTF (3, 4);

static void
report (const gchar *name,
        gboolean     ok,
        const gchar *format,
        ...)
{
  printf ("%s  %s", ok ? "PASS" : "FAIL", name);
  if (format)
    {
      va_list args;
      gchar  *detail;

      va_start (args, format);
      detail = g_strdup_vprintf (format, args);
      va_end (args);
      printf (": %s", detail);
      g_free (detail);
    }
  printf ("\n");
  fflush (stdout);
  if (ok)
    n_passed++;
  else
    n_failed++;
}

static void note (const gchar *name, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

static void
note (const gchar *name,
      const gchar *format,
      ...)
{
  va_list args;
  gchar  *detail;

  va_start (args, format);
  detail = g_strdup_vprintf (format, args);
  va_end (args);
  printf ("NOTE  %s: %s\n", name, detail);
  fflush (stdout);
  g_free (detail);
}

static gchar *fmt_props (const gchar *format, ...) G_GNUC_PRINTF (1, 2);

static gchar *
fmt_props (const gchar *format, ...)
{
  va_list args;
  gchar  *s;

  va_start (args, format);
  s = g_strdup_vprintf (format, args);
  va_end (args);
  return s;
}

/* lightness, in double precision ---------------------------------------- */

static gdouble
L_to_Y (gdouble L)
{
  return L > 0.08 ? pow ((L + 0.16) / 1.16, 3.0) : L * 2700.0 / 24389.0;
}

static gdouble
Y_to_L (gdouble Y)
{
  return Y > 216.0 / 24389.0 ? 1.16 * cbrt (Y) - 0.16 : Y * 24389.0 / 2700.0;
}

/* babl's luminance of the sRGB primaries (D50 adapted) */
static gdouble
lum (const gfloat *p)
{
  return 0.22248794 * p[0] + 0.71690384 * p[1] + 0.06060826 * p[2];
}

/* CIE Lab of n pixels of LIN (sRGB), from babl (not the operation's own
 * formulas), L 0 to 100 */
static gfloat *
to_lab (const gfloat *img,
        gsize         n)
{
  gfloat *lab = g_new (gfloat, n * 3);

  babl_process (babl_fish (babl_format (LIN), babl_format ("CIE Lab float")), img, lab, (long) n);
  return lab;
}

/* running the operation ------------------------------------------------ */

/* the origin of the input buffer (a layer that is not at 0, 0) */
static gint origin_x = 0, origin_y = 0;

static GeglBuffer *
new_buffer (gint        w,
            gint        h,
            const Babl *fmt,
            gint        tile_w,
            gint        tile_h)
{
  if (tile_w > 0)
    return g_object_new (GEGL_TYPE_BUFFER, "x", origin_x, "y", origin_y, "width", w,
                         "height", h, "format", fmt, "tile-width", tile_w,
                         "tile-height", tile_h, NULL);
  return gegl_buffer_new (GEGL_RECTANGLE (origin_x, origin_y, w, h), fmt);
}

static void
set_props (GeglNode    *node,
           const gchar *props)
{
  gchar **p = g_strsplit (props ? props : "", " ", -1);
  gint    i;

  for (i = 0; p[i]; i++)
    if (p[i][0] && ! node_set_from_string (node, p[i]))
      report ("set_property", FALSE, "%s", p[i]);
  g_strfreev (p);
}

/* the operation with props ("name=value ...") on w x h pixels in in_fmt,
 * the result in out_fmt, of the region roi (NULL: all, relative to the
 * buffer's origin); tile_w > 0 gives the input buffer that tile size.
 * g_free the result. */
static gpointer
run_full (const gchar         *props,
          const Babl          *in_fmt,
          gconstpointer        in,
          gint                 w,
          gint                 h,
          const Babl          *out_fmt,
          const GeglRectangle *roi,
          gint                 tile_w,
          gint                 tile_h)
{
  GeglBuffer   *buf = new_buffer (w, h, in_fmt, tile_w, tile_h);
  GeglNode     *graph, *src, *node;
  GeglRectangle r = roi ? *roi : *GEGL_RECTANGLE (0, 0, w, h);
  gpointer      out;

  r.x += origin_x;
  r.y += origin_y;
  gegl_buffer_set (buf, NULL, 0, in_fmt, in, GEGL_AUTO_ROWSTRIDE);
  graph = gegl_node_new ();
  src   = gegl_node_new_child (graph, "operation", "gegl:buffer-source", "buffer", buf, NULL);
  node  = gegl_node_new_child (graph, "operation", OP, NULL);
  set_props (node, props);
  gegl_node_link (src, node);
  out = g_malloc0 ((gsize) r.width * r.height * babl_format_get_bytes_per_pixel (out_fmt));
  gegl_node_blit (node, 1.0, &r, out_fmt, out, GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
  g_object_unref (graph);
  g_object_unref (buf);
  return out;
}

static gfloat *
run (const gchar  *props,
     const gfloat *in,
     gint          w,
     gint          h)
{
  return run_full (props, babl_format (LIN), in, w, h, babl_format (LIN), NULL, 0, 0);
}

static gdouble
max_diff (const gfloat *a,
          const gfloat *b,
          gsize         n)
{
  gdouble d = 0;
  gsize   i;

  for (i = 0; i < n; i++)
    {
      gdouble e = fabs ((gdouble) a[i] - b[i]);
      if (! (e <= d))
        d = isnan (e) ? INFINITY : e;
    }
  return d;
}

static GRand *rnd;

/* images ------------------------------------------------------------------- */

static void
set_gray (gfloat  *p,
          gdouble  L)
{
  gfloat Y = L_to_Y (L);

  p[0] = p[1] = p[2] = Y;
  p[3] = 1.0f;
}

static gfloat *
flat (gint    w,
      gint    h,
      gdouble L)
{
  gfloat *img = g_new (gfloat, (gsize) w * h * 4);
  gint    i;

  for (i = 0; i < w * h; i++)
    set_gray (img + (gsize) i * 4, L);
  return img;
}

/* a color from CIE Lab (L 0 to 100), as linear sRGB, clipped to 0 */
static void
set_lab (gfloat *p,
         gdouble L,
         gdouble a,
         gdouble b)
{
  gfloat lab[3] = { L, a, b };
  gint   c;

  babl_process (babl_fish (babl_format ("CIE Lab float"), babl_format ("RGB float")), lab, p, 1);
  for (c = 0; c < 3; c++)
    p[c] = MAX (p[c], 0.0f);
  p[3] = 1.0f;
}

/* a red flower among green leaves, w x h: the flower a disc of radius
 * flower_r at the center, with shading and noise, crossed by green leaf
 * stripes, so that red and green pixels lie at the same distances from
 * a point on the flower; around it green leaves of varying lightness.
 * is_red (w x h) marks the flower's pixels. */
static gfloat *
flower_scene (gint      w,
              gint      h,
              gdouble   flower_r,
              guint8  **is_red)
{
  gfloat *img = g_new (gfloat, (gsize) w * h * 4);
  guint8 *red = g_new0 (guint8, (gsize) w * h);
  gint    x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *p  = img + ((gsize) y * w + x) * 4;
        gdouble dx = x + 0.5 - w / 2.0, dy = y + 0.5 - h / 2.0;
        gdouble d  = sqrt (dx * dx + dy * dy);
        gdouble n  = g_rand_double_range (rnd, -1, 1);
        /* leaf stripes at 30 degrees, 14 of 40 pixels wide */
        gboolean leaf_stripe = fmod (fabs (0.866 * x + 0.5 * y), 40.0) < 14.0;

        if (d < flower_r && ! leaf_stripe)
          {
            /* petals: L 38 to 52, a strong red */
            gdouble L = 45 + 7 * sin (d * 0.08 + atan2 (dy, dx) * 5) + 2 * n;
            set_lab (p, L, 62 + 3 * n, 45 + 3 * n);
            red[(gsize) y * w + x] = 1;
          }
        else
          {
            /* leaves: L 30 to 60, green */
            gdouble L = 45 + 12 * sin (x * 0.05) * cos (y * 0.07) + 3 * sin (x * 0.4 + y * 0.3) + 2 * n;
            set_lab (p, L, -38 + 4 * n, 40 + 4 * n);
          }
      }
  *is_red = red;
  return img;
}

/* a photo-like image with colour and texture everywhere */
static gfloat *
photo (gint w,
       gint h)
{
  gfloat *img = g_new (gfloat, (gsize) w * h * 4);
  gdouble ph[10];
  gint    x, y, k;

  for (k = 0; k < 10; k++)
    ph[k] = g_rand_double_range (rnd, 0, 2 * G_PI);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gfloat *p = img + ((gsize) y * w + x) * 4;
        gdouble u = (gdouble) x / w, v = (gdouble) y / h;
        gdouble L = 55 + 25 * sin (3 * u + ph[0]) * cos (2 * v + ph[1])
                  + 6 * sin (x * 0.21 + ph[2]) * sin (y * 0.17 + ph[3])
                  + 3 * sin (x * 0.9 + ph[4]) * cos (y * 1.1 + ph[5]);
        gdouble a = 50 * sin (4 * u + ph[6]) * cos (3 * v + ph[7]);
        gdouble b = 50 * cos (5 * v + ph[8]) * sin (2 * u + ph[9]);

        set_lab (p, CLAMP (L, 2, 98), a, b);
      }
  return img;
}

/* the settings that change an image a lot: three points on, with
 * structure and selectivity */
#define BUSY "p1-x=0.3 p1-y=0.4 p1-size=40 p1-brightness=40 p1-structure=60 p1-saturation=30 " \
             "p2-enabled=true p2-x=0.7 p2-y=0.6 p2-size=35 p2-contrast=50 p2-warmth=-60 p2-selectivity=80 " \
             "p3-enabled=true p3-x=0.55 p3-y=0.2 p3-size=25 p3-saturation=-80 p3-selectivity=0 " \
             "structure-size=3"

/* ============================================================== */

static void
check_registration (void)
{
  GParamSpec        **specs;
  guint               n, i, n_picker = 0;
  const gchar        *menu;

  printf ("-- registration\n");
  report ("registered", gegl_has_operation (OP), "%s", OP);
  if (! gegl_has_operation (OP))
    return;
  menu  = gegl_operation_get_key (OP, "gimp:menu-path");
  report ("menu_path", menu && strcmp (menu, "<Image>/Colors") == 0, "%s", menu ? menu : "none");
  specs = gegl_operation_list_properties (OP, &n);
  /* x immediately followed by y, both relative coordinates: GIMP's
   * "Pick coordinates from the image" button (gimppropgui-generic.c) */
  for (i = 0; i + 1 < n; i++)
    {
      const gchar *ax = gegl_param_spec_get_property_key (specs[i], "axis");
      const gchar *ay = gegl_param_spec_get_property_key (specs[i + 1], "axis");
      const gchar *ux = gegl_param_spec_get_property_key (specs[i], "unit");
      const gchar *uy = gegl_param_spec_get_property_key (specs[i + 1], "unit");

      if (ax && ay && ux && uy && ! strcmp (ax, "x") && ! strcmp (ay, "y") &&
          ! strcmp (ux, "relative-coordinate") && ! strcmp (uy, "relative-coordinate"))
        n_picker++;
    }
  report ("properties", n == 8 * 10 + 3 && n_picker == 8,
          "%u properties (8 points of 10, and point, view, structure-size), %u x/y pairs "
          "that get GIMP's coordinate picker", n, n_picker);
  g_free (specs);
}

/* no point changes anything: the input comes out bit for bit */
static void
check_identity (void)
{
  static const gchar *const cases[] = {
    "",                                          /* the defaults: point 1 on, all 0 */
    "p1-enabled=false p1-brightness=80 p2-saturation=50 p3-structure=100",   /* all off */
    "p2-enabled=true p3-enabled=true p4-enabled=true p5-enabled=true p6-enabled=true "
    "p7-enabled=true p8-enabled=true p1-selectivity=100 p3-size=150",       /* all on, all 0 */
  };
  const gint w = 300, h = 200;
  gfloat    *in = photo (w, h);
  guint      k;

  printf ("-- identity\n");
  in[(100 * w + 150) * 4] = NAN;
  in[(10 * w + 10) * 4 + 3] = 0.3f;
  for (k = 0; k < G_N_ELEMENTS (cases); k++)
    {
      gfloat *out = run (cases[k], in, w, h);
      gchar  *name = g_strdup_printf ("identity_%u", k + 1);

      report (name, memcmp (in, out, sizeof (gfloat) * w * h * 4) == 0,
              "%s: bit exact", k == 0 ? "defaults" : k == 1 ? "every point off, adjustments set"
                                                          : "every point on, all adjustments 0");
      g_free (name);
      g_free (out);
    }
  /* a point that is on but far away changes nothing either */
  {
    guint8       *raw = g_malloc ((gsize) w * h * 4), *got;

    babl_process (babl_fish (babl_format (LIN), babl_format ("R'G'B'A u8")), in, raw, w * h);
    got = run_full ("p1-brightness=50 p1-x=0 p1-y=0 p1-size=0.5", babl_format ("R'G'B'A u8"), raw,
                    w, h, babl_format ("R'G'B'A u8"), NULL, 0, 0);
    /* only the pixels within 1.5 pixels of the corner may change */
    {
      gint x, y, bad = 0;

      for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
          if ((x > 2 || y > 2) && memcmp (raw + ((gsize) y * w + x) * 4, got + ((gsize) y * w + x) * 4, 4))
            bad++;
      report ("outside_every_point_unchanged_8_bit", bad == 0,
              "a tiny point in the corner, 8 bit: %d pixels further away changed", bad);
    }
    g_free (raw);
    g_free (got);
  }
  g_free (in);
}

/* the flower among the leaves: mean change in L* and C* of the red and
 * the green pixels within half the point's size */
static void
flower_change (const gfloat *in,
               const gfloat *out,
               const guint8 *is_red,
               gint          w,
               gint          h,
               gdouble       cx,
               gdouble       cy,
               gdouble       reach,
               gdouble      *red_dE,
               gdouble      *green_dE,
               gint         *n_red,
               gint         *n_green)
{
  gfloat *la = to_lab (in, (gsize) w * h), *lb = to_lab (out, (gsize) w * h);
  gdouble sr = 0, sg = 0;
  gint    nr = 0, ng = 0, x, y;

  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gdouble dx = x + 0.5 - cx, dy = y + 0.5 - cy;
        gsize   i  = (gsize) y * w + x;
        gdouble e;

        if (dx * dx + dy * dy > reach * reach)
          continue;
        e = sqrt ((la[i * 3] - lb[i * 3]) * (la[i * 3] - lb[i * 3]) +
                  (la[i * 3 + 1] - lb[i * 3 + 1]) * (la[i * 3 + 1] - lb[i * 3 + 1]) +
                  (la[i * 3 + 2] - lb[i * 3 + 2]) * (la[i * 3 + 2] - lb[i * 3 + 2]));
        if (is_red[i])
          sr += e, nr++;
        else
          sg += e, ng++;
      }
  *red_dE   = nr ? sr / nr : 0;
  *green_dE = ng ? sg / ng : 0;
  *n_red    = nr;
  *n_green  = ng;
  g_free (la);
  g_free (lb);
}

static void
check_selectivity (void)
{
  const gint    w = 600, h = 400;
  guint8       *is_red;
  gfloat       *in = flower_scene (w, h, 110, &is_red);
  /* the point on a petal (between two leaf stripes) near the center;
   * its size 150 pixels */
  const gdouble px = 0.475, py = 0.5, size = 25.0;
  const gdouble cx = px * w, cy = py * h, R = size / 100 * w;
  static const gdouble sels[] = { 0, 25, 50, 75, 100 };
  gdouble       ratio50 = 0, ratio0 = 0;
  guint         k;

  printf ("-- selectivity: a red flower among green leaves\n");
  /* the pixel under the point must be red */
  report ("selectivity_point_on_the_flower", is_red[(gsize) (gint) cy * w + (gint) cx] == 1,
          "the point at %.0f, %.0f", cx, cy);
  for (k = 0; k < G_N_ELEMENTS (sels); k++)
    {
      gchar  *props = fmt_props ("p1-x=%g p1-y=%g p1-size=%g p1-selectivity=%g "
                                 "p1-brightness=60 p1-saturation=-60", px, py, size, sels[k]);
      gfloat *out   = run (props, in, w, h);
      gdouble r, g;
      gint    nr, ng;

      flower_change (in, out, is_red, w, h, cx, cy, 0.5 * R, &r, &g, &nr, &ng);
      note ("selectivity", "selectivity %3.0f: within half the size, mean change dE %.2f of the "
            "flower (%d pixels), %.2f of the leaves (%d pixels): ratio %.1f",
            sels[k], r, nr, g, ng, g > 0 ? r / g : INFINITY);
      if (sels[k] == 50)
        ratio50 = g > 0 ? r / g : INFINITY;
      if (sels[k] == 0)
        ratio0 = g > 0 ? r / g : INFINITY;
      if (sels[k] == 50)
        {
          report ("selectivity_50_changes_the_flower", r > 10.0,
                  "mean dE of the flower within half the size %.1f (> 10)", r);
          report ("selectivity_50_keeps_the_leaves", g < 0.5,
                  "mean dE of the leaves at the same distances %.3f (< 0.5)", g);
        }
      g_free (out);
      g_free (props);
    }
  report ("selectivity_0_acts_on_all", ratio0 < 1.6 && ratio0 > 0.6,
          "with selectivity 0 the flower and the leaves change alike: ratio %.2f", ratio0);
  report ("selectivity_50_ratio", ratio50 > 50,
          "flower change over leaf change at selectivity 50: %.0f (> 50)", ratio50);
  g_free (in);
  g_free (is_red);
}

/* the weight against distance, on a flat gray image */
static void
check_falloff (void)
{
  const gint w = 401, h = 401;
  gfloat    *in = flat (w, h, 0.5);
  /* point at the pixel 200, 200 (center 200.5): x = 200.5 / 401 */
  const gdouble R = 150.0, size = R / 401 * 100;
  gchar     *mp = fmt_props ("p1-x=0.5 p1-y=0.5 p1-size=%.17g p1-selectivity=0 view=mask", size);
  gchar     *rp = fmt_props ("p1-x=0.5 p1-y=0.5 p1-size=%.17g p1-brightness=50", size);
  gfloat    *mask = run (mp, in, w, h), *res = run (rp, in, w, h);
  gdouble    worst = 0, prev = INFINITY, at[5] = { 0, };
  gboolean   mono = TRUE, zero_beyond = TRUE;
  gint       x;

  printf ("-- falloff with distance\n");
  for (x = 200; x < w; x++)
    {
      const gfloat *m = mask + ((gsize) 200 * w + x) * 4;
      const gfloat *r = res + ((gsize) 200 * w + x) * 4;
      gdouble d = x + 0.5 - 200.5, t = d / R;
      gdouble want = t < 1 ? (1 - t * t) * (1 - t * t) : 0;
      gdouble got  = Y_to_L (m[0]);
      gdouble dL   = (Y_to_L (lum (r)) - Y_to_L (lum (in + ((gsize) 200 * w + x) * 4))) * 100;

      worst = MAX (worst, fabs (got - want));
      if (dL > prev + 1e-4)
        mono = FALSE;
      prev = dL;
      if (d >= R + 1e-3 && fabs (dL) > 0)
        zero_beyond = FALSE;
      if (x - 200 == 0) at[0] = dL;
      if (x - 200 == 38) at[1] = dL;
      if (x - 200 == 75) at[2] = dL;
      if (x - 200 == 113) at[3] = dL;
      if (x - 200 == 150) at[4] = dL;
    }
  report ("falloff_mask_is_the_formula", worst < 2e-4,
          "mask along a radius against (1 - (d/R)^2)^2: max difference %.2g", worst);
  report ("falloff_monotonic_and_zero_beyond", mono && zero_beyond && at[0] > 5,
          "brightness 50 at L* 50: change %.2f, %.2f, %.2f, %.2f, %.2f L* at 0, 1/4, 1/2, 3/4, 1 "
          "of the size; decreasing %s, 0 beyond the size %s",
          at[0], at[1], at[2], at[3], at[4], mono ? "yes" : "NO", zero_beyond ? "yes" : "NO");
  g_free (mp);
  g_free (rp);
  g_free (mask);
  g_free (res);
  g_free (in);
}

/* the largest step between neighbouring pixels, in L*, and the range */
static void
steps (const gfloat *img,
       gint          w,
       gint          h,
       gdouble      *max_step,
       gdouble      *lo,
       gdouble      *hi)
{
  gint x, y;

  *max_step = 0;
  *lo = G_MAXDOUBLE;
  *hi = -G_MAXDOUBLE;
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
        gdouble L = Y_to_L (lum (img + ((gsize) y * w + x) * 4)) * 100;

        *lo = MIN (*lo, L);
        *hi = MAX (*hi, L);
        if (x + 1 < w)
          *max_step = MAX (*max_step, fabs (L - Y_to_L (lum (img + ((gsize) y * w + x + 1) * 4)) * 100));
        if (y + 1 < h)
          *max_step = MAX (*max_step, fabs (L - Y_to_L (lum (img + ((gsize) (y + 1) * w + x) * 4)) * 100));
      }
}

static void
check_blend (void)
{
  const gint w = 600, h = 300;
  gfloat    *in = flat (w, h, 0.5);
  gfloat    *one, *two, *same, *prot;
  gdouble    s1, lo1, hi1, s2, lo2, hi2, s3, lo3, hi3;
  gdouble    mid_one, mid_prot;

  printf ("-- two points\n");
  /* two points 180 pixels apart, reach 150 each, opposite brightness */
  one = run ("p1-x=0.35 p1-y=0.5 p1-size=25 p1-brightness=70", in, w, h);
  two = run ("p1-x=0.35 p1-y=0.5 p1-size=25 p1-brightness=70 "
             "p2-enabled=true p2-x=0.65 p2-y=0.5 p2-size=25 p2-brightness=-70", in, w, h);
  steps (one, w, h, &s1, &lo1, &hi1);
  steps (two, w, h, &s2, &lo2, &hi2);
  report ("two_points_blend_without_seams", s2 < 0.6 && hi2 - lo2 > 15,
          "brightness +70 and -70, overlapping: largest step between neighbouring pixels "
          "%.3f L* (one point alone %.3f) over a range of %.1f L*", s2, s1, hi2 - lo2);

  /* the same settings twice: never more than one point */
  same = run ("p1-x=0.45 p1-y=0.5 p1-size=25 p1-brightness=70 "
              "p2-enabled=true p2-x=0.55 p2-y=0.5 p2-size=25 p2-brightness=70", in, w, h);
  steps (same, w, h, &s3, &lo3, &hi3);
  report ("two_same_points_not_more_than_one", hi3 <= hi1 + 1e-3 && s3 < 0.6,
          "brightness +70 twice, 60 pixels apart: brightest %.3f L* (one point %.3f), "
          "largest step %.3f L*", hi3, hi1, s3);

  /* a neutral point protects what it is on */
  prot = run ("p1-x=0.45 p1-y=0.5 p1-size=25 p1-brightness=70 "
              "p2-enabled=true p2-x=0.55 p2-y=0.5 p2-size=10", in, w, h);
  mid_one  = Y_to_L (lum (one + ((gsize) 150 * w + 330) * 4)) * 100;
  mid_prot = Y_to_L (lum (prot + ((gsize) 150 * w + 330) * 4)) * 100;
  {
    gfloat *p1 = run ("p1-x=0.45 p1-y=0.5 p1-size=25 p1-brightness=70", in, w, h);
    gdouble a  = Y_to_L (lum (p1 + ((gsize) 150 * w + 330) * 4)) * 100;

    report ("neutral_point_protects", mid_prot - 50 < 0.7 * (a - 50),
            "at the center of a neutral point 60 pixels from a brightness +70 point: "
            "change %.2f L* instead of %.2f", mid_prot - 50, a - 50);
    g_free (p1);
  }
  (void) mid_one;
  g_free (one);
  g_free (two);
  g_free (same);
  g_free (prot);
  g_free (in);
}

/* each adjustment at the center of a point, selectivity 0 */
static void
check_adjustments (void)
{
  const gint w = 200, h = 200;
  gfloat    *in = g_new (gfloat, (gsize) w * h * 4);
  gint       x, y;

  printf ("-- adjustments\n");
  /* a colour ramp: hue across, lightness down, with a fine texture */
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      set_lab (in + ((gsize) y * w + x) * 4, 20 + 60.0 * y / h + 4 * sin (x * 1.3) * sin (y * 1.1),
               40 * cos (x * 0.03), 40 * sin (x * 0.03));
  {
    /* measured in the middle 60 x 60 */
    static const struct { const gchar *prop; const gchar *what; } adj[] = {
      { "p1-brightness=60",  "brightness" }, { "p1-brightness=-60", "brightness" },
      { "p1-contrast=80",    "contrast" },   { "p1-contrast=-80",   "contrast" },
      { "p1-saturation=60",  "saturation" }, { "p1-saturation=-100", "saturation" },
      { "p1-warmth=70",      "warmth" },     { "p1-warmth=-70",     "warmth" },
      { "p1-structure=80",   "structure" },  { "p1-structure=-80",  "structure" },
    };
    gfloat  *lin = to_lab (in, (gsize) w * h);
    guint    k;

    for (k = 0; k < G_N_ELEMENTS (adj); k++)
      {
        gchar   *props = fmt_props ("p1-selectivity=0 p1-size=150 %s", adj[k].prop);
        gfloat  *out   = run (props, in, w, h);
        gfloat  *lout  = to_lab (out, (gsize) w * h);
        gdouble  mL0 = 0, mL1 = 0, sL0 = 0, sL1 = 0, C0 = 0, C1 = 0, b0 = 0, b1 = 0;
        gdouble  t0 = 0, t1 = 0, Yd = 0;
        gint     n = 0;
        gboolean ok;
        gchar   *name;

        for (y = 70; y < 130; y++)
          for (x = 70; x < 130; x++)
            {
              gsize i = (gsize) y * w + x;
              gdouble L0 = lin[i * 3], L1 = lout[i * 3];

              mL0 += L0; mL1 += L1;
              sL0 += L0 * L0; sL1 += L1 * L1;
              C0 += hypot (lin[i * 3 + 1], lin[i * 3 + 2]);
              C1 += hypot (lout[i * 3 + 1], lout[i * 3 + 2]);
              b0 += lin[i * 3 + 2]; b1 += lout[i * 3 + 2];
              /* texture: the difference to the right neighbour */
              t0 += fabs (L0 - lin[(i + 1) * 3]);
              t1 += fabs (L1 - lout[(i + 1) * 3]);
              Yd = MAX (Yd, fabs (lum (in + i * 4) - lum (out + i * 4)) / MAX (1e-6, lum (in + i * 4)));
              n++;
            }
        mL0 /= n; mL1 /= n;
        sL0 = sqrt (sL0 / n - mL0 * mL0);
        sL1 = sqrt (sL1 / n - mL1 * mL1);
        C0 /= n; C1 /= n; b0 /= n; b1 /= n; t0 /= n; t1 /= n;
        if (! strcmp (adj[k].what, "brightness"))
          ok = strstr (adj[k].prop, "=-") ? mL1 < mL0 - 5 : mL1 > mL0 + 5;
        else if (! strcmp (adj[k].what, "contrast"))
          ok = strstr (adj[k].prop, "=-") ? sL1 < 0.8 * sL0 : sL1 > 1.15 * sL0;
        else if (! strcmp (adj[k].what, "saturation"))
          ok = strstr (adj[k].prop, "=-100") ? C1 < 1.0 : C1 > 1.2 * C0;
        else if (! strcmp (adj[k].what, "warmth"))
          ok = (strstr (adj[k].prop, "=-") ? b1 < b0 - 3 : b1 > b0 + 3) && Yd < 1e-4;
        else
          ok = strstr (adj[k].prop, "=-") ? t1 < 0.8 * t0 : t1 > 1.3 * t0;
        name = g_strdup_printf ("adjust_%s", adj[k].prop + 3);
        report (name, ok,
                "mean L* %.1f to %.1f, std-dev L* %.1f to %.1f, chroma %.1f to %.1f, b* %.1f to "
                "%.1f, pixel texture %.2f to %.2f, luminance change (warmth keeps it) %.1g",
                mL0, mL1, sL0, sL1, C0, C1, b0, b1, t0, t1, Yd);
        g_free (name);
        g_free (lout);
        g_free (out);
        g_free (props);
      }
    g_free (lin);
  }
  g_free (in);
}

static void
check_alpha_and_broken (void)
{
  const gint w = 200, h = 150;
  gfloat    *in = photo (w, h);
  gfloat    *out;
  gint       i, bad_alpha = 0, spread = 0;
  gboolean   broken_kept = TRUE, finite = TRUE;
  gint       broken[] = { 40 * w + 60, 75 * w + 100, 76 * w + 100, 10 * w + 190 };

  printf ("-- alpha, broken pixels, huge values\n");
  for (i = 0; i < w * h; i++)
    in[i * 4 + 3] = (i % 7) / 6.0f;
  in[broken[0] * 4 + 0] = NAN;
  in[broken[1] * 4 + 1] = INFINITY;
  /* under the point (75, 100 is 0.5, 0.5) */
  in[broken[2] * 4 + 2] = -INFINITY;
  in[broken[3] * 4 + 0] = 1e30f;
  out = run ("p1-size=80 p1-brightness=50 p1-contrast=40 p1-saturation=50 p1-warmth=30 "
             "p1-structure=70 p1-selectivity=30", in, w, h);
  for (i = 0; i < w * h; i++)
    {
      gboolean is_broken = i == broken[0] || i == broken[1] || i == broken[2];

      if (out[i * 4 + 3] != in[i * 4 + 3])
        bad_alpha++;
      if (is_broken)
        {
          if (memcmp (in + i * 4, out + i * 4, 16) != 0 && ! (isnan (in[i * 4]) && isnan (out[i * 4])))
            broken_kept = FALSE;
        }
      else if (! (isfinite (out[i * 4]) && isfinite (out[i * 4 + 1]) && isfinite (out[i * 4 + 2])))
        {
          finite = FALSE;
          spread++;
        }
    }
  report ("alpha_passed_through", bad_alpha == 0, "%d pixels with another alpha", bad_alpha);
  report ("broken_pixels_kept_and_not_spread", broken_kept && finite,
          "NaN and infinite pixels (one under the point) come out unchanged: %s; other pixels "
          "not finite: %d", broken_kept ? "yes" : "NO", spread);
  g_free (out);

  /* huge and negative values: finite results */
  {
    gfloat *hdr = photo (w, h);

    for (i = 0; i < w * h; i++)
      {
        gint c;
        for (c = 0; c < 3; c++)
          hdr[i * 4 + c] *= (i % 3 == 0) ? 40.0f : (i % 3 == 1) ? 1.0f : -0.1f;
      }
    out = run ("p1-size=80 p1-brightness=-50 p1-contrast=-60 p1-saturation=80 p1-warmth=-50 "
               "p1-structure=-60", hdr, w, h);
    finite = TRUE;
    for (i = 0; i < w * h * 4; i++)
      if (! isfinite (out[i]))
        finite = FALSE;
    report ("high_dynamic_range_and_negative_values", finite,
            "values up to 40 and below 0 give finite results: %s", finite ? "yes" : "NO");
    g_free (out);
    g_free (hdr);
  }
  g_free (in);
}

/* tiles, threads, regions ----------------------------------------------------- */

static void
check_invariance (void)
{
  const gint w = 700, h = 460;
  gsize      n = (gsize) w * h * 4;
  gfloat    *in = photo (w, h);
  gint       threads;
  static const gint tiles[][2] = { { 32, 32 }, { 64, 128 }, { 256, 256 }, { 700, 16 } };
  static const gint nth[] = { 1, 3, 8 };
  static const gchar *const settings[] = { BUSY, BUSY " view=mask point=p2", BUSY " view=all" };
  guint      s;

  printf ("-- tiles, threads and regions\n");
  g_object_get (gegl_config (), "threads", &threads, NULL);
  for (s = 0; s < G_N_ELEMENTS (settings); s++)
    {
      gfloat *ref = run (settings[s], in, w, h), *out;
      gdouble dt = 0, dth = 0, dr = 0, dor;
      guint   k;
      gchar  *name;

      for (k = 0; k < G_N_ELEMENTS (tiles); k++)
        {
          out = run_full (settings[s], babl_format (LIN), in, w, h, babl_format (LIN), NULL,
                          tiles[k][0], tiles[k][1]);
          dt = MAX (dt, max_diff (ref, out, n));
          g_free (out);
        }
      for (k = 0; k < G_N_ELEMENTS (nth); k++)
        {
          g_object_set (gegl_config (), "threads", nth[k], NULL);
          out = run (settings[s], in, w, h);
          dth = MAX (dth, max_diff (ref, out, n));
          g_free (out);
        }
      g_object_set (gegl_config (), "threads", threads, NULL);
      /* a buffer that does not start at 0, 0 (a layer with offsets) */
      origin_x = 37;
      origin_y = -21;
      out = run_full (settings[s], babl_format (LIN), in, w, h, babl_format (LIN), NULL, 64, 64);
      origin_x = origin_y = 0;
      dor = max_diff (ref, out, n);
      g_free (out);
      /* regions asked for on their own, each in a new graph: bands at the
       * edges, one inside a point but far from where its color is read,
       * the corner; none contains the pixels under the points */
      {
        static const gint rois[][4] = { { 0, 0, 37, 460 }, { 330, 300, 64, 64 },
                                        { 650, 400, 50, 60 }, { 100, 60, 3, 2 } };
        for (k = 0; k < G_N_ELEMENTS (rois); k++)
          {
            GeglRectangle r = { rois[k][0], rois[k][1], rois[k][2], rois[k][3] };
            gint          x, y, c;

            out = run_full (settings[s], babl_format (LIN), in, w, h, babl_format (LIN), &r, 0, 0);
            for (y = 0; y < r.height; y++)
              for (x = 0; x < r.width; x++)
                for (c = 0; c < 4; c++)
                  dr = MAX (dr, fabs (out[((gsize) y * r.width + x) * 4 + c] -
                                      ref[((gsize) (y + r.y) * w + x + r.x) * 4 + c]));
            g_free (out);
          }
      }
      name = g_strdup_printf ("same_for_any_tiles_threads_regions_%s",
                              s == 0 ? "result" : s == 1 ? "mask" : "all_masks");
      report (name, dt == 0 && dor == 0 && dth == 0 && dr == 0,
              "max difference: tile sizes 32x32 to 700x16 %.2g, an origin at 37, -21 %.2g, 1, 3 "
              "and 8 threads (default %d) %.2g, regions away from the points %.2g", dt, dor,
              threads, dth, dr);
      g_free (name);
      g_free (ref);
    }
  g_free (in);
}

/* the region a change of the input invalidates */
static void
check_invalidation (void)
{
  const gint    w = 400, h = 300;
  GeglBuffer   *buf = gegl_buffer_new (GEGL_RECTANGLE (0, 0, w, h), babl_format (LIN));
  GeglNode     *g   = gegl_node_new ();
  GeglNode     *src = gegl_node_new_child (g, "operation", "gegl:buffer-source", "buffer", buf, NULL);
  GeglNode     *op  = gegl_node_new_child (g, "operation", OP, NULL);
  GeglOperation *o;
  GeglRectangle a, b;

  printf ("-- invalidation\n");
  set_props (op, "p1-x=0.5 p1-y=0.5 p1-size=20 p1-brightness=30 p1-structure=0");
  gegl_node_link (src, op);
  gegl_node_get_bounding_box (op);
  g_object_get (op, "gegl-operation", &o, NULL);
  /* a pixel far from the point: only itself; the pixel under the point:
   * everything the point reaches (radius 80) */
  a = gegl_operation_get_invalidated_by_change (o, "input", GEGL_RECTANGLE (10, 10, 1, 1));
  b = gegl_operation_get_invalidated_by_change (o, "input", GEGL_RECTANGLE (200, 150, 1, 1));
  report ("invalidated_by_change", a.width == 1 && a.height == 1 &&
          b.x <= 200 - 80 && b.x + b.width >= 200 + 80 && b.y <= 150 - 80 && b.y + b.height >= 150 + 80,
          "a pixel away from the point %d,%d %dx%d; the pixel under it %d,%d %dx%d", a.x, a.y,
          a.width, a.height, b.x, b.y, b.width, b.height);
  g_object_unref (o);
  g_object_unref (g);
  g_object_unref (buf);
}

/* formats and spaces --------------------------------------------------------------- */

static void
check_formats (void)
{
  static const struct { const gchar *name; gdouble tol; } fmts[] =
    {
      { "R'G'B'A u8", 1.0 / 255 + 1e-6 }, { "R'G'B'A u16", 2.0 / 65535 },
      { "R'G'B'A half", 2e-3 }, { "R'G'B'A float", 1e-5 }, { "RGB float", 1e-5 },
      { "RGBA double", 1e-6 }, { "Y'A u8", 1.0 / 255 + 1e-6 }, { "Y float", 1e-5 },
      { "RaGaBaA float", 1e-4 },
    };
  const gint w = 256, h = 192;
  gint       n = w * h;
  guint      f;

  printf ("-- buffer formats\n");
  for (f = 0; f < G_N_ELEMENTS (fmts); f++)
    {
      const Babl *fmt  = babl_format (fmts[f].name);
      gint        bpp  = babl_format_get_bytes_per_pixel (fmt);
      gfloat     *base = photo (w, h);
      gpointer    raw  = g_malloc ((gsize) n * bpp);
      gfloat     *same = g_new (gfloat, (gsize) 4 * n);
      gfloat     *want, *got = g_new (gfloat, (gsize) 4 * n);
      gpointer    got_raw, tmp = g_malloc ((gsize) n * bpp);
      gdouble     worst = 0;
      gint        i;
      gchar      *name;
      gboolean    no_alpha = ! strchr (fmts[f].name, 'A');

      babl_process (babl_fish (babl_format (LIN), fmt), base, raw, n);
      babl_process (babl_fish (fmt, babl_format (LIN)), raw, same, n);
      want = run (BUSY, same, w, h);
      got_raw = run_full (BUSY, fmt, raw, w, h, fmt, NULL, 0, 0);
      babl_process (babl_fish (fmt, babl_format (LIN)), got_raw, got, n);
      babl_process (babl_fish (babl_format (LIN), fmt), want, tmp, n);
      babl_process (babl_fish (fmt, babl_format (LIN)), tmp, want, n);
      for (i = 0; i < 4 * n; i++)
        {
          if (no_alpha && i % 4 == 3)
            continue;
          worst = MAX (worst, fabs (got[i] - want[i]));
        }
      name = g_strdup_printf ("format_%s", fmts[f].name);
      report (name, worst <= fmts[f].tol + 1e-6,
              "against the same pixels in float, stored in the format: max difference %.2g", worst);
      g_free (name);
      g_free (base); g_free (raw); g_free (same); g_free (want); g_free (got);
      g_free (got_raw); g_free (tmp);
    }
}

/* the same colours in another RGB space: the masks are the same (CIE
 * Lab does not depend on the space), and so is the result of the
 * lightness adjustments */
static void
check_spaces (void)
{
  const gint  w = 256, h = 192;
  gint        n = w * h;
  gfloat     *in = photo (w, h);
  const Babl *prophoto = babl_space ("ProPhoto");
  const Babl *fmt2    = babl_format_with_space (LIN, prophoto);
  gfloat     *in2 = g_new (gfloat, (gsize) n * 4);
  static const gchar *const cases[][2] = {
    { BUSY " view=all", "masks" },
    { "p1-size=60 p1-selectivity=60 p1-brightness=50 p1-contrast=40 p1-structure=50", "lightness" },
  };
  guint       k;

  printf ("-- RGB spaces\n");
  babl_process (babl_fish (babl_format (LIN), fmt2), in, in2, n);
  for (k = 0; k < G_N_ELEMENTS (cases); k++)
    {
      gfloat *a  = run (cases[k][0], in, w, h);
      gfloat *b2 = run_full (cases[k][0], fmt2, in2, w, h, babl_format (LIN), NULL, 0, 0);
      gdouble d  = max_diff (a, b2, (gsize) n * 4);
      gchar  *name = g_strdup_printf ("same_in_prophoto_rgb_%s", cases[k][1]);

      report (name, d < 1e-4, "the same colours in ProPhoto RGB: max difference %.2g", d);
      g_free (name);
      g_free (a);
      g_free (b2);
    }
  g_free (in);
  g_free (in2);
}

/* the masks ------------------------------------------------------------------ */

static void
check_masks (void)
{
  const gint w = 300, h = 200;
  gfloat    *in = photo (w, h);
  gfloat    *all = run (BUSY " view=all", in, w, h);
  gfloat    *m[3];
  gdouble    worst_sum = 0;
  gboolean   range = TRUE;
  gint       i, k;

  printf ("-- masks\n");
  for (k = 0; k < 3; k++)
    {
      gchar *p = fmt_props (BUSY " view=mask point=p%d", k + 1);
      m[k] = run (p, in, w, h);
      g_free (p);
    }
  for (i = 0; i < w * h; i++)
    {
      gdouble s = 0, a = Y_to_L (all[i * 4]);

      for (k = 0; k < 3; k++)
        {
          gdouble v = Y_to_L (m[k][i * 4]);
          s += v;
          if (v < -1e-6 || v > 1 + 1e-6)
            range = FALSE;
        }
      if (a < -1e-6 || a > 1 + 1e-6)
        range = FALSE;
      worst_sum = MAX (worst_sum, fabs (s - a));
    }
  report ("masks_add_up", range && worst_sum < 1e-4,
          "the masks of the three points add up to the mask of all: max difference %.2g, "
          "all within 0 to 1: %s", worst_sum, range ? "yes" : "NO");
  {
    gchar  *p = fmt_props (BUSY " view=mask point=p5");
    gfloat *off = run (p, in, w, h);
    gdouble mx = 0;

    for (i = 0; i < w * h; i++)
      mx = MAX (mx, off[i * 4]);
    report ("mask_of_a_point_that_is_off", mx == 0, "black: largest value %.2g", mx);
    g_free (off);
    g_free (p);
  }
  for (k = 0; k < 3; k++)
    g_free (m[k]);
  g_free (all);
  g_free (in);
}

/* photos ------------------------------------------------------------------ */

static gfloat *
load_photo (const gchar *path,
            gint        *w,
            gint        *h)
{
  GeglNode     *g = gegl_node_new ();
  GeglNode     *l = gegl_node_new_child (g, "operation", "gegl:load", "path", path, NULL);
  GeglRectangle r = gegl_node_get_bounding_box (l);
  gfloat       *buf;

  if (r.width <= 0)
    {
      g_object_unref (g);
      return NULL;
    }
  buf = g_new (gfloat, (gsize) r.width * r.height * 4);
  gegl_node_blit (l, 1.0, &r, babl_format (LIN), buf, GEGL_AUTO_ROWSTRIDE, GEGL_BLIT_DEFAULT);
  g_object_unref (g);
  *w = r.width;
  *h = r.height;
  return buf;
}

static void
check_photos (void)
{
  /* a point on a petal of each photo; red and green told apart by hue in
   * CIE Lab (red: a* > 25 and a* > |b*| / 2; green: a* < -8) */
  static const struct { const gchar *file; gdouble x, y; } photos[] = {
    { "poppy-leaves.jpg", 0.39, 0.33 },
    { "rose-green.jpg",   0.22, 0.55 },
  };
  guint k, found = 0;

  printf ("-- photos\n");
  for (k = 0; k < G_N_ELEMENTS (photos); k++)
    {
      gchar  *path = g_build_filename (SRCDIR, "tests", "images", photos[k].file, NULL);
      gint    w, h;
      gfloat *in = g_file_test (path, G_FILE_TEST_EXISTS) ? load_photo (path, &w, &h) : NULL;

      if (in)
        {
          gdouble R  = 0.2 * MAX (w, h);
          gdouble cx = photos[k].x * w, cy = photos[k].y * h;
          gchar  *props = fmt_props ("p1-x=%g p1-y=%g p1-size=20 p1-brightness=60 p1-saturation=-60",
                                     photos[k].x, photos[k].y);
          gfloat *out = run (props, in, w, h);
          gfloat *lab = to_lab (in, (gsize) w * h);
          guint8 *is_red = g_new0 (guint8, (gsize) w * h);
          guint8 *is_green = g_new0 (guint8, (gsize) w * h);
          gfloat *la = lab, *lb = to_lab (out, (gsize) w * h);
          gdouble sr = 0, sg = 0;
          gint    nr = 0, ng = 0, x, y;
          gchar  *name;

          for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
              {
                gsize   i  = (gsize) y * w + x;
                gdouble dx = x + 0.5 - cx, dy = y + 0.5 - cy, e;
                gdouble a  = la[i * 3 + 1], b = la[i * 3 + 2];

                if (dx * dx + dy * dy > R * R)
                  continue;
                e = sqrt ((la[i * 3] - lb[i * 3]) * (la[i * 3] - lb[i * 3]) +
                          (la[i * 3 + 1] - lb[i * 3 + 1]) * (la[i * 3 + 1] - lb[i * 3 + 1]) +
                          (la[i * 3 + 2] - lb[i * 3 + 2]) * (la[i * 3 + 2] - lb[i * 3 + 2]));
                if (a > 25 && a > fabs (b) / 2)
                  sr += e, nr++, is_red[i] = 1;
                else if (a < -8)
                  sg += e, ng++, is_green[i] = 1;
              }
          found++;
          name = g_strdup_printf ("photo_%s", photos[k].file);
          sr = nr ? sr / nr : 0;
          sg = ng ? sg / ng : 0;
          report (name, nr > 1000 && ng > 1000 && sr > 8 * sg,
                  "point on a petal, selectivity 50, brightness +60 and saturation -60: within "
                  "its reach mean dE %.1f of the red pixels (%d) and %.2f of the green ones (%d), "
                  "ratio %.0f", sr, nr, sg, ng, sg > 0 ? sr / sg : INFINITY);
          g_free (name);
          g_free (is_red);
          g_free (is_green);
          g_free (la);
          g_free (lb);
          g_free (out);
          g_free (props);
          g_free (in);
        }
      g_free (path);
    }
  if (! found)
    note ("photos", "none in tests/images (tests/images/fetch.py downloads them)");
}

int
main (int    argc,
      char **argv)
{
  gchar *dir;

  if (argc < 2)
    {
      fprintf (stderr, "usage: %s <build folder>\n", argv[0]);
      return 2;
    }
  setlocale (LC_ALL, "C");
  dir = modules_setup (argv[1]);
  if (! dir)
    return 2;
  gegl_init (&argc, &argv);
  g_log_set_default_handler (log_handler, NULL);
  rnd = g_rand_new_with_seed (20260928);

  check_registration ();
  check_identity ();
  check_selectivity ();
  check_falloff ();
  check_blend ();
  check_adjustments ();
  check_alpha_and_broken ();
  check_masks ();
  check_invalidation ();
  check_invariance ();
  check_formats ();
  check_spaces ();
  if (! g_getenv ("CP_CHECK_NO_PHOTOS"))
    check_photos ();

  report ("no_warnings_or_criticals", n_messages == 0, "%d", n_messages);

  g_rand_free (rnd);
  gegl_exit ();
  if (! g_getenv ("CP_CHECK_KEEP_MODULE"))
    modules_cleanup (dir);
  else
    g_free (dir);

  printf ("%d passed, %d failed\n", n_passed, n_failed);
  return n_failed ? 1 : 0;
}
