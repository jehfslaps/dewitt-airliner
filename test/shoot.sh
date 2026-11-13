#!/bin/bash
# usage: shoot.sh <script-for-yhost> <seconds> <actions...>   (coords as x,y)
export DISPLAY=:99 HOME=/tmp/yhome LIBGL_ALWAYS_SOFTWARE=1
mkdir -p /tmp/.X11-unix && chmod 1777 /tmp/.X11-unix
pkill Xvfb 2>/dev/null; sleep 0.3
Xvfb :99 -screen 0 1400x1000x24 > /tmp/xvfb.log 2>&1 &
sleep 1.5
cd /home/claude/howard/test
./htest ../plugin/bin/Howard.clap gui ${W:-1080} ${H:-640} "$1" "$2" > /tmp/gui.log 2>&1 &
sleep 4
shift 2
for a in "$@"; do
  v="${a#*:}"; xy="${v/,/ }"
  case "$a" in
    shot:*) xwd -root -silent | convert xwd:- -crop ${W:-1080}x${H:-640}+0+0 "$v" ;;
    rclick:*) xdotool mousemove $xy click 3 ;;
    click:*) xdotool mousemove $xy click 1 ;;
    down:*) xdotool mousemove $xy mousedown 1 ;;
    up) xdotool mouseup 1 ;;
    move:*) xdotool mousemove $xy ;;
    sleep:*) sleep "$v" ;;
    type:*) xdotool type --delay 60 "$v" ;;
    key:*) xdotool key "$v" ;;
    note:*) echo "note $v 0" > /tmp/ymidi; sleep 0.5 ;;
    note2:*) echo "note $v 1" > /tmp/ymidi; sleep 0.5 ;;
    cc:*) echo "cc ${v/,/ } 0" > /tmp/ymidi; sleep 0.3 ;;
    noteon:*) echo "on ${v/,/ } 0" > /tmp/ymidi; sleep 0.3 ;;
    noteoff:*) echo "off $v 0" > /tmp/ymidi; sleep 0.3 ;;
    at:*) echo "at ${v/,/ } 0" > /tmp/ymidi; sleep 0.3 ;;
  esac
  sleep 0.6
done
pkill yhost; pkill Xvfb
