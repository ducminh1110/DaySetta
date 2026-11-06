// Software pointer authentication for a host without FEAT_PAuth (ds-exec, milestone R2).
//
// Design choices (the brief fixes the model; these are the concrete decisions):
//  * MAC: SipHash-2-4 (128-bit key, 16-byte message = extended pointer || modifier).  It is a
//    keyed PRF, NOT QARMA, so PAC values differ from real hardware; only self-consistency
//    (sign/auth round trip, failure poisoning) is preserved.  Keys are random per process.
//  * Layout ("TBI" model): the PAC occupies bits [54:va_bits]; bit 55 selects the
//    extension; the top byte (63:56) is left untouched.  va_bits defaults to 48: the brief
//    suggested 47, but Linux user pointers here use bit 47 (PIE images load at 0xaaaa........),
//    and a pointer with a set bit inside the PAC field is non-canonical and could never
//    authenticate.  The VA size macOS arm64e userspace uses is UNKNOWN (parameter).
//  * Failure (CPU without FEAT_FPAC): the extended pointer is returned with error code
//    (key B ? 0b10 : 0b01) in bits [54:53]; those bits are never canonical, so any use as an
//    address faults.  Verified only against the Arm ARM description as recalled by the
//    author, not against hardware (host has no PAC): class INFERRED.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ds::exec {

enum class PacKey : uint8_t { IA, IB, DA, DB, GA };

struct PacKeys {
    uint64_t lo[5];
    uint64_t hi[5];
};

struct AuthResult {
    uint64_t value;
    bool ok;
};

// SipHash-2-4, exposed for the test vector.
uint64_t siphash24(uint64_t k0, uint64_t k1, const uint8_t* data, size_t len);

class Pac {
public:
    explicit Pac(unsigned va_bits = 48);  // random keys from getrandom(2); aborts if unavailable
    Pac(unsigned va_bits, const PacKeys& keys);

    unsigned va_bits() const { return va_bits_; }
    uint64_t field_mask() const { return field_; }  // bits [54:va_bits]

    uint64_t sign(PacKey key, uint64_t ptr, uint64_t modifier) const;
    AuthResult auth(PacKey key, uint64_t ptr, uint64_t modifier) const;
    uint64_t strip(uint64_t ptr) const;  // XPACI/XPACD
    uint64_t pacga(uint64_t value, uint64_t modifier) const;
    bool canonical(uint64_t ptr) const { return (ptr & field_) == extension(ptr); }

    // ptrauth_blend_discriminator: address with its top 16 bits replaced by `diversity`.
    static uint64_t blend(uint64_t addr, uint16_t diversity) {
        return (addr & 0x0000FFFFFFFFFFFFull) | (uint64_t(diversity) << 48);
    }

private:
    uint64_t extension(uint64_t ptr) const { return (ptr >> 55) & 1 ? field_ : 0; }
    uint64_t ext_ptr(uint64_t ptr) const { return (ptr & ~field_) | extension(ptr); }
    uint64_t mac(PacKey key, uint64_t extptr, uint64_t modifier) const;

    unsigned va_bits_;
    uint64_t field_;
    PacKeys keys_;
};

}  // namespace ds::exec
