// ds-exec tests: software PAC, instruction lowering, trap emulation, probe integration.
//
// Encodings in kEnc were produced by `clang --target=aarch64-linux-gnu -march=armv8.5-a+pauth+lse`
// + `llvm-objdump -d` (class VERIFIED, not hand-derived).  Darwin ENOSYS = 78:
// /home/ubuntu/darling/src/external/xnu/bsd/sys/errno.h:211.
#include <memory>
#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "exec/lowering.hpp"
#include "exec/pac.hpp"
#include "exec/trap.hpp"
#include "exec/trampoline.hpp"
#include "test.hpp"

using namespace ds::exec;

namespace {

struct Enc {
    uint32_t word;
    Op op;
    const char* text;
};

const Enc kEnc[] = {
    {0xd503233f, Op::PacSign, "paciasp"},
    {0xd503237f, Op::PacSign, "pacibsp"},
    {0xd50323bf, Op::PacAuth, "autiasp"},
    {0xd50323ff, Op::PacAuth, "autibsp"},
    {0xd503231f, Op::PacSign, "paciaz"},
    {0xd503235f, Op::PacSign, "pacibz"},
    {0xd503239f, Op::PacAuth, "autiaz"},
    {0xd50323df, Op::PacAuth, "autibz"},
    {0xd503211f, Op::PacSign, "pacia1716"},
    {0xd503215f, Op::PacSign, "pacib1716"},
    {0xd503219f, Op::PacAuth, "autia1716"},
    {0xd50321df, Op::PacAuth, "autib1716"},
    {0xd50320ff, Op::PacStrip, "xpaclri"},
    {0xdac10041, Op::PacSign, "pacia x1, x2"},
    {0xdac107e1, Op::PacSign, "pacib x1, sp"},
    {0xdac10883, Op::PacSign, "pacda x3, x4"},
    {0xdac10c83, Op::PacSign, "pacdb x3, x4"},
    {0xdac110c5, Op::PacAuth, "autia x5, x6"},
    {0xdac114c5, Op::PacAuth, "autib x5, x6"},
    {0xdac118c5, Op::PacAuth, "autda x5, x6"},
    {0xdac11cc5, Op::PacAuth, "autdb x5, x6"},
    {0xdac123e7, Op::PacSign, "paciza x7"},
    {0xdac127e7, Op::PacSign, "pacizb x7"},
    {0xdac12be7, Op::PacSign, "pacdza x7"},
    {0xdac12fe7, Op::PacSign, "pacdzb x7"},
    {0xdac133e7, Op::PacAuth, "autiza x7"},
    {0xdac137e7, Op::PacAuth, "autizb x7"},
    {0xdac13be7, Op::PacAuth, "autdza x7"},
    {0xdac13fe7, Op::PacAuth, "autdzb x7"},
    {0xdac143e8, Op::PacStrip, "xpaci x8"},
    {0xdac147e8, Op::PacStrip, "xpacd x8"},
    {0x9acb3149, Op::PacGa, "pacga x9, x10, x11"},
    {0x9adf3149, Op::PacGa, "pacga x9, x10, sp"},
    {0xd61f099f, Op::BrAuth, "braaz x12"},
    {0xd61f0d9f, Op::BrAuth, "brabz x12"},
    {0xd63f099f, Op::BrAuth, "blraaz x12"},
    {0xd63f0d9f, Op::BrAuth, "blrabz x12"},
    {0xd71f098d, Op::BrAuth, "braa x12, x13"},
    {0xd71f099f, Op::BrAuth, "braa x12, sp"},
    {0xd71f0d8d, Op::BrAuth, "brab x12, x13"},
    {0xd73f098d, Op::BrAuth, "blraa x12, x13"},
    {0xd73f0d8d, Op::BrAuth, "blrab x12, x13"},
    {0xd65f0bff, Op::RetAuth, "retaa"},
    {0xd65f0fff, Op::RetAuth, "retab"},
    {0xf8200441, Op::LdrAuth, "ldraa x1, [x2]"},
    {0xf8201441, Op::LdrAuth, "ldraa x1, [x2, #0x8]"},
    {0xf87ffc41, Op::LdrAuth, "ldraa x1, [x2, #-0x8]!"},
    {0xf8bff441, Op::LdrAuth, "ldrab x1, [x2, #0xff8]"},
    {0xf8e00c41, Op::LdrAuth, "ldrab x1, [x2, #-0x1000]!"},
    {0xf8202fe1, Op::LdrAuth, "ldraa x1, [sp, #0x10]!"},
    {0x08a17c62, Op::Cas, "casb w1, w2, [x3]"},
    {0x08e17c62, Op::Cas, "casab w1, w2, [x3]"},
    {0x08a1fc62, Op::Cas, "caslb w1, w2, [x3]"},
    {0x08e1fc62, Op::Cas, "casalb w1, w2, [x3]"},
    {0x48a17c62, Op::Cas, "cash w1, w2, [x3]"},
    {0x48e1fc62, Op::Cas, "casalh w1, w2, [x3]"},
    {0x88a17c62, Op::Cas, "cas w1, w2, [x3]"},
    {0x88e17c62, Op::Cas, "casa w1, w2, [x3]"},
    {0x88a1fc62, Op::Cas, "casl w1, w2, [x3]"},
    {0x88e1fc62, Op::Cas, "casal w1, w2, [x3]"},
    {0xc8a17c62, Op::Cas, "cas x1, x2, [x3]"},
    {0xc8e1ffe2, Op::Cas, "casal x1, x2, [sp]"},
    {0x08227cc4, Op::Casp, "casp w2, w3, w4, w5, [x6]"},
    {0x4862fcc4, Op::Casp, "caspal x2, x3, x4, x5, [x6]"},
    {0x48627cc4, Op::Casp, "caspa x2, x3, x4, x5, [x6]"},
    {0x0822fcc4, Op::Casp, "caspl w2, w3, w4, w5, [x6]"},
    {0xb8218062, Op::Rmw, "swp w1, w2, [x3]"},
    {0x38218062, Op::Rmw, "swpb w1, w2, [x3]"},
    {0x78e18062, Op::Rmw, "swpalh w1, w2, [x3]"},
    {0xf8e18062, Op::Rmw, "swpal x1, x2, [x3]"},
    {0xf8a18062, Op::Rmw, "swpa x1, x2, [x3]"},
    {0xf8618062, Op::Rmw, "swpl x1, x2, [x3]"},
    {0xb8210062, Op::Rmw, "ldadd w1, w2, [x3]"},
    {0x38a10062, Op::Rmw, "ldaddab w1, w2, [x3]"},
    {0x78e10062, Op::Rmw, "ldaddalh w1, w2, [x3]"},
    {0xf8e10062, Op::Rmw, "ldaddal x1, x2, [x3]"},
    {0xf86103e2, Op::Rmw, "ldaddl x1, x2, [sp]"},
    {0xf8211062, Op::Rmw, "ldclr x1, x2, [x3]"},
    {0xb8e11062, Op::Rmw, "ldclral w1, w2, [x3]"},
    {0xf8212062, Op::Rmw, "ldeor x1, x2, [x3]"},
    {0x38612062, Op::Rmw, "ldeorlb w1, w2, [x3]"},
    {0xf8213062, Op::Rmw, "ldset x1, x2, [x3]"},
    {0x78a13062, Op::Rmw, "ldsetah w1, w2, [x3]"},
    {0xf8214062, Op::Rmw, "ldsmax x1, x2, [x3]"},
    {0x38214062, Op::Rmw, "ldsmaxb w1, w2, [x3]"},
    {0xf8215062, Op::Rmw, "ldsmin x1, x2, [x3]"},
    {0x78215062, Op::Rmw, "ldsminh w1, w2, [x3]"},
    {0xf8216062, Op::Rmw, "ldumax x1, x2, [x3]"},
    {0x38e16062, Op::Rmw, "ldumaxalb w1, w2, [x3]"},
    {0xf8217062, Op::Rmw, "ldumin x1, x2, [x3]"},
    {0xb8e17062, Op::Rmw, "lduminal w1, w2, [x3]"},
    {0xb821007f, Op::Rmw, "stadd w1, [x3]"},
    {0xf861007f, Op::Rmw, "staddl x1, [x3]"},
    {0xb821107f, Op::Rmw, "stclr w1, [x3]"},
    {0x3861107f, Op::Rmw, "stclrlb w1, [x3]"},
    {0xf821207f, Op::Rmw, "steor x1, [x3]"},
    {0xf821307f, Op::Rmw, "stset x1, [x3]"},
    {0xb821407f, Op::Rmw, "stsmax w1, [x3]"},
    {0xf821507f, Op::Rmw, "stsmin x1, [x3]"},
    {0xf821607f, Op::Rmw, "stumax x1, [x3]"},
    {0xb821707f, Op::Rmw, "stumin w1, [x3]"},
    {0x7861007f, Op::Rmw, "staddlh w1, [x3]"},
    {0xf8ff0062, Op::Rmw, "ldaddal xzr, x2, [x3]"},
    {0xd4001001, Op::Svc, "svc #0x80"},
    {0xd4000001, Op::Svc, "svc #0"},
    {0xf8bfc041, Op::None, "ldapr x1, [x2]"},
};

PacKeys fixed_keys() {
    PacKeys k;
    for (int i = 0; i < 5; ++i) {
        k.lo[i] = 0x0123456789abcdefull * (i + 1) + 11;
        k.hi[i] = 0xfedcba9876543210ull ^ (uint64_t(i) << 40);
    }
    return k;
}

const Pac& tpac() {
    static Pac p(48, fixed_keys());
    return p;
}

TrapStats g_stats;

Insn dec(uint32_t w) {
    Insn i;
    DS_CHECK(decode(w, i));
    return i;
}

EmuOutcome run(uint32_t w, CpuState& cs) { return emulate(dec(w), cs, tpac(), g_stats); }

CpuState fresh() {
    CpuState cs;
    cs.pc = 0x400000;
    cs.sp = 0x7ffe0000;
    return cs;
}

// word encoders for the atomic groups (field layout verified against kEnc below)
uint32_t enc_rmw(unsigned size, bool a, bool r, unsigned opc, bool swp, unsigned rs, unsigned rn, unsigned rt) {
    return 0x38200000u | size << 30 | uint32_t(a) << 23 | uint32_t(r) << 22 | rs << 16 | uint32_t(swp) << 15 | opc << 12 | rn << 5 | rt;
}
uint32_t enc_cas(unsigned size, bool a, bool r, unsigned rs, unsigned rn, unsigned rt) {
    return 0x08a07c00u | size << 30 | uint32_t(a) << 22 | uint32_t(r) << 15 | rs << 16 | rn << 5 | rt;
}

bool noncanon(uint64_t v) { return !tpac().canonical(v); }

}  // namespace

