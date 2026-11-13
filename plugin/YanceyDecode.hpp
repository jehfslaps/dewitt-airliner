// Yancey: audio file decoding for the sample pads.
#pragma once
#include "YanceySamples.hpp"
#include <string>
namespace yancey {
// WAV, AIFF, FLAC or MP3; longer files are trimmed to maxSeconds. fullSeconds (if given) gets the
// untrimmed length, so a caller can tell a one-shot from a whole beat.
bool decodeAudioFile(const char* utf8Path, SampleData& out, std::string& error,
                     double maxSeconds = kMaxSampleSeconds, double* fullSeconds = nullptr);
}
