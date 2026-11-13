// Airliner test host: loads Airliner.clap (or Airliner Looper / Vinyl) and checks the audio.
#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/ext/params.h"
#include "clap/ext/latency.h"
#include "clap/ext/state.h"
#include "portable.hpp"      // loading a CLAP (a Mac .clap is a bundle) and folders, per platform
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include "../airliner/AirlinerShared.hpp"
#include "../plugin/YanceyDSP.hpp"
using namespace airliner;

static void noop(const clap_host*) {}
static const void* get_ext(const clap_host*, const char*) { return nullptr; }
static clap_host host = { CLAP_VERSION, nullptr, "atest", "t", "", "1", get_ext, noop, noop, noop };

union Ev { clap_event_header h; clap_event_param_value pv; clap_event_midi midi; };
struct EvList { std::vector<Ev> ev; };
static uint32_t ev_size(const clap_input_events* l) { return (uint32_t)((EvList*)l->ctx)->ev.size(); }
static const clap_event_header* ev_get(const clap_input_events* l, uint32_t i) { return &((EvList*)l->ctx)->ev[i].h; }
static bool out_push(const clap_output_events*, const clap_event_header*) { return true; }

static const clap_plugin* P;
static const clap_plugin_params* PR;
static EvList gEv;
static const int kBlock = 256;
static double gSr = 48000.0, gT = 0.0;
static uint32_t gRng = 12345;
static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); ++failures; } else printf("ok   %s\n", msg); } while (0)

static void pushParam(int id, double v) { Ev e{}; e.pv.header = { sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 }; e.pv.param_id = (clap_id)id; e.pv.note_id = -1; e.pv.port_index = -1; e.pv.channel = -1; e.pv.key = -1; e.pv.value = v; gEv.ev.push_back(e); }

static void pushMidi(uint8_t a, uint8_t b, uint8_t c) { Ev e{}; e.midi.header = { sizeof(clap_event_midi), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0 }; e.midi.port_index = 0; e.midi.data[0] = a; e.midi.data[1] = b; e.midi.data[2] = c; gEv.ev.push_back(e); }
struct Buf { std::vector<uint8_t> d; size_t pos = 0; };
static int64_t ws_write(const clap_ostream* s, const void* b, uint64_t n) { auto* B = (Buf*)s->ctx; B->d.insert(B->d.end(), (const uint8_t*)b, (const uint8_t*)b + n); return (int64_t)n; }
static int64_t is_read(const clap_istream* s, void* b, uint64_t n) { auto* B = (Buf*)s->ctx; uint64_t k = std::min<uint64_t>(n, B->d.size() - B->pos); memcpy(b, B->d.data() + B->pos, k); B->pos += k; return (int64_t)k; }
static void genInput(float* l, float* r, int n, float gain)
{
    for (int i = 0; i < n; ++i) {
        const double t = gT; gT += 1.0 / gSr;
        const double bt = std::fmod(t, 0.5);
        const double kick = std::sin(2 * M_PI * (50 + 120 * std::exp(-bt * 30)) * bt) * std::exp(-bt * 9);
        const double chord = 0.25 * (std::sin(2 * M_PI * 220 * t) + std::sin(2 * M_PI * 277.2 * t) + std::sin(2 * M_PI * 329.6 * t));
        gRng = gRng * 1664525u + 1013904223u;
        const double hat = (((gRng >> 9) & 0xffff) / 32768.0 - 1.0) * std::exp(-std::fmod(t, 0.25) * 60) * 0.3;
        const double x = gain * (0.6 * kick + 0.3 * chord + hat);
        l[i] = (float)x; r[i] = (float)(x * 0.97);
    }
}

struct Rec { std::vector<float> inL, inR, outL, outR; };
static Rec gRec;
static bool gRecord = false;

