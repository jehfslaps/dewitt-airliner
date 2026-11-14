#!/bin/bash
# screenshots for the manual, at 2x: shots.sh <Dewitt.clap>  (run from the repo root; needs Xvfb, xdotool, ImageMagick)
PLUG="$1"; OUT=docs/manual/img; S=2
export DISPLAY=:96 LIBGL_ALWAYS_SOFTWARE=1 HTEST_NATIVE=1 DPF_SCALE_FACTOR=$S
pkill -f "Xvfb :96"; sleep 0.3; Xvfb :96 -screen 0 2600x1500x24 >/dev/null 2>&1 & XV=$!; sleep 1.5
run() {   # name, then pairs of: click x,y (1x units) | key name | wait
  local name="$1"; shift
  export HOME=/tmp/manual-home; rm -rf $HOME; mkdir -p $HOME/.config/slapworx/Dewitt
  printf "welcomed3=1\n%s" "${PREFS:-}" > $HOME/.config/slapworx/Dewitt/settings.txt
  build-tests/htest "$PLUG" gui 200 200 "" 30 > /dev/null 2>&1 & local hp=$!
  sleep 4.5
  for a in "$@"; do
    case "$a" in
      r:*) local xy=${a#r:}; xdotool mousemove $(( ${xy%,*} * S )) $(( ${xy#*,} * S )) click 3; sleep 0.9 ;;
      m:*) local xy=${a#m:}; xdotool mousemove $(( ${xy%,*} * S )) $(( ${xy#*,} * S )); sleep 0.6 ;;
      d:*) local xy=${a#d:}; xdotool mousemove $(( ${xy%,*} * S )) $(( ${xy#*,} * S )) mousedown 1; sleep 0.5 ;;
      *)   xdotool mousemove $(( ${a%,*} * S )) $(( ${a#*,} * S )) click 1; sleep 0.9 ;;
    esac
  done
  xwd -root -silent | convert xwd:- -crop $((1080 * S))x$((640 * S))+0+0 +repage "$OUT/$name.png"
  xdotool mouseup 1 2>/dev/null; kill $hp; wait $hp 2>/dev/null
}
run main
run typemenu 124,171
run sets 418,34
run settings 307,80

run padright r:94,196
run move 203,502 m:700,600
run plusmenu 105,115 94,196 m:700,600
run empty 105,115 60,232 m:700,600
run setup 205,80
run dark 122,80 m:700,600
run knobtype 307,80 360,152
kill $XV
ls -la $OUT
