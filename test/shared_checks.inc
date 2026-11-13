// Checks shared by every Dewitt build: the ENDLESS knob type and clicks when the Vinyl Sim switches.
#include <vector>
// like run(), but keeps every input and output sample
static void runCap(double seconds, float gain, std::vector<float>& inL, std::vector<float>& inR, std::vector<float>& outL, std::vector<float>& outR)
{
    const int blocks = (int)(seconds * kSr / kBlock);
    std::vector<float> iL(kBlock), iR(kBlock), oL(kBlock), oR(kBlock);
    static double beats = 0.0;
    for (int b = 0; b < blocks; ++b) {
        genInput(iL.data(), iR.data(), kBlock, gain);
        float* ins[2] = { iL.data(), iR.data() }; float* outs[2] = { oL.data(), oR.data() };
        clap_audio_buffer ib{}; ib.data32 = ins; ib.channel_count = 2;
        clap_audio_buffer ob{}; ob.data32 = outs; ob.channel_count = 2;
        clap_input_events ie = { &gEv, ev_size, ev_get };
        clap_output_events oe = { nullptr, out_push };
        clap_event_transport tr{};
        tr.header = { sizeof(tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        tr.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_IS_PLAYING;
        tr.tempo = 120.0; tr.tsig_num = 4; tr.tsig_denom = 4;
        tr.song_pos_beats = (clap_beattime)std::llround(beats * CLAP_BEATTIME_FACTOR);
        clap_process pr{}; pr.audio_inputs = &ib; pr.audio_outputs = &ob; pr.audio_inputs_count = 1; pr.audio_outputs_count = 1;
        pr.frames_count = kBlock; pr.in_events = &ie; pr.out_events = &oe; pr.transport = &tr; pr.steady_time = -1;
        P->process(P, &pr);
        gEv.ev.clear();
        beats += kBlock / kSr * 2.0;
        inL.insert(inL.end(), iL.begin(), iL.end()); inR.insert(inR.end(), iR.begin(), iR.end());
        outL.insert(outL.end(), oL.begin(), oL.end()); outR.insert(outR.end(), oR.begin(), oR.end());
    }
}


static int loopPeriod(const float* y, int n, int minLag, int maxLag)
{
    std::vector<double> cs(maxLag + 2, 0.0); double bc = -1;
    const int w = std::min(8000, n - maxLag - 2);
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double c = 0, e1 = 0, e2 = 0;
        for (int i = 0; i < w; ++i) { c += y[i] * y[i + lag]; e1 += y[i] * y[i]; e2 += y[i + lag] * y[i + lag]; }
        cs[lag] = c / (std::sqrt(e1 * e2) + 1e-12); bc = std::max(bc, cs[lag]);
    }
    for (int lag = minLag + 1; lag < maxLag; ++lag) if (cs[lag] > 0.9 * bc && cs[lag] >= cs[lag - 1] && cs[lag] >= cs[lag + 1]) return lag;
    return -1;
}


// ENDLESS knob type: an endless knob that sends 0-127 (M-VAVE SMC-Pad). Each pad's knob carries on from
// where that pad left it.

static void endlessChecks()
{
    std::string dir = prefsTestDir();
    std::string mm;
    for (int i = 0; i < kNumLearn; ++i) {
        const int code = i < 16 ? midiCode(false, 0, 36 + i) : (i == kLearnKnob0 ? midiCode(true, 0, 30) : -1);
        char b[16]; std::snprintf(b, sizeof(b), i ? ",%d" : "%d", code); mm += b;
    }
    { FILE* f = fopen((dir + TEST_SEP "settings.txt").c_str(), "wb"); fprintf(f, "welcomed3=1\nkmode=%d\nmap3=%s\n", (int)kKnobEndAbs, mm.c_str()); fclose(f); }
    { std::string st = saveState(); setStateKey(st, "ctlmap", "endless"); loadState(st); run(0.05); }
    auto focus = [](int note) { pushMidi(0x90, (uint8_t)note, 100); run(0.02); pushMidi(0x80, (uint8_t)note, 0); run(0.02); };
    auto cc = [](int v) { pushMidi(0xB0, 30, (uint8_t)v); run(0.012); };
    const double start = defaultNorm(kFxLpf, 0);
    char msg[200];

    focus(45);                                   // pad A10: LP Filter, knob 1 = RESONANCE
    for (int v = 64; v <= 74; ++v) cc(v);        // controller somewhere in the middle, turned up 10 clicks
    const double a = getv(oJob0);
    std::snprintf(msg, sizeof(msg), "ENDLESS: turning up 10 clicks moves LP Filter's knob from %.3f to %.3f", start, a);
    CHECK(std::fabs(a - (start + 10 / 127.0)) < 0.002, msg);

    focus(46);                                   // pad A11: HP Filter; turn all the way down
    for (int v = 73; v >= 0; --v) cc(v);
    for (int k = 0; k < 40; ++k) cc(0);          // past the controller's end: it keeps sending 0
    const double b = getv(oJob0);
    std::snprintf(msg, sizeof(msg), "ENDLESS: HP Filter's knob turned all the way down (%.3f)", b);
    CHECK(b < 0.001, msg);

    focus(45);                                   // back to LP Filter: the controller is at 0 now
    cc(1); cc(2);
    const double c = getv(oJob0);
    std::snprintf(msg, sizeof(msg), "ENDLESS: back on LP Filter, the knob carries on from %.3f (now %.3f) instead of jumping to the controller's 0", a, c);
    CHECK(std::fabs(c - (a + 2 / 127.0)) < 0.002, msg);
    cc(1); cc(0); cc(0);
    const double d = getv(oJob0);
    std::snprintf(msg, sizeof(msg), "ENDLESS: turning down at the controller's end still moves the knob down (%.3f)", d);
    CHECK(d < c - 2.5 / 127.0, msg);

    { FILE* f = fopen((dir + TEST_SEP "settings.txt").c_str(), "wb"); fprintf(f, "welcomed3=1\n"); fclose(f); }
    { std::string st = saveState(); setStateKey(st, "ctlmap", "endless-off"); loadState(st); run(0.05); }
}

// Clicks: switching the Vinyl Sim on and off over a steady chord. "Edge" is the biggest jump in the
// output's slope (second difference) around the switch, against the same measure in the steady sound.
static double slopeJump(const std::vector<float>& y, size_t a, size_t b)
{
    double m = 0;
    for (size_t i = a + 2; i < b && i < y.size(); ++i) m = std::max(m, (double)std::fabs(y[i] - 2 * y[i - 1] + y[i - 2]));
    return m;
}
static void clickChecks()
{
    auto PAD = [](int slot) { return pPad0 + padOfSlot(slot); };
    const int S = 18;
    char msg[200];
    gTone = true;
    pushParam(slotParam(S, F_TYPE), kFxVinyl);
    pushParam(slotParam(S, F_K1), 127 / 127.0); pushParam(slotParam(S, F_K2), 0.0); pushParam(slotParam(S, F_K3), 21 / 127.0);
    run(1.0, 0.5f);
    double worstOn = 0, worstOff = 0, steady = 0;
    for (int rep = 0; rep < 6; ++rep) {
        std::vector<float> iL, iR, oL, oR;
        runCap(0.3, 0.5f, iL, iR, oL, oR);
        const size_t on = oL.size();
        pushParam(PAD(S), 1.0);
        runCap(0.6, 0.5f, iL, iR, oL, oR);
        const size_t off = oL.size();
        pushParam(PAD(S), 0.0);
        runCap(0.4, 0.5f, iL, iR, oL, oR);
        const size_t w = (size_t)(0.05 * kSr);
        steady = std::max(steady, std::max(slopeJump(oL, on - w, on), slopeJump(oL, on + 3 * w, on + 6 * w)));
        worstOn = std::max(worstOn, slopeJump(oL, on, on + w));
        worstOff = std::max(worstOff, slopeJump(oL, off, off + w));
        run(0.7 + 0.13 * rep, 0.5f);
    }
    std::snprintf(msg, sizeof(msg), "Vinyl Sim click test: edge on press %.4f, on release %.4f, steady sound %.4f", worstOn, worstOff, steady);
    CHECK(worstOn < 1.5 * steady && worstOff < 1.5 * steady, msg);
    gTone = false;
    pushParam(slotParam(S, F_TYPE), kTypeEmpty); run(0.3, 0.3f);
}
