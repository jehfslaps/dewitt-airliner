#!/bin/bash
# Windows installer for Airliner (NSIS 3).
#   installer/make-installer.sh <folder with Airliner.clap and the Airliner.vst3 bundle folder> <output dir> [label]
# label is the text after the version in the file name (default "beta"; "" for a final release).
# Adds README-Windows-Airliner.txt and the third-party licenses.
set -e
SRC="$(cd "$1" && pwd)"; mkdir -p "$2"; OUT="$(cd "$2" && pwd)"; LABEL="${3-beta}"
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/.."
VERSION=1.0; NUMVER=1.0.0.0
[ -f "$SRC/Airliner.clap" ] && [ -d "$SRC/Airliner.vst3" ] || { echo "need Airliner.clap and Airliner.vst3 in $SRC"; exit 1; }
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
cp -R "$SRC/Airliner.vst3" "$SRC/Airliner.clap" "$STAGE/"
sed 's/$/\r/' "$HERE/README-Windows-Airliner.txt" > "$STAGE/README.txt"
mkdir -p "$STAGE/licenses"
sed 's/$/\r/' "$ROOT/dpf/LICENSE" > "$STAGE/licenses/DPF-ISC.txt"
sed 's/$/\r/' "$ROOT/dpf/dgl/src/nanovg/LICENSE.txt" > "$STAGE/licenses/NanoVG-zlib.txt"
sed 's/$/\r/' "$ROOT/ft-src/docs/FTL.TXT" > "$STAGE/licenses/FreeType-FTL.txt"
FILE="Airliner-$VERSION${LABEL:+-$LABEL}-Windows-Setup.exe"
(cd "$HERE" && makensis -V2 -DVERSION="$VERSION" -DNUMVER="$NUMVER" -DSTAGE="$STAGE" -DOUTFILE="$OUT/$FILE" airliner.nsi)
echo "made $OUT/$FILE"
