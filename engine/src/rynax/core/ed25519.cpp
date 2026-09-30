#include "rynax/core/ed25519.h"

#include <cstring>
#include <mutex>

namespace rynax::ed25519 {

// ---------------------------------------------------------------- SHA-512 (FIPS 180-4)

namespace {

// The fractional parts of the cube roots of the first 80 primes (and square roots of the first 8).
const uint64_t kRound[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};
const uint64_t kStart[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

uint64_t rotr(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

struct Sha512 {
    uint64_t h[8];
    uint8_t block[128];
    size_t used = 0;
    uint64_t length = 0;

    Sha512() { std::memcpy(h, kStart, sizeof h); }

    void compress(const uint8_t* p) {
        uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 8; ++j)
                w[i] = (w[i] << 8) | p[i * 8 + j];
        }
        for (int i = 16; i < 80; ++i) {
            uint64_t s0 = rotr(w[i - 15], 1) ^ rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
            uint64_t s1 = rotr(w[i - 2], 19) ^ rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            uint64_t t1 = hh + (rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41)) + ((e & f) ^ (~e & g)) + kRound[i] + w[i];
            uint64_t t2 = (rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g;
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
        h[7] += hh;
    }

    void add(const uint8_t* p, size_t n) {
        length += n;
        while (n > 0) {
            size_t take = n < sizeof block - used ? n : sizeof block - used;
            std::memcpy(block + used, p, take);
            used += take;
            p += take;
            n -= take;
            if (used == sizeof block) {
                compress(block);
                used = 0;
            }
        }
    }

    void finish(uint8_t out[64]) {
        uint64_t bits = length * 8;
        uint8_t pad = 0x80, zero = 0;
        add(&pad, 1);
        while (used != 112)
            add(&zero, 1);
        uint8_t len[16] = {};
        for (int i = 0; i < 8; ++i)
            len[8 + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        add(len, 16);
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j)
                out[i * 8 + j] = static_cast<uint8_t>(h[i] >> (56 - 8 * j));
    }
};

} // namespace

void sha512(const uint8_t* data, size_t size, uint8_t out[64]) {
    Sha512 s;
    s.add(data, size);
    s.finish(out);
}

// ---------------------------------------------------------------- the field: numbers mod p = 2^255 - 19

namespace {

// 16 limbs of 16 bits (with room to grow between carries).
struct Fe {
    int64_t v[16] = {};
};

Fe fe(int64_t small) {
    Fe r;
    r.v[0] = small;
    return r;
}

void carry(Fe& o) {
    for (int i = 0; i < 16; ++i) {
        int64_t c = o.v[i] >> 16; // arithmetic shift (C++20): floor division
        o.v[i] -= c * 65536;
        if (i < 15)
            o.v[i + 1] += c;
        else
            o.v[0] += 38 * c; // 2^256 = 38 (mod p)
    }
}

Fe add(const Fe& a, const Fe& b) {
    Fe r;
    for (int i = 0; i < 16; ++i)
        r.v[i] = a.v[i] + b.v[i];
    return r;
}

Fe sub(const Fe& a, const Fe& b) {
    Fe r;
    for (int i = 0; i < 16; ++i)
        r.v[i] = a.v[i] - b.v[i];
    return r;
}

Fe mul(const Fe& a, const Fe& b) {
    int64_t t[31] = {};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j)
            t[i + j] += a.v[i] * b.v[j];
    for (int i = 0; i < 15; ++i)
        t[i] += 38 * t[i + 16];
    Fe r;
    for (int i = 0; i < 16; ++i)
        r.v[i] = t[i];
    carry(r);
    carry(r);
    return r;
}

// Raising to a power given as 32 little-endian bytes.
Fe power(const Fe& base, const uint8_t exponent[32]) {
    Fe r = fe(1);
    for (int bit = 255; bit >= 0; --bit) {
        r = mul(r, r);
        if ((exponent[bit / 8] >> (bit % 8)) & 1)
            r = mul(r, base);
    }
    return r;
}

// The canonical 32 bytes (fully reduced below p).
void pack(uint8_t out[32], const Fe& n) {
    Fe t = n;
    carry(t);
    carry(t);
    carry(t);
    for (int pass = 0; pass < 2; ++pass) {
        Fe m;
        m.v[0] = t.v[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m.v[i] = t.v[i] - 0xffff - ((m.v[i - 1] >> 16) & 1);
            m.v[i - 1] &= 0xffff;
        }
        m.v[15] = t.v[15] - 0x7fff - ((m.v[14] >> 16) & 1);
        m.v[14] &= 0xffff;
        bool borrow = (m.v[15] >> 16) & 1;
        if (!borrow)
            t = m; // t >= p: use t - p
    }
    for (int i = 0; i < 16; ++i) {
        out[2 * i] = static_cast<uint8_t>(t.v[i] & 0xff);
        out[2 * i + 1] = static_cast<uint8_t>((t.v[i] >> 8) & 0xff);
    }
}

Fe unpack(const uint8_t in[32]) {
    Fe r;
    for (int i = 0; i < 16; ++i)
        r.v[i] = in[2 * i] + (static_cast<int64_t>(in[2 * i + 1]) << 8);
    r.v[15] &= 0x7fff;
    return r;
}

bool equal(const Fe& a, const Fe& b) {
    uint8_t x[32], y[32];
    pack(x, a);
    pack(y, b);
    return std::memcmp(x, y, 32) == 0;
}

bool odd(const Fe& a) {
    uint8_t x[32];
    pack(x, a);
    return x[0] & 1;
}

// Exponents, little-endian: p - 2 (inverse), (p - 5) / 8 and (p - 1) / 4.
uint8_t kPminus2[32], kPminus5over8[32], kPminus1over4[32];

Fe inverse(const Fe& a) { return power(a, kPminus2); }

// ---------------------------------------------------------------- the curve, in extended coordinates

struct Point {
    Fe x, y, z, t;
};

struct Constants {
    Fe d, d2, sqrtm1;
    Point base;
} K;
std::once_flag kSetUp;

Point addPoints(const Point& p, const Point& q) {
    Fe a = mul(sub(p.y, p.x), sub(q.y, q.x));
    Fe b = mul(add(p.y, p.x), add(q.y, q.x));
    Fe c = mul(mul(p.t, q.t), K.d2);
    Fe d = mul(p.z, q.z);
    d = add(d, d);
    Fe e = sub(b, a), f = sub(d, c), g = add(d, c), h = add(b, a);
    return {mul(e, f), mul(h, g), mul(g, f), mul(e, h)};
}

Point scalarMul(const Point& p, const uint8_t scalar[32]) {
    Point r{fe(0), fe(1), fe(1), fe(0)};
    for (int bit = 255; bit >= 0; --bit) {
        r = addPoints(r, r);
        if ((scalar[bit / 8] >> (bit % 8)) & 1)
            r = addPoints(r, p);
    }
    return r;
}

void packPoint(uint8_t out[32], const Point& p) {
    Fe zi = inverse(p.z);
    Fe x = mul(p.x, zi), y = mul(p.y, zi);
    pack(out, y);
    out[31] ^= static_cast<uint8_t>(odd(x) << 7);
}

// RFC 8032 5.1.3. False for bytes that aren't a point on the curve.
bool unpackPoint(Point& out, const uint8_t in[32]) {
    uint8_t canonical[32];
    Fe y = unpack(in);
    pack(canonical, y);
    uint8_t high = in[31] & 0x7f;
    if (std::memcmp(canonical, in, 31) != 0 || canonical[31] != high)
        return false; // y >= p
    bool sign = in[31] >> 7;
    Fe y2 = mul(y, y);
    Fe u = sub(y2, fe(1));
    Fe v = add(mul(K.d, y2), fe(1));
    Fe v3 = mul(mul(v, v), v);
    Fe x = mul(mul(u, v3), power(mul(mul(u, v3), mul(v3, v)), kPminus5over8)); // u v^3 (u v^7)^((p-5)/8)
    Fe vx2 = mul(v, mul(x, x));
    if (!equal(vx2, u)) {
        if (!equal(vx2, sub(fe(0), u)))
            return false;
        x = mul(x, K.sqrtm1);
    }
    if (equal(x, fe(0)) && sign)
        return false;
    if (odd(x) != sign)
        x = sub(fe(0), x);
    out = {x, y, fe(1), mul(x, y)};
    return true;
}

void computeConstants() {
    std::memset(kPminus2, 0xff, 32);
    kPminus2[0] = 0xeb;
    kPminus2[31] = 0x7f;
    std::memset(kPminus5over8, 0xff, 32);
    kPminus5over8[0] = 0xfd;
    kPminus5over8[31] = 0x0f;
    std::memset(kPminus1over4, 0xff, 32);
    kPminus1over4[0] = 0xfb;
    kPminus1over4[31] = 0x1f;
    K.d = mul(sub(fe(0), fe(121665)), inverse(fe(121666)));
    K.d2 = add(K.d, K.d);
    K.sqrtm1 = power(fe(2), kPminus1over4);
    uint8_t by[32];
    pack(by, mul(fe(4), inverse(fe(5)))); // the base point has y = 4/5 and an even x
    unpackPoint(K.base, by);
}

void setUp() { std::call_once(kSetUp, computeConstants); }

// ---------------------------------------------------------------- numbers mod L, the group's order

// L = 2^252 + 27742317777372353535851937790883648493, little-endian.
const uint8_t kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                        0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

// a >= b, for 33-byte little-endian numbers (b padded with a zero byte).
bool atLeast(const uint8_t a[33], const uint8_t b[33]) {
    for (int i = 32; i >= 0; --i)
        if (a[i] != b[i])
            return a[i] > b[i];
    return true;
}

// A 64-byte little-endian number mod L, one bit at a time (slow, and plenty fast for this).
void reduce(uint8_t out[32], const uint8_t in[64]) {
    uint8_t r[33] = {}, l[33] = {};
    std::memcpy(l, kL, 32);
    for (int bit = 511; bit >= 0; --bit) {
        int c = (in[bit / 8] >> (bit % 8)) & 1;
        for (int i = 0; i < 33; ++i) { // r = 2r + bit
            int v = r[i] * 2 + c;
            r[i] = static_cast<uint8_t>(v);
            c = v >> 8;
        }
        if (atLeast(r, l)) {
            int borrow = 0;
            for (int i = 0; i < 33; ++i) {
                int v = r[i] - l[i] - borrow;
                borrow = v < 0;
                r[i] = static_cast<uint8_t>(v + (borrow ? 256 : 0));
            }
        }
    }
    std::memcpy(out, r, 32);
}

} // namespace

bool verify(const uint8_t publicKey[32], const uint8_t* message, size_t size, const uint8_t signature[64]) {
    setUp();
    // S must be below L, or the same signature would have other valid spellings.
    uint8_t s[33] = {}, l[33] = {};
    std::memcpy(s, signature + 32, 32);
    std::memcpy(l, kL, 32);
    if (atLeast(s, l))
        return false;
    Point a, r;
    if (!unpackPoint(a, publicKey) || !unpackPoint(r, signature))
        return false;
    // k = SHA-512(R || A || message) mod L; valid when S*B = R + k*A.
    Sha512 h;
    h.add(signature, 32);
    h.add(publicKey, 32);
    h.add(message, size);
    uint8_t digest[64], k[32];
    h.finish(digest);
    reduce(k, digest);
    uint8_t left[32], right[32];
    packPoint(left, scalarMul(K.base, signature + 32));
    packPoint(right, addPoints(r, scalarMul(a, k)));
    return std::memcmp(left, right, 32) == 0;
}

} // namespace rynax::ed25519
