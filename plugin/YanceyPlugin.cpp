// Dewitt: a live beat-set effector for a master channel.
// Up to three pages of 16 pads. Every pad plays an effect (hold by default, latch optional), a
// performance move or a one-shot sample that plays clean on top. Page A pad 16 is KILL.
// Effects stack freely: each pressed pad borrows a voice of its effect from a pool, and the voices
// always run in one fixed chain order, so the result never depends on press order.
#include "DistrhoPlugin.hpp"
#include "YanceyParams.hpp"
#include "YanceyDSP.hpp"
#include "ValensFx.hpp"
#include "YanceyPlatform.hpp"
#include "YanceySamples.hpp"
#include "YanceyShared.hpp"
#include "YanceyBeats.hpp"
#include "YanceyDecode.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
# include <xmmintrin.h>
# define YANCEY_HAVE_SSE 1
#endif

START_NAMESPACE_DISTRHO

using namespace yancey;

static int parseInts(const char* s, int* out, int n, int fallback)
{
    for (int i = 0; i < n; ++i) out[i] = fallback;
    if (s == nullptr) return 0;
    int idx = 0;
    const char* p = s;
    while (*p != '\0' && idx < n) {
        char* end = nullptr;
        const long v = std::strtol(p, &end, 10);
        if (end == p) { ++p; continue; }
        out[idx++] = (int)v;
        p = end;
        while (*p == ',' || *p == ' ') ++p;
    }
    return idx;
}

// ------------------------------------------------------------------------------------------------
// The effect pool: two instances of every effect, so two pads can run the same effect at once

enum { kVoicesPerFx = 2 };

struct FxPool {
    BpmLooper loop[2];
    TapeLooper dj[2]; VinylDeck vin[2];        // Dewitt: our own (a tape-loop DJ Looper, a record-deck Vinyl Sim)
    FilterDrive flt[2]; Isolator iso[2];
    TapeEcho tape[2]; EchoVerb ev[2]; WaveVerb wv[2]; CloudDelay cloud[2];
    Stopper stop[2]; BackSpin spin[2]; Coil coil[2]; Scatter scat[2];
    ColorFX crush[2]; ModFX phaser[2]; ModFX chorus[2]; ModFX flanger[2]; ColorFX ring[2]; ColorFX comb[2];
    HalfSpeed half[2]; LfoFilter lpf[2], hpf[2], bpf[2]; ColorBox color[2]; Granulator gran[2]; BeatRepeat brep[2]; RevStepper rstep[2];
    MpcDelay mdel[2]; MpcReverb mrev[2];
    ModFX chorusY[2], flangerY[2], phaserY[2]; ColorFX combY[2], ringY[2], crushY[2];
    // the Valens effects
    ValensGrid stut[2] = { ValensGrid(false), ValensGrid(false) };
    ValensGrid rev[2] = { ValensGrid(true), ValensGrid(true) };
    ValensDistortion dist[2];
    Smear smear[2];
    PitchShift pitch[2];

    void prepare(float fs) {
        for (int k = 0; k < 2; ++k) {
            loop[k].prepare(fs); dj[k].prepare(fs); flt[k].prepare(fs); iso[k].prepare(fs);
            tape[k].prepare(fs); ev[k].prepare(fs); wv[k].prepare(fs); cloud[k].prepare(fs);
            stop[k].prepare(fs); spin[k].prepare(fs); coil[k].prepare(fs); scat[k].prepare(fs);
            vin[k].prepare(fs); crush[k].prepare(fs); phaser[k].prepare(fs); chorus[k].prepare(fs);
            flanger[k].prepare(fs); ring[k].prepare(fs); comb[k].prepare(fs);
            half[k].prepare(fs); lpf[k].prepare(fs); hpf[k].prepare(fs); bpf[k].prepare(fs); color[k].prepare(fs);
            gran[k].prepare(fs); brep[k].prepare(fs); rstep[k].prepare(fs); mdel[k].prepare(fs); mrev[k].prepare(fs);
            chorusY[k].prepare(fs); flangerY[k].prepare(fs); phaserY[k].prepare(fs); combY[k].prepare(fs); ringY[k].prepare(fs); crushY[k].prepare(fs);
            stut[k].prepare(fs); rev[k].prepare(fs); dist[k].prepare(fs);
            smear[k].prepare(fs); pitch[k].prepare(fs);
        }
    }
    void block(int e, int k, const BlockCtx& ctx, const float* v) {
        auto I = [](float x) { return (int)std::lround(x); };
        switch (e) {
        case kFxLoopGrab:    loop[k].block(ctx, I(v[0]), I(v[1]), v[2]); break;
        case kFxDjLooper:    dj[k].block(ctx, djTapeMs(v[0]), v[1], 100.f, 30.f); break;    // MIX and GLIDE fixed
        case kFxFilterDrive: flt[k].block(ctx, v[0], v[1], v[2], I(v[3]), v[4]); break;
        case kFxIsolator:    iso[k].block(ctx, v[0], v[1], v[2], v[3], v[4]); break;
        case kFxTapeEcho:    tape[k].block(ctx, I(v[0]), v[1], v[2], v[3], v[4]); break;
        case kFxEchoVerb:    ev[k].block(ctx, I(v[0]), v[1], v[2], v[3], I(v[4])); break;
        case kFxWaveVerb:    wv[k].block(ctx, v[0], v[1], v[2], v[3], v[4]); break;
        case kFxCloudDelay:  cloud[k].block(ctx, v[0], v[1], v[2], v[3], v[4]); break;
        case kFxStopper:     stop[k].block(ctx, v[0], I(v[1]), v[2]); break;
        case kFxBackSpin:    spin[k].block(ctx, I(v[0]), v[1], v[2]); break;
        case kFxCoil:        coil[k].block(ctx, v[0], I(v[1]), v[2], v[3], v[4]); break;
        case kFxScatter:     scat[k].block(ctx, I(v[0]), v[1], v[2], I(v[3])); break;
        case kFxVinyl:       vin[k].block(ctx, v[0], v[1], v[2]); break;
        // the original's knobs only: anything it doesn't expose is fixed
        case kFxCrusher:     { const float c[3] = { v[0], v[1], 100.f }; crush[k].block(ctx, 1, c); break; }
        case kFxPhaser:      { const float c[4] = { v[1], v[2], v[0], 100.f }; phaser[k].block(ctx, 2, c); break; }
        case kFxChorus:      { const float c[4] = { v[0], v[1], v[2], 100.f }; chorus[k].block(ctx, 1, c); break; }
        case kFxFlanger:     { const float c[4] = { v[0], v[1], v[2], 100.f }; flanger[k].block(ctx, 0, c); break; }
        case kFxRingMod:     { const float c[3] = { v[0], 100.f, 100.f }; ring[k].block(ctx, 0, c); break; }
        case kFxComb:        { const float c[4] = { v[0], 60.f, 60.f, 100.f }; comb[k].block(ctx, 2, c); break; }
        case kFxHalfSpeed:   half[k].block(ctx, I(v[0]), v[1]); break;
        case kFxLpf:         lpf[k].block(ctx, 0, v[0], I(v[1]), v[2]); break;
        case kFxHpf:         hpf[k].block(ctx, 1, v[0], I(v[1]), v[2]); break;
        case kFxBpf:         bpf[k].block(ctx, 2, v[0], I(v[1]), v[2]); break;
        case kFxColor:       color[k].block(ctx, I(v[0])); break;
        case kFxGranulator:  gran[k].block(ctx, v[0], v[1], v[2]); break;
        case kFxBeatRepeat:  brep[k].block(ctx, I(v[0]), I(v[1]), v[2]); break;
        case kFxRevStepper:  rstep[k].block(ctx, I(v[0]), I(v[1])); break;
        case kFxDelay:       mdel[k].block(ctx, I(v[0]), v[1], I(v[2])); break;
        case kFxReverb:      mrev[k].block(ctx, v[0], v[1], v[2]); break;
        // Yancey's versions: every knob live, mapped exactly as in Yancey
        case kFxChorusY:     { const float c[4] = { v[0], v[1], v[2], v[3] }; chorusY[k].block(ctx, 1, c); break; }
        case kFxFlangerY:    { const float c[4] = { v[0], v[1], v[2], v[3] }; flangerY[k].block(ctx, 0, c); break; }
        case kFxPhaserY:     phaserY[k].block(ctx, 2, v); break;
        case kFxCombY:       { const float c[4] = { v[0], v[1], v[2], v[3] }; combY[k].block(ctx, 2, c); break; }
        case kFxRingModY:    { const float c[3] = { v[0], v[1], v[2] }; ringY[k].block(ctx, 0, c); break; }
        case kFxCrusherY:    crushY[k].block(ctx, 1, v); break;
        // Valens: knob 1, and the pad's pattern (kept in the pad's 5th knob field) in v[4]
        case kFxStutterV:    stut[k].block(ctx, v[0], v[4]); break;
        case kFxReverseV:    rev[k].block(ctx, v[0], v[4]); break;
        case kFxDistortionV: dist[k].block(ctx, v[0]); break;
        case kFxSmear:       smear[k].block(ctx, v[0], v[1], v[2], v[3], v[4]); break;
        case kFxPitch:       pitch[k].block(ctx, v[0], v[1], v[2], v[3], v[4]); break;
        default: break;
        }
    }
    inline void process(int e, int k, float& l, float& r, bool on, const Clock& ck) {
        switch (e) {
        case kFxLoopGrab:    loop[k].canStart = ck.tick16; loop[k].process(l, r, on); break;
        case kFxFilterDrive: flt[k].process(l, r, on); break;
        case kFxIsolator:    iso[k].process(l, r, on); break;
        case kFxTapeEcho:    tape[k].idleCheck(); tape[k].process(l, r, on); break;
        case kFxEchoVerb:    ev[k].idleCheck(); ev[k].process(l, r, on); break;
        case kFxWaveVerb:    wv[k].idleCheck(); wv[k].process(l, r, on); break;
        case kFxCloudDelay:  cloud[k].idleCheck(); cloud[k].process(l, r, on); break;
        case kFxStopper:     stop[k].process(l, r, on); break;
        case kFxBackSpin:    spin[k].process(l, r, on, ck); break;
        case kFxCoil:        coil[k].process(l, r, on, ck); break;
        case kFxScatter:     scat[k].process(l, r, on, ck); break;
        case kFxDjLooper:    dj[k].process(l, r, on); break;
        case kFxVinyl:       vin[k].process(l, r, on); break;
        case kFxCrusher:     crush[k].process(l, r, on); break;
        case kFxPhaser:      phaser[k].process(l, r, on); break;
        case kFxChorus:      chorus[k].process(l, r, on); break;
        case kFxFlanger:     flanger[k].process(l, r, on); break;
        case kFxRingMod:     ring[k].process(l, r, on); break;
        case kFxComb:        comb[k].process(l, r, on); break;
        case kFxHalfSpeed:   half[k].process(l, r, on); break;
        case kFxLpf:         lpf[k].process(l, r, on, ck); break;
        case kFxHpf:         hpf[k].process(l, r, on, ck); break;
        case kFxBpf:         bpf[k].process(l, r, on, ck); break;
        case kFxColor:       color[k].process(l, r, on); break;
        case kFxGranulator:  gran[k].process(l, r, on); break;
        case kFxBeatRepeat:  brep[k].canStart = ck.tick16; brep[k].process(l, r, on); break;
        case kFxRevStepper:  rstep[k].process(l, r, on); break;
        case kFxDelay:       mdel[k].process(l, r, on); break;
        case kFxReverb:      mrev[k].idleCheck(); mrev[k].process(l, r, on); break;
        case kFxChorusY:     chorusY[k].process(l, r, on); break;
        case kFxFlangerY:    flangerY[k].process(l, r, on); break;
        case kFxPhaserY:     phaserY[k].process(l, r, on); break;
        case kFxCombY:       combY[k].process(l, r, on); break;
        case kFxRingModY:    ringY[k].process(l, r, on); break;
        case kFxCrusherY:    crushY[k].process(l, r, on); break;
        case kFxStutterV:    stut[k].process(l, r, on, ck); break;
        case kFxReverseV:    rev[k].process(l, r, on, ck); break;
        case kFxDistortionV: dist[k].process(l, r, on); break;
        case kFxSmear:       smear[k].idleCheck(); smear[k].process(l, r, on); break;
        case kFxPitch:       pitch[k].process(l, r, on); break;
        default: break;
        }
    }
    // wipe an effect's memory (Cut release): no allocation, safe on the audio thread
    void clear(int e, int k) {
        switch (e) {
        case kFxTapeEcho:   tape[k].clear(); break;
        case kFxEchoVerb:   ev[k].clear(); break;
        case kFxWaveVerb:   wv[k].clear(); break;
        case kFxCloudDelay: cloud[k].clear(); break;
        case kFxVinyl:      if (!vin[k].warm) vin[k].clear(); break;      // a listening vinyl sim keeps its state
        case kFxDjLooper:   dj[k].unfreeze(); break;      // keep what it has been listening to
        case kFxCrusher:    crush[k].clear(); break;
        case kFxRingMod:    ring[k].clear(); break;
        case kFxComb:       comb[k].clear(); break;
        case kFxPhaser:     phaser[k].clearStates(); break;
        case kFxChorus:     chorus[k].clearStates(); break;
        case kFxFlanger:    flanger[k].clearStates(); break;
        case kFxHalfSpeed:  half[k].clear(); break;
        case kFxLpf:        lpf[k].clear(); break;
        case kFxHpf:        hpf[k].clear(); break;
        case kFxBpf:        bpf[k].clear(); break;
        case kFxColor:      color[k].clear(); break;
        case kFxGranulator: gran[k].clear(); break;
        case kFxBeatRepeat: brep[k].clear(); break;
        case kFxRevStepper: rstep[k].clear(); break;
        case kFxDelay:      mdel[k].clear(); break;
        case kFxReverb:     mrev[k].clear(); break;
        case kFxChorusY:    chorusY[k].clearStates(); break;
        case kFxFlangerY:   flangerY[k].clearStates(); break;
        case kFxPhaserY:    phaserY[k].clearStates(); break;
        case kFxCombY:      combY[k].clear(); break;
        case kFxRingModY:   ringY[k].clear(); break;
        case kFxCrusherY:   crushY[k].clear(); break;
        case kFxStutterV:   stut[k].clear(); break;
        case kFxReverseV:   rev[k].clear(); break;
        case kFxSmear:      smear[k].clear(); break;
        case kFxPitch:      pitch[k].clear(); break;
        default: break;
        }
    }
    static inline bool isSelfMixed(int) { return false; }
    inline void processSelfMixed(int, int, float&, float&, bool, float) {}
    // effects that keep listening while their pad is up (the looper)
    int homeSlot(int e, int k) const {
        if (e == kFxDjLooper) return dj[k].homeSlot;
        if (e == kFxVinyl) return vin[k].homeSlot;
        if (e == kFxStutterV) return stut[k].homeSlot;
        if (e == kFxReverseV) return rev[k].homeSlot;
        return -1;
    }
    static inline bool listens(int e) { return e == kFxDjLooper || e == kFxVinyl || e == kFxStutterV || e == kFxReverseV; }
    inline void idleListen(int e, int k, float l, float r, const Clock& ck) {
        // Stutter and Reverse keep hearing the beat, so a press can use what just played
        if (e == kFxStutterV) { if (stut[k].warm) { float a = l, b = r; stut[k].process(a, b, false, ck); } return; }
        if (e == kFxReverseV) { if (rev[k].warm) { float a = l, b = r; rev[k].process(a, b, false, ck); } return; }
        if (e == kFxDjLooper && dj[k].warm) dj[k].feed(l, r);
        if (e == kFxVinyl && vin[k].warm) { float a = l, b = r; vin[k].process(a, b, false); }
    }
    float phase(int e, int k) const {
        if (e == kFxLoopGrab) return loop[k].phase;
        if (e == kFxDjLooper) return dj[k].phase;
        if (e == kFxStutterV) return stut[k].phase;
        if (e == kFxReverseV) return rev[k].phase;
        if (e == kFxBackSpin) return spin[k].phase;
        if (e == kFxBeatRepeat) return brep[k].phase;
        if (e == kFxRevStepper) return rstep[k].phase;
        return -1.f;
    }
};

