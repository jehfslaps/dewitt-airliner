// Yancey: a direct line from the engine to the screen, so knob moves show up the instant they happen
// instead of waiting for the host to pass output parameters along.
#pragma once
#include <atomic>
#include <mutex>
#include <vector>
#include "YanceyParams.hpp"

namespace yancey {

struct YanceyLive {
    std::atomic<int> seq { 0 };
    std::atomic<int> picked { 0 };
    std::atomic<float> job[kNumJobs];
    std::atomic<float> amt[kNumSlots];          // each effect pad's live amount (0 when it isn't playing)
    YanceyLive() { for (int j = 0; j < kNumJobs; ++j) job[j].store(0.f); for (int s = 0; s < kNumSlots; ++s) amt[s].store(0.f); }
};

inline std::mutex& yLiveMutex() { static std::mutex m; return m; }
inline std::vector<std::pair<const void*, YanceyLive*> >& yLiveReg() { static std::vector<std::pair<const void*, YanceyLive*> > r; return r; }
inline void yLiveAdd(const void* key, YanceyLive* l) { std::lock_guard<std::mutex> g(yLiveMutex()); yLiveReg().push_back(std::make_pair(key, l)); }
inline void yLiveRemove(const void* key)
{
    std::lock_guard<std::mutex> g(yLiveMutex());
    std::vector<std::pair<const void*, YanceyLive*> >& r = yLiveReg();
    for (size_t i = 0; i < r.size(); ++i) if (r[i].first == key) { r.erase(r.begin() + (long)i); return; }
}
inline YanceyLive* yLiveFind(const void* key)
{
    std::lock_guard<std::mutex> g(yLiveMutex());
    for (const auto& e : yLiveReg()) if (e.first == key) return e.second;
    return nullptr;
}

} // namespace yancey
