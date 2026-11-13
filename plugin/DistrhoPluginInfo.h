#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// Dewitt was called JAY up to 1.2. The IDs never changed, so projects made with JAY open in Dewitt.
#define DISTRHO_PLUGIN_BRAND    "slapworx"
#define DISTRHO_PLUGIN_NAME     "Dewitt"
#define DISTRHO_PLUGIN_URI      "urn:slapworx:jay"
#define DISTRHO_PLUGIN_CLAP_ID  "audio.slapworx.jay"
#define DISTRHO_PLUGIN_BRAND_ID  Slpx
#define DISTRHO_PLUGIN_UNIQUE_ID Jayx

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       2
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_TIMEPOS     1
#define DISTRHO_PLUGIN_WANT_PROGRAMS    0

#define DISTRHO_UI_USE_NANOVG           1
#define DISTRHO_UI_USER_RESIZABLE       1
#define DISTRHO_UI_FILE_BROWSER         1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_UI_DEFAULT_WIDTH        1080
#define DISTRHO_UI_DEFAULT_HEIGHT       840

// AU: an effect that takes MIDI (the pads), so Logic lists it as a MIDI-controlled effect
#define DISTRHO_PLUGIN_AU_TYPE          aumf
#define DISTRHO_PLUGIN_VST3_CATEGORIES  "Fx|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES    "audio-effect", "stereo"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
