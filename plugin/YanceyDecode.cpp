// Yancey: decode a dropped / chosen audio file into a one-shot sample.
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DR_FLAC_NO_STDIO
#define DR_MP3_NO_STDIO
#include "third_party/dr_wav.h"
#include "third_party/dr_flac.h"
#include "third_party/dr_mp3.h"

#include "YanceyDecode.hpp"
#include "YanceyPlatform.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace yancey {

static std::string lowerExt(const std::string& path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return std::string();
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

static std::string baseName(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = n.find_last_of('.');
    if (dot != std::string::npos && dot > 0) n = n.substr(0, dot);
    if (n.size() > 40) n = n.substr(0, 40);
    return n;
}

bool decodeAudioFile(const char* path, SampleData& out, std::string& error, double maxSeconds, double* fullSeconds)
{
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes)) { error = "Couldn't open that file"; return false; }

    const std::string ext = lowerExt(path);
    unsigned int ch = 0, rate = 0;
    drwav_uint64 frames = 0;
    float* pcm = nullptr;
    bool fromMp3 = false, fromFlac = false;

    if (ext == "mp3") {
        drmp3_config cfg;
        drmp3_uint64 n = 0;
        pcm = drmp3_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &cfg, &n, nullptr);
        ch = cfg.channels; rate = cfg.sampleRate; frames = n; fromMp3 = true;
    } else if (ext == "flac") {
        drflac_uint64 n = 0;
        pcm = drflac_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &ch, &rate, &n, nullptr);
        frames = n; fromFlac = true;
    } else {
        // WAV, AIFF and anything else dr_wav recognises
        pcm = drwav_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &ch, &rate, &frames, nullptr);
    }
    if (pcm == nullptr || frames == 0 || ch == 0 || rate < 1000) {
        if (pcm) { if (fromMp3) drmp3_free(pcm, nullptr); else if (fromFlac) drflac_free(pcm, nullptr); else drwav_free(pcm, nullptr); }
        error = "That file type isn't supported (use WAV, AIFF, FLAC or MP3)";
        return false;
    }

    if (fullSeconds != nullptr) *fullSeconds = rate > 0 ? (double)frames / rate : 0.0;
    const drwav_uint64 maxFrames = (drwav_uint64)(maxSeconds * rate);
    const int n = (int)std::min(frames, maxFrames);
    out.name = baseName(path);
    out.rate = rate;
    out.channels = ch >= 2 ? 2 : 1;
    out.frames = n;
    out.l.assign(n, 0.f);
    out.r.assign(out.channels == 2 ? n : 0, 0.f);
    for (int i = 0; i < n; ++i) {
        const float* f = pcm + (size_t)i * ch;
        if (ch == 1) out.l[i] = f[0];
        else if (ch == 2) { out.l[i] = f[0]; out.r[i] = f[1]; }
        else {
            // more than two channels: fold everything down to stereo
            float a = 0.f, b = 0.f;
            for (unsigned c = 0; c < ch; ++c) (c % 2 ? b : a) += f[c];
            out.l[i] = a / ((ch + 1) / 2); out.r[i] = b / (ch / 2);
        }
    }
    if (fromMp3) drmp3_free(pcm, nullptr); else if (fromFlac) drflac_free(pcm, nullptr); else drwav_free(pcm, nullptr);
    return true;
}

} // namespace yancey
