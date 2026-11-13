#!/bin/bash
# Mac release build: Dewitt and Airliner as VST3, AU and CLAP, universal (Apple Silicon + Intel, macOS 10.15
# and newer). Builds each architecture on its own and joins them with lipo, runs
# the checks (natively, and the Intel half under Rosetta when it's there), signs, validates the Audio Units,
# makes the installers and, when the Apple credentials are set, notarizes them.
#
#   dist/release/   installers (.pkg) and bare bundles (.zip)
#   dist/logs/      build logs, check results, auval reports, notarization logs
#
# Runs on GitHub Actions (.github/workflows/mac.yml) or on any Mac with Xcode's command line tools.
# Optional, from the environment:
#   LABEL                 text after the version in file names (default "beta"; empty for a final release)
#   AIRLINER_ALL=1        also build Airliner Looper and Airliner Vinyl
#   MAC_SIGN_APP          "Developer ID Application: ..." identity (in the keychain) to sign the bundles
#   MAC_SIGN_INSTALLER    "Developer ID Installer: ..." identity to sign the installers
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD   to notarize and staple the installers (needs both identities)
# Without the identities everything is signed ad-hoc: it works, but macOS warns the first time (see the readme
# inside each installer).
set -e
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEWITT_VERSION=1.2
AIRLINER_VERSION=1.0
LABEL="${LABEL-beta}"
SUFFIX="${LABEL:+-$LABEL}"
MINOS=10.15
export MACOSX_DEPLOYMENT_TARGET=$MINOS
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
NATIVE=$(uname -m)            # arm64 on Apple Silicon runners
rm -rf dist build-tests stage && mkdir -p dist/logs dist/release build-tests stage

step() { echo; echo "=== $*"; }
die() { echo "ERROR: $*"; exit 1; }

# product list: name  folder  make-arguments
PRODUCTS=(
  "Dewitt      plugin   VARIANT="
  "Airliner    airliner KIND=both"
)
if [ "${AIRLINER_ALL:-0}" = 1 ]; then
  PRODUCTS+=("AirlinerLooper airliner KIND=looper" "AirlinerVinyl airliner KIND=vinyl")
fi

step "DPF"
tools/setup-dpf.sh

# ---------------------------------------------------------------------------------------------------------------
# 1. build every product once per architecture
for arch in arm64 x86_64; do
  A="-arch $arch -mmacosx-version-min=$MINOS"
  step "Build for $arch"
  rm -rf dpf/build* plugin/build* airliner/build* plugin/bin airliner/bin
  CFLAGS="$A" tools/build-freetype.sh freetype/libfreetype-mac.a clang ar > "dist/logs/build-freetype-$arch.log" 2>&1 \
    || { tail -20 "dist/logs/build-freetype-$arch.log"; die "FreeType ($arch)"; }
  for p in "${PRODUCTS[@]}"; do
    set -- $p; name=$1; dir=$2; shift 2
    log="dist/logs/build-$name-$arch.log"
    make -C "$dir" -j"$JOBS" "$@" CFLAGS="$A" CXXFLAGS="$A" LDFLAGS="$A" > "$log" 2>&1 \
      || { tail -40 "$log"; die "$name ($arch)"; }
    echo "built $name ($arch)"
  done
  mkdir -p "stage/$arch"
  cp -R plugin/bin "stage/$arch/plugin"
  cp -R airliner/bin "stage/$arch/airliner"
done