// A voice: one pool instance lent to one pad
enum VoiceState { kVoiceFree = 0, kVoiceOn, kVoiceRing, kVoiceCut };
struct Voice {
    int slot = -1, state = kVoiceFree;
    unsigned stamp = 0;
    float cut = 1.f, quietFor = 0.f;
    float amt = 1.f, amtT = 1.f;           // how much of the effect: velocity and pressure
    bool amtSet = false;
    float v[kKnobsPerSlot] = {};
};

// One-shot player: pressing again restarts the sample and the previous play fades out in a few ms.
struct SamplePad {
    std::atomic<SampleData*> data { nullptr };
    std::atomic<bool> stopReq { false };
    // Swapped-out samples wait here until the audio thread has finished a block that started after the swap,
    // so a sample is never freed while a block is still reading it. Never freed on the audio thread.
    std::atomic<uint32_t> swapSeq { 0 }, doneSeq { 0 };
    std::vector<std::pair<SampleData*, uint32_t> > retired;
    double pos = 0.0, fadePos = 0.0;
    bool playing = false, fading = false, fresh = false;
    float fadeGain = 0.f;
    Smooth vol;

    void trigger() {
        if (playing) { fading = true; fadePos = pos; fadeGain = 1.f; }
        else if (!fading) fresh = true;
        pos = 0.0; playing = true;
    }
    void set(SampleData* sd) {
        freeRetired();
        SampleData* old = data.exchange(sd, std::memory_order_acq_rel);
        const uint32_t seq = swapSeq.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (old) retired.push_back(std::make_pair(old, seq));
        stopReq.store(true);
    }
    // audio thread: call at the start of a block (before reading data) and at its end
    uint32_t blockStart() const { return swapSeq.load(std::memory_order_acquire); }
    void blockDone(uint32_t seenAtStart) { doneSeq.store(seenAtStart, std::memory_order_release); }
    void freeRetired() {
        const uint32_t done = doneSeq.load(std::memory_order_acquire);
        for (size_t i = 0; i < retired.size();)
            if ((int32_t)(done - retired[i].second) >= 0) { delete retired[i].first; retired.erase(retired.begin() + (long)i); }
            else ++i;
    }
    ~SamplePad() { delete data.load(); for (auto& o : retired) delete o.first; }
};

// A beat pad points at audio loaded (in the background) from a linked file. The audio lives in the
// plugin's beat store, shared by file, so dragging pads around never reloads or interrupts anything.
struct BeatPad {
    std::atomic<BeatData*> data { nullptr };
    std::atomic<float> bpm { 0.f };
    std::string path;                    // state thread only
};
struct BeatStoreEntry { std::string path; BeatData* data; bool stale; };

static char gSmpKeys[kNumSlots][8];
static char gBeatKeys[kNumSlots][8];

class YanceyPlugin : public Plugin
{
public:
    YanceyPlugin()
        : Plugin(kParamCount, 0, kStateCount)
    {
        for (int k = 0; k < kNumSlots; ++k) { std::snprintf(gSmpKeys[k], sizeof(gSmpKeys[k]), "smp%d", k); std::snprintf(gBeatKeys[k], sizeof(gBeatKeys[k]), "beat%d", k); }
        for (int i = 0; i < kParamCount; ++i) fParams[i] = fLive[i] = paramDefault(i);
        for (int s = 0; s < kNumSlots; ++s) { fLatched[s] = fHeld[s] = fActive[s] = false; fSmpPos[s] = -1.f; }
        for (int p = 0; p < kNumPads; ++p) { fPadMidi[p] = fPadNote[p] = fPadPrev[p] = false; fPadAmt[p] = 1.f; }
        for (int& h : fFollowHeld) h = -1;
        for (int m = 0; m < kNumMoves; ++m) fMoveOn[m] = false;
        for (int i = 0; i < kNumLearn; ++i) fMap[i].store(-1);
        for (int j = 0; j < kNumJobs; ++j) fSavedType[j].store(0);
        for (int i = 0; i < 16 * 128; ++i) fCcPrev[i] = 0;
        for (int j = 0; j < kNumJobs; ++j) {
            fJobTarget[j] = -2; fJobPrev[j] = -1.f; fJobPicked[j] = false; fJobSet[j] = -1.f; fJobAcc[j] = 0.f;
            fJobOut[j] = 0.f; fJobLastVal[j] = -1; fJobAbsLast[j] = -1; fJobDetect[j] = 0; fJobSeenLow[j] = fJobSeenHigh[j] = fJobSeenMid[j] = false;
            fJobLastT[j] = 0; fCtrl[j] = 0.f; fCtrlDirty[j] = false;
        }
        fLearnSlot.store(-1);
        fNoteStart.store(-1);
        fNoteOrder.store(0);
        fKnobMode.store(kKnobAuto);
        fLearnGuard.store(false);
        fAllOffReq.store(false);
        loadComputerSetup();
        prepareAll(getSampleRate());
        yLiveAdd(static_cast<const Plugin*>(this), &fLiveOut);
    }

    ~YanceyPlugin() override
    {
        yLiveRemove(static_cast<const Plugin*>(this));
        { std::lock_guard<std::mutex> g(fLoadMutex); fLoaderQuit = true; }
        fLoadCv.notify_all();
        if (fLoader.joinable()) fLoader.join();
        for (BeatStoreEntry& e : fBeatStore) delete e.data;
    }

    BeatData* storeFind(const std::string& path)
    {
        for (BeatStoreEntry& e : fBeatStore) if (e.path == path) { e.stale = false; return e.data; }
        return nullptr;
    }
    // free audio no pad (and nothing playing) has used for two passes in a row
    void storeCollect()
    {
        for (size_t i = 0; i < fBeatStore.size();) {
            BeatData* d = fBeatStore[i].data;
            bool used = d == fPlayCur.load() || d == fPlayOld.load();
            for (int j = 0; j < kNumSlots && !used; ++j) used = fBeats[j].data.load() == d;
            if (used) { fBeatStore[i].stale = false; ++i; continue; }
            if (fBeatStore[i].stale) { delete d; fBeatStore.erase(fBeatStore.begin() + (long)i); continue; }
            fBeatStore[i].stale = true; ++i;
        }
    }

