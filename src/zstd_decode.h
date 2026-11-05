#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
namespace zstdd {
inline uint64_t Rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
inline uint64_t Rd64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
inline uint32_t Rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
inline uint64_t Xxh64(const uint8_t* p, size_t len, uint64_t seed = 0) {
    const uint64_t P1 = 11400714785074694791ULL, P2 = 14029467366897019727ULL, P3 = 1609587929392839161ULL,
                   P4 = 9650029242287828579ULL, P5 = 2870177450012600261ULL;
    const uint8_t* end = p + len;
    uint64_t h;
    auto round = [&](uint64_t acc, uint64_t in) { acc += in * P2; acc = Rotl64(acc, 31); return acc * P1; };
    auto merge = [&](uint64_t acc, uint64_t v) { v = round(0, v); acc ^= v; return acc * P1 + P4; };
    if (len >= 32) {
        uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        const uint8_t* lim = end - 32;
        do {
            v1 = round(v1, Rd64(p)), v2 = round(v2, Rd64(p + 8)), v3 = round(v3, Rd64(p + 16)), v4 = round(v4, Rd64(p + 24));
            p += 32;
        } while (p <= lim);
        h = Rotl64(v1, 1) + Rotl64(v2, 7) + Rotl64(v3, 12) + Rotl64(v4, 18);
        h = merge(h, v1), h = merge(h, v2), h = merge(h, v3), h = merge(h, v4);
    } else {
        h = seed + P5;
    }
    h += static_cast<uint64_t>(len);
    while (p + 8 <= end) h ^= round(0, Rd64(p)), h = Rotl64(h, 27) * P1 + P4, p += 8;
    if (p + 4 <= end) h ^= static_cast<uint64_t>(Rd32(p)) * P1, h = Rotl64(h, 23) * P2 + P3, p += 4;
    while (p < end) h ^= (*p++) * P5, h = Rotl64(h, 11) * P1;
    h ^= h >> 33, h *= P2, h ^= h >> 29, h *= P3, h ^= h >> 32;
    return h;
}
inline int HighBit(uint32_t v) {
    int n = 0;
    while (v >>= 1) ++n;
    return n;
}
struct BackBits {
    const uint8_t* b = nullptr;
    size_t len = 0;
    int64_t pos = 0;
    bool Init(const uint8_t* src, size_t n) {
        b = src;
        len = n;
        if (!n || !src[n - 1]) return false;
        pos = static_cast<int64_t>(n) * 8 - 8 + HighBit(src[n - 1]);
        return true;
    }
    uint64_t Window(int64_t lo) const {
        size_t i0 = static_cast<size_t>(lo >> 3);
        uint64_t w = 0;
        size_t k = len - i0 < 8 ? len - i0 : 8;
        memcpy(&w, b + i0, k);
        return w >> (lo & 7);
    }
    uint64_t Peek(int n) const {
        if (n <= 0) return 0;
        int64_t lo = pos - n;
        uint64_t mask = (n >= 64) ? ~0ull : ((1ull << n) - 1);
        if (lo >= 0) return Window(lo) & mask;
        if (pos <= 0) return 0;
        uint64_t v = Window(0) & ((1ull << pos) - 1);
        return (v << (-lo)) & mask;
    }
    uint64_t Read(int n) {
        uint64_t v = Peek(n);
        pos -= n;
        return v;
    }
    bool Over() const { return pos < 0; }
};
struct FseEntry {
    uint16_t sym = 0, base = 0;
    uint8_t bits = 0;
};
struct FseTable {
    int log = 0;
    std::vector<FseEntry> t;
};
struct FwdBits {
    const uint8_t* b;
    size_t n, pos = 0;
    uint32_t Read(int k) {
        uint32_t v = 0;
        for (int i = 0; i < k; ++i, ++pos) {
            if ((pos >> 3) >= n) return 0xFFFFFFFFu;
            v |= ((b[pos >> 3] >> (pos & 7)) & 1u) << i;
        }
        return v;
    }
    uint32_t Peek(int k) const {
        FwdBits c = *this;
        return c.Read(k);
    }
};
inline bool BuildFse(const int16_t* norm, int maxSym, int log, FseTable& out) {
    const int size = 1 << log;
    out.log = log;
    out.t.assign(size, FseEntry());
    std::vector<uint16_t> next(maxSym + 1);
    int high = size - 1;
    for (int s = 0; s <= maxSym; ++s) {
        if (norm[s] == -1) {
            out.t[high--].sym = static_cast<uint16_t>(s);
            next[s] = 1;
        } else {
            next[s] = static_cast<uint16_t>(norm[s] > 0 ? norm[s] : 0);
        }
    }
    const int step = (size >> 1) + (size >> 3) + 3, mask = size - 1;
    int pos = 0;
    for (int s = 0; s <= maxSym; ++s)
        for (int i = 0; i < norm[s]; ++i) {
            out.t[pos].sym = static_cast<uint16_t>(s);
            do pos = (pos + step) & mask;
            while (pos > high);
        }
    if (pos != 0) return false;
    for (int u = 0; u < size; ++u) {
        int s = out.t[u].sym;
        int ns = next[s]++;
        if (ns <= 0) return false;
        int nb = log - HighBit(static_cast<uint32_t>(ns));
        out.t[u].bits = static_cast<uint8_t>(nb);
        out.t[u].base = static_cast<uint16_t>((ns << nb) - size);
    }
    return true;
}
inline size_t ReadFseDesc(const uint8_t* src, size_t n, int maxLog, int maxSym, FseTable& out) {
    FwdBits r{src, n};
    uint32_t a = r.Read(4);
    if (a == 0xFFFFFFFFu) return 0;
    int log = static_cast<int>(a) + 5;
    if (log > maxLog) return 0;
    int remaining = (1 << log) + 1, threshold = 1 << log, nb = log + 1, sym = 0;
    std::vector<int16_t> norm(maxSym + 1, 0);
    while (remaining > 1 && sym <= maxSym) {
        int maxv = 2 * threshold - 1 - remaining;
        uint32_t lowv = r.Peek(nb - 1);
        if (lowv == 0xFFFFFFFFu) return 0;
        int v;
        if (static_cast<int>(lowv & (threshold - 1)) < maxv) {
            v = static_cast<int>(lowv & (threshold - 1));
            r.Read(nb - 1);
        } else {
            uint32_t full = r.Read(nb);
            if (full == 0xFFFFFFFFu) return 0;
            v = static_cast<int>(full & (2 * threshold - 1));
            if (v >= threshold) v -= maxv;
        }
        int cnt = v - 1;
        remaining -= cnt < 0 ? -cnt : cnt;
        norm[sym++] = static_cast<int16_t>(cnt);
        if (cnt == 0) {
            for (;;) {
                uint32_t rep = r.Read(2);
                if (rep == 0xFFFFFFFFu) return 0;
                for (uint32_t k = 0; k < rep; ++k) {
                    if (sym > maxSym) return 0;
                    norm[sym++] = 0;
                }
                if (rep != 3) break;
            }
        }
        while (remaining < threshold) --nb, threshold >>= 1;
    }
    if (remaining != 1 || sym > maxSym + 1) return 0;
    if (!BuildFse(norm.data(), maxSym, log, out)) return 0;
    return (r.pos + 7) >> 3;
}
inline void RleFse(int sym, FseTable& out) {
    out.log = 0;
    out.t.assign(1, FseEntry());
    out.t[0].sym = static_cast<uint16_t>(sym);
}
struct HufTable {
    int log = 0;
    std::vector<uint8_t> sym, bits;
};
inline bool BuildHuf(const std::vector<uint8_t>& w0, HufTable& out) {
    std::vector<uint8_t> w = w0;
    uint32_t total = 0;
    for (uint8_t x : w) {
        if (x > 11) return false;
        if (x) total += 1u << (x - 1);
    }
    if (!total) return false;
    int log = HighBit(total) + 1;
    if (log > 11) return false;
    uint32_t rest = (1u << log) - total;
    if (rest & (rest - 1)) return false;
    w.push_back(static_cast<uint8_t>(HighBit(rest) + 1));
    if (w.size() > 256) return false;
    out.log = log;
    out.sym.assign(1u << log, 0);
    out.bits.assign(1u << log, 0);
    uint32_t rankStart[13] = {0};
    uint32_t rankCnt[13] = {0};
    for (uint8_t x : w) ++rankCnt[x];
    uint32_t next = 0;
    for (int k = 1; k <= log; ++k) {
        rankStart[k] = next;
        next += rankCnt[k] << (k - 1);
    }
    if (next != (1u << log)) return false;
    for (size_t s = 0; s < w.size(); ++s) {
        int k = w[s];
        if (!k) continue;
        uint32_t len = 1u << (k - 1);
        for (uint32_t i = 0; i < len; ++i) {
            out.sym[rankStart[k] + i] = static_cast<uint8_t>(s);
            out.bits[rankStart[k] + i] = static_cast<uint8_t>(log + 1 - k);
        }
        rankStart[k] += len;
    }
    return true;
}
inline size_t ReadHufDesc(const uint8_t* src, size_t n, HufTable& out) {
    if (!n) return 0;
    uint8_t hb = src[0];
    std::vector<uint8_t> w;
    if (hb >= 128) {
        size_t num = hb - 127, bytes = (num + 1) / 2;
        if (1 + bytes > n) return 0;
        for (size_t i = 0; i < num; ++i) w.push_back(i & 1 ? (src[1 + i / 2] & 15) : (src[1 + i / 2] >> 4));
        if (!BuildHuf(w, out)) return 0;
        return 1 + bytes;
    }
    size_t csize = hb;
    if (!csize || 1 + csize > n) return 0;
    FseTable ft;
    size_t d = ReadFseDesc(src + 1, csize, 6, 255, ft);
    if (!d || d >= csize) return 0;
    BackBits bb;
    if (!bb.Init(src + 1 + d, csize - d)) return 0;
    uint32_t s1 = static_cast<uint32_t>(bb.Read(ft.log)), s2 = static_cast<uint32_t>(bb.Read(ft.log));
    if (bb.Over()) return 0;
    for (;;) {
        if (w.size() > 254) return 0;
        const FseEntry& e1 = ft.t[s1];
        w.push_back(static_cast<uint8_t>(e1.sym));
        s1 = e1.base + static_cast<uint32_t>(bb.Read(e1.bits));
        if (bb.Over()) {
            w.push_back(static_cast<uint8_t>(ft.t[s2].sym));
            break;
        }
        const FseEntry& e2 = ft.t[s2];
        w.push_back(static_cast<uint8_t>(e2.sym));
        s2 = e2.base + static_cast<uint32_t>(bb.Read(e2.bits));
        if (bb.Over()) {
            w.push_back(static_cast<uint8_t>(ft.t[s1].sym));
            break;
        }
    }
    if (!BuildHuf(w, out)) return 0;
    return 1 + csize;
}
inline bool HufStream(const uint8_t* src, size_t n, const HufTable& h, uint8_t* dst, size_t count) {
    BackBits bb;
    if (!bb.Init(src, n)) return false;
    for (size_t i = 0; i < count; ++i) {
        uint32_t v = static_cast<uint32_t>(bb.Peek(h.log));
        dst[i] = h.sym[v];
        bb.pos -= h.bits[v];
    }
    return bb.pos == 0;
}
static const uint32_t kLLBase[36] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512,
                                     1024, 2048, 4096, 8192, 16384, 32768, 65536};