# ---------------------------------------------------------------------------------------------------------------
# 2. join the two architectures into universal bundles in plugin/bin and airliner/bin
step "Universal bundles"
rm -rf plugin/bin airliner/bin && mkdir -p plugin/bin airliner/bin
for p in "${PRODUCTS[@]}"; do
  set -- $p; name=$1; dir=$2
  for ext in vst3 component clap; do
    a="stage/arm64/$dir/$name.$ext"; x="stage/x86_64/$dir/$name.$ext"; u="$dir/bin/$name.$ext"
    [ -d "$a" ] && [ -d "$x" ] || die "missing $name.$ext"
    cp -R "$a" "$u"
    # every Mach-O file in the bundle (the main executable, and anything else) becomes arm64 + x86_64
    while IFS= read -r f; do
      rel="${f#$a/}"
      if file "$f" | grep -q "Mach-O"; then
        lipo -create "$f" "$x/$rel" -output "$u/$rel"
      fi
    done < <(find "$a" -type f)
    exe="$u/Contents/MacOS/$name"
    archs=$(lipo -archs "$exe")
    case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) die "$exe is only '$archs'";; esac
    # our own bundle id and version instead of DPF's defaults
    case "$name" in Dewitt*) ver=$DEWITT_VERSION;; *) ver=$AIRLINER_VERSION;; esac
    org=audio.slapworx
    plist="$u/Contents/Info.plist"
    lc=$(echo "$name" | tr '[:upper:]' '[:lower:]')
    /usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier $org.$lc.$ext" "$plist" 2>/dev/null \
      || /usr/libexec/PlistBuddy -c "Add :CFBundleIdentifier string $org.$lc.$ext" "$plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleVersion $ver" "$plist" 2>/dev/null \
      || /usr/libexec/PlistBuddy -c "Add :CFBundleVersion string $ver" "$plist"
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $ver" "$plist" 2>/dev/null \
      || /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string $ver" "$plist"
    echo "$name.$ext: $archs, $org.$lc.$ext $ver"
  done
done

# ---------------------------------------------------------------------------------------------------------------
# 3. checks (the check programs are native; the bundles are universal, so each loads its own half)
N="-arch $NATIVE -mmacosx-version-min=$MINOS"
step "Checks"
FAILED=0
CXX="clang++ -std=gnu++17 -O2"
$CXX $N -Idpf/distrho/src test/wtest.cpp -o build-tests/wtest
runcheck() {   # label exe plugin rate [arch-prefix]
  local log="dist/logs/checks-$1-$4.txt"
  $5 "build-tests/$2" "plugin/bin/$3" "$4" > "$log" 2>&1 || true
  local ok=$(grep -c '^ok' "$log" || true) fails=$(tail -1 "$log")
  echo "$1 @ $4 Hz: $ok ok, $fails"
  grep '^FAIL' "$log" || true
  case "$fails" in "0 failure(s)") ;; *) [ "$4" = 48000 ] && FAILED=1 ;; esac
}
runcheck Dewitt    wtest    Dewitt.clap    48000
runcheck Dewitt    wtest    Dewitt.clap    44100
runcheck Dewitt    wtest    Dewitt.clap    96000

$CXX $N -Iplugin test/foldtest.cpp -o build-tests/foldtest
build-tests/foldtest > dist/logs/checks-foldtest.txt 2>&1 || true
echo "Knob folding: $(grep -c '^ok' dist/logs/checks-foldtest.txt) ok, $(tail -1 dist/logs/checks-foldtest.txt)"
grep '^FAIL' dist/logs/checks-foldtest.txt && FAILED=1 || true

AFL="-std=gnu++17 -O2 -ffast-math -fno-finite-math-only -Idpf/distrho/src -Iplugin"
acheck() {   # name kind(0/1/2) [arch]
  local arch="${3:-$NATIVE}"
  local exe="build-tests/atest_$1_$arch"
  clang++ $AFL -arch "$arch" -mmacosx-version-min=$MINOS -DAIRLINER_KIND=$2 test/atest.cpp -o "$exe"
  local log="dist/logs/checks-$1-$arch.txt"
  rm -rf build-tests/acfg
  arch -"$arch" "$exe" "airliner/bin/$1.clap" "$ROOT/build-tests/acfg" > "$log" 2>&1 || true
  echo "$1 ($arch): $(grep -c '^ok' "$log" || true) ok, $(tail -1 "$log")"
  grep '^FAIL' "$log" && FAILED=1 || true
}
acheck Airliner 0
if [ "${AIRLINER_ALL:-0}" = 1 ]; then acheck AirlinerLooper 1; acheck AirlinerVinyl 2; fi

# the Intel half, under Rosetta (GitHub's Apple Silicon runners have it; skipped where it isn't installed)
if [ "$NATIVE" = arm64 ] && /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null; then
  step "Checks, Intel half (Rosetta)"
  X="-arch x86_64 -mmacosx-version-min=$MINOS"
  $CXX $X -Idpf/distrho/src test/wtest.cpp -o build-tests/wtest_x86
  runcheck Dewitt-intel wtest_x86 Dewitt.clap 48000 "arch -x86_64"
  acheck Airliner 0 x86_64
