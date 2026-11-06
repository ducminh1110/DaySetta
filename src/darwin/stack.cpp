#include "stack.hpp"

#include <cstdio>
#include <cstring>

namespace ds::darwin {

uint64_t build_initial_stack(uint64_t top, size_t capacity, const StackSpec& s) {
    std::vector<std::string> apple;
    apple.push_back("executable_path=" + s.executable_path);
    char b[128];
    std::snprintf(b, sizeof b, "ptr_munge=0x%llx", (unsigned long long)s.entropy[0]);
    apple.push_back(b);
    // like xnu (exec_add_entropy_key embedNUL): clear byte 1 of the guard to stop string overflows
    std::snprintf(b, sizeof b, "stack_guard=0x%llx", (unsigned long long)(s.entropy[1] & ~(0xffull << 8)));
    apple.push_back(b);
    std::snprintf(b, sizeof b, "malloc_entropy=0x%llx,0x%llx", (unsigned long long)s.entropy[2], (unsigned long long)s.entropy[3]);
    apple.push_back(b);
    if (s.with_main_stack) {
        std::snprintf(b, sizeof b, "main_stack=0x%llx,0x%llx,0x%llx,0x%llx", (unsigned long long)s.main_stack[0],
                      (unsigned long long)s.main_stack[1], (unsigned long long)s.main_stack[2], (unsigned long long)s.main_stack[3]);
        apple.push_back(b);
    }
    size_t str_bytes = 0;
    for (auto& a : s.argv) str_bytes += a.size() + 1;
    for (auto& a : s.envp) str_bytes += a.size() + 1;
    for (auto& a : apple) str_bytes += a.size() + 1;
    const size_t nptr = 1 + 1 + (s.argv.size() + 1) + (s.envp.size() + 1) + (apple.size() + 1);
    const size_t total = ((str_bytes + 15) & ~size_t(15)) + ((nptr * 8 + 15) & ~size_t(15));
    if (total + 16 > capacity) return 0;

    uint64_t strp = top - ((str_bytes + 15) & ~size_t(15));
    uint64_t sp = (strp - nptr * 8) & ~uint64_t(15);
    // Keep SP 16-byte aligned: shift the string area if needed (sp only moves down).
    uint64_t* w = reinterpret_cast<uint64_t*>(sp);
    auto put_str = [&](const std::string& str) {
        uint64_t a = strp;
        std::memcpy(reinterpret_cast<void*>(strp), str.c_str(), str.size() + 1);
        strp += str.size() + 1;
        return a;
    };
    *w++ = s.mh;
    *w++ = s.argv.size();
    for (auto& a : s.argv) *w++ = put_str(a);
    *w++ = 0;
    for (auto& a : s.envp) *w++ = put_str(a);
    *w++ = 0;
    for (auto& a : apple) *w++ = put_str(a);
    *w++ = 0;
    return sp;
}

}  // namespace ds::darwin
