#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace flac {
class Bits {
public:
    Bits(const unsigned char* d, size_t n) : d_(d), n_(n) {}
    uint64_t Pos() const { return uint64_t(i_) * 8 - avail_; }
    void Seek(uint64_t bit) {
        i_ = size_t(bit >> 3);
        cache_ = 0;
        avail_ = 0;
        Refill();
        Skip(int(bit & 7));
    }
    bool Bad() const { return bad_ || Pos() > uint64_t(n_) * 8; }
    uint32_t Get(int n) {
        if (!n) return 0;
        if (avail_ < n) Refill();
        const uint32_t v = uint32_t(cache_ >> (64 - n));
        cache_ <<= n;
        avail_ -= n;
        return v;
    }
    int32_t GetSigned(int n) {
        if (!n) return 0;
        return int32_t(Get(n) << (32 - n)) >> (32 - n);
    }
    uint32_t Unary() {
        uint32_t zeros = 0;
        for (;;) {
            if (avail_ < 32) Refill();
            if (cache_) {
                const int lz = __builtin_clzll(cache_);
                if (lz < avail_) {
                    cache_ <<= lz + 1;
                    avail_ -= lz + 1;
                    return zeros + uint32_t(lz);
                }
            }
            zeros += uint32_t(avail_);
            cache_ = 0;
            avail_ = 0;
            if (i_ > n_ + 8 || zeros > (1u << 20)) {
                bad_ = true;
                return 0;
            }
        }
    }
    void AlignByte() { Skip(avail_ & 7); }
private:
    void Refill() {
        if (i_ + 8 <= n_) {
            uint64_t w;
            memcpy(&w, d_ + i_, 8);
            w = __builtin_bswap64(w);
            cache_ |= w >> avail_;
            const int take = (63 - avail_) >> 3;
            i_ += take;
            avail_ += take * 8;
        } else {
            while (avail_ <= 56) {
                cache_ |= uint64_t(i_ < n_ ? d_[i_] : 0) << (56 - avail_);
                ++i_;
                avail_ += 8;
            }
        }
    }
    void Skip(int n) {
        if (avail_ < n) Refill();
        cache_ = n >= 64 ? 0 : cache_ << n;
        avail_ -= n;
    }
    const unsigned char* d_;
    size_t n_, i_ = 0;
    uint64_t cache_ = 0;
    int avail_ = 0;
    bool bad_ = false;
};
template <int O>
inline void LpcRestore(const int32_t* coef, int shift, uint32_t bs, int32_t* out) {
    uint32_t i = O;
    for (; i + 1 < bs; i += 2) {
        int64_t s0 = 0, s1 = 0;
        for (int j = 0; j < O; ++j) s0 += int64_t(coef[j]) * out[i - 1 - j];
        for (int j = 1; j < O; ++j) s1 += int64_t(coef[j]) * out[i - j];
        const int32_t x0 = int32_t(out[i] + (s0 >> shift));
        out[i] = x0;
        s1 += int64_t(coef[0]) * x0;
        out[i + 1] = int32_t(out[i + 1] + (s1 >> shift));
    }
    for (; i < bs; ++i) {
        int64_t s = 0;
        for (int j = 0; j < O; ++j) s += int64_t(coef[j]) * out[i - 1 - j];
        out[i] = int32_t(out[i] + (s >> shift));
    }
}
inline void LpcRestoreAny(uint32_t order, const int32_t* coef, int shift, uint32_t bs, int32_t* out) {
    switch (order) {
#define HFPS_LPC(o) \
    case o: LpcRestore<o>(coef, shift, bs, out); return;
        HFPS_LPC(1) HFPS_LPC(2) HFPS_LPC(3) HFPS_LPC(4) HFPS_LPC(5) HFPS_LPC(6) HFPS_LPC(7) HFPS_LPC(8)
        HFPS_LPC(9) HFPS_LPC(10) HFPS_LPC(11) HFPS_LPC(12) HFPS_LPC(13) HFPS_LPC(14) HFPS_LPC(15) HFPS_LPC(16)
        HFPS_LPC(17) HFPS_LPC(18) HFPS_LPC(19) HFPS_LPC(20) HFPS_LPC(21) HFPS_LPC(22) HFPS_LPC(23) HFPS_LPC(24)
        HFPS_LPC(25) HFPS_LPC(26) HFPS_LPC(27) HFPS_LPC(28) HFPS_LPC(29) HFPS_LPC(30) HFPS_LPC(31) HFPS_LPC(32)
#undef HFPS_LPC
        default: return;
    }
}
inline uint8_t Crc8(const unsigned char* p, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = uint8_t(c & 0x80 ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}
inline uint16_t Crc16(const unsigned char* p, size_t n) {
    static uint16_t table[256];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 256; ++i) {
            uint16_t c = uint16_t(i << 8);
            for (int k = 0; k < 8; ++k) c = uint16_t(c & 0x8000 ? (c << 1) ^ 0x8005 : c << 1);
            table[i] = c;
        }
        ready = true;
    }
    uint16_t c = 0;
    for (size_t i = 0; i < n; ++i) c = uint16_t((c << 8) ^ table[(c >> 8) ^ p[i]]);
    return c;
}
inline bool Residual(Bits& br, uint32_t bs, uint32_t order, int32_t* out) {
    const uint32_t method = br.Get(2);
    if (method > 1) return false;
    const int paramBits = method ? 5 : 4;
    const uint32_t escape = method ? 31 : 15;
    const uint32_t po = br.Get(4);
    const uint32_t parts = 1u << po, psz = bs >> po;
    if (psz * parts != bs || psz < order) return false;
    uint32_t i = order;
    for (uint32_t p = 0; p < parts; ++p) {
        const uint32_t n = p ? psz : psz - order;
        const uint32_t k = br.Get(paramBits);
        if (k == escape) {
            const int raw = int(br.Get(5));
            for (uint32_t j = 0; j < n; ++j) out[i++] = br.GetSigned(raw);
        } else {
            for (uint32_t j = 0; j < n; ++j) {
                const uint64_t u = (uint64_t(br.Unary()) << k) | br.Get(int(k));
                if (u >> 32) return false;
                out[i++] = int32_t(uint32_t(u) >> 1) ^ -int32_t(u & 1);
            }
        }
        if (br.Bad()) return false;
    }
    return true;
}
inline bool Subframe(Bits& br, int bps, uint32_t bs, int32_t* out) {
    if (br.Get(1)) return false;
    const uint32_t type = br.Get(6);
    int wasted = 0;
    if (br.Get(1)) wasted = int(br.Unary()) + 1;
    if (wasted >= bps) return false;
    bps -= wasted;
    if (type == 0) {
        const int32_t v = br.GetSigned(bps);
        for (uint32_t i = 0; i < bs; ++i) out[i] = v;
    } else if (type == 1) {
        for (uint32_t i = 0; i < bs; ++i) out[i] = br.GetSigned(bps);
    } else if (type >= 8 && type <= 12) {
        const uint32_t order = type - 8;
        if (order > bs) return false;
        for (uint32_t i = 0; i < order; ++i) out[i] = br.GetSigned(bps);
        if (!Residual(br, bs, order, out)) return false;
        for (uint32_t i = order; i < bs; ++i) {
            int64_t pred = 0;
            switch (order) {
                case 1: pred = out[i - 1]; break;
                case 2: pred = 2ll * out[i - 1] - out[i - 2]; break;
                case 3: pred = 3ll * out[i - 1] - 3ll * out[i - 2] + out[i - 3]; break;
                case 4: pred = 4ll * out[i - 1] - 6ll * out[i - 2] + 4ll * out[i - 3] - out[i - 4]; break;
                default: break;
            }
            out[i] = int32_t(out[i] + pred);
        }
    } else if (type >= 32) {
        const uint32_t order = type - 31;
        if (order > bs) return false;
        for (uint32_t i = 0; i < order; ++i) out[i] = br.GetSigned(bps);
        const int precision = int(br.Get(4)) + 1;
        if (precision == 16) return false;
        const int shift = br.GetSigned(5);
        if (shift < 0) return false;
        int32_t coef[32];
        for (uint32_t j = 0; j < order; ++j) coef[j] = br.GetSigned(precision);
        if (!Residual(br, bs, order, out)) return false;
        LpcRestoreAny(order, coef, shift, bs, out);
    } else {
        return false;
    }
    if (wasted)
        for (uint32_t i = 0; i < bs; ++i) out[i] = int32_t(uint32_t(out[i]) << wasted);
    return !br.Bad();
}
inline bool Decode16(const unsigned char* d, size_t n, unsigned* channels, unsigned* rate, unsigned char** pcm, size_t* pcmBytes,
                     char* err, size_t errLen) {
    auto fail = [&](const char* why) {
        if (err && errLen) snprintf(err, errLen, "flac: %s", why);
        return false;
    };
    if (n < 42 || memcmp(d, "fLaC", 4)) return fail("not a flac file");
    size_t p = 4;
    unsigned ch = 0, sr = 0, bits = 0;
    uint64_t total = 0;
    for (bool first = true;; first = false) {
        if (p + 4 > n) return fail("metadata truncated");
        const unsigned type = d[p] & 0x7F, last = d[p] & 0x80;
        const size_t len = (size_t(d[p + 1]) << 16) | (size_t(d[p + 2]) << 8) | d[p + 3];
        p += 4;
        if (p + len > n) return fail("metadata block truncated");
        if (first) {
            if (type != 0 || len < 34) return fail("missing streaminfo");
            Bits si(d + p + 10, 8);
            sr = si.Get(20);
            ch = si.Get(3) + 1;
            bits = si.Get(5) + 1;
            total = (uint64_t(si.Get(4)) << 32) | si.Get(32);
        }
        p += len;
        if (last) break;
    }
    if (bits != 16 || ch < 1 || ch > 2 || !sr || !total) return fail("unsupported stream (need 16-bit mono/stereo with sample count)");
    if (total * ch * 2 > (256ull << 20)) return fail("stream too long");
    const size_t outBytes = size_t(total) * ch * 2;
    unsigned char* o = static_cast<unsigned char*>(malloc(outBytes));
    int32_t* buf = static_cast<int32_t*>(malloc(sizeof(int32_t) * 2 * 65536));
    if (!o || !buf) {
        free(o);
        free(buf);
        return fail("out of memory");
    }
    Bits br(d, n);
    br.Seek(uint64_t(p) * 8);
    uint64_t done = 0;
    const char* why = nullptr;
    while (done < total && !why) {
        const size_t start = size_t(br.Pos() >> 3);
        if (br.Get(15) != 0x7FFC) {
            why = "lost frame sync";
            break;
        }
        br.Get(1);
        const uint32_t bsCode = br.Get(4), srCode = br.Get(4), chCode = br.Get(4), ssCode = br.Get(3);
        if (br.Get(1)) {
            why = "reserved header bit";
            break;
        }
        const uint32_t lead = br.Get(8);
        int extra = lead < 0x80 ? 0 : (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2 : (lead & 0xF8) == 0xF0 ? 3
                  : (lead & 0xFC) == 0xF8 ? 4 : (lead & 0xFE) == 0xFC ? 5 : lead == 0xFE ? 6 : -1;
        if (extra < 0) {
            why = "bad frame number";
            break;
        }
        while (extra--)
            if ((br.Get(8) & 0xC0) != 0x80) why = "bad frame number";
        uint32_t bs = 0;
        if (bsCode == 1) bs = 192;
        else if (bsCode >= 2 && bsCode <= 5) bs = 576u << (bsCode - 2);
        else if (bsCode == 6) bs = br.Get(8) + 1;
        else if (bsCode == 7) bs = br.Get(16) + 1;
        else if (bsCode >= 8) bs = 256u << (bsCode - 8);
        if (srCode == 12) br.Get(8);
        else if (srCode == 13 || srCode == 14) br.Get(16);
        else if (srCode == 15) why = "bad sample rate code";
        if (!bs || bs > 65535) why = "bad block size";
        if (why) break;
        const size_t hdrEnd = size_t(br.Pos() >> 3);
        if (br.Get(8) != Crc8(d + start, hdrEnd - start) || br.Bad()) {
            why = "frame header crc mismatch";
            break;
        }
        if (ssCode != 0 && ssCode != 4) {
            why = "sample size is not 16-bit";
            break;
        }
        const unsigned fch = chCode <= 7 ? chCode + 1 : 2;
        if (chCode > 10 || fch != ch) {
            why = "channel layout differs from streaminfo";
            break;
        }
        int32_t* c0 = buf;
        int32_t* c1 = buf + 65536;
        for (unsigned c = 0; c < ch && !why; ++c) {
            const bool side = (chCode == 8 && c == 1) || (chCode == 9 && c == 0) || (chCode == 10 && c == 1);
            if (!Subframe(br, 16 + (side ? 1 : 0), bs, c ? c1 : c0)) why = "bad subframe";
        }
        if (why) break;
        br.AlignByte();
        const size_t crcAt = size_t(br.Pos() >> 3);
        if (br.Get(16) != Crc16(d + start, crcAt - start) || br.Bad()) {
            why = "frame crc mismatch";
            break;
        }
        if (done + bs > total) {
            why = "more samples than streaminfo says";
            break;
        }
        int16_t* w = reinterpret_cast<int16_t*>(o) + done * ch;
        for (uint32_t i = 0; i < bs; ++i) {
            int32_t l = c0[i], r = ch == 2 ? c1[i] : 0;
            if (chCode == 8) r = l - r;
            else if (chCode == 9) l = l + r;
            else if (chCode == 10) {
                const int32_t mid = int32_t(uint32_t(l) << 1) | (r & 1);
                l = (mid + r) >> 1;
                r = (mid - r) >> 1;
            }
            if (l < -32768 || l > 32767 || r < -32768 || r > 32767) {
                why = "sample out of 16-bit range";
                break;
            }
            w[i * ch] = int16_t(l);
            if (ch == 2) w[i * ch + 1] = int16_t(r);
        }
        done += bs;
    }
    free(buf);
    if (!why && done != total) why = "fewer samples than streaminfo says";
    if (why) {
        free(o);
        return fail(why);
    }
    *channels = ch;
    *rate = sr;
    *pcm = o;
    *pcmBytes = outBytes;
    return true;
}
}
