#include "crypto.h"

#include <cstring>

namespace crypto {
namespace {

const int kRounds = 14;  // AES-256

struct Tables {
    uint8_t sbox[256];
    uint8_t inv[256];
    Tables() {
        // Generate the S-box from the multiplicative inverse in GF(2^8) + affine transform.
        uint8_t p = 1, q = 1;
        do {
            p = static_cast<uint8_t>(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
            q ^= static_cast<uint8_t>(q << 1);
            q ^= static_cast<uint8_t>(q << 2);
            q ^= static_cast<uint8_t>(q << 4);
            if (q & 0x80) q ^= 0x09;
            uint8_t x = static_cast<uint8_t>(q ^ rotl(q, 1) ^ rotl(q, 2) ^ rotl(q, 3) ^ rotl(q, 4));
            sbox[p] = static_cast<uint8_t>(x ^ 0x63);
        } while (p != 1);
        sbox[0] = 0x63;
        for (int i = 0; i < 256; ++i) inv[sbox[i]] = static_cast<uint8_t>(i);
    }
    static uint8_t rotl(uint8_t x, int s) { return static_cast<uint8_t>((x << s) | (x >> (8 - s))); }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

inline uint8_t xtime(uint8_t x) { return static_cast<uint8_t>((x << 1) ^ (((x >> 7) & 1) * 0x1B)); }

inline uint8_t mul(uint8_t x, uint8_t y) {
    uint8_t r = 0;
    while (y) {
        if (y & 1) r ^= x;
        x = xtime(x);
        y >>= 1;
    }
    return r;
}

// state[c][r]: column c, row r (input byte i maps to column i/4, row i%4)
typedef uint8_t State[4][4];

void addRoundKey(int round, State& s, const uint8_t* rk) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) s[c][r] ^= rk[round * 16 + c * 4 + r];
}

void subBytes(State& s, const uint8_t* box) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) s[c][r] = box[s[c][r]];
}

void shiftRows(State& s) {
    uint8_t t = s[0][1];
    s[0][1] = s[1][1]; s[1][1] = s[2][1]; s[2][1] = s[3][1]; s[3][1] = t;
    t = s[0][2]; s[0][2] = s[2][2]; s[2][2] = t;
    t = s[1][2]; s[1][2] = s[3][2]; s[3][2] = t;
    t = s[0][3];
    s[0][3] = s[3][3]; s[3][3] = s[2][3]; s[2][3] = s[1][3]; s[1][3] = t;
}

void invShiftRows(State& s) {
    uint8_t t = s[3][1];
    s[3][1] = s[2][1]; s[2][1] = s[1][1]; s[1][1] = s[0][1]; s[0][1] = t;
    t = s[0][2]; s[0][2] = s[2][2]; s[2][2] = t;
    t = s[1][2]; s[1][2] = s[3][2]; s[3][2] = t;
    t = s[0][3];
    s[0][3] = s[1][3]; s[1][3] = s[2][3]; s[2][3] = s[3][3]; s[3][3] = t;
}

void mixColumns(State& s) {
    for (int c = 0; c < 4; ++c) {
        uint8_t a0 = s[c][0], a1 = s[c][1], a2 = s[c][2], a3 = s[c][3];
        uint8_t all = static_cast<uint8_t>(a0 ^ a1 ^ a2 ^ a3);
        s[c][0] ^= static_cast<uint8_t>(all ^ xtime(static_cast<uint8_t>(a0 ^ a1)));
        s[c][1] ^= static_cast<uint8_t>(all ^ xtime(static_cast<uint8_t>(a1 ^ a2)));
        s[c][2] ^= static_cast<uint8_t>(all ^ xtime(static_cast<uint8_t>(a2 ^ a3)));
        s[c][3] ^= static_cast<uint8_t>(all ^ xtime(static_cast<uint8_t>(a3 ^ a0)));
    }
}

void invMixColumns(State& s) {
    for (int c = 0; c < 4; ++c) {
        uint8_t a = s[c][0], b = s[c][1], d = s[c][2], e = s[c][3];
        s[c][0] = static_cast<uint8_t>(mul(a, 0x0e) ^ mul(b, 0x0b) ^ mul(d, 0x0d) ^ mul(e, 0x09));
        s[c][1] = static_cast<uint8_t>(mul(a, 0x09) ^ mul(b, 0x0e) ^ mul(d, 0x0b) ^ mul(e, 0x0d));
        s[c][2] = static_cast<uint8_t>(mul(a, 0x0d) ^ mul(b, 0x09) ^ mul(d, 0x0e) ^ mul(e, 0x0b));
        s[c][3] = static_cast<uint8_t>(mul(a, 0x0b) ^ mul(b, 0x0d) ^ mul(d, 0x09) ^ mul(e, 0x0e));
    }
}

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

} // namespace

