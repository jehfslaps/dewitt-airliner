#!/bin/bash
# The Airliner Windows download: Airliner_v<version>_Windows.zip
#   tools/package-windows-airliner.sh <folder with Airliner.clap and the Airliner.vst3 bundle> <output dir> [label]
# Inside: Install Airliner.exe, README.txt, licenses/, manual install/ (Airliner.vst3 + Airliner.clap). label: text after the version ("" for a release, e.g. "beta").
set -e
SRC="$(cd "$1" && pwd)"; mkdir -p "$2"; OUT="$(cd "$2" && pwd)"; LABEL="${3-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION=$(sed -n 's/^VERSION=\([0-9.]*\);.*/\1/p' "$ROOT/installer/make-installer-airliner.sh")
NAME="Airliner_v$VERSION${LABEL:+-$LABEL}_Windows"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
bash "$ROOT/installer/make-installer-airliner.sh" "$SRC" "$W/exe" "$LABEL" > /dev/null
D="$W/$NAME"; mkdir -p "$D/manual install" "$D/licenses"
mv "$W"/exe/*.exe "$D/Install Airliner.exe"
sed 's/$/\r/' "$ROOT/installer/README-Windows-Airliner.txt" > "$D/README.txt"
sed 's/$/\r/' "$ROOT/dpf/LICENSE" > "$D/licenses/DPF-ISC.txt"
sed 's/$/\r/' "$ROOT/dpf/dgl/src/nanovg/LICENSE.txt" > "$D/licenses/NanoVG-zlib.txt"
sed 's/$/\r/' "$ROOT/ft-src/docs/FTL.TXT" > "$D/licenses/FreeType-FTL.txt"
cp -R "$SRC/Airliner.vst3" "$SRC/Airliner.clap" "$D/manual install/"
rm -f "$OUT/$NAME.zip"
(cd "$W" && zip -qr -X "$OUT/$NAME.zip" "$NAME")
echo "made $OUT/$NAME.zip"
