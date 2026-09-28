#!/bin/sh
# The filter's dialog in GIMP on a Broadway display, driven with a
# headless Chrome, for the screenshots in docs/: the Colors menu with
# Control Points; point 1 placed on a poppy with GIMP's "Pick
# coordinates from the image" button, brightness and saturation
# changed; its mask; point 2 placed on the leaves with the picker, with
# warmth; the mask of both; the result in split view; then OK, the
# image saved and the XCF checked in headless GIMP: the layer keeps the
# filter, live, with the picked positions and the settings. The photo is
# the CC0 tests/images/poppy-leaves.jpg (tests/images/fetch.py). Run
# tests/gimp-check.sh first (it puts the module in place). Lists the
# user's folders before and after and fails if anything changed.
#
#   tests/gui/shots.sh
#
# On Broadway, a press on the canvas does not reach the picker of a
# dialog that was dragged in its lifetime (GIMP gets the pointer
# position wrong, the status bar shows -1200, -554, and the filter tool
# ends); a dialog that opens where an earlier one was dragged to works.
# So the script opens the dialog once, drags it to the top right corner
# and cancels it; the dialog of the shots then opens there.
#
# Prints PASS or FAIL per step and exits non-zero if one failed.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
docs=$top/docs
images=$top/tests/images
mkdir -p "$docs"
status=0
pass () { echo "PASS  gui_$1"; }
fail () { echo "FAIL  gui_$1"; status=1; }

# the dialog: the last canvas of about its size (467 x 608 with the
# shadow); Broadway draws each window as a canvas with its shadow (26
# pixels here), the title bar's middle 36 pixels below the canvas's top
dialog_rect () {
    $cdp "$view" "eval:(() => { const c = [...document.querySelectorAll('canvas')]
      .map(e => e.getBoundingClientRect())
      .filter(r => r.width > 380 && r.width < 560 && r.height > 450 && r.height < 700).pop();
      return c ? [c.left, c.top, c.width, c.height].map(Math.round).join(',') : '' })()" \
      2>/dev/null | tail -1
}

