#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace lzmadec {
constexpr int kModelBits = 11;
constexpr uint32_t kModelTotal = 1u << kModelBits;
constexpr int kMoveBits = 5;
constexpr uint32_t kTop = 1u << 24;
constexpr uint16_t kProbInit = kModelTotal / 2;
class Range {
public:
    Range(const unsigned char* p, size_t n) : p_(p), end_(p + n) {}
    bool Init() {
        const unsigned char first = Next();
        for (int i = 0; i < 4; ++i) code_ = (code_ << 8) | Next();
        return first == 0 && code_ != range_;
    }
    unsigned Bit(uint16_t* prob) {
        uint32_t v = *prob;
        const uint32_t bound = (range_ >> kModelBits) * v;
        unsigned bit;
        if (code_ < bound) {
            v += (kModelTotal - v) >> kMoveBits;
            range_ = bound;
            bit = 0;
        } else {
            v -= v >> kMoveBits;
            code_ -= bound;
            range_ -= bound;
            bit = 1;
        }
        *prob = static_cast<uint16_t>(v);
        if (range_ < kTop) {
            range_ <<= 8;
            code_ = (code_ << 8) | Next();
        }
        return bit;
    }
    uint32_t Direct(unsigned n) {
        uint32_t r = 0;
        do {
            range_ >>= 1;
            code_ -= range_;
            const uint32_t t = 0u - (code_ >> 31);
            code_ += range_ & t;
            if (code_ == range_) bad_ = true;
            if (range_ < kTop) {
                range_ <<= 8;
                code_ = (code_ << 8) | Next();
            }
            r = (r << 1) + (t + 1);
        } while (--n);
        return r;
    }
    bool Overrun() const { return pad_ != 0; }
    bool Bad() const { return bad_; }
private:
    unsigned char Next() {
        if (p_ < end_) return *p_++;
        ++pad_;
        return 0;
    }
    const unsigned char* p_;
    const unsigned char* end_;
    uint32_t range_ = 0xFFFFFFFFu, code_ = 0;
    size_t pad_ = 0;
    bool bad_ = false;
};
inline unsigned Tree(Range& rc, uint16_t* probs, int bits) {
    unsigned m = 1;
    for (int i = 0; i < bits; ++i) m = (m << 1) + rc.Bit(&probs[m]);
    return m - (1u << bits);
}
inline unsigned TreeReverse(Range& rc, uint16_t* probs, int bits) {
    unsigned m = 1, s = 0;
    for (int i = 0; i < bits; ++i) {
        const unsigned b = rc.Bit(&probs[m]);
        m = (m << 1) + b;
        s |= b << i;
    }
    return s;
}
struct Len {
    uint16_t choice = kProbInit, choice2 = kProbInit;
    uint16_t low[16][8], mid[16][8], high[256];
    Len() {
        for (auto& r : low)
            for (uint16_t& p : r) p = kProbInit;
        for (auto& r : mid)
            for (uint16_t& p : r) p = kProbInit;
        for (uint16_t& p : high) p = kProbInit;
    }
    unsigned Decode(Range& rc, unsigned posState) {
        if (!rc.Bit(&choice)) return Tree(rc, low[posState], 3);
        if (!rc.Bit(&choice2)) return 8 + Tree(rc, mid[posState], 3);
        return 16 + Tree(rc, high, 8);
    }
};
inline bool Decode(const unsigned char* props, const unsigned char* in, size_t n, unsigned char* out, size_t outN, std::string& err) {
    unsigned d = props[0];
    if (d >= 9 * 5 * 5) {
        err = "tzc1-lzma: bad properties byte " + std::to_string(d);
        return false;
    }
    const unsigned lc = d % 9;
    d /= 9;
    const unsigned lp = d % 5, pb = d / 5;
    std::vector<uint16_t> lit(size_t(0x300) << (lc + lp), kProbInit);
    uint16_t isMatch[12 << 4], isRep[12], isRepG0[12], isRepG1[12], isRepG2[12], isRep0Long[12 << 4];
    uint16_t posSlot[4][64], posSpecial[115], align[16];
    for (uint16_t* a : {isMatch, isRep0Long})
        for (int i = 0; i < (12 << 4); ++i) a[i] = kProbInit;
    for (uint16_t* a : {isRep, isRepG0, isRepG1, isRepG2})
        for (int i = 0; i < 12; ++i) a[i] = kProbInit;
    for (auto& r : posSlot)
        for (uint16_t& p : r) p = kProbInit;
    for (uint16_t& p : posSpecial) p = kProbInit;
    for (uint16_t& p : align) p = kProbInit;
    Len len, repLen;
    Range rc(in, n);
    if (!rc.Init()) {
        err = "tzc1-lzma: bad stream header";
        return false;
    }
    const size_t pbMask = (size_t(1) << pb) - 1, lpMask = (size_t(1) << lp) - 1;
    uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    unsigned state = 0;
    size_t pos = 0;
    while (pos < outN) {
        const unsigned ps = static_cast<unsigned>(pos & pbMask);
        if (!rc.Bit(&isMatch[(state << 4) + ps])) {
            const unsigned prev = pos ? out[pos - 1] : 0;
            uint16_t* p = &lit[size_t(0x300) * (((pos & lpMask) << lc) + (prev >> (8 - lc)))];
            unsigned sym = 1;
            if (state >= 7) {
                if (rep0 >= pos) {
                    err = "tzc1-lzma: literal refers before the start";
                    return false;
                }
                unsigned mb = out[pos - rep0 - 1];
                do {
                    const unsigned mbit = (mb >> 7) & 1;
                    mb <<= 1;
                    const unsigned bit = rc.Bit(&p[((1 + mbit) << 8) + sym]);
                    sym = (sym << 1) | bit;
                    if (mbit != bit) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | rc.Bit(&p[sym]);
            out[pos++] = static_cast<unsigned char>(sym - 0x100);
            state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
            continue;
        }
        unsigned l;
        if (rc.Bit(&isRep[state])) {
            if (pos == 0) {
                err = "tzc1-lzma: repeat match at the start";
                return false;
            }
            if (!rc.Bit(&isRepG0[state])) {
                if (!rc.Bit(&isRep0Long[(state << 4) + ps])) {
                    if (rep0 >= pos) {
                        err = "tzc1-lzma: match distance beyond decoded data";
                        return false;
                    }
                    state = state < 7 ? 9 : 11;
                    out[pos] = out[pos - rep0 - 1];
                    ++pos;
                    continue;
                }
            } else {
                uint32_t dist;
                if (!rc.Bit(&isRepG1[state])) {
                    dist = rep1;
                } else {
                    if (!rc.Bit(&isRepG2[state])) {
                        dist = rep2;
                    } else {
                        dist = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = dist;
            }
            l = repLen.Decode(rc, ps);
            state = state < 7 ? 8 : 11;
        } else {
            rep3 = rep2;
            rep2 = rep1;
            rep1 = rep0;
            l = len.Decode(rc, ps);
            state = state < 7 ? 7 : 10;
            const unsigned slot = Tree(rc, posSlot[l < 4 ? l : 3], 6);
            if (slot < 4) {
                rep0 = slot;
            } else {
                const unsigned nd = (slot >> 1) - 1;
                uint32_t dist = (2u | (slot & 1)) << nd;
                if (slot < 14) {
                    dist += TreeReverse(rc, posSpecial + dist - slot, static_cast<int>(nd));
                } else {
                    dist += rc.Direct(nd - 4) << 4;
                    dist += TreeReverse(rc, align, 4);
                }
                rep0 = dist;
            }
            if (rep0 == 0xFFFFFFFFu) break;
        }
        l += 2;
        if (rep0 >= pos) {
            err = "tzc1-lzma: match distance beyond decoded data";
            return false;
        }
        if (l > outN - pos) {
            err = "tzc1-lzma: match runs past the end";
            return false;
        }
        const unsigned char* s = out + pos - rep0 - 1;
        unsigned char* t = out + pos;
        if (rep0 + 1 >= l) {
            memcpy(t, s, l);
        } else {
            for (unsigned i = 0; i < l; ++i) t[i] = s[i];
        }
        pos += l;
    }
    if (pos != outN) {
        err = "tzc1-lzma: end marker at " + std::to_string(pos) + " of " + std::to_string(outN) + " bytes";
        return false;
    }
    if (rc.Bad() || rc.Overrun()) {
        err = rc.Overrun() ? "tzc1-lzma: stream truncated" : "tzc1-lzma: corrupt stream";
        return false;
    }
    return true;
}
}
