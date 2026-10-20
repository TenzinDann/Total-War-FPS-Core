#include "../src/vm_game.cpp"
#include <cstdarg>
#include <string>
#include <vector>
namespace {
std::vector<std::string> g_lines, g_ev;
void TLog(const char* fmt, ...) {
    char buf[8192];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    g_lines.push_back(buf);
    printf("%s\n", buf);
}
int g_fail = 0;
void Check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}
int Has(const char* s) {
    int n = 0;
    for (auto& l : g_lines)
        if (l.find(s) != std::string::npos) ++n;
    return n;
}
ID3D11Device* g_dev;
ID3D11DeviceContext* g_ctx;
ID3D11RenderTargetView *g_bbRtv, *g_sceneRtv;
ID3D11Texture2D *g_bb, *g_scene, *g_sceneLdr, *g_green, *g_staging;
ID3D11ShaderResourceView* g_srv;
ID3D11SamplerState* g_samp;
ID3D11Buffer* g_cb;
ID3D11BlendState* g_blend;
ID3D11DepthStencilView* g_dsv;
const float kBf[4] = {0.1f, 0.2f, 0.3f, 0.4f};
const D3D11_VIEWPORT kVp = {10, 20, 300, 200, 0, 1};
ID3D11VertexShader* g_fakeVs;
ID3D11Buffer* g_fakeIb;
void BindFake(ID3D11DeviceContext* ctx) {
    ctx->VSSetShader(g_fakeVs, nullptr, 0);
    ctx->PSSetShader(nullptr, nullptr, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetIndexBuffer(g_fakeIb, DXGI_FORMAT_R16_UINT, 0);
}
bool MakeFake() {
    using FnCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**,
                                       ID3DBlob**);
    HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = m ? reinterpret_cast<FnCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
    if (!compile) return false;
    const char src[] = "float4 main(uint id : SV_VertexID) : SV_Position { return float4(2, 2, 0, 1); }";
    ID3DBlob *code = nullptr, *err = nullptr;
    if (FAILED(compile(src, sizeof src - 1, "fake", nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, &err))) return false;
    bool ok = SUCCEEDED(g_dev->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_fakeVs));
    code->Release();
    unsigned short idx[36] = {};
    for (int i = 0; i < 36; ++i) idx[i] = static_cast<unsigned short>(i % 3);
    D3D11_BUFFER_DESC bd = {sizeof idx, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA sd = {idx, 0, 0};
    return ok && SUCCEEDED(g_dev->CreateBuffer(&bd, &sd, &g_fakeIb));
}
__attribute__((noinline)) void EngineScene(ID3D11DeviceContext* ctx) {
    BindFake(ctx);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->OMSetRenderTargets(1, &g_sceneRtv, nullptr);
    const float c[4] = {0, 0, 0, 1};
    ctx->ClearRenderTargetView(g_sceneRtv, c);
    for (int i = 0; i < 4; ++i) ctx->DrawIndexed(36, 0, 0);
}
__attribute__((noinline)) void EngineFinal(ID3D11DeviceContext* ctx) {
    BindFake(ctx);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->OMSetRenderTargets(1, &g_bbRtv, nullptr);
    D3D11_VIEWPORT vp = {0, 0, 640, 360, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->Draw(3, 0);
}
__attribute__((noinline)) void EngineUiQuad(ID3D11DeviceContext* ctx) { ctx->DrawIndexed(6, 0, 0); }
__attribute__((noinline)) void EngineUi(ID3D11DeviceContext* ctx) {
    for (int i = 0; i < 5; ++i) EngineUiQuad(ctx);
}
void EngineState(ID3D11DeviceContext* ctx) {
    ctx->PSSetShaderResources(5, 1, &g_srv);
    ctx->PSSetSamplers(3, 1, &g_samp);
    ctx->PSSetConstantBuffers(7, 1, &g_cb);
    ctx->VSSetConstantBuffers(9, 1, &g_cb);
    ctx->RSSetViewports(1, &kVp);
    ctx->OMSetBlendState(g_blend, kBf, 0xF0F0F0F0u);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->OMSetRenderTargets(1, &g_bbRtv, g_dsv);
}
bool StateSame(ID3D11DeviceContext* ctx) {
    ID3D11ShaderResourceView* s = nullptr;
    ID3D11SamplerState* sp = nullptr;
    ID3D11Buffer *cb = nullptr, *vcb = nullptr;
    UINT nv = 1;
    D3D11_VIEWPORT vp = {};
    ID3D11BlendState* bs = nullptr;
    FLOAT bf[4] = {};
    UINT mask = 0;
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->PSGetShaderResources(5, 1, &s);
    ctx->PSGetSamplers(3, 1, &sp);
    ctx->PSGetConstantBuffers(7, 1, &cb);
    ctx->VSGetConstantBuffers(9, 1, &vcb);
    ctx->RSGetViewports(&nv, &vp);
    ctx->OMGetBlendState(&bs, bf, &mask);
    ctx->IAGetPrimitiveTopology(&topo);
    ctx->OMGetRenderTargets(1, &rtv, &dsv);
    bool ok = s == g_srv && sp == g_samp && cb == g_cb && vcb == g_cb && nv == 1 && vp.TopLeftX == kVp.TopLeftX && vp.Width == kVp.Width &&
              bs == g_blend && bf[2] == kBf[2] && mask == 0xF0F0F0F0u && topo == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP && rtv == g_bbRtv && dsv == g_dsv;
    for (IUnknown* u : {(IUnknown*)s, (IUnknown*)sp, (IUnknown*)cb, (IUnknown*)vcb, (IUnknown*)bs, (IUnknown*)rtv, (IUnknown*)dsv})
        if (u) u->Release();
    return ok;
}
bool g_stateOk = true;
int g_gunCalls = 0;
__attribute__((noinline)) void EngineCopy(ID3D11DeviceContext* ctx) {
    g_ev.push_back("copy");
    ctx->CopyResource(g_bb, g_sceneLdr);
    if (!StateSame(ctx)) g_stateOk = false;
}
__attribute__((noinline)) void EngineUi2D(ID3D11DeviceContext* ctx) {
    g_ev.push_back("ui");
    ctx->CopySubresourceRegion(g_bb, 0, 100, 100, 0, g_green, 0, nullptr);
}
void TestGun(IDXGISwapChain*) {
    g_ev.push_back("gun");
    ++g_gunCalls;
    ID3D11ShaderResourceView* nul = nullptr;
    g_ctx->PSSetShaderResources(5, 1, &nul);
    D3D11_VIEWPORT vp = {0, 0, 640, 360, 0, 1};
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->OMSetRenderTargets(1, &g_bbRtv, nullptr);
    const float red[4] = {1, 0, 0, 1};
    g_ctx->ClearRenderTargetView(g_bbRtv, red);
    g_ctx->CopySubresourceRegion(g_bb, 0, 600, 300, 0, g_green, 0, nullptr);
    g_ctx->ClearRenderTargetView(g_bbRtv, red);
}
UINT Pixel(int x, int y) {
    g_ctx->CopyResource(g_staging, g_bb);
    D3D11_MAPPED_SUBRESOURCE m;
    UINT v = 0;
    HRESULT hr = g_staging ? g_ctx->Map(g_staging, 0, D3D11_MAP_READ, 0, &m) : E_POINTER;
    if (SUCCEEDED(hr)) {
        v = *reinterpret_cast<const UINT*>(static_cast<const unsigned char*>(m.pData) + y * m.RowPitch + x * 4);
        g_ctx->Unmap(g_staging, 0);
    } else {
        static int n = 0;
        if (n++ < 2) printf("      Map failed hr 0x%08lx staging %p\n", hr, (void*)g_staging);
    }
    return v;
}
ID3D11Texture2D* Tex(UINT w, UINT h, DXGI_FORMAT f, UINT bind, D3D11_USAGE u = D3D11_USAGE_DEFAULT, UINT cpu = 0, UINT fill = 0) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w, td.Height = h, td.MipLevels = 1, td.ArraySize = 1, td.Format = f, td.SampleDesc.Count = 1, td.BindFlags = bind, td.Usage = u,
    td.CPUAccessFlags = cpu;
    std::vector<UINT> px(w * h, fill);
    D3D11_SUBRESOURCE_DATA sd = {px.data(), w * 4, 0};
    ID3D11Texture2D* t = nullptr;
    g_dev->CreateTexture2D(&td, (fill && f == DXGI_FORMAT_R8G8B8A8_UNORM) ? &sd : nullptr, &t);
    return t;
}
}
int wmain() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"hfps_ui_layer_test";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"ui layer", WS_OVERLAPPEDWINDOW, 0, 0, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = 640, sd.BufferDesc.Height = 360, sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1, sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT, sd.BufferCount = 1, sd.OutputWindow = hwnd, sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &g_dev,
                                             nullptr, &g_ctx))) {
        printf("device failed\n");
        return 2;
    }
    using namespace vmgame;
    g_log = TLog;
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_t0);
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_fp = true;
    sc->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&g_bb));
    g_dev->CreateRenderTargetView(g_bb, nullptr, &g_bbRtv);
    g_scene = Tex(320, 180, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET);
    g_dev->CreateRenderTargetView(g_scene, nullptr, &g_sceneRtv);
    g_sceneLdr = Tex(640, 360, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_DEFAULT, 0, 0xff404040u);
    g_green = Tex(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_DEFAULT, 0, 0xff00ff00u);
    g_staging = Tex(640, 360, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
    ID3D11Texture2D* srvTex = Tex(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 0xffffffffu);
    g_dev->CreateShaderResourceView(srvTex, nullptr, &g_srv);
    D3D11_SAMPLER_DESC smp = {};
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, smp.MaxLOD = 1;
    g_dev->CreateSamplerState(&smp, &g_samp);
    D3D11_BUFFER_DESC bd = {16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    g_dev->CreateBuffer(&bd, nullptr, &g_cb);
    D3D11_BLEND_DESC bl = {};
    bl.RenderTarget[0].BlendEnable = TRUE, bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA, bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD, bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE, bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD, bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_dev->CreateBlendState(&bl, &g_blend);
    ID3D11Texture2D* depth = Tex(640, 360, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL);
    g_dev->CreateDepthStencilView(depth, nullptr, &g_dsv);
    uintptr_t vtOrig = *reinterpret_cast<uintptr_t*>(g_ctx);
    if (!MakeFake()) {
        printf("fake engine shader failed (d3dcompiler_47)\n");
        return 3;
    }
    printf("      device removed reason before anything: 0x%08lx\n", g_dev->GetDeviceRemovedReason());
    printf("== probe\n");
    g_uiLayerMode = 0, g_probeEnabled = getenv("HFPS_TEST_NOHOOK") ? 0 : 1;
    for (int frame = 0; frame < kProbeWaitFrames + 6; ++frame) {
        EngineScene(g_ctx);
        EngineFinal(g_ctx);
        EngineUi(g_ctx);
        g_ctx->CopyResource(g_staging, g_bb);
        g_probeMute = true;
        UiLayerOnPresent(g_ctx, g_bb, 640, 360);
        g_ctx->OMSetRenderTargets(1, &g_bbRtv, nullptr);
        g_ctx->Draw(3, 0);
        g_probeMute = false;
        sc->Present(0, 0);
    }
    Check(Has("ui-probe: hooked immediate context") == 1, "probe: hooked the immediate context once");
    Check(Has("ui-probe: frame ") == 2, "probe: two frames logged");
    Check(Has("2 render-target bindings (0 dropped), 10 draws (0 before any binding), 1 bindings / 6 draws on the back buffer, 2 draw runs on it") == 2,
          "probe: per frame 2 bindings, 10 draws, back buffer 1 binding / 6 draws in 2 runs (our muted draw not counted)");
    Check(Has("rt=tex 320x180 fmt 10 rtv 1 dsv 0 draws 4 [DI 4 D 0") == 2, "probe: offscreen scene segment 4 DrawIndexed, format 10");
    uintptr_t rf = reinterpret_cast<uintptr_t>(&EngineFinal) - g_base, ru = reinterpret_cast<uintptr_t>(&EngineUiQuad) - g_base;
    int runFinal = 0, runUi = 0;
    for (auto& l : g_lines) {
        size_t s = l.find("stack: e+");
        if (l.find("bb run ") == std::string::npos || s == std::string::npos) continue;
        unsigned long long rva = strtoull(l.c_str() + s + 9, nullptr, 16);
        if (l.find(": 1 draws (D )") != std::string::npos && rva > rf && rva < rf + 0x80) ++runFinal;
        if (l.find(": 5 draws (DI )") != std::string::npos && rva > ru && rva < ru + 0x40) ++runUi;
    }
    Check(runFinal == 2 && runUi == 2, "probe: draw runs split by call site, first frame = EngineFinal / EngineUiQuad");
    Check(Has("CopyResource BACKBUFFER 640x360 f28 -> 640x360 f28") == 2, "probe: copy out of the back buffer recorded");
    Check(Has("vtable restored") == 1 && *reinterpret_cast<uintptr_t*>(g_ctx) == vtOrig, "probe: vtable restored after recording (mode 0)");
    Check(Has("ui-probe: done (2 frames logged)") == 1, "probe: done line");
    printf("      device removed reason after probe part: 0x%08lx\n", g_dev->GetDeviceRemovedReason());
    printf("== ui layer\n");
    g_uiLayerMode = 1, g_probeEnabled = 0;
    g_lastSc = sc, g_presentTid = GetCurrentThreadId();
    g_uiLayerGunFn = TestGun;
    std::vector<int> parts;
    std::vector<UINT> pxUi, pxGun;
    std::vector<std::string> order;
    auto frame = [&](bool copy) {
        g_ev.clear();
        EngineScene(g_ctx);
        EngineState(g_ctx);
        if (copy) EngineCopy(g_ctx);
        EngineState(g_ctx);
        EngineUi2D(g_ctx);
        int part = UiLayerPresentPart();
        g_probeMute = true;
        UiLayerOnPresent(g_ctx, g_bb, 640, 360);
        if (part == kPartAll) {
            g_ev.push_back("gun@present");
            const float red[4] = {1, 0, 0, 1};
            g_ctx->ClearRenderTargetView(g_bbRtv, red);
        }
        g_probeMute = false;
        parts.push_back(part);
        pxUi.push_back(Pixel(108, 108));
        pxGun.push_back(Pixel(300, 300));
        std::string o;
        for (auto& e : g_ev) o += e + " ";
        order.push_back(o);
        sc->Present(0, 0);
        printf("      after frame %zu: device removed reason 0x%08lx\n", parts.size() - 1, g_dev->GetDeviceRemovedReason());
    };
    for (int i = 0; i < 4; ++i) frame(true);
    const UINT kRed = 0xff0000ffu, kGreen = 0xff00ff00u;
    for (size_t i = 0; i < order.size(); ++i) printf("      frame %zu: part %d  ui px %08x  gun px %08x  order %s\n", i, parts[i], pxUi[i], pxGun[i], order[i].c_str());
    Check(Has("ui-layer: own pipeline state for mid-frame drawing created") == 1, "layer: own pipeline state object created once");
    Check(parts[0] == kPartAll && order[0] == "copy ui gun@present ", "layer: frame 0 (hook not yet on) draws everything at Present");
    bool midOk = true;
    for (int i = 1; i < 4; ++i) midOk &= parts[i] == kPartCross && order[i] == "copy gun ui " && pxUi[i] == kGreen && pxGun[i] == kRed;
    Check(midOk, "layer: frames 1-3: gun right after the copy, before the UI; Present draws only the crosshair; UI pixel green, gun pixel red");
    Check(g_gunCalls == 3, "layer: gun drawn exactly once per frame (its own back-buffer copy did not re-trigger)");
    Check(g_stateOk, "layer: engine pipeline state (SRV 5, sampler 3, CB 7, VS CB 9, viewport, blend, topology, RT + DSV) intact when the copy returns");
    Check(*reinterpret_cast<uintptr_t*>(g_ctx) == reinterpret_cast<uintptr_t>(g_ctxVtCopy), "layer: context still on our table after state swaps");
    Check(Has("ui-layer: gun drawn right after the game copied into the back buffer (caller e+") == 1, "layer: first mid-frame draw logged with caller");
    frame(false);
    frame(true);
    Check(parts[4] == kPartAll && pxUi[4] == kRed && parts[5] == kPartCross && pxUi[5] == kGreen, "layer: frame without copy falls back to Present, next frame mid again");
    Check(Has("ui-layer: a frame without a back-buffer copy from the game") == 1, "layer: fallback logged once");
    uintptr_t base = g_base;
    g_base = 0x10000;
    frame(true);
    g_base = base;
    Check(parts[6] == kPartAll && order[6] == "copy ui gun@present ", "layer: copy not called from the game exe -> no mid-frame draw");
    DWORD tid = g_presentTid;
    g_presentTid = tid + 1;
    frame(true);
    g_presentTid = tid;
    Check(parts[7] == kPartAll && Has("not drawing there") == 1, "layer: copy on another thread -> no mid-frame draw (logged once)");
    frame(true);
    Check(parts[8] == kPartCross && pxUi[8] == kGreen, "layer: back to mid-frame drawing afterwards");
    printf(g_fail ? "UI LAYER TEST FAILED (%d)\n" : "UI LAYER TEST OK\n", g_fail);
    return g_fail ? 1 : 0;
}
