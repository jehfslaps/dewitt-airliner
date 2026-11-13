// Dewitt: the DSP modules.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <climits>

#include "YanceyParams.hpp"

namespace yancey {

static constexpr float kPi = 3.14159265358979f;

static inline float dbToLin(float db) { return std::pow(10.f, db * 0.05f); }

// Soft safety ceiling: transparent below 0.8, never exceeds 1.0
static inline float softCeil(float y)
{
    const float a = std::fabs(y);
    if (a <= 0.8f) return y;
    const float s = y < 0.f ? -1.f : 1.f;
    return s * (0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f));
}

static inline float smoothstep01(float t)
{
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return t * t * (3.f - 2.f * t);
}

// 4-point cubic Hermite interpolation
static inline float hermite(float xm1, float x0, float x1, float x2, float t)
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((((a * t) - b) * t + c) * t + x0);
}

struct Rng {
    uint32_t s = 0x9E3779B9u;
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() { return (next() >> 8) * (1.0f / 16777216.0f); }       // 0..1
    inline float bi()  { return uni() * 2.f - 1.f; }                            // -1..1
};

struct Smooth {
    float y = 0.f, coef = 0.01f;
    void setTime(float fs, float seconds) { coef = 1.f - std::exp(-1.f / std::max(1.f, seconds * fs)); }
    inline float tick(float target) { y += (target - y) * coef; return y; }
};

// Linear on/off ramp used for click-free module switching
struct Ramp {
    float v = 0.f, step = 0.001f;
    void setTime(float fs, float seconds) { step = 1.f / std::max(1.f, seconds * fs); }
    inline float tick(bool on) {
        if (on) { v += step; if (v > 1.f) v = 1.f; }
        else    { v -= step; if (v < 0.f) v = 0.f; }
        return v;
    }
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    void setLP(float fs, float f, float q) {
        f = std::min(f, fs * 0.45f);
        const float w = 2.f * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2.f * q), n = 1.f / (1.f + al);
        b0 = (1.f - c) * 0.5f * n; b1 = (1.f - c) * n; b2 = b0; a1 = -2.f * c * n; a2 = (1.f - al) * n;
    }
    void setHP(float fs, float f, float q) {
        f = std::min(f, fs * 0.45f);
        const float w = 2.f * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2.f * q), n = 1.f / (1.f + al);
        b0 = (1.f + c) * 0.5f * n; b1 = -(1.f + c) * n; b2 = b0; a1 = -2.f * c * n; a2 = (1.f - al) * n;
    }
    // peaking EQ (RBJ cookbook)
    void setPeak(float fs, float f, float q, float db) {
        f = std::min(f, fs * 0.45f);
        const float A = std::pow(10.f, db / 40.f), w = 2.f * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2.f * q);
        const float n = 1.f / (1.f + al / A);
        b0 = (1.f + al * A) * n; b1 = -2.f * c * n; b2 = (1.f - al * A) * n; a1 = -2.f * c * n; a2 = (1.f - al / A) * n;
    }
    inline float tick(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct OnePole {
    float a = 0.f, y = 0.f;
    void setLP(float fs, float f) { a = 1.f - std::exp(-2.f * kPi * f / fs); }
    inline float lp(float x) { y += a * (x - y); return y; }
    inline float hp(float x) { y += a * (x - y); return x - y; }
};

struct BlockCtx {
    float fs = 44100.f;
    double bpm = 120.0;
    bool limit = false;
};

static const float kButterQ = 0.70710678f;

// ---------------------------------------------------------------------------------------------
// 01 Isolator: three-band Linkwitz-Riley (24 dB/oct) split, per-band kill to +6 dB
class Isolator {
public:
    Ramp ramp; Smooth gl, gm, gh;
    float f1 = 250, f2 = 2500, cf1 = -1, cf2 = -1;

    void prepare(float fs) {
        ramp.setTime(fs, 0.008f);
        gl.setTime(fs, 0.004f); gm.setTime(fs, 0.004f); gh.setTime(fs, 0.004f);
        gl.y = gm.y = gh.y = 1.f;
        cf1 = cf2 = -1;
        reset();
    }
    void reset() {
        for (int c = 0; c < 2; ++c) for (int s = 0; s < 2; ++s) {
            lp1[c][s].reset(); hp1[c][s].reset(); lp2[c][s].reset(); hp2[c][s].reset(); apl[c][s].reset(); aph[c][s].reset();
        }
    }
    void block(const BlockCtx& ctx, float lowN, float midN, float highN, float split1, float split2) {
        tl = isoGain(lowN); tm = isoGain(midN); th = isoGain(highN);
        f1 = split1; f2 = std::max(split2, split1 * 1.6f);
        if (std::fabs(f1 - cf1) > 0.5f || std::fabs(f2 - cf2) > 0.5f) {
            cf1 = f1; cf2 = f2;
            for (int c = 0; c < 2; ++c) for (int s = 0; s < 2; ++s) {
                lp1[c][s].setLP(ctx.fs, f1, kButterQ); hp1[c][s].setHP(ctx.fs, f1, kButterQ);
                lp2[c][s].setLP(ctx.fs, f2, kButterQ); hp2[c][s].setHP(ctx.fs, f2, kButterQ);
                apl[c][s].setLP(ctx.fs, f2, kButterQ); aph[c][s].setHP(ctx.fs, f2, kButterQ);
            }
        }
    }
    inline void process(float& l, float& r, bool on) {
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        const float g1 = gl.tick(tl), g2 = gm.tick(tm), g3 = gh.tick(th);
        float* io[2] = { &l, &r };
        for (int c = 0; c < 2; ++c) {
            const float x = *io[c];
            float low = lp1[c][1].tick(lp1[c][0].tick(x));
            low = apl[c][1].tick(apl[c][0].tick(low)) + aph[c][1].tick(aph[c][0].tick(low));
            const float rest = hp1[c][1].tick(hp1[c][0].tick(x));
            const float mid = lp2[c][1].tick(lp2[c][0].tick(rest));
            const float high = hp2[c][1].tick(hp2[c][0].tick(rest));
            const float y = low * g1 + mid * g2 + high * g3;
            *io[c] = x + (y - x) * a;
        }
    }
private:
    float tl = 1, tm = 1, th = 1;
    Biquad lp1[2][2], hp1[2][2], lp2[2][2], hp2[2][2], apl[2][2], aph[2][2];
};

// ---------------------------------------------------------------------------------------------
// 02 Delay: tempo-synced stereo delay; the input gate closes on OFF so echoes ring out (throws)
class Delay {
public:
    Ramp ramp; Smooth timeSm, fbSm, mixSm;
    float fs = 44100;
    std::vector<float> buf[2];
    int size = 0, w = 0;

    void prepare(float sr) {
        fs = sr;
        size = (int)(fs * 4.2f) + 8;
        buf[0].assign(size, 0.f); buf[1].assign(size, 0.f);
        w = 0;
        ramp.setTime(fs, 0.006f);
        timeSm.setTime(fs, 0.04f); fbSm.setTime(fs, 0.004f); mixSm.setTime(fs, 0.004f);
        timeSm.y = fs * 0.375f;
        chf = clf = -1;
        for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); }
    }
    void reset() { if (size) { std::fill(buf[0].begin(), buf[0].end(), 0.f); std::fill(buf[1].begin(), buf[1].end(), 0.f); } }
    void block(const BlockCtx& ctx, int timeIdx, float fbPct, float mixPct, float hpf, float lpf) {
        timeIdx = timeIdx < 0 ? 0 : (timeIdx > 12 ? 12 : timeIdx);
        double sec = kDelayTimeBeats[timeIdx] * 60.0 / std::max(20.0, ctx.bpm);
        while (sec > 2.0) sec *= 0.5;   // SX limit: about 2000 ms
        tTime = (float)(sec * fs);
        float fb = fbPct * 0.01f;
        if (ctx.limit) fb = std::min(fb, 0.8f);
        tFb = fb;
        tMix = mixPct * 0.01f;
        if (std::fabs(hpf - chf) > 0.5f || std::fabs(lpf - clf) > 1.f) {
            chf = hpf; clf = lpf;
            for (int c = 0; c < 2; ++c) { hp[c].setHP(fs, hpf, kButterQ); lp[c].setLP(fs, lpf, kButterQ); }
        }
        // stay asleep once fully off and the tail has died away
        idle = (ramp.v <= 0.f && tail < 1e-6f);
    }
    inline void process(float& l, float& r, bool on) {
        if (!on && idle) return;
        const float a = ramp.tick(on);
        const float d = timeSm.tick(tTime);
        const float fb = fbSm.tick(tFb);
        const float mix = mixSm.tick(tMix);
        const float dryG = std::min(1.f, 2.f * (1.f - mix));
        const float wetG = std::min(1.f, 2.f * mix);
        float* io[2] = { &l, &r };
        float peak = 0.f;
        const float rp = (float)w - d;
        float rpos = rp < 0.f ? rp + (float)size : rp;
        const int i0 = (int)rpos;
        const float t = rpos - (float)i0;
        for (int c = 0; c < 2; ++c) {
            float* b = buf[c].data();
            const int im1 = (i0 - 1 + size) % size, i1 = (i0 + 1) % size, i2 = (i0 + 2) % size;
            const float echo = hermite(b[im1], b[i0], b[i1], b[i2], t);
            const float filt = lp[c].tick(hp[c].tick(echo));
            const float x = *io[c];
            b[w] = x * a + softCeil(filt * fb);
            peak = std::max(peak, std::fabs(filt));
            *io[c] = x * (1.f + (dryG - 1.f) * a) + filt * wetG;
        }
        tail = std::max(tail * 0.99995f, peak);
        if (++w >= size) w = 0;
    }
private:
    float tTime = 0, tFb = 0.4f, tMix = 0.35f, chf = -1, clf = -1, tail = 0.f;
    bool idle = true;
    Biquad hp[2], lp[2];
};

// ---------------------------------------------------------------------------------------------
// 03 BPM Looper: grabs a tempo-synced slice from the moment it is engaged and repeats it
class BpmLooper {
public:
    Ramp ramp; Smooth mixSm;
    float fs = 44100;
    std::vector<float> buf[2];
    int cap = 0;
    enum { kIdle, kRec, kPlay } st = kIdle;
    int recPos = 0, recLen = 0, playPos = 0, lenNow = 0, elapsed = 0, retrigPeriod = 0;
    bool wasOn = false;
    float phase = -1.f;

    void prepare(float sr) {
        fs = sr;
        cap = (int)(fs * 6.2f);
        buf[0].assign(cap, 0.f); buf[1].assign(cap, 0.f);
        ramp.setTime(fs, 0.005f);
        mixSm.setTime(fs, 0.004f); mixSm.y = 1.f;
        st = kIdle; wasOn = false;
    }
    void block(const BlockCtx& ctx, int lenIdx, int retrig, float mixPct) {
        lenIdx = lenIdx < 0 ? 0 : (lenIdx > 5 ? 5 : lenIdx);
        const double spb = 60.0 / std::max(20.0, ctx.bpm) * fs;
        lenNow = std::max(32, std::min(cap - 1, (int)(kLoopLenBeats[lenIdx] * spb)));
        retrigPeriod = retrig > 0 ? std::max(64, (int)(retrig * 0.5 * spb)) : 0;
        tMix = mixPct * 0.01f;
    }
    bool canStart = true, pending = false;   // GRID timing: a press waits for the next 1/16
    inline void process(float& l, float& r, bool on) {
        if (on && !wasOn) { if (canStart) { st = kRec; recPos = 0; recLen = lenNow; elapsed = 0; } else pending = true; }
        if (pending) {
            if (!on) pending = false;
            else if (canStart) { pending = false; st = kRec; recPos = 0; recLen = lenNow; elapsed = 0; }
        }
        if (!on && wasOn) { st = kIdle; }
        wasOn = on;
        const float a = ramp.tick(on);
        if (a <= 0.f && st == kIdle) { phase = -1.f; return; }
        const float mix = mixSm.tick(tMix);
        float lo = l, ro = r;
        if (st == kRec) {
            buf[0][recPos] = l; buf[1][recPos] = r;
            if (++recPos >= recLen) { st = kPlay; playPos = 0; }
            phase = (float)recPos / (float)recLen;
        } else if (st == kPlay) {
            const int L = std::max(32, std::min(lenNow, recLen));
            if (playPos >= L) playPos = 0;
            const float F = std::max(8.f, std::min(fs * 0.002f, L * 0.125f));
            const float g = std::min(1.f, std::min((playPos + 1) / F, (L - playPos) / F));
            lo = buf[0][playPos] * g; ro = buf[1][playPos] * g;
            phase = (float)playPos / (float)L;
            if (++playPos >= L) playPos = 0;
        }
        if (on && retrigPeriod > 0 && ++elapsed >= retrigPeriod) {
            elapsed = 0; st = kRec; recPos = 0; recLen = lenNow;
        }
        const float yl = l + (lo - l) * mix, yr = r + (ro - r) * mix;
        l += (yl - l) * a; r += (yr - r) * a;
    }
private:
    float tMix = 1.f;
};

// ---------------------------------------------------------------------------------------------
// 04 DJFX Looper: freezes the last LENGTH of audio and plays it at variable speed/direction
class DjfxLooper {
public:
    Ramp ramp; Smooth mixSm, speedSm, lenSm;
    float fs = 44100;
    std::vector<float> hist[2], loop[2];
    int histSize = 0, hw = 0, maxLen = 0;
    bool wasOn = false, frozen = false;
    double p = 0.0;
    float phase = -1.f;
    bool warm = false;      // JAY: keep listening while the pad is up (set by the pool)
    int homeSlot = -1;      // the DJ Looper pad this looper listens for

