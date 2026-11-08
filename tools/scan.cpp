// Reads "<addr>: <word>" lines of words base-v8.0 objdump cannot decode; prints groups ds-exec does not cover.
#include "lowering.hpp"
#include <cstdio>
#include <map>
using namespace ds::exec;
int main() {
    char line[256]; std::map<uint32_t, std::pair<unsigned long, uint32_t>> g; unsigned long tot = 0, cov = 0;
    while (fgets(line, sizeof line, stdin)) {
        unsigned long a; unsigned w;
        if (sscanf(line, "%lx: %x", &a, &w) != 2) continue;
        ++tot; Insn in;
        if (decode(w, in) || is_known_unsupported(w)) { ++cov; continue; }
        auto& e = g[w & 0xFFE0FC00]; if (!e.first) e.second = w; ++e.first;
    }
    printf("unknown=%lu covered=%lu\n", tot, cov);
    for (auto& [k, v] : g) printf("%08x %lu\n", v.second, v.first);
}
