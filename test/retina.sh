#!/bin/bash
# Retina check without a Mac: opens a plug-in's window the way a DAW does, at DPF_SCALE_FACTOR=1 and =2, and
# checks the size (2x screen = 2x the design size, not 4x) and that a click lands where it's drawn.
# usage: test/retina.sh <plugin.clap> <design width> <design height> <click x,y at 1x> <shot prefix>
#   (needs Xvfb, xdotool, ImageMagick; build-tests/htest from test/htest.cpp)
set -e
PLUG="$1"; DW="$2"; DH="$3"; CXY="$4"; OUT="$5"
export DISPLAY=:98 HOME="${HOME_FOR_TEST:-/tmp/retina-home}" LIBGL_ALWAYS_SOFTWARE=1 HTEST_NATIVE=1
mkdir -p "$HOME" /tmp/.X11-unix
pkill -f "Xvfb :98" 2>/dev/null || true; sleep 0.3; rm -f /tmp/.X98-lock
Xvfb :98 -screen 0 3000x1800x24 > /dev/null 2>&1 &
XV=$!
sleep 1.5
FAIL=0
for sf in 1 2; do
  rm -rf "$HOME"; mkdir -p "$HOME"      # no saved window size
  DPF_SCALE_FACTOR=$sf build-tests/htest "$PLUG" gui 200 200 "" 12 > "$OUT-$sf.log" 2>&1 &
  HP=$!
  sleep 4
  size=$(grep -o "plug-in size [0-9]*x[0-9]*" "$OUT-$sf.log" | grep -o "[0-9]*x[0-9]*" || true)
  want="$((DW * sf))x$((DH * sf))"
  xwd -root -silent | convert xwd:- -crop "$((DW * sf))x$((DH * sf))+0+0" +repage "$OUT-$sf.png"
  x=$(( ${CXY%,*} * sf )); y=$(( ${CXY#*,} * sf ))
  xdotool mousemove $x $y click 1; sleep 1.2
  xwd -root -silent | convert xwd:- -crop "$((DW * sf))x$((DH * sf))+0+0" +repage "$OUT-$sf-click.png"
  diff=$(compare -metric AE "$OUT-$sf.png" "$OUT-$sf-click.png" null: 2>&1 || true)
  # the drawing must fill the window: the far corner isn't the empty X background (black)
  corner=$(convert "$OUT-$sf.png" -crop 4x4+$((DW * sf - 8))+$((DH * sf - 8)) -format "%[fx:mean]" info:)
  echo "scale $sf: window $size (want $want), drawing at the far corner $corner, click at $x,$y changed $diff pixels"
  [ "$size" = "$want" ] || { echo "FAIL: window size at scale $sf"; FAIL=1; }
  awk "BEGIN{exit !($corner > 0.02)}" || { echo "FAIL: the drawing doesn't fill the window at scale $sf"; FAIL=1; }
  awk "BEGIN{exit !($diff > 200)}" || { echo "FAIL: the click at scale $sf did nothing"; FAIL=1; }
  kill $HP 2>/dev/null || true; wait $HP 2>/dev/null || true
done
# the same click must do the same thing at both scales: the 2x result, scaled down, looks like the 1x result
convert "$OUT-2-click.png" -resize "${DW}x${DH}!" "$OUT-2-click-1x.png"
same=$(compare -metric RMSE "$OUT-1-click.png" "$OUT-2-click-1x.png" null: 2>&1 | sed 's/.*(\(.*\))/\1/' || true)
echo "1x and 2x after the click: difference $same (0 = identical, 1 = opposite)"
awk "BEGIN{exit !($same < 0.08)}" || { echo "FAIL: the click did something different at 2x"; FAIL=1; }
kill $XV 2>/dev/null || true
[ $FAIL = 0 ] && echo "retina check: ok" || { echo "retina check: FAILED"; exit 1; }
