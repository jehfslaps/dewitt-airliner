// Yancey: one-shot sample storage shared by the DSP and the UI.
// Samples live inside the project as a state string: "name|rate|channels|frames|<base64 16-bit PCM>".
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace yancey {

static constexpr double kMaxSampleSeconds = 12.0;

struct SampleData {
    std::string name;
    double rate = 44100.0;
    int channels = 1;
    int frames = 0;
    std::vector<float> l, r;     // planar; r is empty for mono
};

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline std::string base64Encode(const uint8_t* d, size_t n)
{
    std::string out;
    out.reserve(((n + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        const uint32_t v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i < n) {
        uint32_t v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63];
        out += (i + 1 < n) ? kB64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

static inline bool base64Decode(const char* s, size_t n, std::vector<uint8_t>& out)
{
    static int8_t table[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i) table[i] = -1;
        for (int i = 0; i < 64; ++i) table[(uint8_t)kB64[i]] = (int8_t)i;
        init = true;
    }
    out.clear();
    out.reserve(n / 4 * 3);
    uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t c = (uint8_t)s[i];
        if (c == '=') break;
        const int8_t v = table[c];
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((acc >> bits) & 0xFF)); }
    }
    return true;
}

static inline std::string encodeSample(const SampleData& sd)
{
    std::vector<uint8_t> pcm((size_t)sd.frames * sd.channels * 2);
    size_t k = 0;
    for (int i = 0; i < sd.frames; ++i)
        for (int c = 0; c < sd.channels; ++c) {
            float v = (c == 0 ? sd.l[i] : sd.r[i]);
            v = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
            const int16_t q = (int16_t)(v * 32767.f);
            pcm[k++] = (uint8_t)(q & 0xFF);
            pcm[k++] = (uint8_t)((q >> 8) & 0xFF);
        }
    std::string name = sd.name;
    for (char& ch : name) if (ch == '|') ch = '/';
    return name + "|" + std::to_string((int)sd.rate) + "|" + std::to_string(sd.channels) + "|" +
           std::to_string(sd.frames) + "|" + base64Encode(pcm.data(), pcm.size());
}

// header only (name, rate, channels, frames) without touching the audio; returns false if empty
static inline bool peekSample(const char* s, std::string& name, double& rate, int& channels, int& frames)
{
    if (s == nullptr || s[0] == '\0') return false;
    const char* p[4]; int found = 0;
    for (const char* q = s; *q && found < 4; ++q) if (*q == '|') p[found++] = q;
    if (found < 4) return false;
    name.assign(s, p[0] - s);
    rate = std::atof(p[0] + 1);
    channels = std::atoi(p[1] + 1);
    frames = std::atoi(p[2] + 1);
    return frames > 0 && rate > 1000.0 && (channels == 1 || channels == 2);
}

static inline bool decodeSample(const char* s, SampleData& sd)
{
    if (!peekSample(s, sd.name, sd.rate, sd.channels, sd.frames)) return false;
    const char* data = s;
    for (int bars = 0; *data && bars < 4; ++data) if (*data == '|') ++bars;
    std::vector<uint8_t> pcm;
    base64Decode(data, std::strlen(data), pcm);
    const int frames = (int)(pcm.size() / (2 * (size_t)sd.channels));
    if (frames < sd.frames) sd.frames = frames;
    sd.l.assign(sd.frames, 0.f);
    sd.r.assign(sd.channels == 2 ? sd.frames : 0, 0.f);
    size_t k = 0;
    for (int i = 0; i < sd.frames; ++i)
        for (int c = 0; c < sd.channels; ++c) {
            const int16_t q = (int16_t)(pcm[k] | (pcm[k + 1] << 8));
            k += 2;
            (c == 0 ? sd.l : sd.r)[i] = q / 32768.f;
        }
    return sd.frames > 0;
}

} // namespace yancey
