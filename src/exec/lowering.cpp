#include <unordered_map>
#include "lowering.hpp"
#include "trampoline.hpp"

namespace ds::exec {

const char* op_name(Op op) {
    static const char* names[] = {"none",    "pac-sign", "pac-auth", "pac-strip", "pacga", "br-auth",
                                  "ret-auth", "ldr-auth", "cas",      "casp",      "rmw",   "svc",
                                  "mrs-tpidrro", "ldapur", "cpa", "mrs-tpidr", "dot"};
    return names[static_cast<int>(op)];
}

const char* rmw_name(Rmw r) {
    static const char* names[] = {"ldadd", "ldclr", "ldeor", "ldset", "ldsmax",
                                  "ldsmin", "ldumax", "ldumin", "swp"};
    return names[static_cast<int>(r)];
}

namespace {
constexpr uint32_t bits(uint32_t w, int hi, int lo) { return (w >> lo) & ((1u << (hi - lo + 1)) - 1); }

bool decode_hint(uint32_t w, Insn& o) {
    if ((w & 0xFFFFF01F) != 0xD503201F) return false;
    const uint32_t crm_op2 = bits(w, 11, 5);  // CRm:op2
    switch (crm_op2) {
        case 0x07: o.op = Op::PacStrip; o.rd = 30; return true;                       // XPACLRI
        case 0x08: o.op = Op::PacSign; o.key = PacKey::IA; o.rd = 17; o.mod = ModSrc::Reg; o.rm = 16; return true;  // PACIA1716
        case 0x0A: o.op = Op::PacSign; o.key = PacKey::IB; o.rd = 17; o.mod = ModSrc::Reg; o.rm = 16; return true;  // PACIB1716
        case 0x0C: o.op = Op::PacAuth; o.key = PacKey::IA; o.rd = 17; o.mod = ModSrc::Reg; o.rm = 16; return true;  // AUTIA1716
        case 0x0E: o.op = Op::PacAuth; o.key = PacKey::IB; o.rd = 17; o.mod = ModSrc::Reg; o.rm = 16; return true;  // AUTIB1716
        case 0x18: o.op = Op::PacSign; o.key = PacKey::IA; o.mod = ModSrc::Zero; break;  // PACIAZ
        case 0x19: o.op = Op::PacSign; o.key = PacKey::IA; o.mod = ModSrc::Sp; break;    // PACIASP
        case 0x1A: o.op = Op::PacSign; o.key = PacKey::IB; o.mod = ModSrc::Zero; break;  // PACIBZ
        case 0x1B: o.op = Op::PacSign; o.key = PacKey::IB; o.mod = ModSrc::Sp; break;    // PACIBSP
        case 0x1C: o.op = Op::PacAuth; o.key = PacKey::IA; o.mod = ModSrc::Zero; break;  // AUTIAZ
        case 0x1D: o.op = Op::PacAuth; o.key = PacKey::IA; o.mod = ModSrc::Sp; break;    // AUTIASP
        case 0x1E: o.op = Op::PacAuth; o.key = PacKey::IB; o.mod = ModSrc::Zero; break;  // AUTIBZ
        case 0x1F: o.op = Op::PacAuth; o.key = PacKey::IB; o.mod = ModSrc::Sp; break;    // AUTIBSP
        default: return false;
    }
    o.rd = 30;
    return true;
}

bool decode_pac_1src(uint32_t w, Insn& o) {
    if ((w & 0xFFFF8000) != 0xDAC10000) return false;  // sf=1, 1-source, opcode2 = 00001
    const uint32_t opc = bits(w, 15, 10);
    const uint32_t rn = bits(w, 9, 5), rd = bits(w, 4, 0);
    o.rd = rd;
    if (opc < 8) {  // PAC/AUT with register modifier (Rn == 31 means SP)
        o.op = opc < 4 ? Op::PacSign : Op::PacAuth;
        static const PacKey keys[4] = {PacKey::IA, PacKey::IB, PacKey::DA, PacKey::DB};
        o.key = keys[opc & 3];
        o.rn = rn;
        o.mod = rn == 31 ? ModSrc::Sp : ModSrc::Reg;
        o.rm = rn;
        return true;
    }
    if (rn != 31) return false;
    if (opc < 16) {  // Z forms
        o.op = opc < 12 ? Op::PacSign : Op::PacAuth;
        static const PacKey keys[4] = {PacKey::IA, PacKey::IB, PacKey::DA, PacKey::DB};
        o.key = keys[opc & 3];
        o.mod = ModSrc::Zero;
        return true;
    }
    if (opc == 16 || opc == 17) { o.op = Op::PacStrip; return true; }  // XPACI / XPACD
    return false;
}

bool decode_branch(uint32_t w, Insn& o) {
    // bit 24: 0 = zero-modifier form (BRAAZ...), 1 = BRAA Xn, Xm|SP; bit 21: BLR; bit 10: key B
    o.op = Op::BrAuth;
    o.key = bits(w, 10, 10) ? PacKey::IB : PacKey::IA;
    o.rn = bits(w, 9, 5);
    o.link = bits(w, 21, 21);
    if (!bits(w, 24, 24)) {
        if (bits(w, 4, 0) != 31) return false;
        o.mod = ModSrc::Zero;
    } else {
        o.rm = bits(w, 4, 0);
        o.mod = o.rm == 31 ? ModSrc::Sp : ModSrc::Reg;
    }
    return true;
}

bool decode_lse(uint32_t w, Insn& o) {
    if ((w & 0x3F200C00) == 0x38200000) {  // atomic memory operations
        const uint32_t o3 = bits(w, 15, 15), opc = bits(w, 14, 12);
        if (o3 && opc != 0) return false;  // LDAPR (FEAT_LRCPC) and LS64 forms: not LSE RMW
        o.op = Op::Rmw;
        o.rmw = o3 ? Rmw::Swp : static_cast<Rmw>(opc);
        o.size = bits(w, 31, 30);
        o.acquire = bits(w, 23, 23);
        o.release = bits(w, 22, 22);
        o.rs = bits(w, 20, 16);
        o.rn = bits(w, 9, 5);
        o.rd = bits(w, 4, 0);
        return true;
    }
    if ((w & 0x3FA07C00) == 0x08A07C00) {  // CAS{,A,L,AL}{,B,H}
        o.op = Op::Cas;
        o.size = bits(w, 31, 30);
        o.acquire = bits(w, 22, 22);
        o.release = bits(w, 15, 15);
        o.rs = bits(w, 20, 16);
        o.rn = bits(w, 9, 5);
        o.rd = bits(w, 4, 0);
        return true;
    }
    if ((w & 0xBFA07C00) == 0x08207C00) {  // CASP{,A,L,AL}
        o.op = Op::Casp;
        o.size = bits(w, 30, 30) ? 3 : 2;
        o.acquire = bits(w, 22, 22);
        o.release = bits(w, 15, 15);
        o.rs = bits(w, 20, 16);
        o.rn = bits(w, 9, 5);
        o.rd = bits(w, 4, 0);
        if ((o.rs & 1) || (o.rd & 1)) return false;  // odd register pair: UNDEFINED, leave native
        return true;
    }
    return false;
}

bool decode_ldraa(uint32_t w, Insn& o) {
    if ((w & 0xFF200400) != 0xF8200400) return false;
    o.op = Op::LdrAuth;
    o.key = bits(w, 23, 23) ? PacKey::DB : PacKey::DA;
    const uint32_t s = bits(w, 22, 22), imm9 = bits(w, 20, 12);
    int32_t simm = int32_t((s << 9) | imm9);
    if (s) simm -= 1 << 10;
    o.imm = simm * 8;
    o.writeback = bits(w, 11, 11);
    o.rn = bits(w, 9, 5);
    o.rd = bits(w, 4, 0);
    return true;
}

// FP16 (FEAT_FP16) vector ops on Vd.<4h|8h>: per-lane widen-compute-narrow emulation.
// Patterns (mask, value) from llvm-mc-assembled operand grids (ground truth); operands
// use the standard SIMD layout (rm=[20:16], rn=[9:5], rd=[4:0]) except indexed forms.
// Trap path only (trampoline_wants false for Op::Fp16V).
bool decode_fp16v(uint32_t w, Insn& o) {
    struct VP { uint32_t mask, value; Fp16Kind hk; };
    static const VP pats[] = {
        {0xFFE0FE80u, 0x0E401400u, Fp16Kind::FaddV},  // fadd.4h swept
        {0xFFE0FE80u, 0x4E401400u, Fp16Kind::FaddV},  // fadd.8h swept
        {0xFFE0FE80u, 0x0EC01400u, Fp16Kind::FsubV},  // fsub.4h swept
        {0xFFE0FE80u, 0x4EC01400u, Fp16Kind::FsubV},  // fsub.8h swept
        {0xFFE0FE80u, 0x2E401C00u, Fp16Kind::FmulV},  // fmul.4h swept
        {0xFFE0FE80u, 0x6E401C00u, Fp16Kind::FmulV},  // fmul.8h swept
        {0xFFE0FE80u, 0x2E403C00u, Fp16Kind::FdivV},  // fdiv.4h swept
        {0xFFE0FE80u, 0x6E403C00u, Fp16Kind::FdivV},  // fdiv.8h swept
        {0xFFE0FE80u, 0x0E403400u, Fp16Kind::FmaxV},  // fmax.4h swept
        {0xFFE0FE80u, 0x4E403400u, Fp16Kind::FmaxV},  // fmax.8h swept
        {0xFFE0FE80u, 0x0EC03400u, Fp16Kind::FminV},  // fmin.4h swept
        {0xFFE0FE80u, 0x4EC03400u, Fp16Kind::FminV},  // fmin.8h swept
        {0xFFE0FE80u, 0x0E400400u, Fp16Kind::FmaxnmV},  // fmaxnm.4h swept
        {0xFFE0FE80u, 0x4E400400u, Fp16Kind::FmaxnmV},  // fmaxnm.8h swept
        {0xFFE0FE80u, 0x0EC00400u, Fp16Kind::FminnmV},  // fminnm.4h swept
        {0xFFE0FE80u, 0x4EC00400u, Fp16Kind::FminnmV},  // fminnm.8h swept
        {0xFFE0FE80u, 0x0E400C00u, Fp16Kind::FmlaV},  // fmla.4h swept
        {0xFFE0FE80u, 0x4E400C00u, Fp16Kind::FmlaV},  // fmla.8h swept
        {0xFFE0FE80u, 0x0EC00C00u, Fp16Kind::FmlsV},  // fmls.4h swept
        {0xFFE0FE80u, 0x4EC00C00u, Fp16Kind::FmlsV},  // fmls.8h swept
        {0xFFE0FE80u, 0x0E402400u, Fp16Kind::FcmeqV},  // fcmeq.4h swept
        {0xFFE0FE80u, 0x4E402400u, Fp16Kind::FcmeqV},  // fcmeq.8h swept
        {0xFFE0FE80u, 0x2E402400u, Fp16Kind::FcmgeV},  // fcmge.4h swept
        {0xFFE0FE80u, 0x6E402400u, Fp16Kind::FcmgeV},  // fcmge.8h swept
        {0xFFE0FE80u, 0x2EC02400u, Fp16Kind::FcmgtV},  // fcmgt.4h swept
        {0xFFE0FE80u, 0x6EC02400u, Fp16Kind::FcmgtV},  // fcmgt.8h swept
        {0xFFFFFC00u, 0x0EF8D800u, Fp16Kind::Fcmeq0V},  // fcmeq0.4h swept
        {0xFFFFFC00u, 0x4EF8D800u, Fp16Kind::Fcmeq0V},  // fcmeq0.8h swept
        {0xFFFFFC00u, 0x2EF8C800u, Fp16Kind::Fcmge0V},  // fcmge0.4h swept
        {0xFFFFFC00u, 0x6EF8C800u, Fp16Kind::Fcmge0V},  // fcmge0.8h swept
        {0xFFFFFC00u, 0x0EF8C800u, Fp16Kind::Fcmgt0V},  // fcmgt0.4h swept
        {0xFFFFFC00u, 0x4EF8C800u, Fp16Kind::Fcmgt0V},  // fcmgt0.8h swept
        {0xFFFFFD40u, 0x2EF8F800u, Fp16Kind::FnegV},  // fneg.4h swept
        {0xFFFFFD40u, 0x6EF8F800u, Fp16Kind::FnegV},  // fneg.8h swept
        {0xFFFFFD40u, 0x0EF8F800u, Fp16Kind::FabsV},  // fabs.4h swept
        {0xFFFFFD40u, 0x4EF8F800u, Fp16Kind::FabsV},  // fabs.8h swept
        {0xFFFFFD40u, 0x0EF9B800u, Fp16Kind::FcvtzsV},  // fcvtzs.4h swept
        {0xFFFFFD40u, 0x4EF9B800u, Fp16Kind::FcvtzsV},  // fcvtzs.8h swept
        {0xFFFFFD40u, 0x2EF9B800u, Fp16Kind::FcvtzuV},  // fcvtzu.4h swept
        {0xFFFFFD40u, 0x6EF9B800u, Fp16Kind::FcvtzuV},  // fcvtzu.8h swept
        {0xFFFFFD40u, 0x0E79D800u, Fp16Kind::ScvtfV},  // scvtf.4h swept
        {0xFFFFFD40u, 0x4E79D800u, Fp16Kind::ScvtfV},  // scvtf.8h swept
        {0xFFFFFD40u, 0x2E79D800u, Fp16Kind::UcvtfV},  // ucvtf.4h swept
        {0xFFFFFD40u, 0x6E79D800u, Fp16Kind::UcvtfV},  // ucvtf.8h swept
        {0xFFFFFD40u, 0x2EF99800u, Fp16Kind::FrintiV},  // frinti.4h swept
        {0xFFFFFD40u, 0x6EF99800u, Fp16Kind::FrintiV},  // frinti.8h swept
        {0xFFFFFD40u, 0x2E79A800u, Fp16Kind::FcvtnuV},  // fcvtnu.4h swept
        {0xFFFFFD40u, 0x6E79A800u, Fp16Kind::FcvtnuV},  // fcvtnu.8h swept
        {0xFFE0FD40u, 0x2E401400u, Fp16Kind::FaddpV},  // faddp.4h swept
        {0xFFE0FD40u, 0x6E401400u, Fp16Kind::FaddpV},  // faddp.8h swept
        {0xFFE0FD40u, 0x2E403400u, Fp16Kind::FmaxpV},  // fmaxp.4h swept
        {0xFFE0FD40u, 0x6E403400u, Fp16Kind::FmaxpV},  // fmaxp.8h swept
        {0xFFE0FD40u, 0x2EC03400u, Fp16Kind::FminpV},  // fminp.4h swept
        {0xFFE0FD40u, 0x6EC03400u, Fp16Kind::FminpV},  // fminp.8h swept
        {0xFFC0F700u, 0x0F009000u, Fp16Kind::FmulIdxV},  // fmulby.4h swept
        {0xFFC0F700u, 0x4F009000u, Fp16Kind::FmulIdxV},  // fmulby.8h swept
        {0xFFC0F700u, 0x0F001000u, Fp16Kind::FmlaIdxV},  // fmlaby.4h swept
        {0xFFC0F700u, 0x4F001000u, Fp16Kind::FmlaIdxV},  // fmlaby.8h swept
        {0xFFC0F700u, 0x0F005000u, Fp16Kind::FmlsIdxV},  // fmlsby.4h swept
        {0xFFC0F700u, 0x4F005000u, Fp16Kind::FmlsIdxV},  // fmlsby.8h swept
        {0xFFE3FF00u, 0x0E020400u, Fp16Kind::DupV},  // dup.4h swept
        {0xFFE3FF00u, 0x4E020400u, Fp16Kind::DupV},  // dup.8h swept
    };
    for (const auto& p : pats) {
        if ((w & p.mask) != p.value) continue;
        o.op = Op::Fp16V;
        o.hk = p.hk;
        o.rd = bits(w, 4, 0);
        o.rn = bits(w, 9, 5);
        o.rm = bits(w, 20, 16);
        o.q = !!(w >> 30) & 1;                 // .8h forms have bit30 set
        // by-element index (only for the indexed*/dup rows): low bits of Rm (20, 21) and size-bit 11
        o.lane = (p.hk == Fp16Kind::FmulIdxV || p.hk == Fp16Kind::FmlaIdxV || p.hk == Fp16Kind::FmlsIdxV || p.hk == Fp16Kind::DupV)
            ? int8_t(bits(w, 11, 11) * 4 + bits(w, 21, 21) * 2 + bits(w, 20, 20))
            : int8_t(-1);
        return true;
    }
    return false;
}

// FP16 (FEAT_FP16) scalar ops on H registers.  The host is ARMv8.0 (no FP16 hardware), so
// these are lowered to BRK slots or inline ARMv8.0 sequences built from FCVT between half
// and single/double (ARMv8.0 baseline) plus single/double arithmetic.  Register operand
// fields are always the SIMD layout (rm=[20:16], rn=[9:5], rd=[4:0]).  Mask/value pairs
// below were derived by assembling operand grids with llvm-mc (-mattr=+fullfp16) and
// diffing the operand fields across the grid; they are cross-checked by the
// exec_fp16_decode test (assemble-vs-decode over random operands).  Op kinds go in
// Insn::hk (Fp16Kind).
bool decode_fp16(uint32_t w, Insn& o) {
    struct Pat { uint32_t mask, value; Fp16Kind hk; };
    static const Pat pats[] = {
        {0xFFE0FC00, 0x1EE01000, Fp16Kind::FmovImm},
        {0xFFFFFC00, 0x1EE04000, Fp16Kind::FmovRegH},                 // fmov hd, hn
        {0xFFFFFC00, 0x1EE70000, Fp16Kind::FmovWtoH},                 // fmov hd, wn
        {0xFFFFFC00, 0x1EE60000, Fp16Kind::FmovHtoW},                 // fmov wd, hn
        {0xFFBFFC00, 0x1E23C000, Fp16Kind::FcvtToH},                  // fcvt hd, sn
        {0xFFBFFC00, 0x1E63C000, Fp16Kind::FcvtToH},                  // fcvt hd, dn
        {0xFFFF7C00, 0x1EE24000, Fp16Kind::FcvtFromH},                // fcvt sn, hn
        {0xFFFF7C00, 0x1EE2C000, Fp16Kind::FcvtFromH},                // fcvt dn, hn
        {0xFFE0FC00, 0x1EE02800, Fp16Kind::Fadd},
        {0xFFE0FC00, 0x1EE03800, Fp16Kind::Fsub},
        {0xFFE0FC00, 0x1EE00800, Fp16Kind::Fmul},
        {0xFFE0FC00, 0x1EE01800, Fp16Kind::Fdiv},
        {0xFFE08000, 0x1FC00000, Fp16Kind::Fmadd},
        {0xFFE08000, 0x1FC08000, Fp16Kind::Fmsub},
        {0xFFE08000, 0x1FE00000, Fp16Kind::Fnmadd},
        {0xFFE08000, 0x1FE08000, Fp16Kind::Fnmsub},
        {0xFFE00C00, 0x1EE00C00, Fp16Kind::Fcsel},
        {0xFFE00C00, 0x1EE00400, Fp16Kind::Fccmp},
        {0xFFFFFC00, 0x5E79D800, Fp16Kind::Scvtf},
        {0xFFFFFC00, 0x7E79D800, Fp16Kind::Ucvtf},
        {0xFFFFFC00, 0x1EE0C000, Fp16Kind::Fabs},
        {0xFFFFFC00, 0x1EE14000, Fp16Kind::Fneg},
        {0xFFE0FC00, 0x1EE06800, Fp16Kind::Fmaxnm},
        {0xFFE0FC00, 0x1EE07800, Fp16Kind::Fminnm},
    };
    for (const auto& p : pats) {
        if ((w & p.mask) != p.value) continue;
        o.op = Op::Fp16;
        o.hk = p.hk;
        o.rd = bits(w, 4, 0);
        o.rn = bits(w, 9, 5);
        o.rm = bits(w, 20, 16);
        switch (p.hk) {
            case Fp16Kind::FmovImm: o.imm = int32_t(bits(w, 20, 13)); break;
            case Fp16Kind::FcvtToH:
            case Fp16Kind::FcvtFromH: o.size = bits(w, 23, 23) ? 1 : 2; break;  // 2=S, 1=D
            case Fp16Kind::Fmadd:
            case Fp16Kind::Fmsub:
            case Fp16Kind::Fnmadd:
            case Fp16Kind::Fnmsub:
                // The 4-operand FP16 FMA with all four vector registers = h31 is the
                // signature of embedded Apple data tables (e.g. the FSE literal-mask
                // table in libcompression reads {2^n-1, 0}; 0x1fffffff = entry 29).
                // ds-exec must never lower data words inside executable sections, and a
                // legitimate compiler never emits h31 as all four operands of an FMA.
                // Treat that exact shape as data words (not an instruction).
                if (o.rd == 31 && o.rn == 31 && o.rm == 31 && (o.rs = bits(w, 14, 10)) == 31) {
                    o.rs = bits(w, 14, 10);
                    return false;
                }
                o.rs = bits(w, 14, 10); o.sub = bits(w, 15, 15); break;
            case Fp16Kind::Fcsel: o.imm = int32_t(bits(w, 15, 12)); break;
            case Fp16Kind::Fccmp: o.imm = int32_t(bits(w, 15, 12)); o.rs = bits(w, 3, 0); break;
            case Fp16Kind::Fcmp:
            case Fp16Kind::Fcmp0: o.mul = bits(w, 4, 4); break;  // signalling bit
            default: break;
        }
        return true;
    }
    // FCMP/FCMPE (scalar, register and #0.0): reg = 0x1EE02000, #0.0 = 0x1EE02008
    // (bit 3 differentiates; e-bit is bit 4; zero-form also has rm == 0).
    if ((w & 0xFFE0FC00) == 0x1EE02000) {
        o.op = Op::Fp16;
        o.rd = bits(w, 4, 0);
        o.rn = bits(w, 9, 5);
        o.rm = bits(w, 20, 16);
        o.mul = bits(w, 4, 4);
        o.hk = bits(w, 3, 3) ? Fp16Kind::Fcmp0 : Fp16Kind::Fcmp;
        return true;
    }
    return false;
}
}

bool decode(uint32_t w, Insn& out) {
    Insn o;
    bool ok = false;
    if ((w & 0xFFE0001F) == 0xD4000001) {  // SVC #imm16
        o.op = Op::Svc;
        o.imm = int32_t(bits(w, 20, 5));
        ok = true;
    } else if (w == 0xD65F0BFF || w == 0xD65F0FFF) {  // RETAA / RETAB
        o.op = Op::RetAuth;
        o.key = w == 0xD65F0BFF ? PacKey::IA : PacKey::IB;
        ok = true;
    } else if (w == 0xDAC1A3FE || w == 0xDAC1A7FE) {  // PACIASPPC / PACIBSPPC (FEAT_PAuth_LR)
        o.op = Op::PacSign;
        o.key = w == 0xDAC1A3FE ? PacKey::IA : PacKey::IB;
        o.rd = 30;
        o.mod = ModSrc::SpPc;
        ok = true;
    } else if ((w & 0xFFC0001F) == 0xF380001F) {  // AUTIASPPC/AUTIBSPPC <label>: bit 21 = key B, label = pc - imm16*4
        o.op = Op::PacAuth;
        o.key = bits(w, 21, 21) ? PacKey::IB : PacKey::IA;
        o.rd = 30;
        o.mod = ModSrc::SpPc;
        o.imm = int32_t(bits(w, 20, 5)) * 4;
        ok = true;
    } else if ((w & 0xFFC0001F) == 0x5500001F) {  // RETAASPPC/RETABSPPC <label>
        o.op = Op::RetAuth;
        o.key = bits(w, 21, 21) ? PacKey::IB : PacKey::IA;
        o.mod = ModSrc::SpPc;
        o.imm = int32_t(bits(w, 20, 5)) * 4;
        ok = true;
    } else if ((w & 0xFFFFF01F) == 0xD503201F) {
        ok = decode_hint(w, o);
    } else if ((w & 0xFFFF8000) == 0xDAC10000) {
        ok = decode_pac_1src(w, o);
    } else if ((w & 0xFFE0FC00) == 0x9AC03000) {  // PACGA Xd, Xn, Xm|SP
        o.op = Op::PacGa;
        o.rd = bits(w, 4, 0);
        o.rn = bits(w, 9, 5);
        o.rm = bits(w, 20, 16);
        ok = true;
    } else if ((w & 0xFEDFF800) == 0xD61F0800) {
        ok = decode_branch(w, o);
    } else if ((w & 0xFFFFFFE0) == 0xD53BD040) {  // MRS Xt, TPIDR_EL0 (op2=2)
        o.op = Op::MrsTpidr;
        o.rd = bits(w, 4, 0);
        ok = true;
    } else if ((w & 0xFFFFFFE0) == 0xD53BD060) {  // MRS Xt, TPIDRRO_EL0 (op0=3 op1=3 CRn=13 CRm=0 op2=3)
        o.op = Op::Mrs;
        o.rd = bits(w, 4, 0);
        ok = true;
    } else if ((w & 0x3F200C00) == 0x19000000 && bits(w, 23, 22) != 0 &&
               !(bits(w, 31, 30) == 3 && bits(w, 23, 22) != 1) && !(bits(w, 31, 30) == 2 && bits(w, 23, 22) == 3)) {
        o.op = Op::Ldapur;  // size 011001 opc 0 imm9 00 Rn Rt, opc != 00 (00 is STLUR)
        o.size = bits(w, 31, 30);
        o.sext = bits(w, 23, 22) == 1 ? 0 : bits(w, 23, 22) == 2 ? 1 : 2;
        const int32_t i9 = int32_t(bits(w, 20, 12));
        o.imm = i9 >= 256 ? i9 - 512 : i9;
        o.rn = bits(w, 9, 5);
        o.rd = bits(w, 4, 0);
        ok = true;
    } else if ((w & 0xBFE0E000) == 0x9A002000) {  // ADDPT/SUBPT Xd|SP, Xn|SP, Xm{, LSL #imm3}
        o.op = Op::Cpa;
        o.sub = bits(w, 30, 30);
        o.imm = int32_t(bits(w, 12, 10));
        o.rm = bits(w, 20, 16);
        o.rn = bits(w, 9, 5);
        o.rd = bits(w, 4, 0);
        ok = true;
    } else if ((w & 0xFFE00000) == 0x9B600000) {  // MADDPT/MSUBPT
        o.op = Op::Cpa;
        o.mul = true;
        o.sub = bits(w, 15, 15);
        o.rm = bits(w, 20, 16);
        o.rs = bits(w, 14, 10);
        o.rn = bits(w, 9, 5);
        o.rd = bits(w, 4, 0);
        ok = true;
    } else if ((w & 0x9FE0FC00) == 0x0E809400 || (w & 0x9FC0F400) == 0x0F80E000) {
        // UDOT/SDOT Vd.<2S|4S>, Vn.<8B|16B>, Vm.<8B|16B>   : 0 Q U 01110 10 0 Rm 100101 Rn Rd
        // UDOT/SDOT Vd.<2S|4S>, Vn.<8B|16B>, Vm.4B[H:L]     : 0 Q U 01111 10 L M Rm 1110 H 0 Rn Rd
        o.op = Op::Dot;
        o.q = bits(w, 30, 30);
        o.sgn = !bits(w, 29, 29);
        o.rm = bits(w, 20, 16);
        o.rn = bits(w, 9, 5);
        o.rd = bits(w, 4, 0);
        if (bits(w, 24, 24)) o.lane = int8_t((bits(w, 11, 11) << 1) | bits(w, 21, 21));
        ok = true;
    } else if ((w & 0xFF200400) == 0xF8200400) {
        ok = decode_ldraa(w, o);
    } else if (decode_fp16(w, o)) {
        ok = true;
    } else if (decode_fp16v(w, o)) {
        ok = true;
    } else {
        ok = decode_lse(w, o);
    }
    if (ok) out = o;
    return ok;
}

bool is_known_unsupported(uint32_t w) {
    // LDAPR{,B,H}: size 111 0 00 1 0 1 11111 1 100 00 Rn Rt (A=1,R=0,Rs=11111,o3=1,opc=100)
    return (w & 0x3FFFFC00) == 0x38BFC000;
}

int64_t TrapTable::add(uint64_t pc, uint32_t word) {
    const uint32_t i = n_.load(std::memory_order_relaxed);
    if (i < kMax) {
        e_[i] = TrapEntry{pc, word, 0};
        n_.store(i + 1, std::memory_order_release);
        return i;
    }
    while (overflow_lock_.test_and_set(std::memory_order_acquire)) {}
    overflow_[pc] = TrapEntry{pc, word, 0};
    overflow_lock_.clear(std::memory_order_release);
    return kOverflowIdx;
}

const TrapEntry* TrapTable::find(uint32_t idx, uint64_t pc) const {
    if (idx == kOverflowIdx) {
        while (overflow_lock_.test_and_set(std::memory_order_acquire)) {}
        auto it = overflow_.find(pc);
        const TrapEntry* r = it == overflow_.end() ? nullptr : &it->second;  // node-stable
        overflow_lock_.clear(std::memory_order_release);
        return r;
    }
    if (idx >= n_.load(std::memory_order_acquire)) return nullptr;
    return e_[idx].pc == pc ? &e_[idx] : nullptr;
}

TrapTable& trap_table() {
    static TrapTable t;
    return t;
}

bool lower_range(uint32_t* begin, uint32_t* end, const LowerPolicy& policy, LowerCounts& counts,
                 std::string* err) {
    for (uint32_t* p = begin; p < end; ++p) {
        ++counts.words;
        const uint32_t w = *p;
        // Debug point breakpoint (DS_EXEC_FORCE_BRK=<hex pc>): rewrite one chosen instruction
        // to BRK and register it in the trap table so emulate() fires there for a full state dump.
        {
            static const uint64_t fbrk = getenv("DS_EXEC_FORCE_BRK") ? strtoull(getenv("DS_EXEC_FORCE_BRK"), nullptr, 16) : 0;
            if (fbrk && reinterpret_cast<uint64_t>(p) == fbrk) {
                const int64_t idx = trap_table().add(fbrk, w);
                if (idx >= 0) {
                    *p = brk_word(static_cast<uint32_t>(idx));
                    ++counts.patched[0];
                } else {
                    *p = w;
                }
                continue;
            }
        }
        Insn in;
        if (!decode(w, in)) {
            if (is_known_unsupported(w) && policy.ldapr) {
                // LDAPR{,B,H} -> LDAR{,B,H}: same size/Rn/Rt fields, acquire RCsc instead of RCpc.
                // LDAR encodings: size 001000 1 1 0 11111 1 11111 Rn Rt (0x08DFFC00 | size<<30).
                *p = 0x08DFFC00u | (w & 0xC0000000u) | (w & 0x3FFu);
                ++counts.ldapr_rewritten;
            } else if (is_known_unsupported(w)) {
                ++counts.unsupported_seen;
            }
            continue;
        }
        const bool want = in.op == Op::Svc ? policy.svc
                          : in.op == Op::Mrs || in.op == Op::MrsTpidr || in.op == Op::Ldapur ? policy.sysreg
                          : in.op == Op::Cpa ? policy.pac
                          : in.op == Op::Dot || in.op == Op::Fp16 || in.op == Op::Fp16V ? policy.simd
                          : (in.op == Op::Cas || in.op == Op::Casp || in.op == Op::Rmw) ? policy.lse
                          : policy.pac;
        if (!want) continue;
        if (in.op == Op::PacStrip && policy.strip_mask) {
            const uint32_t w2 = encode_and_imm64(in.rd, policy.strip_mask);
            if (w2) {
                *p = w2;
                ++counts.patched[static_cast<int>(in.op)];
                ++counts.inlined_strip;
                continue;
            }
        }
        if (in.op == Op::MrsTpidr && policy.trampolines && in.rd != 31) {  // Darwin's cluster hint reads 0 here: mov xd, #0 in place
            *p = 0xD2800000u | in.rd;
            ++counts.patched[static_cast<int>(in.op)];
            ++counts.inlined_strip;
            continue;
        }
        if (policy.trampolines && trampoline_wants(in)) {
            const uint32_t b = trampoline_make(p, w, in, policy.island_skip);
            if (b) {
                if (getenv("DS_EXEC_DUMP")) fprintf(stderr, "ds-exec: lower pc=0x%llx w=0x%08x op=%d -> trampoline\n", (unsigned long long)(uintptr_t)p, w, (int)in.op);
                *p = b;
                ++counts.patched[static_cast<int>(in.op)];
                ++counts.trampolined;
                continue;
            }
        }
        const int64_t idx = trap_table().add(reinterpret_cast<uint64_t>(p), w);
        if (idx < 0) {
            if (err) *err = "ds-exec: trap side table full (65536 distinct words)";
            return false;
        }
        if (getenv("DS_EXEC_DUMP")) fprintf(stderr, "ds-exec: lower pc=0x%llx w=0x%08x op=%d -> brk\n", (unsigned long long)(uintptr_t)p, w, (int)in.op);
        *p = brk_word(static_cast<uint32_t>(idx));
        ++counts.patched[static_cast<int>(in.op)];
    }
    return true;
}

void flush_icache(void* begin, void* end) { __builtin___clear_cache(static_cast<char*>(begin), static_cast<char*>(end)); }

size_t restore_original(uint32_t* dst, const uint32_t* src_pc, size_t n) {
    const uint64_t base = reinterpret_cast<uint64_t>(src_pc);
    size_t restored = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint64_t pc = base + 4ull * i;
        const uint32_t w = dst[i];
        // Only words still in a lowered form are rewritten: a BRK trap word keyed to this pc,
        // or a `b stub` branch.  Everything else (native instructions, in-place PC-independent
        // rewrites, and words of code that was remapped over a lowered range without being
        // lowered) already holds its final form and is left alone.
        if ((w & 0xFFE0001Fu) == 0xD4200000u) {  // brk #idx
            if (const TrapEntry* e = trap_table().find((w >> 5) & 0xFFFF, pc)) {
                dst[i] = e->word;
                ++restored;
            }
        } else if ((w & 0xFC000000u) == 0x14000000u) {  // b stub
            if (const SiteInfo* s = trampoline_lookup(pc)) {
                dst[i] = s->word;
                ++restored;
            }
        }
    }
    return restored;
}

}  // namespace ds::exec
