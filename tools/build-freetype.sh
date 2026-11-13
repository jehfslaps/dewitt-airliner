#!/bin/bash
# Builds the small static FreeType Dewitt uses for text (see BUILDING.md).
# usage: tools/build-freetype.sh <output .a> [cc] [ar]      e.g. tools/build-freetype.sh freetype/libfreetype-mac.a cc ar
# Sources: ft-src/ (FreeType 2.x, FTL license, see ft-src/LICENSE.TXT); config: freetype/custom.
set -e
cd "$(dirname "$0")/.."
OUT="$1"; CC="${2:-cc}"; AR="${3:-ar}"
[ -n "$OUT" ] || { echo "usage: $0 <output .a> [cc] [ar]"; exit 1; }
OBJ="$(mktemp -d)"
SRCS="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c base/ftglyph.c base/ftmm.c
      autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c psnames/psnames.c"
for s in $SRCS; do
  "$CC" -c -O2 -fPIC -fvisibility=hidden $CFLAGS -DFT2_BUILD_LIBRARY -Ifreetype/custom -Ift-src/include \
        "ft-src/src/$s" -o "$OBJ/$(echo "$s" | tr / _).o"
done
rm -f "$OUT"
"$AR" rcs "$OUT" "$OBJ"/*.o
rm -rf "$OBJ"
echo "built $OUT"