else
  echo "(no Rosetta here: the Intel half is built and joined, but not run)"
fi

# ---------------------------------------------------------------------------------------------------------------
# 4. sign and validate
step "Sign"
SIGNED=0
if [ -n "$MAC_SIGN_APP" ]; then
  SIGNED=1
  echo "signing with: $MAC_SIGN_APP"
fi
for p in "${PRODUCTS[@]}"; do
  set -- $p; name=$1; dir=$2
  for ext in vst3 component clap; do
    b="$dir/bin/$name.$ext"
    if [ "$SIGNED" = 1 ]; then
      codesign --force --deep --options runtime --timestamp --sign "$MAC_SIGN_APP" "$b"
    else
      codesign --force --deep --sign - "$b"
    fi
    codesign --verify --deep --strict "$b" || die "signature of $b"
  done
done

step "AU validation"
mkdir -p ~/Library/Audio/Plug-Ins/Components
for p in "${PRODUCTS[@]}"; do set -- $p; cp -R "$2/bin/$1.component" ~/Library/Audio/Plug-Ins/Components/; done
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2
auv() {   # name subtype maker
  if auval -v aumf "$2" "$3" > "dist/logs/auval-$1.txt" 2>&1; then echo "auval $1: PASS"
  else echo "auval $1: FAIL (dist/logs/auval-$1.txt)"; tail -5 "dist/logs/auval-$1.txt"; FAILED=1; fi
}
auv Dewitt Jayx Slpx; auv Airliner Airl Slpx
if [ "${AIRLINER_ALL:-0}" = 1 ]; then auv AirlinerLooper AlpS Slpx; auv AirlinerVinyl AvnS Slpx; fi

# ---------------------------------------------------------------------------------------------------------------
# 5. installers: one .pkg per product, the formats as choices (all on by default), welcome and readme pages
step "Installers"
readme() {   # title   -> text on the installer's readme page
  cat <<EOF
$1 for Mac (VST3, Audio Unit and CLAP). Universal: Apple Silicon and Intel, macOS $MINOS or newer.

The plug-ins go into the standard folders, for every user on this Mac:
  /Library/Audio/Plug-Ins/VST3
  /Library/Audio/Plug-Ins/Components   (Audio Unit: Logic, GarageBand, Ableton Live, ...)
  /Library/Audio/Plug-Ins/CLAP

After installing, restart your music app (or rescan plug-ins) and look for $1 under slapworx.
EOF
  if [ "$SIGNED" = 0 ]; then cat <<'EOF'

This test build is not signed with an Apple Developer ID yet, so macOS asks before opening it:
  - If double-clicking the installer says it "can't be opened", Control-click (right-click) it, choose Open,
    then Open again. On newer macOS: System Settings > Privacy & Security > "Open Anyway".
  - If your music app then refuses the plug-in, open Terminal and run:
      sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3" "/Library/Audio/Plug-Ins/Components" "/Library/Audio/Plug-Ins/CLAP"
EOF
  fi
}
mkpkg() {   # name title version pkg-id
  local name="$1" title="$2" ver="$3" id="$4" out=release w="build-tests/pkg-$1" dir=plugin
  case "$name" in Airliner*) dir=airliner;; esac
  rm -rf "$w" && mkdir -p "$w/res" "$w/scripts"
  printf '#!/bin/sh\n# let Logic and GarageBand see the new Audio Unit right away\nkillall -9 AudioComponentRegistrar 2>/dev/null\nexit 0\n' > "$w/scripts/postinstall"
  chmod +x "$w/scripts/postinstall"
  local choices="" outline=""
  for ext in vst3 component clap; do
    case $ext in vst3) folder=VST3; label="VST3";; component) folder=Components; label="Audio Unit (AU)";; clap) folder=CLAP; label="CLAP";; esac
    mkdir -p "$w/root-$ext/Library/Audio/Plug-Ins/$folder"
    cp -R "$dir/bin/$name.$ext" "$w/root-$ext/Library/Audio/Plug-Ins/$folder/"
    pkgbuild --analyze --root "$w/root-$ext" "$w/$ext.plist" > /dev/null
    /usr/bin/python3 - "$w/$ext.plist" <<'PY'
