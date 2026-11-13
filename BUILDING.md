# Building Dewitt and Airliner

Both use [DPF](https://github.com/DISTRHO/DPF) with NanoVG, a small static FreeType for text, and (Dewitt) the
single-file dr_wav, dr_flac and dr_mp3 decoders in `plugin/third_party` for sample pads.

## Quick start (Linux or Mac)

```
tools/setup-dpf.sh                                           # DPF at the tested commit, with the patches
tools/build-freetype.sh freetype/libfreetype-linux.a gcc ar  # -mac.a with clang; -win.a with x86_64-w64-mingw32-gcc
make -C plugin                                               # Dewitt
make -C airliner KIND=both|looper|vinyl                      # Airliner, Airliner Looper, Airliner Vinyl
```

Output lands in `plugin/bin/` and `airliner/bin/`. Linux needs the libgl and libx11 dev packages.

**Windows** (cross-compiling with MinGW-w64): add
`WINDOWS=true CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ AR=x86_64-w64-mingw32-ar` to the `make` lines
(and build `freetype/libfreetype-win.a`). Delete `dpf/build*` when switching between Windows and Linux builds.
The Windows installer: `installer/make-installer.sh <folder with Dewitt.clap and Dewitt.vst3> <output dir>`
(needs NSIS).

**Mac:** `tools/build-mac.sh` does everything for Dewitt and Airliner: DPF, FreeType, each plug-in for arm64 and
x86_64 separately, joined with `lipo` into universal VST3/AU/CLAP bundles (macOS 10.15+), the checks (natively,
plus the Intel half under Rosetta), signing (Developer ID when `MAC_SIGN_APP` is set, otherwise ad-hoc), auval,
installers (one .pkg per product, the formats as choices) and, with the Apple credentials, notarization.
Output: `dist/release/` and `dist/logs/`. `AIRLINER_ALL=1` adds Airliner Looper and Vinyl; `LABEL=` sets the
text after the version in file names (default `beta`). GitHub Actions runs it (`.github/workflows/mac.yml`).

## Checks

```
g++ -std=gnu++17 -O2 -Idpf/distrho/src test/wtest.cpp -o wtest -ldl
./wtest plugin/bin/Dewitt.clap 48000          # 123 checks

g++ -std=gnu++17 -O2 -ffast-math -fno-finite-math-only -Idpf/distrho/src -Iplugin -DAIRLINER_KIND=0 test/atest.cpp -o atest -ldl
./atest airliner/bin/Airliner.clap <empty settings folder>     # KIND 1 = Airliner Looper, 2 = Airliner Vinyl

g++ -std=gnu++17 -O2 -Iplugin test/foldtest.cpp -o foldtest && ./foldtest    # the fewer-knobs rules
```

On Windows, build the checks with x86_64-w64-mingw32-g++ -static. One known failure at 44.1 kHz only,
"CRUSHER · ALT sounds the same as LOFI", is old and harmless. `test/htest.cpp` is a small CLAP host that can
also open the UI (`./htest plugin/bin/Dewitt.clap gui`, X11).

## DPF patches

`tools/setup-dpf.sh` copies the three files in `plugin/dpf-patches/` over DPF's:

- `Makefile` -> `dpf/dgl/Makefile` (turns on FreeType for text)
- `nanovg.c` -> `dpf/dgl/src/nanovg/nanovg.c` (2x glyph oversampling)
- `fontstash.h` -> `dpf/dgl/src/nanovg/fontstash.h` (unhinted glyphs)

## FreeType

`tools/build-freetype.sh` builds FreeType (the source is in `ft-src/`) as a static library with only the modules
in `freetype/custom` (zlib, png, bzip2, brotli and harfbuzz off).
