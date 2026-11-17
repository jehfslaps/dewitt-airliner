// Airliner: Dewitt's DJ Looper (A) and Vinyl Sim (B) in one effect, in series.
// The engines are Dewitt's tape looper and record-deck vinyl sim (YanceyDSP.hpp).
// LOOP grabs a loop the moment it goes on and drops it when it goes off. ON fades the vinyl sim in and out;
// the vinyl sim keeps running underneath while it's off, so turning it on starts settled on the music.
// ORDER swaps the two (vinyl first, then the loop of the vinyl'd sound). OUT and LIMIT work like Dewitt's.
// A MIDI controller can be linked (right-click a control): a pad for LOOP, a pad for ON, and up to six knobs.
// With a small controller its first three knobs follow the side you last pressed (LOOP: LENGTH, SPEED;
// ON: COMP, NOISE, WOW FLUT); with six or more, each knob gets its own control. Knobs pick up: one does
// nothing until it reaches the control's current value, so nothing jumps. The links are saved on this
// computer, not in the project (the same controller works in every project).
#include "DistrhoPlugin.hpp"
#include "AirlinerShared.hpp"
#include "YanceyDSP.hpp"
#include "YanceyPlatform.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

using namespace airliner;
using yancey::BlockCtx;
using yancey::Ramp;
using yancey::Smooth;


// ---------------------------------------------------------------------------------------------

static const char* const kHoldLatch[2] = { "Hold", "Latch" };
static const char* const kOrders[2] = { "Loop > Vinyl", "Vinyl > Loop" };

