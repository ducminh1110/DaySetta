#include "trap.hpp"

#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ds::exec {

namespace {

// ---- LL/SC primitives -------------------------------------------------------------------
// One compare-and-swap loop per (size, acquire, release): LDX/LDAX + STX/STLX, CLREX on
// mismatch.  Returns the value observed in memory (== expected on success).  The RMW
// emulations below are CAS loops around these, so every architecturally visible memory
// operation is a real exclusive pair with the variant's acquire/release semantics.
#define DS_CAS(NAME, LDX, STX, M)                                                          \
    inline uint64_t NAME(void* p, uint64_t exp, uint64_t nv) {                             \
        uint64_t old;                                                                      \
        uint32_t fail;                                                                     \
        asm volatile("1: " LDX " %" M "[old], [%[p]]\n"                                    \
                     "   cmp %[old], %[exp]\n"                                             \
                     "   b.ne 2f\n"                                                        \
                     "   " STX " %w[fail], %" M "[nv], [%[p]]\n"                           \
                     "   cbnz %w[fail], 1b\n"                                              \
                     "   b 3f\n"                                                           \
                     "2: clrex\n"                                                          \
                     "3:\n"                                                                \
                     : [old] "=&r"(old), [fail] "=&r"(fail)                                \
                     : [p] "r"(p), [exp] "r"(exp), [nv] "r"(nv)                            \
                     : "cc", "memory");                                                    \
        return old;                                                                        \
    }

DS_CAS(cas8_00, "ldxrb", "stxrb", "w")
DS_CAS(cas8_10, "ldaxrb", "stxrb", "w")
DS_CAS(cas8_01, "ldxrb", "stlxrb", "w")
DS_CAS(cas8_11, "ldaxrb", "stlxrb", "w")
DS_CAS(cas16_00, "ldxrh", "stxrh", "w")
DS_CAS(cas16_10, "ldaxrh", "stxrh", "w")
DS_CAS(cas16_01, "ldxrh", "stlxrh", "w")
DS_CAS(cas16_11, "ldaxrh", "stlxrh", "w")
DS_CAS(cas32_00, "ldxr", "stxr", "w")
DS_CAS(cas32_10, "ldaxr", "stxr", "w")
DS_CAS(cas32_01, "ldxr", "stlxr", "w")
DS_CAS(cas32_11, "ldaxr", "stlxr", "w")
DS_CAS(cas64_00, "ldxr", "stxr", "x")
DS_CAS(cas64_10, "ldaxr", "stxr", "x")
DS_CAS(cas64_01, "ldxr", "stlxr", "x")
DS_CAS(cas64_11, "ldaxr", "stlxr", "x")

#define DS_CASP(NAME, LDX, STX)                                                            \
    inline void NAME(void* p, uint64_t e0, uint64_t e1, uint64_t n0, uint64_t n1,          \
                     uint64_t* o0, uint64_t* o1) {                                         \
        uint64_t a, b;                                                                     \
        uint32_t fail;                                                                     \
        asm volatile("1: " LDX " %[a], %[b], [%[p]]\n"                                     \
                     "   cmp %[a], %[e0]\n"                                                \
                     "   ccmp %[b], %[e1], #0, eq\n"                                       \
                     "   b.ne 2f\n"                                                        \
                     "   " STX " %w[fail], %[n0], %[n1], [%[p]]\n"                         \
                     "   cbnz %w[fail], 1b\n"                                              \
                     "   b 3f\n"                                                           \
                     "2: clrex\n"                                                          \
                     "3:\n"                                                                \
                     : [a] "=&r"(a), [b] "=&r"(b), [fail] "=&r"(fail)                      \
                     : [p] "r"(p), [e0] "r"(e0), [e1] "r"(e1), [n0] "r"(n0), [n1] "r"(n1) \
                     : "cc", "memory");                                                    \
        *o0 = a;                                                                           \
        *o1 = b;                                                                           \
    }

DS_CASP(casp_00, "ldxp", "stxp")
DS_CASP(casp_10, "ldaxp", "stxp")
DS_CASP(casp_01, "ldxp", "stlxp")
DS_CASP(casp_11, "ldaxp", "stlxp")

using CasFn = uint64_t (*)(void*, uint64_t, uint64_t);
CasFn pick_cas(unsigned size, bool acq, bool rel) {
    static const CasFn t[4][4] = {
        {cas8_00, cas8_01, cas8_10, cas8_11},
        {cas16_00, cas16_01, cas16_10, cas16_11},
        {cas32_00, cas32_01, cas32_10, cas32_11},
        {cas64_00, cas64_01, cas64_10, cas64_11},
    };
    return t[size][(acq ? 2 : 0) | (rel ? 1 : 0)];
}

uint64_t size_mask(unsigned size) { return size == 3 ? ~uint64_t(0) : (uint64_t(1) << (8u << size)) - 1; }

int64_t sext(uint64_t v, unsigned size) {
    const unsigned sh = 64 - (8u << size);
    return int64_t(v << sh) >> sh;
}

uint64_t rmw_value(Rmw op, unsigned size, uint64_t old, uint64_t val) {
    const uint64_t m = size_mask(size);
    switch (op) {
        case Rmw::Add: return (old + val) & m;
        case Rmw::Clr: return old & ~val & m;
        case Rmw::Eor: return (old ^ val) & m;
        case Rmw::Set: return (old | val) & m;
        case Rmw::Smax: return sext(old, size) > sext(val, size) ? old : val;
        case Rmw::Smin: return sext(old, size) < sext(val, size) ? old : val;
        case Rmw::Umax: return old > val ? old : val;
        case Rmw::Umin: return old < val ? old : val;
        default: return val;  // Swp
    }
}

uint64_t modifier(const Insn& in, const CpuState& c) {
    switch (in.mod) {
        case ModSrc::Zero: return 0;
        case ModSrc::Sp: return c.sp;
        // FEAT_PAuth_LR: hardware hashes SP with a PC; the soft PAC only needs sign/auth to agree.
        // Sign (imm == 0) uses the signing instruction's own address; auth uses the label operand,
        // which the compiler points at the signing instruction.  INFERRED from LLVM output, not HW.
        case ModSrc::SpPc: return c.sp * 0x9E3779B97F4A7C15ull ^ (c.pc - uint64_t(in.imm));
        default: return c.get(in.rm);
    }
}

}  // namespace

