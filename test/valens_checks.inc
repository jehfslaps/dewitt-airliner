// JAY 1.2: checks for the Valens effects (STUTTER, REVERSE, DISTORTION)

// like runCap, but with the transport starting at a given beat, so the 1/16 grid is known exactly
static void runGrid(double seconds, float gain, double& beats, std::vector<float>& inL, std::vector<float>& outL)
{
    const int blocks = (int)(seconds * kSr / kBlock);
    std::vector<float> iL(kBlock), iR(kBlock), oL(kBlock), oR(kBlock);
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
        tr.bar_start = (clap_beattime)std::llround(std::floor(beats / 4) * 4 * CLAP_BEATTIME_FACTOR);
        tr.bar_number = (int32_t)(beats / 4);
        clap_process pr{}; pr.audio_inputs = &ib; pr.audio_outputs = &ob; pr.audio_inputs_count = 1; pr.audio_outputs_count = 1;
        pr.frames_count = kBlock; pr.in_events = &ie; pr.out_events = &oe; pr.transport = &tr; pr.steady_time = -1;
        P->process(P, &pr);
        gEv.ev.clear();
        beats += kBlock / kSr * 2.0;
        inL.insert(inL.end(), iL.begin(), iL.end()); outL.insert(outL.end(), oL.begin(), oL.end());
    }
}

// the smallest max error over a few sample offsets (the host's beat position is rounded a little)
template <class F> static double bestErr(F expect, size_t from, size_t to)
{
    double best = 1e9;
    for (int o = -2; o <= 2; ++o) {
        double m = 0; bool ok = true;
        for (size_t i = from; i < to; ++i) { const double e = expect((long)i, o); if (e > 1e8) { ok = false; break; } m = std::max(m, e); }
        if (ok) best = std::min(best, m);
    }
    return best;
}