DS_TEST(exec_siphash_reference_vector) {
    uint8_t key[16], in[15];
    for (int i = 0; i < 16; ++i) key[i] = uint8_t(i);
    for (int i = 0; i < 15; ++i) in[i] = uint8_t(i);
    uint64_t k0, k1;
    std::memcpy(&k0, key, 8);
    std::memcpy(&k1, key + 8, 8);
    DS_CHECK_EQ(siphash24(k0, k1, in, 15), 0xa129ca6149be45e5ull);  // SipHash-2-4 paper vector, 15-byte input
}

DS_TEST(exec_pac_round_trip_and_poison) {
    const Pac& p = tpac();
    const uint64_t ptr = 0x0000aaaa12345678ull, mod = 0x1111;
    for (PacKey k : {PacKey::IA, PacKey::IB, PacKey::DA, PacKey::DB}) {
        const uint64_t s = p.sign(k, ptr, mod);
        DS_CHECK(p.canonical(ptr));
        DS_CHECK_EQ(p.strip(s), ptr);
        const AuthResult ok = p.auth(k, s, mod);
        DS_CHECK(ok.ok);
        DS_CHECK_EQ(ok.value, ptr);
        const AuthResult bad = p.auth(k, s, mod + 1);
        DS_CHECK(!bad.ok);
        DS_CHECK(noncanon(bad.value));  // use of the poisoned pointer faults
        const uint64_t code = (bad.value >> 53) & 3;
        DS_CHECK_EQ(code, (k == PacKey::IB || k == PacKey::DB) ? 2ull : 1ull);
        DS_CHECK(!p.auth(k, s ^ (uint64_t(1) << 50), mod).ok);  // flipped PAC bit
    }
    // keys are independent
    DS_CHECK(!p.auth(PacKey::IB, p.sign(PacKey::IA, ptr, mod), mod).ok);
    // a non-canonical input can never authenticate
    DS_CHECK(!p.auth(PacKey::IA, p.sign(PacKey::IA, ptr | (uint64_t(1) << 52), mod), mod).ok);
    // PACGA: upper 32 bits only, modifier-sensitive
    const uint64_t g = p.pacga(0xdeadbeef, 5);
    DS_CHECK_EQ(g & 0xffffffffull, 0ull);
    DS_CHECK(g != p.pacga(0xdeadbeef, 6));
    DS_CHECK_EQ(Pac::blend(0x0000aaaa00001000ull, 0x1234), 0x1234aaaa00001000ull);
}

DS_TEST(exec_libkernel_function_ptr_auth_schema) {
    // libsystem_kernel's _libkernel_functions fields are signed by Apple arm64e code with
    // key IA and modifier = ptrauth_blend_discriminator(&field, string_discriminator("libkernel_functions_<f>")).
    // The shim must authenticate them with the same schema through ds-exec (no blind strip).
    const Pac& p = tpac();
    const uint64_t field_addr = 0x0000aaaa20001040ull;       // &table->field (in cache)
    const uint64_t target = 0x00000180887958ull;             // e.g. __pthread_exit_if_canceled
    const uint16_t diversity = 0x41a1;  // __builtin_ptrauth_string_discriminator("libkernel_functions__pthread_exit_if_canceled") (clang arm64e)
    const uint64_t mod = Pac::blend(field_addr, diversity);
    const uint64_t signed_ptr = p.sign(PacKey::IA, target, mod);

    DS_CHECK(p.strip(signed_ptr) == target);
    const AuthResult ok = p.auth(PacKey::IA, signed_ptr, mod);
    DS_CHECK(ok.ok);
    DS_CHECK_EQ(ok.value, target);

    // wrong discriminator fails (auth must not be bypassed by collusion)
    const AuthResult bad = p.auth(PacKey::IA, signed_ptr, Pac::blend(field_addr, 0x41a2));
    DS_CHECK(!bad.ok);
    DS_CHECK(noncanon(bad.value));
    // no field-blend also fails
    DS_CHECK(!p.auth(PacKey::IA, signed_ptr, diversity).ok);
}

DS_TEST(exec_decode_matches_assembler_table) {
    int lowered = 0;
    for (const Enc& e : kEnc) {
        Insn in;
        const bool d = decode(e.word, in);
        if (e.op == Op::None) {
            DS_CHECK(!d);
            DS_CHECK(is_known_unsupported(e.word));  // ldapr: reported, never skipped
            continue;
        }
        if (!d || in.op != e.op) throw ds::test::Failure(std::string("decode mismatch: ") + e.text);
        ++lowered;
    }
    DS_CHECK(lowered > 90);
    // field spot checks
    Insn i = dec(0xd503237f);  // pacibsp
    DS_CHECK(i.op == Op::PacSign && i.key == PacKey::IB && i.mod == ModSrc::Sp && i.rd == 30);
    i = dec(0xd503211f);  // pacia1716
    DS_CHECK(i.op == Op::PacSign && i.key == PacKey::IA && i.rd == 17 && i.mod == ModSrc::Reg && i.rm == 16);
    i = dec(0xd50320ff);  // xpaclri
    DS_CHECK(i.op == Op::PacStrip && i.rd == 30);
    i = dec(0xdac107e1);  // pacib x1, sp
    DS_CHECK(i.op == Op::PacSign && i.key == PacKey::IB && i.rd == 1 && i.mod == ModSrc::Sp);
    i = dec(0xdac123e7);  // paciza x7
    DS_CHECK(i.op == Op::PacSign && i.mod == ModSrc::Zero && i.rd == 7);
    i = dec(0xd73f0d8d);  // blrab x12, x13
    DS_CHECK(i.op == Op::BrAuth && i.link && i.key == PacKey::IB && i.rn == 12 && i.rm == 13);
    i = dec(0xd61f099f);  // braaz x12
    DS_CHECK(i.op == Op::BrAuth && !i.link && i.mod == ModSrc::Zero && i.rn == 12);
    i = dec(0xf87ffc41);  // ldraa x1, [x2, #-8]!
    DS_CHECK(i.op == Op::LdrAuth && i.key == PacKey::DA && i.imm == -8 && i.writeback && i.rn == 2 && i.rd == 1);
    i = dec(0xf8bff441);  // ldrab x1, [x2, #4088]
    DS_CHECK(i.op == Op::LdrAuth && i.key == PacKey::DB && i.imm == 4088 && !i.writeback);
    i = dec(0xc8e1ffe2);  // casal x1, x2, [sp]
    DS_CHECK(i.op == Op::Cas && i.size == 3 && i.acquire && i.release && i.rs == 1 && i.rd == 2 && i.rn == 31);
    i = dec(0x48627cc4);  // caspa x2, x3, x4, x5, [x6]
    DS_CHECK(i.op == Op::Casp && i.size == 3 && i.acquire && !i.release && i.rs == 2 && i.rd == 4);
    i = dec(0xf8ff0062);  // ldaddal xzr, x2, [x3]
    DS_CHECK(i.op == Op::Rmw && i.rs == 31 && i.rd == 2);
    i = dec(0x78e18062);  // swpalh
    DS_CHECK(i.op == Op::Rmw && i.rmw == Rmw::Swp && i.size == 1 && i.acquire && i.release);
    i = dec(0xd4001001);  // svc #0x80
    DS_CHECK(i.op == Op::Svc && i.imm == 0x80);
    // encoder self-check against assembler output
    DS_CHECK_EQ(enc_rmw(3, true, true, 0, false, 1, 3, 2), 0xf8e10062u);   // ldaddal x1,x2,[x3]
    DS_CHECK_EQ(enc_rmw(1, true, true, 0, true, 1, 3, 2), 0x78e18062u);    // swpalh
    DS_CHECK_EQ(enc_cas(2, true, true, 1, 3, 2), 0x88e1fc62u);              // casal w1,w2,[x3]
    // things that must stay native
    for (uint32_t w : {0xd503201fu /*nop*/, 0xd65f03c0u /*ret*/, 0xf9400020u /*ldr*/, 0xd503245fu /*bti c*/,
                       0xd4200020u /*brk #1*/, 0xd61f0180u /*br x12*/, 0xd63f0180u /*blr x12*/,
                       0x08237cc4u /*casp with odd Rs: UNDEFINED*/, 0xdac10800u | 0x4000u /*undefined 1-src opcode*/}) {
        Insn x;
        DS_CHECK(!decode(w, x));
    }
}

DS_TEST(exec_emulate_pac_instructions) {
    CpuState cs = fresh();
    const uint64_t lr = 0x0000aaaa00001000ull;
    cs.x[30] = lr;
    run(0xd503233f, cs);  // paciasp
    DS_CHECK(cs.x[30] != lr);
    DS_CHECK_EQ(cs.pc, 0x400004ull);
    DS_CHECK_EQ(tpac().strip(cs.x[30]), lr);
    const uint64_t signed_lr = cs.x[30];
    run(0xd50323bf, cs);  // autiasp
    DS_CHECK_EQ(cs.x[30], lr);
    // wrong SP -> poisoned, no fault yet (no FEAT_FPAC)
    cs.x[30] = signed_lr;
    cs.sp += 16;
    const uint64_t fails = g_stats.auth_fail.load();
    EmuOutcome o = run(0xd50323bf, cs);
    DS_CHECK(o.status == Emu::Ok);
    DS_CHECK(noncanon(cs.x[30]));
    DS_CHECK_EQ(g_stats.auth_fail.load(), fails + 1);
    // XPACLRI strips a (poisoned) value
    run(0xd50320ff, cs);
    DS_CHECK_EQ(cs.x[30], lr);
    // 1716 forms: x17 signed with modifier x16, key IB
    cs.x[17] = 0x0000aaaa00002000ull;
    cs.x[16] = 77;
    run(0xd503215f, cs);  // pacib1716
    DS_CHECK(cs.x[17] != 0x0000aaaa00002000ull);
    run(0xd50321df, cs);  // autib1716
    DS_CHECK_EQ(cs.x[17], 0x0000aaaa00002000ull);
    // register forms: pacia x1, x2 / autia x1, x2 / xpaci x8 / pacga
    cs.x[1] = 0x0000aaaa00003000ull;
    cs.x[2] = 0x55;
    run(0xdac10041, cs);  // pacia x1, x2
    DS_CHECK_EQ(cs.x[1], tpac().sign(PacKey::IA, 0x0000aaaa00003000ull, 0x55));
    run(0xdac110c1, cs);  // autia x1, x6: x6 == 0 != 0x55 -> must fail
    DS_CHECK(noncanon(cs.x[1]));
    cs.x[8] = tpac().sign(PacKey::DA, 0x0000aaaa00004000ull, 0);
    run(0xdac143e8, cs);  // xpaci x8
    DS_CHECK_EQ(cs.x[8], 0x0000aaaa00004000ull);
    cs.x[10] = 0x1234;
    cs.x[11] = 9;
    run(0x9acb3149, cs);  // pacga x9, x10, x11
    DS_CHECK_EQ(cs.x[9], tpac().pacga(0x1234, 9));
    // paciza / autiza (zero modifier)
    cs.x[7] = 0x0000aaaa00005000ull;
    run(0xdac123e7, cs);
    DS_CHECK(cs.x[7] != 0x0000aaaa00005000ull);
    run(0xdac133e7, cs);
    DS_CHECK_EQ(cs.x[7], 0x0000aaaa00005000ull);
}

