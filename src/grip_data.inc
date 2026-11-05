namespace grip {
struct Prim {
    int box = 0;
    bool grip = false;
    int bone = -1;
    float a[3] = {0, 0, 0}, b[3] = {0, 0, 0}, r = 0;
    float c[3] = {0, 0, 0}, ax[9] = {0}, h[3] = {0, 0, 0};
    float o[3] = {0, 0, 0}, step = 0;
    int n[3] = {0, 0, 0};
    std::vector<signed char> sdf;
};
struct PadDef {
    std::string bone;
    float p[3];
    int palm = -1;
};
struct Finger {
    int bone[3] = {-1, -1, -1};
    std::vector<int> seg;
    std::vector<float> p;
};
struct PoseDef {
    std::string dir, bone;
    float q[4], t[3];
};
struct PoseFix {
    int bone = -1;
    float axis[3] = {1, 0, 0}, ang = 0, t[3] = {0, 0, 0};
};
struct Hist {
    bool have = false;
    float o1[3] = {0, 0, 0}, o2[3] = {0, 0, 0};
    float a1[4][3] = {}, a2[4][3] = {};
    int shape = -1, keep = -1;
};
struct State {
    std::vector<std::string> names;
    std::vector<int> parent;
    std::vector<float> bindPos;
    float palmSign[2] = {1.f, 1.f};
    Hist hist[2];
    double lastT = -1.0;
    float fixedDt = 0.f;
    std::string weaponDir;
    std::vector<PadDef> padDefs;
    bool ready = false;
    std::vector<Prim> prims;
    Finger finger[2][4];
    std::vector<int> palmBone[2];
    std::vector<float> palmP[2];
    std::vector<int> chkBone[2];
    std::vector<float> chkP[2];
    std::vector<PoseDef> poseDefs;
    std::vector<PoseFix> pose[2];
    int logLeft = 0;
};
inline std::mutex& Mx() {
    static std::mutex m;
    return m;
}
inline std::map<const void*, State>& All() {
    static std::map<const void*, State> m;
    return m;
}
inline const void*& Loading() {
    static thread_local const void* p = nullptr;
    return p;
}
inline std::string Utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
inline std::wstring Env(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH * 2);
    return n && n < MAX_PATH * 2 ? std::wstring(buf, n) : std::wstring();
}
inline std::vector<std::string> ReadLines(const std::wstring& path) {
    std::vector<std::string> out;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return out;
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
    fclose(f);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        std::string ln = all.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? all.size() : e + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
        if (!ln.empty()) out.push_back(ln);
    }
    return out;
}
inline std::vector<std::string> Split(const std::string& s, char c) {
    std::vector<std::string> f;
    size_t a = 0;
    for (;;) {
        size_t b = s.find(c, a);
        f.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return f;
}
inline void BeginLoad(const void* who, const wchar_t* path) {
    std::lock_guard<std::mutex> g(Mx());
    State& s = All()[who];
    s = State();
    std::wstring p(path);
    size_t e = p.find_last_of(L"\\/");
    std::wstring dir = e == std::wstring::npos ? L"" : p.substr(0, e);
    size_t e2 = dir.find_last_of(L"\\/");
    s.weaponDir = Utf8(e2 == std::wstring::npos ? dir : dir.substr(e2 + 1));
    Loading() = who;
}
inline void SetKit(const wchar_t* kitPath) {
    if (!Loading()) return;
    std::lock_guard<std::mutex> g(Mx());
    State& s = All()[Loading()];
    std::wstring kp(kitPath);
    if (kp.size() < 5) return;
    for (const auto& ln : ReadLines(kp.substr(0, kp.size() - 4) + L".grip.txt")) {
        auto f = Split(ln, '|');
        PadDef d{};
        if (f.size() == 3)
            d.palm = f[2] == "palm_ri" ? 0 : f[2] == "palm_le" ? 1 : f[2] == "chk_ri" ? 2 : f[2] == "chk_le" ? 3 : -2;
        if ((f.size() == 2 || d.palm >= 0) && sscanf(f[1].c_str(), "%f %f %f", &d.p[0], &d.p[1], &d.p[2]) == 3) d.bone = f[0], s.padDefs.push_back(d);
    }
    for (const auto& ln : ReadLines(kp.substr(0, kp.size() - 4) + L".pose.txt")) {
        auto f = Split(ln, '|');
        PoseDef d{};
        if (f.size() == 4 && sscanf(f[2].c_str(), "%f %f %f %f", &d.q[0], &d.q[1], &d.q[2], &d.q[3]) == 4 &&
            sscanf(f[3].c_str(), "%f %f %f", &d.t[0], &d.t[1], &d.t[2]) == 3)
            d.dir = f[0], d.bone = f[1], s.poseDefs.push_back(d);
    }
}
inline void NoteBone(const void* who, int b, const char* name, size_t nameLen, int parent, const float* invBind) {
    std::lock_guard<std::mutex> g(Mx());
    State& s = All()[who];
    if (static_cast<int>(s.names.size()) <= b) s.names.resize(b + 1), s.parent.resize(b + 1, -1), s.bindPos.resize((b + 1) * 3, 0.f);
    size_t n = 0;
    while (n < nameLen && name[n]) ++n;
    s.names[b].assign(name, n);
    s.parent[b] = parent;
    for (int c = 0; c < 3; ++c)
        s.bindPos[b * 3 + c] = -(invBind[c] * invBind[3] + invBind[4 + c] * invBind[7] + invBind[8 + c] * invBind[11]);
}
inline int Find(const State& s, const std::string& nm) {
    for (size_t i = 0; i < s.names.size(); ++i)
        if (s.names[i] == nm) return static_cast<int>(i);
    return -1;
}
inline void EndLoad(const void* who) {
    std::lock_guard<std::mutex> g(Mx());
    Loading() = nullptr;
    State& s = All()[who];
    for (const auto& d : s.poseDefs) {
        if (d.dir != s.weaponDir) continue;
        int b = Find(s, d.bone), sd = -1;
        for (int q = b; q >= 0 && sd < 0; q = s.parent[q])
            sd = s.names[q] == "j_wrist_ri" ? 0 : (s.names[q] == "j_wrist_le" ? 1 : -1);
        if (b < 0 || sd < 0) continue;
        PoseFix p;
        p.bone = b;
        float w = d.q[3] < 0 ? -d.q[3] : d.q[3], sgn = d.q[3] < 0 ? -1.f : 1.f;
        w = w > 1.f ? 1.f : w;
        p.ang = 2.f * std::acos(w);
        float sn = std::sqrt(1.f - w * w);
        if (sn > 1e-6f) p.axis[0] = sgn * d.q[0] / sn, p.axis[1] = sgn * d.q[1] / sn, p.axis[2] = sgn * d.q[2] / sn;
        p.t[0] = d.t[0], p.t[1] = d.t[1], p.t[2] = d.t[2];
        s.pose[sd].push_back(p);
    }
    s.fixedDt = static_cast<float>(_wtof(Env(L"HFPS_GRIP_DT").c_str()));
    s.logLeft = Env(L"HFPS_GRIP_LOG").empty() ? 0 : (s.fixedDt > 0.f ? 1000000 : 3);
    s.ready = !s.pose[0].empty() || !s.pose[1].empty();
    if (s.padDefs.empty()) return;
    for (int sd = 0; sd < 2; ++sd) {
        static const char* const kPalm[2][4] = {{"j_wrist_ri", "j_index_ri_1", "j_pinky_ri_1", "j_thumb_ri_3"},
                                                {"j_wrist_le", "j_index_le_1", "j_pinky_le_1", "j_thumb_le_3"}};
        const char* const* nm = kPalm[sd];
        int w = Find(s, nm[0]), i = Find(s, nm[1]), p = Find(s, nm[2]), t = Find(s, nm[3]);
        if (w < 0 || i < 0 || p < 0 || t < 0) continue;
        const float* bp = s.bindPos.data();
        float e1[3], e2[3], et[3];
        for (int c = 0; c < 3; ++c) e1[c] = bp[i * 3 + c] - bp[w * 3 + c], e2[c] = bp[p * 3 + c] - bp[w * 3 + c], et[c] = bp[t * 3 + c] - bp[w * 3 + c];
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        s.palmSign[sd] = n[0] * et[0] + n[1] * et[1] + n[2] * et[2] < 0.f ? -1.f : 1.f;
    }
    static const char* const kChain[2][4][3] = {
        {{"j_index_ri_1", "j_index_ri_2", "j_index_ri_3"}, {"bd207b43e7108354", "bd207e43e710886d", "bd207d43e71086ba"},
         {"j_ring_ri_1", "j_ring_ri_2", "j_ring_ri_3"}, {"j_pinky_ri_1", "j_pinky_ri_2", "j_pinky_ri_3"}},
        {{"j_index_le_1", "j_index_le_2", "j_index_le_3"}, {"725b9714677d354e", "725b9614677d339b", "725b9514677d31e8"},
         {"j_ring_le_1", "j_ring_le_2", "j_ring_le_3"}, {"j_pinky_le_1", "j_pinky_le_2", "j_pinky_le_3"}}};
    for (int sd = 0; sd < 2; ++sd)
        for (int f = 0; f < 4; ++f) {
            Finger& F = s.finger[sd][f];
            for (int k = 0; k < 3; ++k) F.bone[k] = Find(s, kChain[sd][f][k]);
            bool chainOk = F.bone[0] >= 0 && F.bone[1] >= 0 && F.bone[2] >= 0 && s.parent[F.bone[0]] >= 0 &&
                           s.parent[F.bone[1]] == F.bone[0] && s.parent[F.bone[2]] == F.bone[1];
            if (!chainOk) {
                F.bone[0] = -1;
                continue;
            }
            for (const auto& d : s.padDefs)
                for (int k = 0; k < 3; ++k)
                    if (d.palm < 0 && d.bone == kChain[sd][f][k]) F.seg.push_back(k), F.p.insert(F.p.end(), d.p, d.p + 3);
        }
    for (const auto& d : s.padDefs) {
        int b = d.palm >= 0 ? Find(s, d.bone) : -1;
        if (b >= 0 && d.palm < 2) s.palmBone[d.palm].push_back(b), s.palmP[d.palm].insert(s.palmP[d.palm].end(), d.p, d.p + 3);
        if (b >= 0 && d.palm >= 2) s.chkBone[d.palm - 2].push_back(b), s.chkP[d.palm - 2].insert(s.chkP[d.palm - 2].end(), d.p, d.p + 3);
    }
    std::wstring gp = Env(L"HFPS_GRIPS");
    if (gp.empty()) return;
    for (const auto& ln : ReadLines(gp)) {
        auto f = Split(ln, '|');
        if (f.size() != 6 || f[0] != s.weaponDir) continue;
        Prim c;
        c.bone = Find(s, f[2]);
        if (c.bone < 0) continue;
        bool ok = false;
        if (f[1] == "ri" || f[1] == "le") {
            c.grip = true;
            ok = sscanf(f[3].c_str(), "%f %f %f", &c.a[0], &c.a[1], &c.a[2]) == 3 && sscanf(f[4].c_str(), "%f %f %f", &c.b[0], &c.b[1], &c.b[2]) == 3 &&
                 sscanf(f[5].c_str(), "%f", &c.r) == 1;
        } else if (f[1] == "box") {
            float* x = c.ax;
            c.box = 1;
            ok = sscanf(f[3].c_str(), "%f %f %f", &c.c[0], &c.c[1], &c.c[2]) == 3 &&
                 sscanf(f[4].c_str(), "%f %f %f %f %f %f %f %f %f", x, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7, x + 8) == 9 &&
                 sscanf(f[5].c_str(), "%f %f %f", &c.h[0], &c.h[1], &c.h[2]) == 3;
        } else if (f[1] == "grid" || f[1] == "gridgrip") {
            c.box = 2;
            c.grip = f[1] == "gridgrip";
            ok = sscanf(f[3].c_str(), "%f %f %f", &c.o[0], &c.o[1], &c.o[2]) == 3 &&
                 sscanf(f[4].c_str(), "%f %d %d %d", &c.step, &c.n[0], &c.n[1], &c.n[2]) == 4 && c.step > 0 && c.n[0] > 1 && c.n[1] > 1 && c.n[2] > 1;
            if (ok) {
                size_t e = gp.find_last_of(L"\\/");
                int wl = MultiByteToWideChar(CP_UTF8, 0, f[5].c_str(), -1, nullptr, 0);
                std::wstring fn(wl > 0 ? wl - 1 : 0, L'\0');
                if (wl > 1) MultiByteToWideChar(CP_UTF8, 0, f[5].c_str(), -1, &fn[0], wl);
                FILE* fp = _wfopen(((e == std::wstring::npos ? L"" : gp.substr(0, e + 1)) + L"握把形状\\" + fn).c_str(), L"rb");
                c.sdf.assign(static_cast<size_t>(c.n[0]) * c.n[1] * c.n[2], 127);
                ok = fp && fread(c.sdf.data(), 1, c.sdf.size(), fp) == c.sdf.size();
                if (fp) fclose(fp);
            }
        }
        if (ok) s.prims.push_back(std::move(c));
    }
    s.ready = s.ready || !s.prims.empty();
}
}