class AirlinerPlugin : public Plugin
{
public:
    AirlinerPlugin() : Plugin(kParamCount, 0, kNumStates)
    {
        for (int i = 0; i < kParamCount; ++i) fP[i] = kSpecs[i].def;
        for (int i = 0; i < kNumLearn; ++i) fMap[i].store(-1);
        std::memset(fCcPrev, 0, sizeof(fCcPrev));
        for (int j = 0; j < kNumCtlKnobs; ++j) { fKTarget[j] = -2; fKPicked[j] = false; fKPrev[j] = -1.f; fKSet[j] = 0.f; }
        loadControllerSetup();
        {
            static std::atomic<int> sCount { 0 };
            fNonce = 1 + (int)((std::chrono::steady_clock::now().time_since_epoch().count() / 1000 + 7919 * ++sCount) % 999983);
        }
        fPhase.store(-1.f);
        fB.warm = true;       // keep running while off
        prepareAll(getSampleRate());
    }

protected:
    const char* getLabel() const override
    {
        return kKind == kKindLooper ? "AirlinerLooper" : (kKind == kKindVinyl ? "AirlinerVinyl" : "Airliner");
    }
    const char* getDescription() const override
    {
        const char* d = kKind == kKindLooper ? "The DJ Looper: grab a slice of the beat and bend the tape."
                      : kKind == kKindVinyl ? "The Vinyl Sim: compression, surface noise, wow and flutter."
                      : "DJ Looper and Vinyl Sim side by side: grab a tape loop, then run it through an old record.";
        return d;
    }
    const char* getMaker() const override { return "slapworx"; }
    const char* getHomePage() const override { return "https://slapworx.net/"; }
    const char* getLicense() const override { return "Proprietary"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }
    int64_t getUniqueId() const override
    {
        return kKind == kKindLooper ? d_cconst('A', 'l', 'p', 'S') : (kKind == kKindVinyl ? d_cconst('A', 'v', 'n', 'S') : d_cconst('A', 'i', 'r', 'l'));
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(const uint32_t index, Parameter& p) override
    {
        const ParamSpec& s = kSpecs[index];
        p.name = s.name; p.symbol = s.symbol;
        p.ranges.min = s.min; p.ranges.max = s.max; p.ranges.def = s.def;
        p.hints = kParameterIsAutomatable;
        if (s.integer) p.hints |= kParameterIsInteger;
        if (s.toggle) p.hints |= kParameterIsBoolean;
        if (isOutput((int)index)) p.hints = kParameterIsOutput;
        if (index > oPhase || !inBuild((int)index)) p.hints |= kParameterIsHidden;
        if (isGain((int)index)) p.unit = "dB";
        if (index == pPadMode) setEnum(p, kHoldLatch, 2);
        if (index == pOrder) setEnum(p, kOrders, 2);
    }

    static void setEnum(Parameter& p, const char* const* names, int count)
    {
        ParameterEnumerationValue* const vals = new ParameterEnumerationValue[count];
        for (int i = 0; i < count; ++i) { vals[i].value = (float)i; vals[i].label = names[i]; }
        p.enumValues.count = (uint8_t)count;
        p.enumValues.restrictedMode = true;
        p.enumValues.values = vals;
    }

    float getParameterValue(const uint32_t index) const override
    {
        switch (index) {
        case oPhase: return fPhase.load();
        case oSide: return (float)fSide.load();
        case oLearnSeq: return (float)fLearnSeq;
        case oLearned: return fLearnedVal;
        case oCtlSeq: return (float)fCtlSeq;
        case oTouch: return fTouch;
        case oNonce: return (float)fNonce;
        case oMeterInA: return fMeter[0].load();
        case oMeterOutA: return fMeter[1].load();
        case oMeterInB: return fMeter[2].load();
        case oMeterOutB: return fMeter[3].load();
        default: break;
        }
        if (index >= (uint32_t)oMirror0 && index < (uint32_t)kParamCount) return fP[kMirrorParam[index - oMirror0]];
        return index < (uint32_t)kParamCount ? fP[index] : 0.f;
    }
    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= (uint32_t)kParamCount || isOutput((int)index)) return;
        fP[index] = clampP((int)index, value);
    }

    enum { kStatePreset = 0, kStateLearn, kStateCtl, kStateSide, kNumStates };
    void initState(const uint32_t index, State& state) override
    {
        switch (index) {
        case kStatePreset: state.key = "preset"; state.defaultValue = "Default"; state.label = "Preset name"; state.hints = kStateIsOnlyForUI; break;
        case kStateLearn:  state.key = "learn"; state.defaultValue = "-1"; state.label = "Controller learn"; break;
        case kStateCtl:    state.key = "ctl"; state.defaultValue = "0"; state.label = "Controller setup"; break;
        default:           state.key = "side"; state.defaultValue = "0"; state.label = "Controller knob side"; break;
        }
    }
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "learn") == 0) {
            // "slot:instance" from this instance's window; anything else (an old value from a project) stops learning
            const int slot = std::atoi(value);
            const char* colon = std::strchr(value, ':');
            const bool mine = colon != nullptr && std::atoi(colon + 1) == fNonce;
            fLearn.store(mine && slot >= 0 && slot < kNumLearn ? slot : -1);
        } else if (std::strcmp(key, "ctl") == 0) {
            loadControllerSetup();          // the window saved a change on this computer: read it again
        } else if (std::strcmp(key, "side") == 0) {
            fSide.store(sideFor(std::atoi(value) == 1 ? 1 : 0));
        }
    }

    // this computer's controller setup (Airliner's own settings file): links, pickup, knob layout
    void loadControllerSetup()
    {
        yancey::Prefs pr;
        int m[kNumLearn];
        for (int i = 0; i < kNumLearn; ++i) m[i] = -1;
        if (yancey::readPrefs(pr)) {
            const char* c = pr.map.c_str();
            for (int i = 0; i < kNumLearn && *c; ++i) {
                m[i] = std::atoi(c);
                while (*c && *c != ',') ++c;
                if (*c == ',') ++c;
            }
            fJump.store(pr.kjump == 1);         // pickup is on unless switched off
            fOneEach.store(pr.kgrid == 1);      // 1: one knob per control
        }
        for (int i = 0; i < kNumLearn; ++i) fMap[i].store(m[i]);
    }

    void sampleRateChanged(const double sr) override { prepareAll(sr); }
    void activate() override { prepareAll(getSampleRate()); }

    void prepareAll(double sr)
    {
        fFs = (float)sr;
        fA.prepare(fFs);
        fB.prepare(fFs);
        fOutSm.setTime(fFs, 0.01f); fOutSm.y = gainOf(pOut, fP[pOut]);
        for (int k = 0; k < 4; ++k) { static const int gp[4] = { pInA, pOutA, pInB, pOutB }; fGainSm[k].setTime(fFs, 0.01f); fGainSm[k].y = gainOf(gp[k], fP[gp[k]]); }
        fOrderStep = 1.f / std::max(1.f, 0.006f * fFs);
        fOrderGain = 1.f; fOrderNow = fP[pOrder] > 0.5f;
        setLatency(0);
    }


    // --------------------------------------------------------------------------------------------
    // the controller

    void handleMidi(const MidiEvent& e)
    {
        const uint8_t* d = e.size > MidiEvent::kDataSize ? e.dataExt : e.data;
        if (e.size < 2) return;
        const uint8_t status = d[0] & 0xF0;
        const int ch = d[0] & 0x0F;
        const int d1 = d[1] & 0x7F;
        const int d2 = e.size > 2 ? (d[2] & 0x7F) : 0;
        const bool noteOn = status == 0x90 && d2 > 0;
        const bool noteOff = status == 0x80 || (status == 0x90 && d2 == 0);
        const bool cc = status == 0xB0;
        if (!noteOn && !noteOff && !cc) return;
        const int prev = cc ? fCcPrev[ch * 128 + d1] : 0;
        if (cc) fCcPrev[ch * 128 + d1] = (uint8_t)d2;

        const int learn = fLearn.load(std::memory_order_relaxed);
        if (learn >= 0) {
            const bool knobSlot = learn >= kLearnKnob0;
            const bool padLike = noteOn || (cc && d2 > 0 && prev == 0);
            if ((knobSlot && cc) || (!knobSlot && padLike)) {
                const int code = midiCode(cc, ch, d1);
                for (int i = 0; i < kNumLearn; ++i) if (fMap[i].load() == code) fMap[i].store(-1);
                fMap[learn].store(code);
                fLearnedVal = (float)((learn + 1) * 32768 + code);
                ++fLearnSeq;
                fLearn.store(-1);
                if (knobSlot) { const int j = learn - kLearnKnob0; fKTarget[j] = -2; fKPicked[j] = false; fKPrev[j] = -1.f; }
                fPadDown[0] = fPadDown[1] = false;
            }
            return;
        }

        const int code = midiCode(cc, ch, d1);
        // pads: a note, or a CC that goes above zero and back
        for (int s = kLearnLoop; s <= kLearnVinyl; ++s) {
            if (fMap[s].load(std::memory_order_relaxed) != code) continue;
            if ((s == kLearnLoop && !kHasLooper) || (s == kLearnVinyl && !kHasVinyl)) continue;
            const bool down = cc ? d2 > 0 : noteOn;
            if (down == fPadDown[s]) continue;
            fPadDown[s] = down;
            if (s == kLearnLoop) {
                if (down) {
                    fSide.store(sideFor(0));
                    if (fP[pPadMode] > 0.5f) fP[pLoop] = fP[pLoop] > 0.5f ? 0.f : 1.f;     // LATCH: press toggles
                    else fP[pLoop] = 1.f;                                                     // HOLD: on while held
                } else if (fP[pPadMode] <= 0.5f) fP[pLoop] = 0.f;
            } else if (down) {
                fSide.store(sideFor(1));
                fP[pVinyl] = fP[pVinyl] > 0.5f ? 0.f : 1.f;
            }
            fTouch = (float)((s == kLearnLoop ? pLoop : pVinyl) * 4);
            ++fCtlSeq;
        }
        // knobs
        if (cc) {
            for (int j = 0; j < kNumCtlKnobs; ++j)
                if (fMap[kLearnKnob0 + j].load(std::memory_order_relaxed) == code) knobAbsolute(j, d2 / 127.f);
        }
    }

    // a knob with endpoints: it takes over once it reaches the control's value (or straight away, with pickup off)
    void knobAbsolute(int j, float n)
    {
        const int t = knobTarget(j, fOneEach.load(std::memory_order_relaxed), fSide.load(std::memory_order_relaxed));
        if (t != fKTarget[j]) { fKTarget[j] = t; fKPicked[j] = false; fKPrev[j] = -1.f; }
        if (t < 0) { fKPrev[j] = n; return; }
        const float cur = normOf(t, fP[t]);
        if (fKPicked[j] && std::fabs(cur - fKSet[j]) > 0.003f) { fKPicked[j] = false; fKPrev[j] = -1.f; }   // moved by something else
        if (fJump.load(std::memory_order_relaxed)) fKPicked[j] = true;
        if (!fKPicked[j]) {
            const bool close = std::fabs(n - cur) < 0.04f;
            const bool crossed = fKPrev[j] >= 0.f && (fKPrev[j] - cur) * (n - cur) <= 0.f;
            if (close || crossed) fKPicked[j] = true;
        }
        fKPrev[j] = n;
        if (!fKPicked[j]) {
            fTouch = (float)(t * 4 + (n < cur ? 1 : 2));
            ++fCtlSeq;
            return;
        }
        const float v = valueOf(t, n);
        fP[t] = v;
        fKSet[j] = normOf(t, v);
        fTouch = (float)(t * 4);
        ++fCtlSeq;
    }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midi, uint32_t midiCount) override
    {
        for (uint32_t m = 0; m < midiCount; ++m) handleMidi(midi[m]);

        const float* inL = inputs[0];
        const float* inR = inputs[1];
        float* outL = outputs[0];
        float* outR = outputs[1];

        BlockCtx ctx; ctx.fs = fFs;
        const bool loopOn = fP[pLoop] > 0.5f, vinOn = fP[pVinyl] > 0.5f, limit = fP[pLimit] > 0.5f;
        const bool wantOrder = fP[pOrder] > 0.5f;
        fA.block(ctx, yancey::djTapeMs(fP[pLen]), fP[pSpeed], 100.f, 30.f);     // MIX and GLIDE as in Dewitt
        fB.block(ctx, fP[pComp], fP[pNoise], fP[pFlu]);
        const float og = gainOf(pOut, fP[pOut]);
        const float gIn[2] = { gainOf(pInA, fP[pInA]), gainOf(pInB, fP[pInB]) };
        const float gOut[2] = { gainOf(pOutA, fP[pOutA]), gainOf(pOutB, fP[pOutB]) };
        float pk[4] = { 0.f, 0.f, 0.f, 0.f };

        for (uint32_t i = 0; i < frames; ++i) {
            float l = inL[i], r = inR[i];
            // ORDER: dip the output for a moment, swap, come back up
            if (wantOrder != fOrderNow) {
                fOrderGain -= fOrderStep;
                if (fOrderGain <= 0.f) { fOrderGain = 0.f; fOrderNow = wantOrder; }
            } else if (fOrderGain < 1.f) { fOrderGain += fOrderStep; if (fOrderGain > 1.f) fOrderGain = 1.f; }

            // each side: INPUT fader, the effect, OUTPUT fader (with their meters)
            auto sideA = [&](float& a, float& b) {
                const float gi = fGainSm[0].tick(gIn[0]); a *= gi; b *= gi;
                pk[0] = std::max(pk[0], std::max(std::fabs(a), std::fabs(b)));
                fA.process(a, b, loopOn);
                const float go = fGainSm[1].tick(gOut[0]); a *= go; b *= go;
                pk[1] = std::max(pk[1], std::max(std::fabs(a), std::fabs(b)));
            };
            auto sideB = [&](float& a, float& b) {
                const float gi = fGainSm[2].tick(gIn[1]); a *= gi; b *= gi;
                pk[2] = std::max(pk[2], std::max(std::fabs(a), std::fabs(b)));
                fB.process(a, b, vinOn);
                const float go = fGainSm[3].tick(gOut[1]); a *= go; b *= go;
                pk[3] = std::max(pk[3], std::max(std::fabs(a), std::fabs(b)));
            };
            if (kKind == kKindLooper) sideA(l, r);
            else if (kKind == kKindVinyl) sideB(l, r);
            else if (!fOrderNow) { sideA(l, r); sideB(l, r); }
            else { sideB(l, r); sideA(l, r); }

            const float g = fOutSm.tick(og) * yancey::smoothstep01(fOrderGain);
            l *= g; r *= g;
            if (limit) { l = yancey::softCeil(l); r = yancey::softCeil(r); }
            else {
                if (std::fabs(l) > 1.f) l = (l > 0.f ? 1.f : -1.f) * (1.f + std::tanh(std::fabs(l) - 1.f));
                if (std::fabs(r) > 1.f) r = (r > 0.f ? 1.f : -1.f) * (1.f + std::tanh(std::fabs(r) - 1.f));
            }
            if (!std::isfinite(l) || !std::isfinite(r)) { l = r = 0.f; }
            outL[i] = l; outR[i] = r;
        }
        fPhase.store(kHasLooper ? fA.phase : -1.f);
        for (int k = 0; k < 4; ++k) fMeter[k].store(pk[k]);     // this block's peak (the window holds and falls)
    }

