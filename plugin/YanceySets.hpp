// Dewitt's built-in sets (the SET menu): Default, then 16 sets in four groups.
// Each set lists its pads in order, bank A pad 1 (top left) to pad 16 (bottom right), then bank B, then C.
// Knob values are in the knob's own units (Hz, %, the n-th choice of a list...); kDef keeps the default.
// Every set keeps bank A's bottom row for the moves you reach for without looking.
#pragma once

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>
#include "YanceyParams.hpp"

namespace yancey {

static const float kDef = NAN;

struct SetPad { int type; bool latch; float k[kKnobsPerSlot]; };
struct FactorySet { const char* group; const char* name; std::vector<SetPad> pads; };

static inline SetPad E(int e, std::initializer_list<float> ks = {}, bool latch = false)
{
    SetPad p { typeOfEffect(e), latch, { kDef, kDef, kDef, kDef, kDef } };
    int i = 0; for (float v : ks) if (i < kKnobsPerSlot) p.k[i++] = v;
    return p;
}
static inline SetPad L(int e, std::initializer_list<float> ks = {}) { return E(e, ks, true); }   // latched
static inline SetPad MV(int m) { return SetPad { typeOfMove(m), false, { kDef, kDef, kDef, kDef, kDef } }; }

// the knob value a set asks for, as the 0..1 value the slot stores
static inline float setKnobNorm(int e, int k, float v)
{
    if (e < 0 || k >= numControls(e)) return 0.f;
    if (std::isnan(v)) return defaultNorm(e, k);
    return ctlSnap(kEffects[e].c[k], ctlToNorm(kEffects[e].c[k], v));
}

static inline const std::vector<FactorySet>& factorySets()
{
    // positions of the time-based choices (see kLoopLenNames, kDivNames, kSyncNames, kDelayTimeNames, kMpcTimeNames)
    enum { L32 = 0, L16, L8, L4, L2, L1BAR };                                      // Loop Grab / Back Spin LENGTH
    enum { D4 = 0, D4T, D8, D8T, D16, D16T, D32, D64 };                            // Beat Repeat / Rev Stepper / Scatter SIZE
    enum { S2BAR = 0, S1BAR, S2, S4, S4T, S8, S8T, S16, S16T, S32 };                 // sync RATE / SPEED / TIME
    enum { LP = 0, HP, BP };
    enum { CASSETTE = 0, FLUTTER, TUBE, VINYL, SAT, RADIO };
    enum { STUT = 0, REV, SHUF, HALF };
    const SetPad dryKill = MV(kMvDryKill), thrw = MV(kMvThrow), frz = MV(kMvFreeze), kill = MV(kMvKill), allOff = MV(kMvAllOff),
                 g8 = MV(kMvGrab8), g4 = MV(kMvGrab4), g2 = MV(kMvGrab2), g1 = MV(kMvGrab1), timing = MV(kMvTiming);
    // Isolator: LOW / MID / HIGH, 0 = cut, 0.5 = flat, 1 = +6 dB
    static const std::vector<FactorySet> sets = {

    // ============================================================ HIP-HOP
    { "HIP-HOP", "Boom Bap Chops", {
        E(kFxLoopGrab, { L4 }), E(kFxBeatRepeat, { D8 }), E(kFxRevStepper, { D8, 4 }), E(kFxStopper, { 100, S2 }),
        E(kFxHalfSpeed, { 1 }), E(kFxLpf, { 40, S1BAR, 70 }), E(kFxFilterDrive, { 900, 30, 45 }), E(kFxIsolator, { 0.f, 0.5f, 0.5f }),
        E(kFxVinyl), E(kFxCrusher, { 55, 35 }), E(kFxTapeEcho, { 6, 45 }), E(kFxReverb, { 20, 45, 70 }),
        dryKill, thrw, g4, kill,
        // B: more chops and colour
        E(kFxLoopGrab, { L8 }), E(kFxLoopGrab, { L2 }), E(kFxBeatRepeat, { D16 }), E(kFxBeatRepeat, { D8, 1 }),
        E(kFxScatter, { STUT }), E(kFxScatter, { SHUF }), E(kFxStutterV, { 60 }), E(kFxReverseV, { 50 }),
        E(kFxBackSpin), E(kFxCoil), E(kFxColor, { CASSETTE }), E(kFxColor, { TUBE }),
        g8, g2, frz, allOff } },

    { "HIP-HOP", "Dusty Sampler", {
        E(kFxVinyl, { 100, 55, 30 }), E(kFxCrusher, { 65, 45 }), E(kFxColor, { VINYL }), E(kFxColor, { CASSETTE }),
        E(kFxHalfSpeed, { 0 }), E(kFxLpf, { 25, S2BAR, 50 }), E(kFxTapeEcho, { 7, 50, 100, 55, 35 }), E(kFxReverb, { 20, 60, 70 }),
        E(kFxLoopGrab, { L4 }), E(kFxRevStepper, { D8, 4 }), E(kFxStopper, { 100, S1BAR }), E(kFxBackSpin, { L4, 45 }),
        dryKill, thrw, g4, kill,
        E(kFxColor, { FLUTTER }), E(kFxColor, { SAT }), E(kFxColor, { RADIO }), E(kFxDistortionV, { 35 }),
        E(kFxCrusherY, { 70, 50, 60 }), E(kFxChorusY, { 0.6f, 40, 10, 50 }), E(kFxWaveVerb, { 5, 35 }), E(kFxEchoVerb, { S4, 50 }),
        E(kFxIsolator, { 0.5f, 0.5f, 0.2f }), E(kFxIsolator, { 0.15f, 0.5f, 0.5f }), E(kFxBpf, { 45, S4, 40 }), E(kFxHpf, { 20, S2, 50 }),
        frz, g8, g2, allOff } },

    { "HIP-HOP", "Dub Siren", {
        E(kFxTapeEcho, { 9, 70, 100, 40, 40 }), E(kFxTapeEcho, { 6, 88 }), E(kFxEchoVerb, { S4, 60 }), E(kFxDelay, { 3, 55, 2 }),
        E(kFxCloudDelay, { 60, 12, 100, 50 }), E(kFxWaveVerb, { 8, 55 }), E(kFxReverb, { 60, 85, 80 }), E(kFxGranulator, { 30, 45, 80 }),
        E(kFxFilterDrive, { 2500, 55, 35, HP }), E(kFxIsolator, { 0.f, 0.5f, 0.5f }), E(kFxPhaser, { 60, S1BAR, 70 }), E(kFxFlanger, { 0.1f, 80, 70 }),
        dryKill, thrw, frz, kill,
        E(kFxDelay, { 6, 70, 1 }), E(kFxDelay, { 2, 65, 2 }), E(kFxEchoVerb, { S8, 80 }), E(kFxTapeEcho, { 10, 95 }),
        E(kFxRingModY, { 300, 60, 50 }), E(kFxCombY, { S4, 70, 70, 70 }), E(kFxChorusY), E(kFxCrusher, { 40, 20 }),
        E(kFxLpf, { 50, S1BAR, 80 }), E(kFxHpf, { 50, S1BAR, 80 }), E(kFxStopper, { 100, S2BAR }), E(kFxBackSpin),
        g2, g1, timing, allOff } },

    { "HIP-HOP", "Chop Shop", {
        E(kFxLoopGrab, { L16 }), E(kFxLoopGrab, { L8 }), E(kFxLoopGrab, { L4 }), E(kFxLoopGrab, { L2 }),
        E(kFxBeatRepeat, { D4 }), E(kFxBeatRepeat, { D8 }), E(kFxBeatRepeat, { D16 }), E(kFxBeatRepeat, { D32 }),
        E(kFxRevStepper, { D8, 4 }), E(kFxRevStepper, { D16, 8 }), E(kFxStutterV, { 70 }), E(kFxReverseV, { 60 }),
        dryKill, thrw, g8, kill,
        E(kFxScatter, { STUT }), E(kFxScatter, { REV }), E(kFxScatter, { SHUF }), E(kFxScatter, { HALF }),
        E(kFxBeatRepeat, { D8, 1 }), E(kFxBeatRepeat, { D16, 1 }), E(kFxHalfSpeed, { 1 }), E(kFxHalfSpeed, { 2 }),
        E(kFxStopper, { 100, S4 }), E(kFxStopper, { 100, S1BAR }), E(kFxBackSpin, { L8 }), E(kFxBackSpin, { L2 }),
        g4, g2, g1, frz,
        // C: the tape looper and the one-knob effects
        E(kFxDjLooper, { 50, 1 }), E(kFxDjLooper, { 80, 1 }), E(kFxDjLooper, { 50, -1 }), E(kFxDjLooper, { 20, 0.5f }),
        E(kFxCoil, { 50, S4 }), E(kFxCoil, { 80, S8 }), E(kFxLoopGrab, { L32 }), E(kFxLoopGrab, { L1BAR }),
        E(kFxStutterV, { 30 }), E(kFxStutterV, { 100 }), E(kFxReverseV, { 25 }), E(kFxReverseV, { 100 }),
        timing, allOff, dryKill, kill } },

    // ============================================================ LO-FI
    { "LO-FI", "Tape Deck", {
        E(kFxColor, { CASSETTE }), E(kFxColor, { FLUTTER }), E(kFxTapeEcho, { 7, 40, 100, 70, 30 }), E(kFxVinyl, { 70, 25, 45 }),
        E(kFxChorusY, { 0.5f, 60, 10, 60 }), E(kFxLpf, { 20, S2BAR, 45 }), E(kFxCrusher, { 40, 25 }), E(kFxHalfSpeed, { 0 }),
        E(kFxWaveVerb, { 4, 50, 70 }), E(kFxReverb, { 30, 55, 80 }), E(kFxLoopGrab, { L2 }), E(kFxStopper, { 100, S2BAR }),
        dryKill, thrw, g2, kill,
        E(kFxColor, { TUBE }), E(kFxColor, { SAT }), E(kFxDistortionV, { 25 }), E(kFxIsolator, { 0.5f, 0.5f, 0.25f }),
        E(kFxFlangerY, { 0.08f, 50, 40, 40 }), E(kFxPhaserY, { S1BAR, 50, 40, 50 }), E(kFxGranulator, { 15, 20, 120 }), E(kFxCloudDelay, { 70, 0, 100, 40, 70 }),
        E(kFxEchoVerb, { S2, 55 }), E(kFxDelay, { 3, 35, 0 }), E(kFxBackSpin, { L2, 30 }), E(kFxRevStepper, { D4, 2 }),
        frz, g4, g1, allOff } },

    { "LO-FI", "Late Night", {
        E(kFxReverb, { 40, 75, 85 }), E(kFxWaveVerb, { 6, 40 }), E(kFxEchoVerb, { S4, 50, 100, 60 }), E(kFxCloudDelay, { 60, 12, 60, 40, 60 }),
        E(kFxChorusY, { 0.7f, 50, 15, 70 }), E(kFxFlangerY, { 0.05f, 60, 30, 40 }), E(kFxPhaserY, { S2BAR, 70, 40, 60 }), E(kFxGranulator, { 10, 25, 150 }),
        E(kFxLpf, { 25, S1BAR, 55 }), E(kFxIsolator, { 0.5f, 0.5f, 0.3f }), E(kFxHalfSpeed, { 1 }), E(kFxLoopGrab, { L1BAR }),
        dryKill, frz, g1, kill,
        E(kFxTapeEcho, { 9, 60, 80, 50, 35 }), E(kFxDelay, { 2, 50, 2 }), E(kFxColor, { FLUTTER }), E(kFxVinyl, { 60, 20, 25 }),
        E(kFxReverb, { 0, 95, 90 }), E(kFxWaveVerb, { 12, 70 }), E(kFxStopper, { 100, S2BAR }), E(kFxBackSpin, { L1BAR, 25 }),
        E(kFxBpf, { 35, S2BAR, 40 }), E(kFxHpf, { 15, S2BAR, 45 }), E(kFxRevStepper, { D4, 2 }), E(kFxReverseV, { 30 }),
        thrw, g2, timing, allOff } },

    { "LO-FI", "Broken Radio", {
        E(kFxColor, { RADIO }), E(kFxCrusher, { 80, 60 }), E(kFxCrusherY, { 85, 70, 70 }), E(kFxDistortionV, { 70 }),
        E(kFxRingMod, { 200 }), E(kFxRingModY, { 800, 70, 60 }), E(kFxComb, { S16 }), E(kFxCombY, { S8, 80, 85, 80 }),
        E(kFxBpf, { 60, S8, 50 }), E(kFxFilterDrive, { 1500, 70, 80, BP }), E(kFxIsolator, { 0.f, 1.f, 0.f }), E(kFxStopper, { 100, S4 }),
        dryKill, thrw, g8, kill,
        E(kFxScatter, { STUT }), E(kFxScatter, { REV }), E(kFxStutterV, { 80 }), E(kFxGranulator, { 120, 60, 20 }),
        E(kFxFlanger, { 4, 90, 85 }), E(kFxPhaser, { 90, S16, 90 }), E(kFxCloudDelay, { 20, -12, 100, 70, 30 }), E(kFxDelay, { 12, 80 }),
        E(kFxColor, { SAT }), E(kFxVinyl, { 100, 80, 60 }), E(kFxHalfSpeed, { 2 }), E(kFxBeatRepeat, { D32, 1, 60 }),
        frz, g4, g2, allOff } },

    { "LO-FI", "Warm Wash", {
        E(kFxGranulator, { 25, 40, 100 }), E(kFxCloudDelay), E(kFxWaveVerb, { 7, 60 }), E(kFxEchoVerb, { S2, 70, 100, 50 }),
        E(kFxChorusY, { 1.2f, 70, 25, 80 }), E(kFxPhaserY, { S2, 60, 50, 70 }), E(kFxFlangerY, { 0.15f, 70, 50, 60 }), E(kFxReverb, { 80, 70, 90 }),
        E(kFxLpf, { 30, S1BAR, 60 }), E(kFxHpf, { 20, S1BAR, 60 }), E(kFxTapeEcho, { 7, 65, 100, 50, 40 }), E(kFxColor, { VINYL }),
        dryKill, thrw, frz, kill,
        E(kFxGranulator, { 200, 70, 15 }), E(kFxCloudDelay, { 85, -12, 100, 60, 90 }), E(kFxWaveVerb, { 12, 90 }), E(kFxReverb, { 20, 100, 90 }),
        E(kFxDelay, { 1, 60, 2 }), E(kFxEchoVerb, { S1BAR, 75 }), E(kFxHalfSpeed, { 1 }), E(kFxLoopGrab, { L1BAR }),
        E(kFxIsolator, { 0.2f, 0.5f, 0.5f }), E(kFxIsolator, { 0.5f, 0.5f, 0.2f }), E(kFxBpf, { 50, S1BAR, 70 }), E(kFxVinyl),
        g2, g1, timing, allOff } },

    // ============================================================ CLUB
    { "CLUB", "DJ Mixer", {
        E(kFxIsolator, { 0.f, 0.5f, 0.5f }), E(kFxIsolator, { 0.5f, 0.f, 0.5f }), E(kFxIsolator, { 0.5f, 0.5f, 0.f }), E(kFxFilterDrive, { 800, 35, 15, LP }),
        E(kFxFilterDrive, { 1500, 35, 15, HP }), E(kFxEchoVerb, { S4, 50 }), E(kFxDelay, { 6, 45, 2 }), E(kFxReverb, { 20, 60, 70 }),
        E(kFxLoopGrab, { L4 }), E(kFxLoopGrab, { L1BAR }), E(kFxBeatRepeat, { D16 }), E(kFxFlanger, { 0.2f, 75, 65 }),
        dryKill, thrw, g4, kill,
        E(kFxPhaser, { 50, S4, 60 }), E(kFxChorusY), E(kFxComb, { S8 }), E(kFxCoil, { 50, S4 }),
        E(kFxLpf, { 40, S1BAR, 70 }), E(kFxHpf, { 40, S1BAR, 70 }), E(kFxBpf, { 50, S2, 60 }), E(kFxTapeEcho, { 6, 60 }),
        E(kFxStopper, { 100, S1BAR }), E(kFxBackSpin, { L4 }), E(kFxScatter, { SHUF }), E(kFxStutterV, { 50 }),
        g8, g2, frz, allOff } },

    { "CLUB", "Build & Drop", {
        E(kFxHpf, { 45, S2BAR, 80 }), E(kFxFilterDrive, { 3000, 60, 40, HP }), E(kFxBeatRepeat, { D8 }), E(kFxBeatRepeat, { D16 }),
        E(kFxBeatRepeat, { D32 }), E(kFxStutterV, { 100 }), E(kFxReverb, { 10, 90, 90 }), E(kFxEchoVerb, { S4, 80 }),
        E(kFxIsolator, { 0.f, 0.5f, 0.5f }), E(kFxStopper, { 100, S1BAR }), E(kFxBackSpin, { L2, 70 }), E(kFxGranulator, { 60, 50, 40 }),
        dryKill, thrw, g2, kill,
        E(kFxLoopGrab, { L4 }), E(kFxLoopGrab, { L8 }), E(kFxLoopGrab, { L16 }), E(kFxLoopGrab, { L32 }),
        E(kFxDelay, { 9, 70, 2 }), E(kFxWaveVerb, { 10, 50 }), E(kFxCloudDelay, { 50, 12, 100, 70 }), E(kFxFlanger, { 0.5f, 90, 80 }),
        E(kFxCoil, { 80, S8 }), E(kFxScatter, { STUT }), E(kFxHalfSpeed, { 1 }), E(kFxCrusher, { 70, 40 }),
        frz, g4, g1, allOff } },

    { "CLUB", "Peak Time", {
        E(kFxFlanger, { 0.25f, 70, 60 }), E(kFxPhaser, { 60, S2, 70 }), E(kFxDelay, { 3, 50, 2 }), E(kFxEchoVerb, { S8, 55 }),
        E(kFxFilterDrive, { 1200, 45, 50, LP }), E(kFxIsolator, { 0.f, 0.5f, 0.5f }), E(kFxComb, { S16 }), E(kFxChorusY),
        E(kFxLoopGrab, { L2 }), E(kFxBeatRepeat, { D16 }), E(kFxScatter, { SHUF }), E(kFxCoil, { 60, S4 }),
        dryKill, thrw, g4, kill,
        // B: latched, to leave running with both hands free
        L(kFxPhaser), L(kFxFlanger), L(kFxLpf, { 35, S1BAR, 65 }), L(kFxHpf, { 35, S1BAR, 65 }),
        L(kFxDelay, { 6, 40, 1 }), L(kFxTapeEcho, { 6, 50 }), L(kFxReverb, { 20, 50, 70 }), L(kFxDistortionV, { 40 }),
        E(kFxStopper, { 100, S2 }), E(kFxBackSpin, { L4 }), E(kFxStutterV, { 60 }), E(kFxReverseV, { 40 }),
        g8, g2, timing, allOff } },

    { "CLUB", "Garage Shuffle", {
        E(kFxScatter, { SHUF, 60, 100, D16 }), E(kFxScatter, { HALF }), E(kFxScatter, { STUT, 60, 100, D16T }), E(kFxRevStepper, { D16, 2 }),
        E(kFxCoil, { 40, S8T }), E(kFxComb, { S8T }), E(kFxPhaser, { 50, S8T, 60 }), E(kFxChorusY),
        E(kFxEchoVerb, { S4T, 50 }), E(kFxDelay, { 7, 45, 2 }), E(kFxBpf, { 50, S4T, 60 }), E(kFxIsolator, { 0.5f, 0.25f, 0.5f }),
        dryKill, thrw, g8, kill,
        E(kFxBeatRepeat, { D8T }), E(kFxBeatRepeat, { D16T }), E(kFxLoopGrab, { L8 }), E(kFxLoopGrab, { L4 }),
        E(kFxStutterV, { 40 }), E(kFxReverseV, { 40 }), E(kFxHalfSpeed, { 0 }), E(kFxStopper, { 100, S4T }),
        E(kFxTapeEcho, { 2, 50 }), E(kFxReverb), E(kFxCrusher), E(kFxColor, { SAT }),
        g4, g2, frz, allOff } },

    // ============================================================ LIVE
    { "LIVE", "Scratch & Spin", {
        E(kFxDjLooper, { 50, 1 }), E(kFxDjLooper, { 50, -1 }), E(kFxDjLooper, { 85, 0.5f }), E(kFxDjLooper, { 20, -0.5f }),
        E(kFxBackSpin, { L8, 80 }), E(kFxBackSpin, { L4, 60 }), E(kFxBackSpin, { L1BAR, 35 }), E(kFxStopper, { 100, S4 }),
        E(kFxStopper, { 100, S1BAR }), E(kFxHalfSpeed, { 1 }), E(kFxVinyl), E(kFxLoopGrab, { L4 }),
        dryKill, thrw, g8, kill,
        E(kFxStopper, { 100, S2BAR }), E(kFxStopper, { 100, S8 }), E(kFxReverseV, { 50 }), E(kFxRevStepper, { D8, 4 }),
        E(kFxBeatRepeat, { D8, 1 }), E(kFxBeatRepeat, { D16 }), E(kFxStutterV, { 50 }), E(kFxScatter, { REV }),
        E(kFxIsolator, { 0.f, 0.5f, 0.5f }), E(kFxFilterDrive, { 700, 35, 25, LP }), E(kFxFilterDrive, { 2000, 35, 25, HP }), E(kFxEchoVerb),
        g4, g2, frz, allOff } },

    { "LIVE", "Latch Jam", {
        L(kFxLoopGrab), L(kFxDjLooper), L(kFxFilterDrive), L(kFxIsolator, { 0.f, 0.5f, 0.5f }),
        L(kFxTapeEcho), L(kFxEchoVerb), L(kFxWaveVerb), L(kFxStopper),
        L(kFxBackSpin), L(kFxCoil), L(kFxScatter), L(kFxVinyl),
        dryKill, thrw, g4, allOff,
        L(kFxLpf, { 35, S1BAR, 65 }), L(kFxHpf, { 35, S1BAR, 65 }), L(kFxPhaser), L(kFxFlanger),
        L(kFxChorusY), L(kFxComb, { S8 }), L(kFxDelay, { 6, 45, 2 }), L(kFxReverb),
        L(kFxCrusher), L(kFxColor, { CASSETTE }), L(kFxGranulator), L(kFxCloudDelay),
        frz, g2, timing, kill } },

    { "LIVE", "Transitions", {
        E(kFxStopper, { 100, S1BAR }), E(kFxStopper, { 100, S2 }), E(kFxBackSpin, { L2, 50 }), E(kFxBackSpin, { L1BAR, 30 }),
        E(kFxHpf, { 40, S2BAR, 80 }), E(kFxLpf, { 40, S2BAR, 80 }), E(kFxFilterDrive, { 400, 50, 30, LP }), E(kFxFilterDrive, { 4000, 50, 30, HP }),
        E(kFxReverb, { 0, 100, 90 }), E(kFxWaveVerb, { 12, 50 }), E(kFxEchoVerb, { S2, 85 }), E(kFxTapeEcho, { 9, 85 }),
        dryKill, thrw, frz, kill,
        // B: halve the loop or the repeat each pad along the row
        E(kFxLoopGrab, { L1BAR }), E(kFxLoopGrab, { L2 }), E(kFxLoopGrab, { L4 }), E(kFxLoopGrab, { L8 }),
        E(kFxBeatRepeat, { D4 }), E(kFxBeatRepeat, { D8 }), E(kFxBeatRepeat, { D16 }), E(kFxBeatRepeat, { D32 }),
        E(kFxHalfSpeed, { 0 }), E(kFxHalfSpeed, { 1 }), E(kFxHalfSpeed, { 2 }), E(kFxIsolator, { 0.f, 0.5f, 0.5f }),
        g1, g2, timing, allOff } },

    { "LIVE", "FX Lab", {
        E(kFxGranulator, { 150, 70, 25 }), E(kFxCloudDelay, { 30, -12, 100, 70, 80 }), E(kFxCloudDelay, { 50, 7, 100, 60, 50 }), E(kFxRingModY, { 1200, 100, 70 }),
        E(kFxCombY, { S32, 90, 90, 100 }), E(kFxFlangerY, { 8, 100, 95, 100 }), E(kFxPhaserY, { S32, 100, 90, 100 }), E(kFxCrusherY, { 95, 85, 100 }),
        E(kFxDistortionV, { 100 }), E(kFxCoil, { 100, S16, 80, 100, 60 }), E(kFxScatter, { HALF, 100 }), E(kFxBeatRepeat, { D64, 0, 80 }),
        dryKill, thrw, frz, kill,
        E(kFxWaveVerb, { 12, 100 }), E(kFxReverb, { 250, 100, 100 }), E(kFxDelay, { 15, 90, 1 }), E(kFxTapeEcho, { 0, 95, 100, 100 }),
        E(kFxFilterDrive, { 200, 90, 100, LP }), E(kFxFilterDrive, { 8000, 90, 60, BP }), E(kFxIsolator, { 1.f, 0.f, 1.f }), E(kFxRingMod, { 400 }),
        E(kFxCrusher, { 100, 90 }), E(kFxColor, { RADIO }), E(kFxHalfSpeed, { 2 }), E(kFxDjLooper, { 100, -1 }),
        g8, g1, timing, allOff } },
    };
    return sets;
}

static inline int numFactorySets() { return 1 + (int)factorySets().size(); }          // Default + the list
static inline const char* factorySetName(int i) { return i <= 0 ? "Default" : factorySets()[(size_t)(i - 1)].name; }
static inline const char* factorySetGroup(int i) { return i <= 0 ? "" : factorySets()[(size_t)(i - 1)].group; }

// a built-in set as slot parameters (vals: kNumSlots * kSlotFields); returns how many banks it uses
static inline int factorySetParams(int i, float* vals)
{
    for (int p = 0; p < kNumSlots * kSlotFields; ++p) vals[p] = paramDefault(p);
    if (i <= 0) return 1;
    const std::vector<SetPad>& pads = factorySets()[(size_t)(i - 1)].pads;
    int used = 1;
    for (int s = 0; s < kNumSlots; ++s) {
        const bool has = s < (int)pads.size();
        const SetPad sp = has ? pads[(size_t)s] : SetPad { kTypeEmpty, false, { kDef, kDef, kDef, kDef, kDef } };
        const int e = effectOfType(sp.type);
        vals[slotParam(s, F_TYPE)] = (float)sp.type;
        vals[slotParam(s, F_LATCH)] = sp.latch ? 1.f : 0.f;
        vals[slotParam(s, F_REL)] = 0.f; vals[slotParam(s, F_SNAP)] = 0.f;
        vals[slotParam(s, F_VOL)] = kPadVolDefault;
        for (int k = 0; k < kKnobsPerSlot; ++k) vals[slotParam(s, F_K1 + k)] = setKnobNorm(e, k, sp.k[k]);
        if (has && sp.type != kTypeEmpty) used = std::max(used, s / kPadsPerPage + 1);
    }
    return used;
}

} // namespace yancey
