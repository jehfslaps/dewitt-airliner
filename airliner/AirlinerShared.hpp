// Airliner: what the plug-in and its window share (parameters, value text, the built-in presets).
//
// One source, six plug-ins:
//   AIRLINER_KIND 0  Airliner         both: the DJ Looper (A) and the Vinyl Sim (B), in series
//   AIRLINER_KIND 1  Airliner Looper  the DJ Looper on its own
//   AIRLINER_KIND 2  Airliner Vinyl   the Vinyl Sim on its own
// Every build has the same parameter list; the ones a build doesn't use are hidden from the host.
#pragma once

#include "YanceyParams.hpp"     // Dewitt's value formatting

#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef AIRLINER_KIND
#define AIRLINER_KIND 0
#endif

namespace airliner {

using yancey::Fmt;

enum Kind { kKindBoth = 0, kKindLooper = 1, kKindVinyl = 2 };
static constexpr int kKind = AIRLINER_KIND;
static constexpr bool kHasLooper = kKind != kKindVinyl;
static constexpr bool kHasVinyl = kKind != kKindLooper;

enum Param {
    pLoop = 0,      // the LOOP button (A)
    pPadMode,       // 0 hold, 1 latch (how the LOOP button behaves when you click it)
    pLen, pSpeed,   // DJ Looper knobs
    pVinyl,         // the ON button (B)
    pComp, pNoise, pFlu,    // Vinyl Sim knobs
    pOrder,         // 0 = A then B (loop, then vinyl), 1 = B then A
    pLimit,
    pOut,           // the first Airliner's OUT knob; kept (hidden) so its projects still open the same
    pInA, pOutA,    // the DJ Looper's INPUT and OUTPUT faders
    pInB, pOutB,    // the Vinyl Sim's
    oPhase,         // output: where the loop is, 0..1 (-1 when not looping)
    // outputs for the window: the MIDI controller
    oSide,          // which side the controller's knobs follow: 0 DJ Looper, 1 Vinyl Sim
    oLearnSeq,      // bumps when a controller link is learned
    oLearned,       // (slot + 1) * 32768 + code
    oCtlSeq,        // bumps when the controller changed something
    oTouch,         // the parameter the controller last touched * 4 + pickup state (0 picked, 1 turn up, 2 turn down)
    oNonce,         // this plug-in instance's number: a learn request must carry it (so a project saved
                    // mid-learn doesn't start learning when it's opened again)
    oMeterInA, oMeterOutA, oMeterInB, oMeterOutB,   // peak levels for the faders' meters (linear)
    oMirror0,       // the values the controller set: pLoop, pVinyl, pLen, pSpeed, pComp, pNoise, pFlu, pOutB
    kParamCount = oMirror0 + 8
};

// does this build use the parameter? (the rest are hidden from the host)
static inline bool inBuild(int p)
{
    switch (p) {
    case pLoop: case pPadMode: case pLen: case pSpeed: case pInA: case pOutA: case oPhase: case oMeterInA: case oMeterOutA:
        return kHasLooper;
    case pVinyl: case pComp: case pNoise: case pFlu: case pInB: case pOutB: case oMeterInB: case oMeterOutB:
        return kHasVinyl;
    case pOrder: return kKind == kKindBoth;
    case pOut: return false;
    default: return true;
    }
}

// ---------------------------------------------------------------------------------------------
// MIDI controller: what can be linked, and which control each knob drives

enum { kLearnLoop = 0, kLearnVinyl, kLearnKnob0, kNumCtlKnobs = 6, kNumLearn = kLearnKnob0 + kNumCtlKnobs };
static const int kMirrorParam[8] = { pLoop, pVinyl, pLen, pSpeed, pComp, pNoise, pFlu, pOutB };
static const int kEachTarget[kNumCtlKnobs] = { pLen, pSpeed, pComp, pNoise, pFlu, pOutB };

// a controller message: CC or note, channel, number
static inline int midiCode(bool cc, int ch, int num) { return (cc ? 0x4000 : 0) | ((ch & 15) << 7) | (num & 127); }

// the side the knobs are on: a single plug-in only has one
static inline int sideFor(int side) { return kKind == kKindLooper ? 0 : (kKind == kKindVinyl ? 1 : side); }

// the control knob j (0-5) drives: either one knob per control (both-sides build only), or the first three
// follow the side you last pressed (LOOP: LENGTH, SPEED; ON: COMP, NOISE, WOW FLUT)
static inline int knobTarget(int j, bool oneEach, int side)
{
    if (j < 0 || j >= kNumCtlKnobs) return -1;
    if (oneEach && kKind == kKindBoth) return kEachTarget[j];
    side = sideFor(side);
    if (side == 0) return j == 0 ? pLen : (j == 1 ? pSpeed : -1);
    return j == 0 ? pComp : (j == 1 ? pNoise : (j == 2 ? pFlu : -1));
}
// which controller knob drives a parameter (or -1)
static inline int knobForParam(int p, bool oneEach)
{
    for (int j = 0; j < kNumCtlKnobs; ++j) {
        if (oneEach && kKind == kKindBoth) { if (kEachTarget[j] == p) return j; }
        else if (knobTarget(j, false, 0) == p || knobTarget(j, false, 1) == p) return j;
    }
    return -1;
}

struct ParamSpec {
    const char* name;       // host name
    const char* symbol;
    const char* label;      // on the knob
    const char* screen;     // on the screen
    float min, max, def;
    bool integer, toggle;
    Fmt fmt;
};

// ours: Dewitt's DJ Looper and Vinyl Sim knobs
#define AIR_KNOBS \
    { "Length", "len", "LENGTH", "LENGTH", 0, 100, 50, false, false, yancey::fTapeLen }, \
    { "Speed", "speed", "SPEED", "SPEED", -1, 1, 1, false, false, yancey::fSpeed }, \
    { "Vinyl", "vinyl", "ON", "VINYL", 0, 1, 0, true, true, yancey::fNum }, \
    { "Compressor", "comp", "COMP", "COMP", 0, 100, 100, false, false, yancey::fNum }, \
    { "Noise", "noise", "NOISE", "NOISE", 0, 100, 30, false, false, yancey::fNum }, \
    { "Wow Flutter", "wowflut", "WOW FLUT", "WOW FLUT", 0, 100, 17, false, false, yancey::fNum },

static const ParamSpec kSpecs[kParamCount] = {
    { "Loop", "loop", "LOOP", "LOOP", 0, 1, 0, true, true, yancey::fNum },
    { "Pad Mode", "padmode", "PAD MODE", "PAD MODE", 0, 1, 1, true, true, yancey::fNum },
    AIR_KNOBS
    { "Order", "order", "ORDER", "ORDER", 0, 1, 0, true, true, yancey::fNum },
    { "Limit", "limit", "LIMIT", "LIMIT", 0, 1, 1, true, true, yancey::fNum },
    { "Output", "out", "OUT", "OUTPUT", -60, 6, 0, false, false, yancey::fNum },
    { "Looper Input", "in_a", "INPUT", "INPUT", -24, 12, 0, false, false, yancey::fNum },
    { "Looper Output", "out_a", "OUTPUT", "OUTPUT", -60, 12, 0, false, false, yancey::fNum },
    { "Vinyl Input", "in_b", "INPUT", "INPUT", -24, 12, 0, false, false, yancey::fNum },
    { "Vinyl Output", "out_b", "OUTPUT", "OUTPUT", -60, 12, 0, false, false, yancey::fNum },
    { "Loop Phase", "phase", "", "", -1, 1, -1, false, false, yancey::fNum },
    { "Knob Side", "o_side", "", "", 0, 1, 0, true, false, yancey::fNum },
    { "Learn Seq", "o_learnseq", "", "", 0, 1e7f, 0, false, false, yancey::fNum },
    { "Learned", "o_learned", "", "", 0, 1e7f, 0, false, false, yancey::fNum },
    { "Controller Seq", "o_ctlseq", "", "", 0, 1e7f, 0, false, false, yancey::fNum },
    { "Controller Touch", "o_touch", "", "", 0, 1e4f, 0, false, false, yancey::fNum },
    { "Instance", "o_nonce", "", "", 0, 1e7f, 0, false, false, yancey::fNum },
    { "Looper In Level", "o_mia", "", "", 0, 4, 0, false, false, yancey::fNum },
    { "Looper Out Level", "o_moa", "", "", 0, 4, 0, false, false, yancey::fNum },
    { "Vinyl In Level", "o_mib", "", "", 0, 4, 0, false, false, yancey::fNum },
    { "Vinyl Out Level", "o_mob", "", "", 0, 4, 0, false, false, yancey::fNum },
    { "Live Loop", "o_m0", "", "", 0, 1, 0, false, false, yancey::fNum },
    { "Live Vinyl", "o_m1", "", "", 0, 1, 0, false, false, yancey::fNum },
    { "Live Length", "o_m2", "", "", -1, 1000, 0, false, false, yancey::fNum },
    { "Live Speed", "o_m3", "", "", -1, 1000, 0, false, false, yancey::fNum },
    { "Live Comp", "o_m4", "", "", -1, 1000, 0, false, false, yancey::fNum },
    { "Live Noise", "o_m5", "", "", -1, 1000, 0, false, false, yancey::fNum },
    { "Live Wow", "o_m6", "", "", -1, 1000, 0, false, false, yancey::fNum },
    { "Live Out", "o_m7", "", "", -100, 100, 0, false, false, yancey::fNum },
};
#undef AIR_KNOBS

static inline bool isOutput(int i) { return i >= oPhase; }
static inline bool isGain(int i) { return i == pOut || i == pInA || i == pOutA || i == pInB || i == pOutB; }
static inline bool isKnob(int i) { return i == pLen || i == pSpeed || i == pComp || i == pNoise || i == pFlu || isGain(i); }
static inline bool isLooperParam(int i) { return i == pLoop || i == pPadMode || i == pLen || i == pSpeed || i == pInA || i == pOutA; }

static inline float clampP(int i, float v)
{
    const ParamSpec& s = kSpecs[i];
    v = v < s.min ? s.min : (v > s.max ? s.max : v);
    if (s.integer) v = std::round(v);
    return v;
}
static inline float normOf(int i, float v) { const ParamSpec& s = kSpecs[i]; return (clampP(i, v) - s.min) / (s.max - s.min); }
static inline float valueOf(int i, float n)
{
    const ParamSpec& s = kSpecs[i];
    n = n < 0.f ? 0.f : (n > 1.f ? 1.f : n);
    return clampP(i, s.min + n * (s.max - s.min));
}
static inline float gainOf(int i, float db) { return (i == pOut || i == pOutA || i == pOutB) && db <= -59.9f ? 0.f : std::pow(10.f, db / 20.f); }

static inline void formatValue(int i, float v, char* buf, size_t sz)
{
    if (isGain(i)) {
        if ((i == pOut || i == pOutA || i == pOutB) && v <= -59.9f) std::snprintf(buf, sz, "-inf");
        else std::snprintf(buf, sz, std::fabs(v) < 0.05f ? "0.0 dB" : "%+.1f dB", v);
        return;
    }
    yancey::formatFmt(kSpecs[i].fmt, v, buf, sz);
}

// the round screen: up to three characters, and what they are
static inline void screenValue(int i, float v, char* digits, size_t dsz, char* caption, size_t csz)
{
    const char* name = kSpecs[i].screen;
    if (isGain(i)) {
        if ((i == pOutA || i == pOutB || i == pOut) && v <= -59.9f) std::snprintf(digits, dsz, "OFF");
        else { const int d = (int)std::lround(v); std::snprintf(digits, dsz, d > 0 ? "+%d" : "%d", d); }
        std::snprintf(caption, csz, "%s \xC2\xB7 DB", name);
        return;
    }
    if (i == pLen) {
        const float ms = yancey::djTapeMs(v);
        if (ms < 999.5f) { std::snprintf(digits, dsz, "%d", (int)std::lround(ms)); std::snprintf(caption, csz, "%s \xC2\xB7 MS", name); }
        else { std::snprintf(digits, dsz, "%.1f", ms / 1000.f); std::snprintf(caption, csz, "%s \xC2\xB7 SEC", name); }
        return;
    }
    if (i == pSpeed) {
        const float r = v;
        std::snprintf(digits, dsz, "%d", (int)std::lround(r * 100.f));
        std::snprintf(caption, csz, "%s \xC2\xB7 %%", name);
        return;
    }
    std::snprintf(digits, dsz, "%03d", (int)std::lround(v));
    std::snprintf(caption, csz, "%s \xC2\xB7 VALUE", name);
}

// ---------------------------------------------------------------------------------------------
// built-in presets, written in Airliner's units (LENGTH 0-100, SPEED -1..1, vinyl knobs 0-100);
// kinds: which plug-ins list it (1 both, 2 looper, 4 vinyl)
struct Preset { const char* name; int kinds; float len, speed, comp, noise, flu; int order; };
static const Preset kPresets[] = {
    { "Default",        7, 50.f,  1.f,   100.f, 30.f, 17.f, 0 },
    { "Dusty Loop",     1, 40.f,  1.f,   100.f, 55.f, 30.f, 0 },
    { "Half-Time Tape", 3, 50.f,  0.5f,  80.f,  25.f, 20.f, 0 },
    { "Backwards Run",  3, 45.f, -1.f,   100.f, 30.f, 17.f, 0 },
    { "Tight Stutter",  3, 95.f,  1.f,   100.f, 15.f, 10.f, 0 },
    { "Long Grab",      2, 5.f,   1.f,   100.f, 30.f, 17.f, 0 },
    { "Slow Crawl",     2, 60.f,  0.25f, 100.f, 30.f, 17.f, 0 },
    { "Warped Record",  5, 50.f,  1.f,   60.f,  45.f, 70.f, 1 },
    { "Clean Squash",   5, 50.f,  1.f,   100.f, 0.f,  0.f,  0 },
    { "Dusty Record",   4, 50.f,  1.f,   100.f, 60.f, 30.f, 0 },
    { "Worn Out",       4, 50.f,  1.f,   70.f,  85.f, 55.f, 0 },
    { "Old Radio Loop", 1, 30.f,  1.f,   100.f, 80.f, 45.f, 1 },
};
static constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
static inline bool presetInBuild(int i) { return (kPresets[i].kinds & (1 << kKind)) != 0; }

// the parameters a preset sets in this build
static const int kPresetParamsAll[6] = { pLen, pSpeed, pComp, pNoise, pFlu, pOrder };
static inline bool presetParamInBuild(int p) { return inBuild(p); }

// the preset's value for one knob, in this build's units
static inline float presetValue(const Preset& p, int i)
{
    float v = 0.f;
    switch (i) {
    case pLen: v = p.len; break;
    case pSpeed: v = p.speed; break;
    case pComp: v = p.comp; break;
    case pNoise: v = p.noise; break;
    case pFlu: v = p.flu; break;
    case pOrder: return (float)p.order;
    default: return kSpecs[i].def;
    }
    if (&p == &kPresets[0]) return kSpecs[i].def;      // Default is exactly the knobs' starting points
    return clampP(i, v);
}

} // namespace airliner