    void prepare(float sr) {
        fs = sr;
        maxLen = (int)(fs * 0.76f) + 8;
        histSize = maxLen + 16;
        hist[0].assign(histSize, 0.f); hist[1].assign(histSize, 0.f);
        loop[0].assign(maxLen, 0.f); loop[1].assign(maxLen, 0.f);
        hw = 0;
        ramp.setTime(fs, 0.004f);
        mixSm.setTime(fs, 0.004f); mixSm.y = 1.f;
        lenSm.setTime(fs, 0.008f); lenSm.y = fs * 0.12f;
        speedSm.setTime(fs, 0.01f); speedSm.y = 1.f;
        wasOn = frozen = false;
    }
    void clear() { std::fill(hist[0].begin(), hist[0].end(), 0.f); std::fill(hist[1].begin(), hist[1].end(), 0.f); frozen = false; wasOn = false; }
    void unfreeze() { frozen = false; wasOn = false; ramp.v = 0.f; }
    // keep the recent-past buffer filled even while another effect owns the slot
    inline void feed(float l, float r) { hist[0][hw] = l; hist[1][hw] = r; if (++hw >= histSize) hw = 0; }
    void block(const BlockCtx&, float lenMs, float speed, float mixPct, float glidePct) {
        tLen = std::max(8.f, std::min((float)(maxLen - 4), lenMs * 0.001f * fs));
        tSpeed = speed;
        tMix = mixPct * 0.01f;
        speedSm.setTime(fs, 0.004f + (glidePct * 0.01f) * (glidePct * 0.01f) * 0.9f);
    }
    inline void process(float& l, float& r, bool on) {
        // always remember the recent past so a press loops what was just heard
        hist[0][hw] = l; hist[1][hw] = r;
        if (++hw >= histSize) hw = 0;

        if (on && !wasOn) {
            for (int i = 0; i < maxLen; ++i) {
                int idx = hw - maxLen + i; if (idx < 0) idx += histSize;
                loop[0][i] = hist[0][idx]; loop[1][i] = hist[1][idx];
            }
            frozen = true; p = 0.0; speedSm.y = tSpeed; lenSm.y = tLen;
        }
        wasOn = on;
        const float a = ramp.tick(on);
        if (a <= 0.f) { frozen = false; phase = -1.f; return; }
        if (!frozen) return;

        const float L = lenSm.tick(tLen);
        const float sp = speedSm.tick(tSpeed);
        const float mix = mixSm.tick(tMix);
        const int base = maxLen - (int)L;
        if (p >= L) p = std::fmod(p, (double)L);
        if (p < 0.0) p = std::fmod(p, (double)L) + L;

        const int i0 = (int)p;
        const float t = (float)(p - i0);
        const int Li = (int)L;
        auto at = [&](int c, int k) { k %= Li; if (k < 0) k += Li; return loop[c][base + k]; };
        const float F = std::max(4.f, std::min(fs * 0.002f, L / 6.f));
        const float edge = std::min(1.f, std::min((float)p / F, (L - (float)p) / F));
        const float env = smoothstep01(std::fabs(sp) / 0.12f);
        const float g = edge * env;
        const float lo = hermite(at(0, i0 - 1), at(0, i0), at(0, i0 + 1), at(0, i0 + 2), t) * g;
        const float ro = hermite(at(1, i0 - 1), at(1, i0), at(1, i0 + 1), at(1, i0 + 2), t) * g;
        phase = (float)(p / L);
        p += sp;

        const float yl = l + (lo - l) * mix, yr = r + (ro - r) * mix;
        l += (yl - l) * a; r += (yr - r) * a;
    }
private:
    float tLen = 5000, tSpeed = 1, tMix = 1;
};

// ---------------------------------------------------------------------------------------------
// The DJ Looper pad (Dewitt 1.2.1): a tape loop. Pressing the pad grabs the last LENGTH of audio onto a
// loop of "tape" that keeps going round: the loop is a delay line that plays back into itself. Turning
// LENGTH moves the play head like a tape machine would, with weight (the head eases toward the new length
// instead of jumping), and because what it plays is recorded back onto the loop, the pitch change sticks:
// a shorter loop plays the same audio faster and higher, a longer one slower and lower, as tape does.
// (The Freeze move still uses DjfxLooper above.)
class TapeLooper {
public:
    Ramp ramp; Smooth mixSm, speedSm;
    float fs = 44100;
    float phase = -1.f;
    bool warm = false;      // keep listening while the pad is up (set by the pool)
    int homeSlot = -1;      // the DJ Looper pad this looper listens for

    // how the play head moves when LENGTH changes: a quick ease-in, then a heavier settle
#ifndef TAPE_EASE_S
#define TAPE_EASE_S 0.002f      // fitted by measurement
#define TAPE_SETTLE_S 0.084f
#endif
    static constexpr float kEaseIn = TAPE_EASE_S, kSettle = TAPE_SETTLE_S;

    void prepare(float sr) {
        fs = sr;
        maxLen = (int)(fs * 0.76f) + 8;
        fade = std::max(4, (int)(fs * 0.002f));
        histSize = maxLen + fade + 16;
        hist[0].assign(histSize, 0.f); hist[1].assign(histSize, 0.f);
        tape[0].assign(maxLen + 64, 0.f); tape[1].assign(maxLen + 64, 0.f);
        tapeSize = maxLen + 64;
        hw = tw = 0;
        ramp.setTime(fs, 0.004f);
        mixSm.setTime(fs, 0.004f); mixSm.y = 1.f;
        speedSm.setTime(fs, 0.01f); speedSm.y = 1.f;
        cEase = 1.f - std::exp(-1.f / std::max(1.f, kEaseIn * fs));
        cSettle = 1.f - std::exp(-1.f / std::max(1.f, kSettle * fs));
        for (int c = 0; c < 2; ++c) loopHP[c].setHP(fs, 20.4f, 0.43f);
        wasOn = looping = false;
    }
    void clear() { std::fill(hist[0].begin(), hist[0].end(), 0.f); std::fill(hist[1].begin(), hist[1].end(), 0.f); looping = false; wasOn = false; }
    void unfreeze() { looping = false; wasOn = false; ramp.v = 0.f; }
    inline void feed(float l, float r) { hist[0][hw] = l; hist[1][hw] = r; if (++hw >= histSize) hw = 0; }
    void block(const BlockCtx&, float lenMs, float speed, float mixPct, float glidePct) {
        tLen = std::max(8.f, std::min((float)(maxLen - 4), lenMs * 0.001f * fs));
        tSpeed = speed;
        tMix = mixPct * 0.01f;
        speedSm.setTime(fs, 0.004f + (glidePct * 0.01f) * (glidePct * 0.01f) * 0.9f);
    }
    inline void process(float& l, float& r, bool on) {
        feed(l, r);                                      // always remember the recent past
        if (on && !wasOn) grab();
        wasOn = on;
        const float a = ramp.tick(on);
        if (a <= 0.f) { looping = false; phase = -1.f; return; }
        if (!looping) return;

        // the play head eases toward the new length
        d1 += (tLen - d1) * cEase;
        D += (d1 - D) * cSettle;
        const int w0 = tw;
        const double rp = (double)w0 - D;                // play head, D samples behind the record head
        const float yl = readTape(0, rp), yr = readTape(1, rp);
        tape[0][tw] = yl; tape[1][tw] = yr;              // what plays goes back onto the tape
        if (++tw >= tapeSize) tw = 0;

        // SPEED: a second head that reads the same loop faster, slower or backwards
        const float sp = speedSm.tick(tSpeed);
        float lo = yl, ro = yr;
        if (sp != 1.f) {
            u += (double)sp - 1.0;
            while (u < 0.0) u += D;
            while (u >= D) u -= D;
            double q = rp + u;
            if (q > (double)w0 - 3.0) q -= D;            // right behind the record head: read a lap earlier
            lo = readTape(0, q); ro = readTape(1, q);
        } else u = 0.0;
        const float env = smoothstep01(std::fabs(sp) / 0.12f);
        lo *= env; ro *= env;
        // the loop's output loses its deepest lows (measured: -3 dB around 38 Hz), so a slowed-down loop
        // doesn't pile up rumble
        lo = loopHP[0].tick(lo); ro = loopHP[1].tick(ro);
        ph += sp / D; ph -= std::floor(ph);
        phase = (float)ph;

        const float mix = mixSm.tick(tMix);
        const float ml = l + (lo - l) * mix, mr = r + (ro - r) * mix;
        l += (ml - l) * a; r += (mr - r) * a;
    }
private:
    std::vector<float> hist[2], tape[2];
    int histSize = 0, hw = 0, maxLen = 0, tapeSize = 0, tw = 0, fade = 88;
    bool wasOn = false, looping = false;
    float cEase = 0.f, cSettle = 0.f;
    Biquad loopHP[2];
    double D = 5000.0, d1 = 5000.0, u = 0.0, ph = 0.0;
    float tLen = 5000, tSpeed = 1, tMix = 1;

    // the last LENGTH of audio goes onto the tape, its end blended into what came just before its start
    // (so the join is smooth), repeated round the whole tape so the head can move anywhere on it
    void grab() {
        const int Dg = std::max(8, std::min(maxLen - 4, (int)std::lround(tLen)));
        const int F = std::min(fade, Dg / 4);
        auto h = [&](int c, int back) { int i = hw - back; while (i < 0) i += histSize; return hist[c][i]; };
        for (int c = 0; c < 2; ++c) {
            // loop content k = 0..Dg-1 is the input from Dg samples ago up to now
            for (int k = 0; k < tapeSize; ++k) {
                const int j = ((k - (tapeSize - Dg)) % Dg + Dg) % Dg;      // tape[tapeSize-Dg] = loop start
                float v = h(c, Dg - j);
                if (j >= Dg - F) {                                         // the end flows into the start
                    const float g = (float)(j - (Dg - F) + 1) / (float)(F + 1);
                    v = v * (1.f - g) + h(c, 2 * Dg - j) * g;
                }
                tape[c][k] = v;
            }
        }
        tw = 0;
        loopHP[0].reset(); loopHP[1].reset();
        D = d1 = (double)Dg;
        u = 0.0; ph = 0.0;
        speedSm.y = tSpeed;
        looping = true;
    }
    inline float readTape(int c, double pos) const {
        double p = std::fmod(pos, (double)tapeSize); if (p < 0) p += tapeSize;
        const int i0 = (int)p; const float t = (float)(p - i0);
        auto at = [&](int k) { k %= tapeSize; if (k < 0) k += tapeSize; return tape[c][k]; };
        // straight-line reading between samples: each lap while the head moves takes a little of the top off,
        // so a loop you keep working darkens and softens, like tape wearing (measured)
        return at(i0) + (at(i0 + 1) - at(i0)) * t;
    }
};

// ---------------------------------------------------------------------------------------------
// 05 Vinyl Sim: compression, wow/flutter, surface noise, playback bandwidth
// Order: record speed (wow/flutter) -> surface noise on the record -> playback
// system bandwidth -> compression -> level.
class VinylSim {
public:
    bool warm = false;      // the Vinyl Sim pad sets this: keep running while the pad is up (see process)
    int homeSlot = -1;
    Ramp ramp; Smooth compSm, wfSm, noiseSm, levelSm, baseSm;
    float fs = 44100;
    std::vector<float> dl[2];
    int dsize = 0, dw = 0;
    Rng rng;

    void prepare(float sr) {
        fs = sr;
        dsize = (int)(fs * 0.08f) + 8;
        dl[0].assign(dsize, 0.f); dl[1].assign(dsize, 0.f);
        dw = 0;
        ramp.setTime(fs, 0.008f);
        compSm.setTime(fs, 0.004f); wfSm.setTime(fs, 0.01f); noiseSm.setTime(fs, 0.004f); levelSm.setTime(fs, 0.004f);
        levelSm.y = 1.f;
        baseSm.setTime(fs, 0.35f); baseSm.y = fs * 0.0005f + 2.f;
        attC = 1.f - std::exp(-1.f / (0.004f * fs));
        rmsC = 1.f - std::exp(-1.f / (0.006f * fs));
        hissHP[0].setLP(fs, 1500.f); hissHP[1].setLP(fs, 1500.f);
        hissLP[0].setLP(fs, 9000.f); hissLP[1].setLP(fs, 9000.f);
        crackHP.setLP(fs, 900.f); crackLP.setLP(fs, 7500.f);
        rumbleLP.setLP(fs, 28.f);
        driftLP.setLP(fs, 0.25f); flutNoiseLP.setLP(fs, 12.f);
        sideHP.setLP(fs, 160.f);
        cFreq = -1.f;
        clear();
    }
    void clear() {
        std::fill(dl[0].begin(), dl[0].end(), 0.f); std::fill(dl[1].begin(), dl[1].end(), 0.f);
        env = ms = 0.f; crackAmp = popAmp = 0.f;
        for (int c = 0; c < 2; ++c) { hp[c].reset(); hp2[c].reset(); lp[c].reset(); lp2[c].reset(); bump[c].reset(); hissHP[c].y = hissLP[c].y = 0.f; }
        sideHP.y = 0.f;
    }
    // comp, noise, wow/flutter, frequency, level: 0..100 like the hardware
    void block(const BlockCtx&, float comp, float noise, float wowFlut, float freq, float level) {
        tComp = comp * 0.01f; tNoise = noise * 0.01f; tWf = wowFlut * 0.01f;
        const float l = level / 80.f; tLevel = l * l;            // 80 = unity, 100 = about +4 dB
        const float f = clampf(freq * 0.01f, 0.f, 1.f);
        if (std::fabs(f - cFreq) > 0.002f) {
            cFreq = f;
            // FREQUENCY 0: thin, boxy playback (old record, cheap deck); 100: full range
            const float hpf = 260.f * std::pow(22.f / 260.f, f);
            const float lpf = std::min(2800.f * std::pow(17000.f / 2800.f, f), fs * 0.45f);
            const float bumpHz = 1100.f + 1600.f * f, bumpDb = 4.5f * (1.f - f) + 1.f;
            for (int c = 0; c < 2; ++c) {
                hp[c].setHP(fs, hpf, kButterQ); hp2[c].setHP(fs, hpf, 0.54f);
                lp[c].setLP(fs, lpf, 0.9f - 0.3f * f); lp2[c].setLP(fs, lpf * 1.15f, kButterQ);
                bump[c].setPeak(fs, bumpHz, 0.8f, bumpDb);
            }
        }
    }
    inline void process(float& l, float& r, bool on) {
        // warm (the Vinyl Sim pad): keep running while off, so a press starts settled, and fade on a curve
        const float ar = ramp.tick(on);
        const float a = warm ? smoothstep01(ar) : ar;
        if (a <= 0.f && !warm) return;
        const float xl = l, xr = r;
        float yl = l, yr = r;

        // 1. WOW FLUT: a 33 1/3 rpm wobble (0.55 Hz) with slow drift, plus fast irregular flutter
        const float wf = wfSm.tick(tWf);
        const float wd = std::pow(wf, 1.5f) * 0.0040f * fs;
        const float fd = std::pow(wf, 1.8f) * 0.00035f * fs;
        wowPh += 0.5555f / fs; if (wowPh >= 1.f) wowPh -= 1.f;
        flutPh += 7.3f / fs;   if (flutPh >= 1.f) flutPh -= 1.f;
        const float drift = clampm(driftLP.lp(rng.bi()) * 9.f);
        const float fnoise = clampm(flutNoiseLP.lp(rng.bi()) * 3.f);
        const float mod = wd * (0.8f * std::sin(2.f * kPi * wowPh) + 0.35f * drift)
                        + fd * (0.6f * std::sin(2.f * kPi * flutPh) + 0.6f * fnoise);
        const float base = baseSm.tick(fs * 0.0005f + 3.f + 1.3f * (wd + fd));
        dl[0][dw] = yl; dl[1][dw] = yr;
        {
            float rp = (float)dw - std::max(2.f, base + mod);
            if (rp < 0.f) rp += (float)dsize;
            const int i0 = (int)rp; const float t = rp - (float)i0;
            const int im1 = (i0 - 1 + dsize) % dsize, i1 = (i0 + 1) % dsize, i2 = (i0 + 2) % dsize;
            yl = hermite(dl[0][im1], dl[0][i0], dl[0][i1], dl[0][i2], t);
            yr = hermite(dl[1][im1], dl[1][i0], dl[1][i1], dl[1][i2], t);
        }
        if (++dw >= dsize) dw = 0;

        // 2. NOISE: crackle, the odd pop, hiss, rumble, and a tick once per revolution when it's dirty
        const float n = noiseSm.tick(tNoise);
        if (n > 0.001f) {
            const float hissAmt = std::pow(n, 1.3f) * 0.005f;
            const float hl = hissLP[0].lp(hissHP[0].hp(rng.bi())) * hissAmt;
            const float hr = hissLP[1].lp(hissHP[1].hp(rng.bi())) * hissAmt;
            if (rng.uni() < (4.f + 60.f * n * n) / fs) {
                const float u = rng.uni();
                crackAmp = n * 0.3f * (0.1f + 0.9f * u * u * u * u);
                crackDecay = std::exp(-1.f / ((0.0001f + 0.0004f * rng.uni()) * fs));
                crackPan = rng.bi() * 0.5f;
            }
            const float crack = crackLP.lp(crackHP.hp(crackAmp * rng.bi()));
            crackAmp *= crackDecay;
            if (rng.uni() < (0.1f + 0.8f * n) * n / fs) { popAmp = n * 0.18f; popPh = 0.f; popPan = rng.bi() * 0.3f; }
            popPh += 55.f / fs;
            const float pop = popAmp * std::sin(2.f * kPi * popPh);
            popAmp *= 1.f - 1.f / (0.005f * fs);
            // a scratch that comes round every revolution
            revPh += 0.5555f / fs;
            if (revPh >= 1.f) { revPh -= 1.f; if (n > 0.55f) tickAmp = (n - 0.55f) * 0.5f; }
            const float tick = tickAmp * rng.bi(); tickAmp *= 0.996f;
            const float rumble = rumbleLP.lp(rng.bi()) * n * 0.06f;
            yl += hl + crack * (1.f + crackPan) + pop * (1.f + popPan) + tick + rumble;
            yr += hr + crack * (1.f - crackPan) + pop * (1.f - popPan) + tick + rumble;
        }

        // 3. FREQUENCY: the playback system, with the cartridge's mid bump; bass narrows toward mono
        yl = bump[0].tick(lp2[0].tick(lp[0].tick(hp2[0].tick(hp[0].tick(yl)))));
        yr = bump[1].tick(lp2[1].tick(lp[1].tick(hp2[1].tick(hp[1].tick(yr)))));
        {
            const float m = 0.5f * (yl + yr), sd = 0.5f * (yl - yr);
            const float sideHi = sideHP.hp(sd);
            yl = m + sideHi + 0.25f * (sd - sideHi);
            yr = m - sideHi - 0.25f * (sd - sideHi);
        }

        // 4. COMP: the squash, smooth RMS detection, soft knee, a touch of saturation
        const float c = compSm.tick(tComp);
        if (c > 0.001f) {
            const float x2 = 0.5f * (yl * yl + yr * yr);
            ms += rmsC * (x2 - ms);
            const float lvl = 10.f * std::log10(ms + 1e-12f);
            const float relC = 1.f - std::exp(-1.f / ((0.22f - 0.14f * c) * fs));
            env += (lvl > env ? attC : relC) * (lvl - env);
            const float thr = -6.f - 26.f * c, ratio = 1.5f + 8.5f * c, knee = 8.f;
            const float over = env - thr;
            float gr = 0.f;
            if (over > knee * 0.5f) gr = over * (1.f - 1.f / ratio);
            else if (over > -knee * 0.5f) { const float o = over + knee * 0.5f; gr = (1.f - 1.f / ratio) * o * o / (2.f * knee); }
            const float makeup = 0.5f * (-thr) * (1.f - 1.f / ratio);
            const float g = dbToLin(makeup - gr);
            const float k = 1.f + c * 0.8f;
            yl = std::tanh(yl * g * k) / k;
            yr = std::tanh(yr * g * k) / k;
        }

        // 5. LEVEL
        const float lv = levelSm.tick(tLevel);
        yl *= lv; yr *= lv;

        l = xl + (yl - xl) * a;
        r = xr + (yr - xr) * a;
    }
private:
    static float clampm(float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); }
    float tComp = 0.55f, tNoise = 0.3f, tWf = 0.25f, tLevel = 1.f;
    float env = -100.f, ms = 0.f, attC = 0.1f, rmsC = 0.01f, cFreq = -1.f;
    float wowPh = 0.f, flutPh = 0.f, revPh = 0.f;
    float crackAmp = 0.f, crackDecay = 0.f, crackPan = 0.f, popAmp = 0.f, popPh = 0.f, popPan = 0.f, tickAmp = 0.f;
    Biquad hp[2], hp2[2], lp[2], lp2[2], bump[2];
    OnePole hissHP[2], hissLP[2], crackHP, crackLP, rumbleLP, driftLP, flutNoiseLP, sideHP;
};

