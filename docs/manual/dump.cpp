// prints Dewitt's effects (name, family, knobs with their default values) as JSON, for the manual
#include <cstdio>
#include <string>
#include "../../plugin/YanceyParams.hpp"
using namespace yancey;
static std::string js(const char* s) { std::string o = "\""; for (; *s; ++s) { if (*s == '"' || *s == '\\') o += '\\'; o += *s; } return o + "\""; }
int main()
{
    std::printf("{\"effects\":[\n");
    for (int i = 0; i < kNumEffects; ++i) {
        const int e = kChainOrder[i];
        const EffectSpec& es = kEffects[e];
        std::printf("%s{\"id\":%d,\"name\":%s,\"family\":%s,\"tail\":%d,\"front\":%d,\"knobs\":[", i ? ",\n" : "", e, js(es.name).c_str(), js(kFamilyNames[es.family]).c_str(), es.tail, es.nFront);
        for (int k = 0; k < numControls(e); ++k) {
            char d[48]; formatFmt(es.c[k].fmt, es.c[k].def, d, sizeof(d));
            char lo[48], hi[48]; formatFmt(es.c[k].fmt, es.c[k].min, lo, sizeof(lo)); formatFmt(es.c[k].fmt, es.c[k].max, hi, sizeof(hi));
            std::printf("%s{\"label\":%s,\"def\":%s,\"lo\":%s,\"hi\":%s}", k ? "," : "", js(es.c[k].label).c_str(), js(d).c_str(), js(lo).c_str(), js(hi).c_str());
        }
        std::printf("]}");
    }
    std::printf("],\n\"moves\":[");
    for (int m = 0; m < kNumMoves; ++m) std::printf("%s{\"id\":%d,\"name\":%s,\"hold\":%d}", m ? "," : "", m, js(kMoveNames[m]).c_str(), moveIsHold(m) ? 1 : 0);
    std::printf("]}\n");
}