struct Stats { float peak = 0, diff = 0, maxDev = 0; bool finite = true; };
// diff: rms of (out - input delayed by lat)
static Stats run(double seconds, float gain = 0.5f, int lat = 0, float expect = 1.f)
{
    Stats s; double sqd = 0; long n = 0;
    const int blocks = (int)std::lround(seconds * gSr / kBlock);
    std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
    static std::vector<float> hist(1 << 16); static int hw = 0;
    for (int b = 0; b < blocks; ++b) {
        genInput(inL.data(), inR.data(), kBlock, gain);
        float* ins[2] = { inL.data(), inR.data() }; float* outs[2] = { outL.data(), outR.data() };
        clap_audio_buffer ib{}; ib.data32 = ins; ib.channel_count = 2;
        clap_audio_buffer ob{}; ob.data32 = outs; ob.channel_count = 2;
        clap_input_events ie = { &gEv, ev_size, ev_get };
        clap_output_events oe = { nullptr, out_push };
        clap_process pr{}; pr.audio_inputs = &ib; pr.audio_outputs = &ob; pr.audio_inputs_count = 1; pr.audio_outputs_count = 1;
        pr.frames_count = kBlock; pr.in_events = &ie; pr.out_events = &oe; pr.transport = nullptr; pr.steady_time = -1;
        std::vector<float> keepL = inL, keepR = inR;
        P->process(P, &pr);
        gEv.ev.clear();
        for (int i = 0; i < kBlock; ++i) {
            hist[hw & 0xffff] = keepL[i];
            const float dry = hist[(hw - lat) & 0xffff] * expect;
            ++hw;
            const float o = outL[i], o2 = outR[i];
            if (!std::isfinite(o) || !std::isfinite(o2)) s.finite = false;
            s.peak = std::max(s.peak, std::max(std::fabs(o), std::fabs(o2)));
            sqd += (o - dry) * (o - dry); ++n;
            s.maxDev = std::max(s.maxDev, std::fabs(o - dry));
        }
        if (gRecord) {
            gRec.inL.insert(gRec.inL.end(), keepL.begin(), keepL.end()); gRec.inR.insert(gRec.inR.end(), keepR.begin(), keepR.end());
            gRec.outL.insert(gRec.outL.end(), outL.begin(), outL.end()); gRec.outR.insert(gRec.outR.end(), outR.begin(), outR.end());
        }
    }
    s.diff = (float)std::sqrt(sqd / std::max(1L, n));
    return s;
}

static const clap_plugin_state* ST;
static std::string saveState() { Buf B; clap_ostream os = { &B, ws_write }; ST->save(P, &os); return std::string(B.d.begin(), B.d.end()); }
static void loadState(const std::string& s) { Buf B; B.d.assign(s.begin(), s.end()); clap_istream is = { &B, is_read }; ST->load(P, &is); }
static bool setStateKey(std::string& s, const char* key, const std::string& val)
{
    size_t pos = s.find(std::string(key) + std::string(1, '\0'));
    if (pos == std::string::npos) return false;
    size_t v0 = pos + strlen(key) + 1, v1 = s.find('\0', v0);
    s.replace(v0, v1 - v0, val);
    return true;
}
static std::string gCfgDir;
static void writePrefsFile(const char* text)
{
    FILE* f = fopen((gCfgDir + "/settings.txt").c_str(), "wb");
    if (f) { fputs(text, f); fclose(f); }
}
static double getv(int id) { double v = 0; PR->get_value(P, (clap_id)id, &v); return v; }
static int latency() { auto* L = (const clap_plugin_latency*)P->get_extension(P, CLAP_EXT_LATENCY); return L ? (int)L->get(P) : -1; }
static void restart(double sr) { P->stop_processing(P); P->deactivate(P); gSr = sr; P->activate(P, sr, 32, kBlock); P->start_processing(P); }