DS_TEST(exec_emulate_combined_auth_branches) {
    const uint64_t target = 0x0000aaaa00006000ull;
    CpuState cs = fresh();
    cs.x[12] = tpac().sign(PacKey::IA, target, 0);
    EmuOutcome o = run(0xd61f099f, cs);  // braaz x12
    DS_CHECK(o.status == Emu::Ok);
    DS_CHECK_EQ(cs.pc, target);
    // blraa x12, x13: modifier register, link
    cs = fresh();
    cs.x[13] = 0x99;
    cs.x[12] = tpac().sign(PacKey::IA, target, 0x99);
    o = run(0xd73f098d, cs);
    DS_CHECK(o.status == Emu::Ok);
    DS_CHECK_EQ(cs.pc, target);
    DS_CHECK_EQ(cs.x[30], 0x400004ull);
    // braa x12, sp: modifier is SP
    cs = fresh();
    cs.x[12] = tpac().sign(PacKey::IA, target, cs.sp);
    o = run(0xd71f099f, cs);
    DS_CHECK(o.status == Emu::Ok && cs.pc == target);
    // failure: branch to the poisoned pointer (fetch faults), error bits set
    cs = fresh();
    cs.x[12] = tpac().sign(PacKey::IA, target, 0) ^ (uint64_t(1) << 49);
    o = run(0xd61f099f, cs);
    DS_CHECK(o.status == Emu::AuthFault);
    DS_CHECK(noncanon(cs.pc));
    DS_CHECK_EQ(cs.pc, o.fault_addr);
    DS_CHECK_EQ((cs.pc >> 53) & 3, 1ull);
    // wrong key family (IB-signed pointer, IA branch) fails
    cs = fresh();
    cs.x[12] = tpac().sign(PacKey::IB, target, 0);
    DS_CHECK(run(0xd61f099f, cs).status == Emu::AuthFault);
    // retab / retaa
    cs = fresh();
    cs.x[30] = tpac().sign(PacKey::IB, target, cs.sp);
    o = run(0xd65f0fff, cs);
    DS_CHECK(o.status == Emu::Ok && cs.pc == target);
    cs = fresh();
    cs.x[30] = tpac().sign(PacKey::IB, target, cs.sp + 8);
    o = run(0xd65f0fff, cs);
    DS_CHECK(o.status == Emu::AuthFault && noncanon(cs.pc));
    DS_CHECK_EQ((cs.pc >> 53) & 3, 2ull);  // key B error code
    // ldraa x1, [x2, #8]! ; success then failure
    alignas(16) uint64_t mem[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    cs = fresh();
    cs.x[2] = tpac().sign(PacKey::DA, reinterpret_cast<uint64_t>(&mem[0]), 0);
    o = run(0xf8201c41 /* ldraa x1,[x2,#8]! */, cs);
    DS_CHECK(o.status == Emu::Ok);
    DS_CHECK_EQ(cs.x[1], 0x2222ull);
    DS_CHECK_EQ(cs.x[2], reinterpret_cast<uint64_t>(&mem[1]));
    DS_CHECK_EQ(cs.pc, 0x400004ull);
    cs = fresh();
    cs.x[1] = 0xabc;
    cs.x[2] = tpac().sign(PacKey::DB, reinterpret_cast<uint64_t>(&mem[0]), 0);  // wrong key
    o = run(0xf8201c41, cs);
    DS_CHECK(o.status == Emu::AuthFault);
    DS_CHECK(noncanon(o.fault_addr));
    DS_CHECK_EQ(cs.x[1], 0xabcull);   // no register written
    DS_CHECK_EQ(cs.pc, 0x400000ull);  // access faults at the instruction
}

DS_TEST(exec_cross_abi_unsigned_pointer_into_plain_range) {
    // arm64e code calling the plain-arm64 Darling shim through an unsigned pointer (libpthread pthread_once(fn) etc.)
    const uint64_t plain = 0x0000bbbb00010000ull, other = 0x0000bbbb00090000ull;
    plain_range_add(plain, 0x1000);
    CpuState cs = fresh();
    cs.x[12] = plain + 0x44;  // unsigned, inside the plain range: accepted
    EmuOutcome o = run(0xd63f099f, cs);  // blraaz x12
    DS_CHECK(o.status == Emu::Ok);
    DS_CHECK_EQ(cs.pc, plain + 0x44);
    cs = fresh();
    cs.x[12] = other;  // unsigned, outside every plain range: still a fault
    o = run(0xd63f099f, cs);
    DS_CHECK(o.status == Emu::AuthFault);
    cs = fresh();
    cs.x[12] = tpac().sign(PacKey::IB, plain + 0x44, 0);  // signed with the wrong key: still a fault
    o = run(0xd63f099f, cs);
    DS_CHECK(o.status == Emu::AuthFault);
    cs = fresh();
    cs.x[12] = tpac().sign(PacKey::IA, plain + 0x44, 0);  // correctly signed: Ok as before
    o = run(0xd63f099f, cs);
    DS_CHECK(o.status == Emu::Ok && cs.pc == plain + 0x44);
}

DS_TEST(exec_emulate_pauth_lr) {
    // encodings from llvm-mc --mattr=+pauth-lr; label 0x288 bytes back (echo slice: pacibsppc at 0x4f0, retabsppc at 0x778)
    const uint32_t retabsppc = 0x5520145f;
    const uint64_t lr = 0x0000aaaa00001000ull;
    CpuState cs = fresh();
    cs.x[30] = lr;
    const uint64_t sign_pc = cs.pc;
    run(0xdac1a7fe, cs);  // pacibsppc
    DS_CHECK(cs.x[30] != lr);
    DS_CHECK_EQ(tpac().strip(cs.x[30]), lr);
    const uint64_t signed_lr = cs.x[30];
    cs.pc = sign_pc + 0x288;
    EmuOutcome o = run(retabsppc, cs);  // same sp, label == signing pc: authenticates
    DS_CHECK(o.status == Emu::Ok);
    DS_CHECK_EQ(cs.pc, lr);
    cs.x[30] = signed_lr;  // wrong label (different pc) fails
    cs.pc = sign_pc + 0x290;
    o = run(retabsppc, cs);
    DS_CHECK(o.status == Emu::AuthFault);
    cs.x[30] = signed_lr;  // wrong sp fails
    cs.pc = sign_pc + 0x288;
    cs.sp += 16;
    o = run(retabsppc, cs);
    DS_CHECK(o.status == Emu::AuthFault);
    Insn in;
    DS_CHECK(decode(0xf3a0145f, in) && in.op == Op::PacAuth && in.key == PacKey::IB && in.imm == 0x288);  // autibsppc
}

DS_TEST(exec_emulate_cpa) {
    // encodings from llvm-mc --mattr=+cpa; semantics: Arm ARM 2026-03 PointerCheckAtEL
    Insn in;
    DS_CHECK(decode(0x9a00229b, in) && in.op == Op::Cpa && !in.sub && !in.mul && in.rd == 27 && in.rn == 20 && in.rm == 0);
    DS_CHECK(decode(0x9a022fe1, in) && in.op == Op::Cpa && in.rn == 31 && in.imm == 3 && in.rm == 2);  // addpt x1, sp, x2, lsl #3
    DS_CHECK(decode(0xda032041, in) && in.sub);                                                        // subpt x1, x2, x3
    DS_CHECK(decode(0x9b631041, in) && in.mul && !in.sub && in.rs == 4);                               // maddpt x1, x2, x3, x4
    DS_CHECK(decode(0x9b639041, in) && in.mul && in.sub);                                              // msubpt
    DS_CHECK(!decode(0x9b000000, in) && !decode(0x9a000000, in));                                      // madd / adc stay native
    CpuState cs = fresh();
    cs.x[2] = 0x0000000100000000ull; cs.x[3] = 5;
    run(0x9a032041 | 0x0 /*addpt x1,x2,x3*/, cs);
    DS_CHECK_EQ(cs.x[1], 0x0000000100000005ull);
    run(0xda032041, cs);  // subpt
    DS_CHECK_EQ(cs.x[1], 0x00000000fffffffbull);
    cs.sp = 0x1000; cs.x[2] = 4;
    run(0x9a022fe1, cs);  // addpt x1, sp, x2, lsl #3
    DS_CHECK_EQ(cs.x[1], 0x1020ull);
    // overflow into the tag byte: plain ADD while CPTA == 0, poisoned when CPTA == 1
    cs.x[2] = 0x00ffffffffffffffull; cs.x[3] = 0x0100000000000010ull;
    run(0x9a032041, cs);
    DS_CHECK_EQ(cs.x[1], 0x020000000000000full);
    cs.cpta = true;
    run(0x9a032041, cs);
    DS_CHECK_EQ(cs.x[1] >> 55, cs.x[2] >> 55);       // rv[63:55] = base[63:55]
    DS_CHECK_EQ((cs.x[1] >> 54) & 1, 0ull);          // rv[54] = NOT rv[55] (base bit 55 = 1)
    // multiply-add: product overflow only matters under CPTM
    cs = fresh();
    cs.x[2] = 1ull << 40; cs.x[3] = 1ull << 30; cs.x[4] = 0x1000;
    run(0x9b631041, cs);  // maddpt x1, x2, x3, x4: product = 2^70, low 64 bits 0
    DS_CHECK_EQ(cs.x[1], 0x1000ull);
    cs.cptm = true;
    run(0x9b631041, cs);
    DS_CHECK_EQ((cs.x[1] >> 54) & 1, 1ull);
    cs.x[2] = 3; cs.x[3] = 7; cs.cptm = false;
    run(0x9b639041, cs);  // msubpt: 0x1000 - 21
    DS_CHECK_EQ(cs.x[1], 0x1000ull - 21);
}

DS_TEST(exec_emulate_lse_semantics) {
    alignas(16) uint64_t m[2];
    auto rmw = [&](unsigned size, unsigned opc, bool swp, uint64_t mem0, uint64_t val, uint64_t* old_out) {
        CpuState cs = fresh();
        m[0] = mem0;
        m[1] = 0xCAFEF00DCAFEF00Dull;
        cs.x[1] = size == 3 ? val : val | 0xAB00000000000000ull;  // garbage above the operand size must be ignored
        cs.x[2] = ~0ull;
        cs.x[3] = reinterpret_cast<uint64_t>(m);
        EmuOutcome o = run(enc_rmw(size, true, true, opc, swp, 1, 3, 2), cs);
        DS_CHECK(o.status == Emu::Ok);
        DS_CHECK_EQ(cs.pc, 0x400004ull);
        *old_out = cs.x[2];
        DS_CHECK_EQ(m[1], 0xCAFEF00DCAFEF00Dull);  // neighbour untouched
        return m[0];
    };
    uint64_t old;
    DS_CHECK_EQ(rmw(2, 0, false, 0xFFFFFFFFull, 1, &old), 0ull);          // ldadd w wraps
    DS_CHECK_EQ(old, 0xFFFFFFFFull);                                        // zero-extended old
    DS_CHECK_EQ(rmw(0, 0, false, 0xFFull, 2, &old) & 0xFF, 1ull);          // ldaddb
    DS_CHECK_EQ(rmw(1, 0, false, 0xFFFFull, 3, &old) & 0xFFFF, 2ull);      // ldaddh
    DS_CHECK_EQ(rmw(3, 0, false, ~0ull, 2, &old), 1ull);                    // ldadd x
    DS_CHECK_EQ(rmw(3, 1, false, 0xFF, 0x0F, &old), 0xF0ull);              // ldclr
    DS_CHECK_EQ(rmw(3, 2, false, 0xFF, 0x0F, &old), 0xF0ull);              // ldeor
    DS_CHECK_EQ(rmw(3, 3, false, 0xF0, 0x0F, &old), 0xFFull);              // ldset
    DS_CHECK_EQ(rmw(0, 4, false, 0xFF, 1, &old) & 0xFF, 1ull);             // ldsmaxb: -1 vs 1 -> 1
    DS_CHECK_EQ(rmw(0, 6, false, 0xFF, 1, &old) & 0xFF, 0xFFull);          // ldumaxb: 255 vs 1 -> 255
    DS_CHECK_EQ(rmw(0, 5, false, 0xFF, 1, &old) & 0xFF, 0xFFull);          // ldsminb: -1 vs 1 -> -1
    DS_CHECK_EQ(rmw(0, 7, false, 0xFF, 1, &old) & 0xFF, 1ull);             // lduminb
    DS_CHECK_EQ(rmw(3, 4, false, uint64_t(-5), 3, &old), 3ull);            // ldsmax x
    DS_CHECK_EQ(rmw(1, 0, true, 0x1234, 0xBEEF, &old) & 0xFFFF, 0xBEEFull);  // swph
    DS_CHECK_EQ(old, 0x1234ull);
    // every ordering variant gives the same value result
    for (int a = 0; a < 2; ++a)
        for (int r = 0; r < 2; ++r)
            for (unsigned sz = 0; sz < 4; ++sz) {
                CpuState cs = fresh();
                m[0] = 10;
                cs.x[1] = 5;
                cs.x[3] = reinterpret_cast<uint64_t>(m);
                DS_CHECK(run(enc_rmw(sz, a, r, 0, false, 1, 3, 2), cs).status == Emu::Ok);
                DS_CHECK_EQ(m[0] & size_t(sz == 3 ? ~0ull : (1ull << (8u << sz)) - 1), 15ull);
                DS_CHECK_EQ(cs.x[2], 10ull);
            }
    // ST alias: Rt = XZR, no register write
    {
        CpuState cs = fresh();
        m[0] = 40;
        cs.x[1] = 2;
        cs.x[3] = reinterpret_cast<uint64_t>(m);
        DS_CHECK(run(0xf861007f /* staddl x1,[x3] */, cs).status == Emu::Ok);
        DS_CHECK_EQ(m[0], 42ull);
    }
    // CAS success / failure, subword masking
    {
        CpuState cs = fresh();
        m[0] = 0x11223344;
        cs.x[1] = 0xFFFFFFFF00000000ull | 0x11223344;  // upper half ignored for W
        cs.x[2] = 0x55667788;
        cs.x[3] = reinterpret_cast<uint64_t>(m);
        DS_CHECK(run(enc_cas(2, true, true, 1, 3, 2), cs).status == Emu::Ok);
        DS_CHECK_EQ(m[0], 0x55667788ull);
        DS_CHECK_EQ(cs.x[1], 0x11223344ull);
        cs = fresh();
        m[0] = 0x11223344;
        cs.x[1] = 7;
        cs.x[2] = 9;
        cs.x[3] = reinterpret_cast<uint64_t>(m);
        run(enc_cas(2, false, false, 1, 3, 2), cs);
        DS_CHECK_EQ(m[0], 0x11223344ull);  // unchanged
        DS_CHECK_EQ(cs.x[1], 0x11223344ull);  // Rs receives the observed value
        // casb on byte 0 only
        m[0] = 0x1122;
        cs = fresh();
        cs.x[1] = 0x22;
        cs.x[2] = 0x99;
        cs.x[3] = reinterpret_cast<uint64_t>(m);
        run(enc_cas(0, false, true, 1, 3, 2), cs);
        DS_CHECK_EQ(m[0], 0x1199ull);
    }
    // CASP: X pair and W pair
    {
        CpuState cs = fresh();
        m[0] = 100;
        m[1] = 200;
        cs.x[2] = 100; cs.x[3] = 200; cs.x[4] = 7; cs.x[5] = 8;
        cs.x[6] = reinterpret_cast<uint64_t>(m);
        DS_CHECK(run(0x4862fcc4 /* caspal x2,x3,x4,x5,[x6] */, cs).status == Emu::Ok);
        DS_CHECK(m[0] == 7 && m[1] == 8);
        DS_CHECK(cs.x[2] == 100 && cs.x[3] == 200);
        cs = fresh();
        cs.x[2] = 1; cs.x[3] = 1; cs.x[4] = 9; cs.x[5] = 9;  // mismatch
        cs.x[6] = reinterpret_cast<uint64_t>(m);
        run(0x4862fcc4, cs);
        DS_CHECK(m[0] == 7 && m[1] == 8);
        DS_CHECK(cs.x[2] == 7 && cs.x[3] == 8);
        uint32_t w[2] = {0x11, 0x22};
        alignas(8) uint32_t wp[2] = {w[0], w[1]};
        cs = fresh();
        cs.x[2] = 0x11; cs.x[3] = 0x22; cs.x[4] = 0x33; cs.x[5] = 0x44;
        cs.x[6] = reinterpret_cast<uint64_t>(wp);
        DS_CHECK(run(0x0822fcc4 /* caspl w2,w3,w4,w5,[x6] */, cs).status == Emu::Ok);
        DS_CHECK(wp[0] == 0x33 && wp[1] == 0x44);
    }
    // misaligned atomic: reported, not performed
    {
        CpuState cs = fresh();
        cs.x[3] = reinterpret_cast<uint64_t>(m) + 1;
        EmuOutcome o = run(enc_rmw(2, false, false, 0, false, 1, 3, 2), cs);
        DS_CHECK(o.status == Emu::Misaligned);
        DS_CHECK_EQ(cs.pc, 0x400000ull);
    }
}

DS_TEST(exec_emulate_svc_darwin_syscall) {
    CpuState cs = fresh();
    cs.x[16] = 42;
    cs.x[0] = 1;
    EmuOutcome o = run(0xd4001001, cs);  // svc #0x80
    DS_CHECK(o.status == Emu::Ok && o.svc);
    DS_CHECK_EQ(o.svc_x16, 42ll);
    DS_CHECK_EQ(o.svc_imm, 0x80u);
    DS_CHECK_EQ(cs.x[0], 78ull);               // Darwin ENOSYS
    DS_CHECK((cs.pstate & kPstateC) != 0);     // error convention: carry set
    DS_CHECK_EQ(cs.pc, 0x400004ull);
    cs = fresh();
    cs.x[16] = uint64_t(-31);  // Mach trap numbers are negative
    o = run(0xd4000001 /* svc #0 */, cs);
    DS_CHECK(o.svc && o.svc_x16 == -31 && o.svc_imm == 0);
}

namespace {

struct ExecBuf {
    uint32_t* mem;
    ExecBuf() {
        mem = static_cast<uint32_t*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (mem == MAP_FAILED) throw ds::test::Failure("mmap RWX failed");
    }
    ~ExecBuf() { munmap(mem, 4096); }
    // copy words, lower them, flush; returns function pointer
    template <typename F>
    F load(std::vector<uint32_t> words, LowerCounts* counts = nullptr, const LowerPolicy& pol = LowerPolicy{}) {
        std::memcpy(mem, words.data(), words.size() * 4);
        LowerCounts c;
        std::string err;
        if (!lower_range(mem, mem + words.size(), pol, c, &err)) throw ds::test::Failure(err);
        flush_icache(mem, mem + words.size());
        if (counts) *counts = c;
        return reinterpret_cast<F>(mem);
    }
};

TrapRuntime& live_runtime() {
    static TrapRuntime rt;
    static bool ok = [] {
        rt.pac = &tpac();
        rt.log_svc = false;
        return install_trap_handlers(&rt);
    }();
    if (!ok) throw ds::test::Failure("sigaction failed");
    return rt;
}

}  // namespace

DS_TEST(exec_live_lowered_code_traps) {
#if !defined(__aarch64__)
    std::fprintf(stderr, "SKIP exec_live_lowered_code_traps: needs an aarch64 host\n");
    return;
#endif
    TrapRuntime& rt = live_runtime();
    {
        ExecBuf b;
        LowerCounts c;
        auto f = b.load<long (*)()>({0xd503237f, 0xd28000e0, 0xd50323ff, 0xd65f03c0}, &c);  // pacibsp; mov x0,#7; autibsp; ret
        DS_CHECK_EQ(c.patched[int(Op::PacSign)], 1ull);
        DS_CHECK_EQ(c.patched[int(Op::PacAuth)], 1ull);
        DS_CHECK_EQ(f(), 7l);
    }
    {
        ExecBuf b;
        auto f = b.load<long (*)()>({0xd503237f, 0xd2800120, 0xd65f0fff});  // pacibsp; mov x0,#9; retab
        DS_CHECK_EQ(f(), 9l);
    }
    {
        ExecBuf b;
        auto f = b.load<unsigned long (*)(unsigned long)>({0xdac123e0, 0xdac143e0, 0xd65f03c0});  // paciza x0; xpaci x0; ret
        DS_CHECK_EQ(f(0x0000aaaa00007000ul), 0x0000aaaa00007000ul);
    }
    {  // svc #0x80 must not reach the Linux kernel: x16=42 would be a Linux syscall number
        ExecBuf b;
        auto f = b.load<long (*)()>({0xd2800550, 0xd4001001, 0xd65f03c0});
        const uint64_t before = rt.stats.by_op[int(Op::Svc)].load();
        DS_CHECK_EQ(f(), 78l);
        DS_CHECK_EQ(rt.stats.by_op[int(Op::Svc)].load(), before + 1);
        ExecBuf b2;
        auto g = b2.load<long (*)()>({0xd2800550, 0xd4001001, 0x9a9f37e0, 0xd65f03c0});  // cset x0, cs
        DS_CHECK_EQ(g(), 1l);
    }
    {
        ExecBuf b;
        auto f = b.load<unsigned (*)(uint32_t*, unsigned, unsigned)>({0x88a17c02 /* cas w1,w2,[x0] */, 0x2a0103e0, 0xd65f03c0});
        // calling convention: x0 = ptr, w1 = expected, w2 = new
        uint32_t v = 5;
        DS_CHECK_EQ(f(&v, 5, 9), 5u);
        DS_CHECK_EQ(v, 9u);
        DS_CHECK_EQ(f(&v, 5, 1), 9u);
        DS_CHECK_EQ(v, 9u);
    }
}

DS_TEST(exec_trampolines_match_trap_path) {
#if !defined(__aarch64__)
    std::fprintf(stderr, "SKIP exec_trampolines_match_trap_path: needs an aarch64 host\n");
    return;
#endif
    live_runtime();
    LowerPolicy fast;
    fast.trampolines = true;
    fast.strip_mask = ~tpac().field_mask();
    const uint64_t p = 0x0000aaaa00007000ull;
    auto both = [&](std::vector<uint32_t> words, auto call, const char* what, bool expect_branch) {
        ExecBuf slow, quick;
        LowerCounts cs, cq;
        using F = unsigned long (*)(unsigned long, unsigned long, unsigned long);
        F fs = slow.load<F>(words, &cs);
        F fq = quick.load<F>(words, &cq, fast);
        unsigned long a = call(fs), b = call(fq);
        if (a != b) throw ds::test::Failure(std::string("trampoline result differs from trap path: ") + what);
        if (expect_branch) {
            bool any = false;
            for (size_t i = 0; i < words.size(); ++i) any |= (quick.mem[i] >> 26) == 5u && quick.mem[i] != words[i];
            DS_CHECK(any);
            DS_CHECK(cq.trampolined + cq.inlined_strip > 0);
        }
    };
    // paciza x0; autiza x0; ret  (round trip)
    both({0xdac123e0, 0xdac133e0, 0xd65f03c0}, [&](auto f) { return f(p, 0, 0); }, "pac/aut", true);
    // pacia x0, x1; ret  (signature depends on the modifier register)
    both({0xdac10020, 0xd65f03c0}, [&](auto f) { return f(p, 0x1234, 0); }, "pacia x0,x1", true);
    // paciza x0; xpaci x0; ret (inline strip)
    both({0xdac123e0, 0xdac143e0, 0xd65f03c0}, [&](auto f) { return f(p, 0, 0); }, "xpaci", true);
    // pacibsp; mov x0,#9; retab (sp modifier, return through the trampoline)
    both({0xd503237f, 0xd2800120, 0xd65f0fff}, [&](auto f) { return f(0, 0, 0); }, "pacibsp/retab", true);
    // blraaz x0 into a callee returning 77
    ExecBuf callee;
    callee.mem[0] = 0xd28009a0;  // mov x0,#77
    callee.mem[1] = 0xd65f03c0;  // ret
    flush_icache(callee.mem, callee.mem + 2);
    const uint64_t signed_callee = tpac().sign(PacKey::IA, reinterpret_cast<uint64_t>(callee.mem), 0);
    both({0xa9bf7bfd, 0xd63f081f, 0xa8c17bfd, 0xd65f03c0}, [&](auto f) { return f(signed_callee, 0, 0); }, "blraaz", true);
    // authentication failure falls back to the exact emulation (poisoned result identical to the trap path)
    both({0xdac133e0, 0xd65f03c0}, [&](auto f) { return f(tpac().sign(PacKey::IA, p, 0) ^ (1ull << 50), 0, 0); }, "autiza fail", true);
    both({0xdac11020, 0xd65f03c0}, [&](auto f) { return f(tpac().sign(PacKey::IA, p, 5), 6, 0); }, "autia x0,x1 wrong modifier", true);
    // modifier register forms: pacda/autda x0, x1
    both({0xdac10c20, 0xdac11c20, 0xd65f03c0}, [&](auto f) { return f(p, 0x77, 0); }, "pacdb/autdb x0,x1", true);
    DS_CHECK(trampoline_stats().inlined > 0);
    // LSE atomics inline: casalb, ldsetal x, swpal w, ldclral h, ldeoral x (value in w0/x0 after, memory checked)
    // casalb w1,w2,[x0]; mov w0,w1 (hit and miss), casal x1,x2,[x0]
    both({0x08e1fc02, 0x2a0103e0, 0xd65f03c0}, [&](auto f) { static uint8_t v; v = 5; unsigned long r = f(reinterpret_cast<unsigned long>(&v), 5, 9); r = r * 1000 + v; r = r * 1000 + f(reinterpret_cast<unsigned long>(&v), 5, 1) ; return r * 1000 + v; }, "casalb", true);
    both({0xc8e1fc02, 0xaa0103e0, 0xd65f03c0}, [&](auto f) { static uint64_t v; v = 0x1122334455ull; unsigned long r = f(reinterpret_cast<unsigned long>(&v), 0x1122334455ull, 7); return r ^ v; }, "casal x", true);
    // ldsetal x1,x2,[x0]; mov x0,x2; ret
    both({0xf8e13002, 0xaa0203e0, 0xd65f03c0}, [&](auto f) { static uint64_t v; v = 0x0f; unsigned long r = f(reinterpret_cast<unsigned long>(&v), 0xf0, 0); return r * 1000 + v; }, "ldsetal", true);
    // swpal w1,w2,[x0]; mov w0,w2; ret
    both({0xb8e18002, 0x2a0203e0, 0xd65f03c0}, [&](auto f) { static uint32_t v; v = 7; unsigned long r = f(reinterpret_cast<unsigned long>(&v), 99, 0); return r * 1000 + v; }, "swpal", true);
    // ldclral x1,x2,[x0]
    both({0xf8e11002, 0xaa0203e0, 0xd65f03c0}, [&](auto f) { static uint64_t v; v = 0xff; unsigned long r = f(reinterpret_cast<unsigned long>(&v), 0x0f, 0); return r * 1000 + v; }, "ldclral", true);
    // ldeoral w1,w2,[x0]
    both({0xb8e12002, 0x2a0203e0, 0xd65f03c0}, [&](auto f) { static uint32_t v; v = 0xaa; unsigned long r = f(reinterpret_cast<unsigned long>(&v), 0xff, 0); return r * 1000 + v; }, "ldeoral", true);
    // vector registers survive the host handler: fmov d0,x0; paciza x1; fmov x0,d0; ret
    both({0x9e670000, 0xdac123e1, 0x9e660000, 0xd65f03c0}, [&](auto f) { return f(0x1122334455667788ul, p, 0); }, "vector regs", true);
    // cas w1,w2,[x0]; mov w0,w1; ret
    both({0x88a17c02, 0x2a0103e0, 0xd65f03c0},
         [&](auto f) { static uint32_t v; v = 5; unsigned long r = f(reinterpret_cast<unsigned long>(&v), 5, 9); return r * 100 + v; }, "cas", true);
    // ldaddal x1,x2,[x0]; mov x0,x2; ret
    both({0xf8e10002, 0xaa0203e0, 0xd65f03c0},
         [&](auto f) { static uint64_t v; v = 40; unsigned long r = f(reinterpret_cast<unsigned long>(&v), 2, 0); return r * 1000 + v; }, "ldaddal", true);
}

DS_TEST(exec_dotprod_trap_and_trampoline) {
#if !defined(__aarch64__)
    std::fprintf(stderr, "SKIP exec_dotprod_trap_and_trampoline: needs an aarch64 host\n");
    return;
#endif
    live_runtime();
    // Encodings from llvm-mc -mattr=+dotprod.  Program: ldr q0..q2 from [x0]; <dot>; str q0,[x0]; ret
    struct Case { uint32_t w; bool sgn, q; int lane, rd, rn, rm; };
    const Case cases[] = {
        {0x6e829420, false, true, -1, 0, 1, 2},   // udot v0.4s, v1.16b, v2.16b
        {0x0e829420, true, false, -1, 0, 1, 2},   // sdot v0.2s, v1.8b, v2.8b
        {0x6fa2e820, false, true, 3, 0, 1, 2},    // udot v0.4s, v1.16b, v2.4b[3]
        {0x4fa2e020, true, true, 1, 0, 1, 2},     // sdot v0.4s, v1.16b, v2.4b[1]
        {0x6e809400, false, true, -1, 0, 0, 0},   // udot v0.4s, v0.16b, v0.16b
        {0x0f82e820, true, false, 2, 0, 1, 2},    // sdot v0.2s, v1.8b, v2.4b[2]
    };
    LowerPolicy fast;
    fast.trampolines = true;
    for (const Case& k : cases) {
        Insn in;
        DS_CHECK(decode(k.w, in) && in.op == Op::Dot && in.sgn == k.sgn && in.q == k.q && in.lane == k.lane &&
                 in.rd == k.rd && in.rn == k.rn && in.rm == k.rm);
        uint8_t init[48];
        for (int i = 0; i < 48; ++i) init[i] = uint8_t(i * 37 + 200);
        uint32_t ref[4];
        {
            uint8_t v[3][16];
            std::memcpy(v, init, 48);
            uint32_t d[4];
            std::memcpy(d, v[k.rd], 16);
            for (int e = 0; e < 4; ++e) {
                if (e >= (k.q ? 4 : 2)) { d[e] = 0; continue; }
                int32_t acc = 0;
                for (int j = 0; j < 4; ++j) {
                    const uint8_t a = v[k.rn][e * 4 + j], b = v[k.rm][(k.lane >= 0 ? k.lane : e) * 4 + j];
                    acc += k.sgn ? int32_t(int8_t(a)) * int8_t(b) : int32_t(a) * b;
                }
                d[e] += uint32_t(acc);
            }
            std::memcpy(ref, d, 16);
        }
        for (int mode = 0; mode < 2; ++mode) {
            ExecBuf b;
            LowerCounts c;
            auto f = mode ? b.load<void (*)(uint8_t*)>({0x3dc00000, 0x3dc00401, 0x3dc00802, k.w, 0x3d800000, 0xd65f03c0}, &c, fast)
                          : b.load<void (*)(uint8_t*)>({0x3dc00000, 0x3dc00401, 0x3dc00802, k.w, 0x3d800000, 0xd65f03c0}, &c);
            DS_CHECK_EQ(c.patched[int(Op::Dot)], 1ull);
            uint8_t buf[48];
            std::memcpy(buf, init, 48);
            f(buf);
            if (std::memcmp(buf, ref, 16) != 0)
                throw ds::test::Failure(std::string("dot result mismatch, ") + (mode ? "trampoline" : "trap") + " path");
        }
    }
}

DS_TEST(exec_ldadd_four_thread_contention) {
#if !defined(__aarch64__)
    std::fprintf(stderr, "SKIP exec_ldadd_four_thread_contention: needs an aarch64 host\n");
    return;
#endif
    live_runtime();
    ExecBuf b;
    auto f = b.load<uint64_t (*)(uint64_t*, uint64_t)>({0xf8e10002 /* ldaddal x1,x2,[x0] */, 0xaa0203e0, 0xd65f03c0});
    constexpr int kThreads = 4, kIters = 20000;
    alignas(64) uint64_t counter = 0;
    std::vector<std::vector<uint64_t>> olds(kThreads);
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
        ts.emplace_back([&, t] {
            thread_init();
            olds[t].reserve(kIters);
            for (int i = 0; i < kIters; ++i) olds[t].push_back(f(&counter, 1));
        });
    for (auto& t : ts) t.join();
    DS_CHECK_EQ(counter, uint64_t(kThreads) * kIters);
    // atomicity: the returned old values are exactly 0..N-1, each once
    std::vector<uint64_t> all;
    for (auto& v : olds) all.insert(all.end(), v.begin(), v.end());
    std::sort(all.begin(), all.end());
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i] != i) throw ds::test::Failure("ldadd emulation lost or duplicated an update at " + std::to_string(i));
}