// ---------------------------------------------------------------------------------------------
// The Vinyl Sim pad (Dewitt 1.2.1): a record deck. Our own code, tuned by measurement (output only),
// knob for knob:
//   WOW FLUT: the platter speed sways up and down in a straight-line "triangle" once a turn (0.567 Hz,
//             up to about +-20 % speed at full), with a 10 Hz flutter on top; depth grows with the square
//             of the knob. The play head only ever falls behind, so at 0 there's no delay at all.
//   COMP:     a squash that lifts quiet sound (up to about +21 dB) and holds loud sound down, even at 0
//             it's gently on; it grabs within a couple of ms and lets go over about 50 ms.
//   NOISE:    a soft, dark surface noise (mono), its level straight with the knob; its hiss "breathes"
//             open and shut once a turn, on its own clock (not the warble's).
//   Tone:     almost flat, a soft top roll-off and a steep cut just under 20 kHz (on the noise too).
// (The older VinylSim above is still used by the combo effects.)
class VinylDeck {
public:
    bool warm = false;      // the Vinyl Sim pad sets this: keep running while the pad is up
    int homeSlot = -1;
    Ramp ramp; Smooth depthSm, noiseSm, compSm;
    float fs = 44100;

    void prepare(float sr) {
        fs = sr;
        dsize = (int)(fs * 0.13f) + 8;
        dl[0].assign(dsize, 0.f); dl[1].assign(dsize, 0.f);
        dw = 0;
        ramp.setTime(fs, 0.008f);
        depthSm.setTime(fs, 0.15f); noiseSm.setTime(fs, 0.01f); compSm.setTime(fs, 0.02f);
        setTone();
        detC = 1.f - std::exp(-1.f / (0.0003f * fs));      // a quick level reading,
        attC = 1.f - std::exp(-1.f / (0.0015f * fs));      // a 1.5 ms grab (on it within ~2 ms)
        relC = 1.f - std::exp(-1.f / (0.050f * fs));
        noiseLP.setLP(fs, 538.f);
        const float a = noiseLP.a;
        noiseNorm = 1.f / std::sqrt(a / (2.f - a) + kFloor * kFloor);
        clear();
    }
    void clear() {
        std::fill(dl[0].begin(), dl[0].end(), 0.f); std::fill(dl[1].begin(), dl[1].end(), 0.f);
        ms = 0.f; gdb = 0.f;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < kMaxTone; ++i) tone[c][i].z1 = tone[c][i].z2 = 0.f;
        noiseLP.y = 0.f; breathLP.y = 0.f;
    }
    // COMP, NOISE, WOW FLUT: 0..100
    void block(const BlockCtx&, float comp, float noise, float wowFlut) {
        tComp = clampf(comp, 0.f, 100.f) * 0.01f;
        tNoise = clampf(noise, 0.f, 100.f) * 0.01f;
        const float w = clampf(wowFlut, 0.f, 100.f) * 0.01f;
        tDepth = std::pow(w, 2.05f);
    }
    inline void process(float& l, float& r, bool on) {
        const float ar = ramp.tick(on);
        const float a = warm ? smoothstep01(ar) : ar;
        if (a <= 0.f && !warm) return;
        const float xl = l, xr = r;

        // 1. WOW FLUT: the play head falls behind and catches up as the platter speed sways
        dl[0][dw] = l; dl[1][dw] = r;
        const float depth = depthSm.tick(tDepth);
        wowPh += kWowHz / fs; if (wowPh >= 1.0) wowPh -= 1.0;
        flutPh += kFlutHz / fs; if (flutPh >= 1.0) flutPh -= 1.0;
        const double I = wowPh <= 0.5 ? wowPh - 2.0 * wowPh * wowPh : 2.0 * wowPh * wowPh - 3.0 * wowPh + 1.0;   // integral of the triangle
        const double back = depth * fs * ((kWowAmt / kWowHz) * (0.125 - I)
                                        + (kFlutAmt / (2.0 * kPi * kFlutHz)) * (1.0 + std::cos(2.0 * kPi * flutPh)));
        float yl = l, yr = r;
        if (back > 0.0005) {
            double rp = (double)dw - back; if (rp < 0.0) rp += dsize;
            const int i0 = (int)rp; const float t = (float)(rp - i0);
            const int i1 = (i0 + 1) % dsize;
            yl = dl[0][i0] + (dl[0][i1] - dl[0][i0]) * t;
            yr = dl[1][i0] + (dl[1][i1] - dl[1][i0]) * t;
        }
        if (++dw >= dsize) dw = 0;

        // 2. COMP: the level decides the gain from the knob's curve, gain moves fast down, slower up
        const float c = compSm.tick(tComp);
        ms += detC * (0.5f * (yl * yl + yr * yr) - ms);
        const float L = 10.f * std::log10(ms + 1e-14f);
        const float want = compGain(L, c);
        gdb += (want < gdb ? attC : relC) * (want - gdb);
        const float g = dbToLin(gdb + 0.75f);              // +0.75: the quick reading rides the waveform a little
        yl *= g; yr *= g;

        // 3. NOISE (mono), then 4. the playback tone over both (the noise goes through the output too)
        const float n = noiseSm.tick(tNoise);
        if (n > 0.0001f) {
            // the surface "breathes" once a turn: its hiss opens up and closes again (a moving top cut)
            breathPh += kBreathHz / fs; if (breathPh >= 1.0) breathPh -= 1.0;
            if ((breathCnt++ & 15) == 0) {
                const double ph = breathPh;
                double u = 0.0;
                if (ph >= kOpen && ph < kPeak) u = (ph - kOpen) / (kPeak - kOpen);
                else if (ph >= kPeak && ph < kShut) u = (kShut - ph) / (kShut - kPeak);
                const double fc = kBreathLo * std::exp(std::log(kBreathHi / kBreathLo) * u);      // (a one-pole is fine at any cutoff)
                breathLP.a = (float)(1.0 - std::exp(-2.0 * kPi * fc / fs));
            }
            const float s = breathLP.lp((noiseLP.lp(gauss()) + kFloor * gauss()) * noiseNorm) * (kNoiseRms * n);
            yl += s; yr += s;
        }
        for (int i = 0; i < nTone; ++i) { yl = tone[0][i].tick(yl); yr = tone[1][i].tick(yr); }
        yl *= toneTrim; yr *= toneTrim;

        l = xl + (yl - xl) * a;
        r = xr + (yr - xr) * a;
    }

private:
    // measured (see the comment above)
    static constexpr double kWowHz = 0.567, kWowAmt = 0.205, kFlutHz = 10.0, kFlutAmt = 0.0243;
    static constexpr float kNoiseRms = 0.00216f, kFloor = 0.0851f;
    // the breathing: once a turn (0.56 Hz) the hiss's top cut opens from about 6 kHz to 21.4 kHz and closes again
    static constexpr double kBreathHz = 0.5599, kBreathLo = 5800.0, kBreathHi = 21400.0, kOpen = 0.365, kPeak = 0.64, kShut = 0.89;
    static constexpr int kMaxTone = 7;
    std::vector<float> dl[2];
    int dsize = 0, dw = 0;
    double wowPh = 0.0, flutPh = 0.0;
    float tComp = 1.f, tNoise = 0.3f, tDepth = 0.03f;
    float ms = 0.f, gdb = 0.f, detC = 0.1f, attC = 0.1f, relC = 0.01f, noiseNorm = 1.f, toneTrim = 1.f;
    Biquad tone[2][kMaxTone];
    int nTone = 0;
    OnePole noiseLP, breathLP;
    double breathPh = 0.0; unsigned breathCnt = 0;
    Rng rng;
    // The playback tone: nearly flat, a soft top roll-off, then a steep cut just under 20 kHz, at each
    // sample rate (the reference runs at 44.1 kHz). Filters designed here by fitting the measured response: within about half a dB on average. Other rates use the 44.1 kHz shape.
    void setTone() {
        struct Sec { float b0, b1, b2, a1, a2; };
        struct Set { int rate, n; float trimDb; Sec s[kMaxTone]; };
        static const Set kSets[] = {
        { 44100, 3, 1.41796f, { { 0.85479385, -0.144033193, -0.637217381, -0.144033193, -0.782423531 }, { 0.592335626, 1.18467125, 0.592335626, 1.13959843, 0.229744075 }, { 0.98152882, 1.96305764, 0.98152882, 1.9611189, 0.964996375 } } },
        { 48000, 6, 1.19631f, { { 0.854900542, -0.072287756, -0.666003532, -0.072287756, -0.81110299 }, { 0.433285614, 0.866571228, 0.433285614, 0.565353314, 0.167789141 }, { 0.95390922, 1.90781844, 0.95390922, 1.88507357, 0.930563311 }, { 0.318721873, 0.635884712, 0.318721873, 0.820620979, 0.208564565 }, { 1, 1.9650718, 1, 1.2595476, 0.572580156 }, { 1, 1.93756937, 1, 1.59165801, 0.871602171 } } },
        { 88200, 6, 1.10265f, { { 0.862433133, -0.31324544, -0.471567004, -0.31324544, -0.609133871 }, { 0.256857526, 0.513715052, 0.256857526, -0.615696667, 0.643126772 }, { 0.0801615046, 0.160323009, 0.0801615046, -1.05279076, 0.373436776 }, { 0.0719550639, 0.112329272, 0.0719550639, -0.545724856, 0.172949889 }, { 1, 0.342272598, 1, -0.404564116, 0.602286085 }, { 1, -0.0503600571, 1, -0.330113003, 0.900104957 } } },
        { 96000, 7, 1.10868f, { { 0.864465172, -0.308955184, -0.492975448, -0.308955184, -0.628510276 }, { 0.217478464, 0.434956928, 0.217478464, -0.782144741, 0.652058597 }, { 0.0684249472, 0.136849894, 0.0684249472, -1.12388922, 0.397589006 }, { 0.0445253435, 0.0384806359, 0.0445253435, -0.27364085, 0 }, { 1, 1, 0, -0.537722075, 0.298166901 }, { 1, -0.0201395205, 1, -0.526236722, 0.663212512 }, { 1, -0.293044086, 1, -0.534753207, 0.907319764 } } },
        { 176400, 7, 1.00781f, { { 0.882469339, -0.638074382, -0.210143837, -0.638074382, -0.327674498 }, { 0.0669461737, 0.133892347, 0.0669461737, -1.52191967, 0.789704363 }, { 0.0196174128, 0.0392348257, 0.0196174128, -1.57504392, 0.653513574 }, { 0.0197541828, 0.0197541828, 0, -0.553102054, 0 }, { 1, -0.597023774, 1, -1.21952781, 0.499559409 }, { 1, -1.28879507, 1, -1.38567314, 0.781073502 }, { 1, -1.42678165, 1, -1.48720594, 0.943875174 } } },
        { 192000, 7, 0.998106f, { { 0.886376071, -0.683312742, -0.172316148, -0.683312742, -0.285940077 }, { 0.0565260782, 0.113052156, 0.0565260782, -1.57629139, 0.802395701 }, { 0.016650091, 0.033300182, 0.016650091, -1.61153094, 0.678131309 }, { 0.0178420627, 0.0178420627, 0, -0.586850908, 0 }, { 1, -0.76590684, 1, -1.29078913, 0.531029062 }, { 1, -1.39198925, 1, -1.45921827, 0.797544908 }, { 1, -1.51199001, 1, -1.55946885, 0.948580999 } } }
        };
        for (const Set& t : kSets) {
            if (std::fabs(fs - (float)t.rate) > 1.f) continue;
            bool stable = true;                                     // (all are; checked when designed)
            for (int i = 0; i < t.n; ++i) if (std::fabs(t.s[i].a2) >= 1.f || std::fabs(t.s[i].a1) >= 1.f + t.s[i].a2) stable = false;
            if (!stable) break;
            nTone = t.n;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < t.n; ++i) {
                Biquad& q = tone[c][i];
                q.b0 = t.s[i].b0; q.b1 = t.s[i].b1; q.b2 = t.s[i].b2; q.a1 = t.s[i].a1; q.a2 = t.s[i].a2; q.z1 = q.z2 = 0.f;
            }
            toneTrim = dbToLin(t.trimDb);
            return;
        }
        nTone = 3;
        for (int c = 0; c < 2; ++c) {
            tone[c][0].setPeak(fs, std::min(5947.f, fs * 0.4f), 0.05f, -1.545f);
            tone[c][1].setLP(fs, std::min(19346.f, fs * 0.45f), 0.3f);
            tone[c][2].setLP(fs, std::min(21609.f, fs * 0.47f), 1.762f);
        }
        auto mag = [&](const Biquad& b, double f) {
            const double w = 2.0 * kPi * f / fs, cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
            const double nr = b.b0 + b.b1 * cr + b.b2 * c2r, ni = b.b1 * ci + b.b2 * c2i;
            const double dr = 1.0 + b.a1 * cr + b.a2 * c2r, di = b.a1 * ci + b.a2 * c2i;
            return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
        };
        toneTrim = (float)(1.0 / (mag(tone[0][0], 1000.0) * mag(tone[0][1], 1000.0) * mag(tone[0][2], 1000.0)));
    }
    inline float gauss() { return (rng.uni() + rng.uni() + rng.uni() + rng.uni() - 2.f) * 1.7320508f; }
    // the static curve: gain in dB for a level L (dB RMS) at COMP x (0..1); a smooth fit, max ~1.5 dB off
    static inline float compGain(float L, float x) {
        static const float p[20] = { 3.08414f, 22.8879f, 0.205581f, -4.93931f, -7.25112f, -4.35334f, 35.387f, -20.3553f,
                                     -4.07971f, -25.7928f, 26.1354f, -15.4218f, 3.4941f, 5.691f, -7.11239f, -1.25763f,
                                     -3.03831f, 4.27887f, -1.86931f, 0.300685f };
        auto q = [&](int i) { return p[i] + x * (p[i + 1] + x * (p[i + 2] + x * p[i + 3])); };
        const float G = q(0), u = (L + 24.f) / 24.f;
        const float f = q(4) + q(8) * u + q(12) * u * u + q(16) * u * u * u;
        const float d = (G - f) / 0.7f;                        // smooth min(G, f)
        return G - 0.7f * (d > 30.f ? d : std::log1p(std::exp(d)));
    }
};

