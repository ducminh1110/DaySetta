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
}  // namespace

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
                          : in.op == Op::Dot ? policy.simd
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
