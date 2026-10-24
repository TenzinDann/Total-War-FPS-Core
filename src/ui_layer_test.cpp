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
alignas(16) unsigned char g_fakeRenderer[0x4200];
uintptr_t g_fakeRendererPtr;
uintptr_t g_fakeRendererVt[0x210 / 8];
uintptr_t g_fakeBbVt[9];
uintptr_t g_fakeBbObj[8], g_fakeBbObj2[8], g_fakeRctx[4], g_fakeSrc[6];
__attribute__((noinline)) uintptr_t FakeGetBb(void* r) { return *reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(r) + 0x4128); }
__attribute__((noinline)) uintptr_t FakeNop(void*) { return 0; }
__attribute__((noinline)) char FakeBbCopy(void* obj, void* rctx, void* src, uintptr_t) {
    auto* c = *reinterpret_cast<ID3D11DeviceContext**>(static_cast<unsigned char*>(rctx) + 0x18);
    auto* d = *reinterpret_cast<ID3D11Resource**>(static_cast<unsigned char*>(obj) + 0x28);
    auto* s = *reinterpret_cast<ID3D11Resource**>(static_cast<unsigned char*>(src) + 0x28);
    c->CopyResource(d, s);
    return 1;
}
uintptr_t CurBbObj() { return *reinterpret_cast<uintptr_t*>(g_fakeRenderer + 0x4128); }
__attribute__((noinline)) void EngineCopy(ID3D11DeviceContext* ctx) {
    g_ev.push_back("copy");
    auto* t = reinterpret_cast<uintptr_t*>(CurBbObj());
    auto fn = reinterpret_cast<char (*)(void*, void*, void*, uintptr_t)>(reinterpret_cast<uintptr_t*>(t[0])[5]);
    fn(t, g_fakeRctx, g_fakeSrc, 0);
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
alignas(16) unsigned char g_fakeGfx[0x16b490];
uintptr_t g_fakeGfxPtr;
alignas(16) unsigned char g_fakeFg[0x660];
uintptr_t g_fakePassArr[3];
uintptr_t g_fakeCompVt[9];
uintptr_t g_fakeOtherVt[9];
uintptr_t g_fakeComp[4], g_fakeComp2[4], g_fakeOther[2], g_fakeOther2[2];
int g_compMode = 0;
int& FakeAa() { return *reinterpret_cast<int*>(g_fakeGfx + 0x16b488); }
__attribute__((noinline)) uintptr_t FakeCompExec(void*, uintptr_t, uintptr_t, uintptr_t) {
    g_ev.push_back("comp");
    if (FakeAa() < 1 || FakeAa() > 3) return 0;
    if (g_compMode == 1) {
        EngineCopy(g_ctx);
        return 1;
    }
    g_ctx->OMSetRenderTargets(1, &g_bbRtv, nullptr);
    g_ctx->CopyResource(g_bb, g_sceneLdr);
    if (g_compMode == 2) g_ctx->OMSetRenderTargets(1, &g_sceneRtv, nullptr);
    return 1;
}
__attribute__((noinline)) uintptr_t FakeOtherExec(void*, uintptr_t, uintptr_t, uintptr_t) {
    g_ev.push_back("pass");
    return 0;
}
__attribute__((noinline)) void EnginePasses() {
    uint32_t n = *reinterpret_cast<uint32_t*>(g_fakeFg + 0x654);
    auto* arr = *reinterpret_cast<uintptr_t**>(g_fakeFg + 0x658);
    for (uint32_t i = 0; i < n; ++i) {
        auto* p = reinterpret_cast<uintptr_t*>(arr[i]);
        auto fn = reinterpret_cast<uintptr_t (*)(void*, uintptr_t, uintptr_t, uintptr_t)>(reinterpret_cast<uintptr_t*>(p[0])[1]);
        fn(p, 0, 0, 0);
    }
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
    for (int i = 0; i < 8; ++i) g_fakeBbVt[i] = reinterpret_cast<uintptr_t>(&FakeNop);
    g_fakeBbVt[5] = reinterpret_cast<uintptr_t>(&FakeBbCopy);
    g_fakeBbObj[0] = g_fakeBbObj2[0] = reinterpret_cast<uintptr_t>(g_fakeBbVt);
    g_fakeBbObj[5] = g_fakeBbObj2[5] = reinterpret_cast<uintptr_t>(g_bb);
    g_fakeRctx[3] = reinterpret_cast<uintptr_t>(g_ctx);
    g_fakeSrc[5] = reinterpret_cast<uintptr_t>(g_sceneLdr);
    g_fakeRendererVt[0x208 / 8] = reinterpret_cast<uintptr_t>(&FakeGetBb);
    *reinterpret_cast<uintptr_t*>(g_fakeRenderer) = reinterpret_cast<uintptr_t>(g_fakeRendererVt);
    *reinterpret_cast<uintptr_t*>(g_fakeRenderer + 0x4128) = reinterpret_cast<uintptr_t>(g_fakeBbObj);
    g_fakeRendererPtr = reinterpret_cast<uintptr_t>(g_fakeRenderer);
    g_ra.globalRenderer = reinterpret_cast<uintptr_t>(&g_fakeRendererPtr) - g_base;
    g_ra.vtGetBbSlot = 0x208, g_ra.fnGetBb = reinterpret_cast<uintptr_t>(&FakeGetBb) - g_base, g_ra.oBackBufferObj = 0x4128;
    g_ra.fnBbCopy = reinterpret_cast<uintptr_t>(&FakeBbCopy) - g_base;
    g_bbRouteOk = true;
    const uintptr_t fakeVt = reinterpret_cast<uintptr_t>(g_fakeBbVt);
    bool ctxUntouched = true;
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
        UiLayerBbStep();
        ctxUntouched &= *reinterpret_cast<uintptr_t*>(g_ctx) == vtOrig;
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
    Check(Has("ui-layer: own pipeline state for mid-frame drawing created") == 1, "layer: own pipeline state object created once");
    Check(parts[0] == kPartAll && order[0] == "copy ui gun@present ", "layer: frame 0 (hook not yet on) draws everything at Present");
    bool midOk = true;
    for (int i = 1; i < 4; ++i) midOk &= parts[i] == kPartCross && order[i] == "copy gun ui " && pxUi[i] == kGreen && pxGun[i] == kRed;
    Check(midOk, "layer: frames 1-3: gun right after the copy, before the UI; Present draws only the crosshair; UI pixel green, gun pixel red");
    Check(g_gunCalls == 3, "layer: gun drawn exactly once per frame (its own back-buffer copy did not re-trigger)");
    Check(g_stateOk, "layer: engine pipeline state (SRV 5, sampler 3, CB 7, VS CB 9, viewport, blend, topology, RT + DSV) intact when the copy returns");
    Check(g_fakeBbObj[0] == reinterpret_cast<uintptr_t>(g_bbVtCopy) && g_bbObj == reinterpret_cast<uintptr_t>(g_fakeBbObj) &&
              Has("ui-layer: hooked the game's back-buffer object") == 1 && Has("copied (8 slots)") == 1,
          "layer: only the game's back-buffer object got our vtable copy (8 slots copied)");
    Check(g_bbVtCopy[0] == reinterpret_cast<uintptr_t>(&FakeNop) && g_bbVtCopy[5] == reinterpret_cast<uintptr_t>(&HkBbCopy) &&
              g_bbCopyOrig == reinterpret_cast<FnBbCopy>(&FakeBbCopy) && g_fakeBbVt[5] == reinterpret_cast<uintptr_t>(&FakeBbCopy),
          "layer: copy keeps the other slots, slot +0x28 ours, original vtable itself not modified");
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
    Check(g_fakeBbObj[0] == reinterpret_cast<uintptr_t>(g_bbVtCopy) && Has("back-buffer object not found") == 1,
          "layer: renderer unreadable for a frame -> hook kept, logged once");
    DWORD tid = g_presentTid;
    g_presentTid = tid + 1;
    frame(true);
    g_presentTid = tid;
    Check(parts[7] == kPartAll && Has("not drawing there") == 1, "layer: copy on another thread -> no mid-frame draw (logged once)");
    frame(true);
    Check(parts[8] == kPartCross && pxUi[8] == kGreen, "layer: back to mid-frame drawing afterwards");
    g_fp = false;
    UiLayerBbStep();
    Check(g_fakeBbObj[0] == fakeVt && !g_bbObj && Has("vtable restored (first person off)") == 1,
          "layer: first person off -> back-buffer object vtable restored");
    frame(true);
    Check(parts[9] == kPartAll && order[9] == "copy ui gun@present " && g_fakeBbObj[0] == fakeVt, "layer: first person off -> plain copy, nothing hooked");
    g_fp = true;
    frame(true);
    frame(true);
    Check(parts[10] == kPartAll && parts[11] == kPartCross && pxUi[11] == kGreen && Has("ui-layer: hooked the game's back-buffer object") == 2 &&
              Has("copied (8 slots)") == 1,
          "layer: first person on again -> hooked again (vtable copy reused), mid-frame drawing next frame");
    *reinterpret_cast<uintptr_t*>(g_fakeRenderer + 0x4128) = reinterpret_cast<uintptr_t>(g_fakeBbObj2);
    frame(true);
    frame(true);
    Check(g_fakeBbObj[0] == fakeVt && g_fakeBbObj2[0] == reinterpret_cast<uintptr_t>(g_bbVtCopy) &&
              Has("vtable restored (back-buffer object changed)") == 1 && parts[12] == kPartAll && parts[13] == kPartCross && pxUi[13] == kGreen,
          "layer: back-buffer object replaced -> old one restored, new one hooked, mid-frame drawing continues");
    g_bbRouteOk = false;
    frame(true);
    frame(true);
    Check(g_fakeBbObj2[0] == fakeVt && Has("vtable restored (mid-frame route off)") == 1 && parts[15] == kPartAll &&
              order[15] == "copy ui gun@present ",
          "layer: route not verified -> nothing hooked, gun at Present");
    Check(ctxUntouched, "layer: D3D immediate context vtable never touched in mode 1 (game draws reach other hooks exactly as without us)");
    printf("== composite step\n");
    for (int i = 0; i < 8; ++i) g_fakeCompVt[i] = g_fakeOtherVt[i] = reinterpret_cast<uintptr_t>(&FakeNop);
    g_fakeCompVt[1] = reinterpret_cast<uintptr_t>(&FakeCompExec);
    g_fakeOtherVt[1] = reinterpret_cast<uintptr_t>(&FakeOtherExec);
    g_fakeComp[0] = g_fakeComp2[0] = reinterpret_cast<uintptr_t>(g_fakeCompVt);
    g_fakeOther[0] = g_fakeOther2[0] = reinterpret_cast<uintptr_t>(g_fakeOtherVt);
    g_fakePassArr[0] = reinterpret_cast<uintptr_t>(g_fakeOther), g_fakePassArr[1] = reinterpret_cast<uintptr_t>(g_fakeComp),
    g_fakePassArr[2] = reinterpret_cast<uintptr_t>(g_fakeOther2);
    *reinterpret_cast<uint32_t*>(g_fakeFg + 0x654) = 3;
    *reinterpret_cast<uintptr_t*>(g_fakeFg + 0x658) = reinterpret_cast<uintptr_t>(g_fakePassArr);
    *reinterpret_cast<uintptr_t*>(g_fakeGfx + 0x1d0) = reinterpret_cast<uintptr_t>(g_fakeFg);
    FakeAa() = 2;
    g_fakeGfxPtr = reinterpret_cast<uintptr_t>(g_fakeGfx);
    g_ra.globalGfx = reinterpret_cast<uintptr_t>(&g_fakeGfxPtr) - g_base, g_ra.oFrameGraph = 0x1d0, g_ra.oPassList = 0x650, g_ra.oAaSetting = 0x16b488;
    g_ra.vtComposite = reinterpret_cast<uintptr_t>(g_fakeCompVt) - g_base;
    g_compRouteOk = true;
    const uintptr_t compVt = reinterpret_cast<uintptr_t>(g_fakeCompVt);
    std::vector<int> cparts;
    std::vector<UINT> cpxUi, cpxGun;
    std::vector<std::string> corder;
    auto cframe = [&]() {
        g_ev.clear();
        EngineScene(g_ctx);
        EngineState(g_ctx);
        EnginePasses();
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
        UiLayerBbStep();
        UiLayerCompStep();
        ctxUntouched &= *reinterpret_cast<uintptr_t*>(g_ctx) == vtOrig;
        cparts.push_back(part);
        cpxUi.push_back(Pixel(108, 108));
        cpxGun.push_back(Pixel(300, 300));
        std::string o;
        for (auto& e : g_ev) o += e + " ";
        corder.push_back(o);
        sc->Present(0, 0);
    };
    int guns0 = g_gunCalls;
    for (int i = 0; i < 4; ++i) cframe();
    Check(cparts[0] == kPartAll && corder[0] == "pass comp pass ui gun@present ", "comp: frame 0 (not hooked yet) -> everything at Present");
    bool cOk = true;
    for (int i = 1; i < 4; ++i) cOk &= cparts[i] == kPartCross && corder[i] == "pass comp gun pass ui " && cpxUi[i] == kGreen && cpxGun[i] == kRed;
    Check(cOk, "comp: no upscaler, frames 1-3: gun right after the composite step, before the next pass and the UI; UI pixel green, gun pixel red");
    Check(g_gunCalls - guns0 == 3, "comp: gun drawn exactly once per frame");
    Check(g_fakeComp[0] == reinterpret_cast<uintptr_t>(g_compVtCopy) && g_compObj == reinterpret_cast<uintptr_t>(g_fakeComp) &&
              g_fakeOther[0] == reinterpret_cast<uintptr_t>(g_fakeOtherVt) && g_fakeOther2[0] == reinterpret_cast<uintptr_t>(g_fakeOtherVt) &&
              Has("ui-layer: hooked the game's composite step") == 1 && Has("(entry 1 of 3, 1 with that vtable)") == 1,
          "comp: only the composite step (entry 1 of 3) got our vtable copy, the other passes untouched");
    Check(g_compVtCopy[1] == reinterpret_cast<uintptr_t>(&HkCompExec) && g_compVtCopy[0] == reinterpret_cast<uintptr_t>(&FakeNop) &&
              g_fakeCompVt[1] == reinterpret_cast<uintptr_t>(&FakeCompExec) && Has("copied (8 slots), slot +0x8 -> our hook") == 1,
          "comp: copy keeps the other slots, slot +0x8 ours, the game's vtable itself not modified");
    Check(Has("ui-layer: gun drawn right after the game's composite step drew the scene into the back buffer") == 1 && Has("anti-aliasing 2)") == 1,
          "comp: first draw after the composite step logged once (with the anti-aliasing setting)");
    g_compMode = 1;
    *reinterpret_cast<uintptr_t*>(g_fakeRenderer + 0x4128) = reinterpret_cast<uintptr_t>(g_fakeBbObj);
    g_bbRouteOk = true;
    guns0 = g_gunCalls;
    cframe();
    cframe();
    cframe();
    Check(cparts[4] == kPartCross && corder[4] == "pass comp copy gun pass ui " && cparts[5] == kPartCross && corder[5] == "pass comp copy gun pass ui " &&
              cparts[6] == kPartCross && cpxUi[6] == kGreen && g_gunCalls - guns0 == 3 && g_bbObj == reinterpret_cast<uintptr_t>(g_fakeBbObj),
          "comp: with an upscaler copy both hooks are on, gun still once per frame, under the UI");
    g_bbRouteOk = false;
    cframe();
    g_compMode = 0;
    FakeAa() = 0;
    cframe();
    Check(cparts[8] == kPartAll && corder[8] == "pass comp pass ui gun@present " && cpxUi[8] == kRed && Has("anti-aliasing setting 0") == 1,
          "comp: anti-aliasing off -> composite step does nothing, gun at Present (logged once)");
    cframe();
    Check(Has("anti-aliasing setting 0") == 1 && cparts[9] == kPartAll, "comp: anti-aliasing off logged only once");
    FakeAa() = 3;
    g_compMode = 2;
    cframe();
    Check(cparts[10] == kPartAll && Has("not the back buffer") == 1, "comp: render target left on another texture -> no mid-frame draw (logged once)");
    g_compMode = 0;
    cframe();
    Check(cparts[11] == kPartCross && cpxUi[11] == kGreen, "comp: back to drawing after the composite step");
    g_fp = false;
    UiLayerCompStep();
    Check(g_fakeComp[0] == compVt && !g_compObj && Has("vtable restored (first person off)") == 2,
          "comp: first person off -> composite step vtable restored");
    cframe();
    Check(cparts[12] == kPartAll && corder[12] == "pass comp pass ui gun@present " && g_fakeComp[0] == compVt, "comp: first person off -> nothing hooked");
    g_fp = true;
    cframe();
    cframe();
    Check(cparts[13] == kPartAll && cparts[14] == kPartCross && Has("ui-layer: hooked the game's composite step") == 2 &&
              Has("copied (8 slots), slot +0x8 -> our hook") == 1,
          "comp: first person on again -> hooked again (vtable copy reused)");
    g_fakePassArr[1] = reinterpret_cast<uintptr_t>(g_fakeComp2);
    cframe();
    cframe();
    Check(g_fakeComp[0] == compVt && g_fakeComp2[0] == reinterpret_cast<uintptr_t>(g_compVtCopy) && Has("vtable restored (composite step object changed)") == 1 &&
              cparts[15] == kPartAll && cparts[16] == kPartCross && cpxUi[16] == kGreen,
          "comp: composite step replaced -> old one restored, new one hooked, drawing continues");
    g_fakePassArr[1] = reinterpret_cast<uintptr_t>(g_fakeOther);
    cframe();
    cframe();
    Check(!g_compObj && Has("released without writing it (composite step gone from the pass list)") == 1 && Has("not in the 3-entry render pass list") == 1 &&
              g_fakeComp2[0] == reinterpret_cast<uintptr_t>(g_compVtCopy) && cparts[18] == kPartAll,
          "comp: composite step gone from the list -> not written (may be freed), logged once, gun at Present");
    g_fakeComp2[0] = compVt;
    g_fakePassArr[1] = reinterpret_cast<uintptr_t>(g_fakeComp2);
    cframe();
    cframe();
    Check(cparts[20] == kPartCross && g_compObj == reinterpret_cast<uintptr_t>(g_fakeComp2), "comp: back in the list -> hooked again");
    *reinterpret_cast<uintptr_t*>(g_fakeGfx + 0x1d0) = 0;
    cframe();
    Check(Has("render pass list not found") == 1 && g_fakeComp2[0] == reinterpret_cast<uintptr_t>(g_compVtCopy), "comp: pass list unreadable for a frame -> hook kept, logged once");
    *reinterpret_cast<uintptr_t*>(g_fakeGfx + 0x1d0) = reinterpret_cast<uintptr_t>(g_fakeFg);
    g_compRouteOk = false;
    cframe();
    cframe();
    Check(g_fakeComp2[0] == compVt && Has("vtable restored (composite route off)") == 1 && cparts[23] == kPartAll,
          "comp: route not verified -> nothing hooked, gun at Present");
    Check(ctxUntouched, "comp: D3D immediate context vtable never touched");
    for (size_t i = 0; i < corder.size(); ++i)
        printf("      comp frame %zu: part %d  ui px %08x  gun px %08x  order %s\n", i, cparts[i], cpxUi[i], cpxGun[i], corder[i].c_str());
    for (size_t i = 0; i < order.size(); ++i)
        printf("      frame %zu: part %d  ui px %08x  gun px %08x  order %s\n", i, parts[i], pxUi[i], pxGun[i], order[i].c_str());
    printf(g_fail ? "UI LAYER TEST FAILED (%d)\n" : "UI LAYER TEST OK\n", g_fail);
    return g_fail ? 1 : 0;
}