// ---------------------------------------------------------------------------------------------
// Shared helpers for the combo modules

static inline double syncHz(int idx, double bpm)
{
    idx = idx < 0 ? 0 : (idx > 10 ? 10 : idx);
    return std::max(20.0, bpm) / 60.0 / kSyncBeats[idx];
}

// Click-free switching between the effect types inside a combo module
struct TypeSwitch {
    int cur = 0, want = 0;
    float gain = 1.f, step = 0.001f;
    void setTime(float fs, float seconds) { step = 1.f / std::max(1.f, seconds * fs); }
    // returns true on the sample where the type actually changes
    inline bool tick() {
        if (want != cur) {
            gain -= step;
            if (gain <= 0.f) { gain = 0.f; cur = want; return true; }
        } else if (gain < 1.f) {
            gain += step; if (gain > 1.f) gain = 1.f;
        }
        return false;
    }
};

// ---------------------------------------------------------------------------------------------
// 06 Mod: Flanger / Chorus / Phaser
class ModFX {
public:
    Ramp ramp; Smooth rateSm, depthSm, fbSm, mixSm; TypeSwitch ts;
    float fs = 44100;
    std::vector<float> buf[2];
    int size = 0, w = 0;
    double ph = 0.0;

    void prepare(float sr) {
        fs = sr; ph = 0.0;                         // every restart begins the sweep from the same point
        size = (int)(fs * 0.05f) + 8;
        buf[0].assign(size, 0.f); buf[1].assign(size, 0.f);
        w = 0;
        ramp.setTime(fs, 0.006f);
        rateSm.setTime(fs, 0.01f); depthSm.setTime(fs, 0.004f); fbSm.setTime(fs, 0.004f); mixSm.setTime(fs, 0.004f);
        rateSm.y = 0.3f; depthSm.y = 0.6f; mixSm.y = 0.5f;
        ts.setTime(fs, 0.006f);
        clearStates();
    }
    void clearStates() {
        std::fill(buf[0].begin(), buf[0].end(), 0.f); std::fill(buf[1].begin(), buf[1].end(), 0.f);
        for (int c = 0; c < 2; ++c) { fbState[c] = 0.f; for (int k = 0; k < 6; ++k) ap[c][k] = 0.f; }
    }
    // v: flanger/chorus = RATE Hz, DEPTH %, FEEDBACK %, MIX %; phaser = SPEED index, RANGE %, FEEDBACK %, MIX %
    void block(const BlockCtx& ctx, int type, const float* v) {
        ts.want = type;
        switch (type) {
        case 0:
            tRate = v[0]; tDepth = v[1] * 0.01f; tMix = v[3] * 0.01f;
            tFb = v[2] * 0.01f * 0.95f; if (ctx.limit) tFb = std::min(tFb, 0.7f);
            break;
        case 1:
            tRate = v[0]; tDepth = v[1] * 0.01f; tMix = v[3] * 0.01f;
            tFb = v[2] * 0.01f * 0.9f; if (ctx.limit) tFb = std::min(tFb, 0.6f);
            break;
        default:
            tRate = (float)syncHz((int)std::lround(v[0]), ctx.bpm);
            tDepth = v[1] * 0.01f; tMix = v[3] * 0.01f;
            tFb = v[2] * 0.01f * 0.9f; if (ctx.limit) tFb = std::min(tFb, 0.6f);
            break;
        }
    }
    inline void process(float& l, float& r, bool on) {
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        if (ts.tick()) clearStates();
        const float rate = rateSm.tick(tRate), depth = depthSm.tick(tDepth), fb = fbSm.tick(tFb), mix = mixSm.tick(tMix);
        ph += rate / fs; if (ph >= 1.0) ph -= 1.0;
        float* io[2] = { &l, &r };
        for (int c = 0; c < 2; ++c) {
            const float x = *io[c];
            float wet = 0.f;
            if (ts.cur == 2) {
                // six-stage phaser, sweep 160 Hz up to ~9 kHz scaled by RANGE
                const float lfo = 0.5f + 0.5f * (float)std::sin(2.0 * 3.14159265358979 * (ph + c * 0.08));
                const float f = std::min(160.f * std::pow(2.f, depth * 5.8f * lfo), fs * 0.45f);
                const float t = std::tan(kPi * f / fs);
                const float co = (t - 1.f) / (t + 1.f);
                float u = x + fb * fbState[c];
                for (int k = 0; k < 6; ++k) { const float y = co * u + ap[c][k]; ap[c][k] = u - co * y; u = y; }
                fbState[c] = softCeil(u);
                wet = u;
            } else {
                float* b = buf[c].data();
                auto tap = [&](float dms) {
                    float rp = (float)w - dms * 0.001f * fs;
                    if (rp < 0.f) rp += (float)size;
                    const int i0 = (int)rp; const float t = rp - (float)i0;
                    return hermite(b[(i0 - 1 + size) % size], b[i0], b[(i0 + 1) % size], b[(i0 + 2) % size], t);
                };
                float y;
                if (ts.cur == 0) {   // flanger: one tap sweeping 0.25 ms .. ~7 ms
                    const float lfo = 0.5f + 0.5f * (float)std::sin(2.0 * 3.14159265358979 * (ph + c * 0.25));
                    y = tap(0.25f + depth * 7.f * lfo);
                    b[w] = x + softCeil(fb * y);
                } else {             // chorus: two voices per side, opposite phase, up to +-3 ms around 12 / 17 ms
                    const double p0 = ph + c * 0.25;
                    const float l1 = (float)std::sin(2.0 * 3.14159265358979 * p0);
                    const float l2 = (float)std::sin(2.0 * 3.14159265358979 * (p0 + 0.5));
                    const float d = 0.4f + depth * 2.6f;
                    y = 0.5f * (tap(12.f + d * l1) + tap(17.f + d * l2));
                    b[w] = x + softCeil(fb * 0.7f * y);
                }
                wet = y;
            }
            // 100% is the classic equal blend: these effects need the dry sound to make their notches
            const float m2 = 0.5f * mix;
            float y = x * (1.f - m2) + wet * m2;
            y = x + (y - x) * ts.gain;
            *io[c] = x + (y - x) * a;
        }
        if (++w >= size) w = 0;
    }
private:
    float tRate = 0.3f, tDepth = 0.6f, tFb = 0.5f, tMix = 0.5f;
    float ap[2][6] = {}, fbState[2] = {};
};

// ---------------------------------------------------------------------------------------------
// 08 Color: Ring Mod / LoFi / Comb Filter
class ColorFX {
public:
    Ramp ramp; Smooth freqSm, depthSm, mixSm, bitsSm, decSm, rangeSm, fbSm; TypeSwitch ts;
    float fs = 44100;
    std::vector<float> cb[2];
    int size = 0, w = 0;
    double ringPh = 0.0, combPh = 0.0;

    void prepare(float sr) {
        fs = sr; ringPh = combPh = 0.0;            // every restart begins the sweep from the same point
        size = (int)(fs * 0.03f) + 8;
        cb[0].assign(size, 0.f); cb[1].assign(size, 0.f);
        w = 0;
        ramp.setTime(fs, 0.006f);
        freqSm.setTime(fs, 0.004f); depthSm.setTime(fs, 0.004f); mixSm.setTime(fs, 0.004f);
        bitsSm.setTime(fs, 0.004f); decSm.setTime(fs, 0.004f); rangeSm.setTime(fs, 0.004f); fbSm.setTime(fs, 0.004f);
        freqSm.y = 80.f; depthSm.y = 1.f; mixSm.y = 1.f; bitsSm.y = 13.f;
        ts.setTime(fs, 0.006f);
        holdAcc = 0.f; held[0] = held[1] = 0.f;
    }
    // v: ring = FREQ Hz, DEPTH %, MIX %; lofi = BITS knob, DECIMATE %, MIX %; comb = SPEED index, RANGE %, FEEDBACK %, MIX %
    void block(const BlockCtx& ctx, int type, const float* v) {
        ts.want = type;
        if (type == 0) { tFreq = v[0]; tDepth = v[1] * 0.01f; tMix = v[2] * 0.01f; }
        else if (type == 1) { tBits = lofiBits(v[0]); tDec = v[1] * 0.01f; tMix = v[2] * 0.01f; }
        else {
            combHz = (float)syncHz((int)std::lround(v[0]), ctx.bpm);
            tRange = v[1] * 0.01f;
            tFb = v[2] * 0.01f * 0.95f; if (ctx.limit) tFb = std::min(tFb, 0.75f);
            tMix = v[3] * 0.01f;
        }
    }
    void clear() { std::fill(cb[0].begin(), cb[0].end(), 0.f); std::fill(cb[1].begin(), cb[1].end(), 0.f); held[0] = held[1] = 0.f; }
    inline void process(float& l, float& r, bool on) {
        if (on && ramp.v <= 0.f) {
            // starting from silence: the knobs jump straight to where they are (nothing to click yet), so every
            // start sounds the same however the knobs moved while it was off
            freqSm.y = tFreq; depthSm.y = tDepth; mixSm.y = tMix; bitsSm.y = tBits; decSm.y = tDec;
            rangeSm.y = tRange; fbSm.y = tFb; holdAcc = 0.f;
        }
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        if (ts.tick()) { std::fill(cb[0].begin(), cb[0].end(), 0.f); std::fill(cb[1].begin(), cb[1].end(), 0.f); }
        const float mix = mixSm.tick(tMix);
        float wl = l, wr = r;
        if (ts.cur == 0) {
            // ring modulator: at low FREQ it pulses and ducks, higher gets metallic
            const float f = freqSm.tick(tFreq), d = depthSm.tick(tDepth);
            ringPh += f / fs; if (ringPh >= 1.0) ringPh -= 1.0;
            const float m = 1.f - d + d * (float)std::sin(2.0 * 3.14159265358979 * ringPh);
            wl = l * m; wr = r * m;
        } else if (ts.cur == 1) {
            // LoFi: sample-rate decimation (sample and hold) plus bit crushing
            const float bits = bitsSm.tick(tBits), dec = decSm.tick(tDec);
            const float holdLen = 1.f + dec * dec * 39.f;
            holdAcc += 1.f;
            if (holdAcc >= holdLen) { holdAcc -= holdLen; held[0] = l; held[1] = r; }
            const float levels = std::pow(2.f, bits - 1.f);
            wl = std::round(held[0] * levels) / levels;
            wr = std::round(held[1] * levels) / levels;
        } else {
            // comb filter: feedback comb whose delay sweeps with a tempo-synced LFO
            const float range = rangeSm.tick(tRange), fb = fbSm.tick(tFb);
            combPh += combHz / fs; if (combPh >= 1.0) combPh -= 1.0;
            float* io[2] = { &wl, &wr };
            for (int c = 0; c < 2; ++c) {
                const double p = combPh + c * 0.05;
                const float tri = (float)(1.0 - std::fabs(2.0 * (p - std::floor(p)) - 1.0));
                const float dms = 0.6f + range * 9.f * tri;
                float rp = (float)w - dms * 0.001f * fs;
                if (rp < 0.f) rp += (float)size;
                const int i0 = (int)rp; const float t = rp - (float)i0;
                float* b = cb[c].data();
                const float d = hermite(b[(i0 - 1 + size) % size], b[i0], b[(i0 + 1) % size], b[(i0 + 2) % size], t);
                const float y = *io[c] + fb * d;
                b[w] = softCeil(y);
                *io[c] = softCeil(y * (1.f - 0.5f * fb));
            }
            if (++w >= size) w = 0;
        }
        const float g = ts.gain;
        const float yl = l + ((l * (1.f - mix) + wl * mix) - l) * g;
        const float yr = r + ((r * (1.f - mix) + wr * mix) - r) * g;
        l += (yl - l) * a; r += (yr - r) * a;
    }
private:
    float tFreq = 80.f, tDepth = 1.f, tBits = 13.f, tDec = 0.3f, tRange = 0.6f, tFb = 0.57f, tMix = 1.f, combHz = 0.5f;
    float holdAcc = 0.f, held[2] = {};
};

