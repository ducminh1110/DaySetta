// ds-exec tests: software PAC, instruction lowering, trap emulation, probe integration.
//
// Encodings in kEnc were produced by `clang --target=aarch64-linux-gnu -march=armv8.5-a+pauth+lse`
// + `llvm-objdump -d` (class VERIFIED, not hand-derived).  Darwin ENOSYS = 78:
// /home/ubuntu/darling/src/external/xnu/bsd/sys/errno.h:211.
#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "exec/lowering.hpp"
#include "exec/pac.hpp"
#include "exec/trap.hpp"
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
    F load(std::vector<uint32_t> words, LowerCounts* counts = nullptr) {
        std::memcpy(mem, words.data(), words.size() * 4);
        LowerCounts c;
        std::string err;
        if (!lower_range(mem, mem + words.size(), LowerPolicy{}, c, &err)) throw ds::test::Failure(err);
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

DS_TEST(exec_ldadd_four_thread_contention) {
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
    DS_CHECK(trap_table().find(0xFFFF, pc) == nullptr);
}

DS_TEST(exec_probe_genuine_apple_functions) {
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
