// Yancey test host: loads the CLAP build, runs audio checks, and can open the GUI on X11.
#include "clap/entry.h"
#include "clap/plugin-factory.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/ext/gui.h"
#include "clap/ext/timer-support.h"
#include <dlfcn.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include "../plugin/YanceyParams.hpp"
#include "../plugin/YanceySamples.hpp"
#include "../plugin/YanceyBeats.hpp"
#include <thread>
#include <sys/stat.h>
using namespace yancey;

static const clap_plugin_timer_support_t* gTimerExt = nullptr;
static bool reg_timer(const clap_host_t*, uint32_t, clap_id* id) { *id = 1; return true; }
static bool unreg_timer(const clap_host_t*, clap_id) { return true; }
static clap_host_timer_support_t gHostTimer = { reg_timer, unreg_timer };
static void hg_resize_hints(const clap_host_t*) {}
static bool hg_req_resize(const clap_host_t*, uint32_t, uint32_t) { return true; }
static bool hg_req_show(const clap_host_t*) { return true; }
static bool hg_req_hide(const clap_host_t*) { return true; }
static void hg_closed(const clap_host_t*, bool) {}
static clap_host_gui_t gHostGui = { hg_resize_hints, hg_req_resize, hg_req_show, hg_req_hide, hg_closed };
static const void* get_ext(const clap_host*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &gHostTimer;
    if (!std::strcmp(id, CLAP_EXT_GUI)) return &gHostGui;
    return nullptr; }
static void noop(const clap_host*) {}
static clap_host host = { CLAP_VERSION, nullptr, "yhost", "t", "", "1", get_ext, noop, noop, noop };

union Ev { clap_event_header h; clap_event_param_value pv; clap_event_midi midi; };
struct EvList { std::vector<Ev> ev; };
static uint32_t ev_size(const clap_input_events* l) { return (uint32_t)((EvList*)l->ctx)->ev.size(); }
static const clap_event_header* ev_get(const clap_input_events* l, uint32_t i) { return &((EvList*)l->ctx)->ev[i].h; }
static bool out_push(const clap_output_events*, const clap_event_header*) { return true; }
struct Buf { std::vector<uint8_t> d; size_t pos = 0; };
static int64_t ws_write(const clap_ostream* s, const void* b, uint64_t n) { auto* B = (Buf*)s->ctx; B->d.insert(B->d.end(), (const uint8_t*)b, (const uint8_t*)b + n); return (int64_t)n; }
static int64_t is_read(const clap_istream* s, void* b, uint64_t n) { auto* B = (Buf*)s->ctx; uint64_t k = std::min<uint64_t>(n, B->d.size() - B->pos); memcpy(b, B->d.data() + B->pos, k); B->pos += k; return (int64_t)k; }

static const clap_plugin* P;
static const clap_plugin_params* PR;
static const clap_plugin_state* ST;
static EvList gEv;
static const int kBlock = 256;
static const double kSr = 48000.0;
static double gPhase = 0.0, gT = 0.0;
static uint32_t gRng = 12345;
static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); ++failures; } else printf("ok   %s\n", msg); } while (0)

static void pushParam(int id, double v) { Ev e{}; e.pv.header = { sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 }; e.pv.param_id = (clap_id)id; e.pv.note_id = -1; e.pv.port_index = -1; e.pv.channel = -1; e.pv.key = -1; e.pv.value = v; gEv.ev.push_back(e); }
static void pushMidi(uint8_t a, uint8_t b, uint8_t c) { Ev e{}; e.midi.header = { sizeof(clap_event_midi), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0 }; e.midi.port_index = 0; e.midi.data[0] = a; e.midi.data[1] = b; e.midi.data[2] = c; gEv.ev.push_back(e); }

struct Stats { float peak = 0, rms = 0, inRms = 0, diff = 0; bool finite = true; };

