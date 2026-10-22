#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace hvmslim {
constexpr uint32_t kHeader = 64, kTexture = 80, kSubmesh = 32, kBone = 140, kVertex = 56, kClipHeader = 48;
constexpr int kGroupOff[3] = {0, 3, 7}, kGroupLen[3] = {3, 4, 3};
inline uint32_t U32(const unsigned char* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
inline bool IsV2(const unsigned char* d, size_t len) { return len >= kHeader && !memcmp(d, "HVM1", 4) && U32(d + 4) == 2; }
inline bool ToV1(const unsigned char* d, size_t len, unsigned char** out, size_t* outLen, char* err, size_t errLen) {
    auto fail = [&](const char* why) {
        if (err && errLen) snprintf(err, errLen, "hvm2: %s", why);
        return false;
    };
    if (!IsV2(d, len)) return fail("not a v2 model");
    const uint32_t nv = U32(d + 8), ni = U32(d + 12), ns = U32(d + 16), nb = U32(d + 20), nc = U32(d + 24), nt = U32(d + 28);
    if (!nb || nb > 256 || nc > 512 || nt > 64 || ns > 256) return fail("counts out of range");
    const uint64_t fixed = kHeader + uint64_t(nt) * kTexture + uint64_t(ns) * kSubmesh + uint64_t(nb) * kBone + uint64_t(nv) * kVertex +
                           uint64_t(ni) * 4;
    if (fixed > len) return fail("truncated before clips");
    const uint64_t mask = (nb + 3u) & ~3u;
    uint64_t at = fixed, total = fixed;
    for (uint32_t c = 0; c < nc; ++c) {
        if (at + kClipHeader + mask > len) return fail("clip header truncated");
        const uint32_t frames = U32(d + at + 36);
        if (!frames || frames > 100000) return fail("clip frame count out of range");
        const unsigned char* flags = d + at + kClipHeader;
        uint64_t floats = 0;
        for (uint32_t b = 0; b < nb; ++b) {
            if (flags[b] & ~7u) return fail("bad constant flags");
            for (int g = 0; g < 3; ++g) floats += uint64_t(kGroupLen[g]) * ((flags[b] >> g) & 1 ? 1 : frames);
        }
        for (uint64_t k = nb; k < mask; ++k)
            if (flags[k]) return fail("bad flag padding");
        at += kClipHeader + mask + floats * 4;
        if (at > len) return fail("clip data truncated");
        total += kClipHeader + uint64_t(frames) * nb * 40;
    }
    if (at != len) return fail("trailing bytes");
    if (total > (512ull << 20)) return fail("expanded model too large");
    unsigned char* o = new unsigned char[static_cast<size_t>(total)];
    memcpy(o, d, static_cast<size_t>(fixed));
    const uint32_t one = 1;
    memcpy(o + 4, &one, 4);
    uint64_t src = fixed, dst = fixed;
    for (uint32_t c = 0; c < nc; ++c) {
        const uint32_t frames = U32(d + src + 36);
        memcpy(o + dst, d + src, kClipHeader);
        const unsigned char* flags = d + src + kClipHeader;
        src += kClipHeader + mask;
        dst += kClipHeader;
        unsigned char* clip = o + dst;
        for (uint32_t b = 0; b < nb; ++b) {
            for (int g = 0; g < 3; ++g) {
                const size_t n = kGroupLen[g] * 4;
                const bool fixedGroup = (flags[b] >> g) & 1;
                for (uint32_t f = 0; f < frames; ++f)
                    memcpy(clip + (size_t(f) * nb + b) * 40 + kGroupOff[g] * 4, d + src + (fixedGroup ? 0 : size_t(f) * n), n);
                src += fixedGroup ? n : size_t(frames) * n;
            }
        }
        dst += uint64_t(frames) * nb * 40;
    }
    *out = o;
    *outLen = static_cast<size_t>(total);
    return true;
}
inline uint32_t Crc32(const unsigned char* d, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
inline bool IsV3(const unsigned char* d, size_t len) { return len >= 32 && !memcmp(d, "HVM1", 4) && U32(d + 4) == 3; }
inline bool V3BaseRel(const unsigned char* d, size_t len, char* rel, size_t relLen, char* err, size_t errLen) {
    if (!IsV3(d, len)) {
        if (err && errLen) snprintf(err, errLen, "hvm3: not a v3 model");
        return false;
    }
    uint16_t n;
    memcpy(&n, d + 24, 2);
    if (26u + n > len || n == 0 || n + 1u > relLen) {
        if (err && errLen) snprintf(err, errLen, "hvm3: bad base path");
        return false;
    }
    memcpy(rel, d + 26, n);
    rel[n] = 0;
    return true;
}
inline bool V3ToV2(const unsigned char* d, size_t len, const unsigned char* base, size_t baseLen, unsigned char** out, size_t* outLen, char* err,
                   size_t errLen) {
    auto fail = [&](const char* why) {
        if (err && errLen) snprintf(err, errLen, "hvm3: %s", why);
        return false;
    };
    if (!IsV3(d, len)) return fail("not a v3 model");
    const uint32_t tlen = U32(d + 8), tcrc = U32(d + 12), blen = U32(d + 16), bcrc = U32(d + 20);
    if (blen != baseLen || Crc32(base, baseLen) != bcrc) return fail("base model mismatch");
    if (tlen > (512u << 20)) return fail("model too large");
    uint16_t n;
    memcpy(&n, d + 24, 2);
    uint64_t off = (26u + n + 3u) & ~3ull;
    if (off + 4 > len) return fail("truncated op count");
    const uint32_t m = U32(d + off);
    off += 4;
    if (off + uint64_t(m) * 8 > len) return fail("truncated op table");
    const unsigned char* table = d + off;
    uint64_t lit = off + uint64_t(m) * 8, total = 0;
    for (uint32_t k = 0; k < m; ++k) {
        const uint32_t o = U32(table + k * 8), n2 = U32(table + k * 8 + 4);
        if (o == 0xFFFFFFFFu) {
            if (lit + n2 > len) return fail("literal pool truncated");
            lit += n2;
        } else if (uint64_t(o) + n2 > baseLen) {
            return fail("copy out of base range");
        }
        total += n2;
    }
    if (lit != len) return fail("trailing bytes");
    if (total != tlen) return fail("length mismatch");
    unsigned char* o = new unsigned char[tlen ? tlen : 1];
    uint64_t at = 0;
    lit = off + uint64_t(m) * 8;
    for (uint32_t k = 0; k < m; ++k) {
        const uint32_t src = U32(table + k * 8), n2 = U32(table + k * 8 + 4);
        if (src == 0xFFFFFFFFu) {
            memcpy(o + at, d + lit, n2);
            lit += n2;
        } else {
            memcpy(o + at, base + src, n2);
        }
        at += n2;
    }
    if (Crc32(o, tlen) != tcrc) {
        delete[] o;
        return fail("crc mismatch");
    }
    *out = o;
    *outLen = tlen;
    return true;
}
}
