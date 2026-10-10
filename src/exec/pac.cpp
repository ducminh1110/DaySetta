#include "pac.hpp"

#include <sys/random.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ds::exec {

namespace {
inline uint64_t rotl(uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

struct Sip {
    uint64_t v0, v1, v2, v3;
    void round() {
        v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32);
        v2 += v3; v3 = rotl(v3, 16); v3 ^= v2;
        v0 += v3; v3 = rotl(v3, 21); v3 ^= v0;
        v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32);
    }
};

uint64_t load64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);  // little-endian host (aarch64, x86_64)
    return v;
}
}  // namespace

uint64_t siphash24(uint64_t k0, uint64_t k1, const uint8_t* data, size_t len) {
    Sip s{k0 ^ 0x736f6d6570736575ull, k1 ^ 0x646f72616e646f6dull, k0 ^ 0x6c7967656e657261ull,
          k1 ^ 0x7465646279746573ull};
    const uint8_t* end = data + (len & ~size_t(7));
    for (; data != end; data += 8) {
        uint64_t m = load64(data);
        s.v3 ^= m;
        s.round(); s.round();
        s.v0 ^= m;
    }
    uint64_t b = uint64_t(len) << 56;
    for (size_t i = 0; i < (len & 7); ++i) b |= uint64_t(data[i]) << (8 * i);
    s.v3 ^= b;
    s.round(); s.round();
    s.v0 ^= b;
    s.v2 ^= 0xff;
    s.round(); s.round(); s.round(); s.round();
    return s.v0 ^ s.v1 ^ s.v2 ^ s.v3;
}

Pac::Pac(unsigned va_bits) : Pac(va_bits, [] {
    PacKeys k;
    if (getrandom(&k, sizeof k, 0) != ssize_t(sizeof k)) {
        std::fprintf(stderr, "ds-exec: getrandom failed; refusing to run with predictable PAC keys\n");
        std::abort();
    }
    return k;
}()) {}

Pac::Pac(unsigned va_bits, const PacKeys& keys)
    : va_bits_(va_bits), field_(((uint64_t(1) << (55 - va_bits)) - 1) << va_bits), keys_(keys) {}

uint64_t Pac::mac(PacKey key, uint64_t extptr, uint64_t modifier) const {
    uint8_t msg[16];
    std::memcpy(msg, &extptr, 8);
    std::memcpy(msg + 8, &modifier, 8);
    const int k = static_cast<int>(key);
    return siphash24(keys_.lo[k], keys_.hi[k], msg, sizeof msg);
}

uint64_t Pac::sign(PacKey key, uint64_t ptr, uint64_t modifier) const {
    uint64_t pac = (mac(key, ext_ptr(ptr), modifier) << va_bits_) & field_;
    // Input whose extension bits were not canonical gets one PAC bit flipped (bit 54) so
    // that authentication of it can never succeed.
    if (!canonical(ptr)) pac ^= uint64_t(1) << 54;
    return (ptr & ~field_) | pac;
}

AuthResult Pac::auth(PacKey key, uint64_t ptr, uint64_t modifier) const {
    const uint64_t ext = ext_ptr(ptr);
    const uint64_t pac = (mac(key, ext, modifier) << va_bits_) & field_;
    if ((ptr & field_) == pac) return {ext, true};
    const uint64_t code = (key == PacKey::IB || key == PacKey::DB) ? 2 : 1;
    return {(ext & ~(uint64_t(3) << 53)) | (code << 53), false};
}

uint64_t Pac::strip(uint64_t ptr) const { return ext_ptr(ptr); }

uint64_t Pac::pacga(uint64_t value, uint64_t modifier) const {
    return mac(PacKey::GA, value, modifier) & 0xFFFFFFFF00000000ull;
}

}  // namespace ds::exec