// beat-ish test signal: a kick every beat at 120 bpm plus a chord and noise hats
static void genInput(float* l, float* r, int n, float gain)
{
    for (int i = 0; i < n; ++i) {
        const double t = gT; gT += 1.0 / kSr;
        const double bt = std::fmod(t, 0.5);
        const double kick = std::sin(2 * M_PI * (50 + 120 * std::exp(-bt * 30)) * bt) * std::exp(-bt * 9);
        const double chord = 0.25 * (std::sin(2 * M_PI * 220 * t) + std::sin(2 * M_PI * 277.2 * t) + std::sin(2 * M_PI * 329.6 * t));
        gRng = gRng * 1664525u + 1013904223u;
        const double hat = (((gRng >> 9) & 0xffff) / 32768.0 - 1.0) * std::exp(-std::fmod(t, 0.25) * 60) * 0.3;
        const double x = gain * (0.6 * kick + 0.3 * chord + hat);
        l[i] = (float)x; r[i] = (float)(x * 0.97);
    }
}

static Stats run(double seconds, float gain = 0.5f, bool playing = true)
{
    Stats s; double sq = 0, sqin = 0, sqd = 0; long n = 0;
    const int blocks = (int)(seconds * kSr / kBlock);
    std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
    static double beats = 0.0;
    for (int b = 0; b < blocks; ++b) {
        genInput(inL.data(), inR.data(), kBlock, gain);
        float* ins[2] = { inL.data(), inR.data() }; float* outs[2] = { outL.data(), outR.data() };
        clap_audio_buffer ib{}; ib.data32 = ins; ib.channel_count = 2;
        clap_audio_buffer ob{}; ob.data32 = outs; ob.channel_count = 2;
        clap_input_events ie = { &gEv, ev_size, ev_get };
        clap_output_events oe = { nullptr, out_push };
        clap_event_transport tr{};
        tr.header = { sizeof(tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | (playing ? CLAP_TRANSPORT_IS_PLAYING : 0);
        tr.tempo = 120.0; tr.tsig_num = 4; tr.tsig_denom = 4;
        tr.song_pos_beats = (clap_beattime)std::llround(beats * CLAP_BEATTIME_FACTOR);
        tr.bar_start = (clap_beattime)std::llround(std::floor(beats / 4) * 4 * CLAP_BEATTIME_FACTOR);
        tr.bar_number = (int32_t)(beats / 4);
        clap_process pr{}; pr.audio_inputs = &ib; pr.audio_outputs = &ob; pr.audio_inputs_count = 1; pr.audio_outputs_count = 1;
        pr.frames_count = kBlock; pr.in_events = &ie; pr.out_events = &oe; pr.transport = &tr; pr.steady_time = -1;
        P->process(P, &pr);
        gEv.ev.clear();
        if (playing) beats += kBlock / kSr * 2.0;
        for (int i = 0; i < kBlock; ++i) {
            const float o = outL[i], o2 = outR[i];
            if (!std::isfinite(o) || !std::isfinite(o2)) s.finite = false;
            s.peak = std::max(s.peak, std::max(std::fabs(o), std::fabs(o2)));
            sq += o * o; sqin += inL[i] * inL[i]; sqd += (o - inL[i]) * (o - inL[i]); ++n;
        }
    }
    s.rms = (float)std::sqrt(sq / std::max(1L, n)); s.inRms = (float)std::sqrt(sqin / std::max(1L, n)); s.diff = (float)std::sqrt(sqd / std::max(1L, n));
    return s;
}

static double getv(int id) { double v = 0; PR->get_value(P, (clap_id)id, &v); return v; }

static std::string saveState() { Buf B; clap_ostream os = { &B, ws_write }; ST->save(P, &os); return std::string(B.d.begin(), B.d.end()); }
static void loadState(const std::string& s) { Buf B; B.d.assign(s.begin(), s.end()); clap_istream is = { &B, is_read }; ST->load(P, &is); }
static void setStateKey(std::string& s, const char* key, const std::string& val)
{
    size_t pos = s.find(std::string(key) + std::string(1, '\0'));
    if (pos == std::string::npos) { printf("key %s not found\n", key); return; }
    size_t v0 = pos + strlen(key) + 1, v1 = s.find('\0', v0);
    s.replace(v0, v1 - v0, val);
}


// a 16-bit stereo WAV: a tone with a click on every beat, so a beat pad has something to play
static void writeBeatWav(const char* path, float hz, float bpm, float secs)
{
    const int sr = 44100, n = (int)(secs * sr);
    std::vector<int16_t> d(n * 2);
    for (int i = 0; i < n; ++i) {
        const double t = (double)i / sr, bt = std::fmod(t, 60.0 / bpm);
        const double v = 0.35 * std::sin(2 * M_PI * hz * t) + 0.4 * std::exp(-bt * 40) * std::sin(2 * M_PI * 60 * bt);
        d[i * 2] = d[i * 2 + 1] = (int16_t)(v * 32767);
    }
    FILE* f = fopen(path, "wb");
    const int bytes = n * 4;
    auto w32 = [&](int v) { fwrite(&v, 4, 1, f); }; auto w16 = [&](int v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(bytes); fwrite(d.data(), 2, d.size(), f); fclose(f);
}
static bool beatReady(int slot) { const int m = (int)std::lround(getv(slot < 24 ? oBeatReadyLo : oBeatReadyHi)); return (m >> (slot < 24 ? slot : slot - 24)) & 1; }
static bool waitBeat(int slot) { for (int i = 0; i < 200 && !beatReady(slot); ++i) { run(0.02, 0.f); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } return beatReady(slot); }

static void tap(int padParam) { pushParam(padParam, 1); run(0.02); pushParam(padParam, 0); run(0.02); }

int main(int argc, char** argv)
{
    void* h = dlopen(argv[1], RTLD_NOW);
    if (!h) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    auto* e = (const clap_plugin_entry*)dlsym(h, "clap_entry");
    e->init(argv[1]);
    auto* f = (const clap_plugin_factory*)e->get_factory(CLAP_PLUGIN_FACTORY_ID);
    const clap_plugin_descriptor* d = f->get_plugin_descriptor(f, 0);
    printf("plugin: %s (%s)\n", d->name, d->id);
    P = f->create_plugin(f, &host, d->id);
    P->init(P);
    PR = (const clap_plugin_params*)P->get_extension(P, CLAP_EXT_PARAMS);
    ST = (const clap_plugin_state*)P->get_extension(P, CLAP_EXT_STATE);
    P->activate(P, kSr, 32, kBlock);
    P->start_processing(P);
    const bool gui = argc > 2 && !std::strcmp(argv[2], "gui");

    if (!gui) {
        auto PAD = [](int slot) { return pPad0 + padOfSlot(slot); };
        printf("params: %u (expected %d)\n", PR->count(P), kParamCount);
        Stats s = run(1.0);
        CHECK(s.finite && std::fabs(s.rms - s.inRms) / s.inRms < 0.08f, "nothing on: output follows input");
        const int mpc[16] = { kFxBeatRepeat, kFxRevStepper, kFxDelay, kFxReverb, kFxRingMod, kFxCrusher, kFxColor, kFxGranulator,
                              kFxComb, kFxLpf, kFxHpf, kFxBpf, kFxHalfSpeed, kFxChorus, kFxFlanger, kFxPhaser };
        bool ok = true; for (int i = 0; i < 16; ++i) ok &= (int)getv(slotParam(i, F_TYPE)) == mpc[i];
        bool empty = true; for (int i = 16; i < 48; ++i) empty &= (int)getv(slotParam(i, F_TYPE)) == kTypeEmpty;
        CHECK(ok, "bank A is the MPC Sample's Pad FX layout");
        CHECK(empty, "banks B and C start empty");
        bool hold = true; for (int i = 0; i < 16; ++i) hold &= getv(slotParam(i, F_LATCH)) < 0.5;
        CHECK(hold, "every pad holds by default");

        pushParam(PAD(20), 1); s = run(0.5); pushParam(PAD(20), 0); run(0.1);
        CHECK(std::fabs(s.rms - s.inRms) / s.inRms < 0.08f && getv(oVoices) == 0, "an empty pad does nothing");

        // every effect, held, heavy input, extreme knobs
        for (int fx = 0; fx < kNumEffects; ++fx) {
            pushParam(slotParam(16, F_TYPE), fx);
            for (int k = 0; k < 5; ++k) pushParam(slotParam(16, F_K1 + k), defaultNorm(fx, k));
            run(0.05);
            pushParam(PAD(16), 1);
            const auto t0 = std::chrono::steady_clock::now();
            Stats a = run(3.0, 0.9f);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            for (int k = 0; k < 5; ++k) pushParam(slotParam(16, F_K1 + k), 1.0);
            Stats b2 = run(1.0, 0.9f);
            for (int k = 0; k < 5; ++k) pushParam(slotParam(16, F_K1 + k), 0.0);
            Stats c = run(1.0, 0.9f);
            pushParam(PAD(16), 0);
            Stats t = run(2.0, 0.9f);
            char msg[160];
            std::snprintf(msg, sizeof(msg), "%-14s finite, bounded (peak %.2f/%.2f/%.2f, diff %.3f, %.0f%% CPU)", kEffects[fx].name, a.peak, b2.peak, c.peak, a.diff, ms / 30.0);
            CHECK(a.finite && b2.finite && c.finite && t.finite && a.peak <= 1.0f && b2.peak <= 1.0f && c.peak <= 1.0f, msg);
            if (fx != kFxIsolator) { std::snprintf(msg, sizeof(msg), "%-14s audibly changes the beat", kEffects[fx].name); CHECK(a.diff > 0.01f, msg); }
        }
        pushParam(slotParam(16, F_TYPE), kTypeEmpty); run(0.5);

        // all 16 MPC pads at once
        { for (int i = 0; i < 16; ++i) pushParam(PAD(i), 1);
          const auto t0 = std::chrono::steady_clock::now(); Stats a = run(3.0, 0.9f);
          const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
          char msg[120]; std::snprintf(msg, sizeof(msg), "all 16 MPC pads held at once stay bounded (%.0f%% CPU)", ms / 30.0);
          CHECK(a.finite && a.peak <= 1.0f && getv(oVoices) == 16, msg);
          for (int i = 0; i < 16; ++i) pushParam(PAD(i), 0); run(1.0); }

        // Kill is a move you can put on any pad
        pushParam(slotParam(17, F_TYPE), typeOfMove(kMvKill)); run(0.05);
        pushParam(PAD(17), 1); run(0.2); Stats k2 = run(0.3);
        CHECK(k2.peak < 1e-4f && getv(oKill) > 0.5, "a KILL pad on bank B mutes everything");
        pushParam(PAD(17), 0); s = run(0.3);
        CHECK(s.rms > 0.05f, "letting go of KILL brings the sound back");

        // Delay and Reverb: cut by default, ring out when set
        pushParam(PAD(2), 1); run(2.0); pushParam(PAD(2), 0); run(0.05); Stats cut = run(0.5, 0.f);
        CHECK(cut.peak < 0.003f, "Delay cuts on release by default");
        pushParam(slotParam(2, F_REL), 1); pushParam(PAD(2), 1); run(2.0); pushParam(PAD(2), 0); run(0.05); Stats ring = run(0.5, 0.f);
        CHECK(ring.rms > 0.002f, "Delay with RING OUT keeps echoing");
        pushParam(slotParam(2, F_REL), 0); run(6.0, 0.f);
        pushParam(slotParam(3, F_REL), 1); pushParam(PAD(3), 1); run(2.0); pushParam(PAD(3), 0); run(0.05); ring = run(0.5, 0.f);
        CHECK(ring.rms > 0.002f, "Reverb with RING OUT keeps ringing");
        pushParam(slotParam(3, F_REL), 0); run(12.0, 0.f);

        // knobs follow the selected pad: Chorus has 3, Comb 1, Vinyl Sim 5, nothing is OUT
        float pv[kParamCount]; for (int i = 0; i < kParamCount; ++i) pv[i] = (float)getv(i);
        CHECK(jobTarget(2, 13, pv, false) == slotParam(13, F_K3) && jobTarget(3, 13, pv, false) == -1, "Chorus uses knobs 1-3 only");
        CHECK(jobTarget(0, 8, pv, false) == slotParam(8, F_K1) && jobTarget(1, 8, pv, false) == -1, "Comb Filter uses knob 1 only, like the MPC");
        pv[slotParam(18, F_TYPE)] = kFxVinyl;
        CHECK(jobTarget(4, 18, pv, false) == slotParam(18, F_K5), "Vinyl Sim uses all five knobs");
        bool noOut = true; for (int j = 0; j < kNumJobs; ++j) noOut &= jobTarget(j, 13, pv, false) != pOut;
        CHECK(noOut, "no knob is the output");
        CHECK(jobTarget(0, 20, pv, false) == -1, "an empty pad has no knobs");

        // ------------------------------------------------ Yancey versions: same sound as the MPC versions at the same settings
        {
            const int pairs[6][2] = { { kFxChorus, kFxChorusY }, { kFxFlanger, kFxFlangerY }, { kFxPhaser, kFxPhaserY },
                                      { kFxComb, kFxCombY }, { kFxRingMod, kFxRingModY }, { kFxCrusher, kFxCrusherY } };
            for (auto& pr : pairs) {
                double rms[2];
                for (int v = 0; v < 2; ++v) {
                    const int fx = pr[v];
                    pushParam(slotParam(20, F_TYPE), fx);
                    for (int k = 0; k < 5; ++k) pushParam(slotParam(20, F_K1 + k), defaultNorm(fx, k));
                    run(0.3);
                    P->stop_processing(P); P->deactivate(P); P->activate(P, kSr, 32, kBlock); P->start_processing(P);   // fresh start: sweeps begin at the same point
                    gT = 0.0; gRng = 12345;
                    pushParam(PAD(20), 1); Stats a = run(2.0); pushParam(PAD(20), 0); run(2.0);
                    rms[v] = a.rms;
                }
                printf("     %-18s %.5f vs %.5f\n", kEffects[pr[1]].name, rms[0], rms[1]);
                char msg[96]; std::snprintf(msg, sizeof(msg), "%s sounds the same as %s at default settings", kEffects[pr[1]].name, kEffects[pr[0]].name);
                CHECK(std::fabs(rms[0] - rms[1]) < 0.002 * rms[0] + 1e-5, msg);
            }
            float pv[kParamCount]; for (int i = 0; i < kParamCount; ++i) pv[i] = (float)getv(i);
            pv[slotParam(20, F_TYPE)] = kFxChorusY;
            CHECK(jobTarget(3, 20, pv, false) == slotParam(20, F_K4), "CHORUS \xC2\xB7 Y has its MIX knob (knob 4)");
            pushParam(slotParam(20, F_TYPE), kTypeEmpty); run(0.3);
        }

        // ------------------------------------------------ Pad Feel: velocity and pressure set how much of the effect plays
        {
            const char* home = getenv("HOME");
            std::string dir = std::string(home) + "/.config"; mkdir(dir.c_str(), 0755); dir += "/Howard"; mkdir(dir.c_str(), 0755);
            std::string mm;
            for (int i = 0; i < kNumLearn; ++i) { char b[16]; std::snprintf(b, sizeof(b), i ? ",%d" : "%d", i < 16 ? midiCode(false, 0, 36 + i) : -1); mm += b; }
            auto writeFeel = [&](int feel) { FILE* f = fopen((dir + "/settings.txt").c_str(), "wb"); fprintf(f, "welcomed3=1\npadfeel=%d\nmap3=%s\n", feel, mm.c_str()); fclose(f); };
            auto reload = [&](const char* tag) { std::string st = saveState(); setStateKey(st, "ctlmap", tag); loadState(st); run(0.05); };
            writeFeel(0); reload("feel0");
            // Filter on LP Filter (pad A10 = note 45): soft hit vs hard hit
            auto hit = [&](int vel) { gT = 0.0; gRng = 12345; pushMidi(0x90, 45, (uint8_t)vel); Stats a = run(1.0); pushMidi(0x80, 45, 0); run(0.5); return a; };
            Stats soft = hit(25), hard = hit(127);
            printf("     LP Filter change from dry: soft hit %.4f, hard hit %.4f\n", soft.diff, hard.diff);
            CHECK(soft.diff < 0.5f * hard.diff && soft.diff > 0.0f, "a soft hit plays less of the effect than a hard one");
            // aftertouch moves it while held
            gT = 0.0; gRng = 12345; pushMidi(0x90, 45, 30); Stats a1 = run(0.6);
            pushMidi(0xA0, 45, 127); run(0.1); Stats a2 = run(0.6); pushMidi(0x80, 45, 0); run(0.5);
            CHECK(a2.diff > 1.5f * a1.diff, "pressing harder while held (poly aftertouch) brings in more of the effect");
            gT = 0.0; gRng = 12345; pushMidi(0x90, 45, 127); run(0.3); pushMidi(0xD0, 10, 0); run(0.1); Stats a3 = run(0.6); pushMidi(0x80, 45, 0); run(0.5);
            CHECK(a3.diff < 0.5f * hard.diff, "easing off (channel pressure) pulls the effect back");
            // Velocity only ignores aftertouch
            writeFeel(1); reload("feel1");
            gT = 0.0; gRng = 12345; pushMidi(0x90, 45, 30); run(0.3); pushMidi(0xA0, 45, 127); run(0.1); Stats v1 = run(0.6); pushMidi(0x80, 45, 0); run(0.5);
            CHECK(v1.diff < 0.5f * hard.diff, "Velocity only: aftertouch doesn't change it");
            // Full: a soft hit is the whole effect
            writeFeel(2); reload("feel2");
            Stats f1 = hit(10);
            CHECK(std::fabs(f1.diff - hard.diff) < 0.1f * hard.diff, "Full: even the softest hit plays the whole effect");
            writeFeel(0); reload("feel0b");
            // Kill always mutes completely, however soft the hit
            pushParam(slotParam(17, F_TYPE), typeOfMove(kMvKill)); run(0.05);
            pushMidi(0x90, 36 + 17 - 16 + 0, 1); run(0.01); pushMidi(0x80, 36 + 1, 0);
            pushParam(PAD(17), 0.05); run(0.2); Stats k = run(0.3); pushParam(PAD(17), 0); run(0.3);
            CHECK(k.peak < 1e-4f, "a soft press on KILL still mutes everything");
            // latch keeps the strongest press
            pushParam(slotParam(9, F_LATCH), 1); run(0.05);
            pushMidi(0x90, 45, 40); run(0.2); pushMidi(0xA0, 45, 120); run(0.2); pushMidi(0xA0, 45, 5); run(0.1); pushMidi(0x80, 45, 0); run(0.2);
            gT = 0.0; gRng = 12345; Stats l1 = run(0.6);
            CHECK(l1.diff > 0.75f * hard.diff, "a latched pad keeps the strongest amount you pressed");
            pushMidi(0x90, 45, 100); run(0.05); pushMidi(0x80, 45, 0); run(0.3);
            pushParam(slotParam(9, F_LATCH), 0); run(0.05);
        }

        printf("\n%d failure(s)\n", failures);
    } else {
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) { printf("no display\n"); return 1; }
        const int W = argc > 3 ? atoi(argv[3]) : 1080, H = argc > 4 ? atoi(argv[4]) : 840;
        Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, W, H, 0, 0, 0);
        XMapWindow(dpy, win); XFlush(dpy);
        auto* g = (const clap_plugin_gui*)P->get_extension(P, CLAP_EXT_GUI);
        gTimerExt = (const clap_plugin_timer_support_t*)P->get_extension(P, CLAP_EXT_TIMER_SUPPORT);
        g->create(P, CLAP_WINDOW_API_X11, false);
        g->set_scale(P, 1.0);
        g->set_size(P, W, H);
        clap_window cw; cw.api = CLAP_WINDOW_API_X11; cw.x11 = win;
        g->set_parent(P, &cw);
        g->show(P);
        for (int i = 0; i < 20; ++i) { if (gTimerExt) gTimerExt->on_timer(P, 1); usleep(10000); }
        { uint32_t aw = W, ah = H; g->adjust_size(P, &aw, &ah); bool ok = g->set_size(P, aw, ah); uint32_t gw = 0, gh = 0; g->get_size(P, &gw, &gh); printf("resize %dx%d -> adjust %ux%u ok=%d now %ux%u\n", W, H, aw, ah, ok, gw, gh); fflush(stdout); }
        // optional scripted setup through parameters
        const char* script = argc > 5 ? argv[5] : "";
        if (std::strstr(script, "pads")) { tap(pPad0 + 2); tap(pPad0 + 4); }
        if (std::strstr(script, "sample")) {
            SampleData sd; sd.name = "Air horn"; sd.rate = 44100; sd.channels = 1; sd.frames = 66150;
            sd.l.resize(sd.frames);
            for (int i = 0; i < sd.frames; ++i) sd.l[i] = 0.3f * (float)std::sin(2 * M_PI * 440.0 * i / 44100.0);
            std::string st = saveState(); setStateKey(st, "smp9", encodeSample(sd)); loadState(st);
        }
        if (std::strstr(script, "pad3")) { tap(pPad0 + 2); }
        if (std::strstr(script, "beats")) {
            writeBeatWav("/tmp/Dusty Loop 92bpm.wav", 330.f, 92.f, 20.f);
            writeBeatWav("/tmp/ybeat_b.wav", 550.f, 88.f, 20.f);
            BeatInfo a; a.path = "/tmp/Dusty Loop 92bpm.wav"; a.name = "Dusty Loop 92bpm"; a.bpm = 92.f; a.len = 20.f;
            BeatInfo b; b.path = "/tmp/ybeat_b.wav"; b.name = "Late Night Flip"; b.bpm = 88.f; b.len = 20.f;
            BeatInfo m; m.path = "/tmp/gone/Old Beat.wav"; m.name = "Old Beat"; m.bpm = 90.f; m.len = 180.f;
            std::string st = saveState(); setStateKey(st, "beat8", beatToString(a)); setStateKey(st, "beat9", beatToString(b)); setStateKey(st, "beat10", beatToString(m)); loadState(st);
            if (std::strstr(script, "play")) { waitBeat(8); tap(pPad0 + 8); }
        }
        if (std::strstr(script, "backup")) {
            std::string st = saveState();
            setStateKey(st, "midimap", "65584,65585,65586,65587,65588,65589,65590,65591,65592,65593,65594,65595,65596,65597,65598,65599,131092,131093,131094,131095,131096,131097,131098,131099,-1,-1,-1,-1,-1,-1,-1,-1");
            loadState(st);
        }

        if (std::strstr(script, "hold")) pushParam(pPad0 + 9, 1);
        // keep running audio + timers; the controlling script takes screenshots meanwhile
        const int secs = argc > 6 ? atoi(argv[6]) : 30;
        for (int i = 0; i < secs * 60; ++i) {
            // test script drops /tmp/ymidi containing "note N" or "cc N V" to send MIDI
            if (FILE* mf = std::fopen("/tmp/ymidi", "r")) {
                char kind[8] = {}; int a = 0, b = 0, ch = 0;
                const int got = std::fscanf(mf, "%7s %d %d %d", kind, &a, &b, &ch);
                std::fclose(mf); std::remove("/tmp/ymidi");
                if (got >= 2 && !std::strcmp(kind, "note")) { pushMidi((uint8_t)(0x90 | (b & 15)), (uint8_t)a, 100); run(0.02); pushMidi((uint8_t)(0x80 | (b & 15)), (uint8_t)a, 0); }
                if (got >= 3 && !std::strcmp(kind, "cc")) pushMidi((uint8_t)(0xB0 | (ch & 15)), (uint8_t)a, (uint8_t)b);
                if (got >= 3 && !std::strcmp(kind, "on")) pushMidi(0x90, (uint8_t)a, (uint8_t)b);
                if (got >= 2 && !std::strcmp(kind, "off")) pushMidi(0x80, (uint8_t)a, 0);
                if (got >= 3 && !std::strcmp(kind, "at")) pushMidi(0xA0, (uint8_t)a, (uint8_t)b);
            }
            run(1.0 / 60.0, 0.5f);
            if (gTimerExt) gTimerExt->on_timer(P, 1);
            while (XPending(dpy)) { XEvent xe; XNextEvent(dpy, &xe); }
            usleep(16000);
        }
        g->destroy(P);
    }
    P->stop_processing(P);
    P->deactivate(P);
    P->destroy(P);
    e->deinit();
    return failures ? 2 : 0;
}