// =============================================================================================
// Yancey additions
// =============================================================================================

// Musical clock for one sample: host position when playing, free-running otherwise
struct Clock {
    double beat = 0.0;      // position in beats
    double spb = 22050.0;   // samples per beat
    bool grid = false;      // GRID timing: loop starts and cycles follow the beat
    bool tick16 = true;     // this sample starts a new 1/16 (always true in RAW)
};

// Recent-past stereo buffer with fractional reads behind the write head
struct History {
    std::vector<float> b[2];
    int size = 0, w = 0;
    void prepare(int n) { size = std::max(64, n); b[0].assign(size, 0.f); b[1].assign(size, 0.f); w = 0; }
    void clear() { std::fill(b[0].begin(), b[0].end(), 0.f); std::fill(b[1].begin(), b[1].end(), 0.f); }
    inline void push(float l, float r) { b[0][w] = l; b[1][w] = r; if (++w >= size) w = 0; }
    double maxBack() const { return (double)size - 8.0; }
    // 'back' samples behind the newest written sample (kept >= 2 so we never read unwritten data)
    inline float read(int c, double back) const {
        if (back < 2.0) back = 2.0;
        if (back > maxBack()) back = maxBack();
        double p = (double)w - 1.0 - back;
        while (p < 0.0) p += (double)size;
        const int i0 = (int)p;
        const float t = (float)(p - (double)i0);
        const float* B = b[c].data();
        const int im1 = i0 > 0 ? i0 - 1 : size - 1;
        const int i1 = i0 + 1 < size ? i0 + 1 : 0;
        const int i2 = i1 + 1 < size ? i1 + 1 : 0;
        return hermite(B[im1], B[i0], B[i1], B[i2], t);
    }
};

// Topology-preserving state variable filter (one channel)
struct Svf {
    float ic1 = 0.f, ic2 = 0.f;
    void reset() { ic1 = ic2 = 0.f; }
    // mode 0 low pass, 1 high pass, 2 band pass
    inline float tick(float x, float g, float k, int mode) {
        const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1; ic2 = 2.f * v2 - ic2;
        if (mode == 0) return v2;
        if (mode == 1) return x - k * v1 - v2;
        return v1 * k;
    }
};
static inline float svfG(float fs, float hz) { return std::tan(kPi * std::min(std::max(hz, 10.f), fs * 0.45f) / fs); }

// Schroeder all-pass diffuser
struct Allpass {
    std::vector<float> buf; int n = 1, i = 0; float g = 0.6f;
    void prepare(int len, float gain) { n = std::max(1, len); buf.assign(n, 0.f); i = 0; g = gain; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); }
    inline float tick(float x) {
        const float d = buf[i];
        const float y = -g * x + d;
        buf[i] = x + g * y;
        if (++i >= n) i = 0;
        return y;
    }
};

// echoes and reverbs: the beat always stays at full level; MIX sets how loud the echoes are
static inline float mixDry(float) { return 1.f; }
static inline float mixWet(float mix) { return mix; }

// ---------------------------------------------------------------------------------------------
// Filter + Drive: resonant filter (low, high or band pass) with a drive stage in front
// tanh with first-order antiderivative anti-aliasing: removes most of the false tones a plain
// waveshaper folds back below Nyquist, at the cost of half a sample of delay
struct AdaaTanh {
    float xm1 = 0.f, fm1 = 0.f;
    static inline float F(float x) { const float a = std::fabs(x); return a + std::log1p(std::exp(-2.f * a)) - 0.69314718f; }
    void reset() { xm1 = fm1 = 0.f; }
    inline float tick(float x) {
        const float fx = F(x), d = x - xm1;
        const float y = std::fabs(d) < 1e-4f ? std::tanh(0.5f * (x + xm1)) : (fx - fm1) / d;
        xm1 = x; fm1 = fx;
        return y;
    }
};

class FilterDrive {
public:
    Ramp ramp; Smooth cutSm, resSm, drvSm, mixSm;
    float fs = 44100.f;
    void prepare(float sr) {
        fs = sr;
        ramp.setTime(fs, 0.006f);
        cutSm.setTime(fs, 0.004f); resSm.setTime(fs, 0.004f); drvSm.setTime(fs, 0.004f); mixSm.setTime(fs, 0.004f);
        cutSm.y = std::log(1200.f); mixSm.y = 1.f;
        clear();
    }
    void clear() { for (int c = 0; c < 2; ++c) { f[c].reset(); sat[c].reset(); } }
    void block(const BlockCtx& ctx, float cutHz, float resoPct, float drivePct, int type, float mixPct) {
        tCut = std::log(std::min(std::max(cutHz, 20.f), fs * 0.45f));
        tRes = resoPct * 0.01f; if (ctx.limit) tRes = std::min(tRes, 0.85f);
        tDrv = drivePct * 0.01f; mode = type < 0 ? 0 : (type > 2 ? 2 : type); tMix = mixPct * 0.01f;
    }
    inline void process(float& l, float& r, bool on) {
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        const float fc = std::exp(cutSm.tick(tCut)), res = resSm.tick(tRes), drv = drvSm.tick(tDrv), mix = mixSm.tick(tMix);
        const float g = svfG(fs, fc), k = 2.f - 1.94f * res;
        const float pre = 1.f + drv * 7.f, post = 1.f / (1.f + drv * 2.2f);
        float* io[2] = { &l, &r };
        for (int c = 0; c < 2; ++c) {
            const float x = *io[c];
            const float sd = sat[c].tick(x * pre);       // keep the anti-aliased shaper running so turning drive up is smooth
            const float xd = drv > 0.002f ? sd * post * (1.f + drv) : x;
            float y = f[c].tick(xd, g, k, mode);
            if (!std::isfinite(y)) { f[c].reset(); y = 0.f; }
            y = softCeil(y);
            const float o = x * (1.f - mix) + y * mix;
            *io[c] = x + (o - x) * a;
        }
    }
private:
    float tCut = 7.f, tRes = 0.35f, tDrv = 0.25f, tMix = 1.f;
    int mode = 0;
    Svf f[2];
    AdaaTanh sat[2];
};

// ---------------------------------------------------------------------------------------------
// Tape Echo: synced echo with tape wow, saturation in the loop and a darkening tone control
class TapeEcho {
public:
    void clear() { reset(); for (int c = 0; c < 2; ++c) { hp[c].y = 0.f; tone[c].y = 0.f; } }
    void idleCheck() { idle = (ramp.v <= 0.f && tail < 1e-6f); }
    Ramp ramp; Smooth timeSm, fbSm, mixSm, wowSm;
    float fs = 44100.f;
    std::vector<float> buf[2];
    int size = 0, w = 0;
    Rng rng;

    void prepare(float sr) {
        fs = sr;
        size = (int)(fs * 4.3f) + 8;
        buf[0].assign(size, 0.f); buf[1].assign(size, 0.f);
        w = 0;
        ramp.setTime(fs, 0.006f);
        timeSm.setTime(fs, 0.06f); fbSm.setTime(fs, 0.004f); mixSm.setTime(fs, 0.004f); wowSm.setTime(fs, 0.004f);
        timeSm.y = fs * 0.375f;
        drift.setLP(fs, 0.7f);
        for (int c = 0; c < 2; ++c) { hp[c].setLP(fs, 70.f); hp[c].y = 0.f; tone[c].y = 0.f; }
        cTone = -1.f; tail = 0.f; idle = true;
    }
    void reset() { std::fill(buf[0].begin(), buf[0].end(), 0.f); std::fill(buf[1].begin(), buf[1].end(), 0.f); tail = 0.f; }
    void block(const BlockCtx& ctx, int timeIdx, float fbPct, float mixPct, float wowPct, float tonePct) {
        timeIdx = timeIdx < 0 ? 0 : (timeIdx > 12 ? 12 : timeIdx);
        double sec = kDelayTimeBeats[timeIdx] * 60.0 / std::max(20.0, ctx.bpm);
        while (sec > 2.0) sec *= 0.5;
        tTime = (float)(sec * fs);
        float fb = fbPct * 0.01f * 1.04f;
        if (ctx.limit) fb = std::min(fb, 0.85f);
        tFb = fb; tMix = mixPct * 0.01f; tWow = wowPct * 0.01f;
        const float th = 1400.f * std::pow(13000.f / 1400.f, tonePct * 0.01f);
        if (std::fabs(th - cTone) > 1.f) { cTone = th; tone[0].setLP(fs, th); tone[1].setLP(fs, th); }
        idle = (ramp.v <= 0.f && tail < 1e-6f);
    }
    inline void process(float& l, float& r, bool on) {
        if (!on && idle) return;
        const float a = ramp.tick(on);
        const float d = timeSm.tick(tTime), fb = fbSm.tick(tFb), mix = mixSm.tick(tMix), wd = wowSm.tick(tWow);
        wowPh += 0.63f / fs; if (wowPh >= 1.f) wowPh -= 1.f;
        const float mod = wd * wd * 0.0035f * fs * (0.75f * std::sin(2.f * kPi * wowPh) + 0.25f * std::max(-1.f, std::min(1.f, drift.lp(rng.bi()) * 8.f)));
        float rp = (float)w - (d + mod);
        while (rp < 0.f) rp += (float)size;
        const int i0 = (int)rp; const float t = rp - (float)i0;
        const float dryG = mixDry(mix), wetG = mixWet(mix);
        float* io[2] = { &l, &r };
        float peak = 0.f;
        for (int c = 0; c < 2; ++c) {
            float* b = buf[c].data();
            const int im1 = (i0 - 1 + size) % size, i1 = (i0 + 1) % size, i2 = (i0 + 2) % size;
            const float echo = hermite(b[im1], b[i0], b[i1], b[i2], t);
            const float filt = tone[c].lp(hp[c].hp(echo));
            const float x = *io[c];
            b[w] = x * a + std::tanh(filt * fb * 1.15f) / 1.15f;
            peak = std::max(peak, std::fabs(filt));
            *io[c] = x * (1.f + (dryG - 1.f) * a) + filt * wetG;
        }
        tail = std::max(tail * 0.99995f, peak);
        if (++w >= size) w = 0;
    }
private:
    float tTime = 0, tFb = 0.45f, tMix = 0.35f, tWow = 0.3f, cTone = -1.f, tail = 0.f, wowPh = 0.f;
    bool idle = true;
    OnePole hp[2], tone[2], drift;
};

// ---------------------------------------------------------------------------------------------
// Echo Verb: a synced echo whose repeats get smeared into reverb (each pass is diffused more)
class EchoVerb {
public:
    void clear() { std::fill(buf[0].begin(), buf[0].end(), 0.f); std::fill(buf[1].begin(), buf[1].end(), 0.f); for (int c = 0; c < 2; ++c) { for (int k = 0; k < 4; ++k) ap[c][k].clear(); hp[c].y = 0.f; damp[c].y = 0.f; } tail = 0.f; }
    void idleCheck() { idle = (ramp.v <= 0.f && tail < 1e-6f); }
    Ramp ramp; Smooth timeSm, fbSm, mixSm;
    float fs = 44100.f;
    std::vector<float> buf[2];
    int size = 0, w = 0;

    void prepare(float sr) {
        fs = sr;
        size = (int)(fs * 4.3f) + 8;
        buf[0].assign(size, 0.f); buf[1].assign(size, 0.f);
        w = 0;
        ramp.setTime(fs, 0.006f);
        timeSm.setTime(fs, 0.04f); fbSm.setTime(fs, 0.004f); mixSm.setTime(fs, 0.004f);
        timeSm.y = fs * 0.5f;
        static const float ms[2][4] = { { 4.1f, 6.7f, 9.3f, 12.9f }, { 4.7f, 7.3f, 10.1f, 13.7f } };
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) ap[c][k].prepare((int)(ms[c][k] * 0.001f * fs), 0.62f);
        for (int c = 0; c < 2; ++c) { hp[c].setLP(fs, 90.f); hp[c].y = damp[c].y = 0.f; }
        cDamp = -1.f; tail = 0.f; idle = true;
    }
    void block(const BlockCtx& ctx, int syncIdx, float fbPct, float mixPct, float dampPct, int spread) {
        double sec = (double)kSyncBeats[syncIdx < 0 ? 0 : (syncIdx > 10 ? 10 : syncIdx)] * 60.0 / std::max(20.0, ctx.bpm);
        while (sec > 2.0) sec *= 0.5;
        tTime = (float)(sec * fs);
        float fb = fbPct * 0.01f; if (ctx.limit) fb = std::min(fb, 0.8f);
        tFb = fb; tMix = mixPct * 0.01f; pan = spread > 0;
        const float dh = 12000.f * std::pow(900.f / 12000.f, dampPct * 0.01f);
        if (std::fabs(dh - cDamp) > 1.f) { cDamp = dh; damp[0].setLP(fs, dh); damp[1].setLP(fs, dh); }
        idle = (ramp.v <= 0.f && tail < 1e-6f);
    }
    inline void process(float& l, float& r, bool on) {
        if (!on && idle) return;
        const float a = ramp.tick(on);
        const float d = std::max(16.f, timeSm.tick(tTime)), fb = fbSm.tick(tFb), mix = mixSm.tick(tMix);
        float rp = (float)w - d;
        while (rp < 0.f) rp += (float)size;
        const int i0 = (int)rp; const float t = rp - (float)i0;
        float e[2];
        for (int c = 0; c < 2; ++c) {
            const float* b = buf[c].data();
            const float raw = hermite(b[(i0 - 1 + size) % size], b[i0], b[(i0 + 1) % size], b[(i0 + 2) % size], t);
            float y = raw;
            for (int k = 0; k < 4; ++k) y = ap[c][k].tick(y);
            e[c] = damp[c].lp(hp[c].hp(y));
        }
        const float xl = l, xr = r;
        const float inL = pan ? (xl + xr) * 0.5f : xl, inR = pan ? 0.f : xr;
        const float fl = pan ? e[1] : e[0], fr = pan ? e[0] : e[1];   // PAN: repeats bounce left/right
        buf[0][w] = inL * a + softCeil(fl * fb);
        buf[1][w] = inR * a + softCeil(fr * fb);
        if (++w >= size) w = 0;
        tail = std::max(tail * 0.99995f, std::max(std::fabs(e[0]), std::fabs(e[1])));
        const float dryG = mixDry(mix), wetG = mixWet(mix);
        l = xl * (1.f + (dryG - 1.f) * a) + e[0] * wetG;
        r = xr * (1.f + (dryG - 1.f) * a) + e[1] * wetG;
    }
