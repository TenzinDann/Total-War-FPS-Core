namespace lowpoly {
struct LpVert {
    float p[3], n[3], t[4], uv[2];
    uint8_t bi[4], bw[4];
};
static_assert(sizeof(LpVert) == 56, "hvm 顶点 56 字节");
inline uint32_t Crc32(const uint8_t* p, size_t n) {
    static uint32_t t[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
inline bool ReadAll(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(f, &sz) && sz.QuadPart < (1ll << 31);
    if (ok) {
        out.resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
    }
    CloseHandle(f);
    return ok;
}
inline bool WriteAll(const std::wstring& path, const std::vector<uint8_t>& data) {
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &put, nullptr) && put == data.size();
    CloseHandle(f);
    if (!ok) return DeleteFileW(tmp.c_str()), false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
inline std::wstring Wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
inline std::string Lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '\\') c = '/';
    }
    return s;
}
struct PackHit {
    std::wstring pack;
    unsigned long long off = 0;
    uint32_t size = 0;
    uint8_t comp = 0;
};
inline bool FindInPack(const std::wstring& pack, const std::string& want, PackHit& hit) {
    HANDLE f = CreateFileW(pack.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    uint8_t head[28];
    DWORD got = 0;
    if (ReadFile(f, head, 28, &got, nullptr) && got == 28 && (!memcmp(head, "PFH5", 4) || !memcmp(head, "PFH4", 4))) {
        bool v5 = !memcmp(head, "PFH5", 4);
        uint32_t flags, npack, pidx, nfile, fidx;
        memcpy(&flags, head + 4, 4), memcpy(&npack, head + 8, 4), memcpy(&pidx, head + 12, 4), memcpy(&nfile, head + 16, 4), memcpy(&fidx, head + 20, 4);
        (void)npack;
        uint32_t hsize = 28 + ((v5 && (flags & 0x100)) ? 20 : 0);
        if (!(flags & (0x80 | 0x10)) && fidx < (512u << 20)) {
            std::vector<uint8_t> idx(fidx);
            LARGE_INTEGER pos;
            pos.QuadPart = hsize + pidx;
            if (SetFilePointerEx(f, pos, nullptr, FILE_BEGIN) && ReadFile(f, idx.data(), fidx, &got, nullptr) && got == fidx) {
                unsigned long long dataOff = static_cast<unsigned long long>(hsize) + pidx + fidx;
                size_t o = 0;
                for (uint32_t i = 0; i < nfile && o + 4 <= idx.size(); ++i) {
                    uint32_t sz;
                    memcpy(&sz, idx.data() + o, 4), o += 4;
                    if (flags & 0x40) o += 4;
                    uint8_t comp = 0;
                    if (v5) comp = idx[o++];
                    size_t e = o;
                    while (e < idx.size() && idx[e]) ++e;
                    if (e >= idx.size()) break;
                    if (e - o == want.size() && Lower(std::string(reinterpret_cast<const char*>(idx.data() + o), e - o)) == want) {
                        hit.pack = pack, hit.off = dataOff, hit.size = sz, hit.comp = comp;
                        found = true;
                        break;
                    }
                    o = e + 1;
                    dataOff += sz;
                }
            }
        }
    }
    CloseHandle(f);
    return found;
}
inline std::wstring GameData() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"HFPS_GAMEDATA", buf, MAX_PATH * 2);
    if (n && n < MAX_PATH * 2) return buf;
    n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::wstring p(buf, n);
    size_t s = p.find_last_of(L"\\/");
    return (s == std::wstring::npos ? p : p.substr(0, s)) + L"\\data";
}
inline bool VanillaFile(const std::string& hintPack, const std::string& rel, uint32_t rawSize, uint32_t crc, std::vector<uint8_t>& out,
                        std::string& note) {
    std::wstring data = GameData();
    std::string want = Lower(rel);
    PackHit hit;
    bool found = !hintPack.empty() && FindInPack(data + L"\\" + Wide(hintPack), want, hit);
    if (!found) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((data + L"\\*.pack").c_str(), &fd);
        std::vector<std::wstring> names;
        if (h != INVALID_HANDLE_VALUE) {
            do names.push_back(fd.cFileName);
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(names.begin(), names.end());
        for (const auto& nm : names) {
            PackHit h2;
            if (FindInPack(data + L"\\" + nm, want, h2)) hit = h2, found = true;
        }
    }
    if (!found) return note = "vanilla file not found: " + rel, false;
    std::vector<uint8_t> raw(hit.size);
    HANDLE f = CreateFileW(hit.pack.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return note = "pack open failed", false;
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<long long>(hit.off);
    DWORD got = 0;
    bool ok = SetFilePointerEx(f, pos, nullptr, FILE_BEGIN) && ReadFile(f, raw.data(), hit.size, &got, nullptr) && got == hit.size;
    CloseHandle(f);
    if (!ok) return note = "pack read failed", false;
    if (hit.comp) {
        if (raw.size() < 8 || zstdd::Rd32(raw.data() + 4) != 0xFD2FB528u) return note = "vanilla entry not zstd: " + rel, false;
        if (!zstdd::Decompress(raw.data() + 4, raw.size() - 4, out) || out.size() != zstdd::Rd32(raw.data()))
            return note = "zstd decode failed: " + rel, false;
    } else {
        out.swap(raw);
    }
    if (out.size() != rawSize || Crc32(out.data(), out.size()) != crc) return note = "vanilla file changed (size / crc): " + rel, false;
    return true;
}
struct Mid {
    uint64_t key;
    uint32_t idx;
};
inline void MixW(const LpVert& a, const LpVert& b, LpVert& o) {
    int bone[8];
    float w[8];
    int n = 0;
    auto add = [&](int bi, float wv) {
        for (int k = 0; k < n; ++k)
            if (bone[k] == bi) return void(w[k] += wv);
        bone[n] = bi, w[n++] = wv;
    };
    for (int k = 0; k < 4; ++k) {
        if (a.bw[k]) add(a.bi[k], 0.5f * a.bw[k]);
        if (b.bw[k]) add(b.bi[k], 0.5f * b.bw[k]);
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (w[j] > w[i]) std::swap(w[i], w[j]), std::swap(bone[i], bone[j]);
    int m = n < 4 ? n : 4;
    float s = 0;
    for (int k = 0; k < m; ++k) s += w[k];
    int q[4] = {0, 0, 0, 0}, sum = 0, big = 0;
    for (int k = 0; k < 4; ++k) {
        o.bi[k] = k < m ? static_cast<uint8_t>(bone[k]) : 0;
        q[k] = k < m && s > 0 ? static_cast<int>(w[k] / s * 255.f + 0.5f) : 0;
        sum += q[k];
        if (q[k] > q[big]) big = k;
    }
    q[big] += 255 - sum;
    for (int k = 0; k < 4; ++k) o.bw[k] = static_cast<uint8_t>(q[k] < 0 ? 0 : q[k]);
}
inline bool Subdivide(std::vector<LpVert>& v, std::vector<uint32_t>& idx) {
    const size_t nv = v.size(), nt = idx.size() / 3;
    std::map<std::array<long long, 3>, uint32_t> weldMap;
    std::vector<uint32_t> wid(nv);
    std::vector<uint32_t> rep;
    for (size_t i = 0; i < nv; ++i) {
        std::array<long long, 3> k = {std::llround(v[i].p[0] * 1e6), std::llround(v[i].p[1] * 1e6), std::llround(v[i].p[2] * 1e6)};
        auto it = weldMap.find(k);
        if (it == weldMap.end()) {
            it = weldMap.emplace(k, static_cast<uint32_t>(rep.size())).first;
            rep.push_back(static_cast<uint32_t>(i));
        }
        wid[i] = it->second;
    }
    const size_t nw = rep.size();
    std::map<uint64_t, std::vector<uint32_t>> edges;
    auto ek = [](uint32_t a, uint32_t b) { return a < b ? (static_cast<uint64_t>(a) << 32) | b : (static_cast<uint64_t>(b) << 32) | a; };
    for (size_t t = 0; t < nt; ++t) {
        uint32_t a = wid[idx[t * 3]], b = wid[idx[t * 3 + 1]], c = wid[idx[t * 3 + 2]];
        edges[ek(a, b)].push_back(c), edges[ek(b, c)].push_back(a), edges[ek(c, a)].push_back(b);
    }
    std::vector<std::vector<uint32_t>> nb(nw), bnd(nw);
    for (auto& e : edges) {
        uint32_t u = static_cast<uint32_t>(e.first >> 32), w = static_cast<uint32_t>(e.first & 0xFFFFFFFFu);
        nb[u].push_back(w), nb[w].push_back(u);
        if (e.second.size() == 1) bnd[u].push_back(w), bnd[w].push_back(u);
    }
    auto P = [&](uint32_t w, int c) { return static_cast<double>(v[rep[w]].p[c]); };
    std::vector<std::array<double, 3>> Pe(nw);
    for (size_t i = 0; i < nw; ++i) {
        std::sort(nb[i].begin(), nb[i].end());
        nb[i].erase(std::unique(nb[i].begin(), nb[i].end()), nb[i].end());
        for (int c = 0; c < 3; ++c) {
            if (bnd[i].size() == 2) {
                Pe[i][c] = 0.75 * P(static_cast<uint32_t>(i), c) + 0.125 * (P(bnd[i][0], c) + P(bnd[i][1], c));
            } else {
                size_t k = nb[i].size();
                if (k < 3) {
                    Pe[i][c] = P(static_cast<uint32_t>(i), c);
                    continue;
                }
                double beta = k == 3 ? 3.0 / 16 : 3.0 / (8.0 * k), s = 0;
                for (uint32_t q : nb[i]) s += P(q, c);
                Pe[i][c] = (1 - k * beta) * P(static_cast<uint32_t>(i), c) + beta * s;
            }
        }
    }
    std::vector<LpVert> out(v);
    for (size_t i = 0; i < nv; ++i)
        for (int c = 0; c < 3; ++c) out[i].p[c] = static_cast<float>(Pe[wid[i]][c]);
    std::unordered_map<uint64_t, uint32_t> mid;
    auto midOf = [&](uint32_t a, uint32_t b) -> uint32_t {
        uint64_t key = ek(a, b);
        auto it = mid.find(key);
        if (it != mid.end()) return it->second;
        uint32_t wa = wid[a], wb = wid[b];
        const auto& opp = edges[ek(wa, wb)];
        LpVert m{};
        for (int c = 0; c < 3; ++c) {
            m.p[c] = static_cast<float>(opp.size() == 2 ? 0.375 * (P(wa, c) + P(wb, c)) + 0.125 * (P(opp[0], c) + P(opp[1], c)) : 0.5 * (P(wa, c) + P(wb, c)));
            m.n[c] = v[a].n[c] + v[b].n[c];
            m.t[c] = v[a].t[c] + v[b].t[c];
        }
        m.t[3] = v[a].t[3];
        m.uv[0] = 0.5f * (v[a].uv[0] + v[b].uv[0]), m.uv[1] = 0.5f * (v[a].uv[1] + v[b].uv[1]);
        MixW(v[a], v[b], m);
        out.push_back(m);
        uint32_t id = static_cast<uint32_t>(out.size() - 1);
        mid.emplace(key, id);
        return id;
    };
    std::vector<uint32_t> ni;
    ni.reserve(idx.size() * 4);
    for (size_t t = 0; t < nt; ++t) {
        uint32_t a = idx[t * 3], b = idx[t * 3 + 1], c = idx[t * 3 + 2];
        uint32_t ab = midOf(a, b), bc = midOf(b, c), ca = midOf(c, a);
        const uint32_t tri4[12] = {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca};
        ni.insert(ni.end(), tri4, tri4 + 12);
    }
    std::map<std::array<long long, 3>, uint32_t> wm2;
    std::vector<uint32_t> w2(out.size());
    std::vector<std::array<double, 3>> acc;
    for (size_t i = 0; i < out.size(); ++i) {
        std::array<long long, 3> k = {std::llround(out[i].p[0] * 1e6), std::llround(out[i].p[1] * 1e6), std::llround(out[i].p[2] * 1e6)};
        auto it = wm2.find(k);
        if (it == wm2.end()) it = wm2.emplace(k, static_cast<uint32_t>(acc.size())).first, acc.push_back({0, 0, 0});
        w2[i] = it->second;
    }
    for (size_t t = 0; t < ni.size() / 3; ++t) {
        const float *p0 = out[ni[t * 3]].p, *p1 = out[ni[t * 3 + 1]].p, *p2 = out[ni[t * 3 + 2]].p;
        double e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        double fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (int k = 0; k < 3; ++k)
            for (int c = 0; c < 3; ++c) acc[w2[ni[t * 3 + k]]][c] += fn[c];
    }
    for (size_t i = 0; i < out.size(); ++i) {
        const auto& a = acc[w2[i]];
        double l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (l < 1e-20) continue;
        double n[3] = {a[0] / l, a[1] / l, a[2] / l};
        double d = n[0] * out[i].n[0] + n[1] * out[i].n[1] + n[2] * out[i].n[2];
        if (d < 0) n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
        for (int c = 0; c < 3; ++c) out[i].n[c] = static_cast<float>(n[c]);
        double t[3] = {out[i].t[0], out[i].t[1], out[i].t[2]};
        double tn = t[0] * n[0] + t[1] * n[1] + t[2] * n[2];
        for (int c = 0; c < 3; ++c) t[c] -= tn * n[c];
        double tl = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (tl > 1e-12)
            for (int c = 0; c < 3; ++c) out[i].t[c] = static_cast<float>(t[c] / tl);
    }
    v.swap(out);
    idx.swap(ni);
    return v.size() < (1u << 24);
}
inline bool Expand(const wchar_t* kitPath, std::vector<uint8_t>& bytes, char* err, size_t errLen) {
    auto fail = [&](const std::string& s) {
        snprintf(err, errLen, "lowpoly: %s", s.c_str());
        return false;
    };
    std::wstring kp(kitPath);
    if (kp.size() < 8 || kp.compare(kp.size() - 7, 7, L".lp.hvm")) return fail("not .lp.hvm");
    std::wstring dir = kp.substr(0, kp.find_last_of(L"\\/"));
    std::vector<uint8_t> txt;
    if (!ReadAll(kp.substr(0, kp.size() - 4) + L".txt", txt)) return fail("sidecar missing");
    int subdiv = 0;
    struct TexReq {
        int idx;
        std::string hint, rel;
        uint32_t size, crc;
    };
    std::vector<TexReq> texReq;
    std::string all(txt.begin(), txt.end());
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        std::string ln = all.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? all.size() : e + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
        if (!ln.compare(0, 7, "subdiv=")) subdiv = atoi(ln.c_str() + 7);
        if (!ln.compare(0, 4, "tex=")) {
            std::vector<std::string> f;
            size_t a = 4;
            for (;;) {
                size_t b = ln.find('|', a);
                f.push_back(ln.substr(a, b == std::string::npos ? std::string::npos : b - a));
                if (b == std::string::npos) break;
                a = b + 1;
            }
            if (f.size() != 5) return fail("bad tex line");
            texReq.push_back({atoi(f[0].c_str()), f[1], f[2], static_cast<uint32_t>(strtoul(f[3].c_str(), nullptr, 10)),
                              static_cast<uint32_t>(strtoul(f[4].c_str(), nullptr, 16))});
        }
    }
    if (subdiv < 0 || subdiv > 3) return fail("bad subdiv");
    if (bytes.size() < sizeof(HvmHeader)) return fail("short");
    HvmHeader h;
    memcpy(&h, bytes.data(), sizeof h);
    if (memcmp(h.magic, "HVM1", 4) || h.version != 1 || h.clipCount) return fail("header");
    size_t off = sizeof h;
    std::vector<HvmTexture> tex(h.textureCount);
    std::vector<HvmSubmesh> subs(h.submeshCount);
    std::vector<HvmBone> bones(h.boneCount);
    size_t need = off + tex.size() * sizeof(HvmTexture) + subs.size() * sizeof(HvmSubmesh) + bones.size() * sizeof(HvmBone) +
                  static_cast<size_t>(h.vertexCount) * sizeof(LpVert) + static_cast<size_t>(h.indexCount) * 4;
    if (need > bytes.size()) return fail("truncated");
    memcpy(tex.data(), bytes.data() + off, tex.size() * sizeof(HvmTexture)), off += tex.size() * sizeof(HvmTexture);
    memcpy(subs.data(), bytes.data() + off, subs.size() * sizeof(HvmSubmesh)), off += subs.size() * sizeof(HvmSubmesh);
    memcpy(bones.data(), bytes.data() + off, bones.size() * sizeof(HvmBone)), off += bones.size() * sizeof(HvmBone);
    std::vector<LpVert> v(h.vertexCount);
    memcpy(v.data(), bytes.data() + off, v.size() * sizeof(LpVert)), off += v.size() * sizeof(LpVert);
    std::vector<uint32_t> idx(h.indexCount);
    memcpy(idx.data(), bytes.data() + off, idx.size() * 4);
    for (uint32_t i : idx)
        if (i >= v.size()) return fail("index range");
    for (const auto& r : texReq) {
        if (r.idx < 0 || r.idx >= static_cast<int>(tex.size())) return fail("tex index");
        tex[r.idx].file[75] = 0;
        std::wstring cache = dir + L"\\" + Wide(tex[r.idx].file);
        for (auto& c : cache)
            if (c == L'/') c = L'\\';
        std::vector<uint8_t> have;
        if (ReadAll(cache, have) && have.size() == r.size && Crc32(have.data(), have.size()) == r.crc) continue;
        std::vector<uint8_t> dec;
        std::string note;
        if (!VanillaFile(r.hint, r.rel, r.size, r.crc, dec, note)) return fail(note);
        std::wstring cdir = cache.substr(0, cache.find_last_of(L'\\'));
        CreateDirectoryW(cdir.c_str(), nullptr);
        if (!WriteAll(cache, dec)) return fail("cache write failed");
    }
    for (int s = 0; s < subdiv; ++s) {
        if (!Subdivide(v, idx)) return fail("subdivide");
        for (auto& sm : subs) sm.firstIndex *= 4, sm.indexCount *= 4;
    }
    h.vertexCount = static_cast<uint32_t>(v.size());
    h.indexCount = static_cast<uint32_t>(idx.size());
    std::vector<uint8_t> o(sizeof h + tex.size() * sizeof(HvmTexture) + subs.size() * sizeof(HvmSubmesh) + bones.size() * sizeof(HvmBone) +
                           v.size() * sizeof(LpVert) + idx.size() * 4);
    size_t q = 0;
    memcpy(o.data() + q, &h, sizeof h), q += sizeof h;
    memcpy(o.data() + q, tex.data(), tex.size() * sizeof(HvmTexture)), q += tex.size() * sizeof(HvmTexture);
    memcpy(o.data() + q, subs.data(), subs.size() * sizeof(HvmSubmesh)), q += subs.size() * sizeof(HvmSubmesh);
    memcpy(o.data() + q, bones.data(), bones.size() * sizeof(HvmBone)), q += bones.size() * sizeof(HvmBone);
    memcpy(o.data() + q, v.data(), v.size() * sizeof(LpVert)), q += v.size() * sizeof(LpVert);
    memcpy(o.data() + q, idx.data(), idx.size() * 4);
    bytes.swap(o);
    return true;
}
}
