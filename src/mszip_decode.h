#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
namespace mszip {
class Bits {
public:
    Bits(const unsigned char* p, size_t n) : p_(p), end_(p + n) {}
    uint32_t Get(int n) {
        if (cnt_ < n) Refill();
        const uint32_t v = uint32_t(buf_ & ((uint64_t(1) << n) - 1));
        buf_ >>= n;
        cnt_ -= n;
        return v;
    }
    uint32_t Peek(int n) {
        if (cnt_ < n) Refill();
        return uint32_t(buf_ & ((uint64_t(1) << n) - 1));
    }
    void Drop(int n) {
        buf_ >>= n;
        cnt_ -= n;
    }
    void Align() { Drop(cnt_ & 7); }
    bool Overrun() const { return pad_ * 8 > size_t(cnt_); }
    bool Copy(unsigned char* out, size_t n) {
        while (n && cnt_ >= 8) {
            *out++ = static_cast<unsigned char>(Get(8));
            --n;
        }
        if (n > size_t(end_ - p_)) return false;
        memcpy(out, p_, n);
        p_ += n;
        return true;
    }
private:
    void Refill() {
        while (cnt_ <= 56) {
            uint64_t b = 0;
            if (p_ < end_) b = *p_++;
            else ++pad_;
            buf_ |= b << cnt_;
            cnt_ += 8;
        }
    }
    const unsigned char* p_;
    const unsigned char* end_;
    uint64_t buf_ = 0;
    int cnt_ = 0;
    size_t pad_ = 0;
};
inline bool Fail(std::string& err, const char* what) {
    err = std::string("mszip: ") + what;
    return false;
}
inline unsigned Reverse(unsigned code, int len) {
    unsigned r = 0;
    for (int i = 0; i < len; ++i, code >>= 1) r = (r << 1) | (code & 1);
    return r;
}
constexpr int kFastBits = 10;
struct Huff {
    uint16_t fast[1 << kFastBits];
    uint16_t count[16];
    uint16_t sym[288];
    bool Build(const unsigned char* len, int n) {
        memset(count, 0, sizeof count);
        for (int i = 0; i < n; ++i) ++count[len[i]];
        int left = 1;
        for (int l = 1; l <= 15; ++l) {
            left = (left << 1) - count[l];
            if (left < 0) return false;
        }
        uint16_t offs[16] = {0};
        for (int l = 1; l < 15; ++l) offs[l + 1] = uint16_t(offs[l] + count[l]);
        for (int i = 0; i < n; ++i)
            if (len[i]) sym[offs[len[i]]++] = uint16_t(i);
        memset(fast, 0, sizeof fast);
        unsigned code = 0;
        int k = 0;
        for (int l = 1; l <= kFastBits; ++l) {
            for (int j = 0; j < count[l]; ++j, ++k) {
                const uint16_t e = uint16_t(sym[k] << 4 | l);
                for (unsigned f = Reverse(code + j, l); f < (1u << kFastBits); f += 1u << l) fast[f] = e;
            }
            code = (code + count[l]) << 1;
        }
        return true;
    }
    int Decode(Bits& b) const {
        const uint16_t e = fast[b.Peek(kFastBits)];
        if (e) {
            b.Drop(e & 15);
            return e >> 4;
        }
        const unsigned bits = b.Peek(15);
        int code = 0, first = 0, index = 0;
        for (int l = 1; l <= 15; ++l) {
            code |= (bits >> (l - 1)) & 1;
            if (code - first < count[l]) {
                b.Drop(l);
                return sym[index + code - first];
            }
            index += count[l];
            first = (first + count[l]) << 1;
            code <<= 1;
        }
        return -1;
    }
};
inline const Huff* FixedTables() {
    static Huff t[2];
    static const bool built = [] {
        unsigned char len[288];
        for (int i = 0; i < 288; ++i) len[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        t[0].Build(len, 288);
        for (int i = 0; i < 30; ++i) len[i] = 5;
        t[1].Build(len, 30);
        return true;
    }();
    (void)built;
    return t;
}
inline bool Codes(Bits& b, const Huff& lit, const Huff& dist, unsigned char* out, size_t& pos, size_t limit, std::string& err) {
    static const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int s = lit.Decode(b);
        if (s < 0) return Fail(err, "bad literal / length code");
        if (s < 256) {
            if (pos >= limit) return Fail(err, "block decodes to more bytes than expected");
            out[pos++] = static_cast<unsigned char>(s);
            continue;
        }
        if (s == 256) return true;
        s -= 257;
        if (s >= 29) return Fail(err, "bad length symbol");
        const size_t len = kLenBase[s] + b.Get(kLenExtra[s]);
        const int d = dist.Decode(b);
        if (d < 0 || d >= 30) return Fail(err, "bad distance code");
        const size_t back = kDistBase[d] + b.Get(kDistExtra[d]);
        if (back > pos) return Fail(err, "distance points before the start of the file");
        if (len > limit - pos) return Fail(err, "block decodes to more bytes than expected");
        unsigned char* o = out + pos;
        const unsigned char* from = o - back;
        if (back >= len) memcpy(o, from, len);
        else
            for (size_t i = 0; i < len; ++i) o[i] = from[i];
        pos += len;
    }
}
inline bool Stored(Bits& b, unsigned char* out, size_t& pos, size_t limit, std::string& err) {
    b.Align();
    const unsigned len = b.Get(16), nlen = b.Get(16);
    if ((len ^ 0xFFFFu) != nlen) return Fail(err, "stored block length check failed");
    if (len > limit - pos) return Fail(err, "block decodes to more bytes than expected");
    if (!b.Copy(out + pos, len)) return Fail(err, "stored block is truncated");
    pos += len;
    return true;
}
inline bool Dynamic(Bits& b, unsigned char* out, size_t& pos, size_t limit, std::string& err) {
    static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = int(b.Get(5)) + 257, ndist = int(b.Get(5)) + 1, ncode = int(b.Get(4)) + 4;
    if (nlen > 286 || ndist > 30) return Fail(err, "bad dynamic block header");
    unsigned char cl[19] = {0};
    for (int i = 0; i < ncode; ++i) cl[kOrder[i]] = static_cast<unsigned char>(b.Get(3));
    Huff lencode, lit, dist;
    if (!lencode.Build(cl, 19)) return Fail(err, "bad code length code");
    unsigned char lens[286 + 30];
    int i = 0;
    while (i < nlen + ndist) {
        const int s = lencode.Decode(b);
        if (s < 0) return Fail(err, "bad code length symbol");
        if (s < 16) {
            lens[i++] = static_cast<unsigned char>(s);
            continue;
        }
        unsigned char v = 0;
        int rep;
        if (s == 16) {
            if (!i) return Fail(err, "length repeat with no previous length");
            v = lens[i - 1];
            rep = 3 + int(b.Get(2));
        } else if (s == 17) {
            rep = 3 + int(b.Get(3));
        } else {
            rep = 11 + int(b.Get(7));
        }
        if (i + rep > nlen + ndist) return Fail(err, "code lengths run past the end");
        while (rep--) lens[i++] = v;
    }
    if (!lens[256]) return Fail(err, "no end-of-block code");
    if (!lit.Build(lens, nlen)) return Fail(err, "bad literal / length code lengths");
    if (!dist.Build(lens + nlen, ndist)) return Fail(err, "bad distance code lengths");
    return Codes(b, lit, dist, out, pos, limit, err);
}
inline bool Stream(Bits& b, unsigned char* out, size_t& pos, size_t limit, std::string& err) {
    for (;;) {
        if (b.Overrun()) return Fail(err, "deflate data is truncated");
        const unsigned last = b.Get(1), type = b.Get(2);
        bool ok;
        if (type == 0) ok = Stored(b, out, pos, limit, err);
        else if (type == 1) ok = Codes(b, FixedTables()[0], FixedTables()[1], out, pos, limit, err);
        else if (type == 2) ok = Dynamic(b, out, pos, limit, err);
        else return Fail(err, "bad block type");
        if (!ok) return false;
        if (last) return !b.Overrun() || Fail(err, "deflate data is truncated");
    }
}
inline uint64_t U64(const unsigned char* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}
inline bool Decode(const unsigned char* in, size_t n, unsigned char* out, size_t outN, std::string& err) {
    static const unsigned char kSig[4] = {0x0A, 0x51, 0xE5, 0xC0};
    if (n < 24 || memcmp(in, kSig, 4) != 0 || in[4] != 24 || in[5] != 0 || in[7] != 2) return Fail(err, "not a Compress() MSZIP buffer");
    const uint64_t total = U64(in + 8), blk = U64(in + 16);
    if (total != outN) return Fail(err, "header size differs from the TZC1 size");
    if (!blk || blk > 32768) return Fail(err, "bad block size in the header");
    size_t at = 24, pos = 0;
    for (unsigned k = 0; pos < outN; ++k) {
        uint32_t m;
        if (n - at < 4) return Fail(err, "data ends before the last block");
        memcpy(&m, in + at, 4);
        at += 4;
        if (m < 2 || m > n - at || in[at] != 'C' || in[at + 1] != 'K') return Fail(err, "block has no CK signature or a bad length");
        const size_t start = pos, want = outN - pos < blk ? outN - pos : size_t(blk);
        Bits b(in + at + 2, m - 2);
        bool ok = Stream(b, out, pos, start + want, err);
        if (ok && pos != start + want) ok = Fail(err, "block decodes to fewer bytes than expected");
        if (!ok) {
            err += " (block " + std::to_string(k) + ")";
            return false;
        }
        at += m;
    }
    return true;
}
}
