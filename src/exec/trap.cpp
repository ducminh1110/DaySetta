#include <algorithm>
#include "trap.hpp"
#include "trampoline.hpp"

#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#if defined(__aarch64__)
#include <asm/sigcontext.h>
#endif
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace ds::exec {

namespace {

#if defined(__aarch64__)
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

#else  // !__aarch64__: portable fallback (x86_64 etc.) so the emulation semantics stay testable off-target.
// Same contract as the LL/SC versions: returns the value observed in memory.  CASP uses a mutex (no 128-bit
// atomic without cx16/libatomic); it is atomic only against other CASP emulation, which is all tests need.
#define DS_CAS(NAME, T)                                                                    \
    inline uint64_t NAME(void* p, uint64_t exp, uint64_t nv) {                             \
        T e = T(exp);                                                                      \
        __atomic_compare_exchange_n(static_cast<T*>(p), &e, T(nv), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
        return e;                                                                          \
    }
DS_CAS(cas8_00, uint8_t) DS_CAS(cas8_10, uint8_t) DS_CAS(cas8_01, uint8_t) DS_CAS(cas8_11, uint8_t)
DS_CAS(cas16_00, uint16_t) DS_CAS(cas16_10, uint16_t) DS_CAS(cas16_01, uint16_t) DS_CAS(cas16_11, uint16_t)
DS_CAS(cas32_00, uint32_t) DS_CAS(cas32_10, uint32_t) DS_CAS(cas32_01, uint32_t) DS_CAS(cas32_11, uint32_t)
DS_CAS(cas64_00, uint64_t) DS_CAS(cas64_10, uint64_t) DS_CAS(cas64_01, uint64_t) DS_CAS(cas64_11, uint64_t)

inline void casp_any(void* p, uint64_t e0, uint64_t e1, uint64_t n0, uint64_t n1, uint64_t* o0, uint64_t* o1) {
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    auto* q = static_cast<uint64_t*>(p);
    *o0 = q[0];
    *o1 = q[1];
    if (*o0 == e0 && *o1 == e1) { q[0] = n0; q[1] = n1; }
}
inline void casp_00(void* p, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t* x, uint64_t* y) { casp_any(p, a, b, c, d, x, y); }
inline void casp_10(void* p, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t* x, uint64_t* y) { casp_any(p, a, b, c, d, x, y); }
inline void casp_01(void* p, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t* x, uint64_t* y) { casp_any(p, a, b, c, d, x, y); }
inline void casp_11(void* p, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t* x, uint64_t* y) { casp_any(p, a, b, c, d, x, y); }
#endif

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

// Arm ARM (2026-03) shared/functions/cpa/PointerCheckAtEL.
uint64_t pointer_check(const CpuState& c, uint64_t result, uint64_t base, bool cptm_detected) {
    const bool prev = ((base >> 55) & 1) != ((base >> 54) & 1);
    const bool cpta_detected = (result >> 56) != (base >> 56) || prev;
    if ((cpta_detected && c.cpta) || (cptm_detected && c.cptm)) {
        result = (result & ((uint64_t(1) << 55) - 1)) | (base & ~((uint64_t(1) << 55) - 1));  // rv[63:55] = base[63:55]
        result = (result & ~(uint64_t(1) << 54)) | ((~(result >> 55) & 1) << 54);              // rv[54] = NOT rv[55]
    }
    return result;
}

}  // namespace

namespace {
struct PlainRange { std::atomic<uint64_t> start{0}, end{0}; };
constexpr int kPlainMax = 64;
PlainRange g_plain[kPlainMax];
std::atomic<int> g_plain_n{0};
}  // namespace

void plain_range_add(uint64_t start, uint64_t size) {
    const int i = g_plain_n.fetch_add(1);
    if (i >= kPlainMax) { g_plain_n.store(kPlainMax); return; }
    g_plain[i].start.store(start);
    g_plain[i].end.store(start + size);
}

bool plain_range_contains(uint64_t a) {
    const int n = std::min(g_plain_n.load(), kPlainMax);
    for (int i = 0; i < n; ++i)
        if (a >= g_plain[i].start.load() && a < g_plain[i].end.load()) return true;
    return false;
}

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
            {   // debug spin sampler (env MLDR_DSEXEC_SPIN, off by default)
                static std::atomic<uint64_t> n{0};
                static const bool spin = getenv("MLDR_DSEXEC_SPIN") != nullptr;
                if (spin && n.fetch_add(1, std::memory_order_relaxed) % 200000 == 199999)
                    fprintf(stderr, "ds-exec: br-auth spin sample at pc=0x%llx -> 0x%llx lr=0x%llx\n", (unsigned long long)c.pc,
                            (unsigned long long)r.value, (unsigned long long)c.x[30]);
            }
            if (!r.ok) {
                const uint64_t raw = c.get(in.rn), bare = pac.strip(raw);
                if (raw == bare && plain_range_contains(bare)) {  // cross-ABI rule: unsigned pointer into a plain-arm64 image
                    c.pc = bare;
                    return out;
                }
            }
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
            {   // DEBUG watch (MLDR_DSEXEC_WATCH=<hex addr>)
                static const uint64_t w = getenv("MLDR_DSEXEC_WATCH") ? strtoull(getenv("MLDR_DSEXEC_WATCH"), nullptr, 16) : 0;
                if (w && (reinterpret_cast<uint64_t>(p) & ~7ull) == (w & ~7ull))
                    fprintf(stderr, "ds-exec: CAS %p exp=0x%llx new=0x%llx old=0x%llx pc=0x%llx tpidrro=0x%llx\n", p,
                            (unsigned long long)(c.get(in.rs) & m), (unsigned long long)(c.get(in.rd) & m), (unsigned long long)old,
                            (unsigned long long)c.pc, (unsigned long long)c.tpidrro);
            }
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
        case Op::Cpa: {
            uint64_t r, base, ovf = 0;
            if (in.mul) {  // MADDPT/MSUBPT: X[] (no SP) operands, 128-bit signed product
                base = c.get(in.rs);
                int64_t p;
                ovf = __builtin_mul_overflow(int64_t(c.get(in.rn)), int64_t(c.get(in.rm)), &p);  // SInt product != SInt(product[63:0])
                const uint64_t lo = uint64_t(p);
                r = in.sub ? base - lo : base + lo;
            } else {
                base = c.get_sp(in.rn);
                const uint64_t off = c.get(in.rm) << in.imm;
                r = in.sub ? base - off : base + off;
            }
            r = pointer_check(c, r, base, ovf);
            if (in.mul) c.set(in.rd, r); else c.set_sp(in.rd, r);
            break;
        }
        case Op::Mrs:
            c.set(in.rd, c.tpidrro);
            break;
        case Op::MrsTpidr:
            c.set(in.rd, 0);
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
        case Op::Dot: {  // UDOT/SDOT: each 32-bit lane += sum of four byte products
            if (!c.v) { out.status = Emu::Bad; break; }
            uint8_t n[16], m[16];
            uint32_t d[4];
            std::memcpy(n, c.v[in.rn], 16);
            std::memcpy(m, c.v[in.rm], 16);
            std::memcpy(d, c.v[in.rd], 16);
            const int lanes = in.q ? 4 : 2;
            for (int e = 0; e < 4; ++e) {
                if (e >= lanes) { d[e] = 0; continue; }
                const int mb = in.lane >= 0 ? in.lane * 4 : e * 4;
                int32_t acc = 0;
                for (int k = 0; k < 4; ++k)
                    acc += in.sgn ? int32_t(int8_t(n[e * 4 + k])) * int8_t(m[mb + k]) : int32_t(n[e * 4 + k]) * m[mb + k];
                d[e] += uint32_t(acc);
            }
            std::memcpy(c.v[in.rd], d, 16);
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

#if defined(__aarch64__)
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
    s.v = nullptr;  // fpsimd_context record in the signal frame (written back by the kernel on sigreturn)
    for (size_t off = 0; off + sizeof(_aarch64_ctx) <= sizeof(uc->uc_mcontext.__reserved);) {
        auto* h = reinterpret_cast<const _aarch64_ctx*>(uc->uc_mcontext.__reserved + off);
        if (h->magic == 0 || h->size == 0) break;
        if (h->magic == FPSIMD_MAGIC) {
            s.v = reinterpret_cast<uint8_t (*)[16]>(const_cast<fpsimd_context*>(reinterpret_cast<const fpsimd_context*>(h))->vregs);
            break;
        }
        off += h->size;
    }
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
TrapRes try_trap(ucontext_t* uc, uint32_t* idx_out, uint64_t tsd = 0) {
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
    cs.tpidrro = g_rt->tsd ? g_rt->tsd(g_rt->ctx) : tsd;
    const uint64_t x0_in = cs.x[0], pstate_in = cs.pstate;
    g_rt->stats.brk_traps.fetch_add(1, std::memory_order_relaxed);
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
            if (getenv("MLDR_DSEXEC_REGS"))  // debug, off by default
                say("ds-exec:   lr=0x%llx x0=0x%llx x1=0x%llx x6=0x%llx x8=0x%llx x16=0x%llx x17=0x%llx\n", (unsigned long long)cs.x[30], (unsigned long long)cs.x[0], (unsigned long long)cs.x[1], (unsigned long long)cs.x[6], (unsigned long long)cs.x[8], (unsigned long long)cs.x[16], (unsigned long long)cs.x[17]);
            if (getenv("MLDR_DSEXEC_BT")) {  // debug: frame-pointer walk
                const uint64_t* fp = reinterpret_cast<const uint64_t*>(cs.x[29]);
                for (int i = 0; i < 12 && fp && reinterpret_cast<uint64_t>(fp) > 0x10000; ++i) {
                    fprintf(stderr, "ds-exec:   bt[%d] ra=0x%llx\n", i, (unsigned long long)(fp[1] & 0xFFFFFFFFFull));
                    const uint64_t* n = reinterpret_cast<const uint64_t*>(fp[0]);
                    if (n <= fp) break;
                    fp = n;
                }
            }
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

// Trampoline entry (see trampoline.hpp): same emulation as the SIGTRAP path, on a frame built by the island's common block.
void tramp_entry(uint64_t* f, const SiteInfo* si) {
    CpuState cs;
    for (int i = 0; i < 31; ++i) cs.x[i] = f[kFrameX + i];
    cs.sp = f[kFrameSp];
    cs.pc = si->pc;
    cs.pstate = f[kFrameNzcv];
    cs.tpidrro = ((si->in.op == Op::Mrs || si->in.op == Op::MrsTpidr) && g_rt->tsd) ? g_rt->tsd(g_rt->ctx) : 0;
    g_rt->stats.tramp_calls.fetch_add(1, std::memory_order_relaxed);
    const EmuOutcome o = emulate(si->in, cs, *g_rt->pac, g_rt->stats);
    switch (o.status) {
        case Emu::Ok:
            break;
        case Emu::AuthFault:
            say("ds-exec: PAC authentication failure at pc=0x%llx (%s) word=0x%08x: poisoned 0x%llx\n",
                (unsigned long long)si->pc, where(si->pc).c_str(), si->word, (unsigned long long)o.fault_addr);
            if (getenv("MLDR_DSEXEC_REGS"))
                say("ds-exec:   lr=0x%llx x0=0x%llx x1=0x%llx x6=0x%llx x8=0x%llx x16=0x%llx x17=0x%llx\n", (unsigned long long)cs.x[30],
                    (unsigned long long)cs.x[0], (unsigned long long)cs.x[1], (unsigned long long)cs.x[6],
                    (unsigned long long)cs.x[8], (unsigned long long)cs.x[16], (unsigned long long)cs.x[17]);
            if (si->in.op == Op::LdrAuth) {  // access through the poisoned address faults like hardware
                die_default(SIGSEGV);
                raise(SIGSEGV);
            }
            break;  // branch to the poisoned pc: the fetch faults, reported by on_segv
        case Emu::Misaligned:
            say("ds-exec: misaligned atomic at pc=0x%llx (%s) word=0x%08x addr=0x%llx\n", (unsigned long long)si->pc,
                where(si->pc).c_str(), si->word, (unsigned long long)o.fault_addr);
            die_default(SIGBUS);
            raise(SIGBUS);
            break;
        default:
            say("ds-exec: cannot emulate word 0x%08x at pc=0x%llx\n", si->word, (unsigned long long)si->pc);
            die_default(SIGTRAP);
            raise(SIGTRAP);
            break;
    }
    for (int i = 0; i < 31; ++i) f[kFrameX + i] = cs.x[i];
    f[kFrameNext] = cs.pc;
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

bool handle_sigtrap(void* vuc, uint64_t tsd) { return try_trap(static_cast<ucontext_t*>(vuc), nullptr, tsd) == TrapRes::Done; }

bool handle_sigsegv(void* vuc, void* vsi) {
    auto* uc = static_cast<ucontext_t*>(vuc);
    auto* si = static_cast<siginfo_t*>(vsi);
    if (!g_rt) return false;
    const uint64_t pc = uc->uc_mcontext.pc;
    if (g_rt->lazy_slide && si->si_code == SEGV_ACCERR && g_rt->lazy_slide(reinterpret_cast<uint64_t>(si->si_addr), g_rt->ctx)) return true;
    if (getenv("DS_EXEC_TRACE_STRIP") && !g_rt->pac->canonical(pc))
        say("ds-exec: segv tid=%d code=%d addr=0x%llx pc=0x%llx sp=0x%llx\n", (int)gettid(), si->si_code,
            (unsigned long long)reinterpret_cast<uint64_t>(si->si_addr), (unsigned long long)pc, (unsigned long long)uc->uc_mcontext.sp);
    // Plain-arm64 callee (e.g. Darling's libsystem_kernel) reached through a pointer an arm64e caller signed:
    // real arm64e hardware would fault the same way for a plain `blr`, so the Darling layer has no PAC.  Drop the
    // signature (no authentication) and run the target.  Only fires for a fetch from a non-canonical address.
    if (reinterpret_cast<uint64_t>(si->si_addr) == pc && !g_rt->pac->canonical(pc)) {
        const uint64_t t = g_rt->pac->strip(pc);
        if (t != pc && g_rt->pac->canonical(t)) {
            if (getenv("DS_EXEC_TRACE_STRIP"))
                say("ds-exec: fetch from signed pointer 0x%llx -> 0x%llx lr=0x%llx\n", (unsigned long long)pc,
                    (unsigned long long)t, (unsigned long long)uc->uc_mcontext.regs[30]);
            uc->uc_mcontext.pc = t;
            g_rt->stats.by_op[static_cast<int>(Op::PacStrip)].fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    if (!g_rt->lazy_lower) return false;
    if (si->si_code != SEGV_ACCERR || reinterpret_cast<uint64_t>(si->si_addr) != pc) return false;
    std::string err;
    if (!g_rt->lazy_lower(pc, g_rt->ctx, &err)) {
        if (!err.empty()) say("ds-exec: lazy lowering failed: %s\n", err.c_str());
        return false;
    }
    g_rt->stats.lazy_pages.fetch_add(1, std::memory_order_relaxed);
    return true;
}

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
    trampoline_set_handler(&tramp_entry);
    trampoline_set_pac(rt->pac);
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

#else  // !__aarch64__: guest traps need aarch64 ucontext; ds-exec only decodes/lowers/emulates (no live execution).
bool handle_sigtrap(void*, uint64_t) { return false; }
bool handle_sigsegv(void*, void*) { return false; }
void thread_init() {}
bool install_trap_handlers(TrapRuntime*) { return false; }
#endif

}  // namespace ds::exec
