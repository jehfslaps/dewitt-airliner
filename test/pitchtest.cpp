// PITCH: lands on the right note (sine in, frequency out), glides, and hands back the exact dry sound
#include <cmath>
#include <cstdio>
#include <vector>
#include "../plugin/YanceyParams.hpp"
#include "../plugin/YanceyDSP.hpp"
using namespace yancey;
static int fails = 0;
#define CHECK(c, m) do { if (c) std::printf("ok   %s\n", m); else { std::printf("FAIL %s\n", m); ++fails; } } while (0)
#include <complex>
// the loudest frequency (FFT of a Hann-windowed stretch, zero-padded, peak refined by a parabola)
static float freqOf(const std::vector<float>& y, float fs)
{
    size_t n = 1; while (n < y.size() * 4) n <<= 1;
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < y.size(); ++i) a[i] = y[i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * i / (y.size() - 1)));
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> w = std::polar(1.0, -2.0 * M_PI / len);
        for (size_t i = 0; i < n; i += len) { std::complex<double> wk = 1; for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], v = a[i + k + len / 2] * wk; a[i + k] = u + v; a[i + k + len / 2] = u - v; wk *= w; } }
    }
    size_t best = 1; double bm = 0;
    for (size_t k = 1; k < n / 2 - 1; ++k) { const double m = std::abs(a[k]); if (m > bm) { bm = m; best = k; } }
    const double m0 = std::log(std::abs(a[best - 1]) + 1e-12), m1 = std::log(bm + 1e-12), m2 = std::log(std::abs(a[best + 1]) + 1e-12);
    const double d = 0.5 * (m0 - m2) / (m0 - 2 * m1 + m2);
    return (float)((best + d) * fs / n);
}
static std::vector<float> run(PitchShift& p, float fs, float hz, float secs, bool on, float st, float glide, float cents, float grain, double& ph)
{
    BlockCtx ctx; ctx.fs = fs;
    std::vector<float> out;
    for (int i = 0; i < (int)(secs * fs); ++i) {
        if (i % 64 == 0) p.block(ctx, st, glide, 100.f, cents, grain);
        float l = 0.5f * (float)std::sin(ph), r = l; ph += 2.0 * M_PI * hz / fs;
        p.process(l, r, on); out.push_back(l);
    }
    return out;
}
int main()
{
    for (float fs : { 44100.f, 48000.f, 96000.f }) {
        const struct { float st, cents; } cases[] = { { -12, 0 }, { 12, 0 }, { 7, 0 }, { -5, 0 }, { 0, 50 } };
        for (auto c : cases) for (float grain : { 0.f, 100.f }) {
            PitchShift p; p.prepare(fs); double ph = 0;
            run(p, fs, 440.f, 0.3f, true, c.st, 0.f, c.cents, grain, ph);
            const std::vector<float> y = run(p, fs, 440.f, 1.0f, true, c.st, 0.f, c.cents, grain, ph);
            const float want = 440.f * std::exp2((c.st + c.cents * 0.01f) / 12.f), got = freqOf(y, fs);
            const float errCents = 1200.f * std::log2(got / want);
            char m[160]; std::snprintf(m, sizeof(m), "%5.0f Hz: %+3.0f st %+3.0f ct, GRAIN %3.0f -> %.1f Hz (want %.1f, %+.1f ct)", fs, c.st, c.cents, grain, got, want, errCents);
            CHECK(std::fabs(errCents) < 3.f, m);
        }
    }
    {   // warble: how much of a steady note's energy stays on the note (a two-head shifter without lining up: ~60-80%)
        const float fs = 48000.f;
        for (float st : { -12.f, 7.f, 12.f }) for (float grain : { 0.f, 100.f }) {
            PitchShift p; p.prepare(fs); double ph = 0;
            run(p, fs, 440.f, 0.3f, true, st, 0.f, 0, grain, ph);
            const std::vector<float> y = run(p, fs, 440.f, 1.0f, true, st, 0.f, 0, grain, ph);
            const double want = 440.0 * std::exp2(st / 12.0); double on = 0, all = 0;
            for (double f = 40; f < 4000; f += 1.0) {
                double re = 0, im = 0; for (size_t i = 0; i < y.size(); i += 2) { re += y[i] * std::cos(2 * M_PI * f * i / fs); im += y[i] * std::sin(2 * M_PI * f * i / fs); }
                const double e = re * re + im * im; all += e; if (std::fabs(f - want) < 6.0) on += e;
            }
            char m[120]; std::snprintf(m, sizeof(m), "%+3.0f st, GRAIN %3.0f: %.1f%% of the energy on the note", st, grain, 100.0 * on / all);
            CHECK(on / all > (grain > 50.f ? 0.80 : 0.95), m);
        }
    }
    {   // GLIDE 500 ms: halfway there after 250 ms, there after 500 ms
        const float fs = 48000.f; PitchShift p; p.prepare(fs); double ph = 0;
        run(p, fs, 440.f, 0.2f, true, 0, 0.f, 0, 30, ph);
        std::vector<float> y = run(p, fs, 440.f, 0.25f, true, -12, 500.f, 0, 30, ph);
        const float mid = freqOf(std::vector<float>(y.end() - 4800, y.end()), fs);
        y = run(p, fs, 440.f, 0.5f, true, -12, 500.f, 0, 30, ph);
        const float end = freqOf(std::vector<float>(y.end() - 9600, y.end()), fs);
        char m[160]; std::snprintf(m, sizeof(m), "GLIDE 500 ms: %.0f Hz at 250 ms (halfway is ~311), %.0f Hz at 750 ms (want 220)", mid, end);
        CHECK(mid > 280.f && mid < 345.f && std::fabs(end - 220.f) < 2.f, m);
        // let go: glides back, then the exact dry sound
        y = run(p, fs, 440.f, 0.6f, false, -12, 500.f, 0, 30, ph);
        const float back = freqOf(std::vector<float>(y.end() - 9600, y.end()), fs);
        BlockCtx ctx; ctx.fs = fs; float maxd = 0.f;
        for (int i = 0; i < 4800; ++i) {
            float l = 0.5f * (float)std::sin(ph), r = l; const float x = l; ph += 2.0 * M_PI * 440.0 / fs;
            if (i % 64 == 0) p.block(ctx, -12, 500.f, 100.f, 0, 30);
            p.process(l, r, false); maxd = std::max(maxd, std::fabs(l - x));
        }
        std::snprintf(m, sizeof(m), "let go: back to %.0f Hz, then exactly dry (largest difference %.2g)", back, maxd);
        CHECK(std::fabs(back - 440.f) < 3.f && maxd == 0.f, m);
    }
    std::printf("%d failure(s)\n", fails);
    return fails ? 1 : 0;
}