private:
    float tTime = 0, tFb = 0.45f, tMix = 0.35f, cDamp = -1.f, tail = 0.f;
    bool idle = true, pan = true;
    Allpass ap[2][4];
    OnePole hp[2], damp[2];
};

// ---------------------------------------------------------------------------------------------
// Eight-line feedback delay network reverb with modulated lines
class Fdn8 {
public:
    float fs = 44100.f;
    void prepare(float sr, float sizeScale) {
        fs = sr;
        static const float baseMs[8] = { 29.7f, 37.1f, 41.3f, 43.7f, 53.9f, 59.3f, 67.1f, 73.7f };
        for (int i = 0; i < 8; ++i) {
            len[i] = baseMs[i] * 0.001f * fs * sizeScale;
            n[i] = (int)(len[i] + 0.006f * fs) + 8;
            line[i].assign(n[i], 0.f);
            wi[i] = 0;
            lp[i].y = 0.f;
            ph[i] = i * 0.137f;
            rate[i] = (0.31f + 0.11f * i) / fs;
        }
        setDecay(2.f); setTone(8000.f);
    }
    void clear() { for (int i = 0; i < 8; ++i) { std::fill(line[i].begin(), line[i].end(), 0.f); lp[i].y = 0.f; } }
    void setDecay(float rt60) {
        rt60 = std::max(0.1f, rt60);
        for (int i = 0; i < 8; ++i) gain[i] = std::pow(10.f, -3.f * len[i] / (fs * rt60));
    }
    void setTone(float hz) { for (int i = 0; i < 8; ++i) lp[i].setLP(fs, hz); }
    // modDepth in samples
    inline void tick(float inL, float inR, float modDepth, float& outL, float& outR) {
        float v[8];
        for (int i = 0; i < 8; ++i) {
            ph[i] += rate[i]; if (ph[i] >= 1.f) ph[i] -= 1.f;
            const float d = len[i] - modDepth * (1.f + std::sin(2.f * kPi * ph[i]));
            float rp = (float)wi[i] - std::max(4.f, d);
            while (rp < 0.f) rp += (float)n[i];
            const int i0 = (int)rp; const float t = rp - (float)i0;
            const int i1 = i0 + 1 < n[i] ? i0 + 1 : 0;
            const float y = line[i][i0] + (line[i][i1] - line[i][i0]) * t;
            v[i] = lp[i].lp(y) * gain[i];
        }
        outL = (v[0] - v[2] + v[4] - v[6]) * 0.5f;
        outR = (v[1] - v[3] + v[5] - v[7]) * 0.5f;
        // 8-point Hadamard mix
        for (int s = 1; s < 8; s <<= 1)
            for (int i = 0; i < 8; i += s << 1)
                for (int j = i; j < i + s; ++j) { const float a = v[j], b = v[j + s]; v[j] = a + b; v[j + s] = a - b; }
        const float sc = 0.35355339f;
        for (int i = 0; i < 8; ++i) {
            const float in = (i & 1) ? inR : inL;
            line[i][wi[i]] = softCeil(v[i] * sc + in);
            if (++wi[i] >= n[i]) wi[i] = 0;
        }
    }
private:
    std::vector<float> line[8];
    int n[8] = {}, wi[8] = {};
    float len[8] = {}, gain[8] = {}, ph[8] = {}, rate[8] = {};
    OnePole lp[8];
};

// ---------------------------------------------------------------------------------------------
// Wave Verb: lush reverb whose lines drift in pitch for an undulating, wavelike tail
class WaveVerb {
public:
    void clear() { pre.clear(); fdn.clear(); for (int c = 0; c < 2; ++c) { ap[c][0].clear(); ap[c][1].clear(); hp[c].y = 0.f; } tail = 0.f; }
    void idleCheck() { idle = (ramp.v <= 0.f && tail < 1e-6f); }
    Ramp ramp; Smooth mixSm, modSm, preSm;
    float fs = 44100.f;
    void prepare(float sr) {
        fs = sr;
        ramp.setTime(fs, 0.008f);
        mixSm.setTime(fs, 0.004f); modSm.setTime(fs, 0.004f); preSm.setTime(fs, 0.03f);
        pre.prepare((int)(fs * 0.27f));
        fdn.prepare(fs, 1.25f);
        static const float ms[2][2] = { { 5.3f, 8.9f }, { 6.1f, 9.7f } };
        for (int c = 0; c < 2; ++c) { for (int k = 0; k < 2; ++k) ap[c][k].prepare((int)(ms[c][k] * 0.001f * fs), 0.6f); hp[c].setLP(fs, 110.f); hp[c].y = 0.f; }
        cDecay = cTone = -1.f; tail = 0.f; idle = true;
    }
    void block(const BlockCtx&, float decayS, float modPct, float mixPct, float preMs, float tonePct) {
        if (std::fabs(decayS - cDecay) > 0.01f) { cDecay = decayS; fdn.setDecay(decayS); }
        const float th = 1800.f * std::pow(15000.f / 1800.f, tonePct * 0.01f);
        if (std::fabs(th - cTone) > 1.f) { cTone = th; fdn.setTone(th); }
        tMod = modPct * 0.01f; tMix = mixPct * 0.01f; tPre = preMs * 0.001f * fs;
        idle = (ramp.v <= 0.f && tail < 1e-6f);
    }
    inline void process(float& l, float& r, bool on) {
        if (!on && idle) return;
        const float a = ramp.tick(on);
        const float mix = mixSm.tick(tMix), m = modSm.tick(tMod), pd = preSm.tick(tPre);
        pre.push(hp[0].hp(l) * a, hp[1].hp(r) * a);
        float il = pre.read(0, pd), ir = pre.read(1, pd);
        for (int k = 0; k < 2; ++k) { il = ap[0][k].tick(il); ir = ap[1][k].tick(ir); }
        float wl, wr;
        fdn.tick(il * 0.6f, ir * 0.6f, m * m * 0.004f * fs, wl, wr);
        tail = std::max(tail * 0.99997f, std::max(std::fabs(wl), std::fabs(wr)));
        const float dryG = mixDry(mix), wetG = mixWet(mix);
        l = l * (1.f + (dryG - 1.f) * a) + wl * wetG;
        r = r * (1.f + (dryG - 1.f) * a) + wr * wetG;
    }
private:
    History pre;
    Fdn8 fdn;
    Allpass ap[2][2];
    OnePole hp[2];
    float tMod = 0.45f, tMix = 0.35f, tPre = 0.f, cDecay = -1.f, cTone = -1.f, tail = 0.f;
    bool idle = true;
};

// ---------------------------------------------------------------------------------------------
// Cloud Delay: pitch-shifted grains fed back on themselves and blurred into a cloud
class CloudDelay {
public:
    void clear() { hist.clear(); fdn.clear(); tail = 0.f; fbL = fbR = 0.f; }
    void idleCheck() { idle = (ramp.v <= 0.f && tail < 1e-6f); }
    Ramp ramp; Smooth mixSm, winSm, fbSm, cloudSm, ratioSm;
    float fs = 44100.f;
    void prepare(float sr) {
        fs = sr;
        ramp.setTime(fs, 0.008f);
        mixSm.setTime(fs, 0.004f); winSm.setTime(fs, 0.03f); fbSm.setTime(fs, 0.004f); cloudSm.setTime(fs, 0.004f); ratioSm.setTime(fs, 0.004f);
        winSm.y = 0.2f * fs; ratioSm.y = 1.f;
        hist.prepare((int)(fs * 1.2f));
        fdn.prepare(fs, 0.7f); fdn.setDecay(2.4f); fdn.setTone(6500.f);
        lag[0] = 0.f; lag[1] = 0.5f;
        tail = 0.f; idle = true;
        fbL = fbR = 0.f;
    }
    void block(const BlockCtx& ctx, float windowPct, float semis, float mixPct, float fbPct, float cloudyPct) {
        tWin = (0.03f + 0.37f * windowPct * 0.01f) * fs;
        tRatio = std::pow(2.f, semis / 12.f);
        tMix = mixPct * 0.01f;
        float fb = fbPct * 0.01f; if (ctx.limit) fb = std::min(fb, 0.75f);
        tFb = fb; tCloud = cloudyPct * 0.01f;
        idle = (ramp.v <= 0.f && tail < 1e-6f);
    }
    inline void process(float& l, float& r, bool on) {
        if (!on && idle) return;
        const float a = ramp.tick(on);
        const float mix = mixSm.tick(tMix), W = std::max(64.f, winSm.tick(tWin)), fb = fbSm.tick(tFb), cl = cloudSm.tick(tCloud);
        const float ratio = ratioSm.tick(tRatio);
        hist.push(l * a + softCeil(fbL * fb), r * a + softCeil(fbR * fb));
        // two grains half a window apart; each grain's read point drifts at the pitch ratio
        float gl = 0.f, gr = 0.f;
        const float base = 0.01f * fs;
        for (int k = 0; k < 2; ++k) {
            lag[k] += (1.f - ratio) / W;
            if (lag[k] >= 1.f) lag[k] -= 1.f;
            if (lag[k] < 0.f) lag[k] += 1.f;
            const float win = std::sin(kPi * lag[k]);
            const double back = base + lag[k] * W;
            gl += hist.read(0, back) * win * win;
            gr += hist.read(1, back) * win * win;
        }
        float cl1, cr1;
        fdn.tick(gl * 0.5f, gr * 0.5f, 0.0015f * fs, cl1, cr1);
        const float wl = gl * (1.f - 0.6f * cl) + cl1 * cl * 1.4f;
        const float wr = gr * (1.f - 0.6f * cl) + cr1 * cl * 1.4f;
        fbL = wl; fbR = wr;
        tail = std::max(tail * 0.99997f, std::max(std::fabs(wl), std::fabs(wr)));
        const float dryG = mixDry(mix), wetG = mixWet(mix);
        l = l * (1.f + (dryG - 1.f) * a) + wl * wetG;
        r = r * (1.f + (dryG - 1.f) * a) + wr * wetG;
    }
private:
    History hist;
    Fdn8 fdn;
    float lag[2] = {};
    float tWin = 0.2f, tRatio = 1.f, tMix = 0.4f, tFb = 0.45f, tCloud = 0.5f, tail = 0.f, fbL = 0.f, fbR = 0.f;
    bool idle = true;
};

// ---------------------------------------------------------------------------------------------
// Stopper: the record slows to a stop over RATE (turntable power-off), with a closing filter
class Stopper {
public:
    Ramp ramp; float fs = 44100.f;
    void prepare(float sr) { fs = sr; ramp.setTime(fs, 0.004f); hist.prepare((int)(fs * 10.f)); wasOn = false; }
    void block(const BlockCtx& ctx, float depthPct, int syncIdx, float resoPct) {
        const double spb = 60.0 / std::max(20.0, ctx.bpm) * fs;
        T = (float)std::max(64.0, kSyncBeats[syncIdx < 0 ? 0 : (syncIdx > 10 ? 10 : syncIdx)] * spb);
        depth = depthPct * 0.01f; reso = resoPct * 0.01f; if (ctx.limit) reso = std::min(reso, 0.8f);
    }
    inline void process(float& l, float& r, bool on) {
        hist.push(l, r);
        if (on && !wasOn) { lag = 2.0; elapsed = 0.f; f[0].reset(); f[1].reset(); }
        wasOn = on;
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        const float u = std::min(1.f, elapsed / T);
        if (on) elapsed += 1.f;
        const float speed = 1.f - depth * smoothstep01(u);
        lag += 1.0 - speed;
        if (lag > hist.maxBack()) lag = hist.maxBack();
        const float fc = 150.f + 18000.f * speed * speed;
        const float g = svfG(fs, fc), k = 2.f - 1.7f * reso;
        const float lvl = smoothstep01(speed / 0.12f);
        const float yl = f[0].tick(hist.read(0, lag), g, k, 0) * lvl;
        const float yr = f[1].tick(hist.read(1, lag), g, k, 0) * lvl;
        l += (softCeil(yl) - l) * a; r += (softCeil(yr) - r) * a;
    }
private:
    History hist; Svf f[2];
    double lag = 2.0; float elapsed = 0.f, T = 44100.f, depth = 1.f, reso = 0.2f;
    bool wasOn = false;
};

// ---------------------------------------------------------------------------------------------
// Back Spin: the record is yanked backwards and slows to a stop, over and over every LENGTH
class BackSpin {
public:
    Ramp ramp; Smooth mixSm; float fs = 44100.f;
    float phase = -1.f;
    void prepare(float sr) { fs = sr; ramp.setTime(fs, 0.004f); mixSm.setTime(fs, 0.004f); mixSm.y = 1.f; hist.prepare((int)(fs * 12.f)); wasOn = false; }
    void block(const BlockCtx& ctx, int lenIdx, float speedPct, float mixPct) {
        lenIdx = lenIdx < 0 ? 0 : (lenIdx > 5 ? 5 : lenIdx);
        lenBeats = kLoopLenBeats[lenIdx];
        const double spb = 60.0 / std::max(20.0, ctx.bpm) * fs;
        L = (float)std::max(256.0, std::min(lenBeats * spb, hist.maxBack() / 3.0));
        vmax = 1.5f + 3.5f * speedPct * 0.01f;
        tMix = mixPct * 0.01f;
    }
    inline void process(float& l, float& r, bool on, const Clock& ck) {
        hist.push(l, r);
        if (on && !wasOn) startCycle();
        wasOn = on;
        const float a = ramp.tick(on);
        if (a <= 0.f) { phase = -1.f; return; }
        if (ck.grid) {
            const long cyc = (long)std::floor(ck.beat / lenBeats);
            if (on && cyc != lastCyc) { if (lastCyc != LONG_MIN) startCycle(); lastCyc = cyc; }
        } else if (on && t >= L) startCycle();
        const float u = std::min(1.f, t / L);
        const float v = -vmax * (1.f - u) * (1.f - u);   // fast backwards, easing to a stop
        lag += 1.0 - v;
        if (lag > hist.maxBack()) lag = hist.maxBack();
        t += 1.f;
        const float F = std::max(16.f, 0.003f * fs);
        const float env = std::min(1.f, std::min(t / F, std::max(0.f, (L - t)) / F + (ck.grid ? 1.f : 0.f))) * smoothstep01((1.f - u) * 6.f);
        const float mix = mixSm.tick(tMix);
        const float yl = hist.read(0, lag) * env, yr = hist.read(1, lag) * env;
        phase = u;
        const float ol = l + (yl - l) * mix, orr = r + (yr - r) * mix;
        l += (ol - l) * a; r += (orr - r) * a;
    }
private:
    void startCycle() { lag = 2.0; t = 0.f; }
    History hist;
    double lag = 2.0, lenBeats = 1.0; long lastCyc = LONG_MIN;
    float t = 0.f, L = 44100.f, vmax = 3.f, tMix = 1.f;
    bool wasOn = false;
};

