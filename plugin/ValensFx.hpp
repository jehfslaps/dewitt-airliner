// JAY: the Valens effects (from Jehf's Valens plug-in, v0.5.1): STUTTER, REVERSE and DISTORTION, one
// knob each, 0-100.
//
// DISTORTION is Valens' own code (its applyDistortion): the same soft clipper and level makeup.
// The one change agreed with Jehf: 0 % is a true bypass (Valens still clamps and trims to 0.85 at 0 %).
//
// STUTTER and REVERSE work on hits in Valens. JAY plays no sounds of its own, so every 1/16 step of the
// song is a hit here (agreed with Jehf), and Valens' rules are applied to those steps:
//   - each step position in the bar (16 of them) has fixed random numbers r1 / r2, drawn the same way as
//     Valens' HitRandom; a step stutters when r1 < STUTTER, reverses when r2 < REVERSE, so turning the
//     knob up adds steps one at a time and the same steps stay affected every bar. The pattern is kept per
//     pad (saved with the project); "New pattern" draws a new one.
//   - a hit is cut at the next hit: here, at the next step.
//   - STUTTER: sub = a 1/32 at the current tempo; n = min(4, floor(step / sub)) repeats that fit (on a
//     1/16 step that's 2); fewer than 2 = plays normally. Each repeat restarts from the step's start,
//     92 % of a 1/32 long, at 100 / 86 / 72 / 58 %.
//   - REVERSE: the step plays backwards. So it can happen the instant the step starts (no added latency),
//     it reverses the PREVIOUS step (agreed with Jehf): the swell into the next hit stays.
//   - every piece gets Valens' 2 ms fade-in and 12 ms fade-out (at most 30 % of its length).
// Steps that aren't affected pass through untouched. Where an affected step meets an untouched one the
// dry sound fades the Valens way too (12 ms out before a reversed step, 2 ms in after an affected one).
#pragma once

#include "YanceyDSP.hpp"

#include <climits>
#include <random>
#include <vector>

namespace yancey {

// ---------------------------------------------------------------------------------------------
// DISTORTION
class ValensDistortion {
public:
    bool warm = false; int homeSlot = -1;      // unused (lets the pool treat every pad alike)
    void prepare(float sr) { ramp.setTime(sr, 0.004f); ramp.v = 0.f; amtSm.setTime(sr, 0.01f); amtSm.y = tAmt; }
    void clear() {}
    void block(const BlockCtx&, float pct) { tAmt = clampf(pct, 0.f, 100.f) * 0.01f; }
    inline void process(float& l, float& r, bool on) {
        const float g = ramp.tick(on);
        const float amount = amtSm.tick(tAmt);
        if (g <= 0.f || amount < 0.0005f) return;               // 0 % = true bypass
        // Valens' applyDistortion, one sample
        const float a = amount * 40.0f;
        const float makeup = 0.85f / (1.0f + amount * 0.9f);
        float x = clampf(l, -1.0f, 1.0f), y = clampf(r, -1.0f, 1.0f);
        if (a > 0) { x = ((1 + a) * x) / (1 + a * std::fabs(x)); y = ((1 + a) * y) / (1 + a * std::fabs(y)); }
        x *= makeup; y *= makeup;
        l += (x - l) * g; r += (y - r) * g;
    }
private:
    Ramp ramp; Smooth amtSm;
    float tAmt = 0.5f;
};

// ---------------------------------------------------------------------------------------------
// STUTTER (reverse = false) and REVERSE (reverse = true) on the 1/16 grid
class ValensGrid {
public:
    bool warm = false; int homeSlot = -1;      // the pool keeps it listening while a pad has it
    explicit ValensGrid(bool rev) : fRev(rev) {}

    void prepare(float sr) {
        fs = sr;
        size = (int)(4.0f * fs) + 16;                          // 1/16 steps up to 1 s, twice over
        hist[0].assign(size, 0.f); hist[1].assign(size, 0.f);
        n = 0; curStep = LONG_MIN; curAff = prevAff = false;
        att = std::max(1.0, std::round(0.002 * fs));
        seedNow = -1.f;
    }
    // keep what it has heard: only forget which steps are affected
    void clear() { curAff = prevAff = false; }
    void block(const BlockCtx&, float pct, float seedNorm) {
        amount = clampf(pct, 0.f, 100.f) * 0.01f;
        if (seedNorm != seedNow) { seedNow = seedNorm; makePattern((unsigned)std::lround(clampf(seedNorm, 0.f, 1.f) * 65535.f)); }
    }
    float phase = -1.f;

