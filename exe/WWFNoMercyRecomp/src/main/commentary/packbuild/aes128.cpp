// [commentary2 2026-11-18] See aes128.h. Straight from FIPS-197: the S-box computed (multiplicative inverse in GF(2^8)
// + the affine map), key expansion, the four round steps and their inverses. Byte-wise with looked-up products: small
// and plain, fast enough for a disc's few hundred MB once.
#include "aes128.h"

#include <cstring>

namespace comm_disc {

namespace {

uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
uint8_t mul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) { if (b & 1) r ^= a; a = xtime(a); b >>= 1; }
    return r;
}

struct Tables {
    uint8_t sbox[256], inv[256], m2[256], m3[256], m9[256], m11[256], m13[256], m14[256];   // (the products, looked up)
    Tables() {
        for (int x = 0; x < 256; x++) {
            m2[x] = mul((uint8_t)x, 2); m3[x] = mul((uint8_t)x, 3); m9[x] = mul((uint8_t)x, 9);
            m11[x] = mul((uint8_t)x, 11); m13[x] = mul((uint8_t)x, 13); m14[x] = mul((uint8_t)x, 14);
        }
        for (int x = 0; x < 256; x++) {
            uint8_t v = 0;   // the multiplicative inverse (0 for 0)
            for (int y = 1; y < 256 && x; y++) if (mul((uint8_t)x, (uint8_t)y) == 1) { v = (uint8_t)y; break; }
            uint8_t s = v;
            for (int k = 1; k < 5; k++) s ^= (uint8_t)((v << k) | (v >> (8 - k)));
            s ^= 0x63;
            sbox[x] = s;
            inv[s] = (uint8_t)x;
        }
    }
};
const Tables& tables() { static const Tables t; return t; }

}  // namespace

Aes128::Aes128(const uint8_t key[16]) {
    const Tables& t = tables();
    std::memcpy(rk_, key, 16);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t w[4];
        std::memcpy(w, rk_ + i - 4, 4);
        if (i % 16 == 0) {
            const uint8_t r = w[0];
            w[0] = (uint8_t)(t.sbox[w[1]] ^ rcon); w[1] = t.sbox[w[2]]; w[2] = t.sbox[w[3]]; w[3] = t.sbox[r];
            rcon = xtime(rcon);
        }
        for (int k = 0; k < 4; k++) rk_[i + k] = (uint8_t)(rk_[i - 16 + k] ^ w[k]);
    }
}

void Aes128::encrypt_block(const uint8_t in[16], uint8_t out[16]) const {
    const Tables& t = tables();
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = (uint8_t)(in[i] ^ rk_[i]);
    for (int round = 1; round <= 10; round++) {
        uint8_t u[16];
        for (int i = 0; i < 16; i++) u[i] = t.sbox[s[i]];
        // ShiftRows (column-major state: byte r + 4c)
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) s[r + 4 * c] = u[r + 4 * ((c + r) & 3)];
        if (round < 10)
            for (int c = 0; c < 4; c++) {
                uint8_t* q = s + 4 * c;
                const uint8_t a0 = q[0], a1 = q[1], a2 = q[2], a3 = q[3];
                q[0] = (uint8_t)(t.m2[a0] ^ t.m3[a1] ^ a2 ^ a3);
                q[1] = (uint8_t)(a0 ^ t.m2[a1] ^ t.m3[a2] ^ a3);
                q[2] = (uint8_t)(a0 ^ a1 ^ t.m2[a2] ^ t.m3[a3]);
                q[3] = (uint8_t)(t.m3[a0] ^ a1 ^ a2 ^ t.m2[a3]);
            }
        for (int i = 0; i < 16; i++) s[i] ^= rk_[16 * round + i];
    }
    std::memcpy(out, s, 16);
}

void Aes128::decrypt_block(const uint8_t in[16], uint8_t out[16]) const {
    const Tables& t = tables();
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = (uint8_t)(in[i] ^ rk_[160 + i]);
    for (int round = 9; round >= 0; round--) {
        uint8_t u[16];
        // InvShiftRows, InvSubBytes
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) u[r + 4 * ((c + r) & 3)] = s[r + 4 * c];
        for (int i = 0; i < 16; i++) s[i] = (uint8_t)(t.inv[u[i]] ^ rk_[16 * round + i]);
        if (round > 0)
            for (int c = 0; c < 4; c++) {
                uint8_t* q = s + 4 * c;
                const uint8_t a0 = q[0], a1 = q[1], a2 = q[2], a3 = q[3];
                q[0] = (uint8_t)(t.m14[a0] ^ t.m11[a1] ^ t.m13[a2] ^ t.m9[a3]);
                q[1] = (uint8_t)(t.m9[a0] ^ t.m14[a1] ^ t.m11[a2] ^ t.m13[a3]);
                q[2] = (uint8_t)(t.m13[a0] ^ t.m9[a1] ^ t.m14[a2] ^ t.m11[a3]);
                q[3] = (uint8_t)(t.m11[a0] ^ t.m13[a1] ^ t.m9[a2] ^ t.m14[a3]);
            }
    }
    std::memcpy(out, s, 16);
}

void Aes128::cbc_decrypt(uint8_t* data, size_t n, const uint8_t iv[16]) const {
    uint8_t prev[16], cur[16];
    std::memcpy(prev, iv, 16);
    for (size_t at = 0; at + 16 <= n; at += 16) {
        std::memcpy(cur, data + at, 16);
        decrypt_block(cur, data + at);
        for (int i = 0; i < 16; i++) data[at + (size_t)i] ^= prev[i];
        std::memcpy(prev, cur, 16);
    }
}

}  // namespace comm_disc