// ---------------------------------------------------------------------------------------------
// Coil: playback slows and catches back up every RATE, with a filter and level that cycle along
class Coil {
public:
    Ramp ramp; Smooth depthSm, resSm, fltSm, ampSm; float fs = 44100.f;
    void prepare(float sr) {
        fs = sr; ramp.setTime(fs, 0.006f);
        depthSm.setTime(fs, 0.004f); resSm.setTime(fs, 0.004f); fltSm.setTime(fs, 0.004f); ampSm.setTime(fs, 0.004f);
        hist.prepare((int)(fs * 4.f)); wasOn = false;
    }
    void block(const BlockCtx& ctx, float depthPct, int syncIdx, float resoPct, float fltPct, float ampPct) {
        periodBeats = kSyncBeats[syncIdx < 0 ? 0 : (syncIdx > 10 ? 10 : syncIdx)];
        const double spb = 60.0 / std::max(20.0, ctx.bpm) * fs;
        P = (float)std::max(256.0, std::min(periodBeats * spb, (double)fs * 3.0));
        tDepth = depthPct * 0.01f; tRes = resoPct * 0.01f; if (ctx.limit) tRes = std::min(tRes, 0.8f);
        tFlt = fltPct * 0.01f; tAmp = ampPct * 0.01f;
    }
    inline void process(float& l, float& r, bool on, const Clock& ck) {
        hist.push(l, r);
        if (on && !wasOn) { t = 0.f; f[0].reset(); f[1].reset(); }
        wasOn = on;
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        float u;
        if (ck.grid) { const double q = ck.beat / periodBeats; u = (float)(q - std::floor(q)); }
        else { u = t / P; t += 1.f; if (t >= P) t -= P; }
        const float c01 = 0.5f - 0.5f * std::cos(2.f * kPi * u);      // 0 -> 1 -> 0 over the cycle
        const float D = depthSm.tick(tDepth) * 0.25f * P;
        const double back = 2.0 + (double)(D * c01);
        const float fc = 20000.f * std::pow(0.03f, fltSm.tick(tFlt) * c01);
        const float g = svfG(fs, fc), k = 2.f - 1.6f * resSm.tick(tRes);
        const float amp = 1.f - ampSm.tick(tAmp) * 0.7f * c01;
        const float yl = softCeil(f[0].tick(hist.read(0, back), g, k, 0) * amp);
        const float yr = softCeil(f[1].tick(hist.read(1, back), g, k, 0) * amp);
        l += (yl - l) * a; r += (yr - r) * a;
    }
private:
    History hist; Svf f[2];
    double periodBeats = 1.0;
    float t = 0.f, P = 44100.f, tDepth = 0.5f, tRes = 0.4f, tFlt = 0.6f, tAmp = 0.3f;
    bool wasOn = false;
};

// ---------------------------------------------------------------------------------------------
// Scatter: chops the beat into steps and, at DEPTH odds, replaces steps with stutters,
// reversals, shuffled slices or half-speed slices of what just played
class Scatter {
public:
    Ramp ramp; Smooth mixSm; float fs = 44100.f;
    void prepare(float sr) { fs = sr; ramp.setTime(fs, 0.004f); mixSm.setTime(fs, 0.004f); mixSm.y = 1.f; hist.prepare((int)(fs * 10.f)); wasOn = false; }
    void block(const BlockCtx& ctx, int type, float depthPct, float mixPct, int sizeIdx) {
        mode = type < 0 ? 0 : (type > 3 ? 3 : type);
        depth = depthPct * 0.01f; tMix = mixPct * 0.01f;
        stepBeats = kDivBeats[sizeIdx < 0 ? 0 : (sizeIdx > 7 ? 7 : sizeIdx)];
        const double spb = 60.0 / std::max(20.0, ctx.bpm) * fs;
        S = (float)std::max(128.0, std::min(stepBeats * spb, hist.maxBack() / 9.0));
    }
    inline void process(float& l, float& r, bool on, const Clock& ck) {
        hist.push(l, r);
        if (on && !wasOn) { t = 0.f; held = 0; lastStep = LONG_MIN; newStep(); }
        wasOn = on;
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        if (ck.grid) {
            const long st = (long)std::floor(ck.beat / stepBeats);
            if (st != lastStep) { if (lastStep != LONG_MIN) newStep(); lastStep = st; }
        } else if (t >= S) newStep();
        double back;
        switch (act) {
        case 1: back = S * (double)held; break;                       // stutter: hold the previous slice
        case 2: back = 2.0 * t; break;                                // reverse the previous slice
        case 3: back = S * (double)jump; break;                       // shuffle: a slice from the last bar
        case 4: back = S + 0.5 * t; break;                            // half speed, an octave down
        default: back = 0.0; break;
        }
        t += 1.f;
        const float F = std::max(16.f, 0.0025f * fs);
        const float edge = act ? std::min(1.f, std::min(t / F, std::max(0.f, S - t) / F + (ck.grid ? 0.f : 0.f))) : 0.f;
        const float mix = mixSm.tick(tMix) * edge;
        if (mix > 0.f) {
            const float yl = hist.read(0, back), yr = hist.read(1, back);
            const float ol = l + (yl - l) * mix, orr = r + (yr - r) * mix;
            l += (ol - l) * a; r += (orr - r) * a;
        }
    }
private:
    void newStep() {
        t = 0.f;
        if (rng.uni() < depth) {
            act = mode + 1;
            if (act == 1) held = std::min(8, held + 1); else held = 0;
            if (act == 3) jump = 1 + (int)(rng.uni() * 7.99f);
        } else { act = 0; held = 0; }
    }
    History hist; Rng rng;
    double stepBeats = 0.25; long lastStep = LONG_MIN;
    float t = 0.f, S = 4096.f, depth = 0.6f, tMix = 1.f;
    int mode = 0, act = 0, held = 0, jump = 1;
    bool wasOn = false;
};

// ---------------------------------------------------------------------------------------------
// Tape Sim (texture chain): saturation, head bump, wow, tone roll-off and hiss
class TapeSim {
public:
    Ramp ramp; Smooth amtSm; float fs = 44100.f;
    void prepare(float sr) {
        fs = sr; ramp.setTime(fs, 0.03f); amtSm.setTime(fs, 0.004f);
        hist.prepare((int)(fs * 0.05f));
        drift.setLP(fs, 0.5f); for (int c = 0; c < 2; ++c) { bump[c].setLP(fs, 95.f); bump[c].y = 0.f; tone[c].y = 0.f; hiss[c].setLP(fs, 6000.f); hiss[c].y = 0.f; }
        cTone = -1.f;
    }
    void block(float amt, float toneN) {
        tAmt = amt;
        const float th = 3500.f * std::pow(19000.f / 3500.f, toneN);
        if (std::fabs(th - cTone) > 1.f) { cTone = th; tone[0].setLP(fs, std::min(th, fs * 0.45f)); tone[1].setLP(fs, std::min(th, fs * 0.45f)); }
    }
    inline void process(float& l, float& r, bool on) {
        hist.push(l, r);
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        const float m = amtSm.tick(tAmt);
        ph += 0.9f / fs; if (ph >= 1.f) ph -= 1.f;
        const double back = 0.004 * fs + m * 0.0011f * fs * (0.7f * std::sin(2.f * kPi * ph) + 0.3f * std::max(-1.f, std::min(1.f, drift.lp(rng.bi()) * 8.f)));
        const float d = 1.f + m * 2.5f;
        float y[2];
        for (int c = 0; c < 2; ++c) {
            float x = hist.read(c, back);
            x = std::tanh(x * d) / d * (1.f + 0.2f * m);
            x += m * 0.35f * bump[c].lp(x);
            x = tone[c].lp(x);
            x += hiss[c].lp(rng.bi()) * m * 0.0025f;
            y[c] = x;
        }
        l += (y[0] - l) * a; r += (y[1] - r) * a;
    }
private:
    History hist; Rng rng; OnePole drift, bump[2], tone[2], hiss[2];
    float tAmt = 0.5f, cTone = -1.f, ph = 0.f;
};

// ---------------------------------------------------------------------------------------------
// Safety filter: one knob, low pass to the left, high pass to the right, off in the middle
class DjFilter {
public:
    Smooth knobSm; float fs = 44100.f;
    void prepare(float sr) { fs = sr; knobSm.setTime(fs, 0.004f); knobSm.y = 0.5f; f[0].reset(); f[1].reset(); n = 0; }
    inline void process(float& l, float& r, float knob) {
        const float v = knobSm.tick(knob);
        const float off = std::fabs(v - 0.5f);
        if (off < 0.004f) { f[0].reset(); f[1].reset(); return; }
        if (n-- <= 0) {
            n = 16;
            mode = v < 0.5f ? 0 : 1;
            const float u = std::min(1.f, std::max(0.f, (off - 0.02f) / 0.48f));
            const float hz = mode == 0 ? 20000.f * std::pow(110.f / 20000.f, u) : 20.f * std::pow(7000.f / 20.f, u);
            g = svfG(fs, hz);
            amt = smoothstep01(off / 0.04f);
        }
        const float yl = f[0].tick(l, g, 1.15f, mode), yr = f[1].tick(r, g, 1.15f, mode);
        l += (yl - l) * amt; r += (yr - r) * amt;
    }
private:
    Svf f[2]; float g = 1.f, amt = 0.f; int mode = 0, n = 0;
};


// =================================================================================================
// The pad effects (JAY). Same shape as every other module: prepare, block, process, clear.

static inline float hann(float t) { return 0.5f - 0.5f * std::cos(2.f * kPi * t); }

// a stereo history buffer read with cubic interpolation
struct Hist2 {
    std::vector<float> b[2]; int size = 1, w = 0;
    void prepare(int n) { size = std::max(8, n); b[0].assign(size, 0.f); b[1].assign(size, 0.f); w = 0; }
    void clear() { std::fill(b[0].begin(), b[0].end(), 0.f); std::fill(b[1].begin(), b[1].end(), 0.f); }
    inline void push(float l, float r) { b[0][w] = l; b[1][w] = r; if (++w >= size) w = 0; }
    // position counted in samples back from the newest one
    inline float at(int c, double back) const {
        double p = (double)w - 1.0 - back;
        while (p < 0.0) p += size;
        const int i0 = (int)p; const float t = (float)(p - i0);
        const float* d = b[c].data();
        return hermite(d[(i0 - 1 + size) % size], d[i0 % size], d[(i0 + 1) % size], d[(i0 + 2) % size], t);
    }
};

// HALF SPEED: from the press on, the beat plays back slower (and lower) than it comes in; when it falls
// two bars behind it catches up with a short crossfade
class HalfSpeed {
public:
    Ramp ramp; Smooth mixSm; Hist2 h; float fs = 44100.f;
    void prepare(float sr) { fs = sr; h.prepare((int)(sr * 12.f)); ramp.setTime(fs, 0.006f); mixSm.setTime(fs, 0.004f); mixSm.y = 1.f; clear(); }
    void clear() { h.clear(); lag = 0.0; was = false; xf = 0.f; }
    void block(const BlockCtx& ctx, int speedIdx, float mixPct) {
        static const double r[3] = { 1.5, 2.0, 4.0 };
        rate = 1.0 / r[speedIdx < 0 ? 0 : (speedIdx > 2 ? 2 : speedIdx)];
        tMix = mixPct * 0.01f;
        maxLag = std::min((double)h.size - 64.0, 8.0 * 60.0 / std::max(20.0, ctx.bpm) * fs);
    }
    inline void process(float& l, float& r, bool on) {
        h.push(l, r);
        if (on && !was) { lag = 0.0; xf = 0.f; }
        was = on;
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        lag += 1.0 - rate;                                  // falls behind by (1 - rate) every sample
        float wl = h.at(0, lag), wr = h.at(1, lag);
        if (lag > maxLag) { oldLag = lag; lag = 0.0; xf = 1.f; }
        if (xf > 0.f) {                                     // catch up without a click
            oldLag += 1.0 - rate;
            wl = wl * (1.f - xf) + h.at(0, oldLag) * xf;
            wr = wr * (1.f - xf) + h.at(1, oldLag) * xf;
            xf -= 1.f / (0.01f * fs);
        }
        const float m = mixSm.tick(tMix) * a;
        l += (wl - l) * m; r += (wr - r) * m;
    }
    float phase = -1.f;
private:
    double rate = 0.5, lag = 0.0, oldLag = 0.0, maxLag = 1.0; float tMix = 1.f, xf = 0.f; bool was = false;
};

// LP / HP / BP FILTER: a resonant filter swept by a tempo-synced LFO. RANGE is how far it sweeps.
class LfoFilter {
public:
    Ramp ramp; Smooth resoSm, rangeSm; Svf f[2]; float fs = 44100.f; int mode = 0;
    void prepare(float sr) { fs = sr; ramp.setTime(fs, 0.006f); resoSm.setTime(fs, 0.004f); rangeSm.setTime(fs, 0.004f); clear(); }
    void clear() { f[0].reset(); f[1].reset(); cnt = 0; }
    void block(const BlockCtx&, int m, float resoPct, int speedIdx, float rangePct) {
        mode = m; tReso = resoPct * 0.01f; tRange = rangePct * 0.01f;
        beats = kSyncBeats[speedIdx < 0 ? 0 : (speedIdx > 10 ? 10 : speedIdx)];
    }
    inline void process(float& l, float& r, bool on, const Clock& ck) {
        const float a = ramp.tick(on);
        if (a <= 0.f) return;
        const float reso = resoSm.tick(tReso), range = rangeSm.tick(tRange);
        if (cnt-- <= 0) {
            cnt = 8;
            const double ph = std::fmod(ck.beat / beats, 1.0);
            const float u = 0.5f - 0.5f * std::cos(2.f * kPi * (float)ph);
            float hz;
            if (mode == 0) hz = 18000.f * std::pow(150.f / 18000.f, range * u);
            else if (mode == 1) hz = 25.f * std::pow(6000.f / 25.f, range * u);
            else hz = 1340.f * std::pow(4.5f, (u - 0.5f) * 2.f * range);
            g = svfG(fs, hz); k = 2.f - 1.9f * reso;
        }
        const float yl = f[0].tick(l, g, k, mode), yr = f[1].tick(r, g, k, mode);
        l += (yl - l) * a; r += (yr - r) * a;
    }
    float phase = -1.f;
private:
    float tReso = 0.3f, tRange = 0.6f, g = 0.5f, k = 1.4f; double beats = 1.0; int cnt = 0;
};

