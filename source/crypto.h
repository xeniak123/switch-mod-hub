// AES-256 (ECB) and Base64 helpers for the PC-style Team Cherry save container, plus SHA-256.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crypto {

class Aes256 {
public:
    explicit Aes256(const uint8_t key[32]);
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) const;
    void decryptBlock(const uint8_t in[16], uint8_t out[16]) const;

private:
    uint8_t roundKeys_[240];
};

// ECB with PKCS#7 padding.
std::vector<uint8_t> aesEcbEncrypt(const uint8_t key[32], const uint8_t* data, size_t len);
// Returns false if the length is not a multiple of 16 or the padding is invalid.
bool aesEcbDecrypt(const uint8_t key[32], const uint8_t* data, size_t len, std::vector<uint8_t>& out);

std::string base64Encode(const uint8_t* data, size_t len);
// Strict decoder (whitespace is skipped, anything else outside the alphabet fails).
bool base64Decode(const char* text, size_t len, std::vector<uint8_t>& out);

// SHA-256 (FIPS 180-4), e.g. to verify downloads.
void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
std::string sha256Hex(const uint8_t* data, size_t len);

} // namespace crypto
