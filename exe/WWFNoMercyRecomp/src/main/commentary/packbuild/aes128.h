// [commentary2 2026-11-18] AES-128 (FIPS-197), decryption in CBC mode only (and one-block encryption, for the tests):
// what a plain Wii disc image needs once the player has supplied the key (wii_disc.cpp). Our own small implementation;
// the test (packbuild/test/wii_crypt_test.cpp) checks it on FIPS-197's example and against Python's cryptography.
#pragma once
#include <cstddef>
#include <cstdint>

namespace comm_disc {

class Aes128 {
public:
    explicit Aes128(const uint8_t key[16]);
    void encrypt_block(const uint8_t in[16], uint8_t out[16]) const;
    void decrypt_block(const uint8_t in[16], uint8_t out[16]) const;
    // CBC decryption of n bytes (a multiple of 16) in place; iv is the 16 bytes before the first block
    void cbc_decrypt(uint8_t* data, size_t n, const uint8_t iv[16]) const;
private:
    uint8_t rk_[176];   // the 11 round keys
};

}  // namespace comm_disc