static const uint8_t kLLBits[36] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const uint32_t kMLBase[53] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                                     33, 34, 35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};
static const uint8_t kMLBits[53] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                    1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const int16_t kLLDef[36] = {4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
static const int16_t kMLDef[53] = {1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
static const int16_t kOFDef[29] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};
struct FrameState {
    FseTable ll, of, ml;
    bool llOk = false, ofOk = false, mlOk = false;
    HufTable huf;
    bool hufOk = false;
    uint64_t rep[3] = {1, 4, 8};
};
inline long ReadSeqTable(int mode, const uint8_t* src, size_t n, const int16_t* def, int defMax, int defLog, int maxLog, int maxSym,
                         FseTable& t, bool& ok) {
    if (mode == 0) {
        ok = BuildFse(def, defMax, defLog, t);
        return ok ? 0 : -1;
    }
    if (mode == 1) {
        if (!n || src[0] > maxSym) return -1;
        RleFse(src[0], t);
        ok = true;
        return 1;
    }
    if (mode == 2) {
        size_t d = ReadFseDesc(src, n, maxLog, maxSym, t);
        if (!d) return -1;
        ok = true;
        return static_cast<long>(d);
    }
    return ok ? 0 : -1;
}
inline bool DecodeBlock(const uint8_t* src, size_t n, FrameState& fs, std::vector<uint8_t>& out, size_t frameStart) {
    if (!n) return false;
    const uint8_t* p = src;
    int ltype = p[0] & 3, sf = (p[0] >> 2) & 3;
    size_t regen = 0, csize = 0, hsz = 0;
    int streams = 1;
    if (ltype <= 1) {
        if (sf == 0 || sf == 2) regen = p[0] >> 3, hsz = 1;
        else if (sf == 1) {
            if (n < 2) return false;
            regen = (p[0] >> 4) + (static_cast<size_t>(p[1]) << 4), hsz = 2;
        } else {
            if (n < 3) return false;
            regen = (p[0] >> 4) + (static_cast<size_t>(p[1]) << 4) + (static_cast<size_t>(p[2]) << 12), hsz = 3;
        }
    } else {
        if (sf <= 1) {
            if (n < 3) return false;
            uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
            regen = (v >> 4) & 0x3FF, csize = (v >> 14) & 0x3FF, hsz = 3, streams = sf == 0 ? 1 : 4;
        } else if (sf == 2) {
            if (n < 4) return false;
            uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
            regen = (v >> 4) & 0x3FFF, csize = v >> 18, hsz = 4, streams = 4;
        } else {
            if (n < 5) return false;
            uint64_t v = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint64_t>(p[3]) << 24) | (static_cast<uint64_t>(p[4]) << 32);
            regen = (v >> 4) & 0x3FFFF, csize = (v >> 22) & 0x3FFFF, hsz = 5, streams = 4;
        }
    }
    if (regen > (128u << 10)) return false;
    std::vector<uint8_t> lit(regen);
    size_t used;
    if (ltype == 0) {
        if (hsz + regen > n) return false;
        if (regen) memcpy(lit.data(), p + hsz, regen);
        used = hsz + regen;
    } else if (ltype == 1) {
        if (hsz + 1 > n) return false;
        memset(lit.data(), p[hsz], regen);
        used = hsz + 1;
    } else {
        if (hsz + csize > n) return false;
        const uint8_t* q = p + hsz;
        size_t qn = csize;
        if (ltype == 2) {
            size_t d = ReadHufDesc(q, qn, fs.huf);
            if (!d) return false;
            fs.hufOk = true;
            q += d, qn -= d;
        } else if (!fs.hufOk) {
            return false;
        }
        if (streams == 1) {
            if (!HufStream(q, qn, fs.huf, lit.data(), regen)) return false;
        } else {
            if (qn < 6) return false;
            size_t s1 = q[0] | (q[1] << 8), s2 = q[2] | (q[3] << 8), s3 = q[4] | (q[5] << 8);
            if (6 + s1 + s2 + s3 > qn) return false;
            size_t s4 = qn - 6 - s1 - s2 - s3, seg = (regen + 3) / 4;
            if (seg * 3 > regen) return false;
            const uint8_t* b = q + 6;
            if (!HufStream(b, s1, fs.huf, lit.data(), seg) || !HufStream(b + s1, s2, fs.huf, lit.data() + seg, seg) ||
                !HufStream(b + s1 + s2, s3, fs.huf, lit.data() + 2 * seg, seg) ||
                !HufStream(b + s1 + s2 + s3, s4, fs.huf, lit.data() + 3 * seg, regen - 3 * seg))
                return false;
        }
        used = hsz + csize;
    }
    p += used;
    n -= used;
    if (!n) return false;
    size_t nseq;
    if (p[0] < 128) nseq = p[0], p += 1, n -= 1;
    else if (p[0] < 255) {
        if (n < 2) return false;
        nseq = ((p[0] - 128) << 8) + p[1], p += 2, n -= 2;
    } else {
        if (n < 3) return false;
        nseq = p[1] + (p[2] << 8) + 0x7F00, p += 3, n -= 3;
    }
    size_t li = 0;
    if (nseq) {
        if (!n) return false;
        uint8_t modes = p[0];
        if (modes & 3) return false;
        p += 1, n -= 1;
        long k = ReadSeqTable((modes >> 6) & 3, p, n, kLLDef, 35, 6, 9, 35, fs.ll, fs.llOk);
        if (k < 0) return false;
        p += k, n -= k;
        k = ReadSeqTable((modes >> 4) & 3, p, n, kOFDef, 28, 5, 8, 31, fs.of, fs.ofOk);
        if (k < 0) return false;
        p += k, n -= k;
        k = ReadSeqTable((modes >> 2) & 3, p, n, kMLDef, 52, 6, 9, 52, fs.ml, fs.mlOk);
        if (k < 0) return false;
        p += k, n -= k;
        BackBits bb;
        if (!bb.Init(p, n)) return false;
        uint32_t sl = static_cast<uint32_t>(bb.Read(fs.ll.log)), so = static_cast<uint32_t>(bb.Read(fs.of.log)),
                 sm = static_cast<uint32_t>(bb.Read(fs.ml.log));
        for (size_t i = 0; i < nseq; ++i) {
            const FseEntry &el = fs.ll.t[sl], &eo = fs.of.t[so], &em = fs.ml.t[sm];
            int ofc = eo.sym, llc = el.sym, mlc = em.sym;
            if (ofc > 31 || llc > 35 || mlc > 52) return false;
            uint64_t ofv = (1ull << ofc) + bb.Read(ofc);
            uint64_t ml = kMLBase[mlc] + bb.Read(kMLBits[mlc]);
            uint64_t ll = kLLBase[llc] + bb.Read(kLLBits[llc]);
            uint64_t off;
            if (ofv > 3) {
                off = ofv - 3;
                fs.rep[2] = fs.rep[1], fs.rep[1] = fs.rep[0], fs.rep[0] = off;
            } else {
                int idx = static_cast<int>(ofv) - 1 + (ll == 0 ? 1 : 0);
                if (idx == 0) {
                    off = fs.rep[0];
                } else {
                    off = idx == 3 ? fs.rep[0] - 1 : fs.rep[idx];
                    if (idx != 1) fs.rep[2] = fs.rep[1];
                    fs.rep[1] = fs.rep[0], fs.rep[0] = off;
                }
            }
            if (i + 1 < nseq) {
                sl = el.base + static_cast<uint32_t>(bb.Read(el.bits));
                sm = em.base + static_cast<uint32_t>(bb.Read(em.bits));
                so = eo.base + static_cast<uint32_t>(bb.Read(eo.bits));
            }
            if (li + ll > lit.size()) return false;
            out.insert(out.end(), lit.begin() + li, lit.begin() + li + ll);
            li += ll;
            size_t have = out.size() - frameStart;
            if (!off || off > have) return false;
            size_t from = out.size() - off, at = out.size();
            out.resize(at + ml);
            if (off >= ml) memcpy(out.data() + at, out.data() + from, ml);
            else for (uint64_t c = 0; c < ml; ++c) out[at + c] = out[from + c];
        }
        if (bb.pos != 0) return false;
    }
    out.insert(out.end(), lit.begin() + li, lit.end());
    return true;
}
inline bool Decompress(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    out.clear();
    size_t i = 0;
    bool any = false;
    while (i < n) {
        if (n - i < 4) return false;
        uint32_t magic = Rd32(src + i);
        if ((magic & 0xFFFFFFF0u) == 0x184D2A50u) {
            if (n - i < 8) return false;
            size_t sz = Rd32(src + i + 4);
            if (n - i - 8 < sz) return false;
            i += 8 + sz;
            continue;
        }
        if (magic != 0xFD2FB528u) return false;
        i += 4;
        if (i >= n) return false;
        uint8_t fhd = src[i++];
        int fcsFlag = fhd >> 6, single = (fhd >> 5) & 1, check = (fhd >> 2) & 1, dictFlag = fhd & 3;
        if (fhd & 8) return false;
        if (!single) {
            if (i >= n) return false;
            ++i;
        }
        static const int dsz[4] = {0, 1, 2, 4};
        if (n - i < static_cast<size_t>(dsz[dictFlag])) return false;
        uint32_t dict = 0;
        for (int k = 0; k < dsz[dictFlag]; ++k) dict |= static_cast<uint32_t>(src[i + k]) << (8 * k);
        i += dsz[dictFlag];
        if (dict) return false;
        int fsz = fcsFlag == 0 ? (single ? 1 : 0) : (fcsFlag == 1 ? 2 : (fcsFlag == 2 ? 4 : 8));
        if (n - i < static_cast<size_t>(fsz)) return false;
        uint64_t fcs = 0;
        bool hasFcs = fsz > 0;
        for (int k = 0; k < fsz; ++k) fcs |= static_cast<uint64_t>(src[i + k]) << (8 * k);
        if (fsz == 2) fcs += 256;
        i += fsz;
        size_t start = out.size();
        if (hasFcs) {
            if (fcs > (1ull << 31)) return false;
            out.reserve(start + static_cast<size_t>(fcs));
        }
        FrameState fs;
        for (;;) {
            if (n - i < 3) return false;
            uint32_t bh = src[i] | (src[i + 1] << 8) | (src[i + 2] << 16);
            i += 3;
            int last = bh & 1, btype = (bh >> 1) & 3;
            size_t bsz = bh >> 3;
            if (btype == 0) {
                if (n - i < bsz) return false;
                out.insert(out.end(), src + i, src + i + bsz);
                i += bsz;
            } else if (btype == 1) {
                if (i >= n) return false;
                out.insert(out.end(), bsz, src[i]);
                i += 1;
            } else if (btype == 2) {
                if (n - i < bsz || bsz > (128u << 10)) return false;
                if (!DecodeBlock(src + i, bsz, fs, out, start)) return false;
                i += bsz;
            } else {
                return false;
            }
            if (last) break;
        }
        if (hasFcs && out.size() - start != fcs) return false;
        if (check) {
            if (n - i < 4) return false;
            uint32_t want = Rd32(src + i);
            i += 4;
            if (static_cast<uint32_t>(Xxh64(out.data() + start, out.size() - start)) != want) return false;
        }
        any = true;
    }
    return any;
}
}
