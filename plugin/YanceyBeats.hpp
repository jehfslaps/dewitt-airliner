// Yancey: beat pads. Whole tracks linked from disk, kept in memory as 16-bit audio (about 10 MB per
// stereo minute), with a tempo per pad.
#pragma once
#include "YanceySamples.hpp"
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace yancey {

static constexpr double kMaxBeatSeconds = 600.0;

struct BeatData {
    std::vector<int16_t> l, r;      // r empty for mono
    double rate = 44100.0;
    int frames = 0;
    inline float L(int i) const { return l[i] * (1.f / 32768.f); }
    inline float R(int i) const { return r.empty() ? L(i) : r[i] * (1.f / 32768.f); }
};

inline void toBeat(const SampleData& sd, BeatData& bd)
{
    bd.rate = sd.rate; bd.frames = sd.frames;
    auto q = [](float x) { const float c = x < -1.f ? -1.f : (x > 1.f ? 1.f : x); return (int16_t)std::lrint(c * 32767.f); };
    bd.l.resize(sd.frames);
    for (int i = 0; i < sd.frames; ++i) bd.l[i] = q(sd.l[i]);
    if (sd.channels == 2) { bd.r.resize(sd.frames); for (int i = 0; i < sd.frames; ++i) bd.r[i] = q(sd.r[i]); }
    else bd.r.clear();
}

// A beat pad's saved settings: "bpm=92.00\npath=...\nname=...\nlen=192.3"
struct BeatInfo { std::string path, name; float bpm = 0.f, len = 0.f; };
inline std::string beatToString(const BeatInfo& b)
{
    char h[64]; std::snprintf(h, sizeof(h), "bpm=%.2f\nlen=%.2f\n", b.bpm, b.len);
    return std::string(h) + "name=" + b.name + "\npath=" + b.path + "\n";
}
inline bool beatFromString(const char* s, BeatInfo& b)
{
    b = BeatInfo();
    if (s == nullptr || s[0] == '\0') return false;
    const std::string t(s);
    size_t pos = 0;
    while (pos < t.size()) {
        size_t nl = t.find('\n', pos); if (nl == std::string::npos) nl = t.size();
        const std::string line = t.substr(pos, nl - pos);
        if (line.compare(0, 4, "bpm=") == 0) b.bpm = (float)std::atof(line.c_str() + 4);
        else if (line.compare(0, 4, "len=") == 0) b.len = (float)std::atof(line.c_str() + 4);
        else if (line.compare(0, 5, "name=") == 0) b.name = line.substr(5);
        else if (line.compare(0, 5, "path=") == 0) b.path = line.substr(5);
        pos = nl + 1;
    }
    return !b.path.empty();
}

// A tempo written in the file name ("Dusty 92bpm.wav", "loop_88_bpm"), or 0
inline float bpmFromName(const std::string& name)
{
    std::string low; for (char c : name) low += (char)std::tolower((unsigned char)c);
    const size_t k = low.find("bpm");
    if (k == std::string::npos) return 0.f;
    size_t e = k; while (e > 0 && (low[e - 1] == ' ' || low[e - 1] == '_' || low[e - 1] == '-')) --e;
    size_t b = e; while (b > 0 && std::isdigit((unsigned char)low[b - 1])) --b;
    if (e - b < 2 || e - b > 3) return 0.f;
    const float v = (float)std::atof(low.substr(b, e - b).c_str());
    return (v >= 50.f && v <= 220.f) ? v : 0.f;
}

// Tempo from the rhythm: an onset envelope (rises in loudness, 400 per second), autocorrelated over
// 60-200 BPM with the double-tempo lag reinforcing, then folded into 70-140 BPM. The ½× and 2× buttons
// fix the cases where that guess lands on half or double.
inline float detectBpm(const SampleData& sd)
{
    if (sd.frames < (int)(sd.rate * 4.0)) return 0.f;
    const int hop = std::max(1, (int)std::lround(sd.rate / 400.0));
    const int nf = std::min(sd.frames, (int)(sd.rate * 90.0)) / hop;
    std::vector<float> env(nf, 0.f);
    float prev = 0.f, lp = 0.f;
    for (int f = 0; f < nf; ++f) {
        double e = 0.0;
        for (int i = f * hop, end = i + hop; i < end; ++i) {
            const float x = sd.channels == 2 ? 0.5f * (sd.l[i] + sd.r[i]) : sd.l[i];
            lp += 0.08f * (x - lp);                   // emphasise the low end where kicks and bass live
            e += (double)(x * x) + 4.0 * lp * lp;
        }
        const float le = std::log(1e-9f + (float)(e / hop));
        env[f] = std::max(0.f, le - prev);
        prev = le;
    }
    double mean = 0.0; for (float v : env) mean += v; mean /= std::max(1, nf);
    for (float& v : env) v -= (float)mean;
    const double fps = sd.rate / hop;
    const int lagMin = (int)(fps * 60.0 / 200.0), lagMax = (int)(fps * 60.0 / 60.0);
    if (nf < lagMax * 3) return 0.f;
    std::vector<double> R(lagMax * 2 + 2, 0.0);
    for (int lag = lagMin; lag <= std::min(lagMax * 2, nf / 2); ++lag) {
        double acc = 0.0;
        for (int i = 0; i + lag < nf; ++i) acc += (double)env[i] * env[i + lag];
        R[lag] = acc / (nf - lag);
    }
    int best = -1; double bestScore = -1e30;
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        const double sc = R[lag] + 0.5 * (2 * lag < (int)R.size() ? R[2 * lag] : 0.0);
        if (sc > bestScore) { bestScore = sc; best = lag; }
    }
    if (best <= lagMin || best >= lagMax) return 0.f;
    // parabolic peak for a fraction of a frame
    const double a = R[best - 1], b = R[best], c = R[best + 1];
    const double den = a - 2.0 * b + c;
    const double off = std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
    double bpm = 60.0 * fps / (best + std::max(-0.5, std::min(0.5, off)));
    // refine: fold the whole envelope onto one beat at each candidate tempo; the right tempo stacks the
    // hits into the same few bins (highest spread), a slightly wrong one smears them out
    {
        const int B = 96;
        std::vector<double> hist(B);
        double bestV = -1.0, bestBpm = bpm;
        for (double cand = bpm * 0.97; cand <= bpm * 1.03; cand += 0.02) {
            const double period = fps * 60.0 / cand;
            std::fill(hist.begin(), hist.end(), 0.0);
            for (int i = 0; i < nf; ++i) {
                const double ph = std::fmod((double)i, period) / period;
                hist[(int)(ph * B) % B] += env[i];
            }
            double m = 0.0; for (double h : hist) m += h; m /= B;
            double v = 0.0; for (double h : hist) v += (h - m) * (h - m);
            if (v > bestV) { bestV = v; bestBpm = cand; }
        }
        bpm = bestBpm;
    }
    while (bpm < 70.0) bpm *= 2.0;
    while (bpm > 141.0) bpm *= 0.5;
    return (float)(std::round(bpm * 10.0) / 10.0);
}

} // namespace yancey
