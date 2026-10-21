#include "../src/vm_game.cpp"
#include <cstdarg>
namespace {
void HLog(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}
bool SaveBmp(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc, const wchar_t* path) {
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&bb)))) return false;
    D3D11_TEXTURE2D_DESC td;
    bb->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    bool ok = SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &st));
    if (ok) {
        ctx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        ok = SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m));
        if (ok) {
            FILE* f = _wfopen(path, L"wb");
            if (f) {
                unsigned w = td.Width, h = td.Height, row = w * 3, pad = (4 - row % 4) % 4, size = 54 + (row + pad) * h;
                unsigned char hd[54] = {'B', 'M'};
                memcpy(hd + 2, &size, 4);
                unsigned off = 54, dib = 40, planes = 1 | (24 << 16);
                memcpy(hd + 10, &off, 4);
                memcpy(hd + 14, &dib, 4);
                memcpy(hd + 18, &w, 4);
                memcpy(hd + 22, &h, 4);
                memcpy(hd + 26, &planes, 4);
                fwrite(hd, 1, 54, f);
                unsigned char* line = static_cast<unsigned char*>(malloc(row + pad + 4));
                for (unsigned y = 0; y < h; ++y) {
                    const unsigned char* s = static_cast<const unsigned char*>(m.pData) + static_cast<size_t>(h - 1 - y) * m.RowPitch;
                    for (unsigned x = 0; x < w; ++x) {
                        line[x * 3 + 0] = s[x * 4 + 2];
                        line[x * 3 + 1] = s[x * 4 + 1];
                        line[x * 3 + 2] = s[x * 4 + 0];
                    }
                    memset(line + row, 0, pad);
                    fwrite(line, 1, row + pad, f);
                }
                free(line);
                fclose(f);
            }
            ctx->Unmap(st, 0);
        }
        st->Release();
    }
    bb->Release();
    return ok;
}
void State(const char* tag) {
    using namespace vmgame;
    printf("STATE %-22s model=%d (%ls) phase=%d switching=%ld want=%ld roster=%ld ammo=%ld/%ld reload=%ld canFire=%d busy=%d clip=%d spin=%ld spent=%ld%ld"
           " gunKick=%.4f,%.4f,%.4f m / %.2f,%.2f,%.2f deg adsW=%.2f barrel=%.1fdeg %.0fdeg/s (bone %d dir %+.0f) sustain t=%.2f w=%.2f ik R res %.1f shift %.1f / L res %.1f shift %.1f mm\n",
           tag, g_modelIdx, g_w[g_modelIdx].set.weaponId, g_sw.phase, g_switching, g_wantIdx, g_rosterN, g_mag.ammo, g_mag.size,
           g_mag.reload, CanFire() ? 1 : 0, Busy() ? 1 : 0, g_an.cur, g_spin.state, g_spent[0], g_spent[1], g_gunKick.pos[0],
           g_gunKick.pos[1], g_gunKick.pos[2], g_gunKick.rot[0], g_gunKick.rot[1], g_gunKick.rot[2], g_mo.adsW, g_mo.barrelAngle * 57.29578f,
           g_mo.barrelSpeed * 57.29578f, g_w[g_modelIdx].rend.BarrelBone(), g_w[g_modelIdx].rend.BarrelDir(), g_sustain.t, g_sustain.w,
           g_w[g_modelIdx].rend.IkResidualMm(0), g_w[g_modelIdx].rend.IkShiftMm(0), g_w[g_modelIdx].rend.IkResidualMm(1),
           g_w[g_modelIdx].rend.IkShiftMm(1));
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 4) {
        printf("usage: vm_game_harness <fake game dir> <out dir> <switch|roster>\n");
        return 1;
    }
    const wchar_t *dir = argv[1], *out = argv[2], *script = argv[3];
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"hfps_harness";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"harness", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = 1280;
    sd.BufferDesc.Height = 720;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 1;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                             nullptr, &ctx))) {
        printf("device failed\n");
        return 2;
    }
    using namespace vmgame;
    Setup(0, dir, HLog);
    g_staticOk = true;
    g_fp = true;
    InterlockedExchange(&g_reqRoster, 1);
    ID3D11RenderTargetView* rtv = nullptr;
    {
        ID3D11Texture2D* bb = nullptr;
        sc->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&bb));
        dev->CreateRenderTargetView(bb, nullptr, &rtv);
        bb->Release();
    }
    struct Ev {
        double t;
        int what;
        int n;
        const wchar_t* shot;
    };
    static const Ev evSwitch[] = {
        {0.8, 0, 0, nullptr}, {1.0, 1, 2, nullptr}, {1.05, 0, 0, nullptr}, {1.3, 2, 0, L"a_drop.bmp"},
        {1.3, 0, 0, nullptr}, {1.75, 0, 0, nullptr}, {1.9, 2, 0, L"b_raise.bmp"}, {2.6, 0, 0, nullptr},
        {2.8, 2, 0, L"c_second.bmp"}, {3.0, 1, 1, nullptr}, {4.6, 0, 0, nullptr}, {4.7, 2, 0, L"d_back.bmp"},
        {5.0, 4, 7, nullptr}, {5.05, 3, 0, nullptr}, {5.1, 0, 0, nullptr}, {5.5, 2, 0, L"e_reload.bmp"},
        {7.2, 0, 0, nullptr}, {7.45, 0, 0, nullptr}, {7.6, 4, 0, nullptr}, {7.65, 3, 0, nullptr},
        {7.7, 0, 0, nullptr}, {8.2, 2, 0, L"f_reload_empty.bmp"}, {10.3, 0, 0, nullptr}, {10.5, 0, 0, nullptr},
        {10.6, 5, 1, nullptr}, {10.65, 0, 0, nullptr}, {12.4, 0, 0, nullptr}, {12.5, 5, 1, nullptr},
        {14.3, 0, 0, nullptr},
    };
    static const Ev evRoster[] = {{0.6, 0, 0, nullptr}, {0.7, 2, 0, L"roster.bmp"}};
    static const Ev evSteps[] = {
        {0.8, 7, 6, nullptr}, {2.8, 0, 0, nullptr}, {2.8, 8, 2, nullptr}, {2.8, 7, 10, nullptr}, {5.0, 0, 0, nullptr},
        {5.0, 8, 6, nullptr}, {5.0, 7, 12, nullptr}, {7.2, 0, 0, nullptr}, {7.2, 8, 1, nullptr}, {7.2, 7, 4, nullptr},
        {9.2, 7, 0, nullptr}, {9.6, 0, 0, nullptr},
    };
    static const Ev evSpin[] = {
        {0.8, 1, 2, nullptr}, {3.0, 0, 0, nullptr}, {3.1, 6, 30, nullptr}, {3.3, 0, 0, nullptr}, {3.55, 0, 0, nullptr},
        {3.8, 2, 0, L"s_spinning.bmp"}, {4.1, 0, 0, nullptr}, {6.1, 0, 0, nullptr}, {6.3, 0, 0, nullptr}, {6.5, 6, 120, nullptr},
        {6.55, 0, 0, nullptr}, {12.0, 0, 0, nullptr}, {16.0, 0, 0, nullptr}, {18.6, 0, 0, nullptr}, {19.0, 0, 0, nullptr},
        {21.5, 0, 0, nullptr},
    };
    static const Ev evSpent[] = {
        {0.8, 1, 2, nullptr}, {3.0, 0, 0, nullptr}, {3.1, 6, 30, nullptr}, {3.9, 0, 0, nullptr}, {4.6, 0, 0, nullptr},
        {5.5, 0, 0, nullptr}, {6.6, 0, 0, nullptr}, {6.7, 1, 2, nullptr}, {6.8, 5, 1, nullptr}, {7.5, 0, 0, nullptr},
        {7.6, 9, 0, nullptr}, {8.0, 0, 0, nullptr}, {8.1, 1, 2, nullptr}, {10.6, 0, 0, nullptr},
    };
    static const Ev evEmpty[] = {
        {0.8, 0, 0, nullptr}, {1.0, 6, 45, nullptr}, {2.0, 0, 0, nullptr}, {3.05, 0, 0, nullptr}, {3.2, 0, 0, nullptr},
        {3.4, 2, 0, L"g_auto_reload.bmp"}, {4.5, 0, 0, nullptr}, {5.5, 2, 0, L"h_auto_reload_late.bmp"}, {6.0, 0, 0, nullptr},
        {6.2, 0, 0, nullptr}, {6.6, 0, 0, nullptr},
    };
    static const Ev evKick[] = {
        {0.8, 0, 0, nullptr}, {1.0, 6, 10, nullptr}, {1.1, 0, 0, nullptr}, {1.5, 0, 0, nullptr}, {1.5, 2, 0, L"k_hip_burst.bmp"},
        {1.9, 0, 0, nullptr}, {2.7, 0, 0, nullptr}, {2.7, 2, 0, L"k_hip_settled.bmp"}, {2.8, 8, 1, nullptr}, {3.8, 0, 0, nullptr},
        {3.9, 6, 10, nullptr}, {4.4, 0, 0, nullptr}, {4.4, 2, 0, L"k_ads_burst.bmp"}, {5.6, 0, 0, nullptr},
    };
    static const Ev evAds[] = {
        {0.8, 0, 0, nullptr}, {0.9, 2, 0, L"a_hip.bmp"}, {1.0, 8, 1, nullptr}, {2.2, 0, 0, nullptr}, {2.2, 2, 0, L"a_ads.bmp"},
        {2.4, 8, 0, nullptr}, {3.4, 0, 0, nullptr}, {3.4, 2, 0, L"a_hip_again.bmp"},
    };
    const Ev* ev = !wcscmp(script, L"roster") ? evRoster : !wcscmp(script, L"empty") ? evEmpty : !wcscmp(script, L"steps") ? evSteps
                 : !wcscmp(script, L"spin") ? evSpin : !wcscmp(script, L"spent") ? evSpent : !wcscmp(script, L"kick") ? evKick
                 : !wcscmp(script, L"ads") ? evAds : evSwitch;
    size_t nev = !wcscmp(script, L"roster") ? sizeof evRoster / sizeof *ev
                 : !wcscmp(script, L"kick") ? sizeof evKick / sizeof *ev
                 : !wcscmp(script, L"ads") ? sizeof evAds / sizeof *ev
                 : !wcscmp(script, L"empty") ? sizeof evEmpty / sizeof *ev
                 : !wcscmp(script, L"steps") ? sizeof evSteps / sizeof *ev
                 : !wcscmp(script, L"spin") ? sizeof evSpin / sizeof *ev
                 : !wcscmp(script, L"spent") ? sizeof evSpent / sizeof *ev
                                             : sizeof evSwitch / sizeof *ev;
    float speedNow = 0.f;
    double fireUntil = -1, nextShot = 0;
    int shots = 0, refused = 0;
    double t0 = Now();
    size_t next = 0;
    while (next < nev) {
        double t = Now() - t0;
        const float clear[4] = {0.35f, 0.42f, 0.30f, 1.f};
        ctx->ClearRenderTargetView(rtv, clear);
        for (int i = 0; i < g_modelCount; ++i) {
            vm::Settings& s = g_w[i].set;
            s.fireVolume = s.fireMechVolume = s.fireLfeVolume = s.fireAtmoVolume = s.fireTailVolume = 0.f;
            s.animSoundVolume = s.stepFootVolume = s.stepClothVolume = s.stepRattleVolume = s.spinVolume = 0.f;
        }
        SetTrigger(t < fireUntil);
        if (t < fireUntil && t >= nextShot) {
            double gap = FireInterval() > 0 ? FireInterval() : 0.1;
            if (CanFire()) {
                Fire();
                ++shots;
            } else {
                ++refused;
            }
            nextShot = t - nextShot < gap ? nextShot + gap : t + gap;
        }
        PublishView(0.f, 0.f, speedNow);
        DrawLayer(sc);
        while (next < nev && t >= ev[next].t) {
            const Ev& e = ev[next++];
            char tag[64];
            snprintf(tag, sizeof tag, "t=%.2f", e.t);
            if (e.what == 0) State(tag);
            if (e.what == 1) SelectWeapon(e.n);
            if (e.what == 2) {
                wchar_t p[MAX_PATH * 2];
                swprintf(p, MAX_PATH * 2, L"%ls\\%ls", out, e.shot);
                printf("SHOT %ls %s\n", e.shot, SaveBmp(dev, ctx, sc, p) ? "ok" : "FAILED");
            }
            if (e.what == 3) printf("RELOAD started=%d\n", StartMagReload() ? 1 : 0);
            if (e.what == 4) InterlockedExchange(&g_mag.ammo, e.n);
            if (e.what == 5) CycleWeapon(e.n);
            if (e.what == 6) {
                fireUntil = t + e.n * 0.1;
                printf("FIRE held %.1f s\n", e.n * 0.1);
            }
            if (e.what == 7) speedNow = static_cast<float>(e.n);
            if (e.what == 8) PublishStance((e.n & 1) != 0, (e.n & 2) != 0, (e.n & 4) != 0);
            if (e.what == 9) {
                wchar_t p[MAX_PATH * 2];
                swprintf(p, MAX_PATH * 2, L"%ls\\hero_fps_armory.txt", dir);
                if (FILE* f = _wfopen(p, L"w")) {
                    fprintf(f, "mode=custom\nowned=\n");
                    fclose(f);
                }
                InterlockedExchange(&g_reqRoster, 1);
                printf("NEW BATTLE (armory file rewritten)\n");
            }
            if (e.what == 0) printf("      shots=%d refused-frames=%d\n", shots, refused);
        }
        sc->Present(0, 0);
        Sleep(8);
    }
    printf("DONE failed=%d\n", g_failed ? 1 : 0);
    return g_failed ? 3 : 0;
}
