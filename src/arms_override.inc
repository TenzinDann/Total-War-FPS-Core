namespace {
struct ArmsKit {
    wchar_t path[MAX_PATH * 2];
    char dirName[64];
    std::vector<unsigned char> data;
    HvmHeader h;
    std::vector<HvmTexture> tex;
    std::vector<HvmSubmesh> subs;
    std::vector<HvmBone> bones;
    size_t vertOff = 0, idxOff = 0;
};
std::vector<ArmsKit> g_armsKits;
char g_armsLog[4096] = "";
void ArmsLogAppend(const wchar_t* path, const char* note) {
    size_t n = strlen(g_armsLog);
    if (n + 8 < sizeof g_armsLog) snprintf(g_armsLog + n, sizeof g_armsLog - n, "%ls: %s\n", path, note);
}
void Inv34(const float* m, float* o) {
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    float s = std::fabs(det) > 1e-12f ? 1.f / det : 0.f;
    float r[9] = {A * s, -(b * i - c * h) * s, (b * f - c * e) * s, B * s, (a * i - c * g) * s, -(a * f - c * d) * s,
                  C * s, -(a * h - b * g) * s, (a * e - b * d) * s};
    for (int k = 0; k < 3; ++k) {
        o[k * 4 + 0] = r[k * 3 + 0], o[k * 4 + 1] = r[k * 3 + 1], o[k * 4 + 2] = r[k * 3 + 2];
        o[k * 4 + 3] = -(r[k * 3 + 0] * m[3] + r[k * 3 + 1] * m[7] + r[k * 3 + 2] * m[11]);
    }
}
void Mul34(const float* a, const float* b, float* o) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c)
            o[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] + a[r * 4 + 2] * b[2 * 4 + c] + (c == 3 ? a[r * 4 + 3] : 0.f);
    }
}
bool ParseKit(ArmsKit& k, char* err, size_t errLen) {
    const unsigned char* d = k.data.data();
    size_t len = k.data.size();
    if (len < sizeof(HvmHeader)) return SetErr(err, errLen, "arms kit too short"), false;
    memcpy(&k.h, d, sizeof k.h);
    if (memcmp(k.h.magic, "HVM1", 4) || k.h.version != 1 || !k.h.vertexCount || !k.h.indexCount || !k.h.submeshCount ||
        k.h.boneCount < 1 || k.h.boneCount > kMaxBones)
        return SetErr(err, errLen, "arms kit header"), false;
    size_t off = sizeof k.h;
    size_t need = off + k.h.textureCount * sizeof(HvmTexture) + k.h.submeshCount * sizeof(HvmSubmesh) + k.h.boneCount * sizeof(HvmBone) +
                  static_cast<size_t>(k.h.vertexCount) * kVertexStride + static_cast<size_t>(k.h.indexCount) * 4;
    if (need > len) return SetErr(err, errLen, "arms kit truncated"), false;
    k.tex.resize(k.h.textureCount);
    memcpy(k.tex.data(), d + off, k.tex.size() * sizeof(HvmTexture));
    off += k.tex.size() * sizeof(HvmTexture);
    k.subs.resize(k.h.submeshCount);
    memcpy(k.subs.data(), d + off, k.subs.size() * sizeof(HvmSubmesh));
    off += k.subs.size() * sizeof(HvmSubmesh);
    k.bones.resize(k.h.boneCount);
    memcpy(k.bones.data(), d + off, k.bones.size() * sizeof(HvmBone));
    off += k.bones.size() * sizeof(HvmBone);
    k.vertOff = off;
    k.idxOff = off + static_cast<size_t>(k.h.vertexCount) * kVertexStride;
    for (auto& b : k.bones) b.name[47] = 0;
    for (auto& t : k.tex) t.file[75] = 0;
    return true;
}
}
void SetArmsOverride(const wchar_t* list) {
    g_armsKits.clear();
    g_armsLog[0] = 0;
    if (!list || !list[0]) return;
    wchar_t buf[MAX_PATH * 8];
    wcsncpy(buf, list, sizeof buf / sizeof *buf - 1);
    buf[sizeof buf / sizeof *buf - 1] = 0;
    wchar_t* ctx = nullptr;
    for (wchar_t* tok = wcstok(buf, L";", &ctx); tok; tok = wcstok(nullptr, L";", &ctx)) {
        while (*tok == L' ') ++tok;
        if (!*tok) continue;
        ArmsKit k;
        wcsncpy(k.path, tok, MAX_PATH * 2 - 1);
        k.path[MAX_PATH * 2 - 1] = 0;
        unsigned char* data = nullptr;
        size_t len = 0;
        char err[160] = "";
        if (!ReadHvmV1(k.path, &data, &len, err, sizeof err)) {
            ArmsLogAppend(k.path, err);
            continue;
        }
        k.data.assign(data, data + len);
        delete[] data;
        if (!ParseKit(k, err, sizeof err)) {
            ArmsLogAppend(k.path, err);
            continue;
        }
        wchar_t tmp[MAX_PATH * 2];
        wcscpy(tmp, k.path);
        wchar_t* s1 = wcsrchr(tmp, L'\\');
        if (!s1) s1 = wcsrchr(tmp, L'/');
        if (s1) *s1 = 0;
        wchar_t* s2 = wcsrchr(tmp, L'\\');
        if (!s2) s2 = wcsrchr(tmp, L'/');
        WideCharToMultiByte(CP_UTF8, 0, s2 ? s2 + 1 : tmp, -1, k.dirName, sizeof k.dirName, nullptr, nullptr);
        g_armsKits.push_back(std::move(k));
    }
}
int ArmsOverrideCount() { return static_cast<int>(g_armsKits.size()); }
const char* ArmsOverrideLog() { return g_armsLog; }
namespace {
bool ApplyArmsOverride(unsigned char** pdata, size_t* plen, char* note, size_t noteLen) {
    note[0] = 0;
    if (g_armsKits.empty()) return false;
    const unsigned char* d = *pdata;
    size_t len = *plen;
    if (len < sizeof(HvmHeader)) return false;
    HvmHeader h;
    memcpy(&h, d, sizeof h);
    if (memcmp(h.magic, "HVM1", 4) || h.version != 1) return false;
    size_t off = sizeof h;
    std::vector<HvmTexture> tex(h.textureCount);
    memcpy(tex.data(), d + off, tex.size() * sizeof(HvmTexture));
    off += tex.size() * sizeof(HvmTexture);
    std::vector<HvmSubmesh> subs(h.submeshCount);
    memcpy(subs.data(), d + off, subs.size() * sizeof(HvmSubmesh));
    off += subs.size() * sizeof(HvmSubmesh);
    const size_t boneOff = off;
    std::vector<HvmBone> bones(h.boneCount);
    memcpy(bones.data(), d + off, bones.size() * sizeof(HvmBone));
    off += bones.size() * sizeof(HvmBone);
    const size_t vertOff = off, idxOff = vertOff + static_cast<size_t>(h.vertexCount) * kVertexStride;
    const size_t restOff = idxOff + static_cast<size_t>(h.indexCount) * 4;
    if (restOff > len) return false;
    auto isArmsTex = [&](int t) { return t >= 0 && t < static_cast<int>(tex.size()) && !strncmp(tex[t].file, "../_arms_", 9); };
    std::vector<bool> armSub(subs.size());
    int nArm = 0;
    for (size_t s = 0; s < subs.size(); ++s)
        if (isArmsTex(subs[s].albedo)) armSub[s] = true, ++nArm;
    if (!nArm) return false;
    auto findBone = [&](const char* name) {
        for (size_t b = 0; b < bones.size(); ++b)
            if (!strncmp(bones[b].name, name, 47)) return static_cast<int>(b);
        return -1;
    };
    int best = -1, bestMiss = 1 << 30;
    for (size_t ki = 0; ki < g_armsKits.size(); ++ki) {
        const ArmsKit& k = g_armsKits[ki];
        std::vector<bool> used(k.bones.size());
        const unsigned char* kv = k.data.data() + k.vertOff;
        for (uint32_t v = 0; v < k.h.vertexCount; ++v)
            for (int c = 0; c < 4; ++c)
                if (kv[v * kVertexStride + 52 + c] && kv[v * kVertexStride + 48 + c] < k.bones.size()) used[kv[v * kVertexStride + 48 + c]] = true;
        int miss = 0;
        for (size_t j = 0; j < k.bones.size(); ++j)
            if (used[j] && findBone(k.bones[j].name) < 0) ++miss;
        if (miss < bestMiss) best = static_cast<int>(ki), bestMiss = miss;
    }
    const ArmsKit& k = g_armsKits[best];
    std::vector<int> map(k.bones.size(), -1);
    std::vector<float> xf(k.bones.size() * 12);
    int fallback = 0;
    for (size_t j = 0; j < k.bones.size(); ++j) {
        int a = static_cast<int>(j), w = -1;
        while (a >= 0 && (w = findBone(k.bones[a].name)) < 0) a = k.bones[a].parent;
        if (w < 0) {
            map[j] = 0;
            static const float I[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
            memcpy(&xf[j * 12], I, sizeof I);
            ++fallback;
            continue;
        }
        map[j] = w;
        float wb[12];
        Inv34(bones[w].invBind, wb);
        Mul34(wb, k.bones[a].invBind, &xf[j * 12]);
    }
    std::vector<int> texUse(tex.size());
    for (size_t s = 0; s < subs.size(); ++s)
        if (!armSub[s])
            for (int t : {subs[s].albedo, subs[s].normal, subs[s].material})
                if (t >= 0 && t < static_cast<int>(tex.size())) texUse[t] = 1;
    std::vector<HvmTexture> ntex;
    std::vector<int> texMap(tex.size(), -1);
    for (size_t t = 0; t < tex.size(); ++t)
        if (texUse[t]) texMap[t] = static_cast<int>(ntex.size()), ntex.push_back(tex[t]);
    std::vector<int> kitTexMap(k.tex.size(), -1);
    for (size_t t = 0; t < k.tex.size(); ++t) {
        HvmTexture nt = {};
        int n = snprintf(nt.file, sizeof nt.file, "../%s/%s", k.dirName, k.tex[t].file);
        if (n <= 0 || n >= static_cast<int>(sizeof nt.file)) {
            snprintf(note, noteLen, "arms kit texture path too long: %s", k.tex[t].file);
            return false;
        }
        nt.srgb = k.tex[t].srgb;
        kitTexMap[t] = static_cast<int>(ntex.size());
        ntex.push_back(nt);
    }
    if (ntex.size() > 64) {
        snprintf(note, noteLen, "arms override: %u textures > 64", static_cast<unsigned>(ntex.size()));
        return false;
    }
    std::vector<HvmSubmesh> nsubs;
    for (size_t s = 0; s < subs.size(); ++s) {
        if (armSub[s]) continue;
        HvmSubmesh x = subs[s];
        for (int32_t* t : {&x.albedo, &x.normal, &x.material})
            *t = (*t >= 0 && *t < static_cast<int>(tex.size())) ? texMap[*t] : -1;
        nsubs.push_back(x);
    }
    for (const HvmSubmesh& ks : k.subs) {
        HvmSubmesh x = ks;
        x.firstIndex += h.indexCount;
        for (int32_t* t : {&x.albedo, &x.normal, &x.material})
            *t = (*t >= 0 && *t < static_cast<int>(k.tex.size())) ? kitTexMap[*t] : -1;
        nsubs.push_back(x);
    }
    if (nsubs.size() > 256) return false;
    HvmHeader nh = h;
    nh.vertexCount = h.vertexCount + k.h.vertexCount;
    nh.indexCount = h.indexCount + k.h.indexCount;
    nh.submeshCount = static_cast<uint32_t>(nsubs.size());
    nh.textureCount = static_cast<uint32_t>(ntex.size());
    size_t nlen = sizeof nh + ntex.size() * sizeof(HvmTexture) + nsubs.size() * sizeof(HvmSubmesh) + bones.size() * sizeof(HvmBone) +
                  static_cast<size_t>(nh.vertexCount) * kVertexStride + static_cast<size_t>(nh.indexCount) * 4 + (len - restOff);
    unsigned char* o = new unsigned char[nlen];
    size_t p = 0;
    memcpy(o + p, &nh, sizeof nh), p += sizeof nh;
    memcpy(o + p, ntex.data(), ntex.size() * sizeof(HvmTexture)), p += ntex.size() * sizeof(HvmTexture);
    memcpy(o + p, nsubs.data(), nsubs.size() * sizeof(HvmSubmesh)), p += nsubs.size() * sizeof(HvmSubmesh);
    memcpy(o + p, d + boneOff, bones.size() * sizeof(HvmBone)), p += bones.size() * sizeof(HvmBone);
    memcpy(o + p, d + vertOff, static_cast<size_t>(h.vertexCount) * kVertexStride), p += static_cast<size_t>(h.vertexCount) * kVertexStride;
    const unsigned char* kv = k.data.data() + k.vertOff;
    for (uint32_t v = 0; v < k.h.vertexCount; ++v) {
        const unsigned char* sv = kv + static_cast<size_t>(v) * kVertexStride;
        unsigned char* dv = o + p + static_cast<size_t>(v) * kVertexStride;
        memcpy(dv, sv, kVertexStride);
        float pos[3], nrm[3], tan[4];
        memcpy(pos, sv, 12), memcpy(nrm, sv + 12, 12), memcpy(tan, sv + 24, 16);
        float M[12] = {};
        float wsum = 0;
        for (int c = 0; c < 4; ++c) {
            unsigned bi = sv[48 + c], bw = sv[52 + c];
            if (!bw || bi >= k.bones.size()) continue;
            float w = bw / 255.f;
            for (int e = 0; e < 12; ++e) M[e] += w * xf[bi * 12 + e];
            wsum += w;
            dv[48 + c] = static_cast<unsigned char>(map[bi]);
        }
        if (wsum <= 0) continue;
        for (int e = 0; e < 12; ++e) M[e] /= wsum;
        float np[3], nn[3], nt[3];
        for (int r = 0; r < 3; ++r) {
            np[r] = M[r * 4] * pos[0] + M[r * 4 + 1] * pos[1] + M[r * 4 + 2] * pos[2] + M[r * 4 + 3];
            nn[r] = M[r * 4] * nrm[0] + M[r * 4 + 1] * nrm[1] + M[r * 4 + 2] * nrm[2];
            nt[r] = M[r * 4] * tan[0] + M[r * 4 + 1] * tan[1] + M[r * 4 + 2] * tan[2];
        }
        float ln = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]), lt = std::sqrt(nt[0] * nt[0] + nt[1] * nt[1] + nt[2] * nt[2]);
        if (ln > 1e-8f) nn[0] /= ln, nn[1] /= ln, nn[2] /= ln;
        if (lt > 1e-8f) nt[0] /= lt, nt[1] /= lt, nt[2] /= lt;
        memcpy(dv, np, 12), memcpy(dv + 12, nn, 12), memcpy(dv + 24, nt, 12);
    }
    p += static_cast<size_t>(k.h.vertexCount) * kVertexStride;
    memcpy(o + p, d + idxOff, static_cast<size_t>(h.indexCount) * 4), p += static_cast<size_t>(h.indexCount) * 4;
    const uint32_t* ki = reinterpret_cast<const uint32_t*>(k.data.data() + k.idxOff);
    uint32_t* oi = reinterpret_cast<uint32_t*>(o + p);
    for (uint32_t i = 0; i < k.h.indexCount; ++i) oi[i] = ki[i] + h.vertexCount;
    p += static_cast<size_t>(k.h.indexCount) * 4;
    memcpy(o + p, d + restOff, len - restOff), p += len - restOff;
    delete[] *pdata;
    *pdata = o;
    *plen = nlen;
    const wchar_t* kitFile = wcsrchr(k.path, L'\\');
    if (!kitFile) kitFile = wcsrchr(k.path, L'/');
    snprintf(note, noteLen, "arms override: %d arm submeshes -> kit %s/%ls (%u verts, %d bones missing in weapon%s)", nArm, k.dirName,
             kitFile ? kitFile + 1 : k.path, k.h.vertexCount, bestMiss, fallback ? ", some bones unmatched" : "");
    return true;
}
}