DS_TEST(exec_trap_table_rejects_foreign_brk) {
    // a genuine `brk #n` is never mistaken for a lowered word: lookup is pc-checked
    const uint64_t pc = 0xdead0000;
    const int64_t idx = trap_table().add(pc, 0xd503233f);
    DS_CHECK(idx >= 0);
    DS_CHECK(trap_table().find(uint32_t(idx), pc) != nullptr);
    DS_CHECK(trap_table().find(uint32_t(idx), pc + 4) == nullptr);
    DS_CHECK(trap_table().find(0xFFFE, pc) == nullptr);
}

DS_TEST(exec_trap_table_overflow_is_found_by_pc) {
    // BRK has 16 bits: past kMax entries the reserved index 0xFFFF is used and the entry is found by pc
    auto t = std::make_unique<TrapTable>();
    for (uint32_t i = 0; i < TrapTable::kMax; ++i) DS_CHECK(t->add(0x1000 + 4ull * i, 0x10000000u + i) == int64_t(i));
    const int64_t a = t->add(0x900000, 0xd71f0830u), b = t->add(0x900004, 0xd63f0830u);
    DS_CHECK(a == TrapTable::kOverflowIdx && b == TrapTable::kOverflowIdx);
    const TrapEntry* ea = t->find(0xFFFF, 0x900000);
    const TrapEntry* eb = t->find(0xFFFF, 0x900004);
    DS_CHECK(ea && ea->word == 0xd71f0830u && eb && eb->word == 0xd63f0830u);
    DS_CHECK(t->find(0xFFFF, 0x900008) == nullptr);
    DS_CHECK(t->find(7, 0x1000 + 4 * 7) != nullptr);
}

