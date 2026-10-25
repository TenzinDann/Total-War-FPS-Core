namespace vm {
namespace {
const char kCastMarkShader[] = R"HLSL(struct VI { float2 p : POSITION; float4 c : COLOR; float3 m : TEXCOORD0; };
struct VO { float4 p : SV_Position; float4 c : COLOR; float3 m : TEXCOORD0; };
VO VS(VI i) { VO o; o.p = float4(i.p, 0, 1); o.c = i.c; o.m = i.m; return o; }
float4 PS(VO i) : SV_Target {
    float4 c = i.c;
    c.a *= saturate((i.m.y - abs(i.m.x)) / max(i.m.z, 0.001) + 0.5);
    return c;
}
)HLSL";
struct CmVertex {
    float x, y;
    float r, g, b, a;
    float d, hw, soft;
};
constexpr int kCmVerts = 24576;
constexpr float kCmNear = 0.2f;
struct CmProj {
    const WorldCam* cam;
    float focal, cx, cy;
    void View(const float* p, float* v) const {
        float d[3] = {p[0] - cam->eye[0], p[1] - cam->eye[1], p[2] - cam->eye[2]};
        v[0] = d[0] * cam->right[0] + d[1] * cam->right[1] + d[2] * cam->right[2];
        v[1] = d[0] * cam->up[0] + d[1] * cam->up[1] + d[2] * cam->up[2];
        v[2] = d[0] * cam->fwd[0] + d[1] * cam->fwd[1] + d[2] * cam->fwd[2];
    }
    void Screen(const float* v, float* s) const {
        s[0] = cx + v[0] / v[2] * focal;
        s[1] = cy - v[1] / v[2] * focal;
    }
};
class CmBatch {
public:
    CmBatch(CmVertex* v, UINT w, UINT h) : v_(v), w_(static_cast<float>(w)), h_(static_cast<float>(h)) {}
    int n = 0;
    bool full = false;
    void Vtx(float px, float py, const float* c, float a, float d, float hw, float soft) {
        if (n >= kCmVerts) {
            full = true;
            return;
        }
        v_[n++] = {px / w_ * 2.f - 1.f, 1.f - py / h_ * 2.f, c[0], c[1], c[2], a, d, hw, soft};
    }
    void Tri(const float* a, const float* b, const float* c, const float* col, float alpha) {
        if (n + 3 > kCmVerts) {
            full = true;
            return;
        }
        Vtx(a[0], a[1], col, alpha, 0.f, 1.f, 1.f);
        Vtx(b[0], b[1], col, alpha, 0.f, 1.f, 1.f);
        Vtx(c[0], c[1], col, alpha, 0.f, 1.f, 1.f);
    }
    void Stroke(const float (*p)[2], int np, bool closed, float hw, float soft, const float* col, float alpha) {
        if (np < 2) return;
        if (n + (np + 1) * 6 > kCmVerts) {
            full = true;
            return;
        }
        const float ext = hw + soft * 0.5f + 0.75f;
        auto seg = [&](int i, float* t) {
            const float* a = p[i];
            const float* b = p[(i + 1) % np];
            float dx = b[0] - a[0], dy = b[1] - a[1], l = std::sqrt(dx * dx + dy * dy);
            if (l < 1e-4f) l = 1e-4f;
            t[0] = dx / l, t[1] = dy / l;
        };
        auto norm = [&](int i, float* nrm, float* k) {
            float t0[2] = {1.f, 0.f}, t1[2] = {1.f, 0.f};
            bool hasPrev = closed || i > 0, hasNext = closed || i < np - 1;
            if (hasNext) seg(i, t1);
            if (hasPrev) seg((i - 1 + np) % np, t0);
            if (!hasPrev) t0[0] = t1[0], t0[1] = t1[1];
            if (!hasNext) t1[0] = t0[0], t1[1] = t0[1];
            float mx = t0[0] + t1[0], my = t0[1] + t1[1], ml = std::sqrt(mx * mx + my * my);
            if (ml < 1e-3f)
                mx = t1[0], my = t1[1];
            else
                mx /= ml, my /= ml;
            nrm[0] = -my, nrm[1] = mx;
            float c = nrm[0] * -t1[1] + nrm[1] * t1[0];
            *k = std::fmin(1.f / std::fmax(std::fabs(c), 1e-3f), 1.6f);
        };
        const int segs = closed ? np : np - 1;
        float n0[2], k0;
        norm(0, n0, &k0);
        for (int i = 0; i < segs; ++i) {
            int j = (i + 1) % np;
            float n1[2], k1;
            norm(j, n1, &k1);
            const float* a = p[i];
            const float* b = p[j];
            float a0[2] = {a[0] + n0[0] * ext * k0, a[1] + n0[1] * ext * k0}, a1[2] = {a[0] - n0[0] * ext * k0, a[1] - n0[1] * ext * k0};
            float b0[2] = {b[0] + n1[0] * ext * k1, b[1] + n1[1] * ext * k1}, b1[2] = {b[0] - n1[0] * ext * k1, b[1] - n1[1] * ext * k1};
            Vtx(a0[0], a0[1], col, alpha, ext, hw, soft);
            Vtx(a1[0], a1[1], col, alpha, -ext, hw, soft);
            Vtx(b0[0], b0[1], col, alpha, ext, hw, soft);
            Vtx(b0[0], b0[1], col, alpha, ext, hw, soft);
            Vtx(a1[0], a1[1], col, alpha, -ext, hw, soft);
            Vtx(b1[0], b1[1], col, alpha, -ext, hw, soft);
            n0[0] = n1[0], n0[1] = n1[1], k0 = k1;
        }
    }
    void Beam(const float* a, const float* b, float hw, float soft, const float* col, float alphaA, float alphaB) {
        float dx = b[0] - a[0], dy = b[1] - a[1], l = std::sqrt(dx * dx + dy * dy);
        if (l < 0.5f) return;
        float nx = -dy / l, ny = dx / l, ext = hw + soft * 0.5f + 0.75f;
        float a0[2] = {a[0] + nx * ext, a[1] + ny * ext}, a1[2] = {a[0] - nx * ext, a[1] - ny * ext};
        float b0[2] = {b[0] + nx * ext, b[1] + ny * ext}, b1[2] = {b[0] - nx * ext, b[1] - ny * ext};
        Vtx(a0[0], a0[1], col, alphaA, ext, hw, soft);
        Vtx(a1[0], a1[1], col, alphaA, -ext, hw, soft);
        Vtx(b0[0], b0[1], col, alphaB, ext, hw, soft);
        Vtx(b0[0], b0[1], col, alphaB, ext, hw, soft);
        Vtx(a1[0], a1[1], col, alphaA, -ext, hw, soft);
        Vtx(b1[0], b1[1], col, alphaB, -ext, hw, soft);
    }
private:
    CmVertex* v_;
    float w_, h_;
};
struct CmRuns {
    static constexpr int kMax = cm::kMaxPts + 16;
    float pts[kMax][2];
    int start[24], count[24];
    int runs = 0, total = 0;
    bool closed = false;
};
void CmClipLine(const CmProj& pj, const float (*world)[3], int first, int count, bool closed, CmRuns& out) {
    out.runs = out.total = 0;
    out.closed = false;
    if (count < 2) return;
    float vv[cm::kMaxPts][3];
    bool allIn = true;
    for (int i = 0; i < count; ++i) {
        pj.View(world[first + i], vv[i]);
        allIn = allIn && vv[i][2] >= kCmNear;
    }
    auto push = [&](const float* v, bool newRun) {
        if (out.total >= CmRuns::kMax) return;
        if (newRun) {
            if (out.runs >= 24) return;
            out.start[out.runs] = out.total, out.count[out.runs] = 0, ++out.runs;
        }
        if (!out.runs) return;
        float s[2];
        pj.Screen(v, s);
        int r = out.runs - 1;
        if (out.count[r] > 0) {
            const float* q = out.pts[out.total - 1];
            if ((s[0] - q[0]) * (s[0] - q[0]) + (s[1] - q[1]) * (s[1] - q[1]) < 0.5625f) return;
        }
        out.pts[out.total][0] = s[0], out.pts[out.total][1] = s[1];
        ++out.total, ++out.count[r];
    };
    if (allIn) {
        for (int i = 0; i < count; ++i) push(vv[i], i == 0);
        out.closed = closed;
        return;
    }
    const int segs = closed ? count : count - 1;
    bool open = false;
    int firstRunStartsAt0 = -1;
    for (int i = 0; i < segs; ++i) {
        const float* a = vv[i];
        const float* b = vv[(i + 1) % count];
        bool ina = a[2] >= kCmNear, inb = b[2] >= kCmNear;
        if (!ina && !inb) {
            open = false;
            continue;
        }
        float c[3];
        auto cut = [&](const float* p, const float* q) {
            float t = (kCmNear - p[2]) / (q[2] - p[2]);
            c[0] = p[0] + (q[0] - p[0]) * t, c[1] = p[1] + (q[1] - p[1]) * t, c[2] = kCmNear;
        };
        if (ina && inb) {
            if (!open) {
                if (i == 0) firstRunStartsAt0 = out.runs;
                push(a, true);
                open = true;
            }
            push(b, false);
        } else if (ina) {
            if (!open) push(a, true);
            cut(a, b);
            push(c, false);
            open = false;
        } else {
            cut(b, a);
            push(c, true);
            push(b, false);
            open = true;
        }
    }
    if (closed && open && firstRunStartsAt0 == 0 && out.runs >= 2) {
        int last = out.runs - 1;
        float tmp[CmRuns::kMax][2];
        int k = 0;
        for (int i = 0; i < out.count[last]; ++i) tmp[k][0] = out.pts[out.start[last] + i][0], tmp[k][1] = out.pts[out.start[last] + i][1], ++k;
        for (int i = 1; i < out.count[0] && k < CmRuns::kMax; ++i) tmp[k][0] = out.pts[out.start[0] + i][0], tmp[k][1] = out.pts[out.start[0] + i][1], ++k;
        float rest[CmRuns::kMax][2];
        int rs = 0, rStart[24], rCount[24];
        for (int r = 1; r < last; ++r) {
            rStart[r] = rs, rCount[r] = out.count[r];
            for (int i = 0; i < out.count[r]; ++i) rest[rs][0] = out.pts[out.start[r] + i][0], rest[rs][1] = out.pts[out.start[r] + i][1], ++rs;
        }
        out.total = 0;
        for (int i = 0; i < k; ++i) out.pts[out.total][0] = tmp[i][0], out.pts[out.total][1] = tmp[i][1], ++out.total;
        out.start[0] = 0, out.count[0] = k;
        for (int r = 1; r < last; ++r) {
            out.start[r] = out.total, out.count[r] = rCount[r];
            for (int i = 0; i < rCount[r]; ++i) out.pts[out.total][0] = rest[rStart[r] + i][0], out.pts[out.total][1] = rest[rStart[r] + i][1], ++out.total;
        }
        out.runs = last;
    }
}
}
bool LoadCastMarkerStyle(const wchar_t* ini, CastMarkerStyle& s) {
    s = CastMarkerStyle();
    s.enabled = GetPrivateProfileIntW(L"hud", L"cast_marker", 1, ini) != 0;
    s.width = IniCross(ini, L"hud", L"cast_marker_width", s.width);
    s.glow = IniCross(ini, L"hud", L"cast_marker_glow", s.glow);
    s.glowAlpha = IniCross(ini, L"hud", L"cast_marker_glow_alpha", s.glowAlpha);
    s.fillAlpha = IniCross(ini, L"hud", L"cast_marker_fill", s.fillAlpha);
    s.outline = IniCross(ini, L"hud", L"cast_marker_outline", s.outline);
    s.pillarWidth = IniCross(ini, L"hud", L"cast_marker_pillar_width", s.pillarWidth);
    s.chevron = IniCross(ini, L"hud", L"cast_marker_chevron", s.chevron);
    static const wchar_t* const names[cm::kTones] = {L"cast_marker_color_white", L"cast_marker_color_overcast", L"cast_marker_color_edge",
                                                     L"cast_marker_color_enemy", L"cast_marker_color_out_of_range"};
    for (int t = 0; t < cm::kTones; ++t) {
        wchar_t buf[64];
        GetPrivateProfileStringW(L"hud", names[t], L"", buf, 64, ini);
        float c[3];
        if (buf[0] && swscanf(buf, L"%f , %f , %f", &c[0], &c[1], &c[2]) == 3)
            for (int k = 0; k < 3; ++k) s.tone[t][k] = std::fmin(1.f, std::fmax(0.f, c[k] / 255.f));
    }
    return true;
}
bool CastMarker::Init(ID3D11Device* dev) {
    Release();
    dev_ = dev;
    FnD3DCompile compile = GetCompiler();
    if (!compile) return false;
    ID3DBlob *vsb = nullptr, *psb = nullptr, *msg = nullptr;
    bool ok = SUCCEEDED(compile(kCastMarkShader, sizeof kCastMarkShader - 1, "tenzin_fps_cast_marker", nullptr, nullptr, "VS", "vs_5_0",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(compile(kCastMarkShader, sizeof kCastMarkShader - 1, "tenzin_fps_cast_marker", nullptr, nullptr, "PS", "ps_5_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) &&
         SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_));
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = ok && vsb && SUCCEEDED(dev->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_));
    SafeRelease(vsb);
    SafeRelease(psb);
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(CmVertex) * kCmVerts;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &vb_));
    D3D11_BLEND_DESC bld = {};
    bld.RenderTarget[0].BlendEnable = TRUE;
    bld.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bld.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    ok = ok && SUCCEEDED(dev->CreateBlendState(&bld, &bs_));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(dev->CreateRasterizerState(&rd, &rs_));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    ok = ok && SUCCEEDED(dev->CreateDepthStencilState(&dd, &dss_));
    if (!ok) Release();
    return ok;
}
void CastMarker::Release() {
    SafeRelease(vs_);
    SafeRelease(ps_);
    SafeRelease(layout_);
    SafeRelease(vb_);
    SafeRelease(bs_);
    SafeRelease(rs_);
    SafeRelease(dss_);
    dev_ = nullptr;
}
bool CastMarker::Prewarm(ID3D11Device* dev) {
    if (failed_ || !dev) return false;
    if (dev == dev_ && vs_) return true;
    if (!Init(dev)) {
        failed_ = true;
        return false;
    }
    return true;
}
bool CastMarker::Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, UINT w, UINT h, bool rtvEncodesSrgb, const WorldCam& cam,
                      const cm::Frame& f, const CastMarkerStyle& s) {
    verts_ = 0;
    if (!s.enabled || !f.on || !ctx || !rtv || w < 16 || h < 16 || failed_ || !(cam.fovY > 0.1f && cam.fovY < 3.f)) return false;
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return false;
    if (dev != dev_ || !vs_) {
        if (!Init(dev)) {
            failed_ = true;
            dev->Release();
            return false;
        }
    }
    dev->Release();
    const float k = static_cast<float>(h) / 1080.f;
    CmProj pj = {&cam, static_cast<float>(h) * 0.5f / std::tan(cam.fovY * 0.5f), static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f};
    float tone[cm::kTones][3];
    for (int t = 0; t < cm::kTones; ++t)
        for (int c = 0; c < 3; ++c) tone[t][c] = rtvEncodesSrgb ? SrgbToLinear(s.tone[t][c]) : s.tone[t][c];
    const float dark[3] = {0.02f, 0.025f, 0.035f};
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    CmBatch b(static_cast<CmVertex*>(m.pData), w, h);
    for (int i = 0; i < f.nTris; ++i) {
        float v[3][3], poly[4][3];
        for (int c = 0; c < 3; ++c) pj.View(f.pts[f.tris[i][c]], v[c]);
        int np = 0;
        for (int c = 0; c < 3; ++c) {
            const float* a = v[c];
            const float* q = v[(c + 1) % 3];
            bool ina = a[2] >= kCmNear, inq = q[2] >= kCmNear;
            if (ina) memcpy(poly[np++], a, sizeof poly[0]);
            if (ina != inq && np < 4) {
                float t = (kCmNear - a[2]) / (q[2] - a[2]);
                poly[np][0] = a[0] + (q[0] - a[0]) * t, poly[np][1] = a[1] + (q[1] - a[1]) * t, poly[np][2] = kCmNear;
                ++np;
            }
        }
        float sp[4][2];
        for (int c = 0; c < np; ++c) pj.Screen(poly[c], sp[c]);
        for (int c = 1; c + 1 < np; ++c) b.Tri(sp[0], sp[c], sp[c + 1], tone[f.triTone[i]], s.fillAlpha);
    }
    static CmRuns runs[cm::kMaxLines];
    for (int i = 0; i < f.nLines; ++i) CmClipLine(pj, f.pts, f.lines[i].first, f.lines[i].count, f.lines[i].closed, runs[i]);
    const float hw = std::fmax(0.6f, s.width * k * 0.5f), ol = s.outline * k, glow = s.glow * k;
    for (int pass = 0; pass < 3; ++pass)
        for (int i = 0; i < f.nLines; ++i) {
            const CmRuns& r = runs[i];
            const float* col = tone[f.lines[i].tone];
            for (int j = 0; j < r.runs; ++j) {
                const float (*p)[2] = r.pts + r.start[j];
                bool cl = r.closed && r.runs == 1;
                if (pass == 0 && glow > 0.5f && s.glowAlpha > 0.f) b.Stroke(p, r.count[j], cl, glow * 0.5f, glow, col, s.glowAlpha);
                if (pass == 1 && ol > 0.f) b.Stroke(p, r.count[j], cl, hw + ol, 1.f, dark, 0.55f);
                if (pass == 2) b.Stroke(p, r.count[j], cl, hw, 1.f, col, 0.95f);
            }
        }
    if (f.pillar) {
        float vb3[3], vt3[3], top[3] = {f.pillarAt[0], f.pillarAt[1] + f.pillarH, f.pillarAt[2]};
        pj.View(f.pillarAt, vb3);
        pj.View(top, vt3);
        if (vb3[2] >= kCmNear && vt3[2] >= kCmNear) {
            float sb[2], st[2];
            pj.Screen(vb3, sb);
            pj.Screen(vt3, st);
            float half = std::fmin(20.f * k, std::fmax(s.pillarWidth * k * 0.5f, pj.focal * 0.11f / vb3[2]));
            const float* col = tone[f.pillarTone];
            b.Beam(sb, st, half * 3.f + 4.f * k, half * 6.f + 8.f * k, col, s.glowAlpha * 0.9f, 0.f);
            b.Beam(sb, st, half + ol, 1.f, dark, 0.45f, 0.f);
            b.Beam(sb, st, half, 1.f, col, 0.95f, 0.f);
        }
    }
    if (f.chevron) {
        float va[3];
        pj.View(f.chevronAt, va);
        if (va[2] >= kCmNear) {
            float sa[2];
            pj.Screen(va, sa);
            const float z = s.chevron * k, *col = tone[f.chevronTone];
            for (int t = 1; t >= 0; --t) {
                float oy = -t * z * 1.15f, alpha = t ? 0.55f : 0.95f;
                float tri[3][2] = {{sa[0] - z, sa[1] - z * 1.3f + oy}, {sa[0] + z, sa[1] - z * 1.3f + oy}, {sa[0], sa[1] + oy}};
                b.Stroke(tri, 3, true, 1.2f * k + ol, 1.f, dark, 0.55f * alpha);
                b.Tri(tri[0], tri[1], tri[2], col, alpha);
                b.Stroke(tri, 3, true, 0.5f * k, 1.f, col, alpha);
            }
        }
    }
    ctx->Unmap(vb_, 0);
    verts_ = b.n;
    if (b.full && !warnedFull_) warnedFull_ = true;
    if (!b.n) return false;
    StateBackup bk;
    bk.Save(ctx);
    D3D11_VIEWPORT vp = {0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs_);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    const float bf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(bs_, bf, 0xffffffff);
    ctx->OMSetDepthStencilState(dss_, 0);
    UINT stride = sizeof(CmVertex), off = 0;
    ctx->IASetVertexBuffers(0, 1, &vb_, &stride, &off);
    ctx->IASetInputLayout(layout_);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_, nullptr, 0);
    ctx->PSSetShader(ps_, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->Draw(static_cast<UINT>(b.n), 0);
    bk.Restore(ctx);
    return true;
}
}
