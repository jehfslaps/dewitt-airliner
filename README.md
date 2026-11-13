# Dewitt and Airliner, by slapworx

Audio effect plug-ins:

- **Dewitt**: a live pad effector for beat sets. 3 banks × 16 pads; each pad plays an effect (hold or latch),
  a move, a sample or a beat, and effects stack in a fixed chain. MIDI learn for pads and knobs, and it adapts
  to controllers with any number of knobs.
- **Airliner**: Dewitt's DJ Looper and Vinyl Sim as one effect (plus **Airliner Looper** and **Airliner Vinyl**
  on their own), in a hardware-style layout with INPUT/OUTPUT faders and controller learn.

Formats: Windows VST3 and CLAP; Mac VST3, AU and CLAP (universal: Apple Silicon and Intel, macOS 10.15+).

## Mac builds

GitHub builds the Mac versions on every push to `main`, or by hand with **Actions > Mac build > Run workflow**.
When the run is green, the **Mac-release** download at the bottom of its page has `Dewitt-<version>-Mac.pkg`
and `Airliner-<version>-Mac.pkg` (installers: VST3, AU and CLAP into /Library/Audio/Plug-Ins, each format a
choice) plus the bare bundles as zips. The **logs** download has every check, Apple's AU validator and SHA-256
sums. Tag a commit `mac-v<version>` (for example `mac-v1.2-beta1`) and the run also makes a draft pre-release.

The workflow signs with a Developer ID and notarizes once the repository has the signing secrets (listed at
the top of `.github/workflows/mac.yml`); until then the files are signed ad-hoc and macOS asks the first time
(the installer's readme page explains what to do).

## Building

See [BUILDING.md](BUILDING.md).

## License

Copyright © slapworx. All rights reserved. The source is public so the plug-ins can be built and checked in
the open; it isn't licensed for reuse or redistribution.

Third-party parts keep their own licenses: [DPF](https://github.com/DISTRHO/DPF) (ISC, fetched by
`tools/setup-dpf.sh`; patched files in `plugin/dpf-patches/`, including NanoVG and fontstash, zlib license),
FreeType in `ft-src/` (portions of this software are copyright © The FreeType Project, www.freetype.org, used
under the FreeType License, `ft-src/LICENSE.TXT`), and dr_wav, dr_flac and dr_mp3 in `plugin/third_party/`
(public domain / MIT-0).