EmuOutcome emulate(const Insn& in, CpuState& c, const Pac& pac, TrapStats& stats) {
    EmuOutcome out;
    stats.by_op[static_cast<int>(in.op)].fetch_add(1, std::memory_order_relaxed);
    switch (in.op) {
        case Op::PacSign:
            c.set(in.rd, pac.sign(in.key, c.get(in.rd), modifier(in, c)));
            break;
        case Op::PacAuth: {
            const AuthResult r = pac.auth(in.key, c.get(in.rd), modifier(in, c));
            if (!r.ok) stats.auth_fail.fetch_add(1, std::memory_order_relaxed);
            c.set(in.rd, r.value);  // poisoned on failure: no FEAT_FPAC, the fault comes at use
            break;
        }
        case Op::PacStrip:
            c.set(in.rd, pac.strip(c.get(in.rd)));
            break;
        case Op::PacGa:
            c.set(in.rd, pac.pacga(c.get(in.rn), c.get_sp(in.rm)));
            break;
        case Op::BrAuth: {
            const AuthResult r = pac.auth(in.key, c.get(in.rn), modifier(in, c));
            if (in.link) c.x[30] = c.pc + 4;
            c.pc = r.value;
            if (!r.ok) {
                stats.auth_fail.fetch_add(1, std::memory_order_relaxed);
                out.status = Emu::AuthFault;
                out.fault_addr = r.value;
            }
            return out;
        }
        case Op::RetAuth: {
            const AuthResult r = pac.auth(in.key, c.x[30], in.mod == ModSrc::SpPc ? modifier(in, c) : c.sp);
            c.pc = r.value;
            if (!r.ok) {
                stats.auth_fail.fetch_add(1, std::memory_order_relaxed);
                out.status = Emu::AuthFault;
                out.fault_addr = r.value;
            }
            return out;
        }
        case Op::LdrAuth: {
            const AuthResult r = pac.auth(in.key, c.get_sp(in.rn), 0);
            if (!r.ok) {
                stats.auth_fail.fetch_add(1, std::memory_order_relaxed);
                out.status = Emu::AuthFault;
                out.fault_addr = r.value + int64_t(in.imm);
                return out;  // pc not advanced: the access faults
            }
            const uint64_t addr = r.value + int64_t(in.imm);
            uint64_t v;
            std::memcpy(&v, reinterpret_cast<const void*>(addr), 8);
            c.set(in.rd, v);
            if (in.writeback) c.set_sp(in.rn, addr);
            break;
        }
        case Op::Cas: {
            void* p = reinterpret_cast<void*>(c.get_sp(in.rn));
            if (reinterpret_cast<uint64_t>(p) & ((1u << in.size) - 1)) {
                out.status = Emu::Misaligned;
                out.fault_addr = reinterpret_cast<uint64_t>(p);
                return out;
            }
            const uint64_t m = size_mask(in.size);
            const uint64_t old =
                pick_cas(in.size, in.acquire, in.release)(p, c.get(in.rs) & m, c.get(in.rd) & m);
            c.set(in.rs, old);
            break;
        }
        case Op::Casp: {
            void* p = reinterpret_cast<void*>(c.get_sp(in.rn));
            const uint64_t al = in.size == 3 ? 15 : 7;
            if (reinterpret_cast<uint64_t>(p) & al) {
                out.status = Emu::Misaligned;
                out.fault_addr = reinterpret_cast<uint64_t>(p);
                return out;
            }
            if (in.size == 2) {  // W pair == one 64-bit location, first register in the low half
                const uint64_t e = (c.get(in.rs) & 0xFFFFFFFFull) | (c.get(in.rs + 1) << 32);
                const uint64_t n = (c.get(in.rd) & 0xFFFFFFFFull) | (c.get(in.rd + 1) << 32);
                const uint64_t old = pick_cas(3, in.acquire, in.release)(p, e, n);
                c.set(in.rs, old & 0xFFFFFFFFull);
                c.set(in.rs + 1, old >> 32);
            } else {
                uint64_t o0, o1;
                const uint64_t e0 = c.get(in.rs), e1 = c.get(in.rs + 1), n0 = c.get(in.rd), n1 = c.get(in.rd + 1);
                auto f = in.acquire ? (in.release ? casp_11 : casp_10) : (in.release ? casp_01 : casp_00);
                f(p, e0, e1, n0, n1, &o0, &o1);
                c.set(in.rs, o0);
                c.set(in.rs + 1, o1);
            }
            break;
        }
        case Op::Rmw: {
            void* p = reinterpret_cast<void*>(c.get_sp(in.rn));
            if (reinterpret_cast<uint64_t>(p) & ((1u << in.size) - 1)) {
                out.status = Emu::Misaligned;
                out.fault_addr = reinterpret_cast<uint64_t>(p);
                return out;
            }
            stats.by_rmw[static_cast<int>(in.rmw)].fetch_add(1, std::memory_order_relaxed);
            const uint64_t m = size_mask(in.size), val = c.get(in.rs) & m;
            const CasFn cas = pick_cas(in.size, in.acquire, in.release);
            uint64_t old;
            switch (in.size) {  // initial relaxed read, the CAS below is the architected access
                case 0: old = *static_cast<volatile uint8_t*>(p); break;
                case 1: old = *static_cast<volatile uint16_t*>(p); break;
                case 2: old = *static_cast<volatile uint32_t*>(p); break;
                default: old = *static_cast<volatile uint64_t*>(p); break;
            }
            for (;;) {
                const uint64_t seen = cas(p, old, rmw_value(in.rmw, in.size, old, val));
                if (seen == old) break;
                old = seen;
            }
            c.set(in.rd, old);  // Rt == XZR discards (ST* aliases)
            break;
        }
        case Op::Mrs:
            c.set(in.rd, c.tpidrro);
            break;
        case Op::Ldapur: {
            // Acquire load (LDAR-class on this host): RCsc is stronger than the RCpc the original has.
            const uint64_t a = c.get_sp(in.rn) + int64_t(in.imm);
            uint64_t v = 0;
            switch (in.size) {
                case 0: v = __atomic_load_n(reinterpret_cast<uint8_t*>(a), __ATOMIC_ACQUIRE); break;
                case 1: v = __atomic_load_n(reinterpret_cast<uint16_t*>(a), __ATOMIC_ACQUIRE); break;
                case 2: v = __atomic_load_n(reinterpret_cast<uint32_t*>(a), __ATOMIC_ACQUIRE); break;
                default: v = __atomic_load_n(reinterpret_cast<uint64_t*>(a), __ATOMIC_ACQUIRE); break;
            }
            if (in.sext) {
                v = uint64_t(sext(v, in.size));
                if (in.sext == 2) v &= 0xFFFFFFFFull;
            }
            c.set(in.rd, v);
            break;
        }
        case Op::Svc:
            out.svc = true;
            out.svc_x16 = int64_t(c.x[16]);
            out.svc_imm = uint32_t(in.imm);
            c.x[0] = kDarwinENOSYS;  // Darwin convention: carry set, x0 = errno
            c.pstate |= kPstateC;
            break;
        default:
            out.status = Emu::Bad;
            return out;
    }
    c.pc += 4;
    return out;
}