private:
    float fP[kParamCount];
    float fFs = 44100.f;
    std::atomic<float> fPhase;
    yancey::TapeLooper fA;
    yancey::VinylDeck fB;
    Smooth fOutSm;
    Smooth fGainSm[4];                  // INPUT and OUTPUT faders, A then B
    std::atomic<float> fMeter[4] { { 0.f }, { 0.f }, { 0.f }, { 0.f } };
    // controller
    std::atomic<int> fMap[kNumLearn];
    std::atomic<int> fLearn { -1 }, fSide { 0 };
    std::atomic<bool> fJump { false }, fOneEach { false };
    uint8_t fCcPrev[16 * 128];
    bool fPadDown[2] = { false, false };
    int fKTarget[kNumCtlKnobs];
    bool fKPicked[kNumCtlKnobs];
    float fKPrev[kNumCtlKnobs], fKSet[kNumCtlKnobs];
    int fLearnSeq = 0, fCtlSeq = 0, fNonce = 1;
    float fLearnedVal = 0.f, fTouch = 0.f;
    float fOrderGain = 1.f, fOrderStep = 0.001f;
    bool fOrderNow = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AirlinerPlugin)
};

Plugin* createPlugin() { return new AirlinerPlugin(); }

END_NAMESPACE_DISTRHO