# moves the dialog to the top right corner of the page, then measures
# where it is; sets vx, vy (its visible top left corner) and vw (its
# visible width)
move_dialog () {
    r=$(dialog_rect)
    [ -n "$r" ] || return 1
    l=${r%%,*}; r=${r#*,}; t=${r%%,*}; r=${r#*,}; w=${r%%,*}
    vw=$((w - 52))
    tx=$((1278 - vw)); ty=120
    gx=$((l + w / 2)); gy=$((t + 36))
    $cdp "$view" down:$gx,$gy move:$(( (gx + tx + vw / 2) / 2 )),$(( (gy + ty + 10) / 2 )) \
      move:$((tx + vw / 2)),$((ty + 10)) up:$((tx + vw / 2 + 1)),$((ty + 10)) wait:1200 >/dev/null
    r=$(dialog_rect)
    [ -n "$r" ] || return 1
    l=${r%%,*}; r=${r#*,}; t=${r%%,*}
    vx=$((l + 26)); vy=$((t + 26))
}
# a point in the dialog, from its visible top left corner
p () { echo "$((vx + $1)),$((vy + $2))"; }
# the x of value v on a slider from lo to hi (the bar starts 8 pixels in
# and is 360 wide in a dialog 415 wide)
slider () { echo $(( 8 + ($1 - $2) * (vw - 55) / ($3 - $2) )); }

# rows of the dialog, from its visible top (the points' rows only show
# for the chosen point)
POINT=112; USE=139; PICK_X=370; PICK_Y=178
BRIGHTNESS=273; SATURATION=330; WARMTH=358; SHOW=416
SPLIT_X=315; SPLIT_Y=497; OK_X=365; OK_Y=532
COLORS=321,49
ITEM=374,728
# the image, 1200 x 554 at 66.7 %: its top left corner on the page, and
# the page position of a point of it (relative coordinates in 1/1000)
IX=113; IY=277
at () { echo "$((IX + $1 * 800 / 1000)),$((IY + $2 * 369 / 1000))"; }
PETAL_X=390; PETAL_Y=330
LEAVES_X=250; LEAVES_Y=820

# Colors > Control Points; the dialog opens where the last one was
first_filter () {
    $cdp "$view" click:$COLORS wait:1500 click:$ITEM wait:5000 >/dev/null
}
open_filter () {
    first_filter
    r=$(dialog_rect)
    [ -n "$r" ] || return 1
    l=${r%%,*}; r=${r#*,}; t=${r%%,*}; r=${r#*,}; w=${r%%,*}
    vx=$((l + 26)); vy=$((t + 26)); vw=$((w - 52))
}

# press on the canvas at page position $1, with the picker of the
# dialog on
pick () {
    $cdp "$view" click:"$(p $PICK_X $PICK_Y)" wait:1000 move:"$(( ${1%,*} - 30 )),$(( ${1#*,} - 20 ))" \
      wait:400 move:"$1" wait:600 down:"$1" wait:200 up:"$1" wait:4000 >/dev/null
}
# a combo box: opens it and clicks the item that is n rows below (or
# above, negative) the one shown
combo () {
    $cdp "$view" click:"$(p 374 "$1")" wait:1500 click:"$(p 40 $(( $1 + 29 * $2 )))" wait:4000 \
      move:1000,760 wait:600 >/dev/null
}

snapshot_begin gui
PHOTO=$images/poppy-leaves.jpg
if [ ! -f "$PHOTO" ]; then
    fail "no $PHOTO (tests/images/fetch.py)"
    snapshot_end gui
    exit 1
fi
CP_GUI_PHOTO=$PHOTO
CP_GUI_NAME=points
export CP_GUI_PHOTO CP_GUI_NAME
rm -f "$gui_out/gui-points.xcf"
tries=0
until start_gimp points; do
    stop_all
    tries=$((tries + 1))
    [ $tries -lt 3 ] || { fail "GIMP did not start (log: $gui_out/gimp-points.log)"; exit 1; }
done

# the menu
$cdp "$view" click:$COLORS wait:1500 shot:"$docs/colors-menu.png" key:Escape wait:800 >/dev/null &&
  pass "menu: Colors > Control Points..."

# the dialog once, dragged to the top right corner, and cancelled
first_filter
if move_dialog; then
    $cdp "$view" click:"$(p 272 $OK_Y)" wait:2000 >/dev/null
    pass "first dialog: dragged to the top right corner and cancelled"
else
    fail "first dialog: did not open"
fi

if open_filter; then
    pass "dialog: opened where the first one was dragged to"
    $cdp "$view" wait:500 shot:"$docs/dialog.png" >/dev/null
    # point 1 on a petal, with the picker
    pick "$(at $PETAL_X $PETAL_Y)"
    $cdp "$view" click:"$(p "$(slider 40 -100 100)" $BRIGHTNESS)" wait:1500 \
      click:"$(p "$(slider -60 -100 100)" $SATURATION)" wait:5000 move:1000,760 wait:600 \
      shot:"$docs/point-on-poppy.png" >/dev/null &&
      pass "point 1: picked on a poppy, brightness 40, saturation -60"
    combo $SHOW 1
    $cdp "$view" shot:"$docs/mask-of-point.png" >/dev/null && pass "point 1: its mask"
    # point 2 on the leaves
    combo $POINT 1
    $cdp "$view" shot:"$gui_out/point-2-off.png" click:"$(p 20 $USE)" wait:2000 >/dev/null
    pick "$(at $LEAVES_X $LEAVES_Y)"
    $cdp "$view" click:"$(p "$(slider 70 -100 100)" $WARMTH)" wait:4000 >/dev/null
    combo $SHOW 1
    $cdp "$view" shot:"$docs/mask-of-all.png" >/dev/null &&
      pass "point 2: picked on the leaves, warmth 70; the mask of both points"
    combo $SHOW -2
    $cdp "$view" click:"$(p $SPLIT_X $SPLIT_Y)" wait:6000 move:1000,760 wait:600 \
      shot:"$docs/split-view.png" >/dev/null && pass "result: split view"
    $cdp "$view" click:"$(p $SPLIT_X $SPLIT_Y)" wait:3000 click:"$(p $OK_X $OK_Y)" wait:5000 \
      shot:"$gui_out/after-ok.png" >/dev/null
    touch "$gui_out/save"
    if wait_for "$gui_out/saved" 60; then
        pass "saved with the filter"
    else
        fail "not saved (log: $gui_out/gimp-points.log)"
    fi
else
    fail "dialog: did not open"
fi
stop_all

# the filter in the saved XCF, in headless GIMP: the picked positions
# are those of the image pixels under the presses
xcf=$gui_out/gui-points.xcf
if [ -f "$xcf" ]; then
    GIMP3_DIRECTORY="$out/gimp-profile"
    GEGL_PATH="$out/gimp-check-modules:/app/lib/gegl-0.4"
    export GIMP3_DIRECTORY GEGL_PATH
    pass_env="GIMP3_DIRECTORY GEGL_PATH"
    run_prefix="--timeout=600" in_gimp gimp-console-3.2 --no-interface --no-data --no-fonts \
      --batch-interpreter python-fu-eval -b "
import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio
img = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path('$xcf'))
for l in img.get_layers():
    for f in l.get_filters():
        c = f.get_config()
        print('FILTER', l.get_name(), f.get_operation_name(), ' '.join('%s=%.3f' % (k, c.get_property(k))
              for k in ('p1-x', 'p1-y', 'p1-brightness', 'p1-saturation', 'p2-x', 'p2-y', 'p2-warmth')),
              'p2-enabled=%s' % c.get_property('p2-enabled'))
" --quit >"$gui_out/xcf-check.log" 2>&1
    grep FILTER "$gui_out/xcf-check.log"
    if python3 - "$gui_out/xcf-check.log" $PETAL_X $PETAL_Y $LEAVES_X $LEAVES_Y <<'EOF'
import sys
lines = [l.split() for l in open(sys.argv[1]) if l.startswith('FILTER')]
px, py, lx, ly = [int(v) / 1000 for v in sys.argv[2:6]]
ok = len(lines) == 1 and lines[0][2] == 'control-points:adjust'
if ok:
    v = dict(kv.split('=') for kv in lines[0][3:])
    f = {k: float(x) for k, x in v.items() if k != 'p2-enabled'}
    # a page pixel is 1.5 image pixels: within 0.005 of the target
    ok = (abs(f['p1-x'] - px) < 0.005 and abs(f['p1-y'] - py) < 0.005 and
          abs(f['p2-x'] - lx) < 0.005 and abs(f['p2-y'] - ly) < 0.005 and
          abs(f['p1-brightness'] - 40) < 3 and abs(f['p1-saturation'] + 60) < 3 and
          abs(f['p2-warmth'] - 70) < 3 and v['p2-enabled'] == 'True')
sys.exit(0 if ok else 1)
EOF
    then
        pass "xcf: one live control-points:adjust filter, the picked positions and the settings kept"
    else
        fail "xcf: $(grep FILTER "$gui_out/xcf-check.log")"
    fi
fi

snapshot_end gui || status=1
[ $status = 0 ] && echo "gui: all passed" || echo "gui: FAILED (screenshots in docs, logs in $gui_out)"
exit $status
