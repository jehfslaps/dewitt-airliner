#!/bin/bash
# Windows installer for Dewitt (NSIS 3).
#   installer/make-installer.sh <folder with Dewitt.clap and the Dewitt.vst3 bundle folder> <output dir> [label]
# label is the text after the version in the file name (default "beta"; "" for a final release).
# Adds the user manual, the How to Set Up MIDI pictures, README-Windows.txt and the third-party licenses.
set -e
SRC="$(cd "$1" && pwd)"; mkdir -p "$2"; OUT="$(cd "$2" && pwd)"; LABEL="${3-beta}"
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/.."
VERSION=1.2; NUMVER=1.2.0.0
[ -f "$SRC/Dewitt.clap" ] && [ -d "$SRC/Dewitt.vst3" ] || { echo "need Dewitt.clap and Dewitt.vst3 in $SRC"; exit 1; }
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
cp -R "$SRC/Dewitt.vst3" "$SRC/Dewitt.clap" "$STAGE/"
cp "$ROOT/docs/manual/Dewitt_User_Manual.pdf" "$STAGE/"
cp -R "$ROOT/docs/midi-setup/How to Set Up MIDI" "$STAGE/"
sed 's/$/\r/' "$HERE/README-Windows.txt" > "$STAGE/README.txt"
mkdir -p "$STAGE/licenses"
sed 's/$/\r/' "$ROOT/dpf/LICENSE" > "$STAGE/licenses/DPF-ISC.txt"
sed 's/$/\r/' "$ROOT/dpf/dgl/src/nanovg/LICENSE.txt" > "$STAGE/licenses/NanoVG-zlib.txt"
sed 's/$/\r/' "$ROOT/ft-src/docs/FTL.TXT" > "$STAGE/licenses/FreeType-FTL.txt"
FILE="Dewitt-$VERSION${LABEL:+-$LABEL}-Windows-Setup.exe"
(cd "$HERE" && makensis -V2 -DVERSION="$VERSION" -DNUMVER="$NUMVER" -DSTAGE="$STAGE" -DOUTFILE="$OUT/$FILE" dewitt.nsi)
echo "made $OUT/$FILE"