import plistlib, sys
p = sys.argv[1]
d = plistlib.load(open(p, 'rb'))
for e in d: e['BundleIsRelocatable'] = False      # always install into /Library/Audio/Plug-Ins
plistlib.dump(d, open(p, 'wb'))
PY
    local sc=""; [ $ext = component ] && sc="--scripts $w/scripts"
    pkgbuild --root "$w/root-$ext" --component-plist "$w/$ext.plist" $sc --identifier "$id.$ext" --version "$ver" \
             --install-location / "$w/$name-$ext.pkg" > /dev/null
    choices="$choices
    <choice id=\"$ext\" title=\"$label\" description=\"$title as $label, in /Library/Audio/Plug-Ins/$folder\" start_selected=\"true\">
        <pkg-ref id=\"$id.$ext\"/>
    </choice>
    <pkg-ref id=\"$id.$ext\" version=\"$ver\" onConclusion=\"none\">$name-$ext.pkg</pkg-ref>"
    outline="$outline<line choice=\"$ext\"/>"
  done
  printf '%s\n\nThis installs %s %s by slapworx.\n' "$title" "$title" "$ver" > "$w/res/welcome.txt"
  readme "$title" > "$w/res/readme.txt"
  cat > "$w/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$title $ver</title>
    <organization>audio.slapworx</organization>
    <domains enable_localSystem="true"/>
    <options customize="allow" require-scripts="false" rootVolumeOnly="true" hostArchitectures="arm64,x86_64"/>
    <os-version min="$MINOS"/>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <readme file="readme.txt" mime-type="text/plain"/>
    <choices-outline>$outline</choices-outline>$choices
</installer-gui-script>
EOF
  local file="$name-$ver$SUFFIX-Mac.pkg" sign=()
  [ -n "$MAC_SIGN_INSTALLER" ] && sign=(--sign "$MAC_SIGN_INSTALLER" --timestamp)
  productbuild --distribution "$w/distribution.xml" --resources "$w/res" --package-path "$w" "${sign[@]}" "dist/$out/$file" > /dev/null \
    || die "installer $file"
  # the bare bundles too, for copying by hand
  (cd "$dir/bin" && zip -qry "$ROOT/dist/$out/$name-$ver$SUFFIX-Mac-bundles.zip" "$name.vst3" "$name.component" "$name.clap")
  echo "made dist/$out/$file"
}
mkpkg Dewitt   "Dewitt"   $DEWITT_VERSION   audio.slapworx.dewitt
mkpkg Airliner "Airliner" $AIRLINER_VERSION audio.slapworx.airliner
if [ "${AIRLINER_ALL:-0}" = 1 ]; then
  mkpkg AirlinerLooper "Airliner Looper" $AIRLINER_VERSION audio.slapworx.airlinerlooper
  mkpkg AirlinerVinyl  "Airliner Vinyl"  $AIRLINER_VERSION audio.slapworx.airlinervinyl
fi

# ---------------------------------------------------------------------------------------------------------------
# 6. notarize (only with a Developer ID and an Apple ID app password)
if [ -n "$MAC_SIGN_INSTALLER" ] && [ "$SIGNED" = 1 ] && [ -n "$APPLE_ID" ] && [ -n "$APPLE_TEAM_ID" ] && [ -n "$APPLE_APP_PASSWORD" ]; then
  step "Notarize"
  for f in dist/release/*.pkg; do
    xcrun notarytool submit "$f" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" \
          --wait > "dist/logs/notarize-$(basename "$f").txt" 2>&1 \
      || { cat "dist/logs/notarize-$(basename "$f").txt"; die "notarizing $f"; }
    xcrun stapler staple "$f" > /dev/null && echo "notarized and stapled: $f"
  done
else
  echo; echo "(not notarized: needs MAC_SIGN_APP, MAC_SIGN_INSTALLER, APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD)"
fi

# ---------------------------------------------------------------------------------------------------------------
step "Done"
for f in dist/release/*; do echo "$(du -h "$f" | cut -f1)  $f"; done
( cd dist && shasum -a 256 release/* > logs/SHA256SUMS.txt )
[ "$FAILED" = 0 ] || die "some checks failed (see above)"
echo "all done"