    // background loader: beats are decoded here so opening a project never freezes the host
    void loaderLoop()
    {
        for (;;) {
            std::pair<int, std::string> job;
            {
                std::unique_lock<std::mutex> lk(fLoadMutex);
                fLoadCv.wait(lk, [this] { return fLoaderQuit || !fLoadQueue.empty(); });
                if (fLoaderQuit) return;
                job = fLoadQueue.front(); fLoadQueue.pop_front();
            }
            {
                std::lock_guard<std::mutex> g(fLoadMutex);
                if (storeFind(job.second) != nullptr) continue;        // already loaded for another pad
            }
            BeatData* bd = nullptr;
            SampleData sd; std::string err;
            if (decodeAudioFile(job.second.c_str(), sd, err, kMaxBeatSeconds)) { bd = new BeatData(); toBeat(sd, *bd); }
            std::lock_guard<std::mutex> g(fLoadMutex);
            if (bd == nullptr) continue;                                 // missing or unreadable: the pads stay silent
            fBeatStore.push_back(BeatStoreEntry { job.second, bd, false });
            for (int j = 0; j < kNumSlots; ++j)
                if (fBeats[j].path == job.second) { fBeats[j].data.store(bd, std::memory_order_release); fBeatReady[j].store(true); }
        }
    }

protected:
    const char* getLabel() const override { return "Dewitt"; }
    const char* getDescription() const override { return "Pad effects for live beat sets: banks of pads with stackable effects, moves, samples and beats."; }
    const char* getMaker() const override { return "slapworx"; }
    const char* getHomePage() const override { return "https://slapworx.net/dewitt/"; }
    const char* getLicense() const override { return "Proprietary"; }
    uint32_t getVersion() const override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const override { return d_cconst('H', 'w', 'r', 'd'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    static void setEnum(Parameter& p, const char* const* names, int count)
    {
        ParameterEnumerationValue* const vals = new ParameterEnumerationValue[count];
        for (int i = 0; i < count; ++i) { vals[i].value = (float)i; vals[i].label = names[i]; }
        p.enumValues.count = (uint8_t)count;
        p.enumValues.restrictedMode = true;
        p.enumValues.values = vals;
    }

    static void padName(int s, char* buf, size_t sz)
    {
        const int p = padOfSlot(s);
        std::snprintf(buf, sz, "%c%d", 'A' + p / kPadsPerPage, p % kPadsPerPage + 1);
    }

    void initParameter(const uint32_t index, Parameter& p) override
    {
        char name[64], sym[32], pn[8];
        const int i = (int)index;
        p.hints = 0;
        p.ranges.def = paramDefault(i);
        if (isSlotParam(i)) {
            const int s = slotOf(i), f = fieldOf(i);
            padName(s, pn, sizeof(pn));
            p.hints = kParameterIsAutomatable;
            p.ranges.min = 0.f; p.ranges.max = 1.f;
            switch (f) {
            case F_TYPE: {
                std::snprintf(name, sizeof(name), "Pad %s Type", pn);
                std::snprintf(sym, sizeof(sym), "p%s_type", pn);
                p.ranges.max = (float)(kNumTypes - 1);
                p.hints |= kParameterIsInteger;
                static const char* names[kNumTypes];
                for (int e = 0; e < kNumEffects; ++e) names[typeOfEffect(e)] = kEffects[e].name;
                for (int m = 0; m < kNumMoves; ++m) names[typeOfMove(m)] = kMoveNames[m];
                names[kTypeEmpty] = "EMPTY";
                setEnum(p, names, kNumTypes);
                break;
            }
            case F_LATCH: case F_REL: case F_SNAP: {
                static const char* const fn[3] = { "Mode", "Release", "Snap Back" };
                static const char* const fs[3] = { "latch", "rel", "snap" };
                static const char* const v0[3][2] = { { "Hold", "Latch" }, { "Cut", "Ring Out" }, { "Off", "On" } };
                const int k = f - F_LATCH;
                std::snprintf(name, sizeof(name), "Pad %s %s", pn, fn[k]);
                std::snprintf(sym, sizeof(sym), "p%s_%s", pn, fs[k]);
                p.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
                setEnum(p, v0[k], 2);
                break;
            }
            case F_VOL:
                std::snprintf(name, sizeof(name), "Pad %s Sample Volume", pn);
                std::snprintf(sym, sizeof(sym), "p%s_vol", pn);
                break;
            default:
                std::snprintf(name, sizeof(name), "Pad %s Control %d", pn, f - F_K1 + 1);
                std::snprintf(sym, sizeof(sym), "p%s_k%d", pn, f - F_K1 + 1);
                break;
            }
            p.name = name; p.symbol = sym;
            return;
        }
        if (i >= pPad0 && i < pPad0 + kNumPads) {
            const int pad = i - pPad0;
            std::snprintf(name, sizeof(name), "Pad %c%d", 'A' + pad / kPadsPerPage, pad % kPadsPerPage + 1);
            std::snprintf(sym, sizeof(sym), "pad_%c%d", 'a' + pad / kPadsPerPage, pad % kPadsPerPage + 1);
            p.name = name; p.symbol = sym; p.hints = kParameterIsAutomatable;
            p.ranges.min = 0; p.ranges.max = 1; p.ranges.def = 0;
            return;
        }
        if (i >= pCtrl0 && i <= pCtrl7) {
            std::snprintf(name, sizeof(name), "Knob %d (%s)", i - pCtrl0 + 1, kJobNames[i - pCtrl0]);
            std::snprintf(sym, sizeof(sym), "knob%d", i - pCtrl0 + 1);
            p.name = name; p.symbol = sym; p.hints = kParameterIsAutomatable;
            p.ranges.min = 0; p.ranges.max = 1; p.ranges.def = 0;
            return;
        }
        switch (i) {
        case pOut:     p.name = "Output"; p.symbol = "out"; p.unit = "dB"; p.hints = kParameterIsAutomatable; p.ranges.min = -60; p.ranges.max = 6; return;
        case pLimit:   p.name = "Limit"; p.symbol = "limit"; p.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger; p.ranges.min = 0; p.ranges.max = 1; return;
        case pTiming: {
            p.name = "Timing"; p.symbol = "timing"; p.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger; p.ranges.min = 0; p.ranges.max = 1;
            static const char* const t[2] = { "Raw", "Grid" }; setEnum(p, t, 2); return;
        }
        case pTail:    p.name = "Dry Kill Tail"; p.symbol = "tail"; p.hints = kParameterIsAutomatable; p.ranges.min = 0; p.ranges.max = 1; return;
        case pThrowTime: {
            p.name = "Throw Time"; p.symbol = "throw_time"; p.hints = kParameterIsAutomatable | kParameterIsInteger; p.ranges.min = 0; p.ranges.max = 12;
            setEnum(p, kDelayTimeNames, 13); return;
        }
        case pThrowFb: p.name = "Throw Feedback"; p.symbol = "throw_fb"; p.hints = kParameterIsAutomatable; p.ranges.min = 0; p.ranges.max = 1; return;
        case pFreezeLen: p.name = "Freeze Length"; p.symbol = "freeze_len"; p.hints = kParameterIsAutomatable; p.ranges.min = 0; p.ranges.max = 1; return;
        case pFreezeSpeed: p.name = "Freeze Speed"; p.symbol = "freeze_speed"; p.hints = kParameterIsAutomatable; p.ranges.min = 0; p.ranges.max = 1; return;
        case pSelect: p.name = "Selected Pad"; p.symbol = "select"; p.hints = kParameterIsInteger; p.ranges.min = 0; p.ranges.max = kNumSlots - 1; return;
        default: break;
        }
        // outputs
        p.hints = kParameterIsOutput;
        p.ranges.min = -1.f; p.ranges.max = 100000000.f; p.ranges.def = 0.f;
        if (i >= oEff0 && i < oEff0 + kNumSlots) { padName(i - oEff0, pn, sizeof(pn)); std::snprintf(name, sizeof(name), "Pad %s Active", pn); std::snprintf(sym, sizeof(sym), "on_%s", pn); }
        else if (i >= oSmpPos0 && i < oSmpPos0 + kNumSlots) { padName(i - oSmpPos0, pn, sizeof(pn)); std::snprintf(name, sizeof(name), "Pad %s Sample Position", pn); std::snprintf(sym, sizeof(sym), "smppos_%s", pn); }
        else if (i >= oJob0 && i < oJob0 + kNumJobs) { std::snprintf(name, sizeof(name), "Knob Value %d", i - oJob0 + 1); std::snprintf(sym, sizeof(sym), "job%d", i - oJob0 + 1); }
        else {
            const char* n = "Output"; const char* sy = "o";
            switch (i) {
            case oMoves: n = "Moves"; sy = "moves"; break;
            case oBpm: n = "Host Tempo"; sy = "host_bpm"; break;
            case oFocus: n = "Selected Pad Out"; sy = "focus"; break;
            case oFocusPhase: n = "Selected Loop Phase"; sy = "focus_phase"; break;
            case oPicked: n = "Knobs Picked Up"; sy = "picked"; break;
            case oJobSeq: n = "Knob Seq"; sy = "job_seq"; break;
            case oLearned: n = "MIDI Learned"; sy = "learned"; break;
            case oLearnSeq: n = "MIDI Learn Seq"; sy = "learn_seq"; break;
            case oMidiSeq: n = "MIDI Seq"; sy = "midi_seq"; break;
            case oMidiState: n = "MIDI State"; sy = "midi_state"; break;
            case oPadHit: n = "Pad Hit"; sy = "pad_hit"; break;
            case oPadHitSeq: n = "Pad Hit Seq"; sy = "pad_hit_seq"; break;
            case oLevel: n = "Output Level"; sy = "level"; break;
            case oKill: n = "Kill Active"; sy = "kill_on"; break;
            case oGrabPhase: n = "Grab Phase"; sy = "grab_phase"; break;
            case oLearnDup: n = "MIDI Learn Duplicate"; sy = "learn_dup"; break;
            case oLearnDupSeq: n = "MIDI Learn Duplicate Seq"; sy = "learn_dup_seq"; break;
            case oMidiLast: n = "Unmapped MIDI"; sy = "midi_last"; break;
            case oMidiLastSeq: n = "Unmapped MIDI Seq"; sy = "midi_last_seq"; break;
            case oRepeatSeq: n = "Note Repeat Seq"; sy = "repeat_seq"; break;
            case oVoices: n = "Voices In Use"; sy = "voices"; break;
            case oKnobTypes: n = "Knob Types"; sy = "knob_types"; break;
            case oBeatReadyLo: n = "Beats Ready A"; sy = "beats_ready_a"; break;
            case oBeatReadyHi: n = "Beats Ready B"; sy = "beats_ready_b"; break;
            case oBeatPlaying: n = "Beat Playing"; sy = "beat_playing"; break;
            case oBeatBpm: n = "Beat Tempo"; sy = "beat_bpm"; break;
            case oBank: n = "Bank Shown"; sy = "bank"; break;
            }
            std::snprintf(name, sizeof(name), "%s", n); std::snprintf(sym, sizeof(sym), "%s", sy);
        }
        p.name = name; p.symbol = sym;
    }

    void initState(const uint32_t index, State& state) override
    {
        if (index >= (uint32_t)kStateBeat0 && index < (uint32_t)(kStateBeat0 + kNumSlots)) {
            state.key = gBeatKeys[index - kStateBeat0]; state.defaultValue = ""; state.label = "Pad beat";
            return;
        }
        if (index >= (uint32_t)kStateSmp0 && index < (uint32_t)(kStateSmp0 + kNumSlots)) {
            state.key = gSmpKeys[index - kStateSmp0]; state.defaultValue = ""; state.label = "Pad sample";
            return;
        }
        switch (index) {
        case kStateMidiMap: state.key = "midimap"; state.defaultValue = ""; state.label = "MIDI map backup"; state.hints = kStateIsOnlyForUI; break;
        case kStateCtl:     state.key = "ctlmap";  state.defaultValue = ""; state.label = "Controller setup"; break;
        case kStateLearn:   state.key = "learn";   state.defaultValue = "-1"; state.label = "MIDI learn"; break;
        case kStateAllOff:  state.key = "alloff";  state.defaultValue = "0"; state.label = "All off"; break;
        case kStateReorder: state.key = "reorder"; state.defaultValue = ""; state.label = "Pad reorder"; break;
        case kStateAdv:     state.key = "adv";     state.defaultValue = ""; state.label = "Advanced panels"; state.hints = kStateIsOnlyForUI; break;
        case kStateMem:     state.key = "mem";     state.defaultValue = ""; state.label = "Per-effect memory"; state.hints = kStateIsOnlyForUI; break;
        case kStateDark:    state.key = "dark";    state.defaultValue = ""; state.label = "Dark mode"; state.hints = kStateIsOnlyForUI; break;
        case kStateSize:    state.key = "size";    state.defaultValue = ""; state.label = "Window size"; state.hints = kStateIsOnlyForUI; break;
        case kStatePages:   state.key = "pages";   state.defaultValue = ""; state.label = "Pages shown"; state.hints = kStateIsOnlyForUI; break;
        case kStatePreset:  state.key = "preset";  state.defaultValue = ""; state.label = "Preset name"; state.hints = kStateIsOnlyForUI; break;
        case kStateBeatMode: state.key = "beatmode"; state.defaultValue = "0"; state.label = "Beat handover"; break;
        case kStateBank:    state.key = "bank";    state.defaultValue = "0"; state.label = "Bank shown"; break;
        default: break;
        }
    }

    void setState(const char* key, const char* value) override
    {
        if (value == nullptr) value = "";
        if (std::strncmp(key, "beat", 4) == 0 && std::isdigit((unsigned char)key[4])) {
            const int k = std::atoi(key + 4);
            if (k < 0 || k >= kNumSlots) return;
            BeatInfo bi;
            const bool has = beatFromString(value, bi);
            fBeats[k].bpm.store(has ? bi.bpm : 0.f);
            fBeatHas[k].store(has);
            std::lock_guard<std::mutex> g(fLoadMutex);
            const std::string path = has ? bi.path : std::string();
            if (path != fBeats[k].path) {
                fBeats[k].path = path;
                BeatData* d = path.empty() ? nullptr : storeFind(path);
                fBeats[k].data.store(d, std::memory_order_release);
                fBeatReady[k].store(d != nullptr);
                if (!path.empty() && d == nullptr) {
                    // the loader thread starts with the first beat that needs it, so an instance that never
                    // loads a beat (like the one DPF keeps for plug-in info) never owns a thread to wait for
                    if (!fLoader.joinable()) fLoader = std::thread([this] { loaderLoop(); });
                    fLoadQueue.push_back(std::make_pair(k, path)); fLoadCv.notify_one();
                }
                storeCollect();
            }
            return;
        }
        if (std::strcmp(key, "beatmode") == 0) { fBeatMode.store(std::max(0, std::min(2, std::atoi(value)))); return; }
        if (std::strcmp(key, "bank") == 0) { fBank.store(std::max(0, std::min(kNumPages - 1, std::atoi(value)))); return; }
        if (std::strncmp(key, "smp", 3) == 0) {
            const int k = std::atoi(key + 3);
            if (k < 0 || k >= kNumSlots) return;
            SampleData* sd = nullptr;
            if (value[0] != '\0') {
                sd = new SampleData();
                if (!decodeSample(value, *sd)) { delete sd; sd = nullptr; }
            }
            fSmp[k].set(sd);
        } else if (std::strcmp(key, "ctlmap") == 0) {
            if (fCtlLast != value) { fCtlLast = value; loadComputerSetup(); }
        } else if (std::strcmp(key, "learn") == 0) {
            const int slot = std::atoi(value);
            fLearnGuard.store(std::strchr(value, 'g') != nullptr);
            fLearnSlot.store(slot >= 0 && slot < kNumLearn ? slot : -1);
        } else if (std::strcmp(key, "alloff") == 0) {
            if (fAllOffLast != value) { fAllOffLast = value; fAllOffReq.store(true); }
        } else if (std::strcmp(key, "reorder") == 0) {
            int ord[kNumSlots + 1];
            if (parseInts(value, ord, kNumSlots + 1, -1) >= kNumSlots) {
                bool ok = true, seen[kNumSlots] = {};
                for (int i = 0; i < kNumSlots; ++i) { if (ord[i] < 0 || ord[i] >= kNumSlots || seen[ord[i]]) ok = false; else seen[ord[i]] = true; }
                if (ok) { for (int i = 0; i < kNumSlots; ++i) fReorder[i].store(ord[i]); fReorderReq.store(true); }
            }
        }
    }

    // this computer's controller setup (map, knob type, note map); projects never override it
    void loadComputerSetup()
    {
        Prefs pr;
        readPrefs(pr);
        int mm[kNumLearn];
        parseInts(pr.map.c_str(), mm, kNumLearn, -1);
        for (int i = 0; i < kNumLearn; ++i) fMap[i].store(mm[i]);
        fKnobMode.store(pr.kmode >= kKnobAuto && pr.kmode < kNumKnobModes ? pr.kmode : kKnobAuto);
        fNoteStart.store(pr.notes >= 0 && pr.notes <= 112 ? pr.notes : -1);
        fNoteOrder.store(pr.norder == 1 ? 1 : 0);
        fKnobSpeed.store(pr.kspeed >= 0 && pr.kspeed <= 2 ? pr.kspeed : 1);
        fKnobJump.store(pr.kjump == 1);          // pickup is the default: a knob waits until it reaches the value
        fKnobCountSet.store(pr.kcount >= 1 && pr.kcount <= 5 ? pr.kcount : 0);
        fPadFeel.store(pr.padfeel >= 0 && pr.padfeel <= 2 ? pr.padfeel : 0);
        fFollow.store(pr.follow == 1);   // velocity + pressure by default
        int kt[kNumJobs];
        parseInts(pr.ktypes.c_str(), kt, kNumJobs, 0);
        for (int j = 0; j < kNumJobs; ++j) fSavedType[j].store(kt[j] >= kKnobAbs && kt[j] <= kKnobRel3 ? kt[j] : 0);
        fKnobModeDirty.store(true);
    }

    float getParameterValue(const uint32_t index) const override
    {
        const int i = (int)index;
        if (i >= oEff0 && i < oEff0 + kNumSlots) return fActive[i - oEff0] ? 1.f : 0.f;
        if (i >= oSmpPos0 && i < oSmpPos0 + kNumSlots) return fSmpPos[i - oSmpPos0];
        if (i >= oJob0 && i < oJob0 + kNumJobs) return fJobOut[i - oJob0];
        switch (i) {
        case oMoves: return (float)movesMask();
        case oBpm: return (float)fBpm;
        case oFocus: return (float)fFocus;
        case oFocusPhase: return fFocusPhase;
        case oPicked: { int m = 0; for (int j = 0; j < kNumJobs; ++j) if (fJobPicked[j]) m |= 1 << j; return (float)m; }
        case oJobSeq: return (float)fJobSeq;
        case oLearned: return fLearnedVal;
        case oLearnSeq: return (float)fLearnSeq;
        case oMidiSeq: return (float)fMidiSeq;
        case oMidiState: return (float)fMidiState;
        case oPadHit: return (float)fPadHit;
        case oPadHitSeq: return (float)fPadHitSeq;
        case oLevel: return fLevel;
        case oKill: return fKillOn ? 1.f : 0.f;
        case oGrabPhase: return fGrabPhase;
        case oLearnDup: return (float)fLearnDup;
        case oLearnDupSeq: return (float)fLearnDupSeq;
        case oMidiLast: return (float)fMidiLast;
        case oMidiLastSeq: return (float)fMidiLastSeq;
        case oRepeatSeq: return (float)fRepeatSeq;
        case oVoices: return (float)fVoicesBusy;
        case oBeatReadyLo: { int m = 0; for (int k = 0; k < 24; ++k) if (fBeatReady[k].load()) m |= 1 << k; return (float)m; }
        case oBeatReadyHi: { int m = 0; for (int k = 24; k < kNumSlots; ++k) if (fBeatReady[k].load()) m |= 1 << (k - 24); return (float)m; }
        case oBeatPlaying: return (float)fBeatCur;
        case oBeatBpm: return fBeatCur >= 0 ? fBeats[fBeatCur].bpm.load() : 0.f;
        case oBank: return (float)fBank.load(std::memory_order_relaxed);
        case oKnobTypes: { int m = 0; for (int j = 0; j < kNumJobs; ++j) m |= (fJobDetect[j] & 7) << (3 * j); return (float)m; }
        default: break;
        }
        return fParams[i];
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= (uint32_t)kParamCount || isOutputParam((int)index)) return;
        fParams[index] = value;
        fLive[index] = value;    // a host or UI write always wins over a knob-driven value
        if (index >= (uint32_t)pCtrl0 && index <= (uint32_t)pCtrl7) {
            // the first value is the host restoring a project: remember it, don't act on it
            const int k = (int)index - pCtrl0;
            const float v = clampf(value, 0.f, 1.f);
            if (fCtrlSeen[k] && v != fCtrl[k]) fCtrlDirty[k] = true;
            fCtrlSeen[k] = true;
            fCtrl[k] = v;
        }
        if (index == pTiming) fGrid = value > 0.5f;
        if (index == pSelect) fSelectReq = (int)std::lround(value);
    }

    void sampleRateChanged(const double newSampleRate) override { prepareAll(newSampleRate); }
    void activate() override
    {
        // a restart (new buffer size or sample rate) can swallow note-offs: let go of every pad held by MIDI
        // (latched pads stay latched)
        releaseMidiHeld();
        prepareAll(getSampleRate());
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
       #ifdef YANCEY_HAVE_SSE
        const unsigned int oldCsr = _mm_getcsr();
        _mm_setcsr(oldCsr | 0x8040);
       #endif

        const TimePosition& tp = getTimePosition();
        if (tp.bbt.valid && tp.bbt.beatsPerMinute > 1.0) fHostBpm = tp.bbt.beatsPerMinute;
        if (tp.playing && tp.bbt.valid) {
            const double tpb = tp.bbt.ticksPerBeat > 0.0 ? tp.bbt.ticksPerBeat : 1920.0;
            fBeat = (double)(tp.bbt.bar - 1) * tp.bbt.beatsPerBar + (double)(tp.bbt.beat - 1) + tp.bbt.tick / tpb;
        }
        // a beat pad that's playing sets the tempo for every synced effect, like a sampler bank's BPM
        // the playing beat keeps going while pads move; it only stops if no pad holds it any more
        if (fBeatCurData != nullptr) {
            bool held = false;
            for (int j = 0; j < kNumSlots && !held; ++j) held = fBeats[j].data.load(std::memory_order_relaxed) == fBeatCurData;
            if (!held) beatTapStop();
        }
        const float beatBpm = fBeatCur >= 0 ? fBeats[fBeatCur].bpm.load(std::memory_order_relaxed) : 0.f;
        fBpm = beatBpm > 1.f ? (double)beatBpm : fHostBpm;
        const double beatInc = fBpm / 60.0 / fFs;

        if (fAllOffReq.exchange(false)) allOff();
        if (fReorderReq.exchange(false)) applyReorder();
        if (fKnobModeDirty.exchange(false))
            for (int j = 0; j < kNumJobs; ++j) { fJobDetect[j] = fSavedType[j].load(); fJobLastVal[j] = -1; fJobAbsLast[j] = -1; fJobSeenLow[j] = fJobSeenHigh[j] = fJobSeenMid[j] = false; }
        if (fSelectReq >= 0) { if (fSelectReq < kNumSlots) setFocus(fSelectReq); fSelectReq = -1; }

        int map[kNumLearn];
        for (int i = 0; i < kNumLearn; ++i) map[i] = fMap[i].load(std::memory_order_relaxed);
        const int noteStart = fNoteStart.load(std::memory_order_relaxed);

        applyHostKnobs();
        updatePads();
        pushBlock();

        SampleData* smp[kNumSlots];
        uint32_t smpSeq[kNumSlots];
        float volT[kNumSlots];
        for (int k = 0; k < kNumSlots; ++k) {
            SamplePad& pd = fSmp[k];
            smpSeq[k] = pd.blockStart();
            if (pd.stopReq.exchange(false)) pd.playing = pd.fading = false;
            smp[k] = pd.data.load(std::memory_order_acquire);
            volT[k] = padVolGain(fLive[slotParam(k, F_VOL)]);
            if (smp[k] == nullptr) pd.playing = pd.fading = false;
        }
        const float fadeStep = 1.f / (0.003f * fFs);
        const float killStep = 1.f / (0.004f * fFs);
        const float cutStep = 1.f / (0.008f * fFs);
        const float quietLimit = 0.4f * fFs;
        const bool limit = fParams[pLimit] > 0.5f;

        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];

        Clock ck;
        ck.spb = 60.0 / std::max(20.0, fBpm) * fFs;
        ck.grid = fGrid;

        uint32_t ev = 0;
        float peak = 0.f;
        for (uint32_t i = 0; i < frames; ++i)
        {
            if (ev < midiEventCount && midiEvents[ev].frame <= i) {
                while (ev < midiEventCount && midiEvents[ev].frame <= i) { handleMidi(midiEvents[ev], map, noteStart); ++ev; }
                applyHostKnobs();
                updatePads();
                pushBlock();
                ck.grid = fGrid;
            }

            ck.beat = fBeat;
            const long s16 = (long)std::floor(fBeat * 4.0);
            ck.tick16 = !fGrid || s16 != fLast16;
            fLast16 = s16;
            fBeat += beatInc;

            float l = inL[i], r = inR[i];

            // beat pads play in as the input, so every effect, Dry Kill and Kill work on them
            if (fBeatCur >= 0 || fBeatOld >= 0) {
                float bl = 0.f, br = 0.f;
                const float vg = fBeatVol.tick(fBeatCur >= 0 ? padVolGain(fLive[slotParam(fBeatCur, F_VOL)]) : fBeatVol.y);
                if (fBeatCur >= 0) {
                    const BeatData* bd = fBeatCurData;
                    if (bd == nullptr || fBeatPos >= bd->frames - 3) { fBeatCur = -1; fBeatPending = -1; fBeatCurData = nullptr; fPlayCur.store(nullptr); }
                    else {
                        float x, y; readBeat(*bd, fBeatPos, x, y);
                        fBeatIn = std::min(1.f, fBeatIn + fBeatInStep);
                        bl += x * fBeatIn; br += y * fBeatIn;
                        const double before = fBeatPos;
                        fBeatPos += bd->rate / fFs;
                        if (beatBpm > 1.f) {
                            fBeat = fBeatPos / bd->rate * beatBpm / 60.0;          // the beat's own bar clock
                            if (fBeatPending >= 0) {
                                const double bar = 4.0 * 60.0 / beatBpm * bd->rate;
                                if (std::floor(fBeatPos / bar) != std::floor(before / bar)) { const int nx = fBeatPending; fBeatPending = -1; startBeat(nx, 0.004f); }
                            }
                        }
                    }
                }
                if (fBeatOld >= 0) {
                    const BeatData* od = fBeatOldData;
                    if (od == nullptr || fBeatOldPos >= od->frames - 3 || fBeatOldGain <= 0.f) { fBeatOld = -1; fBeatOldData = nullptr; fPlayOld.store(nullptr); }
                    else {
                        float x, y; readBeat(*od, fBeatOldPos, x, y);
                        bl += x * fBeatOldGain; br += y * fBeatOldGain;
                        fBeatOldPos += od->rate / fFs;
                        fBeatOldGain -= fBeatOldStep;
                    }
                }
                l += bl * vg; r += br * vg;
            }

            // DRY KILL: close the input, let whatever is ringing keep ringing
            const float gate = fInGate.tick(!fMoveOn[kMvDryKill]);
            l *= gate; r *= gate;

            // every busy voice, in the fixed chain order
            for (int ci = 0; ci < kNumEffects; ++ci) {
                const int e = kChainOrder[ci];
                for (int k = 0; k < kVoicesPerFx; ++k) {
                    Voice& vc = fVoice[e][k];
                    if (vc.state == kVoiceFree) {
                        if (FxPool::listens(e)) fPool.idleListen(e, k, l, r, ck);
                        continue;
                    }
                    const float xl = l, xr = r;
                    vc.amt += (vc.amtT - vc.amt) * fAmtCoef;
                    if (FxPool::isSelfMixed(e)) fPool.processSelfMixed(e, k, l, r, vc.state == kVoiceOn, vc.amt);
                    else {
                        fPool.process(e, k, l, r, vc.state == kVoiceOn, ck);
                        if (vc.amt < 0.9995f) { l = xl + (l - xl) * vc.amt; r = xr + (r - xr) * vc.amt; }
                    }
                    if (vc.state == kVoiceCut) {
                        l = xl + (l - xl) * vc.cut; r = xr + (r - xr) * vc.cut;
                        vc.cut -= cutStep;
                        if (vc.cut <= 0.f) { fPool.clear(e, k); vc.state = kVoiceFree; vc.slot = -1; }
                    } else if (vc.state == kVoiceRing) {
                        // free the voice once its tail has gone quiet
                        if (std::fabs(l - xl) + std::fabs(r - xr) < 2e-5f) { vc.quietFor += 1.f; if (vc.quietFor > quietLimit) { vc.state = kVoiceFree; vc.slot = -1; } }
                        else vc.quietFor = 0.f;
                    }
                }
            }

            // grabs and freeze work on everything the effects put out
            fGrab.canStart = ck.tick16;
            fGrab.process(l, r, fGrabOn);
            fFreeze.process(l, r, fMoveOn[kMvFreeze]);

            // THROW: send into a dub echo while held; the echoes ring out after release
            fThrow.process(l, r, fMoveOn[kMvThrow]);

            const float tg = fTailSm.tick(1.f + (fLive[pTail] * 1.25f - 1.f) * (1.f - gate));
            l *= tg; r *= tg;

            // one-shot samples play clean on top
            for (int k = 0; k < kNumSlots; ++k) {
                SamplePad& pd = fSmp[k];
                const SampleData* sd = smp[k];
                if (sd == nullptr || (!pd.playing && !pd.fading)) continue;
                if (pd.fresh) { pd.vol.y = volT[k]; pd.fresh = false; }
                const float vg = pd.vol.tick(volT[k]);
                const double ratio = sd->rate / fFs;
                const int last = sd->frames - 1;
                auto readAt = [&](double ps, float& ol, float& orr) {
                    const int i0 = (int)ps; const float t = (float)(ps - i0);
                    const int i1 = i0 + 1 < sd->frames ? i0 + 1 : i0;
                    ol = sd->l[i0] + (sd->l[i1] - sd->l[i0]) * t;
                    orr = sd->channels == 2 ? sd->r[i0] + (sd->r[i1] - sd->r[i0]) * t : ol;
                };
                if (pd.playing) {
                    if (pd.pos >= last) pd.playing = false;
                    else { float sl, sr; readAt(pd.pos, sl, sr); l += sl * vg; r += sr * vg; pd.pos += ratio; }
                }
                if (pd.fading) {
                    if (pd.fadePos >= last || pd.fadeGain <= 0.f) pd.fading = false;
                    else {
                        float sl, sr; readAt(pd.fadePos, sl, sr);
                        l += sl * vg * pd.fadeGain; r += sr * vg * pd.fadeGain;
                        pd.fadePos += ratio; pd.fadeGain -= fadeStep;
                    }
                }
            }

            const float og = fOutSm.tick(fLive[pOut] <= -59.9f ? 0.f : dbToLin(fLive[pOut]));
            l *= og; r *= og;

            if (fKillOn) { fKillGain -= killStep; if (fKillGain < 0.f) fKillGain = 0.f; }
            else         { fKillGain += killStep; if (fKillGain > 1.f) fKillGain = 1.f; }
            l *= fKillGain; r *= fKillGain;

            if (limit) { l = softCeil(l); r = softCeil(r); }
            else {
                if (std::fabs(l) > 1.f) l = (l > 0.f ? 1.f : -1.f) * (1.f + std::tanh(std::fabs(l) - 1.f));
                if (std::fabs(r) > 1.f) r = (r > 0.f ? 1.f : -1.f) * (1.f + std::tanh(std::fabs(r) - 1.f));
            }
            if (!std::isfinite(l) || !std::isfinite(r)) { l = r = 0.f; fNanGuard = true; }
            outL[i] = l;
            outR[i] = r;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        while (ev < midiEventCount) { handleMidi(midiEvents[ev], map, noteStart); ++ev; }
        updatePads();

        if (fNanGuard) { fNanGuard = false; prepareDsp(); }

        if (peak > fLevel) fLevel = std::min(1.f, peak);
        else fLevel *= std::exp(-(float)frames / (0.3f * fFs));
        if (fLevel < 1e-5f) fLevel = 0.f;

        fGrabPhase = fGrabOn ? fGrab.phase : -1.f;
        fFocusPhase = -1.f;
        fVoicesBusy = 0;
        for (int e = 0; e < kNumEffects; ++e)
            for (int k = 0; k < kVoicesPerFx; ++k) {
                if (fVoice[e][k].state != kVoiceFree) ++fVoicesBusy;
                if (fVoice[e][k].state == kVoiceOn && fVoice[e][k].slot == fFocus) fFocusPhase = fPool.phase(e, k);
            }
        for (int k = 0; k < kNumSlots; ++k) {
            const SampleData* sd = smp[k];
            fSmpPos[k] = (sd && fSmp[k].playing && sd->frames > 0) ? (float)(fSmp[k].pos / sd->frames) : -1.f;
            fSmp[k].blockDone(smpSeq[k]);           // done with whatever this block read
        }
        if (fBeatCur >= 0 && fBeatCurData && fBeatCurData->frames > 0) fSmpPos[fBeatCur] = (float)(fBeatPos / fBeatCurData->frames);
        fSampleClock += frames;
        for (int s = 0; s < kNumSlots; ++s)
            fLiveOut.amt[s].store(fActive[s] && effectOf(fParams, s) >= 0 ? slotAmount(s) : 0.f, std::memory_order_relaxed);

       #ifdef YANCEY_HAVE_SSE
        _mm_setcsr(oldCsr);
       #endif
    }

private:
    enum { kStateMidiMap = 0, kStateCtl, kStateLearn, kStateAllOff, kStateReorder, kStateAdv, kStateMem, kStateDark, kStateSize,
           kStatePages, kStatePreset, kStateBeatMode, kStateBank, kStateSmp0, kStateBeat0 = kStateSmp0 + kNumSlots, kStateCount = kStateBeat0 + kNumSlots };

    void prepareDsp()
    {
        fPool.prepare(fFs);
        for (int e = 0; e < kNumEffects; ++e) for (int k = 0; k < kVoicesPerFx; ++k) { fVoice[e][k].state = kVoiceFree; fVoice[e][k].slot = -1; }
        fGrab.prepare(fFs); fFreeze.prepare(fFs); fThrow.prepare(fFs);
    }

    void prepareAll(double sr)
    {
        if (sr < 8000.0) sr = 44100.0;
        fFs = (float)sr;
        prepareDsp();
        fInGate.setTime(fFs, 0.010f); fInGate.v = 1.f;
        fOutSm.setTime(fFs, 0.004f); fOutSm.y = 1.f;
        fTailSm.setTime(fFs, 0.02f); fTailSm.y = 1.f;
        fBeatVol.setTime(fFs, 0.004f); fBeatVol.y = 1.f;
        fAmtCoef = 1.f - std::exp(-1.f / (0.008f * fFs));
        for (int k = 0; k < kNumSlots; ++k) {
            fSmp[k].vol.setTime(fFs, 0.004f);
            fSmp[k].vol.y = padVolGain(fParams[slotParam(k, F_VOL)]);
            fSmp[k].playing = fSmp[k].fading = false;
        }
    }

    // knob values for every voice (a releasing voice keeps the values it had)
    void pushBlock()
    {
        BlockCtx ctx;
        ctx.fs = fFs;
        ctx.bpm = fBpm;
        ctx.limit = fParams[pLimit] > 0.5f;
        for (int e = 0; e < kNumEffects; ++e)
            for (int k = 0; k < kVoicesPerFx; ++k) {
                Voice& vc = fVoice[e][k];
                if (vc.state == kVoiceFree) continue;
                if (vc.slot >= 0 && effectOf(fParams, vc.slot) == e) {
                    for (int c = 0; c < kKnobsPerSlot; ++c)
                        vc.v[c] = c < numControls(e) ? ctlFromNorm(kEffects[e].c[c], fLive[slotParam(vc.slot, F_K1 + c)]) : 0.f;
                    if (e == kFxStutterV || e == kFxReverseV) vc.v[4] = fParams[slotParam(vc.slot, F_K5)];   // the pad's pattern
                }
                fPool.block(e, k, ctx, vc.v);
            }
        // the listening engines: the DJ Looper ones listen all the time (as many as there are DJ Looper pads),
        // and an engine nobody is playing keeps the knobs of the pad it's waiting for, so a press never
        // starts with the knobs still moving
        for (int e : { (int)kFxDjLooper, (int)kFxVinyl, (int)kFxStutterV, (int)kFxReverseV }) {
            int home[kVoicesPerFx], n = 0;
            for (int s = 0; s < kNumSlots && n < kVoicesPerFx; ++s) if (effectOf(fParams, s) == e && !hasAudio(s)) home[n++] = s;
            for (int k = 0; k < kVoicesPerFx; ++k) {
                if (e == kFxDjLooper) { fPool.dj[k].warm = k < n; fPool.dj[k].homeSlot = k < n ? home[k] : -1; }
                else if (e == kFxVinyl) { fPool.vin[k].warm = k < n; fPool.vin[k].homeSlot = k < n ? home[k] : -1; }
                else if (e == kFxStutterV) { fPool.stut[k].warm = k < n; fPool.stut[k].homeSlot = k < n ? home[k] : -1; }
                else { fPool.rev[k].warm = k < n; fPool.rev[k].homeSlot = k < n ? home[k] : -1; }
                if (fVoice[e][k].state != kVoiceFree || k >= n) continue;
                float v[kKnobsPerSlot];
                for (int c = 0; c < kKnobsPerSlot; ++c) v[c] = c < numControls(e) ? ctlFromNorm(kEffects[e].c[c], fLive[slotParam(home[k], F_K1 + c)]) : 0.f;
                if (e == kFxStutterV || e == kFxReverseV) v[4] = fParams[slotParam(home[k], F_K5)];
                fPool.block(e, k, ctx, v);
            }
        }
        fGrab.block(ctx, fGrabLen, 0, 100.f);
        fFreeze.block(ctx, freezeMs(fLive[pFreezeLen]), freezeSpeed(fLive[pFreezeSpeed]), 100.f, 8.f);
        fThrow.block(ctx, (int)std::lround(fLive[pThrowTime]), clampf(fLive[pThrowFb], 0.f, 1.f) * 95.f, 50.f, 160.f, 5200.f);
    }

    unsigned movesMask() const
    {
        unsigned m = 0;
        for (int k = kMvDryKill; k <= kMvGrab1; ++k) if (fMoveOn[k]) m |= 1u << k;
        if (fGrid) m |= 1u << kMvTiming;
        if (fMoveOn[kMvKill]) m |= 1u << kMvKill;
        return m;
    }

    void releaseVoice(Voice& vc, int e)
    {
        const bool ring = e == kFxSmear || e == kFxPitch        // the smear rings out; pitch glides back home
            || (kEffects[e].tail && vc.slot >= 0 && effectOf(fParams, vc.slot) == e && fParams[slotParam(vc.slot, F_REL)] > 0.5f);
        if (ring) { vc.state = kVoiceRing; vc.quietFor = 0.f; }
        else { vc.state = kVoiceCut; vc.cut = 1.f; }
    }

    void releaseMidiHeld()
    {
        for (int p = 0; p < kNumPads; ++p) fPadMidi[p] = fPadNote[p] = false;
        for (int& h : fFollowHeld) h = -1;
    }

    void allOff()
    {
        for (int s = 0; s < kNumSlots; ++s) fLatched[s] = fHeld[s] = false;
        for (int m = 0; m < kNumMoves; ++m) fMoveOn[m] = false;
        for (int p = 0; p < kNumPads; ++p) fPadMidi[p] = fPadNote[p] = false;
        for (int& h : fFollowHeld) h = -1;
        for (int k = 0; k < kNumSlots; ++k) fSmp[k].playing = fSmp[k].fading = false;
        fBeatCur = fBeatOld = fBeatPending = -1;
        fBeatCurData = fBeatOldData = nullptr; fPlayCur.store(nullptr); fPlayOld.store(nullptr);
        for (int e = 0; e < kNumEffects; ++e)
            for (int k = 0; k < kVoicesPerFx; ++k)
                if (fVoice[e][k].state != kVoiceFree) { fVoice[e][k].state = kVoiceCut; fVoice[e][k].cut = std::min(fVoice[e][k].cut, 1.f); }
        fThrow.reset();
        fGrabOn = false;
    }

    void applyReorder()
    {
        bool latched[kNumSlots], held[kNumSlots], active[kNumSlots];
        int newOf[kNumSlots];
        for (int i = 0; i < kNumSlots; ++i) { latched[i] = fLatched[i]; held[i] = fHeld[i]; active[i] = fActive[i]; }
        for (int p = 0; p < kNumSlots; ++p) {
            const int q = fReorder[p].load();
            fLatched[p] = latched[q]; fHeld[p] = held[q]; fActive[p] = active[q];
            newOf[q] = p;
        }
        if (fFocus >= 0) fFocus = newOf[fFocus];
        if (fBeatCur >= 0) fBeatCur = newOf[fBeatCur];
        if (fBeatOld >= 0) fBeatOld = newOf[fBeatOld];
        if (fBeatPending >= 0) fBeatPending = newOf[fBeatPending];
        for (int e = 0; e < kNumEffects; ++e)
            for (int k = 0; k < kVoicesPerFx; ++k)
                if (fVoice[e][k].slot >= 0) fVoice[e][k].slot = newOf[fVoice[e][k].slot];
        for (int j = 0; j < kNumJobs; ++j) fJobTarget[j] = -2;
    }

    // ---------------------------------------------------------------------------------------------
    // Pads

    bool hasSample(int s) const { return s >= 0 && fSmp[s].data.load(std::memory_order_relaxed) != nullptr; }

    // how much of a pad's effect plays: Full mode always 1; otherwise velocity / pressure while held,
    // the strongest press while latched
    float slotAmount(int s) const
    {
        if (fPadFeel.load(std::memory_order_relaxed) == 2) return 1.f;
        const int p = padOfSlot(s);
        if (fPadPrev[p]) return latchOf(fParams, s) ? std::max(fPadAmt[p], fLatchAmt[s]) : fPadAmt[p];
        return fLatchAmt[s] > 0.f ? fLatchAmt[s] : 1.f;
    }
    bool hasAudio(int s) const { return hasSample(s) || (s >= 0 && fBeatHas[s].load(std::memory_order_relaxed)); }

    static inline void readBeat(const BeatData& bd, double pos, float& l, float& r)
    {
        const int i0 = (int)pos; const float t = (float)(pos - i0);
        const int im1 = i0 > 0 ? i0 - 1 : 0, i1 = i0 + 1, i2 = i0 + 2 < bd.frames ? i0 + 2 : i1;
        l = hermite(bd.L(im1), bd.L(i0), bd.L(i1), bd.L(i2), t);
        r = bd.r.empty() ? l : hermite(bd.R(im1), bd.R(i0), bd.R(i1), bd.R(i2), t);
    }

    // a beat pad was hit: start it, stop it, or hand over from the one that's playing
    void beatTap(int s)
    {
        if (fBeats[s].data.load(std::memory_order_relaxed) == nullptr) return;     // still loading
        if (s == fBeatCur) { beatTapStop(); return; }     // the same pad again stops it
        if (fBeatCur < 0) { startBeat(s, 0.003f); return; }
        switch (fBeatMode.load(std::memory_order_relaxed)) {
        case 1: fBeatPending = s; break;                                  // on the next bar
        case 2: {                                                         // crossfade over two beats
            const float b = fBeats[s].bpm.load();
            startBeat(s, (float)(2.0 * 60.0 / (b > 1.f ? b : fBpm)));
            break;
        }
        default: startBeat(s, 0.004f); break;                             // instantly
        }
    }
    void beatTapStop()
    {
        fBeatPending = -1;
        if (fBeatCur < 0) return;
        fBeatOld = fBeatCur; fBeatOldData = fBeatCurData; fPlayOld.store(fBeatOldData);
        fBeatOldPos = fBeatPos; fBeatOldGain = fBeatIn; fBeatOldStep = 1.f / (0.006f * fFs);
        fBeatCur = -1; fBeatCurData = nullptr; fPlayCur.store(nullptr);
    }
    void startBeat(int s, float fadeSeconds)
    {
        BeatData* d = fBeats[s].data.load(std::memory_order_acquire);
        if (d == nullptr) return;
        if (fBeatCur >= 0) {
            fBeatOld = fBeatCur; fBeatOldData = fBeatCurData; fPlayOld.store(fBeatOldData);
            fBeatOldPos = fBeatPos; fBeatOldGain = fBeatIn; fBeatOldStep = 1.f / std::max(1.f, fadeSeconds * fFs);
        }
        fBeatCur = s; fBeatCurData = d; fPlayCur.store(d);
        fBeatPos = 0.0; fBeatIn = 0.f; fBeatInStep = 1.f / std::max(1.f, fadeSeconds * fFs);
        if (!(fBeatVol.y > 0.f)) fBeatVol.y = padVolGain(fLive[slotParam(s, F_VOL)]);
    }

    void setFocus(int s)
    {
        if (s < 0 || s >= kNumSlots) return;
        fFocus = s;
    }

    // a pad went down or up: it plays whatever its slot holds right now
    void padEvent(int p, bool down)
    {
        const int s = slotOfPad(p);
        if (s < 0) return;                      // KILL is read straight from the pad state
        if (down) setFocus(s);
        if (fBeatHas[s].load(std::memory_order_relaxed)) { if (down) beatTap(s); return; }
        if (hasSample(s)) { if (down) fSmp[s].trigger(); return; }
        if (isEmpty(fParams, s)) return;
        const int m = moveOf(fParams, s);
        if (m >= 0 && !moveIsHold(m)) {
            if (!down) return;
            if (m == kMvTiming) fGrid = !fGrid;
            if (m == kMvAllOff) allOff();
            return;
        }
        if (m >= kMvGrab8 && m <= kMvGrab1 && down) fGrabLen = 2 + (m - kMvGrab8);
        // effects and held moves: hold by default, latch optional
        if (latchOf(fParams, s)) { if (down) fLatched[s] = !fLatched[s]; }
        else fHeld[s] = down;
    }

    Voice* grabVoice(int e, int s)
    {
        Voice* best = nullptr;
        // the voice this pad already had (still ringing or cutting), else a free one, else the oldest
        for (int k = 0; k < kVoicesPerFx; ++k) if (fVoice[e][k].slot == s && fVoice[e][k].state != kVoiceFree) return &fVoice[e][k];
        // a DJ Looper or Vinyl Sim pad takes the engine that has been listening for it
        if (FxPool::listens(e))
            for (int k = 0; k < kVoicesPerFx; ++k)
                if (fVoice[e][k].state == kVoiceFree && fPool.homeSlot(e, k) == s) return &fVoice[e][k];
        for (int k = 0; k < kVoicesPerFx; ++k) if (fVoice[e][k].state == kVoiceFree) return &fVoice[e][k];
        for (int k = 0; k < kVoicesPerFx; ++k) if (fVoice[e][k].state != kVoiceOn && (!best || fVoice[e][k].stamp < best->stamp)) best = &fVoice[e][k];
        if (best) { fPool.clear(e, (int)(best - fVoice[e])); return best; }
        for (int k = 0; k < kVoicesPerFx; ++k) if (!best || fVoice[e][k].stamp < best->stamp) best = &fVoice[e][k];
        if (best && best->slot >= 0) { fLatched[best->slot] = false; fActive[best->slot] = false; }
        if (best) fPool.clear(e, (int)(best - fVoice[e]));
        return best;
    }

    void updatePads()
    {
        const int feel = fPadFeel.load(std::memory_order_relaxed);
        for (int p = 0; p < kNumPads; ++p) {
            const bool midi = fPadMidi[p] || fPadNote[p];
            const bool now = midi || padOn(fParams[pPad0 + p]);
            if (now && !fPadPrev[p] && !midi) fPadAmt[p] = feel == 2 ? 1.f : clampf(fParams[pPad0 + p], 0.05f, 1.f);
            if (now != fPadPrev[p]) { fPadPrev[p] = now; padEvent(p, now); }
        }

        bool moveOn[kNumMoves] = {};
        for (int s = 0; s < kNumSlots; ++s) {
            const int e = effectOf(fParams, s);
            const int m = moveOf(fParams, s);
            const bool smp = hasAudio(s);
            if (smp || (m >= 0 && !moveIsHold(m))) { fLatched[s] = fHeld[s] = false; }
            const bool want = !smp && (latchOf(fParams, s) ? fLatched[s] : fHeld[s]);
            if (!latchOf(fParams, s)) fLatched[s] = false;
            if (m >= 0 && moveIsHold(m) && want) moveOn[m] = true;
            const bool was = fActive[s];
            fActive[s] = want && (e >= 0 || (m >= 0 && moveIsHold(m)));
            if (e < 0) continue;
            // find this pad's voice
            Voice* mine = nullptr;
            for (int k = 0; k < kVoicesPerFx; ++k) if (fVoice[e][k].slot == s && fVoice[e][k].state == kVoiceOn) mine = &fVoice[e][k];
            if (fActive[s] && mine == nullptr) {
                Voice* vc = grabVoice(e, s);
                if (vc) { vc->slot = s; vc->state = kVoiceOn; vc->cut = 1.f; vc->stamp = ++fStampCounter; vc->amtSet = false; }
            } else if (!fActive[s] && mine != nullptr) {
                releaseVoice(*mine, e);
                // DJ Looper SNAP BACK: the speed springs back to +1.00 when the pad lets go
                if (e == kFxDjLooper && fParams[slotParam(s, F_SNAP)] > 0.5f) fLive[slotParam(s, F_K2)] = 1.f;
            }
            (void)was;
        }
        // each pad's amount: live while held, the strongest press for a latched pad
        for (int s = 0; s < kNumSlots; ++s) {
            const int p = padOfSlot(s);
            if (fPadPrev[p] && latchOf(fParams, s) && fLatched[s]) fLatchAmt[s] = std::max(fLatchAmt[s], fPadAmt[p]);
            if (!fLatched[s]) fLatchAmt[s] = 0.f;
        }
        for (int e = 0; e < kNumEffects; ++e)
            for (int k = 0; k < kVoicesPerFx; ++k) {
                Voice& vc = fVoice[e][k];
                if (vc.state != kVoiceOn || vc.slot < 0) continue;
                vc.amtT = slotAmount(vc.slot);
                if (vc.stamp == fStampCounter && vc.cut == 1.f && !vc.amtSet) { vc.amt = vc.amtT; vc.amtSet = true; }
            }
        // voices whose pad changed to something else, or got a sample, let go
        for (int e = 0; e < kNumEffects; ++e)
            for (int k = 0; k < kVoicesPerFx; ++k) {
                Voice& vc = fVoice[e][k];
                if (vc.state == kVoiceOn && (vc.slot < 0 || effectOf(fParams, vc.slot) != e || !fActive[vc.slot])) releaseVoice(vc, e);
            }
        for (int m = kMvDryKill; m <= kMvGrab1; ++m) fMoveOn[m] = moveOn[m];
        fMoveOn[kMvKill] = moveOn[kMvKill];
        fKillOn = moveOn[kMvKill];
        fGrabOn = fMoveOn[kMvGrab8] || fMoveOn[kMvGrab4] || fMoveOn[kMvGrab2] || fMoveOn[kMvGrab1];
    }

    // ---------------------------------------------------------------------------------------------
    // Knobs: absolute knobs pick up before they move anything; endless knobs nudge, faster when spun fast

    int targetOf(int j) const { return jobTarget(j, fFocus, fParams, hasAudio(fFocus)); }

    int knobCountNow() const
    {
        int m[kNumLearn];
        for (int i = 0; i < kNumLearn; ++i) m[i] = isKnobLearn(i) ? fMap[i].load(std::memory_order_relaxed) : -1;
        return knobCount(m, fKnobCountSet.load(std::memory_order_relaxed));
    }

    // fewer knobs than controls: the controls that follow this knob move with it (see foldRule)
    void moveFollowers(int t, float delta)
    {
        if (delta == 0.f || !isSlotKnob(t)) return;
        const int s = slotOf(t), e = effectOf(fParams, s);
        if (e < 0) return;
        const int n = knobCountNow(), nc = numControls(e);
        if (n >= nc) return;
        const int h = fieldOf(t) - F_K1;
        for (int c = n; c < nc; ++c) {
            const Fold f = foldResolve(e, c, n);
            if (f.host != h) continue;
            const int p = slotParam(s, F_K1 + c);
            fLive[p] = clampf(fLive[p] + f.amt * delta, 0.f, 1.f);
        }
    }

    void touchJob(int j, float out)
    {
        fJobOut[j] = out; ++fJobSeq;
        int m = 0; for (int k = 0; k < kNumJobs; ++k) if (fJobPicked[k]) m |= 1 << k;
        fLiveOut.job[j].store(out, std::memory_order_relaxed);
        fLiveOut.picked.store(m, std::memory_order_relaxed);
        fLiveOut.seq.fetch_add(1, std::memory_order_release);
    }

    void jobAbsolute(int j, float n)
    {
        const int t = targetOf(j);
        if (t != fJobTarget[j]) { fJobTarget[j] = t; fJobPicked[j] = false; fJobPrev[j] = -1.f; }
        if (t < 0) { fJobPrev[j] = n; return; }
        const float cur = normOfParam(t, fLive[t]);
        if (fJobPicked[j] && std::fabs(cur - fJobSet[j]) > 0.003f) { fJobPicked[j] = false; fJobPrev[j] = -1.f; }
        if (fKnobJump.load(std::memory_order_relaxed)) fJobPicked[j] = true;     // jump straight to the knob
        if (!fJobPicked[j]) {
            const bool close = std::fabs(n - cur) < 0.04f;
            const bool crossed = fJobPrev[j] >= 0.f && ((fJobPrev[j] - cur) * (n - cur) <= 0.f);
            if (close || crossed) fJobPicked[j] = true;
        }
        fJobPrev[j] = n;
        if (!fJobPicked[j]) { touchJob(j, cur); return; }
        const float s = jobSnap(t, n, fParams);
        fLive[t] = valueOfNorm(t, s);
        moveFollowers(t, normOfParam(t, fLive[t]) - cur);
        fJobSet[j] = s;
        touchJob(j, s);
    }

    void jobRelative(int j, int steps, uint64_t when)
    {
        const int t = targetOf(j);
        if (t < 0) { fJobTarget[j] = t; return; }
        const float cur = normOfParam(t, fLive[t]);
        if (t != fJobTarget[j] || std::fabs(cur - fJobSet[j]) > 0.003f) fJobAcc[j] = cur;
        fJobTarget[j] = t;
        // acceleration: quick clicks move further, so a fast spin sweeps the whole range
        const double dt = (double)(when - fJobLastT[j]) / fFs;
        fJobLastT[j] = when;
        const float accel = dt < 0.025 ? 3.f : (dt < 0.06 ? 2.f : 1.f);
        static const float kBase[3] = { 1.f / 80.f, 1.f / 40.f, 1.f / 24.f };    // slow, normal, fast
        float step = kBase[fKnobSpeed.load(std::memory_order_relaxed)] * accel;
        bool stepped = false;
        if (isSlotKnob(t)) {
            const int e = effectOf(fParams, slotOf(t));
            if (e >= 0) {
                const CtlSpec& c = kEffects[e].c[fieldOf(t) - F_K1];
                if (c.scale == kInt) { step = 1.f / std::max(1.f, c.max - c.min); stepped = true; }
            }
        } else if (t == pThrowTime) { step = 1.f / 12.f; stepped = true; }
        if (stepped) fJobAcc[j] = clampf(cur + (steps > 0 ? 1.f : -1.f) * step, 0.f, 1.f);
        else fJobAcc[j] = clampf(fJobAcc[j] + steps * step, 0.f, 1.f);
        const float s = jobSnap(t, fJobAcc[j], fParams);
        fLive[t] = valueOfNorm(t, s);
        moveFollowers(t, normOfParam(t, fLive[t]) - cur);
        fJobSet[j] = s;
        fJobPicked[j] = true;
        touchJob(j, s);
    }

    // ENDLESS: the knob sends 0-127, but only how far it moved counts. The selected pad's knob moves by that
    // much from wherever it already is, so switching pads never makes a knob jump.
    void jobEndless(int j, int v)
    {
        const int last = fJobAbsLast[j];
        fJobAbsLast[j] = v;
        if (last < 0) return;                       // the first message only tells us where the controller is
        int d = v - last;
        if (d == 0) {
            // turned past the end of the controller's own range: it repeats 0 or 127, so keep going
            if (v == 0) d = -1; else if (v == 127) d = 1; else return;
        }
        d = d > 24 ? 24 : (d < -24 ? -24 : d);      // a big leap (a preset change on the controller) moves only a little
        const int t = targetOf(j);
        if (t < 0) { fJobTarget[j] = t; return; }
        const float cur = normOfParam(t, fLive[t]);
        if (t != fJobTarget[j] || std::fabs(cur - fJobSet[j]) > 0.003f) fJobAcc[j] = cur;
        fJobTarget[j] = t;
        fJobAcc[j] = clampf(fJobAcc[j] + d / 127.f, 0.f, 1.f);
        const float s = jobSnap(t, fJobAcc[j], fParams);
        fLive[t] = valueOfNorm(t, s);
        moveFollowers(t, normOfParam(t, fLive[t]) - cur);
        fJobSet[j] = s;
        fJobPicked[j] = true;
        touchJob(j, s);
    }

    void applyHostKnobs()
    {
        for (int k = 0; k < kNumJobs; ++k) {
            if (!fCtrlDirty[k]) continue;
            fCtrlDirty[k] = false;
            jobAbsolute(k, fCtrl[k]);
        }
    }

    // Which kind of knob is this? A type learned before is used straight away. An unknown knob
    // responds from its very first message: small steps either side of 0 or 64 are treated as an
    // endless knob right away while the pattern is confirmed, anything else as a normal knob.
    int knobModeFor(int j, int v)
    {
        const int global = fKnobMode.load(std::memory_order_relaxed);
        if (global != kKnobAuto) return global;
        if (fJobDetect[j] == 0 || fJobDetect[j] == kKnobAbs) {
            if (v >= 1 && v <= 15) fJobSeenLow[j] = true;
            if (v >= 113) fJobSeenHigh[j] = true;
            if (v >= 65 && v <= 79) fJobSeenMid[j] = true;
            if (fJobDetect[j] == 0) {
                if (v == fJobLastVal[j] && v != 0 && v != 64) {
                    if (v <= 15 || v >= 113) fJobDetect[j] = (fJobSeenMid[j] && !fJobSeenHigh[j]) ? kKnobRel3 : kKnobRel1;
                    else if (v >= 49 && v <= 79) fJobDetect[j] = fJobSeenLow[j] ? kKnobRel3 : kKnobRel2;
                } else if (fJobLastVal[j] >= 0 && v != fJobLastVal[j] && ((v >= 16 && v <= 48) || (v >= 80 && v <= 112))) {
                    fJobDetect[j] = kKnobAbs;     // a sweep through the middle: a normal knob
                }
            }
        }
        fJobLastVal[j] = v;
        if (fJobDetect[j]) return fJobDetect[j];
        if ((v >= 1 && v <= 7) || v >= 121) return kKnobRel1;
        if (v >= 57 && v <= 71 && v != 64) return kKnobRel2;
        return kKnobAbs;
    }

    void knobMessage(int j, int v, uint64_t when)
    {
        switch (knobModeFor(j, v)) {
        case kKnobRel1: jobRelative(j, v < 64 ? v : v - 128, when); break;
        case kKnobRel2: jobRelative(j, v - 64, when); break;
        case kKnobRel3: jobRelative(j, v < 64 ? v : -(v - 64), when); break;
        case kKnobEndAbs: jobEndless(j, v); break;
        default: jobAbsolute(j, v / 127.f); break;
        }
    }

    // ---------------------------------------------------------------------------------------------
    // MIDI

    // follow mode: a bank A pad plays the same spot on the bank on screen; its release goes where its press went
    int followPad(int s, bool press)
    {
        if (s >= kPadsPerPage) return s;
        if (press) {
            if (fFollowHeld[s] >= 0 && fFollowHeld[s] != s) fPadMidi[fFollowHeld[s]] = false;   // re-pressed on another bank
            fFollowHeld[s] = fFollow.load(std::memory_order_relaxed) ? fBank.load(std::memory_order_relaxed) * kPadsPerPage + s : s;
            return fFollowHeld[s];
        }
        const int p = fFollowHeld[s] >= 0 ? fFollowHeld[s] : s;
        fFollowHeld[s] = -1;
        return p;
    }
    void padFromMidi(int p, bool now, bool viaNoteMap, int vel = 127)
    {
        if (viaNoteMap) fPadNote[p] = now; else fPadMidi[p] = now;
        if (now) {
            fPadHit = p; ++fPadHitSeq;
            fPadAmt[p] = fPadFeel.load(std::memory_order_relaxed) == 2 ? 1.f : std::max(0.05f, vel / 127.f);
        }
    }
    // aftertouch moves a held pad's amount (Velocity + Pressure mode only)
    void padPressure(int p, int value)
    {
        if (p < 0 || p >= kNumPads || !(fPadMidi[p] || fPadNote[p])) return;
        fPadAmt[p] = std::max(0.05f, value / 127.f);
    }

    void handleMidi(const MidiEvent& e, const int* map, int noteStart)
    {
        const uint8_t* d = e.size > MidiEvent::kDataSize ? e.dataExt : e.data;
        if (e.size < 2) return;
        const uint8_t status = d[0] & 0xF0;
        const int ch = d[0] & 0x0F;
        const uint8_t d1 = d[1] & 0x7F;
        const uint8_t d2 = e.size > 2 ? (d[2] & 0x7F) : 0;

        const bool noteOn = status == 0x90 && d2 > 0;
        const bool noteOff = status == 0x80 || (status == 0x90 && d2 == 0);
        const bool cc = status == 0xB0;
        if (status == 0xA0 || status == 0xD0) {           // poly aftertouch (per pad) or channel pressure (all held pads)
            ++fMidiSeq;
            if (fPadFeel.load(std::memory_order_relaxed) != 0) return;
            if (status == 0xD0) { for (int p = 0; p < kNumPads; ++p) padPressure(p, d1); return; }
            const int code = midiCode(false, ch, d1);
            bool found = false;
            for (int s = 0; s < kNumPads; ++s)
                if (map[kLearnPad0 + s] >= 0 && (map[kLearnPad0 + s] & 0x1FFFF) == (code & 0x1FFFF)) {
                    padPressure(s < kPadsPerPage && fFollowHeld[s] >= 0 ? fFollowHeld[s] : s, d2); found = true;
                }
            if (!found && noteStart >= 0 && d1 >= noteStart && d1 < noteStart + kNumPads)
                padPressure(notePad(d1 - noteStart, fNoteOrder.load(std::memory_order_relaxed) == 1), d2);
            return;
        }
        if (!noteOn && !noteOff && !cc) return;
        ++fMidiSeq;
        if (cc && (d1 == 120 || d1 == 123)) { releaseMidiHeld(); return; }   // All Sound Off / All Notes Off
        const uint64_t when = fSampleClock + e.frame;

        // Note Repeat: one pad firing many times, faster than fingers do
        if (noteOn) {
            const int code = midiCode(false, ch, d1);
            if (code == fRepCode && (double)(when - fRepLastT) < 0.15 * fFs) {
                if (++fRepCount >= 6) { ++fRepeatSeq; fRepCount = 0; }
            } else { fRepCode = code; fRepCount = 1; }
            fRepLastT = when;
        }

        const int learn = fLearnSlot.load(std::memory_order_relaxed);
        if (learn >= 0) {
            const bool knobSlot = isKnobLearn(learn);
            const bool padLike = noteOn || (cc && d2 > 0 && fCcPrev[ch * 128 + d1] == 0);
            if ((knobSlot && cc) || (!knobSlot && padLike)) {
                const int code = midiCode(cc, ch, d1);
                if (fLearnGuard.load(std::memory_order_relaxed)) {
                    for (int s = 0; s < kNumLearn; ++s)
                        if (s != learn && fMap[s].load() == code) {
                            if (code == fLastLearnedCode && (double)(when - fLastLearnedAt) < 1.5 * fFs) {
                                if (cc) fCcPrev[ch * 128 + d1] = d2;
                                if (cc && isKnobLearn(s)) knobModeFor(s - kLearnKnob0, d2);   // learn what kind of knob it is
                                return;
                            }
                            fLearnDup = s; ++fLearnDupSeq;
                            if (cc) fCcPrev[ch * 128 + d1] = d2;
                            fMidiState = 1;
                            return;
                        }
                }
                for (int s = 0; s < kNumLearn; ++s)
                    if (fMap[s].load() == code) fMap[s].store(-1);
                fMap[learn].store(code);
                fLastLearnedCode = code;
                fLastLearnedAt = when;
                fLearnedVal = (float)((learn + 1) * 262144 + (code & 0x3FFFF));
                ++fLearnSeq;
                fLearnSlot.store(-1);
                if (knobSlot) {
                    const int j = learn - kLearnKnob0;
                    fJobDetect[j] = 0; fJobLastVal[j] = -1; fJobAbsLast[j] = -1; fJobSeenLow[j] = fJobSeenHigh[j] = fJobSeenMid[j] = false;
                    knobModeFor(j, d2);
                }
                if (cc) fCcPrev[ch * 128 + d1] = d2;
                fMidiState = 1;
                return;
            }
        }

        bool handled = false;
        const int code = midiCode(cc, ch, d1);
        for (int s = 0; s < kNumLearn; ++s) {
            const int mc = map[s];
            if (mc < 0) continue;
            const bool isNote = (mc & 0x10000) && (mc & 0xFFF) == (code & 0xFFF) && (noteOn || noteOff);
            const bool isCc = (mc & 0x20000) && (mc & 0xFFF) == (code & 0xFFF) && cc;
            if (!isNote && !isCc) continue;
            handled = true;
            if (isKnobLearn(s)) { if (isCc) knobMessage(s - kLearnKnob0, d2, when); continue; }
            const bool press = isNote ? noteOn : (d2 > 0);
            if (s >= kLearnBank0) { if (press) fBank.store(s - kLearnBank0); continue; }   // a bank button
            padFromMidi(followPad(s, press), press, false, d2);
        }

        // optional note map: notes in a row from the start note, page by page; a learned pad wins
        if (!handled && noteStart >= 0 && (noteOn || noteOff) && d1 >= noteStart && d1 < noteStart + kNumPads) {
            const int pad = notePad(d1 - noteStart, fNoteOrder.load(std::memory_order_relaxed) == 1);
            if (map[pad] < 0) { padFromMidi(pad, noteOn, true, d2); handled = true; }
        }
        if (!handled && (noteOn || (cc && d2 > 0))) { fMidiLast = code; ++fMidiLastSeq; }
        if (cc) fCcPrev[ch * 128 + d1] = d2;
        fMidiState = handled ? 1 : 2;
    }

    // ---------------------------------------------------------------------------------------------

    float fParams[kParamCount];
    float fLive[kParamCount];
    std::atomic<int> fMap[kNumLearn];
    std::atomic<int> fLearnSlot, fNoteStart, fNoteOrder, fKnobMode, fKnobSpeed { 1 }, fSavedType[kNumJobs];
    std::atomic<bool> fKnobJump { false };
    std::atomic<int> fKnobCountSet { 0 };       // 0 = count the linked knobs
    std::atomic<int> fPadFeel { 0 };
    std::atomic<bool> fFollow { false };        // bank A's pads play the bank on screen
    std::atomic<int> fBank { 0 };               // the bank on screen (the UI's tabs, or a learned bank button)
    int fFollowHeld[kPadsPerPage];              // follow mode: the pad each bank A pad started, so its release finds it
    float fPadAmt[kNumPads] = {}, fLatchAmt[kNumSlots] = {}, fAmtCoef = 0.01f;
    std::atomic<bool> fAllOffReq, fKnobModeDirty { false }, fReorderReq { false }, fLearnGuard;
    std::atomic<int> fReorder[kNumSlots];
    std::string fAllOffLast = "0", fCtlLast;
    int fSelectReq = -1;

    bool fLatched[kNumSlots], fHeld[kNumSlots], fActive[kNumSlots];
    unsigned fStampCounter = 0;
    bool fPadMidi[kNumPads], fPadNote[kNumPads], fPadPrev[kNumPads];
    bool fMoveOn[kNumMoves];
    int fFocus = 2;
    int fGrabLen = 3;
    bool fGrabOn = false, fGrid = false, fKillOn = false;
    SamplePad fSmp[kNumSlots];
    float fSmpPos[kNumSlots];

    // knobs
    int fJobTarget[kNumJobs], fJobLastVal[kNumJobs], fJobDetect[kNumJobs], fJobAbsLast[kNumJobs];
    float fJobPrev[kNumJobs], fJobSet[kNumJobs], fJobAcc[kNumJobs], fJobOut[kNumJobs];
    bool fJobPicked[kNumJobs], fJobSeenLow[kNumJobs], fJobSeenHigh[kNumJobs], fJobSeenMid[kNumJobs];
    uint64_t fJobLastT[kNumJobs];
    float fCtrl[kNumJobs]; bool fCtrlDirty[kNumJobs], fCtrlSeen[kNumJobs] = {};
    int fJobSeq = 0;
    int fCcPrev[16 * 128];

    // status for the UI
    float fLearnedVal = 0.f, fLevel = 0.f, fGrabPhase = -1.f, fFocusPhase = -1.f;
    int fLearnSeq = 0, fMidiSeq = 0, fMidiState = 0, fPadHit = -1, fPadHitSeq = 0, fVoicesBusy = 0;
    int fLearnDup = -1, fLearnDupSeq = 0, fMidiLast = -1, fMidiLastSeq = 0, fRepeatSeq = 0;
    int fRepCode = -1, fRepCount = 0, fLastLearnedCode = -1;
    uint64_t fRepLastT = 0, fSampleClock = 0, fLastLearnedAt = 0;
    bool fNanGuard = false;

    // clock
    float fFs = 44100.f;
    double fBpm = 120.0, fBeat = 0.0;
    long fLast16 = LONG_MIN;

    YanceyLive fLiveOut;

    // beat pads
    BeatPad fBeats[kNumSlots];
    std::atomic<bool> fBeatReady[kNumSlots] {}, fBeatHas[kNumSlots] {};
    std::vector<BeatStoreEntry> fBeatStore;          // guarded by fLoadMutex
    BeatData* fBeatCurData = nullptr; BeatData* fBeatOldData = nullptr;   // audio thread
    std::atomic<BeatData*> fPlayCur { nullptr }, fPlayOld { nullptr };    // what's playing, for the store
    std::atomic<int> fBeatMode { 0 };
    int fBeatCur = -1, fBeatOld = -1, fBeatPending = -1;
    double fBeatPos = 0.0, fBeatOldPos = 0.0, fHostBpm = 120.0;
    float fBeatIn = 0.f, fBeatInStep = 0.01f, fBeatOldGain = 0.f, fBeatOldStep = 0.01f;
    Smooth fBeatVol;
    std::thread fLoader;
    std::mutex fLoadMutex;
    std::condition_variable fLoadCv;
    std::deque<std::pair<int, std::string> > fLoadQueue;
    bool fLoaderQuit = false;

    // signal chain
    FxPool fPool;
    Voice fVoice[kNumEffects][kVoicesPerFx];
    BpmLooper fGrab;
    DjfxLooper fFreeze;
    Delay fThrow;
    Ramp fInGate;
    Smooth fOutSm, fTailSm;
    float fKillGain = 1.f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(YanceyPlugin)
};

Plugin* createPlugin()
{
    return new YanceyPlugin();
}

END_NAMESPACE_DISTRHO