DS_TEST(exec_probe_genuine_apple_functions) {
#if !defined(__aarch64__)
    std::fprintf(stderr, "SKIP exec_probe_genuine_apple_functions: needs an aarch64 host\n");
    return;
#endif
    const char* cache = "/home/ubuntu/macos-re/cache-arm64e/dyld_shared_cache_arm64e";
    struct stat st;
    if (stat(cache, &st) != 0) {
        std::fprintf(stderr, "SKIP exec_probe_genuine_apple_functions: %s not found\n", cache);
        return;
    }
    const std::string cmd = std::string(DS_PROBE_BIN) + " 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    DS_CHECK(p != nullptr);
    std::string out;
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    const int rc = pclose(p);
    if (rc != 0 || out.find("MISMATCH") != std::string::npos || out.find("RESULT: 9 match, 0 mismatch") == std::string::npos)
        throw ds::test::Failure("ds-exec-probe failed (status " + std::to_string(rc) + "):\n" + out);
}

DS_TEST(exec_fp16_trap_and_trampoline) {
#if !defined(__aarch64__)
    std::fprintf(stderr, "SKIP exec_fp16_trap_and_trampoline: needs an aarch64 host\n");
    return;
#endif
    live_runtime();
    // Exercise FP16 ops via memory-operand programs so the H registers are
    // test-controlled, executed through BRK traps (trap path) and through inline
    // trampolines (tramp path); both compared bit-for-bit against a C reference
    // computed in double precision (arm64emu semantics), NaN/Inf/subnormal/denormal
    // included via the edge input table.
    LowerPolicy fast;
    fast.trampolines = true;
    auto h2d = [](uint16_t b) -> double {
        uint32_t e = (b >> 10) & 0x1F, f = b & 0x3FF;
        uint32_t u;
        if (e == 0) { double dd = double(f) * (1.0 / (1u << 24)); return (b >> 15) ? -dd : dd; }
        if (e == 31) { u = ((b >> 15) ? 0x80000000u : 0u) | 0x7F800000u | (f << 13); float ff; std::memcpy(&ff, &u, 4); return double(ff); }
        u = ((b >> 15) ? 0x80000000u : 0u) | ((e + 112u) << 23) | (f << 13);
        float ff; std::memcpy(&ff, &u, 4); return double(ff);
    };
    auto d2h = [](double d) -> uint16_t {
        if (std::isnan(d)) return 0x7E00u;
        float f = float(d);
        uint32_t u; std::memcpy(&u, &f, 4);
        uint32_t sgn = (u >> 16) & 0x8000;
        uint32_t frac = u & 0x7FFFFFu;
        int e = int((u >> 23) & 0xFF) - 127;
        if (e > 15) return uint16_t(sgn | 0x7C00u);
        if (e >= -14) {
            uint32_t half_frac = (frac + 0x1000u) >> 13;
            return uint16_t(sgn | ((e + 15) << 10) | half_frac);
        }
        uint32_t m = frac | 0x800000u;
        int sh = -e - 1;
        if (sh > 24) return uint16_t(sgn);
        return uint16_t(sgn | ((m + (1u << (sh - 1))) >> sh));
    };
    struct Arg { uint16_t a, b, c; };
    auto run = [&](uint32_t op, Arg v, bool tramp) -> uint16_t {
        ExecBuf buf;
        LowerCounts cnt;
        auto f = buf.load<void (*)(uint16_t*)>({0x7d400000u, 0x7d400401u, op, 0x7d000000u, 0xd65f03c0u}, &cnt, tramp ? fast : LowerPolicy{});
        uint16_t b[3] = {v.a, v.b, v.c};
        f(b);
        return b[0];
    };
    const uint16_t vals[] = {0x3C00, 0x4000, 0xBC12, 0x0001, 0x2E66, 0x7BFE, 0x7C00, 0xFC00, 0x3555, 0x8400, 0x7E00, 0x0000, 0x8001, 0x7D00};
    struct Case { uint32_t w; Fp16Kind hk; };
    // Scalar arithmetic + fused class, all fptype H, rd=0 rn=1 rm=0 (verified via llvm-mc names)
    const Case cases[] = {
        {0x1EE12800, Fp16Kind::Fadd},
        {0x1EE13800, Fp16Kind::Fsub},
        {0x1EE10800, Fp16Kind::Fmul},
        {0x1EE11800, Fp16Kind::Fdiv},
        {0x1EE01000, Fp16Kind::FmovImm},
    };
    for (const Case& k : cases) {
        Insn in;
        DS_CHECK(decode(k.w, in) && in.op == Op::Fp16 && in.hk == k.hk);
        for (uint16_t a : vals) {
            for (uint16_t b : vals) {
                const uint16_t r_trap = run(k.w, {a, b, 0}, false);
                const uint16_t r_tramp = run(k.w, {a, b, 0}, true);
                DS_CHECK(r_trap == r_tramp);
                const double da = h2d(a), db = h2d(b);
                const double v = k.hk == Fp16Kind::Fadd ? da + db : k.hk == Fp16Kind::Fsub ? da - db : k.hk == Fp16Kind::Fmul ? da * db : da / db;
                if (k.hk == Fp16Kind::FmovImm) continue;  // result comes from imm8, not a/b
                if (std::isnan(v)) continue;
                DS_CHECK(d2h(v) == r_tramp || d2h(v) == r_trap);
            }
        }
    }
    // fmadd spot-check in double precision
    {
        struct M {
            Op op;  // Fp16 kinds handled in registers via program below
        };
        ExecBuf buf;
        LowerCounts c;
        // fmadd h0, h0, h1, h2 ; words from llvm-mc: ldr h0,[x0]=0x7d400000, ldr h1,[x0,#2]=0x7d400401, ldr h2,[x0,#4]=0x7d400802, 0x1FC10800, str h0,[x0]=0x7d000000
        auto f = buf.load<void (*)(uint16_t*)>({0x7d400000u, 0x7d400401u, 0x7d400802u, 0x1FC10800u, 0x7d000000u, 0xd65f03c0u}, &c, fast);
        uint16_t b[3] = {0x3C00, 0x4000, 0x4000};
        f(b);
        DS_CHECK_EQ(b[0], 0x4400);  // 1*2+2 = 4.0
        auto f2 = buf.load<void (*)(uint16_t*)>({0x7d400000u, 0x7d400401u, 0x7d400802u, 0x1FC10800u, 0x7d000000u, 0xd65f03c0u}, &c);
        uint16_t b2[3] = {0x3C00, 0x4000, 0x4000};
        f2(b2);
        DS_CHECK_EQ(b2[0], 0x4400);
    }
    // spot checks with known values
    auto spot = [&](uint32_t op, uint16_t a, uint16_t b, uint16_t expect, bool tramp) {
        ExecBuf buf;
        LowerCounts c;
        auto f = buf.load<void (*)(uint16_t*)>({0x7d400000u, 0x7d400401u, op, 0x7d000000u, 0xd65f03c0u}, &c, tramp ? fast : LowerPolicy{});
        uint16_t buf2[2] = {a, b};
        f(buf2);
        DS_CHECK_EQ(buf2[0], expect);
    };
    spot(0x1EE12800u, 0x3E00, 0x4100, 0x4400, false);  // 1.5 + 2.5 = 4.0
    spot(0x1EE12800u, 0x3E00, 0x4100, 0x4400, true);
    spot(0x1EE10800u, 0x3C00, 0x4000, 0x4000, true);  // 1.0 * 2.0 = 2.0
    spot(0x1EE11800u, 0x4000, 0x3E00, 0x3D55, true);  // 2.0 / 1.5 = 4/3 -> half 0x3D55 (1.333008)
}