// COLOR: six kinds of vintage gear on one knob
class ColorBox {
public:
    Ramp ramp; VinylSim vin; float fs = 44100.f; int mode = 3;
    void prepare(float sr) {
        fs = sr; ramp.setTime(fs, 0.008f); vin.prepare(sr); h.prepare((int)(sr * 0.05f));
        for (int c = 0; c < 2; ++c) { lpCas[c].setLP(fs, 9500.f, 0.7f); lpTube[c].setLP(fs, 11000.f, 0.7f); midTube[c].setPeak(fs, 900.f, 0.8f, 3.f);
                                      hpRad[c].setHP(fs, 450.f, 0.8f); lpRad[c].setLP(fs, 3300.f, 0.9f); hissLp[c].setLP(fs, 7000.f); }
        clear();
    }
    void clear() { vin.clear(); h.clear(); for (int c = 0; c < 2; ++c) { lpCas[c].reset(); lpTube[c].reset(); midTube[c].reset(); hpRad[c].reset(); lpRad[c].reset(); sat[c].reset(); hissLp[c].y = 0.f; } }
    void block(const BlockCtx& ctx, int m) {
        mode = m < 0 ? 0 : (m > 5 ? 5 : m);
        vin.block(ctx, 45.f, 35.f, 20.f, 50.f, 80.f);
    }
    inline void process(float& l, float& r, bool on) {
        const float a = ramp.tick(on);
        if (a <= 0.f && mode != 3) { h.push(l, r); return; }
        const float xl = l, xr = r;
        float yl = l, yr = r;
        switch (mode) {
        case 0: case 1: {      // CASSETTE (gentle wow, rolled-off top, hiss, tape squash) / FLUTTER (heavy wobble)
            h.push(l, r);
            wph += (mode == 0 ? 0.6f : 0.9f) / fs; fph += (mode == 0 ? 9.f : 6.5f) / fs;
            if (wph > 1.f) wph -= 1.f;
            if (fph > 1.f) fph -= 1.f;
            const float dep = mode == 0 ? 0.0009f : 0.0042f;
            const float back = fs * (0.006f + dep * (0.6f * std::sin(2.f * kPi * wph) + 0.4f * std::sin(2.f * kPi * fph)));
            yl = h.at(0, back); yr = h.at(1, back);
            if (mode == 0) {
                yl = lpCas[0].tick(std::tanh(yl * 1.4f) / 1.4f) + hissLp[0].lp(rng.bi()) * 0.0025f;
                yr = lpCas[1].tick(std::tanh(yr * 1.4f) / 1.4f) + hissLp[1].lp(rng.bi()) * 0.0025f;
            }
            break;
        }
        case 2:                // TUBE AMP: warm, uneven saturation with a little mid push
            h.push(l, r);
            yl = lpTube[0].tick(midTube[0].tick((std::tanh(1.8f * (yl + 0.08f)) - 0.1435f) / 1.6f));
            yr = lpTube[1].tick(midTube[1].tick((std::tanh(1.8f * (yr + 0.08f)) - 0.1435f) / 1.6f));
            break;
        case 3:                // VINYL: the Vinyl Sim
            h.push(l, r);
            vin.process(yl, yr, on);
            l = yl; r = yr;
            return;
        case 4:                // SATURATION: driven hard, anti-aliased
            h.push(l, r);
            yl = sat[0].tick(yl * 3.f) * 0.55f; yr = sat[1].tick(yr * 3.f) * 0.55f;
            break;
        default:               // RADIO: a small speaker with a little grit and noise
            h.push(l, r);
            yl = lpRad[0].tick(hpRad[0].tick(std::tanh(yl * 2.f) * 0.8f)) + hissLp[0].lp(rng.bi()) * 0.004f;
            yr = lpRad[1].tick(hpRad[1].tick(std::tanh(yr * 2.f) * 0.8f)) + hissLp[1].lp(rng.bi()) * 0.004f;
            yr = 0.5f * (yl + yr); yl = yr;
            break;
        }
        l = xl + (yl - xl) * a; r = xr + (yr - xr) * a;
    }
    float phase = -1.f;
private:
    Hist2 h; Rng rng; float wph = 0.f, fph = 0.f;
    Biquad lpCas[2], lpTube[2], midTube[2], hpRad[2], lpRad[2]; AdaaTanh sat[2]; OnePole hissLp[2];
};

// GRANULATOR: grains of the last second of sound, scattered and overlapped; FEEDBACK feeds them back in
class Granulator {
public:
    Ramp ramp; Smooth fbSm; Hist2 h; float fs = 44100.f;
    enum { kGrains = 24 };
    void prepare(float sr) { fs = sr; h.prepare((int)(sr * 2.5f)); ramp.setTime(fs, 0.01f); fbSm.setTime(fs, 0.004f); clear(); }
    void clear() { h.clear(); for (Grain& g : gr) g.on = false; acc = 0.f; tail = 0.f; fbl = fbr = 0.f; }
    void block(const BlockCtx&, float densPerSec, float fbPct, float lenMs) { dens = densPerSec; tFb = fbPct * 0.0095f; len = std::max(16.f, lenMs * 0.001f * fs); }
    inline void process(float& l, float& r, bool on) {
        const float a = ramp.tick(on);
        const float fb = fbSm.tick(tFb);
        h.push(l * a + fbl * fb, r * a + fbr * fb);
        if (a <= 0.f && tail < 1e-6f) { fbl = fbr = 0.f; return; }
        if (a > 0.f) {
            acc += dens / fs;
            while (acc >= 1.f) {
                acc -= 1.f;
                for (Grain& g : gr) if (!g.on) {
                    g.on = true; g.t = 0.f; g.len = len * (0.85f + 0.3f * rng.uni());
                    g.back = g.len + rng.uni() * (fs * 0.9f);
                    g.pan = rng.bi() * 0.6f;
                    break;
                }
            }
        }
        float wl = 0.f, wr = 0.f;
        for (Grain& g : gr) {
            if (!g.on) continue;
            const float w = hann(g.t / g.len);
            const double back = g.back - g.t;
            const float sl = h.at(0, back), sr = h.at(1, back);
            wl += w * sl * (1.f - g.pan); wr += w * sr * (1.f + g.pan);
            if ((g.t += 1.f) >= g.len) g.on = false;
        }
        const float norm = 1.f / std::sqrt(std::max(1.f, dens * len / fs));
        wl *= norm; wr *= norm;
        fbl = wl; fbr = wr;
        tail = std::max(std::fabs(wl), std::fabs(wr)) > tail ? std::max(std::fabs(wl), std::fabs(wr)) : tail * 0.9995f;
        l = l * (1.f - 0.5f * a) + wl; r = r * (1.f - 0.5f * a) + wr;
    }
    float phase = -1.f;
private:
    struct Grain { bool on = false; float t = 0.f, len = 1.f, pan = 0.f; double back = 0.0; };
    Grain gr[kGrains]; Rng rng; float acc = 0.f, dens = 20.f, tFb = 0.f, len = 2000.f, tail = 0.f, fbl = 0.f, fbr = 0.f;
};

// BEAT REPEAT: on the press it grabs the last DIVISION and repeats it (backwards with REVERSE);
// RESONANCE puts a ringing filter on the repeats
class BeatRepeat {
public:
    Ramp ramp; Hist2 h; Svf f[2]; float fs = 44100.f; bool canStart = true;
    void prepare(float sr) { fs = sr; h.prepare((int)(sr * 3.f)); ramp.setTime(fs, 0.004f); clear(); }
    void clear() { h.clear(); f[0].reset(); f[1].reset(); state = 0; }
    void block(const BlockCtx& ctx, int divIdx, int rev, float resoPct) {
        const double beats = kDivBeats[divIdx < 0 ? 0 : (divIdx > 7 ? 7 : divIdx)];
        tLen = std::max(32.0, beats * 60.0 / std::max(20.0, ctx.bpm) * fs);
        reverse = rev != 0; reso = resoPct * 0.01f;
        g = svfG(fs, 2200.f); k = 2.f - 1.95f * reso;
    }
    inline void process(float& l, float& r, bool on) {
        h.push(l, r);
        if (on && state == 0 && canStart) { state = 1; len = tLen; pos = 0.0; held = 0; }
        if (!on) state = 0;
        const float a = ramp.tick(state == 1);
        if (a <= 0.f) return;
        // the slice is the len samples that ended when the press came in; it stays fixed while held
        ++held;
        const double frac = pos / len;
        const double p = reverse ? (1.0 - frac) * len : frac * len;
        const double back = held + len - p;
        float yl = h.at(0, back), yr = h.at(1, back);
        const float edge = (float)std::min(frac, 1.0 - frac) * (float)len / (0.002f * fs);
        const float e = edge < 1.f ? edge : 1.f;
        yl *= e; yr *= e;
        if (reso > 0.01f) { yl = f[0].tick(yl, g, k, 0) * (1.f + reso); yr = f[1].tick(yr, g, k, 0) * (1.f + reso); }
        if ((pos += 1.0) >= len) pos = 0.0;
        l += (yl - l) * a; r += (yr - r) * a;
        phase = (float)frac;
    }
    float phase = -1.f;
private:
    double tLen = 1000.0, len = 1000.0, pos = 0.0; long held = 0; bool reverse = false; float reso = 0.f, g = 0.3f, k = 2.f; int state = 0;
};

// REV STEPPER: slices the beat by DELAY TIME and plays each slice forwards, then backwards, REPEATS times
class RevStepper {
public:
    Ramp ramp; Hist2 h; float fs = 44100.f;
    void prepare(float sr) { fs = sr; h.prepare((int)(sr * 3.f)); ramp.setTime(fs, 0.004f); clear(); }
    void clear() { h.clear(); pos = 0.0; rep = 0; age = 0; }
    void block(const BlockCtx& ctx, int divIdx, int repeats) {
        const double beats = kDivBeats[divIdx < 0 ? 0 : (divIdx > 7 ? 7 : divIdx)];
        tLen = std::max(32.0, beats * 60.0 / std::max(20.0, ctx.bpm) * fs);
        reps = repeats < 2 ? 2 : (repeats > 8 ? 8 : repeats);
    }
    inline void process(float& l, float& r, bool on) {
        h.push(l, r);
        const float a = ramp.tick(on);
        if (a <= 0.f) { rep = 0; pos = 0.0; age = 0; len = tLen; return; }
        ++age;                                              // samples since this slice was taken
        const double frac = pos / len;
        const bool backwards = (rep & 1) != 0;
        const double p = backwards ? (1.0 - frac) * len : frac * len;
        const double back = age + len - p;
        const float edge = (float)std::min(frac, 1.0 - frac) * (float)len / (0.002f * fs);
        const float e = edge < 1.f ? edge : 1.f;
        const float yl = h.at(0, back) * e, yr = h.at(1, back) * e;
        if ((pos += 1.0) >= len) { pos = 0.0; if (++rep >= reps) { rep = 0; age = 0; len = tLen; } }
        l += (yl - l) * a; r += (yr - r) * a;
        phase = (float)frac;
    }
    float phase = -1.f;
private:
    double tLen = 1000.0, len = 1000.0, pos = 0.0; long age = 0; int rep = 0, reps = 4;
};

// DELAY: clean synced echoes; RANGE is normal, cross-feedback (each side feeds the other) or ping-pong
class MpcDelay {
public:
    Ramp ramp; Smooth fbSm, tSm; float fs = 44100.f;
    void prepare(float sr) {
        fs = sr; size = (int)(sr * 5.f) + 8;
        d[0].assign(size, 0.f); d[1].assign(size, 0.f); w = 0;
        ramp.setTime(fs, 0.004f); fbSm.setTime(fs, 0.004f); tSm.setTime(fs, 0.04f); tSm.y = fs * 0.5f;
        hp[0].setLP(fs, 40.f); hp[1].setLP(fs, 40.f);
        clear();
    }
    void clear() { std::fill(d[0].begin(), d[0].end(), 0.f); std::fill(d[1].begin(), d[1].end(), 0.f); hp[0].y = hp[1].y = 0.f; }
    void block(const BlockCtx& ctx, int timeIdx, float fbPct, int range) {
        const double beats = kMpcTimeBeats[timeIdx < 0 ? 0 : (timeIdx > 16 ? 16 : timeIdx)];
        tTime = (float)std::min((double)size - 8.0, beats * 60.0 / std::max(20.0, ctx.bpm) * fs);
        tFb = fbPct * 0.0095f; mode = range < 0 ? 0 : (range > 2 ? 2 : range);
    }
    inline void process(float& l, float& r, bool on) {
        const float a = ramp.tick(on);
        const float t = tSm.tick(tTime), fb = fbSm.tick(tFb);
        float rp = (float)w - t; if (rp < 0.f) rp += (float)size;
        const int i0 = (int)rp; const float fr = rp - (float)i0;
        auto rd = [&](int c) { const float* b = d[c].data(); return hermite(b[(i0 - 1 + size) % size], b[i0], b[(i0 + 1) % size], b[(i0 + 2) % size], fr); };
        const float el = rd(0), er = rd(1);
        float inL = l * a, inR = r * a, nl, nr;
        if (mode == 0) { nl = inL + el * fb; nr = inR + er * fb; }
        else if (mode == 1) { nl = inL + er * fb; nr = inR + el * fb; }
        else { nl = 0.5f * (inL + inR) + er * fb; nr = el * fb; }
        d[0][w] = softCeil(nl - hp[0].lp(nl)); d[1][w] = softCeil(nr - hp[1].lp(nr));
        if (++w >= size) w = 0;
        l += el * 0.7f; r += er * 0.7f;
    }
    float phase = -1.f;
private:
    std::vector<float> d[2]; int size = 8, w = 0, mode = 0; float tTime = 1000.f, tFb = 0.4f; OnePole hp[2];
};

// REVERB: a room in front of Yancey's reverb engine; PRE-DELAY, DECAY and DIFFUSION shape it
class MpcReverb {
public:
    WaveVerb verb; Ramp ramp; Smooth difSm; float fs = 44100.f;
    void prepare(float sr) {
        fs = sr; verb.prepare(sr); ramp.setTime(fs, 0.006f); difSm.setTime(fs, 0.004f);
        static const float ms[4] = { 4.7f, 7.3f, 11.9f, 17.1f };
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) ap[c][k].prepare((int)(fs * ms[k] * 0.001f * (c ? 1.07f : 1.f)), 0.5f);
        clear();
    }
    void clear() { verb.clear(); for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) ap[c][k].clear(); }
    void idleCheck() { verb.idleCheck(); }
    void block(const BlockCtx& ctx, float preMs, float decayPct, float diffPct) {
        const float decayS = 0.3f * std::pow(40.f, clampf(decayPct * 0.01f, 0.f, 1.f));    // 0.3 s .. 12 s
        verb.block(ctx, decayS, 12.f, 60.f, preMs, 55.f);
        tDif = diffPct * 0.01f;
    }
    inline void process(float& l, float& r, bool on) {
        const float dif = difSm.tick(tDif);
        const float g = 0.15f + 0.6f * dif;
        float xl = l, xr = r;
        for (int k = 0; k < 4; ++k) { ap[0][k].g = g; ap[1][k].g = g; xl = ap[0][k].tick(xl); xr = ap[1][k].tick(xr); }
        // diffuse what goes into the reverb, keep the dry sound as it was
        const float dl = l, dr = r;
        float wl = xl, wr = xr;
        verb.process(wl, wr, on);
        l = dl + (wl - xl); r = dr + (wr - xr);
    }
    float phase = -1.f;
private:
    Allpass ap[2][4]; float tDif = 0.7f;
};

} // namespace yancey
