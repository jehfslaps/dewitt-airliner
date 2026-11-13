// Yancey: shared definitions for DSP and UI.
// JAY: three banks of 16 pads. Bank A starts with the default layout; banks B and C start
// empty. Every pad plays an effect (hold by default, latch optional), a move (Kill, Dry Kill, Throw...),
// a one-shot sample or a beat. Effects stack freely in a fixed order. MIDI belongs to the pad position.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>

namespace yancey {

// ------------------------------------------------------------------------------------------------
// Effects

enum EffectId {
    kFxLoopGrab = 0, kFxDjLooper, kFxFilterDrive, kFxIsolator,
    kFxTapeEcho, kFxEchoVerb, kFxWaveVerb, kFxCloudDelay,
    kFxStopper, kFxBackSpin, kFxCoil, kFxScatter,
    kFxVinyl, kFxCrusher, kFxPhaser,
    kFxChorus, kFxFlanger, kFxRingMod, kFxComb,
    // the classic sampler set
    kFxHalfSpeed, kFxLpf, kFxHpf, kFxBpf, kFxColor, kFxGranulator, kFxBeatRepeat, kFxRevStepper, kFxDelay, kFxReverb,
    // alternate versions of shared effects: same sound, every knob
    kFxChorusY, kFxFlangerY, kFxPhaserY, kFxCombY, kFxRingModY, kFxCrusherY,
    kNumLegacyEffects,
    // the Valens effects (from Jehf's Valens plug-in): one knob each, 0-100
    kFxStutterV = kNumLegacyEffects, kFxReverseV, kFxDistortionV,
    kNumEffects
};

// the order effects are chained in, whatever order their pads are pressed
static const int kChainOrder[kNumEffects] = {
    kFxLoopGrab, kFxDjLooper, kFxStutterV, kFxReverseV, kFxBeatRepeat, kFxRevStepper, kFxStopper, kFxBackSpin, kFxHalfSpeed, kFxScatter, kFxCoil,
    kFxFilterDrive, kFxIsolator, kFxLpf, kFxHpf, kFxBpf,
    kFxChorus, kFxChorusY, kFxFlanger, kFxFlangerY, kFxPhaser, kFxPhaserY, kFxRingMod, kFxRingModY, kFxComb, kFxCombY,
    kFxVinyl, kFxCrusher, kFxCrusherY, kFxColor, kFxDistortionV,
    kFxTapeEcho, kFxEchoVerb, kFxDelay, kFxCloudDelay, kFxGranulator, kFxWaveVerb, kFxReverb
};

enum FamilyId { kFamTime = 0, kFamFilter, kFamSpace, kFamTexture, kFamMod, kNumFamilies };
static const char* const kFamilyNames[kNumFamilies] = { "TIME", "FILTER", "SPACE", "TEXTURE", "MOD" };

enum Scale { kLin = 0, kLog, kInt, kBool };
enum Fmt { fBipolarDb, fHz, fPct, fTime, fLen, fRetrig, fMs, fSpeed, fNum, fSync, fDiv, fBits,
           fFiltType, fSec, fSemi, fScat, fSpread, fNorm, fLfo, fHalf, fColorMode, fOnOff, fDRange, fDens, fBitsMpc, fMpcTime, fDjLen, fDjSpeed, fValens, fTapeLen };

struct CtlSpec { const char* label; float min, max, def; Scale scale; Fmt fmt; };
// tail: echo and reverb effects, which can cut or ring out on release
struct EffectSpec { const char* name; int family; int nFront, nAdv; int tail; CtlSpec c[5]; };


// the DJ Looper's LENGTH knob (0-100) as a loop length in ms: right = shorter
static inline float djTapeMs(float v) { v = v < 0.f ? 0.f : (v > 100.f ? 100.f : v); return 229.f - 1.92f * v; }

static const CtlSpec kNoCtl = { "", 0, 1, 0, kLin, fNorm };

static const EffectSpec kEffects[kNumEffects] = {
    { "LOOP GRAB", kFamTime, 3, 0, 0, {
        { "LENGTH", 0, 5, 3, kInt, fLen }, { "RETRIG", 0, 8, 0, kInt, fRetrig }, { "MIX", 0, 100, 100, kLin, fPct }, kNoCtl, kNoCtl } },
    // Dewitt: our own tape looper. The pad grabs the loop; MIX (100%) and GLIDE (30%) are fixed.
    // LENGTH (1.2.1): turning right makes the loop shorter (and, as tape, higher), 229 down to 37 ms, even steps.
    { "DJ LOOPER", kFamTime, 2, 0, 0, {
        { "LENGTH", 0, 100, 50, kLin, fTapeLen }, { "SPEED", -1, 1, 1, kLin, fSpeed }, kNoCtl, kNoCtl, kNoCtl } },
    { "FILTER + DRIVE", kFamFilter, 3, 2, 0, {
        { "CUTOFF", 30, 20000, 1200, kLog, fHz }, { "RESO", 0, 100, 35, kLin, fPct }, { "DRIVE", 0, 100, 25, kLin, fPct },
        { "TYPE", 0, 2, 0, kInt, fFiltType }, { "MIX", 0, 100, 100, kLin, fPct } } },
    { "ISOLATOR", kFamFilter, 3, 2, 0, {
        { "LOW", 0, 1, 0.5f, kLin, fBipolarDb }, { "MID", 0, 1, 0.5f, kLin, fBipolarDb }, { "HIGH", 0, 1, 0.5f, kLin, fBipolarDb },
        { "LO SPLIT", 60, 600, 250, kLog, fHz }, { "HI SPLIT", 1000, 8000, 2500, kLog, fHz } } },
    { "TAPE ECHO", kFamSpace, 3, 2, 1, {
        { "TIME", 0, 12, 6, kInt, fTime }, { "FEEDBACK", 0, 99, 45, kLin, fPct }, { "MIX", 0, 100, 100, kLin, fPct },
        { "WOW", 0, 100, 30, kLin, fPct }, { "TONE", 0, 100, 45, kLin, fPct } } },
    { "ECHO VERB", kFamSpace, 3, 2, 1, {
        { "TIME", 0, 10, 3, kInt, fSync }, { "FEEDBACK", 0, 95, 45, kLin, fPct }, { "MIX", 0, 100, 100, kLin, fPct },
        { "DAMP", 0, 100, 40, kLin, fPct }, { "SPREAD", 0, 1, 1, kInt, fSpread } } },
    { "WAVE VERB", kFamSpace, 3, 2, 1, {
        { "DECAY", 0.3f, 12, 3, kLog, fSec }, { "MOD", 0, 100, 45, kLin, fPct }, { "MIX", 0, 100, 100, kLin, fPct },
        { "PRE DELAY", 0, 250, 20, kLin, fMs }, { "TONE", 0, 100, 55, kLin, fPct } } },
    { "CLOUD DELAY", kFamSpace, 3, 2, 1, {
        { "WINDOW", 0, 100, 50, kLin, fPct }, { "PITCH", -12, 12, 12, kInt, fSemi }, { "MIX", 0, 100, 100, kLin, fPct },
        { "FEEDBACK", 0, 95, 45, kLin, fPct }, { "CLOUDY", 0, 100, 50, kLin, fPct } } },
    { "STOPPER", kFamTime, 3, 0, 0, {
        { "DEPTH", 0, 100, 100, kLin, fPct }, { "RATE", 0, 10, 1, kInt, fSync }, { "RESO", 0, 100, 20, kLin, fPct }, kNoCtl, kNoCtl } },
    { "BACK SPIN", kFamTime, 3, 0, 0, {
        { "LENGTH", 0, 5, 3, kInt, fLen }, { "SPEED", 0, 100, 60, kLin, fPct }, { "MIX", 0, 100, 100, kLin, fPct }, kNoCtl, kNoCtl } },
    { "COIL", kFamTime, 3, 2, 0, {
        { "DEPTH", 0, 100, 50, kLin, fPct }, { "RATE", 0, 10, 3, kInt, fSync }, { "RESO", 0, 100, 40, kLin, fPct },
        { "FLT MOD", 0, 100, 60, kLin, fPct }, { "AMP MOD", 0, 100, 30, kLin, fPct } } },
    { "SCATTER", kFamTime, 3, 1, 0, {
        { "TYPE", 0, 3, 0, kInt, fScat }, { "DEPTH", 0, 100, 60, kLin, fPct }, { "MIX", 0, 100, 100, kLin, fPct },
        { "SIZE", 0, 7, 4, kInt, fDiv }, kNoCtl } },
    // Dewitt: our own record-deck vinyl sim (1.2.1), tuned by measurement: same knob spread, same starting points
    { "VINYL SIM", kFamTexture, 3, 0, 0, {
        { "COMP", 0, 100, 100, kLin, fNum }, { "NOISE", 0, 100, 30, kLin, fNum }, { "WOW FLUT", 0, 100, 17, kLin, fNum }, kNoCtl, kNoCtl } },
    { "LOFI", kFamTexture, 2, 0, 0, {
        { "BITCRUSH", 0, 100, 50, kLin, fBitsMpc }, { "DECIMATOR", 0, 100, 30, kLin, fPct }, kNoCtl, kNoCtl, kNoCtl } },
    { "PHASER", kFamMod, 3, 0, 0, {
        { "FEEDBACK", 0, 100, 50, kLin, fPct }, { "SPEED", 0, 10, 3, kInt, fSync }, { "RANGE", 0, 100, 60, kLin, fPct }, kNoCtl, kNoCtl } },
    // chorus rate 0.40-3.20 Hz, flanger rate 0.02-10 Hz
    { "CHORUS", kFamMod, 3, 0, 0, {
        { "RATE", 0.4f, 3.2f, 0.9f, kLog, fLfo }, { "DEPTH", 0, 100, 55, kLin, fPct }, { "FEEDBACK", 0, 100, 15, kLin, fPct }, kNoCtl, kNoCtl } },
    { "FLANGER", kFamMod, 3, 0, 0, {
        { "RATE", 0.02f, 10.f, 0.25f, kLog, fLfo }, { "DEPTH", 0, 100, 70, kLin, fPct }, { "FEEDBACK", 0, 100, 60, kLin, fPct }, kNoCtl, kNoCtl } },
    { "RING MOD", kFamMod, 1, 0, 0, {
        { "MAX FREQ", 40, 400, 120, kLog, fHz }, kNoCtl, kNoCtl, kNoCtl, kNoCtl } },
    { "COMB FILTER", kFamMod, 1, 0, 0, {
        { "SPEED", 0, 10, 5, kInt, fSync }, kNoCtl, kNoCtl, kNoCtl, kNoCtl } },
    // the classic sampler set: a few simple knobs each
    { "HALF SPEED", kFamTime, 2, 0, 0, {
        { "SPEED", 0, 2, 1, kInt, fHalf }, { "MIX", 0, 100, 100, kLin, fPct }, kNoCtl, kNoCtl, kNoCtl } },
    { "LP FILTER", kFamFilter, 3, 0, 0, {
        { "RESONANCE", 0, 100, 30, kLin, fPct }, { "SPEED", 0, 10, 3, kInt, fSync }, { "RANGE", 0, 100, 60, kLin, fPct }, kNoCtl, kNoCtl } },
    { "HP FILTER", kFamFilter, 3, 0, 0, {
        { "RESONANCE", 0, 100, 30, kLin, fPct }, { "SPEED", 0, 10, 3, kInt, fSync }, { "RANGE", 0, 100, 60, kLin, fPct }, kNoCtl, kNoCtl } },
    { "BP FILTER", kFamFilter, 3, 0, 0, {
        { "RESONANCE", 0, 100, 30, kLin, fPct }, { "SPEED", 0, 10, 3, kInt, fSync }, { "RANGE", 0, 100, 60, kLin, fPct }, kNoCtl, kNoCtl } },
    { "COLOR", kFamTexture, 1, 0, 0, {
        { "MODE", 0, 5, 3, kInt, fColorMode }, kNoCtl, kNoCtl, kNoCtl, kNoCtl } },
    { "GRANULATOR", kFamSpace, 3, 0, 1, {
        { "DENSITY", 1, 300, 20, kLog, fDens }, { "FEEDBACK", 0, 100, 30, kLin, fPct }, { "GRAIN LEN", 10, 200, 60, kLog, fMs }, kNoCtl, kNoCtl } },
    { "BEAT REPEAT", kFamTime, 3, 0, 0, {
        { "DIVISION", 0, 7, 2, kInt, fDiv }, { "REVERSE", 0, 1, 0, kInt, fOnOff }, { "RESONANCE", 0, 100, 0, kLin, fPct }, kNoCtl, kNoCtl } },
    { "REV STEPPER", kFamTime, 2, 0, 0, {
        { "DELAY TIME", 0, 7, 2, kInt, fDiv }, { "REPEATS", 2, 8, 4, kInt, fNum }, kNoCtl, kNoCtl, kNoCtl } },
    { "DELAY", kFamSpace, 3, 0, 1, {
        { "TIME", 0, 16, 3, kInt, fMpcTime }, { "FEEDBACK", 0, 100, 40, kLin, fPct }, { "RANGE", 0, 2, 0, kInt, fDRange }, kNoCtl, kNoCtl } },
    { "REVERB", kFamSpace, 3, 0, 1, {
        { "PRE-DELAY", 0, 250, 20, kLin, fMs }, { "DECAY", 0, 100, 50, kLin, fPct }, { "DIFFUSION", 0, 100, 70, kLin, fPct }, kNoCtl, kNoCtl } },
    // alternate versions (the \xC2\xB7 ALT in the name): same sound, with every knob
    { "CHORUS \xC2\xB7 ALT", kFamMod, 3, 1, 0, {
        { "RATE", 0.4f, 3.2f, 0.9f, kLog, fLfo }, { "DEPTH", 0, 100, 55, kLin, fPct }, { "FEEDBACK", 0, 100, 15, kLin, fPct },
        { "MIX", 0, 100, 100, kLin, fPct }, kNoCtl } },
    { "FLANGER \xC2\xB7 ALT", kFamMod, 3, 1, 0, {
        { "RATE", 0.02f, 10.f, 0.25f, kLog, fLfo }, { "DEPTH", 0, 100, 70, kLin, fPct }, { "FEEDBACK", 0, 100, 60, kLin, fPct },
        { "MIX", 0, 100, 100, kLin, fPct }, kNoCtl } },
    { "PHASER \xC2\xB7 ALT", kFamMod, 3, 1, 0, {
        { "SPEED", 0, 10, 3, kInt, fSync }, { "RANGE", 0, 100, 60, kLin, fPct }, { "FEEDBACK", 0, 100, 50, kLin, fPct },
        { "MIX", 0, 100, 100, kLin, fPct }, kNoCtl } },
    { "COMB FILTER \xC2\xB7 ALT", kFamMod, 3, 1, 0, {
        { "SPEED", 0, 10, 5, kInt, fSync }, { "RANGE", 0, 100, 60, kLin, fPct }, { "FEEDBACK", 0, 100, 60, kLin, fPct },
        { "MIX", 0, 100, 100, kLin, fPct }, kNoCtl } },
    { "RING MOD \xC2\xB7 ALT", kFamMod, 3, 0, 0, {
        { "FREQ", 20, 2000, 120, kLog, fHz }, { "DEPTH", 0, 100, 100, kLin, fPct }, { "MIX", 0, 100, 100, kLin, fPct }, kNoCtl, kNoCtl } },
    { "CRUSHER \xC2\xB7 ALT", kFamTexture, 3, 0, 0, {
        { "BITS", 0, 100, 50, kLin, fBits }, { "DECIMATE", 0, 100, 30, kLin, fPct }, { "MIX", 0, 100, 100, kLin, fPct }, kNoCtl, kNoCtl } },
    // the Valens effects: one knob each, 0-100 (Valens' own ranges and curves)
    { "STUTTER", kFamTime, 1, 0, 0, {
        { "STUTTER", 0, 100, 50, kInt, fValens }, kNoCtl, kNoCtl, kNoCtl, kNoCtl } },
    { "REVERSE", kFamTime, 1, 0, 0, {
        { "REVERSE", 0, 100, 50, kInt, fValens }, kNoCtl, kNoCtl, kNoCtl, kNoCtl } },
    { "DISTORTION", kFamTexture, 1, 0, 0, {
        { "DISTORTION", 0, 100, 50, kInt, fValens }, kNoCtl, kNoCtl, kNoCtl, kNoCtl } },
};

static inline int numControls(int e) { return kEffects[e].nFront + kEffects[e].nAdv; }

// ------------------------------------------------------------------------------------------------
// Pads: three pages of 16. Page A pad 16 is KILL; every other pad is an assignable slot.

enum { kNumPages = 3, kPadsPerPage = 16, kNumPads = kNumPages * kPadsPerPage, kKillPad = -100, kNumSlots = kNumPads };
static inline int slotOfPad(int p) { return p; }
static inline int padOfSlot(int s) { return s; }

enum MoveId {
    kMvDryKill = 0, kMvThrow, kMvFreeze, kMvGrab8, kMvGrab4, kMvGrab2, kMvGrab1, kMvTiming, kMvAllOff, kMvKill,
    kNumMoves
};
static const char* const kMoveNames[kNumMoves] = {
    "DRY KILL", "THROW", "FREEZE", "GRAB 1/8", "GRAB 1/4", "GRAB 1/2", "GRAB 1 BAR", "TIMING", "ALL OFF", "KILL"
};
static inline bool moveIsHold(int m) { return (m >= kMvDryKill && m <= kMvGrab1) || m == kMvKill; }

// A slot's type number, as saved in projects and sets: the first 35 effects, then the moves, then EMPTY,
// then effects added later (Stutter, Reverse, Distortion). Adding effects at the end this way never
// changes the number of a move or of EMPTY, so older projects load exactly as they were saved.
enum { kTypeEmpty = kNumLegacyEffects + kNumMoves, kNumTypes = kTypeEmpty + 1 + (kNumEffects - kNumLegacyEffects) };
static constexpr int typeOfMove(int m) { return kNumLegacyEffects + m; }
static constexpr int typeOfEffect(int e) { return e < kNumLegacyEffects ? e : e + kNumMoves + 1; }
static constexpr int effectOfType(int t) { return t < kNumLegacyEffects ? t : (t > kTypeEmpty && t < kNumTypes ? t - kNumMoves - 1 : -1); }
static constexpr int moveOfType(int t) { return (t >= kNumLegacyEffects && t < kTypeEmpty) ? t - kNumLegacyEffects : -1; }

// ------------------------------------------------------------------------------------------------
// Parameters: identical slots, then globals, pad buttons, then read-only outputs

enum { kSlotFields = 10, kKnobsPerSlot = 5 };
enum SlotField { F_TYPE = 0, F_LATCH, F_REL, F_SNAP, F_K1, F_K2, F_K3, F_K4, F_K5, F_VOL };

static const int kDefaultTypes[kNumSlots] = {
    // bank A: the default layout (12 effects, then Dry Kill, Throw, Grab 1/4 and Kill)
    kFxLoopGrab, kFxDjLooper, kFxFilterDrive, kFxIsolator,
    kFxTapeEcho, kFxEchoVerb, kFxWaveVerb, kFxStopper,
    kFxBackSpin, kFxCoil, kFxScatter, kFxVinyl,
    typeOfMove(kMvDryKill), typeOfMove(kMvThrow), typeOfMove(kMvGrab4), typeOfMove(kMvKill),
    // banks B and C: empty, for your own effects, samples and beats
    kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty,
    kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty,
    kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty,
    kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty, kTypeEmpty
};

static inline int slotParam(int s, int field) { return s * kSlotFields + field; }
static inline int slotOf(int param) { return param / kSlotFields; }
static inline int fieldOf(int param) { return param % kSlotFields; }

enum { kNumJobs = 8 };

enum ParamId {
    pOut = kNumSlots * kSlotFields,
    pLimit, pTiming, pTail, pThrowTime, pThrowFb, pFreezeLen, pFreezeSpeed, pSelect,
    pCtrl0, pCtrl7 = pCtrl0 + kNumJobs - 1,
    pPad0,
    // outputs (DSP -> UI)
    oEff0 = pPad0 + kNumPads,
    oSmpPos0 = oEff0 + kNumSlots,
    oMoves = oSmpPos0 + kNumSlots, oBpm, oFocus, oFocusPhase, oPicked,
    oJob0, oJobSeq = oJob0 + kNumJobs, oLearned, oLearnSeq, oMidiSeq, oMidiState, oPadHit, oPadHitSeq, oLevel, oKill, oGrabPhase,
    oLearnDup, oLearnDupSeq, oMidiLast, oMidiLastSeq, oRepeatSeq, oVoices, oKnobTypes,
    oBeatReadyLo, oBeatReadyHi, oBeatPlaying, oBeatBpm,
    kParamCount
};

static inline bool isOutputParam(int i) { return i >= oEff0; }
static inline bool isSlotParam(int i) { return i < pOut; }
static inline bool isSlotKnob(int i) { return i < pOut && fieldOf(i) >= F_K1 && fieldOf(i) <= F_K5; }

// sample pad volume: 0.8 is unity, 1.0 is about +3.9 dB
static constexpr float kPadVolDefault = 0.8f;
static inline float padVolGain(float v) { const float g = v / 0.8f; return g * g; }

// ------------------------------------------------------------------------------------------------
// MIDI learn slots: 0-47 pads (by position, page by page), 48-55 the eight knobs

enum { kLearnPad0 = 0, kLearnKnob0 = kNumPads, kNumLearn = kNumPads + kNumJobs };

// all eight knobs follow the selected pad's controls (no output knob on the controller)
enum JobId { kJobCtl1 = 0, kJobCtl2, kJobCtl3, kJobCtl4, kJobCtl5, kJobCtl6, kJobCtl7, kJobCtl8 };
static const char* const kJobNames[kNumJobs] = { "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4", "KNOB 5", "KNOB 6", "KNOB 7", "KNOB 8" };

// knob encodings: absolute, or one of the three common relative schemes
// ENDLESS: an endless knob that sends 0-127 like a normal knob (M-VAVE SMC-Pad and others). Only the
// change between messages counts, so every pad's knob carries on from where that pad left it.
enum KnobMode { kKnobAuto = 0, kKnobAbs, kKnobRel1, kKnobRel2, kKnobRel3, kKnobEndAbs, kNumKnobModes };
static const char* const kKnobModeNames[kNumKnobModes] = { "AUTO", "ABSOLUTE", "REL 1/127", "REL 63/65", "REL 1/65", "ENDLESS" };

// Pads count as pressed for any value above zero, so the softest hit through FL's
// "Link to controller" (which passes velocity along) still triggers.
static constexpr float kPadOnThreshold = 0.004f;
static inline bool padOn(float v) { return v > kPadOnThreshold; }

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// note map: the n-th note in a row lands on a pad position, page by page, top or bottom row first
static inline int notePad(int idx, bool bottomFirst)
{
    if (!bottomFirst) return idx;
    const int page = idx / kPadsPerPage, i = idx % kPadsPerPage;
    return page * kPadsPerPage + (3 - i / 4) * 4 + i % 4;
}

// ------------------------------------------------------------------------------------------------
// Control value mapping (slot knobs are stored 0..1 and mean different things per effect)

static inline float ctlToNorm(const CtlSpec& c, float v)
{
    v = clampf(v, c.min, c.max);
    if (c.scale == kLog) return std::log(v / c.min) / std::log(c.max / c.min);
    return (c.max > c.min) ? (v - c.min) / (c.max - c.min) : 0.f;
}

static inline float ctlFromNorm(const CtlSpec& c, float n)
{
    n = clampf(n, 0.f, 1.f);
    float v = (c.scale == kLog) ? c.min * std::pow(c.max / c.min, n) : c.min + n * (c.max - c.min);
    if (c.scale == kInt || c.scale == kBool) v = std::round(v);
    return v;
}

static inline float ctlSnap(const CtlSpec& c, float n)
{
    if (c.scale != kInt && c.scale != kBool) return clampf(n, 0.f, 1.f);
    return ctlToNorm(c, ctlFromNorm(c, n));
}

static inline float defaultNorm(int e, int k) { return k < numControls(e) ? ctlToNorm(kEffects[e].c[k], kEffects[e].c[k].def) : 0.f; }
static inline bool ctlBipolar(int e, int k)
{
    return (e == kFxIsolator && k < 3) || (e == kFxDjLooper && k == 1) || (e == kFxCloudDelay && k == 1);
}

static inline int typeOf(const float* params, int s)
{
    const int t = (int)std::lround(params[slotParam(s, F_TYPE)]);
    return t < 0 ? 0 : (t >= kNumTypes ? kNumTypes - 1 : t);
}
// the effect on a slot, or -1 when the slot holds a move
static inline int effectOf(const float* params, int s) { return effectOfType(typeOf(params, s)); }
static inline int moveOf(const float* params, int s) { return moveOfType(typeOf(params, s)); }
static inline bool isEmpty(const float* params, int s) { return typeOf(params, s) == kTypeEmpty; }
static inline bool latchOf(const float* params, int s) { return params[slotParam(s, F_LATCH)] > 0.5f; }

// ------------------------------------------------------------------------------------------------
// Global parameter ranges (most are stored 0..1)

static inline float paramDefault(int i)
{
    if (isSlotParam(i)) {
        const int s = slotOf(i), f = fieldOf(i), t = kDefaultTypes[s];
        if (f == F_TYPE) return (float)t;
        if (f == F_VOL) return 0.8f;
        if (f == F_LATCH) return 0.f;
        if (f == F_REL || f == F_SNAP) return 0.f;
        const int e = effectOfType(t);
        if (e < 0) return 0.f;
        return defaultNorm(e, f - F_K1);
    }
    switch (i) {
    case pOut: return 0.f;
    case pLimit: return 1.f;
    case pTiming: return 0.f;          // RAW
    case pTail: return 0.8f;
    case pThrowTime: return 9.f;       // 1/4 dotted
    case pThrowFb: return 0.7f;
    case pFreezeLen: return 0.35f;
    case pFreezeSpeed: return 1.f;
    case pSelect: return 2.f;
    default: return 0.f;
    }
}

// normalized (0..1) view of any input parameter, for knobs and controller jobs
static inline float normOfParam(int i, float v)
{
    if (i == pOut) return clampf((v + 60.f) / 66.f, 0.f, 1.f);
    if (i == pThrowTime) return clampf(v / 12.f, 0.f, 1.f);
    return clampf(v, 0.f, 1.f);
}
static inline float valueOfNorm(int i, float n)
{
    n = clampf(n, 0.f, 1.f);
    if (i == pOut) return -60.f + 66.f * n;
    if (i == pThrowTime) return std::round(n * 12.f);
    return n;
}

static inline float freezeMs(float n) { return 20.f * std::pow(400.f / 20.f, clampf(n, 0.f, 1.f)); }
static inline float freezeSpeed(float n) { return clampf(n, 0.f, 1.f) * 2.f - 1.f; }

// ------------------------------------------------------------------------------------------------
// Fewer knobs than controls: on a controller with N knobs, knobs 1..N run an effect's first N controls
// (the controls are listed most important first). Each control past N has a fallback: it FOLLOWS a
// control that has a knob (turning that knob moves it too, by amt of the same distance, up or down
// from wherever it is), or it stays FIXED where you set it with the mouse. Only the controller does
// this: the sound is always just the pad's saved values, so a project sounds the same on any computer.

struct Fold { int host; float amt; };      // host = the control it follows, -1 = fixed

static inline Fold foldRule(int e, int c)
{
    switch (e) {
    case kFxTapeEcho:   if (c == 3) return { 1, 0.5f };     // WOW rides on FEEDBACK: more repeats, more wobble
                        if (c == 4) return { 1, -0.4f };    // TONE too, the other way: more repeats, darker
                        break;
    case kFxEchoVerb:   if (c == 3) return { 1, 0.5f };     // DAMP rides on FEEDBACK: longer tails get darker
                        break;
    case kFxWaveVerb:   if (c == 3) return { 0, 0.5f };     // PRE DELAY rides on DECAY: a bigger room starts later
                        break;
    case kFxCloudDelay: if (c == 4) return { 0, 0.7f };     // CLOUDY rides on WINDOW
                        break;
    case kFxCoil:       if (c == 3) return { 0, 0.8f };     // FLT MOD and AMP MOD ride on DEPTH
                        if (c == 4) return { 0, 0.6f };
                        break;
    default: break;
    }
    return { -1, 0.f };     // everything else (MIX, TYPE, SPREAD, SIZE, the splits...) stays where it's set
}

// the knob (control index < n) that moves control c, and by how much; host -1 = nothing moves it
static inline Fold foldResolve(int e, int c, int n)
{
    if (c < n) return { c, 1.f };
    Fold f = foldRule(e, c);
    float a = f.amt;
    int guard = 0;
    while (f.host >= n && guard++ < 5) {
        const Fold g = foldRule(e, f.host);
        if (g.host < 0) return { -1, 0.f };
        a *= g.amt;
        f.host = g.host;
    }
    if (f.host < 0) return { -1, 0.f };
    return { f.host, a };
}

// how many knobs the controller has: set by hand (1-5, 5 = five or more), or counted from the knobs
// linked in Dewitt (the highest one linked); with none linked, all eight (nothing folds)
static inline int knobCount(const int* map, int setByHand)
{
    if (setByHand >= 1 && setByHand <= 5) return setByHand >= 5 ? kNumJobs : setByHand;
    int hi = 0;
    for (int j = 0; j < kNumJobs; ++j) if (map[kLearnKnob0 + j] >= 0) hi = j + 1;
    return hi == 0 ? kNumJobs : hi;
}

// The parameter a controller knob drives right now (or -1): knobs 1-5 the selected pad's controls
static inline int jobTarget(int job, int focusSlot, const float* params, bool focusHasSample)
{
    if (job < 0 || job >= kKnobsPerSlot || focusSlot < 0 || focusSlot >= kNumSlots) return -1;
    if (focusHasSample) return job == kJobCtl1 ? slotParam(focusSlot, F_VOL) : -1;
    const int t = typeOf(params, focusSlot);
    if (t == kTypeEmpty) return -1;
    if (effectOfType(t) >= 0) return job < numControls(effectOfType(t)) ? slotParam(focusSlot, F_K1 + job) : -1;
    switch (moveOfType(t)) {
    case kMvDryKill: return job == 0 ? pTail : -1;
    case kMvThrow: return job == 0 ? pThrowTime : (job == 1 ? pThrowFb : -1);
    case kMvFreeze: return job == 0 ? pFreezeLen : (job == 1 ? pFreezeSpeed : -1);
    default: return -1;
    }
}

// snap a job's normalized value onto the target's steps
static inline float jobSnap(int target, float n, const float* params)
{
    if (target < 0) return n;
    if (isSlotKnob(target)) {
        const int e = effectOf(params, slotOf(target));
        return e >= 0 ? ctlSnap(kEffects[e].c[fieldOf(target) - F_K1], n) : clampf(n, 0.f, 1.f);
    }
    if (target == pThrowTime) return std::round(clampf(n, 0.f, 1.f) * 12.f) / 12.f;
    return clampf(n, 0.f, 1.f);
}

// ------------------------------------------------------------------------------------------------
// Text

static const char* const kDelayTimeNames[13] = {
    "1/32", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1/1"
};
static const double kDelayTimeBeats[13] = {
    0.125, 0.25, 1.0/3.0, 0.375, 0.5, 2.0/3.0, 0.75, 1.0, 4.0/3.0, 1.5, 2.0, 3.0, 4.0
};
static const char* const kLoopLenNames[6] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR" };
static const double kLoopLenBeats[6] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
static const char* const kSyncNames[11] = { "2 BARS", "1 BAR", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" };
static const double kSyncBeats[11] = { 8.0, 4.0, 2.0, 1.0, 2.0/3.0, 0.5, 1.0/3.0, 0.25, 1.0/6.0, 0.125, 0.0625 };
static const char* const kDivNames[8] = { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" };
static const double kDivBeats[8] = { 1.0, 2.0/3.0, 0.5, 1.0/3.0, 0.25, 1.0/6.0, 0.125, 0.0625 };
static const char* const kFiltNames[3] = { "LOW PASS", "HIGH PASS", "BAND PASS" };
static const char* const kColorModes[6] = { "CASSETTE", "FLUTTER", "TUBE AMP", "VINYL", "SATURATION", "RADIO" };
// the Delay effect's 17 times, longest first
static const char* const kMpcTimeNames[17] = { "1/1", "1/2", "1/4D", "1/4", "1/4T", "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T",
                                               "1/32D", "1/32", "1/32T", "1/64D", "1/64", "1/64T" };
static const double kMpcTimeBeats[17] = { 4.0, 2.0, 1.5, 1.0, 2.0/3.0, 0.75, 0.5, 1.0/3.0, 0.375, 0.25, 1.0/6.0,
                                          0.1875, 0.125, 1.0/12.0, 0.09375, 0.0625, 1.0/24.0 };
static const char* const kScatNames[4] = { "STUTTER", "REVERSE", "SHUFFLE", "HALF" };

static inline float isoGain(float n)
{
    if (n <= 0.5f) { const float t = n * 2.f; return t * t; }
    return std::pow(10.f, ((n - 0.5f) * 12.f) / 20.f);
}

static inline float lofiBits(float crush) { return 24.f - clampf(crush, 0.f, 100.f) * 0.22f; }

static inline void formatHz(char* buf, size_t sz, float hz)
{
    if (hz >= 1000.f) std::snprintf(buf, sz, "%.1f kHz", hz / 1000.f);
    else              std::snprintf(buf, sz, "%d Hz", (int)std::lround(hz));
}

static inline void formatFmt(Fmt fmt, float v, char* buf, size_t sz)
{
    switch (fmt)
    {
    case fBipolarDb: {
        if (v <= 0.005f) { std::snprintf(buf, sz, "KILL"); break; }
        const float db = 20.f * std::log10(isoGain(v));
        if (std::fabs(db) < 0.05f) std::snprintf(buf, sz, "0.0 dB");
        else std::snprintf(buf, sz, "%+.1f dB", db);
        break;
    }
    case fHz:      formatHz(buf, sz, v); break;
    case fPct:     std::snprintf(buf, sz, "%d%%", (int)std::lround(v)); break;
    case fTime:    std::snprintf(buf, sz, "%s", kDelayTimeNames[(int)clampf(std::round(v), 0, 12)]); break;
    case fLen:     std::snprintf(buf, sz, "%s", kLoopLenNames[(int)clampf(std::round(v), 0, 5)]); break;
    case fRetrig:  { const int r = (int)std::lround(v); if (r == 0) std::snprintf(buf, sz, "OFF"); else std::snprintf(buf, sz, "%d/8", r); break; }
    case fMs:      std::snprintf(buf, sz, "%d ms", (int)std::lround(v)); break;
    case fSpeed:   std::snprintf(buf, sz, "%+.2f", std::fabs(v) < 0.005f ? 0.f : v); break;
    case fNum:     std::snprintf(buf, sz, "%d", (int)std::lround(v)); break;
    case fSync:    std::snprintf(buf, sz, "%s", kSyncNames[(int)clampf(std::round(v), 0, 10)]); break;
    case fDiv:     std::snprintf(buf, sz, "%s", kDivNames[(int)clampf(std::round(v), 0, 7)]); break;
    case fBits:    std::snprintf(buf, sz, "%.1f bit", lofiBits(v)); break;
    case fFiltType: std::snprintf(buf, sz, "%s", kFiltNames[(int)clampf(std::round(v), 0, 2)]); break;
    case fSec:     std::snprintf(buf, sz, v < 10.f ? "%.1f s" : "%.0f s", v); break;
    case fSemi:    { const int st = (int)std::lround(v); if (st == 0) std::snprintf(buf, sz, "0 st"); else std::snprintf(buf, sz, "%+d st", st); break; }
    case fScat:    std::snprintf(buf, sz, "%s", kScatNames[(int)clampf(std::round(v), 0, 3)]); break;
    case fSpread:  std::snprintf(buf, sz, "%s", v > 0.5f ? "PAN" : "CENTER"); break;
    case fNorm:    std::snprintf(buf, sz, "%d%%", (int)std::lround(v * 100.f)); break;
    case fLfo:     std::snprintf(buf, sz, v < 1.f ? "%.2f Hz" : "%.1f Hz", v); break;
    case fHalf:    { static const char* const h[3] = { "x1.5", "x2", "x4" }; std::snprintf(buf, sz, "%s", h[(int)clampf(std::round(v), 0, 2)]); break; }
    case fColorMode: std::snprintf(buf, sz, "%s", kColorModes[(int)clampf(std::round(v), 0, 5)]); break;
    case fOnOff:   std::snprintf(buf, sz, "%s", v > 0.5f ? "ON" : "OFF"); break;
    case fDRange:  { static const char* const r[3] = { "NORMAL", "X-FEEDBACK", "PING-PONG" }; std::snprintf(buf, sz, "%s", r[(int)clampf(std::round(v), 0, 2)]); break; }
    case fDens:    std::snprintf(buf, sz, "%.1f/s", v); break;
    case fBitsMpc: std::snprintf(buf, sz, "%.2f", lofiBits(v)); break;
    case fMpcTime: std::snprintf(buf, sz, "%s", kMpcTimeNames[(int)clampf(std::round(v), 0, 16)]); break;
    case fTapeLen: std::snprintf(buf, sz, "%d ms", (int)std::lround(djTapeMs(v))); break;
    case fValens:  { const int n = (int)std::lround(v); if (n <= 0) std::snprintf(buf, sz, "OFF"); else std::snprintf(buf, sz, "%d%%", n); break; }
    }
}

// Note names the FL Studio way (middle C = C5, MIDI 0 = C0)
static inline void formatNote(int n, char* buf, size_t sz)
{
    static const char* const names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    n = n < 0 ? 0 : (n > 127 ? 127 : n);
    std::snprintf(buf, sz, "%s%d", names[n % 12], n / 12);
}

// MIDI map codes: -1 none, 0x10000 | ch << 8 | note, 0x20000 | ch << 8 | cc
static inline int midiCode(bool cc, int ch, int num) { return (cc ? 0x20000 : 0x10000) | ((ch & 15) << 8) | (num & 0x7f); }
static inline void formatMidiCode(int code, char* buf, size_t sz)
{
    if (code < 0) { std::snprintf(buf, sz, "Not mapped"); return; }
    const int ch = ((code >> 8) & 15) + 1, num = code & 0x7f;
    if (code & 0x20000) { std::snprintf(buf, sz, "CC %d \xC2\xB7 channel %d", num, ch); return; }
    char nb[8]; formatNote(num, nb, sizeof(nb));
    std::snprintf(buf, sz, "Note %s (%d) \xC2\xB7 channel %d", nb, num, ch);
}

} // namespace yancey
