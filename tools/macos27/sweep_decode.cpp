// cache-sweep decoder bridge: runs the real ds::exec decode() over unique cache words.
// stdin lines "<hexword> <count>", stdout "<hexword> <count> <op> <detail>"; op is
// op_name(), "ldapr" for is_known_unsupported(), "none" when decode() rejects the word.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "exec/lowering.hpp"

using namespace ds::exec;

int main() {
    char line[128];
    while (std::fgets(line, sizeof line, stdin)) {
        char* end;
        const uint32_t w = uint32_t(std::strtoul(line, &end, 16));
        const uint64_t n = std::strtoull(end, nullptr, 10);
        Insn in;
        const char* op = "none";
        char detail[64] = "";
        if (decode(w, in)) {
            op = op_name(in.op);
            if (in.op == Op::Rmw)
                std::snprintf(detail, sizeof detail, "%s size=%u a=%u r=%u", rmw_name(in.rmw), in.size, in.acquire, in.release);
            else if (in.op == Op::Fp16 || in.op == Op::Fp16V)
                std::snprintf(detail, sizeof detail, "hk=%u", unsigned(in.hk));
            else if (in.op == Op::Sha3)
                std::snprintf(detail, sizeof detail, "sk=%u", unsigned(in.sk));
        } else if (is_known_unsupported(w)) {
            op = "ldapr";
        }
        std::printf("%08x %llu %s %s\n", w, (unsigned long long)n, op, detail);
    }
    return 0;
}
