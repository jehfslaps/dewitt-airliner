DEWITT 1.0 by slapworx
======================

40 pad effects for live beat sets. 3 banks x 16 pads: each pad plays an effect
(hold or latch), a move, a sample or a beat. MIDI learn for pads and knobs.

macOS 10.15 or newer, Apple Silicon and Intel. Audio Unit, VST3 and CLAP.


INSTALLING
----------
1. Close your music app.
2. Double-click "Install Dewitt.pkg" and click through the installer.
   Keep the formats you use ticked (AU for Logic and GarageBand).
3. Open your music app. Dewitt shows up under slapworx.

Rather copy the files yourself? Everything is in the "manual install" folder:
  Dewitt.component  ->  /Library/Audio/Plug-Ins/Components
  Dewitt.vst3       ->  /Library/Audio/Plug-Ins/VST3
  Dewitt.clap       ->  /Library/Audio/Plug-Ins/CLAP
(FL Studio on an Apple Silicon Mac without Rosetta: use the installer for the VST3.
 It keeps the Apple Silicon half only, which FL needs there.)


GETTING STARTED
---------------
1. Send your controller's MIDI to Dewitt. The How to Set Up MIDI pictures show
   how for FL Studio, Ableton Live, Logic, Studio One, Bitwig, Reaper and Cubase.
   Logic: load Dewitt as an AU MIDI-controlled effect (see the Logic picture).
2. Click SET UP CONTROLLER in Dewitt and follow the steps.
3. The user manual explains every effect, move and set.


UNINSTALLING
------------
Delete Dewitt.component, Dewitt.vst3 and Dewitt.clap from the folders above.
Your settings are in ~/Library/Application Support/slapworx/Dewitt.


CREDITS
-------
Copyright (c) slapworx. All rights reserved. https://slapworx.net

Dewitt uses:
- DPF (DISTRHO Plugin Framework), ISC license, with NanoVG and fontstash
  (zlib license).
- Portions of this software are copyright (c) The FreeType Project
  (www.freetype.org). All rights reserved. Used under the FreeType License.
- dr_wav, dr_flac and dr_mp3 by David Reid (public domain / MIT-0).
The license texts are in the "licenses" folder.