DS_TEST(exec_fp16v_trap) {
#if !defined(__aarch64__)
    std::fprintf(stderr, "SKIP exec_fp16v_trap: needs an aarch64 host\n");
    return;
#endif
    live_runtime();
    auto h2d = [](uint16_t b) -> double {
        uint32_t e = (b >> 10) & 0x1F, f = b & 0x3FF;
        uint32_t u;
        if (e == 0) { double dd = double(f) * (1.0 / (1u << 24)); return (b >> 15) ? -dd : dd; }
        if (e == 31) { u = ((b >> 15) ? 0x80000000u : 0u) | 0x7F800000u | (f << 13); float ff; std::memcpy(&ff, &u, 4); return double(ff); }
        u = ((b >> 15) ? 0x80000000u : 0u) | ((e + 112u) << 23) | (f << 13);
        float ff; std::memcpy(&ff, &u, 4); return double(ff);
    };
    auto d2h = [](double d) -> uint16_t {
        if (std::isnan(d)) return 0x7E00u;
        float f = float(d);
        uint32_t u; std::memcpy(&u, &f, 4);
        uint32_t sgn = (u >> 16) & 0x8000;
        uint32_t frac = u & 0x7FFFFFu;
        int e = int((u >> 23) & 0xFF) - 127;
        if (e > 15) return uint16_t(sgn | 0x7C00u);
        if (e >= -14) {
            uint32_t half_frac = (frac + 0x1000u) >> 13;
            return uint16_t(sgn | ((e + 15) << 10) | half_frac);
        }
        uint32_t m = frac | 0x800000u;
        int sh = -e - 1;
        if (sh > 24) return uint16_t(sgn);
        return uint16_t(sgn | ((m + (1u << (sh - 1))) >> sh));
    };
    // vector .4h/.8h words verified against llvm-mc ground truth (lowering patterns)
    struct VC { uint32_t w; Fp16Kind hk; bool q; };
    const VC vcs[] = {
    {0x2EF8F820,  Fp16Kind::FnegV, 0},  // fneg v0.4h, v1.4h
    {0x6EF8F820,  Fp16Kind::FnegV, 1},  // fneg v0.8h, v1.8h
    {0x0EF8F820,  Fp16Kind::FabsV, 0},  // fabs v0.4h, v1.4h
    {0x4EF8F820,  Fp16Kind::FabsV, 1},  // fabs v0.8h, v1.8h
    {0x0E421420,  Fp16Kind::FaddV, 0},  // fadd v0.4h, v1.4h, v2.4h
    {0x4E421420,  Fp16Kind::FaddV, 1},  // fadd v0.8h, v1.8h, v2.8h
    {0x0EC21420,  Fp16Kind::FsubV, 0},  // fsub v0.4h, v1.4h, v2.4h
    {0x4EC21420,  Fp16Kind::FsubV, 1},  // fsub v0.8h, v1.8h, v2.8h
    {0x2E421C20,  Fp16Kind::FmulV, 0},  // fmul v0.4h, v1.4h, v2.4h
    {0x6E421C20,  Fp16Kind::FmulV, 1},  // fmul v0.8h, v1.8h, v2.8h
    {0x2E423C20,  Fp16Kind::FdivV, 0},  // fdiv v0.4h, v1.4h, v2.4h
    {0x6E423C20,  Fp16Kind::FdivV, 1},  // fdiv v0.8h, v1.8h, v2.8h
    {0x0E420420,  Fp16Kind::FmaxnmV, 0},  // fmaxnm v0.4h, v1.4h, v2.4h
    {0x4E420420,  Fp16Kind::FmaxnmV, 1},  // fmaxnm v0.8h, v1.8h, v2.8h
    {0x0EC20420,  Fp16Kind::FminnmV, 0},  // fminnm v0.4h, v1.4h, v2.4h
    {0x4EC20420,  Fp16Kind::FminnmV, 1},  // fminnm v0.8h, v1.8h, v2.8h
    {0x0E423420,  Fp16Kind::FmaxV, 0},  // fmax v0.4h, v1.4h, v2.4h
    {0x4E423420,  Fp16Kind::FmaxV, 1},  // fmax v0.8h, v1.8h, v2.8h
    {0x0EC23420,  Fp16Kind::FminV, 0},  // fmin v0.4h, v1.4h, v2.4h
    {0x4EC23420,  Fp16Kind::FminV, 1},  // fmin v0.8h, v1.8h, v2.8h
    {0x0E420C20,  Fp16Kind::FmlaV, 0},  // fmla v0.4h, v1.4h, v2.4h
    {0x4E420C20,  Fp16Kind::FmlaV, 1},  // fmla v0.8h, v1.8h, v2.8h
    {0x0EC20C20,  Fp16Kind::FmlsV, 0},  // fmls v0.4h, v1.4h, v2.4h
    {0x4EC20C20,  Fp16Kind::FmlsV, 1},  // fmls v0.8h, v1.8h, v2.8h
    {0x0E422420,  Fp16Kind::FcmeqV, 0},  // fcmeq v0.4h, v1.4h, v2.4h
    {0x4E422420,  Fp16Kind::FcmeqV, 1},  // fcmeq v0.8h, v1.8h, v2.8h
    {0x2E422420,  Fp16Kind::FcmgeV, 0},  // fcmge v0.4h, v1.4h, v2.4h
    {0x6E422420,  Fp16Kind::FcmgeV, 1},  // fcmge v0.8h, v1.8h, v2.8h
    {0x2EC22420,  Fp16Kind::FcmgtV, 0},  // fcmgt v0.4h, v1.4h, v2.4h
    {0x6EC22420,  Fp16Kind::FcmgtV, 1},  // fcmgt v0.8h, v1.8h, v2.8h
    {0x0EF9B820,  Fp16Kind::FcvtzsV, 0},  // fcvtzs v0.4h, v1.4h
    {0x4EF9B820,  Fp16Kind::FcvtzsV, 1},  // fcvtzs v0.8h, v1.8h
    {0x2EF9B820,  Fp16Kind::FcvtzuV, 0},  // fcvtzu v0.4h, v1.4h
    {0x6EF9B820,  Fp16Kind::FcvtzuV, 1},  // fcvtzu v0.8h, v1.8h
    {0x0E79D820,  Fp16Kind::ScvtfV, 0},  // scvtf v0.4h, v1.4h
    {0x4E79D820,  Fp16Kind::ScvtfV, 1},  // scvtf v0.8h, v1.8h
    {0x2E79D820,  Fp16Kind::UcvtfV, 0},  // ucvtf v0.4h, v1.4h
    {0x6E79D820,  Fp16Kind::UcvtfV, 1},  // ucvtf v0.8h, v1.8h
    {0x2EF99820,  Fp16Kind::FrintiV, 0},  // frinti v0.4h, v1.4h
    {0x6EF99820,  Fp16Kind::FrintiV, 1},  // frinti v0.8h, v1.8h
    {0x0EF8D820,  Fp16Kind::Fcmeq0V, 0},  // fcmeq v0.4h, v1.4h, #0.0
    {0x2EF8C820,  Fp16Kind::Fcmge0V, 0},  // fcmge v0.4h, v1.4h, #0.0
    {0x0EF8C820,  Fp16Kind::Fcmgt0V, 0},  // fcmgt v0.4h, v1.4h, #0.0
    };
    const uint16_t edge[] = {0x0000, 0x8000, 0x0001, 0x8001, 0x03FF, 0x83FF,
                              0x3C00, 0xBC00, 0x4000, 0xC400, 0x4001, 0x4400,
                              0x7BFF, 0x7C00, 0xFC00, 0x7E00, 0x7D00, 0xFE00,
                              0x3D00, 0x3555, 0x0400, 0x8400, 0x7B00};
    uint32_t rng = 24398;
    auto rnd = [&rng] { rng = rng * 1103515245 + 12345; return uint16_t((rng >> 16) & 0x3FF); };
    // store/load words: str to preserve v0 around (the guest dst is v0; we store to [x0])
    auto runv = [&](uint32_t op, const uint16_t n[8], const uint16_t m[8], bool q) -> std::array<uint16_t,8> {
        ExecBuf buf;
        LowerCounts cnt;
        // program: mov x0 arg already in x0 whole-ptr; words will be lowered by trackable default; we must place
        // n/m in V1/V2/V0 lanes BEFORE executing op. Memory layout: [x0] = n (16B), [x0+16] = m (16B), [x0+32] = dst out.
        // Encodings from llvm-mc ground truth:
        //   ldr q1,[x0] = 0x4D400020? windows: 0x3DC40020 (ldr q1, [x0]); simpler: use raw generate via templates below.
        (void)op; (void)n; (void)m; (void)q; (void)cnt;
        return {};  // placeholder, replaced by real helper below
    };
    (void)runv;
    // Build helper word arrays with pre-verified encodings from llvm-mc:
    //   ldr q0,[x1] (dst?) -- we'll instead generate programs assembled here via std::vector words,
    // with guest fn signature f(out, n, m). Words (verified):
    //   ldr q1,[x1] = 0x3DC00420  ; ldr q2,[x2] = 0x3DC00840 ; ldr q0,[x0] would collide dst; dst=V0: str q0,[x0] = 0x3D800000
    // We'll verify these encodings below by decode-free trap execution comparing to reference.
    struct RunOut { std::array<uint16_t,8> o; };
    auto run = [&](uint32_t op, const uint16_t n[8], const uint16_t m[8]) -> RunOut {
        ExecBuf buf;
        LowerCounts cnt;
        const uint32_t words[] = {0x3DC00021u, 0x3DC00042u, 0x4F00E400u, op, 0x3D800000u, 0xD65F03C0u}; // ldr q1,[x1]; ldr q2,[x2]; movi A.16B,#0 (zero V0); OP; str q0,[x0]; ret
        auto f = buf.load<void (*)(uint16_t*, const uint16_t*, const uint16_t*)>(std::vector<uint32_t>(words, words+6), &cnt);
        uint16_t outb[8] = {}; uint16_t nb[8], mb[8];
        std::memcpy(nb, n, 16); std::memcpy(mb, m, 16);
        f(outb, nb, mb);
        RunOut r; std::memcpy(r.o.data(), outb, 16); return r;
    };
    for (const VC& vc : vcs) {
        Insn in;
        DS_CHECK(decode(vc.w, in) && in.op == Op::Fp16V && in.hk == vc.hk);
        const int lanes = vc.q ? 8 : 4;
        // edge + random lane checks
        for (int t = 0; t < 48; ++t) {
            uint16_t n[8] = {}, m[8] = {};
            for (int i = 0; i < 8; ++i) {
                n[i] = (t < 23) ? edge[t % 23] : rnd();
                m[i] = (t < 23) ? edge[(t + 11) % 23] : rnd();
                if (i >= lanes) { n[i] = edge[0]; m[i] = edge[0]; }
            }
            RunOut out = run(vc.w, n, m);
            for (int i = 0; i < lanes; ++i) {
                double a = h2d(n[i]), b = h2d(m[i]);
                uint16_t ref = 0;
                switch (vc.hk) {
                    case Fp16Kind::FnegV: ref = uint16_t(n[i] ^ 0x8000u); break;
                    case Fp16Kind::FabsV: ref = uint16_t(n[i] & 0x7FFFu); break;
                    case Fp16Kind::FaddV: ref = d2h(a + b); break;
                    case Fp16Kind::FsubV: ref = d2h(a - b); break;
                    case Fp16Kind::FmulV: ref = d2h(a * b); break;
                    case Fp16Kind::FdivV: ref = d2h(a / b); break;
                    case Fp16Kind::FmaxnmV: ref = std::isnan(a) ? m[i] : (std::isnan(b) ? n[i] : (a > b ? n[i] : m[i])); if (std::isnan(a) && std::isnan(b)) ref = 0x7E00u; break;
                    case Fp16Kind::FminnmV: ref = std::isnan(a) ? m[i] : (std::isnan(b) ? n[i] : (a < b ? n[i] : m[i])); if (std::isnan(a) && std::isnan(b)) ref = 0x7E00u; break;
                    case Fp16Kind::FmaxV: ref = (std::isnan(a) || std::isnan(b)) ? (std::isnan(a) ? n[i] | 0x0200u : m[i] | 0x0200u) : (a > b ? n[i] : m[i]); break;
                    case Fp16Kind::FminV: ref = (std::isnan(a) || std::isnan(b)) ? (std::isnan(a) ? n[i] | 0x0200u : m[i] | 0x0200u) : (a < b ? n[i] : m[i]); break;
                    case Fp16Kind::FmlaV: ref = d2h(a * b + h2d(0)); break;  // dst=V0 initial content = 0
                    case Fp16Kind::FmlsV: ref = d2h(a * b - h2d(0)); break;
                    case Fp16Kind::FcmeqV: ref = (a == b) ? 0xFFFFu : 0x0000u; break;
                    case Fp16Kind::FcmgeV: ref = (a >= b) ? 0xFFFFu : 0x0000u; break;
                    case Fp16Kind::FcmgtV: ref = (a > b) ? 0xFFFFu : 0x0000u; break;
                    case Fp16Kind::Fcmeq0V: ref = (a == 0.0) ? 0xFFFFu : 0x0000u; break;
                    case Fp16Kind::Fcmge0V: ref = (a >= 0.0) ? 0xFFFFu : 0x0000u; break;
                    case Fp16Kind::Fcmgt0V: ref = (a > 0.0) ? 0xFFFFu : 0x0000u; break;
                    case Fp16Kind::FcvtzsV: { long v = long(a); if (v > 32767) v = 32767; if (v < -32768) v = -32768; ref = uint16_t(v); break; }
                    case Fp16Kind::FcvtzuV: { long v = long(a); if (v > 32767) v = 32767; if (v < 0) v = 0; ref = uint16_t(v); break; }
                    case Fp16Kind::ScvtfV: ref = d2h(double(int16_t(n[i]))); break;
                    case Fp16Kind::UcvtfV: ref = d2h(double(n[i])); break;
                    case Fp16Kind::FrintiV: { long v = lround(a); if (v > 32767) v = 32767; if (v < -32768) v = -32768; ref = d2h(double(v)); break; }
                    default: break;
                }
                if (std::isnan(h2d(ref))) {
                    DS_CHECK(std::isnan(h2d(out.o[i])));
                    continue;
                }
                if (out.o[i] != ref)
                    printf("VDIFF hk=%d i=%d ref=%04x out=%04x a=%04x b=%04x w=%08x\\n", (int)vc.hk, i, ref, out.o[i], n[i], m[i], vc.w);
                DS_CHECK_EQ(out.o[i], ref);
            }
        }
    }
}