static void valensChecks()
{
    auto PAD = [](int slot) { return pPad0 + padOfSlot(slot); };
    char msg[220];
    const int S = 20;
    const double step = kSr * 0.5 / 4.0, sub = step / 2.0;        // 120 bpm: 1/16 and 1/32 in samples
    const long len = (long)std::lround(sub * 0.92), att = (long)std::lround(0.002 * kSr), rel = (long)std::lround(0.012 * kSr);

    CHECK(typeOfMove(kMvKill) == 44 && kTypeEmpty == 45 && typeOfEffect(kFxCrusherY) == 34,
          "older projects keep their pad numbers (moves 35-44, EMPTY 45, new effects after)");
    CHECK(numControls(kFxStutterV) == 1 && numControls(kFxReverseV) == 1 && numControls(kFxDistortionV) == 1,
          "STUTTER, REVERSE and DISTORTION have one knob each");

    gNoise = true;
    // ---- STUTTER at 100: every step is two 1/32 pieces from the step's start, 100 % then 86 %
    {
        pushParam(slotParam(S, F_TYPE), typeOfEffect(kFxStutterV)); pushParam(slotParam(S, F_K1), 1.0);
        double beats = 0.0; std::vector<float> in, out;
        runGrid(0.5, 0.3f, beats, in, out);
        pushParam(PAD(S), 1.0);
        runGrid(2.0, 0.3f, beats, in, out);
        pushParam(PAD(S), 0.0); run(0.3, 0.3f);
        // check steps 6..18 (after the press, inside the recording)
        double worst = 0;
        for (int st = 6; st < 19; ++st) {
            const long s0 = (long)std::lround(st * step);
            auto ex = [&](long i, int o) -> double {
                const long j = i - s0 - o;
                if (j < 0 || j >= (long)step) return 0.0;
                const int rep = j < (long)sub ? 0 : 1;
                const long k = j - (long)std::lround(rep * sub);
                if (k < att + 2 || k >= len - rel - 2) return 0.0;              // skip the fades
                const double want = (rep == 0 ? 1.0 : 0.86) * in[s0 + o + k];
                return std::fabs(out[i] - want);
            };
            worst = std::max(worst, bestErr(ex, (size_t)s0, (size_t)(s0 + step)));
        }
        std::snprintf(msg, sizeof(msg), "STUTTER 100: every 1/16 = its first 1/32 at 100 %%, then again at 86 %% (max error %.2g)", worst);
        CHECK(worst < 1e-4, msg);
    }
    // ---- REVERSE at 100: every step is the previous step backwards
    {
        pushParam(slotParam(S, F_TYPE), typeOfEffect(kFxReverseV)); pushParam(slotParam(S, F_K1), 1.0);
        double beats = 0.0; std::vector<float> in, out;
        runGrid(0.5, 0.3f, beats, in, out);
        pushParam(PAD(S), 1.0);
        runGrid(2.0, 0.3f, beats, in, out);
        pushParam(PAD(S), 0.0); run(0.3, 0.3f);
        double worst = 0;
        for (int st = 6; st < 19; ++st) {
            const long s0 = (long)std::lround(st * step);
            auto ex = [&](long i, int o) -> double {
                const long j = i - s0 - o;
                if (j < att + 2 || j >= (long)step - rel - 2) return 0.0;
                return std::fabs(out[i] - in[s0 + o - 1 - j]);
            };
            worst = std::max(worst, bestErr(ex, (size_t)s0, (size_t)(s0 + step)));
        }
        std::snprintf(msg, sizeof(msg), "REVERSE 100: every 1/16 plays the step before it backwards (max error %.2g)", worst);
        CHECK(worst < 1e-4, msg);
    }
    // ---- 50 %: about half the steps, and the same ones every bar
    for (int which = 0; which < 2; ++which) {
        const int fx = which ? kFxReverseV : kFxStutterV;
        pushParam(slotParam(S, F_TYPE), typeOfEffect(fx)); pushParam(slotParam(S, F_K1), 0.5);
        double beats = 0.0; std::vector<float> in, out;
        runGrid(0.5, 0.3f, beats, in, out);
        pushParam(PAD(S), 1.0);
        runGrid(6.3, 0.3f, beats, in, out);       // covers bars 2 and 3 (steps 16-47)
        pushParam(PAD(S), 0.0); run(0.3, 0.3f);
        int mask[2][16] = {};
        for (int bar = 0; bar < 2; ++bar)
            for (int st = 0; st < 16; ++st) {
                const long s0 = (long)std::lround((16 * (bar + 1) + st) * step);
                double d = 0; for (long i = s0 + 300; i < s0 + (long)step - 800; ++i) d = std::max(d, (double)std::fabs(out[i] - in[i]));
                mask[bar][st] = d > 1e-3;
            }
        int count = 0; bool same = true;
        for (int st = 0; st < 16; ++st) { count += mask[0][st]; same &= mask[0][st] == mask[1][st]; }
        std::snprintf(msg, sizeof(msg), "%s 50: %d of 16 steps affected, the same steps in the next bar", which ? "REVERSE" : "STUTTER", count);
        CHECK(same && count >= 3 && count <= 13, msg);
    }
    // ---- 0 %: untouched
    for (int which = 0; which < 3; ++which) {
        const int fx = which == 0 ? kFxStutterV : (which == 1 ? kFxReverseV : kFxDistortionV);
        pushParam(slotParam(S, F_TYPE), typeOfEffect(fx)); pushParam(slotParam(S, F_K1), 0.0);
        double beats = 0.0; std::vector<float> in, out;
        runGrid(0.5, 0.3f, beats, in, out);
        pushParam(PAD(S), 1.0);
        runGrid(1.5, 0.3f, beats, in, out);
        pushParam(PAD(S), 0.0); run(0.3, 0.3f);
        double md = 0; for (size_t i = (size_t)(0.6 * kSr); i < in.size(); ++i) md = std::max(md, (double)std::fabs(out[i] - in[i]));
        std::snprintf(msg, sizeof(msg), "%s at 0 leaves the sound untouched (max diff %.2g)", kEffects[fx].name, md);
        CHECK(md < 1e-6, msg);
    }
    // ---- DISTORTION at 50: Valens' curve and level makeup
    {
        pushParam(slotParam(S, F_TYPE), typeOfEffect(kFxDistortionV)); pushParam(slotParam(S, F_K1), 0.5);
        double beats = 0.0; std::vector<float> in, out;
        runGrid(0.5, 0.3f, beats, in, out);
        pushParam(PAD(S), 1.0);
        runGrid(1.0, 0.3f, beats, in, out);
        pushParam(PAD(S), 0.0); run(0.3, 0.3f);
        const float amount = 0.5f, a = amount * 40.f, mk = 0.85f / (1.f + amount * 0.9f);
        double md = 0;
        for (size_t i = (size_t)(0.6 * kSr); i < in.size(); ++i) {
            float x = std::max(-1.f, std::min(1.f, in[i]));
            x = ((1 + a) * x) / (1 + a * std::fabs(x)) * mk;
            md = std::max(md, (double)std::fabs(out[i] - x));
        }
        std::snprintf(msg, sizeof(msg), "DISTORTION 50 = Valens' soft clip and makeup (max diff %.2g)", md);
        CHECK(md < 1e-5, msg);
    }
    gNoise = false;
    pushParam(slotParam(S, F_TYPE), kTypeEmpty); run(0.2, 0.3f);
}
