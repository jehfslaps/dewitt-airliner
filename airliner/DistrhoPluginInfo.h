#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

// Three builds from one source (see AirlinerShared.hpp):
//   make                       Airliner
//   make KIND=looper           Airliner Looper
//   make KIND=vinyl            Airliner Vinyl
#ifndef AIRLINER_KIND
#define AIRLINER_KIND 0
#endif

#define DISTRHO_PLUGIN_BRAND    "slapworx"

#if AIRLINER_KIND == 1
    #define DISTRHO_PLUGIN_NAME     "Airliner Looper"
    #define DISTRHO_PLUGIN_URI      "urn:slapworx:airliner-looper"
    #define DISTRHO_PLUGIN_CLAP_ID  "audio.slapworx.airliner-looper"
    #define DISTRHO_PLUGIN_UNIQUE_ID AlpS
  #define DISTRHO_UI_DEFAULT_WIDTH  600
#elif AIRLINER_KIND == 2
    #define DISTRHO_PLUGIN_NAME     "Airliner Vinyl"
    #define DISTRHO_PLUGIN_URI      "urn:slapworx:airliner-vinyl"
    #define DISTRHO_PLUGIN_CLAP_ID  "audio.slapworx.airliner-vinyl"
    #define DISTRHO_PLUGIN_UNIQUE_ID AvnS
  #define DISTRHO_UI_DEFAULT_WIDTH  600
#else
    #define DISTRHO_PLUGIN_NAME     "Airliner"
    #define DISTRHO_PLUGIN_URI      "urn:slapworx:airliner"
    #define DISTRHO_PLUGIN_CLAP_ID  "audio.slapworx.airliner"
    #define DISTRHO_PLUGIN_UNIQUE_ID Airl
  #define DISTRHO_UI_DEFAULT_WIDTH  1180
#endif
  #define DISTRHO_PLUGIN_BRAND_ID  Slpx
#define DISTRHO_UI_DEFAULT_HEIGHT       630

#define DISTRHO_PLUGIN_HAS_UI           1
#define DISTRHO_PLUGIN_IS_RT_SAFE       1
#define DISTRHO_PLUGIN_NUM_INPUTS       2
#define DISTRHO_PLUGIN_NUM_OUTPUTS      2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  1
#define DISTRHO_PLUGIN_WANT_STATE       1
#define DISTRHO_PLUGIN_WANT_FULL_STATE  0
#define DISTRHO_PLUGIN_WANT_LATENCY     1
#define DISTRHO_PLUGIN_WANT_PROGRAMS    0

#define DISTRHO_UI_USE_NANOVG           1
#define DISTRHO_UI_USER_RESIZABLE       1

#define DISTRHO_PLUGIN_AU_TYPE          aumf    // a music effect: it takes the controller's MIDI
#define DISTRHO_PLUGIN_VST3_CATEGORIES  "Fx|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES    "audio-effect", "stereo"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