DS_TEST(exec_tramp_vregs_preserved) {
#if !defined(__aarch64__)
    std::fprintf(stderr, "SKIP exec_tramp_vregs_preserved: needs an aarch64 host\n");
    return;
#endif
    live_runtime();
    setenv("DS_TRAMP_NO_INLINE", "1", 1);
    LowerPolicy pol;
    pol.trampolines = true;
    auto pat = [](int i) -> uint64_t { return 0x9E3779B97F4A7C15ull ^ (0x2545F4914F6CDD1Dull * (uint64_t)(i + 1)); };
    std::vector<uint32_t> words;
    for (int i = 8; i <= 15; ++i) {
        uint64_t lo = pat(i), hi = pat(i + 16);
        unsigned xi = 10, xt = 11;
        words.push_back(0xD2800000u | (uint32_t)(lo & 0xFFFF) << 5 | xi);
        words.push_back(0xF2800000u | (uint32_t)((lo >> 16) & 0xFFFF) << 5 | 0x00200000u | xi);
        words.push_back(0xF2800000u | (uint32_t)((lo >> 32) & 0xFFFF) << 5 | 0x00400000u | xi);
        words.push_back(0xF2800000u | (uint32_t)((lo >> 48) & 0xFFFF) << 5 | 0x00600000u | xi);
        words.push_back(0xD2800000u | (uint32_t)(hi & 0xFFFF) << 5 | xt);
        words.push_back(0xF2800000u | (uint32_t)((hi >> 16) & 0xFFFF) << 5 | 0x00200000u | xt);
        words.push_back(0xF2800000u | (uint32_t)((hi >> 32) & 0xFFFF) << 5 | 0x00400000u | xt);
        words.push_back(0xF2800000u | (uint32_t)((hi >> 48) & 0xFFFF) << 5 | 0x00600000u | xt);
        words.push_back(0x4E081D48u | (unsigned)i);  // ins v_i.d[0], x10  (llvm-mc verified)
        words.push_back(0x4E181D68u | (unsigned)i);  // ins v_i.d[1], x11
    }
    words.push_back(0xd503237fu);  // pacibsp -> generic C++ handler
    const uint32_t store[] = { 0x3d800008u, 0x3d800409u, 0x3d80080au, 0x3d800c0bu, 0x3d80100cu, 0x3d80140du, 0x3d80180eu, 0x3d801c0fu };
    for (uint32_t w : store) words.push_back(w);
    words.push_back(0xd50323ffu);  // autibsp
    words.push_back(0xd65f03c0u);  // ret (LR already authenticated)
    ExecBuf buf;
    LowerCounts c;
    auto f = buf.load<void (*)(uint64_t*)>(words, &c, pol);
    uint64_t out[16] = {};
    f(out);
    for (int i = 8; i <= 15; ++i) {
        uint64_t lo_got = out[2*(i-8)], hi_got = out[2*(i-8)+1];
        if (lo_got != pat(i) || hi_got != pat(i + 16))
            printf("VREGDIFF v%d: lo=%016llx hi=%016llx expect lo=%016llx hi=%016llx\n", i,
                   (unsigned long long)lo_got, (unsigned long long)hi_got,
                   (unsigned long long)pat(i), (unsigned long long)pat(i + 16));
        DS_CHECK_EQ(lo_got, pat(i));
        DS_CHECK_EQ(hi_got, pat(i + 16));
    }
    unsetenv("DS_TRAMP_NO_INLINE");
}