// ---- signal handlers --------------------------------------------------------------------
namespace {

TrapRuntime* g_rt = nullptr;

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) {
        ssize_t r = write(2, buf, size_t(n < int(sizeof buf) ? n : int(sizeof buf) - 1));
        (void)r;
    }
}

std::string where(uint64_t pc) {
    if (g_rt && g_rt->describe) return g_rt->describe(pc, g_rt->ctx);
    return "?";
}

void die_default(int sig) {  // restore default action; the faulting instruction re-executes
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, nullptr);
}

void load_state(CpuState& s, const ucontext_t* uc) {
    for (int i = 0; i < 31; ++i) s.x[i] = uc->uc_mcontext.regs[i];
    s.sp = uc->uc_mcontext.sp;
    s.pc = uc->uc_mcontext.pc;
    s.pstate = uc->uc_mcontext.pstate;
}

void store_state(const CpuState& s, ucontext_t* uc) {
    for (int i = 0; i < 31; ++i) uc->uc_mcontext.regs[i] = s.x[i];
    uc->uc_mcontext.sp = s.sp;
    uc->uc_mcontext.pc = s.pc;
    uc->uc_mcontext.pstate = s.pstate;
}

enum class TrapRes { NotBrk, NotLowered, Done };

// Emulate the lowered instruction under a SIGTRAP.  NotBrk/NotLowered leave the context untouched
// (the BRK is the guest's own); Done means handled (or a terminal fault was raised).
TrapRes try_trap(ucontext_t* uc, uint32_t* idx_out) {
    const uint64_t pc = uc->uc_mcontext.pc;
    const uint32_t word = *reinterpret_cast<const uint32_t*>(pc);
    if ((word & 0xFFE0001F) != 0xD4200000) return TrapRes::NotBrk;
    const uint32_t idx = (word >> 5) & 0xFFFF;
    if (idx_out) *idx_out = idx;
    const TrapEntry* e = trap_table().find(idx, pc);
    Insn in;
    if (!e || !decode(e->word, in)) return TrapRes::NotLowered;
    CpuState cs;
    load_state(cs, uc);
    if (g_rt->tsd) cs.tpidrro = g_rt->tsd(g_rt->ctx);
    const uint64_t x0_in = cs.x[0], pstate_in = cs.pstate;
    const EmuOutcome o = emulate(in, cs, *g_rt->pac, g_rt->stats);
    switch (o.status) {
        case Emu::Ok:
            if (o.svc && g_rt->svc) {
                cs.x[0] = x0_in;  // emulate() preset ENOSYS; the hook sees the guest's original x0 and flags
                cs.pstate = pstate_in;
                g_rt->svc(cs, o.svc_imm, g_rt->ctx);
            }
            else if (o.svc && g_rt->log_svc)
                say("ds-exec: Darwin syscall x16=%lld svc #0x%x at pc=0x%llx (%s) -> ENOSYS(%llu), carry set\n",
                    (long long)o.svc_x16, o.svc_imm, (unsigned long long)pc, where(pc).c_str(),
                    (unsigned long long)kDarwinENOSYS);
            store_state(cs, uc);
            if (g_rt->after_trap) g_rt->after_trap(g_rt->ctx);
            return TrapRes::Done;
        case Emu::AuthFault:
            say("ds-exec: PAC authentication failure at pc=0x%llx (%s) word=0x%08x: poisoned 0x%llx\n",
                (unsigned long long)pc, where(pc).c_str(), e->word, (unsigned long long)o.fault_addr);
            if (in.op == Op::LdrAuth) {  // access through the poisoned address faults like hardware
                die_default(SIGSEGV);
                raise(SIGSEGV);
                return TrapRes::Done;
            }
            store_state(cs, uc);  // branch to the poisoned pc: the fetch faults, reported by on_segv
            return TrapRes::Done;
        case Emu::Misaligned:
            say("ds-exec: misaligned atomic at pc=0x%llx (%s) word=0x%08x addr=0x%llx\n",
                (unsigned long long)pc, where(pc).c_str(), e->word, (unsigned long long)o.fault_addr);
            die_default(SIGBUS);
            raise(SIGBUS);
            return TrapRes::Done;
        default:
            say("ds-exec: cannot emulate word 0x%08x at pc=0x%llx\n", e->word, (unsigned long long)pc);
            die_default(SIGTRAP);
            return TrapRes::Done;
    }
}

