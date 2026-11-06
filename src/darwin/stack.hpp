// Darwin initial process stack ("KernelArgs") as xnu's exec lays it out for dyld:
//   sp-> mh | argc | argv[argc] | NULL | envp[] | NULL | apple[] | NULL | string area
// Source: dyld-1378 dyld/dyldStartup.s header comment (layout) and xnu bsd/kern/kern_exec.c
// exec_add_apple_strings (apple[] keys: executable_path=, stack_guard=, malloc_entropy=,
// ptr_munge=, main_stack=).  Darling mldr/stack.c was read for semantics only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ds::darwin {

struct StackSpec {
    uint64_t mh = 0;  // main executable mach_header address
    std::vector<std::string> argv, envp;
    std::string executable_path;
    uint64_t entropy[8] = {};  // [0] ptr_munge, [1] stack_guard (low byte of bit 8..15 cleared), [2..3] malloc_entropy
    uint64_t main_stack[4] = {};  // user_stack, user_stack_size, user_stack_alloc, user_stack_alloc_size
    bool with_main_stack = true;
};

// Writes the structure into [top - capacity, top) and returns the initial SP (16-byte aligned).
// `top` is a host address of writable memory.  Returns 0 if it does not fit.
uint64_t build_initial_stack(uint64_t top, size_t capacity, const StackSpec& spec);

}  // namespace ds::darwin
