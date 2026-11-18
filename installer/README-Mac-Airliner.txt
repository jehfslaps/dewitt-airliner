AIRLINER 1.0 by slapworx
========================

A DJ looper and a vinyl sim in one effect. LOOP grabs a tape loop of the beat the
moment you hit it and drops it when you let go. ON fades in an old record: compression,
surface noise, wow and flutter. ORDER swaps the two.

macOS 10.15 or newer, Apple Silicon and Intel. Audio Unit, VST3 and CLAP.


INSTALLING
----------
1. Close your music app.
2. Double-click "Install Airliner.pkg" and click through the installer.
   Keep the formats you use ticked (AU for Logic and GarageBand).
3. Open your music app. Airliner shows up under slapworx.

Rather copy the files yourself? Everything is in the "manual install" folder:
  Airliner.component  ->  /Library/Audio/Plug-Ins/Components
  Airliner.vst3       ->  /Library/Audio/Plug-Ins/VST3
  Airliner.clap       ->  /Library/Audio/Plug-Ins/CLAP
(FL Studio on an Apple Silicon Mac without Rosetta: use the installer for the VST3.
 It keeps the Apple Silicon half only, which FL needs there.)


GETTING STARTED
---------------
- Drag a knob or fader to change it, double-click to reset.
- MIDI controller: send your controller's MIDI to Airliner (the same way as for any
  MIDI-controlled effect in your music app; in Logic, load it as an AU MIDI-controlled
  effect), then right-click any control and press the pad or turn the knob you want for
  it. Links are saved on this computer, so the same controller works in every project.


UNINSTALLING
------------
Delete Airliner.component, Airliner.vst3 and Airliner.clap from the folders above.
Your settings are in ~/Library/Application Support/slapworx/Airliner.


CREDITS
-------
Copyright (c) slapworx. All rights reserved. https://slapworx.net

Airliner uses:
- DPF (DISTRHO Plugin Framework), ISC license, with NanoVG and fontstash
  (zlib license).
- Portions of this software are copyright (c) The FreeType Project
  (www.freetype.org). All rights reserved. Used under the FreeType License.
The license texts are in the "licenses" folder.