void on_trap(int sig, siginfo_t*, void* vuc) {
    auto* uc = static_cast<ucontext_t*>(vuc);
    const uint64_t pc = uc->uc_mcontext.pc;
    uint32_t idx = 0;
    switch (try_trap(uc, &idx)) {
        case TrapRes::NotBrk:
            say("ds-exec: SIGTRAP at pc=0x%llx word=0x%08x (%s) is not a BRK\n", (unsigned long long)pc,
                *reinterpret_cast<const uint32_t*>(pc), where(pc).c_str());
            die_default(sig);
            return;
        case TrapRes::NotLowered:
            say("ds-exec: unhandled BRK #%u at pc=0x%llx (%s): not a lowered instruction\n", idx, (unsigned long long)pc, where(pc).c_str());
            die_default(sig);
            return;
        default:
            return;
    }
}

void on_ill(int sig, siginfo_t*, void* vuc) {
    auto* uc = static_cast<ucontext_t*>(vuc);
    const uint64_t pc = uc->uc_mcontext.pc;
    const uint32_t word = *reinterpret_cast<const uint32_t*>(pc);
    say("ds-exec: SIGILL at pc=0x%llx word=0x%08x (%s)%s\n", (unsigned long long)pc, word, where(pc).c_str(),
        is_known_unsupported(word) ? " [FEAT_LRCPC LDAPR: recognised, not lowered]" : "");
    die_default(sig);
}

