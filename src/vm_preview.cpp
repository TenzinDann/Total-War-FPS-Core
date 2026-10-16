#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include "../src/vm_render.h"
namespace {
bool WriteBmp(const wchar_t* path, const unsigned char* rgba, UINT w, UINT h, UINT pitch) {
    FILE* f = _wfopen(path, L"wb");
    if (!f) return false;
    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + w * h * 4;
    ih.biSize = sizeof ih;
    ih.biWidth = static_cast<LONG>(w);
    ih.biHeight = -static_cast<LONG>(h);
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    unsigned char* row = new unsigned char[w * 4];
    for (UINT y = 0; y < h; ++y) {
        const unsigned char* s = rgba + static_cast<size_t>(y) * pitch;
        for (UINT x = 0; x < w; ++x) {
            row[x * 4 + 0] = s[x * 4 + 2];
            row[x * 4 + 1] = s[x * 4 + 1];
            row[x * 4 + 2] = s[x * 4 + 0];
            row[x * 4 + 3] = 255;
        }
        fwrite(row, 1, w * 4, f);
    }
    delete[] row;
    fclose(f);
    return true;
}
float Arg(int argc, wchar_t** argv, int i, float def) { return argc > i ? static_cast<float>(_wtof(argv[i])) : def; }
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 6) {
        wprintf(L"usage: vm_preview <model.hvm> <ini> <out.bmp> <w> <h> [bg.rgba|-] [time swayX swayY bobPhase bobAmount kick clip clipTime]\n");
        return 1;
    }
    const wchar_t *model = argv[1], *ini = argv[2], *out = argv[3];
    UINT w = static_cast<UINT>(_wtoi(argv[4])), h = static_cast<UINT>(_wtoi(argv[5]));
    if (w < 16 || h < 16 || w > 8192 || h > 8192) return 1;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev,
                                   nullptr, &ctx);
    const char* kind = "hardware";
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        kind = "warp";
    }
    if (FAILED(hr)) {
        printf("D3D11CreateDevice failed 0x%08lx\n", hr);
        return 2;
    }
    unsigned char* bg = new unsigned char[static_cast<size_t>(w) * h * 4];
    bool haveBg = false;
    if (argc > 6 && wcscmp(argv[6], L"-") != 0) {
        FILE* f = _wfopen(argv[6], L"rb");
        if (f) {
            haveBg = fread(bg, 1, static_cast<size_t>(w) * h * 4, f) == static_cast<size_t>(w) * h * 4;
            fclose(f);
        }
    }
    if (!haveBg)
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) {
                float t = static_cast<float>(y) / h;
                unsigned char* p = bg + (static_cast<size_t>(y) * w + x) * 4;
                bool ground = t > 0.55f;
                p[0] = static_cast<unsigned char>(ground ? 96 - 30 * (t - 0.55f) : 120 + 70 * t);
                p[1] = static_cast<unsigned char>(ground ? 92 - 30 * (t - 0.55f) : 150 + 50 * t);
                p[2] = static_cast<unsigned char>(ground ? 70 - 20 * (t - 0.55f) : 190 + 30 * t);
                p[3] = 255;
            }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA sd = {bg, w * 4, 0};
    ID3D11Texture2D* rt = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &sd, &rt)) || FAILED(dev->CreateRenderTargetView(rt, nullptr, &rtv))) {
        printf("render target creation failed\n");
        return 2;
    }
    vm::Settings s;
    {
        wchar_t side[MAX_PATH * 2];
        wcsncpy(side, model, MAX_PATH * 2 - 1);
        side[MAX_PATH * 2 - 1] = 0;
        wchar_t* slash = wcsrchr(side, L'\\');
        if (!slash) slash = wcsrchr(side, L'/');
        if (slash) {
            wcscpy(slash + 1, L"viewmodel.ini");
            vm::LoadSettings(side, s);
        }
    }
    if (!vm::LoadSettings(ini, s)) printf("note: ini not found, defaults used\n");
    vm::Renderer r;
    char err[512] = {};
    if (!r.Init(dev, model, err, sizeof err)) {
        printf("init failed: %s\n", err);
        return 3;
    }
    vm::Look look;
    look.time = Arg(argc, argv, 7, 0.0f);
    look.swayX = Arg(argc, argv, 8, 0.0f);
    look.swayY = Arg(argc, argv, 9, 0.0f);
    look.bobPhase = Arg(argc, argv, 10, 0.0f);
    look.bobAmount = Arg(argc, argv, 11, 0.0f);
    look.kick = Arg(argc, argv, 12, 0.0f);
    look.clip = static_cast<int>(Arg(argc, argv, 13, -1.0f));
    look.clipTime = Arg(argc, argv, 14, 0.0f);
    ID3D11Texture2D* rt2 = nullptr;
    ID3D11RenderTargetView* sentinelRtv = nullptr;
    dev->CreateTexture2D(&td, nullptr, &rt2);
    dev->CreateRenderTargetView(rt2, nullptr, &sentinelRtv);
    D3D11_VIEWPORT svp = {3, 5, 77, 55, 0.25f, 0.75f};
    D3D11_RECT ssc = {1, 2, 33, 44};
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_WIREFRAME;
    rd.CullMode = D3D11_CULL_FRONT;
    rd.ScissorEnable = TRUE;
    ID3D11RasterizerState* srs = nullptr;
    dev->CreateRasterizerState(&rd, &srs);
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = 0x5;
    ID3D11BlendState* sbs = nullptr;
    dev->CreateBlendState(&bd, &sbs);
    const float sbf[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    ctx->OMSetRenderTargets(1, &sentinelRtv, nullptr);
    ctx->RSSetViewports(1, &svp);
    ctx->RSSetScissorRects(1, &ssc);
    ctx->RSSetState(srs);
    ctx->OMSetBlendState(sbs, sbf, 0x0f0f0f0fu);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    LARGE_INTEGER q0, q1, qf;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    vm::MuzzleFlash flash;
    char flashErr[256] = {};
    bool flashLoaded = false;
    float flashAge = 0.f;
    if (const char* e = getenv("HFPS_FLASH_AGE_MS")) flashAge = static_cast<float>(atof(e));
    if (s.flashFx[0]) {
        wchar_t dir[MAX_PATH * 2], fx[MAX_PATH * 2];
        wcsncpy(dir, model, MAX_PATH * 2 - 1);
        dir[MAX_PATH * 2 - 1] = 0;
        wchar_t* sl = wcsrchr(dir, L'\\');
        if (!sl) sl = wcsrchr(dir, L'/');
        if (sl) *sl = 0;
        swprintf(fx, MAX_PATH * 2, L"%ls\\%ls", dir, s.flashFx);
        wchar_t gas[MAX_PATH * 2] = L"";
        if (s.flashGas && s.flashGasFx[0]) swprintf(gas, MAX_PATH * 2, L"%ls\\%ls", dir, s.flashGasFx);
        flashLoaded = flash.Load(dev, fx, gas, flashErr, sizeof flashErr);
        if (const char* e = getenv("HFPS_FLASH_SEED")) flash.Seed(static_cast<unsigned>(atoi(e)));
        if (flashLoaded && flashAge >= 0.f) {
            flash.Trigger(getenv("HFPS_FLASH_ADS") ? 1.f : 0.f);
            const float ta[3] = {0.28f, -0.22f, 1.2f}, tb[3] = {0.f, 0.f, 30.f};
            flash.AddTracer(ta, tb);
            flash.BeginFrame(10.0, s);
            look.flashLight = flash.LightLevel(10.0 + flashAge * 0.001);
        }
    }
    bool drew = r.Draw(ctx, rtv, rt, w, h, false, s, look);
    QueryPerformanceCounter(&q1);
    vm::CrosshairSettings cs;
    vm::LoadCrosshair(ini, cs);
    vm::Crosshair cross;
    bool crossDrew = cross.Draw(ctx, rtv, w, h, false, cs);
    printf("CROSSHAIR drew=%d enabled=%d length=%.1f gap=%.1f thickness=%.1f outline=%.1f color=%.2f,%.2f,%.2f,%.2f\n", crossDrew,
           cs.enabled, cs.length, cs.gap, cs.thickness, cs.outline, cs.color[0], cs.color[1], cs.color[2], cs.color[3]);
    vm::MuzzleFrame mf;
    bool fdrew = false;
    if (r.MuzzleView(&mf))
        printf("MUZZLEFRAME pos=%.4f,%.4f,%.4f fwd=%.3f,%.3f,%.3f left=%.3f,%.3f,%.3f up=%.3f,%.3f,%.3f unit=%.5f\n", mf.pos[0], mf.pos[1], mf.pos[2],
               mf.fwd[0], mf.fwd[1], mf.fwd[2], mf.left[0], mf.left[1], mf.left[2], mf.up[0], mf.up[1], mf.up[2], mf.unit);
    vm::WorldCam wc = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 65.f * 3.14159265f / 180.f};
    int tracersBefore = flash.LiveTracers();
    double flashEnd = 10.0 + flashAge * 0.001;
    if (flashLoaded && flashAge >= 0.f && r.MuzzleView(&mf)) {
        int burst = 1;
        float rpm = 750.f;
        if (const char* e = getenv("HFPS_FLASH_BURST")) burst = atoi(e) > 1 ? atoi(e) : 1;
        if (const char* e = getenv("HFPS_FLASH_RPM")) rpm = static_cast<float>(atof(e)) > 1.f ? static_cast<float>(atof(e)) : 750.f;
        const double gap = 60.0 / rpm;
        flashEnd = 10.0 + gap * (burst - 1) + flashAge * 0.001;
        D3D11_TEXTURE2D_DESC sdsc = {};
        sdsc.Width = w;
        sdsc.Height = h;
        sdsc.MipLevels = sdsc.ArraySize = 1;
        sdsc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sdsc.SampleDesc.Count = 1;
        sdsc.Usage = D3D11_USAGE_DEFAULT;
        sdsc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ID3D11Texture2D* scratch = nullptr;
        ID3D11RenderTargetView* scratchRtv = nullptr;
        if (SUCCEEDED(dev->CreateTexture2D(&sdsc, nullptr, &scratch)) && SUCCEEDED(dev->CreateRenderTargetView(scratch, nullptr, &scratchRtv))) {
            int shot = 1;
            for (double t = 10.0 + 1.0 / 60.0; t < flashEnd - 1e-6; t += 1.0 / 60.0) {
                if (shot < burst && t >= 10.0 + gap * shot) {
                    flash.Trigger(getenv("HFPS_FLASH_ADS") ? 1.f : 0.f);
                    flash.BeginFrame(10.0 + gap * shot, s);
                    ++shot;
                }
                flash.Draw(ctx, scratchRtv, r.Depth(w, h), w, h, false, t, &mf, &wc, s);
            }
            for (; shot < burst; ++shot) {
                flash.Trigger(getenv("HFPS_FLASH_ADS") ? 1.f : 0.f);
                flash.BeginFrame(10.0 + gap * shot, s);
            }
        }
        if (scratchRtv) scratchRtv->Release();
        if (scratch) scratch->Release();
    }
    if (flashLoaded && flashAge >= 0.f)
        fdrew = flash.Draw(ctx, rtv, getenv("HFPS_FLASH_NODEPTH") ? nullptr : r.Depth(w, h), w, h, false, flashEnd,
                           r.MuzzleView(&mf) ? &mf : nullptr, &wc, s);
    if (s.flashFx[0])
        printf("FLASH loaded=%d drew=%d age=%.0fms live=%d tracers=%d/%d light=%.2f scale=%.2f intensity=%.2f%s%s\n", flashLoaded, fdrew, flashAge,
               flash.Live(), flash.LiveTracers(), tracersBefore, look.flashLight, s.flashScale, s.flashIntensity, flashErr[0] ? " err=" : "", flashErr);
    else
        printf("FLASH none\n");
    ID3D11RenderTargetView* gotRtv[8] = {};
    ID3D11DepthStencilView* gotDsv = nullptr;
    ctx->OMGetRenderTargets(8, gotRtv, &gotDsv);
    UINT nvp = 16;
    D3D11_VIEWPORT gvp[16];
    ctx->RSGetViewports(&nvp, gvp);
    UINT nsc = 16;
    D3D11_RECT gsc[16];
    ctx->RSGetScissorRects(&nsc, gsc);
    ID3D11RasterizerState* grs = nullptr;
    ctx->RSGetState(&grs);
    ID3D11BlendState* gbs = nullptr;
    float gbf[4];
    UINT gmask = 0;
    ctx->OMGetBlendState(&gbs, gbf, &gmask);
    D3D11_PRIMITIVE_TOPOLOGY gtopo;
    ctx->IAGetPrimitiveTopology(&gtopo);
    ID3D11VertexShader* gvs = nullptr;
    ctx->VSGetShader(&gvs, nullptr, nullptr);
    ID3D11PixelShader* gps = nullptr;
    ctx->PSGetShader(&gps, nullptr, nullptr);
    bool restored = gotRtv[0] == sentinelRtv && gotRtv[1] == nullptr && gotDsv == nullptr && nvp == 1 &&
                    !memcmp(&gvp[0], &svp, sizeof svp) && nsc == 1 && !memcmp(&gsc[0], &ssc, sizeof ssc) &&
                    grs == srs && gbs == sbs && !memcmp(gbf, sbf, sizeof sbf) && gmask == 0x0f0f0f0fu &&
                    gtopo == D3D11_PRIMITIVE_TOPOLOGY_POINTLIST && gvs == nullptr && gps == nullptr;
    for (auto* p : gotRtv)
        if (p) p->Release();
    if (gotDsv) gotDsv->Release();
    if (grs) grs->Release();
    if (gbs) gbs->Release();
    if (gvs) gvs->Release();
    if (gps) gps->Release();
    printf("STATE RESTORE %s\n", restored ? "OK" : "FAILED");
    D3D11_TEXTURE2D_DESC st = td;
    st.Usage = D3D11_USAGE_STAGING;
    st.BindFlags = 0;
    st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* stg = nullptr;
    dev->CreateTexture2D(&st, nullptr, &stg);
    ctx->CopyResource(stg, rt);
    D3D11_MAPPED_SUBRESOURCE mp;
    if (FAILED(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &mp))) {
        printf("readback failed\n");
        return 4;
    }
    size_t changed = 0;
    for (UINT y = 0; y < h; ++y) {
        const unsigned char* a = static_cast<const unsigned char*>(mp.pData) + static_cast<size_t>(y) * mp.RowPitch;
        const unsigned char* b = bg + static_cast<size_t>(y) * w * 4;
        for (UINT x = 0; x < w; ++x)
            if (a[x * 4] != b[x * 4] || a[x * 4 + 1] != b[x * 4 + 1] || a[x * 4 + 2] != b[x * 4 + 2]) ++changed;
    }
    unsigned char* img = static_cast<unsigned char*>(malloc(static_cast<size_t>(w) * h * 4));
    for (UINT y = 0; y < h; ++y)
        memcpy(img + static_cast<size_t>(y) * w * 4,
               static_cast<const unsigned char*>(mp.pData) + static_cast<size_t>(y) * mp.RowPitch, w * 4);
    ctx->Unmap(stg, 0);
    float mx = 0, my = 0, ma = 0;
    if (r.Muzzle(&mx, &my, &ma)) {
        int px = static_cast<int>((mx * 0.5f + 0.5f) * w), py = static_cast<int>((0.5f - my * 0.5f) * h);
        for (int d = -12; d <= 12 && !getenv("HFPS_NO_MARKER"); ++d)
            for (int t = -1; t <= 1; ++t) {
                int pts[2][2] = {{px + d, py + t}, {px + t, py + d}};
                for (auto& p : pts)
                    if (p[0] >= 0 && p[0] < static_cast<int>(w) && p[1] >= 0 && p[1] < static_cast<int>(h)) {
                        unsigned char* q = img + (static_cast<size_t>(p[1]) * w + p[0]) * 4;
                        q[0] = 255;
                        q[1] = 0;
                        q[2] = 255;
                    }
            }
        printf("MUZZLE ndc=%.4f,%.4f aspect=%.4f pixel=%d,%d\n", mx, my, ma, px, py);
    } else {
        printf("MUZZLE none\n");
    }
    bool wrote = WriteBmp(out, img, w, h, w * 4);
    free(img);
    printf("DRAW %s device=%s %ux%u tris=%u bones=%u clips=%u covered=%.2f%% cpu=%.2fms bmp=%s\n",
           drew ? "OK" : "FAILED", kind, w, h, r.Triangles(), r.Bones(), r.Clips(), 100.0 * changed / (w * h),
           (q1.QuadPart - q0.QuadPart) * 1000.0 / qf.QuadPart, wrote ? "written" : "FAILED");
    flash.Release();
    r.Release();
    return drew && wrote && restored ? 0 : 5;
}