    inline void process(float& l, float& r, bool on, const Clock& ck) {
        const int wi = (int)(n % size);
        hist[0][wi] = l; hist[1][wi] = r;

        const double stepLen = ck.spb / 4.0;                   // a 1/16 in samples
        const double q = ck.beat * 4.0;
        const long step = (long)std::floor(q);
        if (step != curStep) {                                 // a new step (a new "hit")
            prevAff = curAff;
            curStep = step;
            stepStart = std::round((double)n - (q - (double)step) * stepLen);   // whole samples, like Valens
            curAff = on && affected(step);
        }
        const double j = (double)n - stepStart;                // where we are in this step, in samples
        ++n;
        phase = curAff ? (float)clampf((float)(j / stepLen), 0.f, 1.f) : -1.f;

        if (!curAff) {
            // untouched: dry, with Valens' fades where it meets an affected step
            float g = 1.f;
            if (prevAff && j < att) g *= (float)(j / att);
            if (fRev && on && affected(step + 1)) {
                const double rel = std::max(1.0, std::min(std::round(0.012 * fs), std::floor(0.3 * std::round(stepLen)))), rem = stepLen - j;
                if (rem < rel) g *= (float)std::max(0.0, rem / rel);
            }
            l *= g; r *= g;
            return;
        }

        if (!fRev) {
            const double sub = ck.spb / 8.0;                   // a 1/32 at the current tempo
            const int reps = std::min(4, (int)std::floor(stepLen / sub + 1e-9));
            if (reps < 2) return;                              // doesn't fit: plays normally
            int i = reps - 1;                                  // which repeat: they start at round(i * sub), as in Valens
            while (i > 0 && j < std::round(i * sub)) --i;
            const double k = j - std::round(i * sub), len = std::max(8.0, std::round(sub * 0.92));
            if (k >= len || i >= reps) { l = r = 0.f; return; }
            float g = 1.0f - i * 0.14f;
            if (!(i == 0 && !prevAff) && k < att) g *= (float)(k / att);     // the first one carries on from dry
            const double rel = std::max(1.0, std::min(std::round(0.012 * fs), std::floor(0.3 * len))), rem = len - k;
            if (rem < rel) g *= (float)(rem / rel);
            const double src = stepStart + k;                  // restart from the step's beginning
            l = g * read(0, src); r = g * read(1, src);
        } else {
            const double len = std::round(std::min(stepLen, 2.0 * fs));    // up to the next hit (max 2 s)
            if (j >= len) { l = r = 0.f; return; }
            float g = 1.f;
            if (j < att) g *= (float)(j / att);
            const double rel = std::max(1.0, std::min(std::round(0.012 * fs), std::floor(0.3 * len))), rem = len - j;
            if (rem < rel) g *= (float)(rem / rel);
            const double src = (stepStart - len) + (len - 1.0 - j); // the previous step, last sample first
            l = g * read(0, src); r = g * read(1, src);
        }
    }

private:
    bool fRev;
    float fs = 44100.f;
    std::vector<float> hist[2];
    int size = 0;
    long long n = 0;
    long curStep = LONG_MIN;
    double stepStart = 0.0, att = 88.0;
    bool curAff = false, prevAff = false;
    float amount = 0.5f, seedNow = -1.f;
    float rnd[16] = {};

    bool affected(long step) const { return amount > 0.f && rnd[(int)(((step % 16) + 16) % 16)] < amount; }

    // Valens' HitRandom::draw for each of the 16 steps: r1 then r2 from a mersenne twister
    void makePattern(unsigned seed) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int s = 0; s < 16; ++s) { const float r1 = u(rng), r2 = u(rng); rnd[s] = fRev ? r2 : r1; }
    }
    // linear interpolation at an absolute sample position in the history
    inline float read(int c, double pos) const {
        if (pos < 0.0) pos = 0.0;
        const long long ip = (long long)pos;
        const float fr = (float)(pos - (double)ip);
        if (ip >= n) return hist[c][(int)((n - 1) % size)];
        const float a = hist[c][(int)(ip % size)];
        const float b = ip + 1 < n ? hist[c][(int)((ip + 1) % size)] : a;
        return a + (b - a) * fr;
    }
};

} // namespace yancey
