// the built-in sets: names unique, at most 48 pads, every knob value inside its knob's range
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include "../plugin/YanceySets.hpp"
using namespace yancey;
int main()
{
    int fails = 0;
    std::set<std::string> names;
    for (int i = 0; i < numFactorySets(); ++i) {
        if (!names.insert(factorySetName(i)).second) { std::printf("FAIL: two sets called %s\n", factorySetName(i)); ++fails; }
        if (i == 0) continue;
        const FactorySet& fs = factorySets()[(size_t)(i - 1)];
        if (fs.pads.size() > (size_t)kNumPads || fs.pads.size() % kPadsPerPage) { std::printf("FAIL: %s has %zu pads\n", fs.name, fs.pads.size()); ++fails; }
        for (size_t s = 0; s < fs.pads.size(); ++s) {
            const SetPad& p = fs.pads[s];
            const int e = effectOfType(p.type);
            for (int k = 0; k < kKnobsPerSlot; ++k) {
                if (std::isnan(p.k[k])) continue;
                if (e < 0 || k >= numControls(e)) { std::printf("FAIL: %s pad %c%zu: knob %d doesn't exist\n", fs.name, 'A' + (int)(s / 16), s % 16 + 1, k + 1); ++fails; continue; }
                const CtlSpec& c = kEffects[e].c[k];
                const bool intOk = c.scale != kInt || std::fabs(p.k[k] - std::round(p.k[k])) < 1e-6f;
                if (p.k[k] < c.min - 1e-6f || p.k[k] > c.max + 1e-6f || !intOk) {
                    std::printf("FAIL: %s pad %c%zu (%s) %s = %g, outside %g..%g\n", fs.name, 'A' + (int)(s / 16), s % 16 + 1, kEffects[e].name, c.label, p.k[k], c.min, c.max); ++fails;
                }
            }
        }
        float vals[kNumSlots * kSlotFields];
        const int banks = factorySetParams(i, vals);
        int hold = 0, moves = 0; for (int s = 0; s < kNumSlots; ++s) { hold += effectOf(vals, s) >= 0; moves += moveOf(vals, s) >= 0; }
        std::printf("%-8s %-16s banks %d, %2d effects, %2d moves\n", fs.group, fs.name, banks, hold, moves);
    }
    std::printf("%d failure(s)\n", fails);
    return fails ? 1 : 0;
}
