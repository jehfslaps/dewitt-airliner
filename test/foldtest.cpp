// Dewitt: checks for the fewer-knobs fallbacks (foldRule / foldResolve / knobCount)
#include "YanceyParams.hpp"
#include <cstdio>
#include <cstring>
using namespace yancey;
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); ++fails; } else printf("ok   %s\n", m); } while (0)
int main()
{
    int map[kNumLearn]; for (int i = 0; i < kNumLearn; ++i) map[i] = -1;
    CHECK(knobCount(map, 0) == kNumJobs, "no knobs linked: all eight (nothing folds)");
    map[kLearnKnob0] = 1; map[kLearnKnob0 + 1] = 2; map[kLearnKnob0 + 2] = 3;
    CHECK(knobCount(map, 0) == 3, "three knobs linked: three");
    CHECK(knobCount(map, 2) == 2 && knobCount(map, 5) == kNumJobs, "set by hand wins (5 = five or more)");
    // every effect, every count: each control is either reachable, follows a reachable knob, or is fixed
    bool ok = true, intFollows = false;
    for (int e = 0; e < kNumEffects; ++e)
        for (int n = 1; n <= 5; ++n)
            for (int c = 0; c < numControls(e); ++c) {
                const Fold f = foldResolve(e, c, n);
                if (c < n) ok &= f.host == c && f.amt == 1.f;
                else {
                    ok &= f.host < n && (f.host < 0 || (std::fabs(f.amt) > 0.f && std::fabs(f.amt) <= 1.f));
                    if (f.host >= 0 && (kEffects[e].c[c].scale == kInt || kEffects[e].c[f.host].scale == kInt)) intFollows = true;
                }
            }
    CHECK(ok, "every effect at 1-5 knobs: each control has its own knob, follows one that does, or stays fixed");
    CHECK(!intFollows, "no stepped control (TIME, TYPE, RATE...) follows or is followed");
    auto follows = [](int e, int c, int n, int host) { return foldResolve(e, c, n).host == host; };
    CHECK(follows(kFxTapeEcho, 3, 3, 1) && follows(kFxTapeEcho, 4, 3, 1) && foldResolve(kFxTapeEcho, 4, 3).amt < 0.f, "3 knobs, TAPE ECHO: WOW and TONE (darker) follow FEEDBACK");
    CHECK(follows(kFxTapeEcho, 4, 4, 1) && foldResolve(kFxTapeEcho, 3, 4).host == 3, "4 knobs, TAPE ECHO: WOW gets knob 4, TONE still follows FEEDBACK");
    CHECK(foldResolve(kFxTapeEcho, 3, 1).host == -1, "1 knob, TAPE ECHO: WOW stays fixed (FEEDBACK has no knob)");
    CHECK(follows(kFxCoil, 3, 3, 0) && follows(kFxCoil, 4, 3, 0), "COIL: FLT MOD and AMP MOD follow DEPTH");
    CHECK(follows(kFxWaveVerb, 3, 3, 0) && foldResolve(kFxWaveVerb, 4, 3).host == -1, "WAVE VERB: PRE DELAY follows DECAY, TONE fixed");
    CHECK(foldResolve(kFxFilterDrive, 3, 3).host == -1 && foldResolve(kFxFilterDrive, 4, 3).host == -1, "FILTER + DRIVE: TYPE and MIX stay where set");
    int big = 0; for (int e = 0; e < kNumEffects; ++e) if (numControls(e) > 3) ++big;
    char m[80]; std::snprintf(m, sizeof(m), "%d effects have more than three controls", big); CHECK(big == 12, m);
    printf("\n%d failure(s)\n", fails);
    return fails ? 1 : 0;
}
