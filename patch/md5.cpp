#include "md5.h"
#include <cmath>
#include <cstring>

namespace mpml {
namespace Detail {
inline uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

void block(uint32_t st[4], const uint8_t* p, const uint32_t* K) {
    uint32_t M[16];
    for (int i = 0; i < 16; ++i)
        M[i] = uint32_t(p[i * 4]) | uint32_t(p[i * 4 + 1]) << 8 | uint32_t(p[i * 4 + 2]) << 16 |
               uint32_t(p[i * 4 + 3]) << 24;
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3 * i + 5) % 16; }
        else             { f = c ^ (b | ~d);       g = (7 * i) % 16; }
        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rol(a + f + K[i] + M[g], S[i]);
        a = tmp;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}
} // namespace Detail

using namespace Detail;

std::array<uint8_t, 16> md5(const uint8_t* data, size_t len) {
    uint32_t K[64];
    for (int i = 0; i < 64; ++i)
        K[i] = uint32_t(uint64_t(std::fabs(std::sin(double(i + 1))) * 4294967296.0));

    uint32_t st[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    size_t full = len / 64;
    for (size_t i = 0; i < full; ++i) block(st, data + i * 64, K);

    uint8_t tail[128] = {0};
    size_t rem = len - full * 64;
    if (rem) std::memcpy(tail, data + full * 64, rem);
    tail[rem] = 0x80;
    size_t tailLen = (rem < 56) ? 64 : 128;
    uint64_t bits = uint64_t(len) * 8;
    for (int i = 0; i < 8; ++i) tail[tailLen - 8 + i] = uint8_t(bits >> (8 * i));
    block(st, tail, K);
    if (tailLen == 128) block(st, tail + 64, K);

    std::array<uint8_t, 16> out;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = uint8_t(st[i] >> (8 * j));
    return out;
}
} // namespace mpml