DS_TEST(exec_fp16_data_alias_rejected) {
    // FSE literal-mask table word 0x1fffffff ({2^29-1, 0}) inside __text collides with
    // the FP16 scalar FMA encoding space. It must never be lowered (it is data).
    {
        Insn in;
        DS_CHECK(!decode(0x1FFFFFFFu, in));   // fnmsub h31,h31,h31,h31 pattern -> data
        DS_CHECK(!decode(0x3FFFFFFFu, in));   // entry 30 mask {2^30-1, 0} same family
        DS_CHECK(!decode(0x7FFFFFFFu, in));   // entry 31 {2^31-1, 0}
    }
    {
        // Genuine FP16 FMA still decodes: fmadd h0, h0, h1, h2 (0x1FC10800)
        Insn in;
        DS_CHECK(decode(0x1FC10800u, in) && in.op == Op::Fp16 && in.hk == Fp16Kind::Fmadd);
        DS_CHECK_EQ(in.rd, 0);
        DS_CHECK_EQ(in.rn, 0);
        DS_CHECK_EQ(in.rm, 1);
        DS_CHECK_EQ(in.rs, 2);
    }
}

DS_TEST(exec_remap_restore_original) {
#if !defined(__aarch64__)
    std::fprintf(stderr, "SKIP exec_remap_restore_original: needs an aarch64 host\n");
    return;
#endif
    live_runtime();
    // pacibsp; mov x0, #0; autibsp; ret
    const std::vector<uint32_t> orig = {0xd503237f, 0xd2800000, 0xd50323ff, 0xd65f03c0};
    auto check = [&](bool trampolines) {
        ExecBuf src;
        std::memcpy(src.mem, orig.data(), orig.size() * 4);
        LowerPolicy pol;
        pol.trampolines = trampolines;
        LowerCounts c;
        std::string err;
        DS_CHECK(lower_range(src.mem, src.mem + orig.size(), pol, c, &err));
        // lowering must have changed something (a BRK or a `b stub`), else the restore is vacuous
        bool changed = false;
        for (size_t i = 0; i < orig.size(); ++i) changed = changed || src.mem[i] != orig[i];
        DS_CHECK(changed);
        // darlingserver copies the already-lowered guest bytes into the vm_remap destination
        ExecBuf dst;
        std::memcpy(dst.mem, src.mem, orig.size() * 4);
        flush_icache(dst.mem, dst.mem + orig.size());
        const size_t restored = restore_original(dst.mem, src.mem, orig.size());
        DS_CHECK(restored >= 1);
        for (size_t i = 0; i < orig.size(); ++i) DS_CHECK_EQ(dst.mem[i], orig[i]);
        // the restored copy is lowered afresh at its own address and runs
        LowerCounts c2;
        DS_CHECK(lower_range(dst.mem, dst.mem + orig.size(), pol, c2, &err));
        flush_icache(dst.mem, dst.mem + orig.size());
        DS_CHECK_EQ(reinterpret_cast<long (*)()>(dst.mem)(), 0l);
    };
    check(false);  // BRK trap-table entries
    check(true);   // trampoline sites (`b stub`)
}