int main(int argc, char** argv)
{
    // a settings folder of our own, with a controller already linked:
    // LOOP pad = note 36, ON pad = note 37, knobs 1-3 = CC 21-23, all on channel 1
    {
        const char* base = argc > 2 ? argv[2] : "/tmp/atest-cfg";
#ifdef _WIN32
        _putenv_s("APPDATA", base);
        gCfgDir = std::string(base) + "\\slapworx\\" + "Airliner";
        CreateDirectoryA(base, nullptr); CreateDirectoryA((std::string(base) + "\\slapworx").c_str(), nullptr); CreateDirectoryA(gCfgDir.c_str(), nullptr);
#elif defined(__APPLE__)
        setenv("HOME", base, 1);
        gCfgDir = std::string(base) + "/Library/Application Support/slapworx/" + "Airliner";
        if (std::system(("mkdir -p '" + gCfgDir + "'").c_str())) {}
#else
        setenv("XDG_CONFIG_HOME", base, 1);
        gCfgDir = std::string(base) + "/slapworx/" + "Airliner";
        if (std::system(("mkdir -p '" + gCfgDir + "'").c_str())) {}
#endif
        char line[200];
        std::snprintf(line, sizeof(line), "map3=%d,%d,%d,%d,%d,-1,-1,-1\n", midiCode(false, 0, 36), midiCode(false, 0, 37), midiCode(true, 0, 21), midiCode(true, 0, 22), midiCode(true, 0, 23));
        writePrefsFile(line);
    }
    void* h = testOpenLib(argv[1]);
    if (!h) { printf("could not load %s\n", argv[1]); return 1; }
    auto* e = (const clap_plugin_entry*)testSym(h, "clap_entry");
    e->init(argv[1]);
    auto* f = (const clap_plugin_factory*)e->get_factory(CLAP_PLUGIN_FACTORY_ID);
    const clap_plugin_descriptor* d = f->get_plugin_descriptor(f, 0);
    printf("plugin: %s (%s)\n", d->name, d->id);
    P = f->create_plugin(f, &host, d->id);
    P->init(P);
    PR = (const clap_plugin_params*)P->get_extension(P, CLAP_EXT_PARAMS);
    ST = (const clap_plugin_state*)P->get_extension(P, CLAP_EXT_STATE);
    P->activate(P, gSr, 32, kBlock);
    P->start_processing(P);

    char msg[200];
#ifndef FW_REF
    {
        // the same engines as Dewitt, the same way: compare sample for sample with Dewitt's DJ Looper and Vinyl Sim
        gRec = Rec(); gRecord = true;
        run(0.5);
        pushParam(pLoop, 1); run(0.8);
        pushParam(pLen, 80); run(0.6);
        pushParam(pVinyl, 1); run(1.0);
        pushParam(pSpeed, -0.5); run(0.7);
        pushParam(pLoop, 0); run(0.5);
        gRecord = false;
        pushParam(pLen, kSpecs[pLen].def); pushParam(pSpeed, kSpecs[pSpeed].def); pushParam(pVinyl, 0); run(0.5);
        yancey::TapeLooper L; yancey::VinylDeck V; V.warm = true;
        L.prepare(48000.f); V.prepare(48000.f);
        yancey::BlockCtx ctx; ctx.fs = 48000.f;
        const long n = (long)gRec.inL.size();
        const long marks[6] = { (long)std::lround(0.5 * 48000 / kBlock), (long)std::lround(0.8 * 48000 / kBlock), (long)std::lround(0.6 * 48000 / kBlock),
                                (long)std::lround(1.0 * 48000 / kBlock), (long)std::lround(0.7 * 48000 / kBlock), (long)std::lround(0.5 * 48000 / kBlock) };
        float len = 50.f, sp = 1.f; bool lp = false, vn = false;
        double worst = 0.0; long blk = 0; int stage = 0; long stageEnd = marks[0];
        for (long i = 0; i < n; i += kBlock, ++blk) {
            while (blk >= stageEnd && stage < 5) {
                ++stage; stageEnd += marks[stage];
                if (stage == 1) lp = true; if (stage == 2) len = 80.f; if (stage == 3) vn = true; if (stage == 4) sp = -0.5f; if (stage == 5) lp = false;
            }
            L.block(ctx, yancey::djTapeMs(len), sp, 100.f, 30.f);
            V.block(ctx, 100.f, 30.f, 17.f);
            for (long j = i; j < i + kBlock && j < n; ++j) {
                float l = gRec.inL[j], r = gRec.inR[j];
                if (kHasLooper) L.process(l, r, lp);
                if (kHasVinyl) V.process(l, r, vn);
                l = yancey::softCeil(l); r = yancey::softCeil(r);
                worst = std::max(worst, (double)std::fabs(l - gRec.outL[j]));
                worst = std::max(worst, (double)std::fabs(r - gRec.outR[j]));
            }
        }
        std::snprintf(msg, sizeof(msg), kKind == kKindLooper ? "sounds exactly like Dewitt's DJ Looper (max diff %.2g)" : kKind == kKindVinyl ? "sounds exactly like Dewitt's Vinyl Sim (max diff %.2g)" : "Airliner sounds exactly like Dewitt's DJ Looper into its Vinyl Sim (max diff %.2g)", worst);
        CHECK(worst < 1e-4, msg);
    }
#endif
    std::snprintf(msg, sizeof(msg), "%u parameters (expected %d)", PR->count(P), kParamCount);
    CHECK((int)PR->count(P) == kParamCount, msg);
    bool defs = true; for (int i = 0; i < kParamCount; ++i) if (!isOutput(i)) defs &= std::fabs(getv(i) - kSpecs[i].def) < 1e-4;
    CHECK(defs, "every knob starts at its default");

    for (double sr : { 48000.0, 44100.0, 96000.0 }) {
        restart(sr);
        const int lat = latency();
        run(0.5, 0.5f, lat);
        Stats s = run(1.0, 0.5f, lat);
        std::snprintf(msg, sizeof(msg), "%.0f Hz: everything off = the input, untouched (latency %d, max dev %.2g)", sr, lat, s.maxDev);
        CHECK(s.finite && s.maxDev < 2e-5f && lat == 0, msg);
    }
    restart(48000.0);
    const int lat = latency();
    std::printf("     latency at 48 kHz: %d samples\n", lat);

    if (kKind != kKindBoth) {
        // the single plug-ins: their own effect works, the other one's switch does nothing
        const int mine = kHasLooper ? pLoop : pVinyl, other = kHasLooper ? pVinyl : pLoop;
        pushParam(other, 1); run(0.3, 0.5f, lat); Stats o = run(0.5, 0.5f, lat);
        std::snprintf(msg, sizeof(msg), "%s does nothing here (max dev %.2g)", kHasLooper ? "ON" : "LOOP", o.maxDev); CHECK(o.maxDev < 2e-5f, msg);
        pushParam(other, 0);
        pushParam(mine, 1); run(0.5, 0.5f, lat); Stats on = run(1.0, 0.5f, lat);
        std::snprintf(msg, sizeof(msg), "%s changes the sound (diff %.3f)", kHasLooper ? "LOOP" : "ON", on.diff); CHECK(on.finite && on.diff > (kHasLooper ? 0.05f : 0.003f), msg);
        CHECK(kHasLooper ? (getv(oPhase) >= 0.0) : (getv(oPhase) < 0.0), kHasLooper ? "LOOP reports where the loop is" : "no loop position in the Vinyl Sim");
        pushParam(mine, 0); run(0.2, 0.5f, lat); Stats off = run(0.5, 0.5f, lat);
        std::snprintf(msg, sizeof(msg), "off again goes back to the input (max dev %.2g)", off.maxDev); CHECK(off.maxDev < 2e-5f, msg);
    } else {
        // LOOP
        pushParam(pLoop, 1); Stats a = run(1.0, 0.5f, lat);
        std::snprintf(msg, sizeof(msg), "LOOP on changes the sound (diff %.3f)", a.diff); CHECK(a.finite && a.diff > 0.05f, msg);
        CHECK(getv(oPhase) >= 0.0 && getv(oPhase) <= 1.0, "LOOP reports where the loop is");
        pushParam(pLoop, 0); run(0.1, 0.5f, lat); Stats b = run(0.5, 0.5f, lat);
        std::snprintf(msg, sizeof(msg), "LOOP off goes back to the input (max dev %.2g)", b.maxDev); CHECK(b.maxDev < 2e-5f, msg);
        CHECK(getv(oPhase) < 0.0, "no loop, no loop position");

        // ON (vinyl)
        pushParam(pVinyl, 1); run(0.5, 0.5f, lat); Stats v = run(1.0, 0.5f, lat);
        std::snprintf(msg, sizeof(msg), "ON changes the sound (diff %.3f)", v.diff); CHECK(v.finite && v.diff > 0.003f, msg);
        pushParam(pVinyl, 0); run(0.2, 0.5f, lat); Stats vb = run(0.5, 0.5f, lat);
        std::snprintf(msg, sizeof(msg), "ON off goes back to the input (max dev %.2g)", vb.maxDev); CHECK(vb.maxDev < 2e-5f, msg);
    }

    // both, both orders, extremes, heavy input
    pushParam(pLoop, 1); pushParam(pVinyl, 1); Stats ab = run(1.0, 0.9f, lat);
    pushParam(pOrder, 1); Stats ba = run(1.0, 0.9f, lat);
    std::snprintf(msg, sizeof(msg), "both on, either order: finite and under 1.0 (peaks %.2f / %.2f)", ab.peak, ba.peak);
    CHECK(ab.finite && ba.finite && ab.peak <= 1.f && ba.peak <= 1.f, msg);
    for (int i : { pLen, pSpeed, pComp, pNoise, pFlu }) pushParam(i, kSpecs[i].max);
    Stats mx = run(1.0, 0.9f, lat);
    for (int i : { pLen, pSpeed, pComp, pNoise, pFlu }) pushParam(i, kSpecs[i].min);
    Stats mn = run(1.0, 0.9f, lat);
    std::snprintf(msg, sizeof(msg), "every knob all the way up, then down: finite and bounded (%.2f / %.2f)", mx.peak, mn.peak);
    CHECK(mx.finite && mn.finite && mx.peak <= 1.f && mn.peak <= 1.f, msg);
    pushParam(pLimit, 0); pushParam(pOut, 6); Stats lo = run(0.5, 0.9f, lat);
    std::snprintf(msg, sizeof(msg), "LIMIT off, OUT +6 dB: still finite, soft-clipped under 2 (%.2f)", lo.peak);
    CHECK(lo.finite && lo.peak < 2.f, msg);
    pushParam(pOut, -60); run(0.3, 0.5f, lat); Stats mute = run(0.3, 0.5f, lat);
    CHECK(mute.peak < 1e-4f, "OUT all the way down is silence");
    pushParam(pOut, 0); run(0.2, 0.5f, lat);
    for (int i = 0; i < kParamCount; ++i) if (!isOutput(i)) pushParam(i, kSpecs[i].def);
    run(1.0, 0.5f, lat);
    // the INPUT and OUTPUT faders (everything off: they're the only thing touching the sound)
    for (int side = 0; side < 2; ++side) {
        if (side == 0 ? !kHasLooper : !kHasVinyl) continue;
        const int in = side ? pInB : pInA, out = side ? pOutB : pOutA, mi = side ? oMeterInB : oMeterInA, mo = side ? oMeterOutB : oMeterOutA;
        const char* nm = side ? "Vinyl Sim" : "DJ Looper";
        pushParam(pLimit, 0);
        pushParam(in, 6); run(0.1, 0.2f, lat); Stats up = run(0.5, 0.2f, lat, std::pow(10.f, 6.f / 20.f));
        std::snprintf(msg, sizeof(msg), "%s INPUT +6 dB doubles the level (max dev from 2x %.2g)", nm, up.maxDev);
        CHECK(up.maxDev < 1e-4f, msg);
        CHECK(getv(mi) > 0.0 && getv(mo) > 0.0, "its meters show the level");
        pushParam(in, 0); pushParam(out, -60); run(0.1, 0.2f, lat); Stats sil = run(0.3, 0.2f, lat);
        std::snprintf(msg, sizeof(msg), "%s OUTPUT all the way down is silence (%.2g)", nm, sil.peak); CHECK(sil.peak < 1e-5f, msg);
        pushParam(out, 0); pushParam(pLimit, 1); run(0.2, 0.5f, lat);
    }

    if (kKind != kKindBoth) {
        // the single plug-ins: their own pad and knobs, and nothing from the other side
        auto live = [&](int p) { for (int k = 0; k < 8; ++k) if (kMirrorParam[k] == p) return getv(oMirror0 + k); return getv(p); };
        auto ctl = [&](uint8_t a, uint8_t b, uint8_t c) { pushMidi(a, b, c); run(0.02, 0.5f, lat); };
        const uint8_t myPad = kHasLooper ? 36 : 37, otherPad = kHasLooper ? 37 : 36;
        const int mine = kHasLooper ? pLoop : pVinyl, other = kHasLooper ? pVinyl : pLoop;
        ctl(0x90, otherPad, 100); ctl(0x80, otherPad, 0);
        CHECK(live(other) < 0.5, "the other effect's pad does nothing");
        ctl(0x90, myPad, 100); ctl(0x80, myPad, 0);
        CHECK(live(mine) > 0.5, "its own pad switches it on");
        ctl(0x90, myPad, 100); ctl(0x80, myPad, 0);
        CHECK(live(mine) < 0.5, "and off again");
        const int k1 = kHasLooper ? pLen : pComp;
        const float half = kSpecs[k1].min + 0.5f * (kSpecs[k1].max - kSpecs[k1].min);
        pushParam(k1, half); run(0.02, 0.5f, lat);
        const float halfSet = clampP(k1, half);
        ctl(0xB0, 21, 10);
        CHECK(std::fabs(live(k1) - halfSet) < 1e-3, "pickup: knob 1 below the value leaves it alone");
        ctl(0xB0, 21, 70);
        CHECK(std::fabs(live(k1) - valueOf(k1, 70 / 127.f)) < 1e-3, kHasLooper ? "knob 1 takes LENGTH once it passes" : "knob 1 takes COMP once it passes");
        if (kHasVinyl) { ctl(0xB0, 23, 127); ctl(0xB0, 23, 64); ctl(0xB0, 23, 0); CHECK(std::fabs(live(pFlu) - valueOf(pFlu, 0.f)) < 1e-3, "knob 3 runs WOW FLUT"); }
        else { const double c0 = live(pComp); ctl(0xB0, 23, 0); ctl(0xB0, 23, 127); CHECK(std::fabs(live(pComp) - c0) < 1e-6 && std::fabs(live(pFlu) - kSpecs[pFlu].def) < 1e-6, "knob 3 does nothing in the Looper"); }
        for (int i = 0; i < kParamCount; ++i) if (!isOutput(i)) pushParam(i, kSpecs[i].def);
        run(0.5, 0.5f, lat);
    }

    // ---- the MIDI controller
    if (kKind == kKindBoth) {
        // what the controller set is read where the window reads it: the plug-in's live values
        auto live = [&](int p) { for (int k = 0; k < 8; ++k) if (kMirrorParam[k] == p) return getv(oMirror0 + k); return getv(p); };
        auto ctl = [&](uint8_t a, uint8_t b, uint8_t c) { pushMidi(a, b, c); run(0.02, 0.5f, lat); };
        const double seq0 = getv(oCtlSeq);
        ctl(0x90, 36, 100);
        CHECK(live(pLoop) > 0.5 && getv(oMirror0) > 0.5 && getv(oCtlSeq) > seq0 && getv(oSide) < 0.5, "LOOP pad: loop on, knobs follow the DJ Looper");
        ctl(0x80, 36, 0);
        CHECK(live(pLoop) > 0.5, "LATCH: letting go of the pad keeps the loop");
        ctl(0x90, 36, 100); ctl(0x80, 36, 0);
        CHECK(live(pLoop) < 0.5, "LATCH: hitting it again drops the loop");
        pushParam(pPadMode, 0);
        ctl(0x90, 36, 100); const bool held = live(pLoop) > 0.5; ctl(0x80, 36, 0);
        CHECK(held && live(pLoop) < 0.5, "HOLD: the loop plays only while the pad is held");
        pushParam(pPadMode, 1);
        ctl(0x99, 36, 100);
        CHECK(live(pLoop) < 0.5, "a note on another channel does nothing");
        ctl(0x90, 37, 100); ctl(0x80, 37, 0);
        CHECK(live(pVinyl) > 0.5 && getv(oSide) > 0.5, "ON pad: vinyl on, knobs follow the Vinyl Sim");

        // pickup: COMP at half, the knob starts low and is ignored until it passes the value
        const float half = kSpecs[pComp].min + 0.5f * (kSpecs[pComp].max - kSpecs[pComp].min);
        pushParam(pComp, half); run(0.02, 0.5f, lat);
        const float halfSet = clampP(pComp, half);
        ctl(0xB0, 21, 10);
        CHECK(std::fabs(live(pComp) - halfSet) < 1e-3 && (int)std::lround(getv(oTouch)) == pComp * 4 + 1, "pickup: knob 1 below COMP leaves it alone and says turn up");
        ctl(0xB0, 21, 70);
        const double picked = live(pComp);
        CHECK(std::fabs(picked - valueOf(pComp, 70 / 127.f)) < 1e-3, "pickup: once knob 1 passes COMP's value it takes over");
        CHECK(std::fabs(getv(oMirror0 + 4) - picked) < 1e-6, "the window is told the new COMP value");
        ctl(0xB0, 21, 20);
        CHECK(std::fabs(live(pComp) - valueOf(pComp, 20 / 127.f)) < 1e-3, "then COMP follows knob 1 both ways");
        ctl(0xB0, 23, 127); ctl(0xB0, 23, 64); ctl(0xB0, 23, 0);
        CHECK(std::fabs(live(pFlu) - valueOf(pFlu, 0.f)) < 1e-3, "knob 3 runs WOW FLUT on the Vinyl Sim side");
        // switch sides: knob 1 now means LENGTH, and has to pick it up first
        const double lenBefore = live(pLen);
        ctl(0x90, 36, 100); ctl(0x80, 36, 0);
        ctl(0xB0, 21, 120);
        CHECK(getv(oSide) < 0.5 && std::fabs(live(pLen) - lenBefore) < 1e-4, "LOOP pad moves the knobs to LENGTH and SPEED, no jump");
        ctl(0xB0, 21, 30);
        CHECK(std::fabs(live(pLen) - valueOf(pLen, 30 / 127.f)) < 1e-3, "knob 1 picks up LENGTH after passing it");
        const double fluBefore = live(pFlu);
        ctl(0xB0, 23, 100);
        CHECK(std::fabs(live(pFlu) - fluBefore) < 1e-6, "knob 3 does nothing on the DJ Looper side");
        ctl(0x90, 36, 100); ctl(0x80, 36, 0);    // loop off again

        // learn: link knob 4 to CC 30 through the window's "learn" state
        std::string st = saveState();
        CHECK(setStateKey(st, "learn", std::to_string(kLearnKnob0 + 3)), "the learn switch is in the plug-in's state");
        loadState(st);
        ctl(0xB0, 30, 64);
        CHECK(getv(oLearnSeq) == 0.0, "a learn switch from a saved project is ignored");
        st = saveState();
        setStateKey(st, "learn", std::to_string(kLearnKnob0 + 3) + ":" + std::to_string((int)std::lround(getv(oNonce))));
        loadState(st);
        const double ls0 = getv(oLearnSeq);
        ctl(0xB0, 30, 64);
        const int lv = (int)std::lround(getv(oLearned));
        CHECK(getv(oLearnSeq) > ls0 && lv / 32768 - 1 == kLearnKnob0 + 3 && lv % 32768 == midiCode(true, 0, 30), "learn: the next knob turned is linked and reported");
        const double noz = live(pNoise);
        ctl(0xB0, 30, 0);
        CHECK(std::fabs(live(pNoise) - noz) < 1e-6, "knob 4 does nothing in the 3-knob layout");
        st = saveState(); setStateKey(st, "learn", "-1"); loadState(st);       // the window says learning is done

        // the 6-knob layout: one knob per control, and pickup off (knobs jump straight there)
        char line[200];
        std::snprintf(line, sizeof(line), "map3=%d,%d,%d,%d,%d,%d,-1,-1\nkjump=1\nkgrid=1\n", midiCode(false, 0, 36), midiCode(false, 0, 37), midiCode(true, 0, 21), midiCode(true, 0, 22), midiCode(true, 0, 23), midiCode(true, 0, 30));
        writePrefsFile(line);
        st = saveState(); setStateKey(st, "ctl", "7"); loadState(st);
        ctl(0xB0, 23, 0);
        CHECK(std::fabs(live(pComp) - valueOf(pComp, 0.f)) < 1e-3, "6-knob layout: knob 3 is COMP, and with pickup off it jumps");
        ctl(0xB0, 30, 127);
        CHECK(std::fabs(live(pNoise) - valueOf(pNoise, 1.f)) < 1e-3, "6-knob layout: knob 4 is NOISE");
        ctl(0xB0, 21, 0);
        CHECK(std::fabs(live(pLen) - valueOf(pLen, 0.f)) < 1e-3, "6-knob layout: knob 1 is LENGTH whatever side");
        Stats cs = run(0.5, 0.5f, lat);
        CHECK(cs.finite, "still sounds fine after all that");
        for (int i = 0; i < kParamCount; ++i) if (!isOutput(i)) pushParam(i, kSpecs[i].def);
        run(0.5, 0.5f, lat);
    }

    // CPU
    {
        pushParam(pLoop, 1); pushParam(pVinyl, 1);
        const auto t0 = std::chrono::steady_clock::now();
        run(10.0, 0.5f, lat);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::snprintf(msg, sizeof(msg), "both on: %.2f%% of one core at 48 kHz", ms / 100.0);
        CHECK(ms / 100.0 < 25.0, msg);
        pushParam(pLoop, 0); pushParam(pVinyl, 0); run(0.5, 0.5f, lat);
    }

    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
