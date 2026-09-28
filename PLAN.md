# Plan

## Done

`control-points:adjust` (README): eight U Point style control points with
position (GIMP's coordinate picker), size, color selectivity,
brightness, contrast, saturation, warmth and structure, mask views, in
Colors > Control Points. Tested with tests/check.sh (50 checks, 48 also
under ASan and UBSan), tests/gimp-check.sh (GIMP 3.2.6 Flatpak: every
precision, grayscale, a layer with offsets, XCF, the command line),
tests/gui/shots.sh (the dialog on Broadway, both points placed with the
picker, masks, split view, OK, the XCF read back; screenshots looked at),
tests/bench.sh (24 megapixels: 0.23 to 0.56 s with 24 threads). Patents
in docs/patents.md (the U Point family is expired).

## Next

1. On-canvas points. GIMP draws handles only for filters with a
   hand-written GUI in core (`app/propgui/gimppropgui-*.c`: focus blur,
   vignette, spiral, supernova); a `gimppropgui-control-points.c` with a
   GimpToolWidget per point (drag to move, a circle for the size) would
   be the real Nik feel. That is a GIMP merge request, keyed on the
   config type name `GimpGegl-control-points-adjust-config` or on a
   future `gegl:` name.
2. Weights from a smoothed image. Binder et al. (2013) note that noise
   in the image makes the weights noisy and use a denoised copy for
   them; here the weights come from the pixels themselves (the color
   under a point is already a 5 x 5 mean). A small blur of the Lab
   values used for the color term (with a margin in
   get_required_for_output, like structure) would make the mask of a
   noisy photo smoother.
3. A texture term. The patents and photoshop-gaps.md mention texture
   next to color; a local variance difference in the weight would tell a
   smooth petal from textured leaves of the same color.
4. More adjustments per point, as Viveza's expanded point has (hue,
   shadows, warmth of the shadows), if people ask; each is one more
   property per point (83 now).
5. Speed. Eight points of size 60 take 0.42 s on 24 megapixels with 24
   threads, 3.3 s with one; the Lab conversion per pixel (three cube
   roots) is most of it. A fast cube root, or the weights on a half size
   copy for the preview, would help on small machines.
6. Offer it to GEGL (operations/common-gpl3+ as `gegl:control-points`)
   without `gimp:menu-path`, with reference hashes; GIMP issue for the
   menu place.

## Found along the way (to report upstream)

- GIMP 3.2.6 on Broadway: a filter dialog that was dragged by its title
  bar in its lifetime gets wrong pointer positions for its canvas
  picker (the status bar shows -1200, -554), and the canvas press ends
  the filter tool (committed) and hands the press to the paintbrush,
  with GDK criticals about the input device (gdk_device_get_axis with no
  device, gimp_device_info_get_device). GIMP's own Color to Alpha picker
  does the same, so it is not this filter. A dialog that opens where an
  earlier one was dragged to picks fine; tests/gui/shots.sh relies on
  that. Not checked on X11 or Wayland (no Xvfb here).
- GIMP 3.2.6 `gimppropgui.c` limits a `pixel-coordinate` slider to
  `area.x` to `area.x + area.width` (the selection's bounds in layer
  coordinates), while `gimp_operation_tool_color_picked` stores picked
  coordinates relative to the area's origin (minus `area.x`), which is
  also what the filter sees. With "use the selection as input" and a
  selection that does not start at the layer's corner the slider range
  and the picked values disagree. This filter uses relative coordinates,
  where both are relative to the area.
- The GIMP filter dialog subtitle reads "Background-2" for a layer named
  "Background" (the drawable's name with the image's ID, it seems);
  not looked into.
