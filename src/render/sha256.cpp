#include "daysetta/render.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace ds::render {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

void compress(std::uint32_t state[8], const std::uint8_t block[64]) {
    std::uint32_t words[64];
    for (unsigned i = 0; i < 16; ++i) {
        words[i] = (static_cast<std::uint32_t>(block[i * 4u]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4u + 1u]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4u + 2u]) << 8) |
                   static_cast<std::uint32_t>(block[i * 4u + 3u]);
    }
    for (unsigned i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotate_right(words[i - 15u], 7) ^
                                 rotate_right(words[i - 15u], 18) ^
                                 (words[i - 15u] >> 3);
        const std::uint32_t s1 = rotate_right(words[i - 2u], 17) ^
                                 rotate_right(words[i - 2u], 19) ^
                                 (words[i - 2u] >> 10);
        words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (unsigned i = 0; i < 64; ++i) {
        const std::uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                                   rotate_right(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temp1 =
            h + sum1 + choice + kRoundConstants[i] + words[i];
        const std::uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                                   rotate_right(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

}  // namespace

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) {
    std::uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };

    const std::uint64_t bit_length =
        static_cast<std::uint64_t>(data.size()) * 8u;
    std::size_t offset = 0;
    while (data.size() - offset >= 64u) {
        compress(state, data.data() + offset);
        offset += 64u;
    }

    std::uint8_t tail[128] = {};
    const std::size_t remaining = data.size() - offset;
    if (remaining != 0) {
        std::memcpy(tail, data.data() + offset, remaining);
    }
    tail[remaining] = 0x80u;
    const std::size_t padded_size = remaining < 56u ? 64u : 128u;
    for (unsigned i = 0; i < 8; ++i) {
        tail[padded_size - 1u - i] =
            static_cast<std::uint8_t>(bit_length >> (i * 8u));
    }
    compress(state, tail);
    if (padded_size == 128u) {
        compress(state, tail + 64u);
    }

    std::array<std::uint8_t, 32> digest{};
    for (unsigned i = 0; i < 8; ++i) {
        digest[i * 4u] = static_cast<std::uint8_t>(state[i] >> 24);
        digest[i * 4u + 1u] = static_cast<std::uint8_t>(state[i] >> 16);
        digest[i * 4u + 2u] = static_cast<std::uint8_t>(state[i] >> 8);
        digest[i * 4u + 3u] = static_cast<std::uint8_t>(state[i]);
    }
    return digest;
}

std::string sha256_hex(std::span<const std::uint8_t> data) {
    static constexpr char kHex[] = "0123456789abcdef";
    const std::array<std::uint8_t, 32> digest = sha256(data);
    std::string result;
    result.reserve(64);
    for (const std::uint8_t byte : digest) {
        result.push_back(kHex[byte >> 4]);
        result.push_back(kHex[byte & 0x0f]);
    }
    return result;
}

}  // namespace ds::render