void on_segv(int sig, siginfo_t* si, void* vuc) {
    auto* uc = static_cast<ucontext_t*>(vuc);
    const uint64_t pc = uc->uc_mcontext.pc, addr = reinterpret_cast<uint64_t>(si->si_addr);
    if (si->si_code == SEGV_ACCERR && addr == pc && g_rt->lazy_lower) {
        std::string err;
        if (g_rt->lazy_lower(pc, g_rt->ctx, &err)) {
            g_rt->stats.lazy_pages.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!err.empty()) say("ds-exec: lazy lowering failed: %s\n", err.c_str());
    }
    const bool noncanon = !g_rt->pac->canonical(addr);
    say("ds-exec: SIGSEGV code=%d addr=0x%llx pc=0x%llx (%s)%s\n", si->si_code, (unsigned long long)addr,
        (unsigned long long)pc, where(pc).c_str(),
        noncanon ? " [non-canonical address: use of a poisoned (failed-auth) pointer?]" : "");
    die_default(sig);
}

struct AltStack {
    void* mem = nullptr;
    ~AltStack() {
        if (!mem) return;
        stack_t ss{};
        ss.ss_flags = SS_DISABLE;
        sigaltstack(&ss, nullptr);
        munmap(mem, kSize);
    }
    static constexpr size_t kSize = 256 * 1024;
};

}  // namespace

bool handle_sigtrap(void* vuc) { return try_trap(static_cast<ucontext_t*>(vuc), nullptr) == TrapRes::Done; }

void thread_init() {
    thread_local AltStack alt;
    if (alt.mem) return;
    void* m = mmap(nullptr, AltStack::kSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) return;
    stack_t ss{};
    ss.ss_sp = m;
    ss.ss_size = AltStack::kSize;
    if (sigaltstack(&ss, nullptr) != 0) {
        munmap(m, AltStack::kSize);
        return;
    }
    alt.mem = m;
}

bool install_trap_handlers(TrapRuntime* rt) {
    g_rt = rt;
    thread_init();
    struct { int sig; void (*fn)(int, siginfo_t*, void*); } hs[] = {
        {SIGTRAP, on_trap}, {SIGILL, on_ill}, {SIGSEGV, on_segv}};
    for (auto& h : hs) {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = h.fn;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        if (sigaction(h.sig, &sa, nullptr) != 0) return false;
    }
    return true;
}

}  // namespace ds::exec
