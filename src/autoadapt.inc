#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>
namespace rx {
struct Route {
    uint8_t type;
    uint16_t dep;
    int32_t a, b;
    uint16_t str;
};
struct Sym {
    uint32_t old;
    uint32_t kind, flags;
    uint32_t len;
    uint32_t pat, patLen;
    uint32_t keep, keepN;
    uint32_t route, routeN;
    uint32_t base, delta;
};
#include "recipes.inc"
constexpr size_t kN = sizeof kSyms / sizeof kSyms[0];
constexpr uint32_t kWait = 0xffffffffu;
using LogFn = void (*)(const char*, ...);
struct Sec {
    uint32_t va = 0, size = 0;
    std::vector<uint8_t> buf;
    bool Has(uint32_t rva, uint32_t n) const { return rva >= va && rva + n <= va + buf.size(); }
    const uint8_t* At(uint32_t rva) const { return buf.data() + (rva - va); }
};
struct Img {
    uintptr_t base = 0;
    uint32_t imageSize = 0, stamp = 0;
    Sec code, rdata, data;
    std::vector<std::pair<uint32_t, uint32_t>> pdata;
    std::unordered_map<uint32_t, std::vector<uint32_t>> byLen;
    std::unordered_map<uint32_t, uint32_t> startLen;
    bool Copy(Sec& s, uint32_t va, uint32_t n) {
        s.va = va;
        s.size = n;
        s.buf.assign(n, 0);
        for (uint32_t o = 0; o < n; o += 1u << 20) {
            SIZE_T want = std::min<uint32_t>(1u << 20, n - o), got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(base + va + o), s.buf.data() + o, want, &got) || got != want)
                return false;
        }
        return true;
    }
    bool Load(uintptr_t b) {
        base = b;
        IMAGE_DOS_HEADER dos;
        IMAGE_NT_HEADERS64 nt;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(b), &dos, sizeof dos, &got) || dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(b + dos.e_lfanew), &nt, sizeof nt, &got) || nt.Signature != IMAGE_NT_SIGNATURE)
            return false;
        imageSize = nt.OptionalHeader.SizeOfImage;
        stamp = nt.FileHeader.TimeDateStamp;
        WORD ns = nt.FileHeader.NumberOfSections;
        if (ns < 3) return false;
        std::vector<IMAGE_SECTION_HEADER> sh(ns);
        uintptr_t sp = b + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(sp), sh.data(), ns * sizeof(IMAGE_SECTION_HEADER), &got)) return false;
        auto sz = [](const IMAGE_SECTION_HEADER& h) { return h.SizeOfRawData < h.Misc.VirtualSize ? h.SizeOfRawData : h.Misc.VirtualSize; };
        if (!(sh[0].Characteristics & IMAGE_SCN_MEM_EXECUTE)) return false;
        if (!Copy(code, sh[0].VirtualAddress, sz(sh[0])) || !Copy(rdata, sh[1].VirtualAddress, sz(sh[1])) || !Copy(data, sh[2].VirtualAddress, sz(sh[2])))
            return false;
        auto& ex = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (!ex.VirtualAddress || !ex.Size) return false;
        std::vector<RUNTIME_FUNCTION> rf(ex.Size / sizeof(RUNTIME_FUNCTION));
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(b + ex.VirtualAddress), rf.data(), rf.size() * sizeof(RUNTIME_FUNCTION), &got))
            return false;
        pdata.reserve(rf.size());
        for (auto& r : rf) pdata.emplace_back(r.BeginAddress, r.EndAddress);
        std::sort(pdata.begin(), pdata.end());
        for (auto& p : pdata) {
            byLen[p.second - p.first].push_back(p.first);
            startLen[p.first] = p.second - p.first;
        }
        return true;
    }
    const Sec* SecOf(uint32_t rva) const {
        for (const Sec* s : {&code, &rdata, &data})
            if (rva >= s->va && rva < s->va + s->buf.size()) return s;
        return nullptr;
    }
    bool Read(uint32_t rva, void* out, uint32_t n) const {
        const Sec* s = SecOf(rva);
        if (!s || !s->Has(rva, n)) return false;
        memcpy(out, s->At(rva), n);
        return true;
    }
};
struct Resolver {
    const Img& im;
    std::vector<uint32_t> res;
    std::vector<uint8_t> state;
    std::vector<uint16_t> how;
    std::vector<const char*> why;
    std::vector<uint8_t> loose;
    std::unordered_map<uint32_t, std::vector<uint32_t>> strRefs;
    std::unordered_map<uint32_t, uint32_t> memo;
    std::unordered_map<uint32_t, std::vector<uint32_t>> candMemo;
    explicit Resolver(const Img& i) : im(i), res(kN, 0), state(kN, 0), how(kN, 0), why(kN, nullptr), loose(kN, 0) {}
    const std::vector<uint32_t>& Copies(size_t i) {
        auto it = candMemo.find(static_cast<uint32_t>(i));
        if (it != candMemo.end()) return it->second;
        const Sym& s = kSyms[i];
        return candMemo[static_cast<uint32_t>(i)] = (s.flags & 1) ? Candidates(s) : Anywhere(s, 1000);
    }
    bool Match(const Sym& s, uint32_t rva) const {
        if (!im.code.Has(rva, s.patLen)) return false;
        const uint8_t* p = im.code.At(rva);
        for (uint32_t k = 0; k < s.keepN; ++k) {
            uint32_t o = kKeep[s.keep + k][0], l = kKeep[s.keep + k][1];
            if (memcmp(p + o, kPat + s.pat + o, l) != 0) return false;
        }
        return true;
    }
    std::vector<uint32_t> Candidates(const Sym& s) const {
        std::vector<uint32_t> out;
        auto it = im.byLen.find(s.len);
        if (it == im.byLen.end()) return out;
        for (uint32_t c : it->second)
            if (Match(s, c)) out.push_back(c);
        return out;
    }
    std::unordered_map<uint32_t, std::vector<uint32_t>> anyMemo;
    std::vector<uint32_t> Anywhere(const Sym& s, size_t cap) const {
        auto it = anyMemo.find(static_cast<uint32_t>(&s - kSyms));
        if (it != anyMemo.end()) {
            std::vector<uint32_t> v = it->second;
            if (v.size() > cap) v.resize(cap);
            return v;
        }
        return AnywhereScan(s, cap);
    }
    void PrepareAnywhere() {
        std::vector<uint32_t> want;
        for (size_t i = 0; i < kN; ++i) {
            const Sym& s = kSyms[i];
            if (!s.keepN) continue;
            bool need = !(s.flags & 1) && s.kind == 0;
            for (uint32_t k = 0; k < s.routeN; ++k) {
                uint8_t t = kRoutes[s.route + k].type;
                if (t == 2 || t == 3) need = true;
            }
            if (need) want.push_back(static_cast<uint32_t>(i));
        }
        struct Key { uint32_t key, sym, ao; };
        std::vector<Key> keys;
        std::vector<uint64_t> bits(65536 / 64, 0);
        for (uint32_t i : want) {
            const Sym& s = kSyms[i];
            uint32_t ao = 0, al = 0;
            for (uint32_t k = 0; k < s.keepN; ++k)
                if (kKeep[s.keep + k][1] > al) ao = kKeep[s.keep + k][0], al = kKeep[s.keep + k][1];
            if (al < 4) {
                anyMemo[i] = AnywhereScan(s, 1000);
                continue;
            }
            uint32_t key;
            memcpy(&key, kPat + s.pat + ao, 4);
            keys.push_back({key, i, ao});
            bits[(key & 0xffff) >> 6] |= 1ull << (key & 63);
            anyMemo[i];
        }
        std::sort(keys.begin(), keys.end(), [](const Key& x, const Key& y) { return x.key < y.key; });
        const uint8_t* c = im.code.buf.data();
        const size_t n = im.code.buf.size();
        for (size_t p = 0; p + 4 <= n; ++p) {
            uint32_t w;
            memcpy(&w, c + p, 4);
            if (!(bits[(w & 0xffff) >> 6] >> (w & 63) & 1)) continue;
            auto lo = std::lower_bound(keys.begin(), keys.end(), w, [](const Key& x, uint32_t v) { return x.key < v; });
            for (; lo != keys.end() && lo->key == w; ++lo) {
                if (p < lo->ao) continue;
                uint32_t st = im.code.va + static_cast<uint32_t>(p) - lo->ao;
                auto& v = anyMemo[lo->sym];
                if (v.size() < 1000 && Match(kSyms[lo->sym], st)) v.push_back(st);
            }
        }
    }
    std::vector<uint32_t> AnywhereScan(const Sym& s, size_t cap) const {
        std::vector<uint32_t> out;
        uint32_t ao = 0, al = 0;
        for (uint32_t k = 0; k < s.keepN; ++k)
            if (kKeep[s.keep + k][1] > al) ao = kKeep[s.keep + k][0], al = kKeep[s.keep + k][1];
        if (!al) return out;
        const uint8_t* first = im.code.buf.data();
        const uint8_t* last = first + im.code.buf.size();
        std::boyer_moore_horspool_searcher<const uint8_t*> srch(kPat + s.pat + ao, kPat + s.pat + ao + al);
        for (const uint8_t* p = first; out.size() < cap;) {
            auto hit = srch(p, last);
            if (hit.first == last) break;
            p = hit.first + 1;
            if (hit.first - first < static_cast<ptrdiff_t>(ao)) continue;
            uint32_t c = im.code.va + static_cast<uint32_t>(hit.first - first) - ao;
            if (Match(s, c)) out.push_back(c);
        }
        return out;
    }
    std::vector<uint32_t> FindString(const char* str) const {
        std::vector<uint32_t> out;
        size_t n = strlen(str) + 1;
        for (const Sec* s : {&im.rdata, &im.data}) {
            const uint8_t* first = s->buf.data();
            const uint8_t* last = first + s->buf.size();
            std::boyer_moore_horspool_searcher<const uint8_t*> srch(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + n);
            for (const uint8_t* p = first; out.size() < 4;) {
                auto hit = srch(p, last);
                if (hit.first == last) break;
                p = hit.first + 1;
                if (hit.first == first || hit.first[-1] == 0) out.push_back(s->va + static_cast<uint32_t>(hit.first - first));
            }
        }
        return out;
    }
    std::vector<uint32_t> QwordRefs(uint32_t target) const {
        std::vector<uint32_t> out;
        uint64_t want = 0x140000000ull + target;
        for (const Sec* s : {&im.rdata, &im.data}) {
            const size_t n = s->buf.size() / 8;
            const uint64_t* q = reinterpret_cast<const uint64_t*>(s->buf.data());
            for (size_t i = 0; i < n && out.size() < 16; ++i)
                if (q[i] == want) out.push_back(s->va + static_cast<uint32_t>(i * 8));
        }
        return out;
    }
    void PrepareStrRefs() {
        std::vector<std::pair<uint32_t, uint32_t>> want;
        uint32_t lo = 0xffffffffu, hi = 0;
        for (size_t i = 0; i < kN; ++i)
            for (uint32_t k = 0; k < kSyms[i].routeN; ++k) {
                const Route& r = kRoutes[kSyms[i].route + k];
                if (r.type != 9) continue;
                for (uint32_t sa : FindString(kStrs[r.str])) {
                    want.emplace_back(sa, static_cast<uint32_t>(r.b - r.a - 4));
                    lo = std::min(lo, sa), hi = std::max(hi, sa);
                }
            }
        if (want.empty()) return;
        std::sort(want.begin(), want.end());
        const uint8_t* c = im.code.buf.data();
        const size_t n = im.code.buf.size();
        for (size_t i = 0; i + 4 <= n; ++i) {
            int32_t d;
            memcpy(&d, c + i, 4);
            const int64_t t0 = static_cast<int64_t>(im.code.va) + static_cast<int64_t>(i) + 4 + d;
            if (t0 + 8 < lo || t0 > hi) continue;
            for (auto& w : want)
                if (t0 + w.second == w.first) strRefs[w.first ^ (w.second << 28)].push_back(im.code.va + static_cast<uint32_t>(i));
        }
    }
    uint32_t RouteAt(size_t i, const Route& r) {
        if (r.type == 1 || r.type == 2 || r.type == 3 || r.type == 8 || r.type == 9) {
            const uint32_t key = static_cast<uint32_t>(&r - kRoutes);
            auto it = memo.find(key);
            if (it != memo.end()) return it->second;
            return memo[key] = RouteCalc(i, r);
        }
        return RouteCalc(i, r);
    }
    uint32_t RouteCalc(size_t i, const Route& r) {
        const Sym& s = kSyms[i];
        switch (r.type) {
            case 1: {
                auto c = Candidates(s);
                return c.size() == 1 ? c[0] : 0;
            }
            case 2:
            case 3: {
                auto c = Anywhere(s, 3);
                return c.size() == 1 ? c[0] : 0;
            }
            case 4:
            case 5:
            case 6: {
                if (state[r.dep] == 2) return 0;
                if (state[r.dep] == 0) return kWait;
                uint32_t nd = res[r.dep];
                if (r.type == 6) return nd + r.a;
                if (r.type == 4) {
                    int32_t d;
                    if (!im.Read(nd + r.a, &d, 4)) return 0;
                    return static_cast<uint32_t>(static_cast<int64_t>(nd) + r.b + d);
                }
                uint64_t q;
                if (!im.Read(nd + r.a, &q, 8)) return 0;
                uint64_t t = q - 0x140000000ull;
                return (q > 0x140000000ull && t < im.imageSize && im.code.Has(static_cast<uint32_t>(t), 1)) ? static_cast<uint32_t>(t) : 0;
            }
            case 7: {
                if (state[r.dep] == 2) return 0;
                if (state[r.dep] == 0) return kWait;
                const auto& cs = Copies(i);
                uint32_t hit = 0, nhit = 0;
                for (uint32_t c : cs) {
                    int32_t d;
                    if (im.Read(c + r.a, &d, 4) && static_cast<uint32_t>(static_cast<int64_t>(c) + r.b + d) == res[r.dep]) hit = c, ++nhit;
                }
                return nhit == 1 ? hit : 0;
            }
            case 8: {
                std::vector<uint32_t> fns;
                for (uint32_t sa : FindString(kStrs[r.str]))
                    for (uint32_t slot : QwordRefs(sa)) {
                        uint64_t q;
                        if (im.Read(slot + r.a, &q, 8) && q > 0x140000000ull && im.code.Has(static_cast<uint32_t>(q - 0x140000000ull), 1))
                            fns.push_back(static_cast<uint32_t>(q - 0x140000000ull));
                    }
                const auto& cs = Copies(i);
                uint32_t hit = 0, nhit = 0;
                for (uint32_t c : cs)
                    if (std::find(fns.begin(), fns.end(), c) != fns.end()) hit = c, ++nhit;
                return nhit == 1 ? hit : 0;
            }
            case 9: {
                uint32_t hit = 0, nhit = 0;
                for (uint32_t sa : FindString(kStrs[r.str])) {
                    auto it = strRefs.find(sa ^ (static_cast<uint32_t>(r.b - r.a - 4) << 28));
                    if (it == strRefs.end()) continue;
                    for (uint32_t disp : it->second) {
                        uint32_t st = disp - r.a;
                        if (Match(s, st)) hit = st, ++nhit;
                    }
                }
                return nhit == 1 ? hit : 0;
            }
        }
        return 0;
    }
    bool Verify(size_t i, uint32_t na, uint16_t via) {
        const Sym& s = kSyms[i];
        if (s.kind == 2) {
            const Sec* sec = im.SecOf(na);
            return sec && sec != &im.code;
        }
        if (s.kind == 1) return true;
        bool ok = Match(s, na);
        if (ok && (s.flags & 1)) {
            auto it = im.startLen.find(na);
            ok = it != im.startLen.end() && it->second == s.len;
        }
        if (!ok && (s.flags & 2) && (via & (1u << 4)) && im.startLen.count(na)) {
            loose[i] = 1;
            return true;
        }
        return ok;
    }
    void Decide(size_t i, const uint32_t* got, const uint8_t* types, int n) {
        uint16_t via = 0;
        for (int k = 0; k < n; ++k) via |= 1u << types[k];
        for (int k = 1; k < n; ++k)
            if (got[k] != got[0]) {
                state[i] = 2, why[i] = "routes disagree";
                return;
            }
        if (!Verify(i, got[0], via)) {
            state[i] = 2, why[i] = "pattern check failed";
            return;
        }
        res[i] = got[0], state[i] = 1, how[i] = via;
    }
    void Run() {
        PrepareStrRefs();
        PrepareAnywhere();
        size_t pending = kN;
        uint32_t got[32];
        uint8_t types[32];
        while (pending) {
            bool progress = false;
            int partI = -1, partN = 0;
            uint32_t partGot[32];
            uint8_t partTypes[32];
            for (size_t i = 0; i < kN; ++i) {
                if (state[i]) continue;
                const Sym& s = kSyms[i];
                int n = 0;
                bool waiting = false;
                for (uint32_t k = 0; k < s.routeN && n < 32; ++k) {
                    const Route& r = kRoutes[s.route + k];
                    uint32_t x = RouteAt(i, r);
                    if (x == kWait) waiting = true;
                    else if (x) got[n] = x, types[n++] = r.type;
                }
                if (!waiting) {
                    if (n) Decide(i, got, types, n);
                    else state[i] = 2, why[i] = "no route resolved";
                    --pending;
                    progress = true;
                } else if (n && partI < 0) {
                    partI = static_cast<int>(i), partN = n;
                    memcpy(partGot, got, n * sizeof got[0]);
                    memcpy(partTypes, types, n);
                }
            }
            if (!progress) {
                if (partI < 0) break;
                Decide(partI, partGot, partTypes, partN);
                --pending;
            }
        }
        for (size_t i = 0; i < kN; ++i)
            if (!state[i]) state[i] = 2, why[i] = "dependency unresolved";
    }
};
}