Aes256::Aes256(const uint8_t key[32]) {
    const uint8_t* sbox = tables().sbox;
    static const uint8_t rcon[8] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40};
    std::memcpy(roundKeys_, key, 32);
    for (int i = 8; i < 4 * (kRounds + 1); ++i) {
        uint8_t t[4];
        std::memcpy(t, &roundKeys_[(i - 1) * 4], 4);
        if (i % 8 == 0) {
            uint8_t first = t[0];
            t[0] = t[1]; t[1] = t[2]; t[2] = t[3]; t[3] = first;
            for (uint8_t& b : t) b = sbox[b];
            t[0] ^= rcon[i / 8];
        } else if (i % 8 == 4) {
            for (uint8_t& b : t) b = sbox[b];
        }
        for (int j = 0; j < 4; ++j)
            roundKeys_[i * 4 + j] = static_cast<uint8_t>(roundKeys_[(i - 8) * 4 + j] ^ t[j]);
    }
}

void Aes256::encryptBlock(const uint8_t in[16], uint8_t out[16]) const {
    const uint8_t* sbox = tables().sbox;
    State s;
    std::memcpy(s, in, 16);
    addRoundKey(0, s, roundKeys_);
    for (int round = 1; round < kRounds; ++round) {
        subBytes(s, sbox);
        shiftRows(s);
        mixColumns(s);
        addRoundKey(round, s, roundKeys_);
    }
    subBytes(s, sbox);
    shiftRows(s);
    addRoundKey(kRounds, s, roundKeys_);
    std::memcpy(out, s, 16);
}

void Aes256::decryptBlock(const uint8_t in[16], uint8_t out[16]) const {
    const uint8_t* inv = tables().inv;
    State s;
    std::memcpy(s, in, 16);
    addRoundKey(kRounds, s, roundKeys_);
    for (int round = kRounds - 1; round > 0; --round) {
        invShiftRows(s);
        subBytes(s, inv);
        addRoundKey(round, s, roundKeys_);
        invMixColumns(s);
    }
    invShiftRows(s);
    subBytes(s, inv);
    addRoundKey(0, s, roundKeys_);
    std::memcpy(out, s, 16);
}

std::vector<uint8_t> aesEcbEncrypt(const uint8_t key[32], const uint8_t* data, size_t len) {
    Aes256 aes(key);
    const size_t pad = 16 - (len % 16);
    std::vector<uint8_t> buf(len + pad, static_cast<uint8_t>(pad));
    if (len) std::memcpy(buf.data(), data, len);
    for (size_t off = 0; off < buf.size(); off += 16) aes.encryptBlock(&buf[off], &buf[off]);
    return buf;
}

bool aesEcbDecrypt(const uint8_t key[32], const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    if (len == 0 || len % 16 != 0) return false;
    Aes256 aes(key);
    out.resize(len);
    for (size_t off = 0; off < len; off += 16) aes.decryptBlock(data + off, &out[off]);
    const uint8_t pad = out.back();
    if (pad == 0 || pad > 16 || pad > out.size()) return false;
    for (size_t i = out.size() - pad; i < out.size(); ++i)
        if (out[i] != pad) return false;
    out.resize(out.size() - pad);
    return true;
}

std::string base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t v = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += kB64[v & 63];
    }
    if (i < len) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) v |= static_cast<uint32_t>(data[i + 1]) << 8;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += (i + 1 < len) ? kB64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

bool base64Decode(const char* text, size_t len, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(len / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    size_t padding = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = text[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '=') {
            ++padding;
            continue;
        }
        if (padding) return false;  // data after padding
        int v = b64Value(c);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return padding <= 2;
}

} // namespace crypto

namespace crypto {
namespace {

const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256Block(uint32_t h[8], const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = k + s1 + ch + kSha256K[i] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + mj;
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
}

} // namespace

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= len; i += 64) sha256Block(h, data + i);
    uint8_t tail[128] = {0};
    const size_t rest = len - i;
    if (rest) std::memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    const size_t blocks = rest + 9 > 64 ? 2 : 1;
    const uint64_t bits = static_cast<uint64_t>(len) * 8;
    for (int b = 0; b < 8; ++b) tail[blocks * 64 - 1 - b] = static_cast<uint8_t>(bits >> (8 * b));
    for (size_t b = 0; b < blocks; ++b) sha256Block(h, tail + b * 64);
    for (int b = 0; b < 8; ++b) {
        out[b * 4] = static_cast<uint8_t>(h[b] >> 24);
        out[b * 4 + 1] = static_cast<uint8_t>(h[b] >> 16);
        out[b * 4 + 2] = static_cast<uint8_t>(h[b] >> 8);
        out[b * 4 + 3] = static_cast<uint8_t>(h[b]);
    }
}

std::string sha256Hex(const uint8_t* data, size_t len) {
    uint8_t d[32];
    sha256(data, len, d);
    static const char* const hexDigits = "0123456789abcdef";
    std::string s;
    for (uint8_t b : d) {
        s += hexDigits[b >> 4];
        s += hexDigits[b & 15];
    }
    return s;
}

} // namespace crypto
