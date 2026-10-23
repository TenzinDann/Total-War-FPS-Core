#include "vm_game.h"
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "vm_audio.h"
#include "vm_render.h"
namespace vmgame {
void MagApply();
void RefillForNewBattle();
namespace {
RenderAddrs g_ra = {};
const unsigned char kScCreateBytes[] = {0x4C, 0x8D, 0x83, 0x50, 0x13, 0x00, 0x00};
constexpr size_t kScSlots = 128;
LogFn g_log = nullptr;
uintptr_t g_base = 0;
bool g_staticOk = false;
bool g_enabled = false;
UINT g_testKey = VK_F7, g_reloadKey = VK_F8, g_animKey = VK_F9, g_modelKey = VK_F10;
UINT g_shieldKey = 'Q', g_shieldHitKey = 0;
wchar_t g_ini[MAX_PATH];
constexpr int kMaxModels = 200;
wchar_t g_models[kMaxModels][MAX_PATH * 2];
int g_modelCount = 0;
int g_modelIdx = 0;
volatile bool g_testLayer = false, g_fp = false, g_failed = false;
volatile LONG g_reload = 0, g_reqModelNext = 0, g_reqAnimNext = 0, g_reqFire = 0, g_reqDraw = 0;
volatile LONG g_reqFireLast = 0;
struct MagState {
    volatile LONG size = 0;
    volatile LONG ammo = 0;
    volatile LONG reload = 0;
    volatile LONG reqAnim = 0;
    volatile LONG64 t0Us = 0;
    volatile LONG refillUs[3] = {};
    volatile LONG fireUs = 0;
    volatile LONG disposable = 0;
    volatile LONG reserve = -1;
    LONG ammoByModel[kMaxModels];
    LONG reserveByModel[kMaxModels];
    MagState() {
        for (int i = 0; i < kMaxModels; ++i) ammoByModel[i] = -1, reserveByModel[i] = -2;
    }
    int loadedModel = -1;
} g_mag;
struct SpinState {
    volatile LONG mode = 0;
    volatile LONG state = 0;
    volatile LONG64 tUs = 0;
    volatile LONG upUs = 500000, keepUs = 800000;
} g_spin;
volatile LONG g_stance = 0;
kick::Params g_kickP[kMaxModels];
SRWLOCK g_kickLock = SRWLOCK_INIT;
volatile LONG g_kickShots = 0;
kick::GunSpring g_gunKick;
volatile LONG g_jlJumpMilli = 0, g_jlLandMilli = 0;
float g_jlX[2] = {}, g_jlV[2] = {};
kick::Sustain g_sustain;
kick::Jitter g_jitter;
kick::Onset g_onset;
double g_sustainLastShot = -1;
struct KickOut {
    float gunPos[3], gunRot[3];
    float susPos[3], susRot[3];
    float onPos[3], onRot[3];
    float w;
    int mode;
} g_kickOut = {};
struct ShieldState {
    volatile LONG want = 0;
    volatile LONG hitN = 0;
    volatile LONG wUs = 0;
    volatile LONG allowed = 0;
    volatile LONG moveMilli = 1000, zoomMilli = 1000;
    volatile LONG kickMilli[3] = {1000, 1000, 1000};
    volatile LONG camKickMdeg = 0;
    volatile LONG blockMilli = 0;
    float w = 0.f;
    float lastBlock = 0.f;
    int phase = 0;
    int lookDraw = -1;
    double shakeAt = -1;
    float shake[2] = {0.f, 0.f};
    double meleeShakeAt = -1;
    float meleeShake[2] = {0.f, 0.f};
    int meleeBlock = 0;
    float meleeShakeNow = 0.f;
    int hitLogs = 0;
    int hitFx = 0;
    unsigned rng = 0x3c6ef372u;
    int logs = 0;
} g_sh;
vm::Shield g_shieldR;
float ShieldEase() {
    float x = g_sh.w < 0.f ? 0.f : (g_sh.w > 1.f ? 1.f : g_sh.w);
    return x * x * (3.f - 2.f * x);
}
void OneHandKick(kick::Params& p, float w, const float* f) {
    if (!(w > 0.f)) return;
    float v = 1.f + (f[0] - 1.f) * w, hz = 1.f + (f[1] - 1.f) * w, r = 1.f + (f[2] - 1.f) * w;
    for (kick::Set* s : {&p.hip, &p.ads}) {
        s->climbPitch[0] *= v, s->climbPitch[1] *= v, s->punch[0] *= v, s->gunMove[1] *= v, s->gunRot[0] *= v;
        s->climbYaw[0] *= hz, s->climbYaw[1] *= hz, s->punch[1] *= hz, s->gunMove[2] *= hz, s->gunRot[1] *= hz, s->gunRot[2] *= hz;
    }
    for (float* j : {p.jitterHip, p.jitterAds}) j[0] *= v, j[1] *= hz, j[2] *= hz;
    p.recover[2] *= r;
}
void SidecarIni(int idx, wchar_t* out) {
    wcsncpy(out, g_models[idx], MAX_PATH * 2 - 1);
    out[MAX_PATH * 2 - 1] = 0;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash) wcscpy(slash + 1, L"viewmodel.ini");
}
vm::CrosshairSettings g_crossSet;
vm::Crosshair g_cross;
void ModelDir(int idx, wchar_t* out) {
    wcsncpy(out, g_models[idx], MAX_PATH * 2 - 1);
    out[MAX_PATH * 2 - 1] = 0;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (slash) *slash = 0;
}
struct IniStamp {
    bool valid = false;
    ULONGLONG size[3] = {};
    FILETIME time[3] = {};
};
IniStamp g_setStamp[kMaxModels];
void StampFile(const wchar_t* path, ULONGLONG* size, FILETIME* time) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (path[0] && GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) {
        *size = (static_cast<ULONGLONG>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        *time = fa.ftLastWriteTime;
    } else {
        *size = ~0ull;
        *time = FILETIME{};
    }
}
void ShieldIniOf(const vm::Settings& s, wchar_t* out) {
    out[0] = 0;
    if (!s.shieldModel[0]) return;
    wcsncpy(out, s.shieldModel, MAX_PATH * 2 - 16);
    out[MAX_PATH * 2 - 16] = 0;
    if (wchar_t* sl = wcsrchr(out, L'\\')) wcscpy(sl, L"\\shield.ini");
}
bool SettingsUnchanged(int idx, const vm::Settings& s) {
    const IniStamp& o = g_setStamp[idx];
    if (!o.valid) return false;
    wchar_t side[MAX_PATH * 2], sh[MAX_PATH * 2];
    SidecarIni(idx, side);
    ShieldIniOf(s, sh);
    const wchar_t* files[3] = {g_ini, sh, side};
    for (int k = 0; k < 3; ++k) {
        ULONGLONG size;
        FILETIME time;
        StampFile(files[k], &size, &time);
        if (size != o.size[k] || CompareFileTime(&time, &o.time[k])) return false;
    }
    return true;
}
void LoadAllSettings(int idx, vm::Settings& s) {
    IniStamp st;
    StampFile(g_ini, &st.size[0], &st.time[0]);
    StampFile(L"", &st.size[1], &st.time[1]);
    {
        wchar_t side[MAX_PATH * 2];
        SidecarIni(idx, side);
        StampFile(side, &st.size[2], &st.time[2]);
    }
    vm::CrosshairSettings cs;
    vm::LoadCrosshair(g_ini, cs);
    g_crossSet = cs;
    s = vm::Settings();
    vm::LoadSettings(g_ini, s);
    if (s.shieldModel[0]) {
        wchar_t rel[520], dir[MAX_PATH * 2], joined[MAX_PATH * 4], full[MAX_PATH * 2];
        wcsncpy(rel, s.shieldModel, 519);
        rel[519] = 0;
        for (wchar_t* c = rel; *c; ++c)
            if (*c == L'/') *c = L'\\';
        bool abs = (rel[0] && rel[1] == L':') || rel[0] == L'\\';
        ModelDir(idx, dir);
        if (wchar_t* sl = wcsrchr(dir, L'\\')) *sl = 0;
        swprintf(joined, MAX_PATH * 4, L"%ls%ls%ls", abs ? L"" : dir, abs ? L"" : L"\\", rel);
        if (!GetFullPathNameW(joined, MAX_PATH * 2, full, nullptr)) wcsncpy(full, joined, MAX_PATH * 2 - 1), full[MAX_PATH * 2 - 1] = 0;
        wcsncpy(s.shieldModel, full, 519);
        s.shieldModel[519] = 0;
        wchar_t shIni[MAX_PATH * 2];
        wcsncpy(shIni, full, MAX_PATH * 2 - 16);
        shIni[MAX_PATH * 2 - 16] = 0;
        if (wchar_t* sl = wcsrchr(shIni, L'\\')) wcscpy(sl, L"\\shield.ini");
        StampFile(shIni, &st.size[1], &st.time[1]);
        vm::LoadSettings(shIni, s);
        wcsncpy(s.shieldModel, full, 519);
    }
    wchar_t side[MAX_PATH * 2];
    SidecarIni(idx, side);
    vm::LoadSettings(side, s);
    kick::Params kp;
    kick::Load(g_ini, kp);
    kick::Load(side, kp);
    AcquireSRWLockExclusive(&g_kickLock);
    g_kickP[idx] = kp;
    ReleaseSRWLockExclusive(&g_kickLock);
    st.valid = true;
    g_setStamp[idx] = st;
    if (g_log)
        g_log("kick[%d]: %s cam x%.2f gun x%.2f | hip climb %.2f-%.2f punch %.2f gun back %.3f pitch %.2f | ads climb %.2f-%.2f punch %.2f "
              "gun back %.3f pitch %.2f | recover %.2f after %.2fs at %.0f deg/s | sustain x%.2f back %.3f pitch %.2f rise %.2fs | "
              "onset x%.2f hip %.4f/%.4f/%.4f m %.2f/%.2f/%.2f deg peak %.3fs end %.2fs | "
              "jitter x%.2f hip %.2f/%.2f/%.2f snap %.2f decay %.0f spring %.1fHz %.2f",
              idx, kp.enabled ? "on" : "off", kp.camScale, kp.gunScale, kp.hip.climbPitch[0], kp.hip.climbPitch[1], kp.hip.punch[0],
              kp.hip.gunMove[0], kp.hip.gunRot[0], kp.ads.climbPitch[0], kp.ads.climbPitch[1], kp.ads.punch[0], kp.ads.gunMove[0],
              kp.ads.gunRot[0], kp.recover[0], kp.recover[1], kp.recover[2], kp.sustainScale, kp.sustainMove[0], kp.sustainRot[0],
              kp.sustainRise, kp.onsetScale, kp.onsetHip[0], kp.onsetHip[1], kp.onsetHip[2], kp.onsetHip[3], kp.onsetHip[4], kp.onsetHip[5],
              kp.onsetPeak, kp.onsetEnd[1], kp.jitterScale, kp.jitterHip[0], kp.jitterHip[1], kp.jitterHip[2], kp.jitterSnap, kp.jitterDecay,
              kp.jitterSpring[0], kp.jitterSpring[1]);
}
CRITICAL_SECTION g_setLock;
vm::Settings g_setPending;
struct Pub {
    volatile LONG seq;
    float yaw, pitch, speed;
} g_pub;
struct CamPub {
    volatile LONG seq;
    bool valid;
    float eye[3], right[3], up[3], fwd[3], fovY;
} g_cam;
struct TracerQueue {
    SRWLOCK lock = SRWLOCK_INIT;
    float a[16][3], b[16][3];
    int n = 0;
} g_trq;
struct HitQueue {
    SRWLOCK lock = SRWLOCK_INIT;
    float p[16][3], d[16][3];
    bool kill[16];
    int n = 0;
} g_hitq;
int g_hitFxCount = 0, g_hitFxKills = 0;
bool ReadCam(vm::WorldCam* c) {
    for (int k = 0; k < 4; ++k) {
        LONG s1 = g_cam.seq;
        if (s1 & 1) continue;
        MemoryBarrier();
        bool v = g_cam.valid;
        memcpy(c->eye, g_cam.eye, sizeof c->eye);
        memcpy(c->right, g_cam.right, sizeof c->right);
        memcpy(c->up, g_cam.up, sizeof c->up);
        memcpy(c->fwd, g_cam.fwd, sizeof c->fwd);
        c->fovY = g_cam.fovY;
        MemoryBarrier();
        if (g_cam.seq != s1) continue;
        return v && c->fovY > 0.1f && c->fovY < 3.f;
    }
    return false;
}
struct MuzzlePub {
    volatile LONG seq;
    bool valid;
    float x, y, aspect;
    double t;
} g_muz;
using FnPresent = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
FnPresent g_origPresent = nullptr;
uintptr_t* g_scVtCopy = nullptr;
uintptr_t g_scVtOrig = 0;
CRITICAL_SECTION g_hookLock;
struct AnimSound {
    int clip;
    float t, vol;
    vm::GunSound snd;
    bool shell = false;
};
constexpr int kMaxAnimSounds = 48;
struct Weapon {
    vm::Renderer rend;
    vm::Settings set;
    vm::GunSound gun, gunAds, mech, mechAds, lfe, atmo, tail;
    vm::GunSound stepFoot, stepRun, stepSprint, stepRattle;
    vm::GunSound spinUp, spinLoop, spinDown;
    AnimSound anim[kMaxAnimSounds];
    int nAnim = 0;
    vm::MuzzleFlash flash;
    vm::ShellEject shells;
    vm::GunSound lastShot, casing, casingAds, shellLand;
    vm::GunSound meleeSfx[vm::kMeleeSfx];
    double landAt[16] = {};
    bool fxDirty = true;
    double lastShotAt = 0;
    bool tailPending = false;
};
Weapon g_w[kMaxModels];
inline Weapon& Wp() { return g_w[g_modelIdx]; }
volatile LONG g_roster[kMaxModels] = {};
volatile LONG g_rosterN = 0;
constexpr int kSlots = 4;
volatile LONG g_slot[kSlots] = {-1, -1, -1, -1};
bool g_loadoutAll = false;
volatile LONG g_rosterAll = 0;
constexpr double kUnloadDelay = 3.0;
double g_unloadAt[kMaxModels] = {};
int g_unloadPending = 0;
volatile LONG g_spent[kMaxModels] = {};
LONG g_ammoLevel[kMaxModels] = {};
volatile LONG g_infAmmo = 0;
volatile LONG g_infTac = 0;
FILETIME g_armoryTime = {};
volatile LONG g_reqRoster = 0;
volatile LONG g_reqWeapon = 0;
volatile LONG g_wantIdx = 0;
volatile LONG g_switching = 0;
struct SwitchState {
    int target = -1, phase = 0;
    double until = 0;
} g_sw;
constexpr double kDropFrac = 0.9, kRaiseFrac = 0.7;
int g_preloadNext = -1;
LARGE_INTEGER g_qpf, g_t0;
void AnimStep(vm::Look& l, double now);
bool MeleeWeapon();
void StepSounds(double now, float speed);
void ShieldStep(vm::Look& l, double now, float dt);
template <class T>
bool Rd(uintptr_t a, T& out) {
    if (a < 0x10000) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(a), &out, sizeof(T), &got) &&
           got == sizeof(T);
}
uintptr_t Ptr(uintptr_t a) {
    uintptr_t v = 0;
    return Rd(a, v) ? v : 0;
}
bool BytesMatch(uintptr_t a, const unsigned char* ref, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned char b = 0;
        if (!Rd(a + i, b) || b != ref[i]) return false;
    }
    return true;
}
double Now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart - g_t0.QuadPart) / static_cast<double>(g_qpf.QuadPart);
}
bool InModule(uintptr_t p) {
    HMODULE m = nullptr;
    return p && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(p), &m) &&
           m;
}
HRESULT STDMETHODCALLTYPE HkPresent(IDXGISwapChain* sc, UINT sync, UINT flags);
bool InstallPresentHook() {
    if (!g_staticOk || !g_enabled) return false;
    EnterCriticalSection(&g_hookLock);
    bool ok = false;
    do {
        uintptr_t rnd = Ptr(g_base + g_ra.globalRenderer);
        uintptr_t rvt = rnd ? Ptr(rnd) : 0;
        if (!rnd || (rvt != g_base + g_ra.vtRenderer && Ptr(rvt + g_ra.vtInitSlot) != g_base + g_ra.fnRendererInit)) {
            g_log("viewmodel: renderer not found (global=%p vt=%p)", (void*)rnd, (void*)rvt);
            break;
        }
        uintptr_t sc = Ptr(rnd + g_ra.oSwapchain);
        uintptr_t vt = sc ? Ptr(sc) : 0;
        if (!sc || !vt) {
            g_log("viewmodel: renderer %p has no swapchain yet", (void*)rnd);
            break;
        }
        if (g_scVtCopy && vt == reinterpret_cast<uintptr_t>(g_scVtCopy)) {
            ok = true;
            break;
        }
        uintptr_t presentFn = Ptr(vt + 8 * 8);
        if (!InModule(vt) || !InModule(presentFn)) {
            g_log("viewmodel: swapchain %p vtable %p / Present %p not inside a module; not hooking", (void*)sc,
                  (void*)vt, (void*)presentFn);
            break;
        }
        IDXGISwapChain* q = nullptr;
        HRESULT hr = reinterpret_cast<IUnknown*>(sc)->QueryInterface(IID_IDXGISwapChain, reinterpret_cast<void**>(&q));
        if (FAILED(hr) || !q) {
            g_log("viewmodel: object %p is not an IDXGISwapChain (hr=0x%08lx)", (void*)sc, hr);
            break;
        }
        DXGI_SWAP_CHAIN_DESC d = {};
        q->GetDesc(&d);
        q->Release();
        if (!g_scVtCopy || g_scVtOrig != vt) {
            auto* copy = static_cast<uintptr_t*>(
                VirtualAlloc(nullptr, (kScSlots + 2) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!copy) break;
            copy[0] = Ptr(vt - 16);
            copy[1] = Ptr(vt - 8);
            size_t n = 0;
            for (; n < kScSlots; ++n) {
                uintptr_t v = 0;
                if (!Rd(vt + 8 * n, v)) break;
                copy[2 + n] = v;
            }
            if (n < 23) {
                VirtualFree(copy, 0, MEM_RELEASE);
                g_log("viewmodel: swapchain vtable %p unreadable after %zu slots", (void*)vt, n);
                break;
            }
            g_origPresent = reinterpret_cast<FnPresent>(copy[2 + 8]);
            copy[2 + 8] = reinterpret_cast<uintptr_t>(&HkPresent);
            DWORD old;
            VirtualProtect(copy, (kScSlots + 2) * sizeof(uintptr_t), PAGE_READONLY, &old);
            g_scVtCopy = copy + 2;
            g_scVtOrig = vt;
        }
        uintptr_t ours = reinterpret_cast<uintptr_t>(g_scVtCopy);
        SIZE_T w = 0;
        if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(sc), &ours, sizeof ours, &w) ||
            w != sizeof ours) {
            g_log("viewmodel: vptr write failed on swapchain %p", (void*)sc);
            break;
        }
        g_log("viewmodel: hooked swapchain %p (vt %p -> copy %p, Present %p) %ux%u fmt=%d buffers=%u swap=%d flags=0x%x "
              "window=%p",
              (void*)sc, (void*)vt, (void*)ours, (void*)g_origPresent, d.BufferDesc.Width, d.BufferDesc.Height,
              d.BufferDesc.Format, d.BufferCount, d.SwapEffect, d.Flags, d.OutputWindow);
        ok = true;
    } while (false);
    LeaveCriticalSection(&g_hookLock);
    return ok;
}
DXGI_FORMAT RtvFormat(DXGI_FORMAT f, bool* hwEncodes) {
    *hwEncodes = false;
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: *hwEncodes = true; return f;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: *hwEncodes = true; return f;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        *hwEncodes = true;
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
volatile LONG g_adsTimeUs[3] = {250000, 250000, 250000}, g_curAdsUs[3] = {0, 0, 0};
volatile LONG g_curveFracMicro = -1, g_curveFovMilli = 0, g_curveFovScaling = 1;
volatile LONG g_spreadMicro = 0;
unsigned g_spreadRng = 0x2545f491u;
volatile LONG64 g_hudCover = 0;
ID3D11Texture2D* g_hudTex = nullptr;
UINT g_hudTexW = 0, g_hudTexH = 0;
DXGI_FORMAT g_hudTexFmt = DXGI_FORMAT_UNKNOWN;
D3D11_BOX g_hudBox = {};
bool g_hudSaved = false;
void HudCoverSave(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* bb, const D3D11_TEXTURE2D_DESC& td) {
    g_hudSaved = false;
    const LONG64 v = g_hudCover;
    if (!v || !g_fp) return;
    auto px = [](LONG64 q, UINT size) { return static_cast<UINT>((static_cast<double>(q & 0xffff) / 65535.0) * size + 0.5); };
    UINT x0 = px(v, td.Width), y0 = px(v >> 16, td.Height), x1 = px(v >> 32, td.Width), y1 = px(v >> 48, td.Height);
    if (x1 > td.Width) x1 = td.Width;
    if (y1 > td.Height) y1 = td.Height;
    if (x1 <= x0 + 1 || y1 <= y0 + 1) return;
    const UINT w = x1 - x0, h = y1 - y0;
    if (!g_hudTex || g_hudTexW != w || g_hudTexH != h || g_hudTexFmt != td.Format) {
        if (g_hudTex) g_hudTex->Release();
        g_hudTex = nullptr;
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w;
        d.Height = h;
        d.MipLevels = d.ArraySize = 1;
        d.Format = td.Format;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &g_hudTex)) || !g_hudTex) {
            g_hudTex = nullptr;
            static bool told = false;
            if (!told && g_log) g_log("viewmodel: hud cover texture %ux%u format %d failed - HUD stays under the gun", w, h, td.Format);
            told = true;
            return;
        }
        g_hudTexW = w;
        g_hudTexH = h;
        g_hudTexFmt = td.Format;
    }
    g_hudBox = {x0, y0, 0, x1, y1, 1};
    ctx->CopySubresourceRegion(g_hudTex, 0, 0, 0, 0, bb, 0, &g_hudBox);
    g_hudSaved = true;
}
void HudCoverRestore(ID3D11DeviceContext* ctx, ID3D11Texture2D* bb) {
    if (!g_hudSaved || !g_hudTex) return;
    ctx->CopySubresourceRegion(bb, 0, g_hudBox.left, g_hudBox.top, 0, g_hudTex, 0, nullptr);
    g_hudSaved = false;
}
bool CurveAdsOn() {
    const float* t = Wp().set.adsCurveTime;
    return t[0] > 0.f && t[1] > 0.f && t[2] > 0.f && t[3] > 0.f;
}
void CurveAdsStep(float& f, float& v, bool want, float dt) {
    const float* t = Wp().set.adsCurveTime;
    float vin = 1000.f / t[0], vout = 1000.f / t[1], sum = vin + vout;
    float target = want ? vin : -vout;
    if (target > v) v = std::fmin(v + sum * 1000.f / t[3] * dt, target);
    else v = std::fmax(v - sum * 1000.f / t[2] * dt, target);
    f += v * dt;
    if (f >= 1.f) f = 1.f, v = v > 0.f ? 0.f : v;
    if (f <= 0.f) f = 0.f, v = v < 0.f ? 0.f : v;
}
float CurveFullTime(bool in) {
    const float* t = Wp().set.adsCurveTime;
    float vin = 1000.f / t[0], vout = 1000.f / t[1], sum = vin + vout;
    float v = in ? vin : vout, a = sum * 1000.f / (in ? t[3] : t[2]);
    float ta = v / a, d = 0.5f * a * ta * ta;
    return d >= 1.f ? std::sqrt(2.f / a) : ta + (1.f - d) / v;
}
float AdsTimeClassic(int k) {
    const vm::Settings& s = Wp().set;
    float w = k == 2 ? s.adsTimeTac : k == 1 ? s.adsTimeSprint : s.adsTime;
    if (w > 0.f) return w;
    LONG us = g_adsTimeUs[k];
    return us > 0 ? us * 1e-6f : 0.25f;
}
float AdsTime(int kind = 0) {
    const vm::Settings& s = Wp().set;
    if (CurveAdsOn()) {
        float full = CurveFullTime(true), want = (kind == 1 || kind == 2) ? AdsTimeClassic(kind) : 0.f;
        return (want > full ? want : full) * (1.f + (s.shieldAdsTime - 1.f) * ShieldEase());
    }
    auto pick = [&](int k) {
        float w = k == 2 ? s.adsTimeTac : k == 1 ? s.adsTimeSprint : s.adsTime;
        if (w > 0.f) return w;
        LONG us = g_adsTimeUs[k];
        return us > 0 ? us * 1e-6f : 0.25f;
    };
    float sk = 1.f + (s.shieldAdsTime - 1.f) * ShieldEase();
    float base = pick(0) * sk;
    if (kind != 1 && kind != 2) return base;
    float t = pick(kind) * sk;
    return t > base ? t : base;
}
int StanceAdsKind(LONG st) { return (st & 1) ? static_cast<int>((st >> 3) & 3) : 0; }
float AdsSmoothStep(float adsW, LONG st, float dt) {
    float target = (st & 1) ? 1.f : 0.f;
    float t = AdsTime(target > adsW ? StanceAdsKind(st) : 0);
    return adsW + (target - adsW) * (1.f - std::exp(-dt * 3.f / t));
}
struct Motion {
    bool init = false, haveView = false;
    double tPrev = 0;
    float lastYaw = 0, lastPitch = 0, swayX = 0, swayY = 0, bobPhase = 0, bobAmount = 0;
    float adsW = 0, sprintW = 0;
    float curveV = 0;
    float blendT = 0;
    float barrelAngle = 0, barrelSpeed = 0;
    int barrelModel = -1;
    float crossKick = 0, crossMove = 0;
    double crossLastShot = -1;
} g_mo;
float AdsWeightStep(float adsW, LONG st, float dt, bool commit) {
    if (!CurveAdsOn()) {
        if (commit) g_mo.curveV = 0.f;
        return AdsSmoothStep(adsW, st, dt);
    }
    float v = g_mo.curveV, sk = 1.f + (Wp().set.shieldAdsTime - 1.f) * ShieldEase();
    {
        int kind = StanceAdsKind(st);
        float want = (kind == 1 || kind == 2) ? AdsTimeClassic(kind) : 0.f, full = CurveFullTime(true);
        if (want > full && full > 0.f) sk *= want / full;
    }
    CurveAdsStep(adsW, v, (st & 1) != 0, dt / sk);
    if (commit) g_mo.curveV = v;
    return adsW;
}
void CrossLook(float& spread, float& alpha) {
    const vm::CrosshairSettings& cs = g_crossSet;
    spread = std::fmin(g_mo.crossKick + g_mo.crossMove, cs.spreadMax);
    alpha = 1.f;
    if (cs.fadeEnd > cs.fadeStart) {
        float t = (g_mo.crossKick - cs.fadeStart) / (cs.fadeEnd - cs.fadeStart);
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        alpha = 1.f - t * t * (3.f - 2.f * t);
    }
}
bool CrossFrame(vm::CrosshairSettings& cs, float& spread, float& alpha) {
    const vm::Settings& s = Wp().set;
    cs = g_crossSet;
    CrossLook(spread, alpha);
    if (s.crossAds) {
        float a = std::fmin(std::fmax(g_mo.adsW, 0.f), 1.f);
        if (!s.crossHip) alpha *= a;
        spread *= 1.f - a;
    } else if (!s.crossHip || (g_stance & 1)) {
        return false;
    }
    if (alpha <= 0.02f) return false;
    if (s.crossScale != 1.f) {
        cs.length *= s.crossScale;
        cs.gap *= s.crossScale;
        cs.dotSize *= s.crossScale;
        spread *= s.crossScale;
    }
    return true;
}
vm::Look ComputeLook() {
    double now = Now();
    if (!g_mo.init) {
        g_mo.init = true;
        g_mo.tPrev = now;
    }
    float dt = static_cast<float>(now - g_mo.tPrev);
    g_mo.tPrev = now;
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    float yaw = 0, pitch = 0, speed = 0;
    for (int tries = 0; tries < 8; ++tries) {
        LONG s1 = g_pub.seq;
        if (s1 & 1) continue;
        MemoryBarrier();
        yaw = g_pub.yaw;
        pitch = g_pub.pitch;
        speed = g_pub.speed;
        MemoryBarrier();
        if (g_pub.seq == s1) break;
    }
    if (g_fp) {
        if (g_mo.haveView) {
            float dy = yaw - g_mo.lastYaw;
            if (dy > 3.14159265f) dy -= 6.2831853f;
            if (dy < -3.14159265f) dy += 6.2831853f;
            float dp = pitch - g_mo.lastPitch;
            g_mo.swayX += -dy * 0.35f;
            g_mo.swayY += dp * 0.35f;
        }
        g_mo.lastYaw = yaw;
        g_mo.lastPitch = pitch;
        g_mo.haveView = true;
    } else {
        g_mo.haveView = false;
        speed = 0;
    }
    const float lim = 0.09f;
    if (g_mo.swayX > lim) g_mo.swayX = lim;
    if (g_mo.swayX < -lim) g_mo.swayX = -lim;
    if (g_mo.swayY > lim) g_mo.swayY = lim;
    if (g_mo.swayY < -lim) g_mo.swayY = -lim;
    float decay = std::exp(-dt * 9.0f);
    g_mo.swayX *= decay;
    g_mo.swayY *= decay;
    float target = speed / 4.5f;
    if (target > 1) target = 1;
    g_mo.bobAmount += (target - g_mo.bobAmount) * (1 - std::exp(-dt * 6.0f));
    g_mo.bobPhase += dt * (2.0f + speed * 1.6f) * Wp().set.bobRate * (g_mo.bobAmount > 0.02f ? 1.f : 0.f);
    if (g_mo.bobPhase > 6283.f) g_mo.bobPhase = std::fmod(g_mo.bobPhase, 6.2831853f);
    {
        if (g_mo.crossLastShot < 0 || now - g_mo.crossLastShot >= g_crossSet.spreadHold)
            g_mo.crossKick *= std::exp(-dt * g_crossSet.spreadRecover);
        float mt = g_crossSet.spreadMove * std::fmin(speed / 6.f, 1.5f);
        g_mo.crossMove += (mt - g_mo.crossMove) * (1 - std::exp(-dt * 8.0f));
    }
    {
        const vm::Settings& s = Wp().set;
        float k = g_crossSet.spreadMax > 0.f ? std::fmin((g_mo.crossKick + g_mo.crossMove) / g_crossSet.spreadMax, 1.f) : 0.f;
        float deg = (s.hipSpreadMin + (s.hipSpreadMax - s.hipSpreadMin) * k) * (1.f - std::fmin(std::fmax(g_mo.adsW, 0.f), 1.f));
        InterlockedExchange(&g_spreadMicro, (!g_fp || MeleeWeapon()) ? 0 : static_cast<LONG>(deg * 17453.2925f + 0.5f));
    }
    vm::Look l;
    AnimStep(l, now);
    StepSounds(now, speed);
    {
        float a[16][3], b[16][3];
        AcquireSRWLockExclusive(&g_trq.lock);
        int n = g_trq.n;
        memcpy(a, g_trq.a, sizeof(float) * 3 * n);
        memcpy(b, g_trq.b, sizeof(float) * 3 * n);
        g_trq.n = 0;
        ReleaseSRWLockExclusive(&g_trq.lock);
        if (g_fp)
            for (int i = 0; i < n; ++i) Wp().flash.AddTracer(a[i], b[i]);
    }
    {
        float p[16][3], d[16][3];
        bool kl[16];
        AcquireSRWLockExclusive(&g_hitq.lock);
        int n = g_hitq.n;
        memcpy(p, g_hitq.p, sizeof(float) * 3 * n);
        memcpy(d, g_hitq.d, sizeof(float) * 3 * n);
        memcpy(kl, g_hitq.kill, sizeof(bool) * n);
        g_hitq.n = 0;
        ReleaseSRWLockExclusive(&g_hitq.lock);
        if (g_fp && n > 0) {
            bool anyKill = false;
            for (int i = 0; i < n; ++i) {
                if (Wp().set.hitFx) Wp().flash.AddImpact(p[i], d[i], kl[i]);
                anyKill = anyKill || kl[i];
            }
            ++g_hitFxCount;
            if (anyKill) ++g_hitFxKills;
        }
    }
    Wp().flash.BeginFrame(now, Wp().set);
    l.flashLight = g_fp ? Wp().flash.LightLevel(now) : 0.f;
    l.time = static_cast<float>(now);
    {
        LONG st = g_fp ? g_stance : 0;
        float k = 1 - std::exp(-dt * 12.0f);
        g_mo.adsW = AdsWeightStep(g_mo.adsW, st, dt, true);
        for (int k = 0; k < 3; ++k) InterlockedExchange(&g_curAdsUs[k], static_cast<LONG>(AdsTime(k) * 1e6f + 0.5f));
        const bool curve = CurveAdsOn();
        InterlockedExchange(&g_curveFracMicro, curve ? static_cast<LONG>(g_mo.adsW * 1e6f + 0.5f) : -1);
        InterlockedExchange(&g_curveFovMilli, curve ? static_cast<LONG>(Wp().set.adsFovAlt * 1000.f + 0.5f) : 0);
        InterlockedExchange(&g_curveFovScaling, Wp().set.adsFovScaling ? 1 : 0);
        if (curve && Wp().set.adsVmFovAlt > 0.f) {
            const float kD = 0.0174532925f;
            float sc = 0.5f - 0.5f * std::cos(3.14159265f * g_mo.adsW);
            float th = std::tan(Wp().set.fovDeg * 0.5f * kD), avm = Wp().set.adsVmFovAlt;
            if (Wp().set.adsFovScaling) avm = std::fmin(avm * 2.f * std::atan(th / 0.75f) / kD / 65.f, 170.f);
            float ta = 0.75f * std::tan(avm * 0.5f * kD);
            l.fovDeg = 2.f * std::atan(th + (ta - th) * sc) / kD;
        }
        g_mo.sprintW += (((st & 6) ? 1.f : 0.f) - g_mo.sprintW) * k;
    }
    float swayK = 1.f - 0.7f * g_mo.adsW, bobK = (1.f - 0.85f * g_mo.adsW) * (1.f - g_mo.sprintW);
    if (MeleeWeapon() && Wp().rend.FindClip("run") >= 0) bobK = 0.f;
    l.swayX = g_mo.swayX * swayK;
    l.swayY = g_mo.swayY * swayK;
    l.bobPhase = g_mo.bobPhase;
    l.bobAmount = g_mo.bobAmount * bobK;
    l.adsAlign = g_mo.adsW;
    ShieldStep(l, now, dt);
    {
        const vm::Settings& s = Wp().set;
        int bone = (s.fireMode != 1 || s.spinBone == -1) ? -1 : (s.spinBone >= 0 ? s.spinBone : Wp().rend.BarrelBone());
        if (g_mo.barrelModel != g_modelIdx || !g_fp) {
            g_mo.barrelModel = g_modelIdx;
            g_mo.barrelAngle = g_mo.barrelSpeed = 0;
        }
        if (bone >= 0) {
            const float kTwoPi = 6.2831853f;
            float pitch = kTwoPi / s.spinBarrels;
            float full = (s.fireRpm > 0 ? s.fireRpm : 1200.f) / 60.f / s.spinBarrels * kTwoPi;
            float a = g_mo.barrelAngle, w = g_mo.barrelSpeed, maxStep = 0.4f * pitch;
            LONG st = g_fp ? g_spin.state : 0;
            if (st == 1 || st == 2) {
                w += full / (s.spinUpTime > 0.05f ? s.spinUpTime : 0.05f) * dt;
                if (w > full) w = full;
                a += w * dt < maxStep ? w * dt : maxStep;
            } else if (w > 0.f || std::fabs(a - pitch * std::round(a / pitch)) > 1e-4f) {
                w -= full / s.spinDownTime * dt;
                if (w < pitch) w = pitch;
                float step = w * dt < maxStep ? w * dt : maxStep;
                float next = (std::floor(a / pitch + 1e-4f) + 1.f) * pitch;
                if (w <= pitch && a + step >= next) {
                    a = next;
                    w = 0.f;
                } else {
                    a += step;
                }
            }
            if (a >= kTwoPi) a -= kTwoPi * std::floor(a / kTwoPi);
            g_mo.barrelAngle = a;
            g_mo.barrelSpeed = w;
            l.spinBone = bone;
            l.spinAngle = Wp().rend.BarrelDir() * a;
        }
    }
    {
        kick::Params kp;
        AcquireSRWLockShared(&g_kickLock);
        kp = g_kickP[g_modelIdx];
        ReleaseSRWLockShared(&g_kickLock);
        OneHandKick(kp, ShieldEase(), Wp().set.shieldKick);
        {
            float d = dt / kp.blendTime;
            g_mo.blendT += ((g_fp ? g_stance : 0) & 1) ? d : -d;
            g_mo.blendT = g_mo.blendT < 0.f ? 0.f : g_mo.blendT > 1.f ? 1.f : g_mo.blendT;
        }
        float bw = kp.blendMode ? kick::SCurve(g_mo.blendT) : g_mo.adsW;
        LONG n = InterlockedExchange(&g_kickShots, 0);
        if (n > 0) g_sustainLastShot = now;
        double gap = FireInterval() > 0 ? FireInterval() : 0.1;
        bool firing = g_fp && g_sustainLastShot > 0 && now - g_sustainLastShot <= (gap * 1.5 > 0.08 ? gap * 1.5 : 0.08);
        if (!g_fp) {
            g_gunKick.Reset();
            g_sustain.Reset();
            g_onset.Reset();
            g_jitter.Reset();
        } else {
            for (LONG i = 0; i < n && i < 8; ++i) {
                g_gunKick.OnShot(kp, g_mo.adsW);
                g_jitter.OnShot(kp, g_mo.adsW);
            }
            g_gunKick.Step(kp, dt);
            g_sustain.Step(kp, firing, dt);
            g_onset.Step(kp, firing, dt);
            g_jitter.Step(kp, dt);
        }
        KickOut& o = g_kickOut;
        g_gunKick.Out(kp, bw, o.gunPos, o.gunRot);
        g_sustain.Out(kp, bw, o.susPos, o.susRot);
        g_onset.Out(kp, bw, o.onPos, o.onRot);
        o.w = bw;
        o.mode = kp.blendMode;
        l.kickPos[0] = o.gunPos[0] + o.susPos[0] + o.onPos[0];
        l.kickPos[1] = o.gunPos[1] + o.susPos[1] + o.onPos[1];
        l.kickPos[2] = -(o.gunPos[2] + o.susPos[2] + o.onPos[2]);
        for (int i = 0; i < 3; ++i) {
            l.kickRot[i] = (o.gunRot[i] + o.susRot[i] + o.onRot[i] + g_jitter.x[i]) * kick::kDeg;
            l.kickPivot[i] = kp.gunPivot[i];
        }
    }
    {
        static const float kJlSpring[2] = {6.f, 0.45f};
        float jump = InterlockedExchange(&g_jlJumpMilli, 0) * 1e-3f, land = InterlockedExchange(&g_jlLandMilli, 0) * 1e-3f;
        if (!g_fp) {
            g_jlX[0] = g_jlX[1] = g_jlV[0] = g_jlV[1] = 0.f;
        } else {
            float k = 1.f - 0.7f * g_mo.adsW;
            if (jump > 0.f) {
                g_jlV[0] += kick::Impulse(-0.010f * k * jump, kJlSpring[0], kJlSpring[1]);
                g_jlV[1] += kick::Impulse(-0.6f * k * jump, kJlSpring[0], kJlSpring[1]);
                Wp().stepFoot.Play(Wp().set.stepFootVolume * 0.6f);
            }
            if (land > 0.f) {
                g_jlV[0] += kick::Impulse(-(0.012f + 0.030f * land) * k, kJlSpring[0], kJlSpring[1]);
                g_jlV[1] += kick::Impulse(-(0.8f + 3.0f * land) * k, kJlSpring[0], kJlSpring[1]);
                Wp().stepFoot.Play(Wp().set.stepFootVolume * (0.8f + 0.7f * land));
            }
            kick::StepSprings(g_jlX, g_jlV, 2, kJlSpring, dt);
            l.kickPos[1] += g_jlX[0];
            l.kickRot[0] += g_jlX[1] * kick::kDeg;
        }
    }
    return l;
}
struct Anim {
    int cur = -1, prev = -1, idle = -1, fireNext = 0, cycleNext = 0, stance = 0;
    int reloadKind = 0;
    double reloadAt = 0;
    int spinSeen = 0;
    int evClip = -1;
    double evStart = 0, evLast = -1;
    double stepK = -1;
    float stepFrac = -1;
    int stepClip = -1;
    double stepStart = -1;
    double stepNext = -1;
    double curStart = 0, prevStart = 0, fadeStart = 0, fadeLen = 0.15;
    double curRate = 1, prevRate = 1;
    double sprintExitEnd = 0;
    bool settled = true;
    int curveClip = -1;
    bool curveDown = false;
} g_an;
struct FxStat {
    int lastFired = 0, lastShots = 0, lastClips = 0, casings = 0, casingAds = 0, lands = 0, landsDropped = 0, settles = 0;
    float landAgeMin = 1e9f, landAgeMax = 0.f;
} g_fxStat;
double ClipT(double now) {
    if (g_an.curveClip >= 0 && g_an.cur == g_an.curveClip) {
        double d = Wp().rend.ClipDuration(g_an.cur);
        return (g_an.curveDown ? 1.0 - g_mo.adsW : g_mo.adsW) * d;
    }
    return (now - g_an.curStart) * g_an.curRate;
}
void Play(int clip, double now, double fade, double rate = 1.0) {
    if (clip < 0) return;
    const bool fracPrev = g_an.curveClip >= 0 && g_an.cur == g_an.curveClip;
    const double tPrev = fracPrev ? ClipT(now) : 0.0;
    g_an.prev = g_an.cur;
    g_an.prevStart = g_an.curStart;
    g_an.prevRate = g_an.curRate;
    if (fracPrev && g_an.prevRate > 0) g_an.prevStart = now - tPrev / g_an.prevRate;
    g_an.cur = clip;
    g_an.curStart = now;
    g_an.curRate = rate;
    g_an.fadeStart = now;
    g_an.fadeLen = g_an.prev >= 0 ? fade : 0;
    g_an.curveClip = -1;
}
void MarkCurveClip(int clip, bool down) {
    if (CurveAdsOn() && clip >= 0 && g_an.cur == clip) g_an.curveClip = clip, g_an.curveDown = down;
}
double AdsRate(int clip, int kind = 0) {
    float d = clip >= 0 ? Wp().rend.ClipDuration(clip) : 0.f;
    if (CurveAdsOn()) return d > 0.f ? d / CurveFullTime(clip != Wp().rend.FindClip("ads_down")) : 1.0;
    return d > 0.f ? d / AdsTime(kind) : 1.0;
}
void AnimReset(double now) {
    g_an = Anim();
    g_an.idle = Wp().rend.FindClip("idle");
    int draw = Wp().rend.FindClip("draw");
    Play(draw >= 0 ? draw : g_an.idle, now, 0);
}
int BoltCycleStance(double now) {
    const float* sp = Wp().set.fireStanceSync;
    if (!(sp[0] > 0.f && sp[1] > 0.f) || g_an.cur < 0 || g_an.curveClip >= 0) return -1;
    int hip = Wp().rend.FindClip("fire1"), ads = Wp().rend.FindClip("ads_fire");
    int k = hip < 0 || ads < 0 ? -1 : g_an.cur == hip ? 0 : g_an.cur == ads ? 1 : -1;
    if (k < 0 || ClipT(now) >= Wp().rend.ClipDuration(g_an.cur) - 0.05) return -1;
    return k;
}
int StanceIdle() {
    int c = g_an.stance == 1 ? Wp().rend.FindClip("ads_idle") : g_an.stance == 2 ? Wp().rend.FindClip("sprint") : -1;
    if (g_an.stance == 3) {
        c = Wp().rend.FindClip("tac_sprint");
        if (c < 0) c = Wp().rend.FindClip("sprint");
    }
    return c >= 0 ? c : g_an.idle;
}
int FindLastClip() {
    int c = g_an.stance == 1 ? Wp().rend.FindClip("ads_fire_last") : -1;
    return c >= 0 ? c : (g_an.stance == 1 ? -1 : Wp().rend.FindClip("fire_last"));
}
#include "melee.inc"
#include "ui_layer.inc"
#include "launcher.inc"
#include "ladder.inc"
void AnimStep(vm::Look& l, double now) {
    bool fired = InterlockedExchange(&g_reqFire, 0) != 0;
    const bool lastFired = fired && InterlockedExchange(&g_reqFireLast, 0) != 0;
    if (fired) {
        bool ads = (g_stance & 1) != 0;
        const vm::Settings& fs = Wp().set;
        if (lastFired && Wp().lastShot.Loaded()) {
            Wp().lastShot.Play(fs.fireLastVolume < 0.f ? fs.fireVolume : fs.fireLastVolume);
            ++g_fxStat.lastShots;
        } else if (ads && Wp().gunAds.Loaded()) {
            Wp().gunAds.Play(fs.fireVolume);
        } else {
            Wp().gun.Play(fs.fireVolume);
        }
        if (ads && Wp().casingAds.Loaded()) {
            Wp().casingAds.Play(fs.fireCasingVolume);
            ++g_fxStat.casingAds;
        } else if (Wp().casing.Loaded()) {
            Wp().casing.Play(fs.fireCasingVolume);
            ++g_fxStat.casings;
        }
        if (lastFired) ++g_fxStat.lastFired;
        if (ads && Wp().mechAds.Loaded())
            Wp().mechAds.Play(Wp().set.fireMechVolume);
        else
            Wp().mech.Play(Wp().set.fireMechVolume);
        Wp().lfe.Play(Wp().set.fireLfeVolume);
        Wp().atmo.Play(Wp().set.fireAtmoVolume);
        Wp().lastShotAt = now;
        Wp().tailPending = Wp().tail.Loaded();
        Wp().flash.Trigger(g_mo.adsW);
        if (!Wp().set.shellEjectAnim) Wp().shells.Trigger(g_mo.adsW);
        g_mo.crossKick = std::fmin(g_mo.crossKick + g_crossSet.spreadShot, g_crossSet.spreadMax);
        g_mo.crossLastShot = now;
    }
    if (Wp().tailPending && now - Wp().lastShotAt >= Wp().set.fireTailGap) {
        Wp().tailPending = false;
        Wp().tail.Play(Wp().set.fireTailVolume);
    }
    for (int i = 0; i < g_modelCount; ++i) {
        Weapon& w = g_w[i];
        vm::GunSound* all[18] = {&w.gun, &w.gunAds, &w.mech, &w.mechAds, &w.lfe, &w.atmo, &w.tail,
                                 &w.stepFoot, &w.stepRun, &w.stepSprint, &w.stepRattle, &w.spinUp, &w.spinLoop, &w.spinDown,
                                 &w.lastShot, &w.casing, &w.casingAds, &w.shellLand};
        for (vm::GunSound* gs : all) gs->Update();
        for (int k = 0; k < w.nAnim; ++k) w.anim[k].snd.Update();
        for (vm::GunSound& m : w.meleeSfx) m.Update();
    }
    InterlockedExchange(&g_meleeOn, MeleeWeapon() ? 1 : 0);
    if (!Wp().rend.Clips()) return;
    if (MeleeWeapon()) {
        MeleeStep(l, now);
        return;
    }
    {
        LONG raw = g_fp ? g_stance : 0;
        int st = (raw & 4) ? 3 : (raw & 2) ? 2 : (raw & 1) ? 1 : 0;
        bool reloadAnim = g_mag.reload != 0 && g_an.cur >= 0 &&
                          (g_an.cur == Wp().rend.FindClip("reload") || g_an.cur == Wp().rend.FindClip("reload_empty"));
        if (st != g_an.stance && Wp().set.fireMode && g_spin.state != 0 && st < 2 && g_an.stance < 2) {
            g_an.stance = st;
            if (g_spin.state == 2) {
                int c = st == 1 ? Wp().rend.FindClip("ads_fire_loop") : -1;
                Play(c >= 0 ? c : Wp().rend.FindClip("fire_loop"), now, 0.12);
            }
        } else if (st != g_an.stance && BoltCycleStance(now) >= 0) {
            if (st < 2 && g_an.stance < 2) {
                const int k = BoltCycleStance(now);
                const float* sp = Wp().set.fireStanceSync;
                const double t = ClipT(now);
                double tTo = t < sp[k] ? t * sp[1 - k] / sp[k] : t - sp[k] + sp[1 - k];
                const int c = Wp().rend.FindClip(st == 1 ? "ads_fire" : "fire1");
                const double dTo = Wp().rend.ClipDuration(c);
                if (tTo > dTo - 0.02) tTo = dTo - 0.02;
                double fade = AdsTime(StanceAdsKind(raw));
                if (!(fade > 0.05 && fade < 0.6)) fade = 0.2;
                g_an.stance = st;
                Play(c, now, fade);
                g_an.curStart = now - tTo;
                g_an.evClip = g_an.cur;
                g_an.evStart = g_an.curStart;
                g_an.evLast = tTo;
            }
        } else if (st != g_an.stance && reloadAnim) {
            g_an.stance = st;
        } else if (st != g_an.stance && g_sw.phase != 0) {
            g_an.stance = st;
        } else if (st != g_an.stance) {
            int from = g_an.stance;
            g_an.stance = st;
            if (st == 1) {
                int c = Wp().rend.FindClip("ads_up");
                if (from == 0 && c >= 0 && now < g_an.sprintExitEnd && g_an.cur == g_an.idle && g_an.prev >= 0) {
                    g_an.cur = c;
                    g_an.curStart = now;
                    g_an.curRate = AdsRate(c, StanceAdsKind(raw));
                    MarkCurveClip(c, false);
                } else {
                    double fade = from == 3 ? 0.3 : from == 2 ? 0.2 : 0.06;
                    Play(c >= 0 ? c : StanceIdle(), now, fade, AdsRate(c, StanceAdsKind(raw)));
                    MarkCurveClip(c, false);
                    if (from >= 2) g_an.sprintExitEnd = now + fade;
                }
            } else if (st >= 2) {
                Play(StanceIdle(), now, 0.25);
            } else if (from == 1) {
                int c = Wp().rend.FindClip("ads_down");
                Play(c >= 0 ? c : g_an.idle, now, 0.06, AdsRate(c));
                MarkCurveClip(c, true);
            } else {
                double fade = from == 3 ? 0.3 : 0.2;
                Play(g_an.idle, now, fade);
                if (from >= 2) g_an.sprintExitEnd = now + fade;
            }
            if (g_an.reloadKind && g_an.reloadAt < g_an.sprintExitEnd) g_an.reloadAt = g_an.sprintExitEnd;
        }
    }
    if (InterlockedExchange(&g_reqDraw, 0)) Play(Wp().rend.FindClip("draw") >= 0 ? Wp().rend.FindClip("draw") : g_an.idle, now, 0.1);
    if (fired && !Wp().set.fireMode) {
        static const char* fires[3] = {"fire1", "fire2", "fire3"};
        int c = lastFired ? FindLastClip() : -1;
        if (c >= 0) ++g_fxStat.lastClips;
        if (c < 0) c = g_an.stance == 1 ? Wp().rend.FindClip("ads_fire") : -1;
        for (int k = 0; k < 3 && c < 0; ++k) c = Wp().rend.FindClip(fires[(g_an.fireNext + k) % 3]);
        g_an.fireNext = (g_an.fireNext + 1) % 3;
        Play(c, now, 0.04);
        g_an.settled = false;
    }
    if (Wp().set.fireSettle && !Wp().set.fireMode && !g_an.settled && g_an.stance == 0 && !g_an.reloadKind && g_mag.reload == 0 &&
        now - Wp().lastShotAt > FireInterval() + 0.03) {
        g_an.settled = true;
        bool hipFire = false;
        for (const char* n : {"fire1", "fire2", "fire3"}) hipFire = hipFire || (g_an.cur >= 0 && g_an.cur == Wp().rend.FindClip(n));
        int c = Wp().rend.FindClip("fire_settle");
        if (hipFire && c >= 0 && ClipT(now) < 0.15) {
            Play(c, now, 0.06);
            ++g_fxStat.settles;
        }
    }
    if (Wp().set.fireMode) {
        Weapon& w = Wp();
        int st = static_cast<int>(g_spin.state);
        if (st != g_an.spinSeen) {
            bool ads = g_an.stance == 1;
            auto clip = [&](const char* hip, const char* adsName) {
                int c = ads ? w.rend.FindClip(adsName) : -1;
                return c >= 0 ? c : w.rend.FindClip(hip);
            };
            bool sprinting = g_an.stance >= 2;
            if (st == 1) {
                w.spinDown.FadeAll();
                if (!sprinting) Play(clip("spin_up", "ads_spin_up"), now, 0.08);
                w.spinUp.Play(w.set.spinVolume);
            } else if (st == 2) {
                w.spinUp.FadeAll();
                w.spinDown.FadeAll();
                Play(clip("fire_loop", "ads_fire_loop"), now, g_an.spinSeen == 3 ? 0.15 : 0.08);
                w.spinLoop.FadeAll();
                w.spinLoop.Play(w.set.spinVolume, true);
            } else if (st == 3) {
                w.spinUp.FadeAll();
                w.spinLoop.FadeAll();
                if (!sprinting) Play(clip("spin_down", "ads_spin_down"), now, 0.1);
                w.spinDown.Play(w.set.spinVolume);
            }
            g_an.spinSeen = st;
        }
    }
    if (LONG ra = InterlockedExchange(&g_mag.reqAnim, 0)) {
        g_an.reloadKind = static_cast<int>(ra);
        g_an.reloadAt = fired ? now + (lastFired && FindLastClip() >= 0 ? 0.2 : 0.12) : now;
        double exitEnd = g_an.stance >= 2 ? now + (g_an.stance == 3 ? 0.3 : 0.2) : g_an.sprintExitEnd;
        if (g_an.reloadAt < exitEnd) g_an.reloadAt = exitEnd;
        if (BoltCycleStance(now) >= 0) {
            double end = now + (Wp().rend.ClipDuration(g_an.cur) - ClipT(now)) / (g_an.curRate > 0 ? g_an.curRate : 1.0);
            if (g_an.reloadAt < end) g_an.reloadAt = end;
        }
        if (g_sh.w > 0.f) {
            double t = now + g_sh.w * Wp().set.shieldLowerTime;
            if (g_an.reloadAt < t) g_an.reloadAt = t;
        }
    }
    if (g_an.reloadKind && now >= g_an.reloadAt) {
        if (g_mag.reload == g_an.reloadKind) {
            int c = g_an.reloadKind == 2 ? Wp().rend.FindClip("reload_empty") : -1;
            if (c < 0) c = Wp().rend.FindClip("reload");
            Play(c, now, 0.1);
            InterlockedExchange64(&g_mag.t0Us, static_cast<LONG64>(now * 1e6));
        }
        g_an.reloadKind = 0;
    }
    if (InterlockedExchange(&g_reqAnimNext, 0)) {
        static const char* demo[5] = {"draw", "fire1", "reload", "inspect", "idle"};
        int c = -1;
        for (int k = 0; k < 5 && c < 0; ++k) {
            c = Wp().rend.FindClip(demo[g_an.cycleNext % 5]);
            g_an.cycleNext = (g_an.cycleNext + 1) % 5;
        }
        Play(c, now, 0.15);
    }
    if (g_an.cur != g_an.evClip || g_an.curStart != g_an.evStart) {
        g_an.evClip = g_an.cur;
        g_an.evStart = g_an.curStart;
        g_an.evLast = -1;
    }
    {
        double t = ClipT(now);
        Weapon& w = Wp();
        for (int i = 0; i < w.nAnim; ++i) {
            AnimSound& a = w.anim[i];
            if (a.clip == g_an.cur && a.t > g_an.evLast && a.t <= t && a.t > t - 0.2) {
                if (a.shell) {
                    if (w.set.shellEject) w.shells.Trigger(g_mo.adsW);
                } else {
                    a.snd.Play(a.vol * w.set.animSoundVolume);
                }
                static int logs = 0;
                if (logs < 80) {
                    ++logs;
                    g_log("anim %s: clip %d at %.2f s (event %.2f s)", a.shell ? "shell" : "sound", a.clip, t, a.t);
                }
            }
        }
        g_an.evLast = t;
    }
    if (g_an.cur >= 0 && !Wp().rend.ClipLoops(g_an.cur) && g_an.cur != StanceIdle() &&
        ClipT(now) >= Wp().rend.ClipDuration(g_an.cur))
        Play(StanceIdle(), now, g_an.stance == 1 ? 0.05 : 0.15);
    l.clip = g_an.cur;
    l.clipTime = static_cast<float>(ClipT(now));
    l.clipPrev = g_an.prev;
    l.clipTimePrev = static_cast<float>((now - g_an.prevStart) * g_an.prevRate);
    l.blend = g_an.fadeLen > 0 ? static_cast<float>((now - g_an.fadeStart) / g_an.fadeLen) : 1.f;
    if (l.blend >= 1.f) {
        l.blend = 1.f;
        l.clipPrev = -1;
    }
}
void ShieldPoseMatrix(const float* p, const float* rd, float* m) {
    const float d = 3.14159265f / 180.f;
    float cy = std::cos(rd[0] * d), sy = std::sin(rd[0] * d), cp = std::cos(rd[1] * d), sp = std::sin(rd[1] * d);
    float cr = std::cos(rd[2] * d), sr = std::sin(rd[2] * d);
    const float Ry[9] = {cy, 0, -sy, 0, 1, 0, sy, 0, cy}, Rx[9] = {1, 0, 0, 0, cp, sp, 0, -sp, cp}, Rz[9] = {cr, sr, 0, -sr, cr, 0, 0, 0, 1};
    float A[9], B[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) A[r * 3 + c] = Ry[r * 3] * Rx[c] + Ry[r * 3 + 1] * Rx[3 + c] + Ry[r * 3 + 2] * Rx[6 + c];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) B[r * 3 + c] = A[r * 3] * Rz[c] + A[r * 3 + 1] * Rz[3 + c] + A[r * 3 + 2] * Rz[6 + c];
    for (int r = 0; r < 3; ++r) m[r * 4] = B[r * 3], m[r * 4 + 1] = B[r * 3 + 1], m[r * 4 + 2] = B[r * 3 + 2], m[r * 4 + 3] = p[r];
}
void ShieldStep(vm::Look& l, double now, float dt) {
    const vm::Settings& s = Wp().set;
    bool allowed = g_fp && s.shieldAllow && Wp().rend.ArmRig() && !s.mirrorX;
    bool meleeBlock = g_fp && MeleeWeapon() && (melee::g.act == melee::kBlock || melee::Guarding(now));
    g_sh.meleeBlock = meleeBlock ? 1 : 0;
    auto blockNow = [&]() {
        if (meleeBlock) return s.shieldBlockAds;
        if (!allowed || g_sh.w < 0.6f) return 0.f;
        float a = g_mo.adsW;
        a = a <= 0.01f ? 0.f : (a >= 0.99f ? 1.f : a);
        return s.shieldBlockHip + (s.shieldBlockAds - s.shieldBlockHip) * a;
    };
    LONG n = InterlockedExchange(&g_sh.hitN, 0);
    if (n > 0) {
        float frac = blockNow();
        if (frac > 0.f && meleeBlock) {
            g_sh.lastBlock = frac;
            g_sh.meleeShakeAt = now;
            g_sh.rng = g_sh.rng * 1664525u + 1013904223u;
            float a = static_cast<float>((g_sh.rng >> 8) & 0xffff) / 65536.f * 6.2831853f;
            g_sh.meleeShake[0] = std::cos(a), g_sh.meleeShake[1] = std::sin(a);
            InterlockedExchangeAdd(&g_sh.camKickMdeg, static_cast<LONG>(s.shieldHitCam * 1000.f * n));
            melee::BlockSound();
            if (g_sh.hitLogs++ < 120) g_log("shield: hit x%ld blocked %.0f%% (spear block)", n, frac * 100.f);
        } else if (frac > 0.f) {
            g_sh.lastBlock = frac;
            g_sh.shakeAt = now;
            g_sh.rng = g_sh.rng * 1664525u + 1013904223u;
            float a = static_cast<float>((g_sh.rng >> 8) & 0xffff) / 65536.f * 6.2831853f;
            g_sh.shake[0] = std::cos(a), g_sh.shake[1] = std::sin(a);
            InterlockedExchangeAdd(&g_sh.camKickMdeg, static_cast<LONG>(s.shieldHitCam * 1000.f * n));
            g_sh.hitFx += n;
            melee::BlockSound();
            if (g_sh.hitLogs++ < 120) g_log("shield: hit x%ld blocked %.0f%% (ads %.2f)", n, frac * 100.f, g_mo.adsW);
        } else if (g_sh.hitLogs++ < 120) {
            g_log("shield: hit x%ld not blocked (raise %.2f, allowed %d, spear block %d)", n, g_sh.w, allowed ? 1 : 0, meleeBlock ? 1 : 0);
        }
    }
    g_sh.meleeShakeNow = 0.f;
    if (g_sh.meleeShakeAt >= 0) {
        float t = static_cast<float>(now - g_sh.meleeShakeAt);
        float amp = s.shieldHitShake * 0.017453293f * std::exp(-t / s.shieldHitShakeTime) * std::cos(t * 6.2831853f * 11.f);
        if (g_fp && MeleeWeapon()) {
            l.swayY += amp * g_sh.meleeShake[0];
            l.swayX += amp * g_sh.meleeShake[1];
            g_sh.meleeShakeNow = amp;
        }
        if (t > s.shieldHitShakeTime * 8.f) g_sh.meleeShakeAt = -1;
    }
    bool reloading = g_mag.reload != 0, switching = g_sw.phase != 0, sprinting = g_an.stance >= 2;
    bool blocked = reloading || switching || sprinting;
    float target = g_sh.want && allowed && !blocked ? 1.f : 0.f;
    if (!allowed) {
        g_sh.w = 0.f;
    } else {
        float step = dt / (target > g_sh.w ? s.shieldRaiseTime : s.shieldLowerTime);
        g_sh.w = target > g_sh.w ? (g_sh.w + step > target ? target : g_sh.w + step) : (g_sh.w - step < target ? target : g_sh.w - step);
    }
    int ph = g_sh.w <= 0.f ? 0 : g_sh.w >= 1.f ? 2 : target > 0.5f ? 1 : 3;
    if (ph != g_sh.phase) {
        static const char* names[4] = {"stowed", "raising", "up", "lowering"};
        if (g_sh.logs++ < 200)
            g_log("shield: %s -> %s at %.3f (want %ld, allowed %d, reload %d, switch %d, sprint %d)", names[g_sh.phase], names[ph], now,
                  static_cast<long>(g_sh.want), allowed ? 1 : 0, reloading ? 1 : 0, switching ? 1 : 0, sprinting ? 1 : 0);
        g_sh.phase = ph;
    }
    float e = ShieldEase();
    InterlockedExchange(&g_sh.wUs, static_cast<LONG>(e * 1e6f));
    InterlockedExchange(&g_sh.allowed, allowed ? 1 : 0);
    InterlockedExchange(&g_sh.blockMilli, static_cast<LONG>(blockNow() * 1000.f + 0.5f));
    InterlockedExchange(&g_sh.moveMilli, static_cast<LONG>(s.shieldMoveScale * 1000.f));
    InterlockedExchange(&g_sh.zoomMilli, static_cast<LONG>(s.shieldAdsZoom * 1000.f));
    for (int k = 0; k < 3; ++k) InterlockedExchange(&g_sh.kickMilli[k], static_cast<LONG>(s.shieldKick[k] * 1000.f));
    auto sm = [](float x) {
        x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
        return x * x * (3.f - 2.f * x);
    };
    float armA = sm(g_sh.w / 0.4f), lift = sm((g_sh.w - 0.25f) / 0.75f), aw = g_mo.adsW;
    float pos[3], rot[3];
    for (int k = 0; k < 3; ++k) {
        float up = s.shieldPos[k] + (s.shieldAdsPos[k] - s.shieldPos[k]) * aw, ur = s.shieldRot[k] + (s.shieldAdsRot[k] - s.shieldRot[k]) * aw;
        pos[k] = s.shieldStowPos[k] + (up - s.shieldStowPos[k]) * lift;
        rot[k] = s.shieldStowRot[k] + (ur - s.shieldStowRot[k]) * lift;
    }
    if (g_sh.shakeAt >= 0) {
        float t = static_cast<float>(now - g_sh.shakeAt);
        float amp = s.shieldHitShake * std::exp(-t / s.shieldHitShakeTime) * std::cos(t * 6.2831853f * 11.f);
        rot[1] += g_sh.shake[0] * amp;
        rot[0] += g_sh.shake[1] * amp * 0.6f;
        if (t > s.shieldHitShakeTime * 8.f) g_sh.shakeAt = -1;
    }
    ShieldPoseMatrix(pos, rot, l.shieldPose);
    l.shieldArmW = allowed ? armA : 0.f;
    l.shieldHoldW = allowed ? sm((lift - 0.8f) / 0.2f) : 0.f;
    for (int k = 0; k < 3; ++k) {
        l.shieldGrip[k] = s.shieldGrip[k];
        l.shieldWristRot[k] = s.shieldWristRot[k];
        l.gunShift[k] = allowed ? s.shieldGunShift[k] * e : 0.f;
    }
    const float* qh = s.shieldHandQuat;
    bool refHand = qh[0] != 0.f || qh[1] != 0.f || qh[2] != 0.f || qh[3] != 0.f;
    for (int k = 0; k < 4; ++k) l.shieldHandQuat[k] = refHand ? qh[k] : 0.f;
    for (int k = 0; k < 3; ++k) l.shieldElbow[k] = s.shieldElbow[k];
    l.shieldFingerOn = allowed && s.shieldFingerN == vm::Settings::kShieldFingers * 4 ? 1 : 0;
    if (l.shieldFingerOn) {
        bool adsF = s.shieldAdsFingerN == vm::Settings::kShieldFingers * 4 && aw > 0.f;
        float t = aw > 1.f ? 1.f : aw;
        for (int k = 0; k < vm::Settings::kShieldFingers; ++k) {
            const float* h = s.shieldFingerPose + k * 4;
            const float* d = s.shieldAdsFingerPose + k * 4;
            float sg = adsF && h[0] * d[0] + h[1] * d[1] + h[2] * d[2] + h[3] * d[3] < 0.f ? -1.f : 1.f, n = 0.f;
            for (int c = 0; c < 4; ++c) l.shieldFinger[k][c] = adsF ? h[c] + (d[c] * sg - h[c]) * t : h[c], n += l.shieldFinger[k][c] * l.shieldFinger[k][c];
            n = std::sqrt(n);
            for (int c = 0; c < 4; ++c) l.shieldFinger[k][c] = n > 1e-6f ? l.shieldFinger[k][c] / n : (c == 3 ? 1.f : 0.f);
        }
    }
    const float* qa = s.shieldAdsHandQuat;
    if (refHand && (qa[0] != 0.f || qa[1] != 0.f || qa[2] != 0.f || qa[3] != 0.f) && aw > 0.f) {
        float t = aw > 1.f ? 1.f : aw, d = qh[0] * qa[0] + qh[1] * qa[1] + qh[2] * qa[2] + qh[3] * qa[3], sg = d < 0.f ? -1.f : 1.f, n = 0.f;
        for (int k = 0; k < 4; ++k) l.shieldHandQuat[k] = qh[k] + (qa[k] * sg - qh[k]) * t, n += l.shieldHandQuat[k] * l.shieldHandQuat[k];
        n = std::sqrt(n);
        for (int k = 0; k < 4; ++k) l.shieldHandQuat[k] /= n;
        for (int k = 0; k < 3; ++k) {
            l.shieldGrip[k] = s.shieldGrip[k] + (s.shieldAdsGrip[k] - s.shieldGrip[k]) * t;
            l.shieldElbow[k] = s.shieldElbow[k] + (s.shieldAdsElbow[k] - s.shieldElbow[k]) * t;
        }
    }
    g_sh.lookDraw = allowed && g_sh.w > 0.001f ? 0 : -1;
}
void ShieldDraw(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, UINT w, UINT h, bool hw, const vm::WorldCam* cam) {
    const vm::Settings& s = Wp().set;
    bool fx = g_sh.hitFx > 0 || g_shieldR.LiveSparks() > 0;
    if (g_sh.lookDraw < 0 && !fx) return;
    if (!g_shieldR.BuiltFor(s)) {
        char err[256] = {};
        static int fails = 0;
        if (!g_shieldR.Load(dev, s, err, sizeof err)) {
            if (fails++ < 3) g_log("viewmodel: %s", err);
            g_sh.hitFx = 0;
            return;
        }
        if (err[0]) g_log("viewmodel: %s", err);
        if (g_shieldR.HasModel())
            g_log("viewmodel: shield model %ls (%d triangles, grip %.3f,%.3f,%.3f, metal x%.2f)", s.shieldModel, g_shieldR.ModelTriangles(),
                  s.shieldGrip[0], s.shieldGrip[1], s.shieldGrip[2], s.shieldModelMetal);
        else
            g_log("viewmodel: shield mesh built (%.2f x %.2f m, grip %.3f,%.3f,%.3f)", s.shieldSize[0], s.shieldSize[1], s.shieldGrip[0],
                  s.shieldGrip[1], s.shieldGrip[2]);
    }
    float m[12], xs = 0, ys = 0;
    if (!Wp().rend.ShieldView(m) || !Wp().rend.Proj(&xs, &ys)) return;
    for (; g_sh.hitFx > 0; --g_sh.hitFx) g_shieldR.Hit();
    g_shieldR.Draw(ctx, rtv, Wp().rend.Depth(w, h), w, h, hw, Now(), g_sh.lookDraw, m, xs, ys, cam, s, Wp().rend.SceneSrv(), Wp().rend.SceneGamma());
}
struct Stats {
    double tStat = 0, msSum = 0, msMax = 0;
    unsigned frames = 0, draws = 0;
    bool loggedFirst = false, loggedFmt = false, loggedDev = false;
} g_st;
void StepSounds(double now, float speed) {
    if (!g_fp) return;
    Weapon& w = Wp();
    const vm::Settings& s = w.set;
    LONG st = g_stance;
    bool sprint = (st & 2) != 0, tac = (st & 4) != 0;
    auto step = [&](bool sprinting, float gain) {
        float k = gain * (sprinting ? (tac && s.stepTacSprintGain >= 0.f ? s.stepTacSprintGain : s.stepSprintGain) : 1.f);
        static int logs = 0;
        if (logs < 60) {
            ++logs;
            g_log("step: %s at %.2f (gain %.2f)", sprinting ? (tac ? "tac sprint" : "sprint") : "run", now, k);
        }
        w.stepFoot.Play(s.stepFootVolume * k);
        vm::GunSound& cloth = sprinting && w.stepSprint.Loaded() ? w.stepSprint : w.stepRun;
        cloth.Play(s.stepClothVolume * k);
        w.stepRattle.Play(s.stepRattleVolume * k);
    };
    int clip = g_an.cur;
    bool inSprintClip = sprint && clip >= 0 && (clip == w.rend.FindClip(tac ? "tac_sprint" : "sprint") ||
                                                (tac && clip == w.rend.FindClip("sprint")));
    const float* fr = tac && s.tacSprintStepN ? s.tacSprintSteps : s.sprintSteps;
    int nfr = tac && s.tacSprintStepN ? s.tacSprintStepN : s.sprintStepN;
    if (inSprintClip && nfr > 0) {
        float dur = w.rend.ClipDuration(clip);
        float f = dur > 0.f ? static_cast<float>(std::fmod(now - g_an.curStart, static_cast<double>(dur))) / dur : 0.f;
        float prev = g_an.stepClip == clip && g_an.stepStart == g_an.curStart ? g_an.stepFrac : -1.f;
        g_an.stepClip = clip;
        g_an.stepStart = g_an.curStart;
        if (prev >= 0.f)
            for (int i = 0; i < nfr; ++i) {
                float x = fr[i];
                bool crossed = f >= prev ? (x > prev && x <= f) : (x > prev || x <= f);
                if (crossed) step(true, 1.f);
            }
        g_an.stepFrac = f;
        g_an.stepK = -1;
        g_an.stepNext = -1;
        return;
    }
    g_an.stepFrac = -1;
    const float iv = sprint ? (tac && s.tacSprintStepTime > 0.f ? s.tacSprintStepTime : s.sprintStepTime) : 0.f;
    if (iv > 0.f && speed >= 1.f) {
        if (g_an.stepNext < 0) {
            g_an.stepNext = now + iv * 0.5;
        } else if (now >= g_an.stepNext) {
            step(true, 1.f);
            g_an.stepNext += iv;
            if (g_an.stepNext <= now) g_an.stepNext = now + iv;
        }
        g_an.stepK = -1;
        return;
    }
    g_an.stepNext = -1;
    if (speed < 1.f || g_mo.bobAmount < 0.25f) {
        g_an.stepK = -1;
        return;
    }
    double k = std::floor(g_mo.bobPhase / 3.14159265358979);
    if (g_an.stepK >= 0 && k != g_an.stepK) step(sprint, (st & 1) ? s.stepAdsGain : 1.f);
    g_an.stepK = k;
}
void LoadWeaponFx(int idx, ID3D11Device* dev) {
    Weapon& w = g_w[idx];
    w.fxDirty = false;
    double tFx = Now();
    wchar_t dir[MAX_PATH * 2], path[MAX_PATH * 2];
    ModelDir(idx, dir);
    char err[256] = {};
    struct Layer {
        vm::GunSound* snd;
        const wchar_t* list;
        float vol;
        int maxVoices;
        const char* tag;
    } layers[11] = {{&w.gun, w.set.fireSound, w.set.fireVolume, w.set.fireMaxVoices, "fire sound"},
                   {&w.gunAds, w.set.fireSoundAds, w.set.fireVolume, w.set.fireMaxVoices, "fire sound (ads)"},
                   {&w.mech, w.set.fireMech, w.set.fireMechVolume, 24, "fire mech"},
                   {&w.mechAds, w.set.fireMechAds, w.set.fireMechVolume, 24, "fire mech (ads)"},
                   {&w.lfe, w.set.fireLfe, w.set.fireLfeVolume, 24, "fire lfe"},
                   {&w.atmo, w.set.fireAtmo, w.set.fireAtmoVolume, w.set.fireAtmoMaxVoices, "fire atmo"},
                   {&w.tail, w.set.fireTail, w.set.fireTailVolume, 2, "fire tail"},
                   {&w.lastShot, w.set.fireLastSound, w.set.fireLastVolume < 0.f ? w.set.fireVolume : w.set.fireLastVolume, 4, "fire last shot"},
                   {&w.casing, w.set.fireCasing, w.set.fireCasingVolume, 4, "fire casing"},
                   {&w.casingAds, w.set.fireCasingAds, w.set.fireCasingVolume, 4, "fire casing (ads)"},
                   {&w.shellLand, w.set.shellLandSound, w.set.shellLandVolume, w.set.shellLandMaxVoices, "shell land"}};
    w.tailPending = false;
    double tSnd = Now();
    for (Layer& ly : layers) {
        ly.snd->Release();
        if (!ly.list[0]) continue;
        err[0] = 0;
        if (ly.snd->Load(dir, ly.list, ly.maxVoices, err, sizeof err))
            g_log("viewmodel: [%d] %s %ls (%d variants, %.2f s, volume %.2f, max voices %d)", idx, ly.tag, ly.list,
                  ly.snd->Variants(), ly.snd->Seconds(), ly.vol, ly.maxVoices);
        else
            g_log("viewmodel: [%d] %s %ls failed: %s", idx, ly.tag, ly.list, err);
    }
    g_log("viewmodel: [%d] sounds %.1f ms", idx, (Now() - tSnd) * 1000.0);
    {
        struct Step {
            vm::GunSound* snd;
            const wchar_t* list;
            const char* tag;
        } steps[7] = {{&w.stepFoot, w.set.stepFoot, "step foot"}, {&w.stepRun, w.set.stepClothRun, "step cloth run"},
                      {&w.stepSprint, w.set.stepClothSprint, "step cloth sprint"}, {&w.stepRattle, w.set.stepRattle, "step rattle"},
                      {&w.spinUp, w.set.spinUpSound, "spin up"}, {&w.spinLoop, w.set.spinLoopSound, "spin loop"},
                      {&w.spinDown, w.set.spinDownSound, "spin down"}};
        for (Step& st : steps) {
            st.snd->Release();
            if (!st.list[0]) continue;
            err[0] = 0;
            if (st.snd->Load(dir, st.list, 3, err, sizeof err))
                g_log("viewmodel: [%d] %s (%d variants)", idx, st.tag, st.snd->Variants());
            else
                g_log("viewmodel: [%d] %s %ls failed: %s", idx, st.tag, st.list, err);
        }
    }
    for (int i = 0; i < vm::kMeleeSfx; ++i) {
        vm::GunSound& m = w.meleeSfx[i];
        m.Release();
        if (!w.set.meleeSfx[i][0]) continue;
        err[0] = 0;
        if (m.Load(dir, w.set.meleeSfx[i], 4, err, sizeof err))
            g_log("viewmodel: [%d] melee sfx %ls (%d variants)", idx, vm::kMeleeSfxNames[i], m.Variants());
        else
            g_log("viewmodel: [%d] melee sfx %ls %ls failed: %s", idx, vm::kMeleeSfxNames[i], w.set.meleeSfx[i], err);
    }
    {
        for (int i = 0; i < w.nAnim; ++i) w.anim[i].snd.Release();
        w.nAnim = 0;
        if (w.set.animSounds[0]) {
            swprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, w.set.animSounds);
            FILE* f = _wfopen(path, L"r");
            int bad = 0;
            if (f) {
                char line[512];
                while (fgets(line, sizeof line, f) && w.nAnim < kMaxAnimSounds) {
                    char clip[64], file[256];
                    float t = 0, v = 1;
                    if (line[0] == '#' || sscanf(line, "%63s %f %255s %f", clip, &t, file, &v) < 3) continue;
                    AnimSound& a = w.anim[w.nAnim];
                    a.clip = w.rend.FindClip(clip);
                    a.t = t;
                    a.vol = v;
                    a.shell = !strcmp(file, "!shell");
                    if (a.shell) {
                        if (a.clip >= 0)
                            ++w.nAnim;
                        else if (bad++ < 4)
                            g_log("viewmodel: [%d] anim shell '%s': no such clip", idx, clip);
                        continue;
                    }
                    wchar_t wf[256];
                    MultiByteToWideChar(CP_UTF8, 0, file, -1, wf, 256);
                    err[0] = 0;
                    if (a.clip >= 0 && a.snd.Load(dir, wf, 2, err, sizeof err)) {
                        ++w.nAnim;
                    } else {
                        if (bad < 4) g_log("viewmodel: [%d] anim sound '%s' %s: %s", idx, clip, file, a.clip < 0 ? "no such clip" : err);
                        ++bad;
                    }
                }
                fclose(f);
                g_log("viewmodel: [%d] anim sounds %ls: %d loaded, %d skipped", idx, w.set.animSounds, w.nAnim, bad);
            } else {
                g_log("viewmodel: [%d] anim sounds %ls not found", idx, path);
            }
        }
    }
    w.flash.Release();
    if (w.set.flashFx[0] || w.set.effectFx[0] || (w.set.flashGas && w.set.flashGasFx[0])) {
        err[0] = 0;
        wchar_t eff[MAX_PATH * 2] = L"", gas[MAX_PATH * 2] = L"";
        path[0] = 0;
        if (w.set.flashFx[0]) swprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, w.set.flashFx);
        if (w.set.effectFx[0]) swprintf(eff, MAX_PATH * 2, L"%ls\\%ls", dir, w.set.effectFx);
        if (w.set.flashGas && w.set.flashGasFx[0]) swprintf(gas, MAX_PATH * 2, L"%ls\\%ls", dir, w.set.flashGasFx);
        if (w.flash.Load(dev, path, eff, gas, err, sizeof err))
            g_log("viewmodel: [%d] muzzle fx: flame from %ls (scale %.2f, intensity %.2f, light %.2f, smoke %d, flame %d); "
                  "tracer / hit mist from %ls; gas %d from %ls (scale %.2f, alpha %.2f)",
                  idx, w.set.flashFx, w.set.flashScale, w.set.flashIntensity, w.set.flashLight, w.set.flashSmoke, w.set.flashFlame,
                  w.set.effectFx, w.flash.GasLoaded(), w.set.flashGasFx, w.set.flashGasScale, w.set.flashGasAlpha);
        else
            g_log("viewmodel: [%d] muzzle fx failed: %s", idx, err);
    }
    w.shells.Release();
    if (w.set.shellEject) {
        err[0] = 0;
        if (w.shells.Load(dev, err, sizeof err))
            g_log("viewmodel: [%d] shell eject on (tag_brass; %d shell%s; scale %.2f, speed %.2f, life %.2f, smoke %.2f, sparks %.2f; "
                  "out %.2f-%.2f fwd %.2f-%.2f up %.2f-%.2f m/s)", idx, w.set.shellCaliber, w.set.shellLink ? " + link" : "", w.set.shellScale,
                  w.set.shellSpeed, w.set.shellLife, w.set.shellSmoke, w.set.shellSparks, w.set.shellOut[0], w.set.shellOut[1], w.set.shellFwd[0],
                  w.set.shellFwd[1], w.set.shellUp[0], w.set.shellUp[1]);
        else
            g_log("viewmodel: [%d] %s", idx, err);
    }
    g_log("viewmodel: [%d] sounds + muzzle fx loaded in %.1f ms", idx, (Now() - tFx) * 1000.0);
}
constexpr int kPrefetchAhead = 2;
constexpr int kPrefetchPcmCap = 1024;
struct PrefetchJob {
    int state = 0;
    bool cancel = false;
    vm::Settings* set = nullptr;
    void** pcm = nullptr;
    int nPcm = 0;
};
PrefetchJob g_pfJob[kMaxModels];
int g_pfOrder[kMaxModels];
int g_pfOrderN = 0;
SRWLOCK g_pfLock = SRWLOCK_INIT;
HANDLE g_pfWake = nullptr;
volatile LONG g_pfRunning = 0;
volatile LONG64 g_pfProgressUs = 0;
bool g_pfStallLogged = false;
bool g_preloadOnBattle = true;
volatile LONG g_warm = 0;
double g_warmT0 = 0;
wchar_t g_armoryPath[MAX_PATH];
FILETIME g_armorySeen = {};
void ArmoryWatch() {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!g_preloadOnBattle || !GetFileAttributesExW(g_armoryPath, GetFileExInfoStandard, &fa) ||
        !CompareFileTime(&fa.ftLastWriteTime, &g_armorySeen))
        return;
    g_armorySeen = fa.ftLastWriteTime;
    if (g_fp || g_warm) return;
    double t0 = Now();
    char err[128] = {};
    bool audio = vm::GunSound::WarmEngine(err, sizeof err);
    g_log("warm: audio engine %s in %.1f ms", audio ? "ready" : err, (Now() - t0) * 1000.0);
    InterlockedExchange(&g_reqRoster, 1);
    InterlockedExchange(&g_warm, 1);
    bool hooked = InstallPresentHook();
    g_log("warm: new battle (tenzin_fps_armory.txt rewritten) - %s",
          hooked ? "loading weapons before first person" : "Present not hooked; weapons load at F6 as before");
}
int SoundListsOf(const vm::Settings& s, const wchar_t** out, int cap) {
    const wchar_t* const all[] = {s.fireSound, s.fireSoundAds, s.fireMech, s.fireMechAds, s.fireLfe, s.fireAtmo,
                                  s.fireTail, s.fireLastSound, s.fireCasing, s.fireCasingAds, s.shellLandSound, s.stepFoot,
                                  s.stepClothRun, s.stepClothSprint, s.stepRattle, s.spinUpSound, s.spinLoopSound, s.spinDownSound};
    int n = 0;
    for (const wchar_t* l : all)
        if (l[0] && n < cap) out[n++] = l;
    for (int i = 0; i < vm::kMeleeSfx && n < cap; ++i)
        if (s.meleeSfx[i][0]) out[n++] = s.meleeSfx[i];
    return n;
}
void PrefetchWork(int idx, const vm::Settings& s, void** pcm, int* nPcm) {
    double t0 = Now();
    char err[256] = {};
    int files = 0;
    size_t bytes = 0;
    bool ok = vm::prefetch::Model(idx, g_models[idx], &files, &bytes, err, sizeof err);
    double t1 = Now();
    wchar_t dir[MAX_PATH * 2];
    ModelDir(idx, dir);
    const wchar_t* lists[64];
    int nl = SoundListsOf(s, lists, 64);
    *nPcm = 0;
    for (int k = 0; k < nl; ++k) *nPcm += vm::GunSound::Prefetch(dir, lists[k], pcm + *nPcm, kPrefetchPcmCap - *nPcm);
    if (s.animSounds[0]) {
        wchar_t path[MAX_PATH * 2];
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, s.animSounds);
        if (FILE* f = _wfopen(path, L"r")) {
            char line[512];
            while (fgets(line, sizeof line, f) && *nPcm < kPrefetchPcmCap) {
                char clip[64], file[256];
                float t = 0, v = 1;
                if (line[0] == '#' || sscanf(line, "%63s %f %255s %f", clip, &t, file, &v) < 3) continue;
                wchar_t wf[256];
                MultiByteToWideChar(CP_UTF8, 0, file, -1, wf, 256);
                *nPcm += vm::GunSound::Prefetch(dir, wf, pcm + *nPcm, kPrefetchPcmCap - *nPcm);
            }
            fclose(f);
        }
    }
    g_log("prefetch: [%d] model %s (%d files, %.1f MB) %.1f ms, %d sounds %.1f ms", idx, ok ? "ok" : err, files, bytes / 1048576.0,
          (t1 - t0) * 1000.0, *nPcm, (Now() - t1) * 1000.0);
}
DWORD WINAPI PrefetchThread(void*) {
    double t0 = Now();
    int sh = vm::prefetch::WarmShaders();
    g_log("prefetch: background thread up; %d shaders compiled in %.1f ms", sh, (Now() - t0) * 1000.0);
    for (;;) {
        int idx = -1;
        vm::Settings* set = nullptr;
        AcquireSRWLockExclusive(&g_pfLock);
        int ready = 0;
        for (int k = 0; k < g_pfOrderN; ++k) ready += g_pfJob[g_pfOrder[k]].state == 3;
        for (int k = 0; k < g_pfOrderN && ready < kPrefetchAhead; ++k) {
            PrefetchJob& j = g_pfJob[g_pfOrder[k]];
            if (j.state != 1) continue;
            idx = g_pfOrder[k];
            j.state = 2;
            j.cancel = false;
            set = j.set;
            j.set = nullptr;
            break;
        }
        ReleaseSRWLockExclusive(&g_pfLock);
        if (idx < 0) {
            WaitForSingleObject(g_pfWake, 500);
            ArmoryWatch();
            continue;
        }
        void** pcm = new void*[kPrefetchPcmCap];
        int nPcm = 0;
        PrefetchWork(idx, *set, pcm, &nPcm);
        delete set;
        AcquireSRWLockExclusive(&g_pfLock);
        PrefetchJob& j = g_pfJob[idx];
        bool drop = j.cancel;
        if (drop) {
            j.state = 0;
        } else {
            j.state = 3;
            j.pcm = pcm;
            j.nPcm = nPcm;
        }
        ReleaseSRWLockExclusive(&g_pfLock);
        InterlockedExchange64(&g_pfProgressUs, static_cast<LONG64>(Now() * 1e6));
        if (drop) {
            for (int k = 0; k < nPcm; ++k) vm::GunSound::Unprefetch(pcm[k]);
            delete[] pcm;
            vm::prefetch::Drop(idx);
        }
    }
}
void PrefetchDiscard(int i) {
    if (!g_pfRunning || i < 0 || i >= kMaxModels) return;
    vm::Settings* set = nullptr;
    void** pcm = nullptr;
    int nPcm = 0;
    bool drop = false;
    AcquireSRWLockExclusive(&g_pfLock);
    PrefetchJob& j = g_pfJob[i];
    if (j.state == 1) {
        set = j.set;
        j.set = nullptr;
        j.state = 0;
    } else if (j.state == 2) {
        j.cancel = true;
    } else if (j.state == 3) {
        pcm = j.pcm;
        nPcm = j.nPcm;
        j.pcm = nullptr;
        j.nPcm = 0;
        j.state = 0;
        drop = true;
    }
    ReleaseSRWLockExclusive(&g_pfLock);
    delete set;
    if (!drop) return;
    for (int k = 0; k < nPcm; ++k) vm::GunSound::Unprefetch(pcm[k]);
    delete[] pcm;
    vm::prefetch::Drop(i);
    SetEvent(g_pfWake);
}
void PrefetchQueue(const volatile LONG* order, LONG n) {
    if (!g_pfRunning) return;
    bool in[kMaxModels] = {};
    int next[kMaxModels], nn = 0;
    for (LONG k = 0; k < n; ++k) {
        int i = order[k];
        if (i < 0 || i >= g_modelCount || in[i] || (g_w[i].rend.Ready() && !g_w[i].fxDirty)) continue;
        in[i] = true;
        next[nn++] = i;
    }
    for (int i = 0; i < g_modelCount; ++i)
        if (!in[i]) PrefetchDiscard(i);
    AcquireSRWLockExclusive(&g_pfLock);
    memcpy(g_pfOrder, next, sizeof(int) * nn);
    g_pfOrderN = nn;
    for (int k = 0; k < nn; ++k) {
        PrefetchJob& j = g_pfJob[next[k]];
        if (j.state == 0) {
            j.set = new vm::Settings(g_w[next[k]].set);
            j.state = 1;
        } else if (j.state == 2) {
            j.cancel = false;
        }
    }
    ReleaseSRWLockExclusive(&g_pfLock);
    InterlockedExchange64(&g_pfProgressUs, static_cast<LONG64>(Now() * 1e6));
    SetEvent(g_pfWake);
}
bool PrefetchReady(int i) {
    if (!g_pfRunning) return true;
    AcquireSRWLockShared(&g_pfLock);
    int st = g_pfJob[i].state;
    ReleaseSRWLockShared(&g_pfLock);
    if (st == 0 || st == 3) return true;
    if (static_cast<LONG64>(Now() * 1e6) - g_pfProgressUs < 5000000) return false;
    if (!g_pfStallLogged) g_log("prefetch: no progress for 5 s at model %d - loading on the render thread", i);
    g_pfStallLogged = true;
    return true;
}
bool EnsureWeapon(int idx, ID3D11Device* dev) {
    if (idx < 0 || idx >= g_modelCount) return false;
    Weapon& w = g_w[idx];
    if (!w.rend.Ready()) {
        double t0 = Now();
        if (!SettingsUnchanged(idx, w.set)) LoadAllSettings(idx, w.set);
        char err[512] = {};
        if (!w.rend.Init(dev, g_models[idx], err, sizeof err)) {
            g_log("viewmodel: init failed (%ls): %s", g_models[idx], err);
            PrefetchDiscard(idx);
            return false;
        }
        g_log("viewmodel: [%d] ready on device %p: %u triangles, %u bones, %u clips, model %ls (init %.1f ms)", idx, (void*)dev,
              w.rend.Triangles(), w.rend.Bones(), w.rend.Clips(), g_models[idx], (Now() - t0) * 1000.0);
        w.fxDirty = true;
        if (idx == g_modelIdx) {
            AnimReset(Now());
            MagApply();
        }
    }
    if (w.fxDirty) {
        LoadWeaponFx(idx, dev);
        PrefetchDiscard(idx);
    }
    return true;
}
bool Carried(int i) {
    for (LONG k = 0; k < g_rosterN; ++k)
        if (g_roster[k] == i) return true;
    return false;
}
void UnloadWeapon(int i) {
    Weapon& w = g_w[i];
    bool had = w.rend.Ready();
    w.rend.Release();
    vm::GunSound* all[18] = {&w.gun, &w.gunAds, &w.mech, &w.mechAds, &w.lfe, &w.atmo, &w.tail,
                             &w.stepFoot, &w.stepRun, &w.stepSprint, &w.stepRattle, &w.spinUp, &w.spinLoop, &w.spinDown,
                             &w.lastShot, &w.casing, &w.casingAds, &w.shellLand};
    for (vm::GunSound* gs : all) gs->Release();
    for (int k = 0; k < w.nAnim; ++k) w.anim[k].snd.Release();
    w.nAnim = 0;
    for (vm::GunSound& m : w.meleeSfx) m.Release();
    w.flash.Release();
    w.shells.Release();
    w.fxDirty = true;
    w.tailPending = false;
    PrefetchDiscard(i);
    if (had) g_log("weapon: unloaded model %d %ls (not carried)", i, w.set.weaponId);
}
void RosterStep(ID3D11Device* dev) {
    if (!InterlockedExchange(&g_reqRoster, 0)) return;
    wchar_t path[MAX_PATH * 2];
    wcsncpy(path, g_ini, MAX_PATH * 2 - 1);
    path[MAX_PATH * 2 - 1] = 0;
    wchar_t* slash = wcsrchr(path, L'\\');
    wcscpy(slash ? slash + 1 : path, L"tenzin_fps_armory.txt");
    static char owned[kMaxModels * 64];
    char mode[32] = "", loadout[kSlots * 64 + 8] = "";
    owned[0] = 0;
    static char ammoUp[kMaxModels * 72];
    ammoUp[0] = 0;
    char infAmmo[16] = "";
    char infTac[16] = "";
    if (FILE* f = _wfopen(path, L"r")) {
        static char line[kMaxModels * 64 + 64];
        while (fgets(line, sizeof line, f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
            if (!strncmp(line, "mode=", 5)) snprintf(mode, sizeof mode, "%s", line + 5);
            if (!strncmp(line, "owned=", 6)) snprintf(owned, sizeof owned, "%s", line + 6);
            if (!strncmp(line, "loadout=", 8)) snprintf(loadout, sizeof loadout, "%s", line + 8);
            if (!strncmp(line, "ammo_up=", 8)) snprintf(ammoUp, sizeof ammoUp, "%s", line + 8);
            if (!strncmp(line, "inf_ammo=", 9)) snprintf(infAmmo, sizeof infAmmo, "%s", line + 9);
            if (!strncmp(line, "inf_tac=", 8)) snprintf(infTac, sizeof infTac, "%s", line + 8);
        }
        fclose(f);
    }
    {
        bool camp = !strcmp(mode, "campaign");
        int nUp = 0;
        for (int i = 0; i < g_modelCount && i < kMaxModels; ++i) {
            LONG lv = 0;
            char a[64];
            snprintf(a, sizeof a, "%ls", g_w[i].set.weaponId);
            size_t la = strlen(a);
            for (const char* p = ammoUp; camp && la && *p;) {
                const char* e = p;
                while (*e && *e != ',') ++e;
                if (static_cast<size_t>(e - p) > la + 1 && !strncmp(p, a, la) && p[la] == ':') lv = atol(p + la + 1);
                p = *e ? e + 1 : e;
            }
            g_ammoLevel[i] = lv < 0 ? 0 : lv > 9 ? 9 : lv;
            nUp += g_ammoLevel[i] > 0;
        }
        if (ammoUp[0] || nUp) g_log("weapon: ammo upgrades '%s' (%s) -> %d weapons upgraded", ammoUp, camp ? "campaign" : "not campaign, ignored", nUp);
        LONG inf = camp && !strcmp(infAmmo, "1") ? 1 : 0;
        if (InterlockedExchange(&g_infAmmo, inf) != inf || inf)
            g_log("weapon: infinite ammo (MCT) %s ('%s', %s)", inf ? "ON" : "off", infAmmo, camp ? "campaign" : "not campaign");
        LONG tac = camp && !strcmp(infTac, "1") ? 1 : 0;
        if (InterlockedExchange(&g_infTac, tac) != tac || tac)
            g_log("move: infinite tactical sprint stamina (MCT) %s ('%s', %s)", tac ? "ON" : "off", infTac, camp ? "campaign" : "not campaign");
    }
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fa) &&
        CompareFileTime(&fa.ftLastWriteTime, &g_armoryTime) != 0) {
        g_armoryTime = fa.ftLastWriteTime;
        RefillForNewBattle();
    }
    bool iniAll = GetPrivateProfileIntW(L"viewmodel", L"loadout_all", 0, g_ini) != 0;
    bool all = g_loadoutAll || iniAll;
    InterlockedExchange(&g_rosterAll, all ? 1 : 0);
    bool campaign = !strcmp(mode, "campaign") && !iniAll;
    auto ownedHas = [&](const wchar_t* id) {
        char a[64];
        snprintf(a, sizeof a, "%ls", id);
        size_t la = strlen(a);
        for (const char* p = owned; *p;) {
            const char* e = p;
            while (*e && *e != ',' && *e != ';') ++e;
            if (static_cast<size_t>(e - p) == la && !strncmp(p, a, la)) return true;
            p = *e ? e + 1 : e;
        }
        return false;
    };
    auto usable = [&](int i) {
        const vm::Settings& s = g_w[i].set;
        return s.weaponId[0] && (!campaign || s.weaponDefault || ownedHas(s.weaponId));
    };
    LONG n = 0;
    LONG slot[kSlots] = {-1, -1, -1, -1};
    const char* from = iniAll ? "ini loadout_all (test: every weapon)" : "all usable";
    if (all) {
        for (int i = 0; i < g_modelCount; ++i)
            if (usable(i)) g_roster[n++] = i;
    } else {
        if (!(campaign && loadout[0])) {
            wchar_t w[kSlots * 64 + 8] = L"";
            from = "ini loadout_default";
            if (!campaign) {
                GetPrivateProfileStringW(L"viewmodel", L"loadout_custom", L"", w, kSlots * 64 + 8, g_ini);
                if (w[0]) from = "ini loadout_custom";
            }
            if (!w[0]) GetPrivateProfileStringW(L"viewmodel", L"loadout_default", L"", w, kSlots * 64 + 8, g_ini);
            snprintf(loadout, sizeof loadout, "%ls", w);
        } else {
            from = "armory file";
        }
        const char* p = loadout;
        for (int k = 0; k < kSlots; ++k) {
            const char* e = p;
            while (*e && *e != ',') ++e;
            char id[64];
            size_t len = static_cast<size_t>(e - p) < sizeof id - 1 ? static_cast<size_t>(e - p) : sizeof id - 1;
            memcpy(id, p, len);
            id[len] = 0;
            while (len && id[len - 1] == ' ') id[--len] = 0;
            const char* idp = id;
            while (*idp == ' ') ++idp;
            if (*idp) {
                wchar_t wid[64];
                MultiByteToWideChar(CP_UTF8, 0, idp, -1, wid, 64);
                for (int i = 0; i < g_modelCount && slot[k] < 0; ++i) {
                    bool dup = false;
                    for (int j = 0; j < k; ++j) dup |= slot[j] == i;
                    if (!dup && !wcscmp(g_w[i].set.weaponId, wid) && usable(i)) slot[k] = i;
                }
                if (slot[k] < 0) g_log("weapon: slot %d '%s' not usable here (not owned / not in models= / duplicate) - left empty", k + 1, idp);
            }
            p = *e ? e + 1 : e;
        }
        for (int k = 0; k < kSlots; ++k)
            if (slot[k] >= 0) g_roster[n++] = slot[k];
        if (!n) {
            from = "first usable (no loadout resolved)";
            for (int i = 0; i < g_modelCount && n < kSlots; ++i)
                if (usable(i)) slot[n] = i, g_roster[n++] = i;
        }
    }
    if (!n) g_roster[n++] = g_modelIdx;
    for (int k = 0; k < kSlots; ++k) InterlockedExchange(&g_slot[k], slot[k]);
    InterlockedExchange(&g_rosterN, n);
    g_log("weapon: roster %ld from %s (armory file %s, mode '%s', owned '%s', loadout '%s')", n, from, mode[0] || owned[0] ? "found" : "missing",
          mode, owned, loadout);
    bool curOk = false;
    for (LONG k = 0; k < n; ++k) {
        int i = g_roster[k];
        g_log("weapon:   %ld = model %d %ls (%ls)", k + 1, i, g_models[i], g_w[i].set.weaponId);
        if (i == g_modelIdx) curOk = true;
    }
    if (!all)
        g_log("weapon: slots 1 primary %d / 2 primary %d / 3 pistol %d / 4 melee %d (model index, -1 = empty)", slot[0], slot[1], slot[2], slot[3]);
    g_preloadNext = 0;
    PrefetchQueue(g_roster, n);
    if (!curOk && g_sw.phase == 0 && EnsureWeapon(g_roster[0], dev)) {
        g_modelIdx = g_roster[0];
        InterlockedExchange(&g_wantIdx, g_modelIdx);
        AnimReset(Now());
        MagApply();
        g_log("weapon: current model not usable here -> %ls", g_models[g_modelIdx]);
    }
    int keep = g_sw.phase ? g_sw.target : -1;
    for (int i = 0; i < g_modelCount; ++i) {
        if (i == g_modelIdx || i == keep || Carried(i)) continue;
        g_unloadAt[i] = 0;
        if (g_w[i].rend.Ready()) UnloadWeapon(i);
    }
}
void UnloadStep() {
    if (!g_unloadPending) return;
    double now = Now();
    int left = 0;
    for (int i = 0; i < g_modelCount; ++i) {
        if (g_unloadAt[i] <= 0) continue;
        if (now < g_unloadAt[i]) {
            ++left;
            continue;
        }
        g_unloadAt[i] = 0;
        if (i != g_modelIdx && !(g_sw.phase && g_sw.target == i) && !Carried(i)) UnloadWeapon(i);
    }
    g_unloadPending = left;
}
void PreloadStep(ID3D11Device* dev) {
    UnloadStep();
    if (g_preloadNext < 0 || g_sw.phase) return;
    while (g_preloadNext < g_rosterN) {
        int i = g_roster[g_preloadNext];
        if (!g_w[i].rend.Ready() || g_w[i].fxDirty) {
            if (!PrefetchReady(i)) return;
            ++g_preloadNext;
            EnsureWeapon(i, dev);
            return;
        }
        ++g_preloadNext;
    }
    g_preloadNext = -1;
}
void SwitchStep(ID3D11Device* dev) {
    double now = Now();
    LONG req = InterlockedExchange(&g_reqWeapon, 0);
    if (req > 0) {
        int k = req - 1;
        int cur = g_sw.phase == 1 ? g_sw.target : g_modelIdx;
        if (k < g_modelCount && k != cur) {
            InterlockedExchange(&g_switching, 1);
            InterlockedExchange(&g_spin.state, 0);
            Wp().spinUp.FadeAll();
            Wp().spinLoop.FadeAll();
            g_an.spinSeen = 0;
            InterlockedExchange(&g_mag.reload, 0);
            InterlockedExchange(&g_mag.reqAnim, 0);
            g_sw.target = k;
            if (g_sw.phase != 1) {
                int c = Wp().rend.FindClip("drop");
                double fade = g_an.stance == 3 ? 0.3 : g_an.stance == 2 || now < g_an.sprintExitEnd ? 0.2 : 0.08;
                if (c >= 0) Play(c, now, fade);
                g_sw.until = now + (c >= 0 ? Wp().rend.ClipDuration(c) * kDropFrac : 0.05);
                g_sw.phase = 1;
            }
        }
    }
    if (g_sw.phase == 1 && now >= g_sw.until) {
        int k = g_sw.target;
        if (EnsureWeapon(k, dev)) {
            int old = g_modelIdx;
            g_modelIdx = k;
            if (old != k && !Carried(old)) {
                g_unloadAt[old] = now + kUnloadDelay;
                ++g_unloadPending;
            }
            AnimReset(now);
            MagApply();
            int c = Wp().rend.FindClip("draw");
            g_sw.until = now + (c >= 0 ? Wp().rend.ClipDuration(c) * kRaiseFrac : 0.05);
            g_sw.phase = 2;
            g_log("weapon: switched to model %d/%d %ls (%ls)", g_modelIdx + 1, g_modelCount, g_models[g_modelIdx], Wp().set.weaponId);
        } else {
            g_sw.phase = 0;
            InterlockedExchange(&g_switching, 0);
            InterlockedExchange(&g_wantIdx, g_modelIdx);
            AnimReset(now);
        }
    } else if (g_sw.phase == 2 && now >= g_sw.until) {
        g_sw.phase = 0;
        InterlockedExchange(&g_switching, 0);
    }
}
void CheckDevice(ID3D11Device* dev) {
    if (!Wp().rend.Ready() || Wp().rend.Device() == dev) return;
    g_log("viewmodel: device changed (%p -> %p); reloading", (void*)Wp().rend.Device(), (void*)dev);
    for (int i = 0; i < g_modelCount; ++i) {
        g_w[i].rend.Release();
        g_w[i].fxDirty = true;
    }
    g_shieldR.Release();
    InterlockedExchange(&g_reqRoster, 1);
}
void WarmStep(IDXGISwapChain* sc) {
    ID3D11Device* dev = nullptr;
    if (FAILED(sc->GetDevice(IID_ID3D11Device, reinterpret_cast<void**>(&dev))) || !dev) {
        InterlockedExchange(&g_warm, 0);
        return;
    }
    if (InterlockedCompareExchange(&g_warm, 2, 1) == 1) g_warmT0 = Now();
    CheckDevice(dev);
    bool ok = EnsureWeapon(g_modelIdx, dev);
    if (ok) {
        RosterStep(dev);
        PreloadStep(dev);
    }
    if (!ok || (g_preloadNext < 0 && !g_reqRoster)) {
        int ready = 0;
        for (LONG k = 0; k < g_rosterN; ++k) ready += g_w[g_roster[k]].rend.Ready();
        g_log("warm: %s in %.2f s before first person (%d of %ld weapons ready)", ok ? "preload done" : "stopped (current weapon failed)",
              Now() - g_warmT0, ready, static_cast<long>(g_rosterN));
        InterlockedExchange(&g_warm, 0);
    }
    dev->Release();
}
void DrawLayer(IDXGISwapChain* sc, int part) {
    if (g_failed || !(g_testLayer || g_fp)) {
        if (!g_failed && g_warm && part == kPartAll) WarmStep(sc);
        return;
    }
    double t0 = Now();
    if (part != kPartCross && InterlockedExchange(&g_reqModelNext, 0) && g_modelCount > 1) {
        int next = ((g_sw.phase == 1 ? g_sw.target : g_modelIdx) + 1) % g_modelCount;
        InterlockedExchange(&g_wantIdx, next);
        InterlockedExchange(&g_reqWeapon, next + 1);
    }
    if (part != kPartCross && InterlockedExchange(&g_reload, 0)) {
        LoadAllSettings(g_modelIdx, Wp().set);
        Wp().fxDirty = true;
        InterlockedExchange(&g_reqRoster, 1);
        if (Wp().rend.Ready()) MagApply();
        LauncherReadTimes(), LauncherRelease();
        LadderRelease();
        g_log("viewmodel: settings reloaded (fov %.1f offset %.3f,%.3f,%.3f rot %.1f,%.1f,%.1f scale %.3f mirror %d)",
              Wp().set.fovDeg, Wp().set.offset[0], Wp().set.offset[1], Wp().set.offset[2], Wp().set.rotDeg[0], Wp().set.rotDeg[1],
              Wp().set.rotDeg[2], Wp().set.scale, Wp().set.mirrorX);
    }
    ID3D11Device* dev = nullptr;
    if (FAILED(sc->GetDevice(IID_ID3D11Device, reinterpret_cast<void**>(&dev))) || !dev) {
        if (!g_st.loggedDev) g_log("viewmodel: swapchain has no D3D11 device; layer off");
        g_st.loggedDev = true;
        g_failed = true;
        return;
    }
    if (part != kPartCross) {
        CheckDevice(dev);
        if (!EnsureWeapon(g_modelIdx, dev)) {
            g_failed = true;
            dev->Release();
            return;
        }
        RosterStep(dev);
        SwitchStep(dev);
        PreloadStep(dev);
    }
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&bb))) && bb) {
        D3D11_TEXTURE2D_DESC td;
        bb->GetDesc(&td);
        bool hw = false;
        DXGI_FORMAT rf = RtvFormat(td.Format, &hw);
        if (rf == DXGI_FORMAT_UNKNOWN || td.SampleDesc.Count != 1) {
            if (!g_st.loggedFmt) g_log("viewmodel: back buffer format %d samples %u not supported; layer off", td.Format,
                                       td.SampleDesc.Count);
            g_st.loggedFmt = true;
            g_failed = true;
        } else {
            D3D11_RENDER_TARGET_VIEW_DESC rd = {};
            rd.Format = rf;
            rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            ID3D11RenderTargetView* rtv = nullptr;
            if (SUCCEEDED(dev->CreateRenderTargetView(bb, &rd, &rtv)) && rtv) {
                ID3D11DeviceContext* ctx = nullptr;
                dev->GetImmediateContext(&ctx);
                if (ctx) {
                    if (part != kPartGun) UiLayerOnPresent(ctx, bb, td.Width, td.Height);
                    float mx = 0, my = 0, ma = 1;
                    bool mv = false, gun = part != kPartCross;
                    if (gun) {
                        vm::Look look = ComputeLook();
                        if (part == kPartAll) HudCoverSave(dev, ctx, bb, td);
                        LadderDraw(dev, ctx, rtv, td.Width, td.Height, hw);
                        const bool gunDown = LadderGunDown();
                        if (!gunDown && Wp().rend.Draw(ctx, rtv, bb, td.Width, td.Height, hw, Wp().set, look)) ++g_st.draws;
                        mv = !gunDown && Wp().rend.Muzzle(&mx, &my, &ma);
                        vm::MuzzleFrame mf;
                        vm::WorldCam wc;
                        bool hasMf = g_fp && !gunDown && Wp().rend.MuzzleView(&mf), hasCam = g_fp && ReadCam(&wc);
                        if (g_fp && !gunDown) ShieldDraw(dev, ctx, rtv, td.Width, td.Height, hw, hasCam ? &wc : nullptr);
                        if (hasMf)
                            Wp().shells.Draw(ctx, rtv, Wp().rend.Depth(td.Width, td.Height), td.Width, td.Height, hw, Now(), &mf,
                                             hasCam ? &wc : nullptr, Wp().set, look.flashLight, Wp().rend.SceneSrv(), Wp().rend.SceneGamma());
                        else
                            Wp().shells.Clear();
                        {
                            Weapon& w = Wp();
                            float dist, age;
                            int kind;
                            static unsigned rng = 0x7f4a7c15u;
                            double tnow = Now();
                            while (w.shells.PopLanding(&dist, &age, &kind)) {
                                if (age < g_fxStat.landAgeMin) g_fxStat.landAgeMin = age;
                                if (age > g_fxStat.landAgeMax) g_fxStat.landAgeMax = age;
                                int recent = 0, oldest = 0;
                                for (int k = 0; k < 16; ++k) {
                                    if (tnow - w.landAt[k] < 1.0) ++recent;
                                    if (w.landAt[k] < w.landAt[oldest]) oldest = k;
                                }
                                if (!w.shellLand.Loaded() || recent >= static_cast<int>(w.set.shellLandPerSec)) {
                                    ++g_fxStat.landsDropped;
                                    continue;
                                }
                                float att = dist <= 1.f ? 1.f : dist >= 3.f ? 0.3f : 1.f - 0.35f * (dist - 1.f);
                                rng = rng * 1664525u + 1013904223u;
                                float pitch = 0.85f + 0.15f * static_cast<float>((rng >> 8) & 0xffff) / 65535.f;
                                w.shellLand.Play(w.set.shellLandVolume * att, false, pitch);
                                w.landAt[oldest] = tnow;
                                ++g_fxStat.lands;
                            }
                        }
                        {
                            static const bool preGun = getenv("HFPS_DISTORT_PRE_GUN") != nullptr;
                            bool dg = false;
                            ID3D11ShaderResourceView* ds = Wp().flash.HasDistortion() ? Wp().rend.CopyForDistort(ctx, bb, td.Width, td.Height, &dg) : nullptr;
                            if (preGun) ds = Wp().rend.SceneSrv(), dg = Wp().rend.SceneGamma();
                            Wp().flash.SetScene(ds, dg, preGun ? nullptr : Wp().rend.DistortDepth());
                        }
                        if (hasMf || hasCam)
                            Wp().flash.Draw(ctx, rtv, Wp().rend.Depth(td.Width, td.Height), td.Width, td.Height, hw, Now(), hasMf ? &mf : nullptr,
                                         hasCam ? &wc : nullptr, Wp().set);
                        LauncherDraw(dev, ctx, rtv, bb, td.Width, td.Height, hw, look);
                        if (part == kPartAll) HudCoverRestore(ctx, bb);
                    }
                    if (part != kPartGun && g_fp) {
                        vm::CrosshairSettings cs;
                        float cspread, calpha;
                        if (CrossFrame(cs, cspread, calpha)) g_cross.Draw(ctx, rtv, td.Width, td.Height, hw, cs, cspread, calpha);
                    }
                    ctx->Release();
                    if (gun) {
                        InterlockedIncrement(&g_muz.seq);
                        MemoryBarrier();
                        g_muz.valid = mv;
                        g_muz.x = mx;
                        g_muz.y = my;
                        g_muz.aspect = ma;
                        g_muz.t = Now();
                        MemoryBarrier();
                        InterlockedIncrement(&g_muz.seq);
                    }
                }
                rtv->Release();
                if (!g_st.loggedFirst) {
                    g_st.loggedFirst = true;
                    g_log("viewmodel: first draw on back buffer %ux%u fmt=%d (rtv %d, %s) thread %lu", td.Width,
                          td.Height, td.Format, rf, hw ? "hw-srgb/linear" : "shader-srgb", GetCurrentThreadId());
                }
            }
        }
        bb->Release();
    }
    dev->Release();
    double ms = (Now() - t0) * 1000.0;
    g_st.msSum += ms;
    if (ms > g_st.msMax) g_st.msMax = ms;
    if (part != kPartGun) ++g_st.frames;
    double now = Now();
    if (now - g_st.tStat >= 5.0) {
        if (g_st.frames)
            g_log("viewmodel: 5s frames=%u draws=%u cpu avg %.3f ms max %.3f ms fp=%d test=%d", g_st.frames, g_st.draws,
                  g_st.msSum / g_st.frames, g_st.msMax, (int)g_fp, (int)g_testLayer);
        g_st.tStat = now;
        g_st.frames = g_st.draws = 0;
        g_st.msSum = g_st.msMax = 0;
    }
}
volatile LONG g_foreignPresents = 0;
__attribute__((noinline)) HRESULT STDMETHODCALLTYPE HkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    uintptr_t ret = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    if (ret == g_base + g_ra.presentReturn) {
        if (!(flags & DXGI_PRESENT_TEST)) {
            g_lastSc = sc, g_presentTid = GetCurrentThreadId();
            int part = UiLayerPresentPart();
            g_probeMute = true;
            DrawLayer(sc, part);
            g_probeMute = false;
            UiLayerBbStep();
        }
    } else if (InterlockedIncrement(&g_foreignPresents) <= 3) {
        g_log("viewmodel: Present from elsewhere (return %p, thread %lu) - forwarded without drawing", (void*)ret,
              GetCurrentThreadId());
    }
    return g_origPresent(sc, sync, flags);
}
UINT IniKey(const wchar_t* key, UINT def) {
    UINT k = GetPrivateProfileIntW(L"viewmodel", key, def, g_ini);
    return (k > 0 && k < 0xFF) ? k : def;
}
}
bool VerifyStatic(uintptr_t exeBase, LogFn log, const RenderAddrs& addrs) {
    g_base = exeBase;
    g_log = log;
    g_ra = addrs;
    bool ok = g_ra.presentHelperBytes && g_ra.globalWriteBytes &&
              Ptr(exeBase + g_ra.vtRenderer + g_ra.vtInitSlot) == exeBase + g_ra.fnRendererInit &&
              BytesMatch(exeBase + g_ra.fnPresentHelper, g_ra.presentHelperBytes, g_ra.presentHelperLen) &&
              BytesMatch(exeBase + g_ra.insGlobalWrite, g_ra.globalWriteBytes, g_ra.globalWriteLen) &&
              BytesMatch(exeBase + g_ra.insScCreate, kScCreateBytes, sizeof kScCreateBytes) &&
              g_ra.presentReturn > g_ra.fnPresentHelper && g_ra.presentReturn < g_ra.fnPresentHelper + 0x80;
    if (!ok) log("viewmodel: renderer / Present signatures do not match this exe; viewmodel layer disabled");
    g_staticOk = ok;
    unsigned char getBb[8] = {0x48, 0x8B, 0x81, 0, 0, 0, 0, 0xC3};
    uint32_t off = static_cast<uint32_t>(g_ra.oBackBufferObj);
    memcpy(getBb + 3, &off, 4);
    g_bbRouteOk = ok && g_ra.fnGetBb && g_ra.fnBbCopy && Ptr(exeBase + g_ra.vtRenderer + g_ra.vtGetBbSlot) == exeBase + g_ra.fnGetBb &&
                  BytesMatch(exeBase + g_ra.fnGetBb, getBb, sizeof getBb) && BytesMatch(exeBase + g_ra.fnBbCopy, kBbCopyBytes, sizeof kBbCopyBytes);
    log("ui-layer: back-buffer object route (renderer +0x%llx, copy slot +0x%llx) %s", static_cast<unsigned long long>(g_ra.oBackBufferObj),
        static_cast<unsigned long long>(kBbCopySlot), g_bbRouteOk ? "ok" : "does not match this exe - gun stays at Present (drawn over the UI)");
    return ok;
}
void Setup(uintptr_t exeBase, const wchar_t* dllDir, LogFn log) {
    g_base = exeBase;
    g_log = log;
    InitializeCriticalSection(&g_setLock);
    InitializeCriticalSection(&g_hookLock);
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_t0);
    swprintf(g_ini, MAX_PATH, L"%ls\\tenzin_fps.ini", dllDir);
    g_enabled = GetPrivateProfileIntW(L"viewmodel", L"enabled", 1, g_ini) != 0;
    g_testKey = IniKey(L"test_key", 0);
    g_reloadKey = IniKey(L"reload_key", VK_F8);
    g_animKey = IniKey(L"anim_key", VK_F9);
    g_modelKey = IniKey(L"model_key", VK_F10);
    g_shieldKey = IniKey(L"shield_key", 'Q');
    g_shieldHitKey = IniKey(L"shield_hit_key", 0);
    static wchar_t list[kMaxModels * MAX_PATH];
    constexpr DWORD kListLen = kMaxModels * MAX_PATH;
    GetPrivateProfileStringW(L"viewmodel", L"models", L"", list, kListLen, g_ini);
    if (!list[0]) GetPrivateProfileStringW(L"viewmodel", L"model", L"tenzin_fps_vm\\m7\\m7.hvm", list, kListLen, g_ini);
    g_modelCount = 0;
    for (wchar_t* tok = list; *tok && g_modelCount < kMaxModels;) {
        wchar_t* end = wcschr(tok, L';');
        if (end) *end = 0;
        while (*tok == L' ') ++tok;
        if (*tok) swprintf(g_models[g_modelCount++], MAX_PATH * 2, L"%ls\\%ls", dllDir, tok);
        if (!end) break;
        tok = end + 1;
    }
    if (!g_modelCount) swprintf(g_models[g_modelCount++], MAX_PATH * 2, L"%ls\\tenzin_fps_vm\\m7\\m7.hvm", dllDir);
    for (int i = 0; i < g_modelCount; ++i) LoadAllSettings(i, g_w[i].set);
    log("viewmodel: enabled=%d static=%d keys test=0x%02X reload=0x%02X anim=0x%02X model=0x%02X models=%d first=%ls "
        "fov=%.1f offset=%.3f,%.3f,%.3f rot=%.1f,%.1f,%.1f",
        g_enabled, g_staticOk, g_testKey, g_reloadKey, g_animKey, g_modelKey, g_modelCount, g_models[0], Wp().set.fovDeg,
        Wp().set.offset[0], Wp().set.offset[1], Wp().set.offset[2], Wp().set.rotDeg[0], Wp().set.rotDeg[1], Wp().set.rotDeg[2]);
    LauncherSetup(dllDir);
    LadderSetup(dllDir);
    if (g_enabled && (g_staticOk || !exeBase)) {
        g_preloadOnBattle = GetPrivateProfileIntW(L"viewmodel", L"preload_on_battle", 1, g_ini) != 0;
        wcsncpy(g_armoryPath, g_ini, MAX_PATH - 1);
        g_armoryPath[MAX_PATH - 1] = 0;
        if (wchar_t* slash = wcsrchr(g_armoryPath, L'\\')) wcscpy(slash + 1, L"tenzin_fps_armory.txt");
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(g_armoryPath, GetFileExInfoStandard, &fa)) g_armorySeen = fa.ftLastWriteTime;
        g_pfWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE th = g_pfWake ? CreateThread(nullptr, 0, PrefetchThread, nullptr, 0, nullptr) : nullptr;
        if (th) {
            SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL);
            CloseHandle(th);
            InterlockedExchange(&g_pfRunning, 1);
            const volatile LONG cur = g_modelIdx;
            PrefetchQueue(&cur, 1);
        } else {
            log("prefetch: background thread not started (%lu) - weapons load on the render thread as before", GetLastError());
        }
    }
}
bool Enabled() { return g_enabled && g_staticOk; }
bool LayerActive() { return Enabled() && (g_testLayer || g_fp); }
UINT TestKey() { return g_testKey; }
UINT ReloadKey() { return g_reloadKey; }
UINT AnimKey() { return g_animKey; }
UINT ModelKey() { return g_modelKey; }
void CycleAnim() {
    if (LayerActive()) InterlockedExchange(&g_reqAnimNext, 1);
}
void CycleModel() {
    if (LayerActive()) InterlockedExchange(&g_reqModelNext, 1);
}
void SpendCurrent() {
    int m = g_mag.loadedModel;
    if (m < 0 || m >= kMaxModels || InterlockedExchange(&g_spent[m], 1)) return;
    LONG cnt = g_rosterN;
    for (LONG k = 0; k < cnt; ++k) {
        LONG idx = g_roster[k];
        if (idx != m && !g_spent[idx]) {
            InterlockedExchange(&g_wantIdx, idx);
            InterlockedExchange(&g_reqWeapon, idx + 1);
            break;
        }
    }
    g_log("weapon: %ls used up for this battle", g_w[m].set.weaponId);
}
void Fire() {
    if (!Enabled() || !g_fp) return;
    bool empty = g_mag.size > 0 && !(g_infAmmo && g_mag.disposable) && InterlockedDecrement(&g_mag.ammo) <= 0;
    if (empty) InterlockedExchange(&g_reqFireLast, 1);
    InterlockedExchange(&g_reqFire, 1);
    InterlockedIncrement(&g_kickShots);
    if (empty) {
        InterlockedExchange(&g_mag.ammo, 0);
        if (g_mag.disposable) SpendCurrent();
        else StartMagReload();
    }
}
double FireInterval() {
    LONG us = g_mag.fireUs;
    return us > 0 ? us * 1e-6 : 0.0;
}
void MagTick() {
    LONG r = g_mag.reload;
    if (r != 1 && r != 2) return;
    LONG64 t0 = g_mag.t0Us;
    if (static_cast<LONG64>(Now() * 1e6) - t0 < g_mag.refillUs[r]) return;
    if (InterlockedCompareExchange(&g_mag.reload, 0, r) == r) {
        LONG size = g_mag.size, have = g_mag.ammo, res = g_mag.reserve;
        if (have < 0) have = 0;
        if (res < 0) {
            InterlockedExchange(&g_mag.ammo, size);
        } else {
            LONG take = size - have < res ? size - have : res;
            if (take < 0) take = 0;
            InterlockedExchange(&g_mag.reserve, res - take);
            InterlockedExchange(&g_mag.ammo, have + take);
        }
    }
}
constexpr LONG kMaxMag = 10000;
LONG EffMagSize(int i) {
    if (i < 0 || i >= g_modelCount || i >= kMaxModels) return 0;
    const auto& s = g_w[i].set;
    LONG size = s.magSize + (s.magDisposable ? g_ammoLevel[i] * s.ammoUpgrade : 0);
    return size > 0 && size <= kMaxMag ? size : 0;
}
LONG EffAmmoTotal(int i) {
    if (i < 0 || i >= g_modelCount || i >= kMaxModels) return 0;
    const auto& s = g_w[i].set;
    return s.ammoTotal > 0 && s.magSize > 0 && !s.magDisposable ? s.ammoTotal + g_ammoLevel[i] * s.ammoUpgrade : 0;
}
void RefillForNewBattle() {
    for (int i = 0; i < g_modelCount && i < kMaxModels; ++i) {
        const auto& s = g_w[i].set;
        LONG tot = EffAmmoTotal(i);
        bool disp = s.magDisposable, total = tot > 0;
        if (!disp && !total) continue;
        if (disp) InterlockedExchange(&g_spent[i], 0);
        if (i < kMaxModels) g_mag.ammoByModel[i] = -1, g_mag.reserveByModel[i] = -2;
        if (i == g_mag.loadedModel) {
            LONG size = EffMagSize(i);
            InterlockedExchange(&g_mag.size, size);
            InterlockedExchange(&g_mag.ammo, size);
            if (total) InterlockedExchange(&g_mag.reserve, g_infAmmo ? -1 : tot > size ? tot - size : 0);
        }
        g_log("weapon: new battle -> %ls refilled%s (ammo level %ld: magazine %ld, total %ld)", s.weaponId,
              total ? " (magazine + reserve)" : "", g_ammoLevel[i], EffMagSize(i), tot);
    }
}
bool Reloading() {
    MagTick();
    return g_mag.reload != 0;
}
bool InfTacStamina() { return g_infTac != 0; }
void AmmoInfo(int* ammo, int* size, bool* reloading, int* reserve) {
    LONG s = g_mag.size, a = g_mag.ammo, r = g_mag.reserve;
    *size = s > 0 ? static_cast<int>(s) : 0;
    *ammo = s > 0 ? static_cast<int>(a < 0 ? 0 : a > s ? s : a) : 0;
    *reloading = s > 0 && g_mag.reload != 0;
    *reserve = s <= 0 ? -1 : g_mag.disposable ? (g_infAmmo ? -1 : 0) : r < 0 ? -1 : static_cast<int>(r);
}
bool StartMagReload() {
    if (!Enabled() || !g_fp || g_mag.disposable) return false;
    LONG size = g_mag.size;
    if (size <= 0 || g_mag.reload != 0 || g_mag.ammo >= size) return false;
    if (g_mag.reserve == 0) return false;
    LONG kind = g_mag.ammo > 0 ? 1 : 2;
    InterlockedExchange64(&g_mag.t0Us, static_cast<LONG64>(Now() * 1e6));
    if (InterlockedCompareExchange(&g_mag.reload, kind, 0) != 0) return false;
    InterlockedExchange(&g_mag.reqAnim, kind);
    return true;
}
bool CanFire() {
    if (g_switching) return false;
    if (g_spin.mode && g_spin.state != 2) return false;
    if (g_mag.size <= 0) return true;
    MagTick();
    if (g_mag.reload != 0) return false;
    if (g_mag.ammo <= 0) {
        if (!g_mag.disposable) StartMagReload();
        return false;
    }
    return true;
}
void MagApply() {
    if (g_mag.loadedModel >= 0 && g_mag.loadedModel < kMaxModels && g_mag.size > 0) {
        g_mag.ammoByModel[g_mag.loadedModel] = g_mag.reload ? (g_mag.reload == 2 ? 0 : g_mag.ammo) : g_mag.ammo;
        g_mag.reserveByModel[g_mag.loadedModel] = g_mag.reserve;
    }
    InterlockedExchange(&g_mag.reload, 0);
    InterlockedExchange(&g_mag.reqAnim, 0);
    InterlockedExchange(&g_spin.mode, Wp().set.fireMode == 1 ? 1 : 0);
    InterlockedExchange(&g_spin.state, 0);
    InterlockedExchange(&g_spin.upUs, static_cast<LONG>((Wp().set.spinUpTime > 0.f ? Wp().set.spinUpTime : 0.f) * 1e6f));
    InterlockedExchange(&g_spin.keepUs, static_cast<LONG>((Wp().set.spinKeep > 0.f ? Wp().set.spinKeep : 0.f) * 1e6f));
    g_an.spinSeen = 0;
    float rpm = Wp().set.fireRpm;
    InterlockedExchange(&g_mag.fireUs, rpm >= 10.f && rpm <= 2000.f ? static_cast<LONG>(60e6 / rpm) : 0);
    LONG size = EffMagSize(g_modelIdx);
    auto refill = [](float sec, int clip) -> LONG {
        if (!(sec > 0.f) && clip >= 0) sec = Wp().rend.ClipDuration(clip) * 0.8f;
        if (!(sec > 0.05f)) sec = 2.f;
        return static_cast<LONG>(sec * 1e6f);
    };
    int cTac = Wp().rend.FindClip("reload"), cEmpty = Wp().rend.FindClip("reload_empty");
    InterlockedExchange(&g_mag.refillUs[1], refill(Wp().set.reloadRefill, cTac));
    InterlockedExchange(&g_mag.refillUs[2], refill(Wp().set.reloadEmptyRefill, cEmpty >= 0 ? cEmpty : cTac));
    bool same = g_mag.loadedModel == g_modelIdx;
    g_mag.loadedModel = g_modelIdx;
    LONG keep = g_modelIdx < kMaxModels ? g_mag.ammoByModel[g_modelIdx] : -1;
    LONG ammo = size > 0 ? (keep >= 0 && keep <= size ? keep : size) : 0;
    if (same && size > 0 && g_mag.ammo >= 0 && g_mag.ammo <= size) ammo = g_mag.ammo;
    if (g_modelIdx < kMaxModels && g_spent[g_modelIdx]) ammo = 0;
    InterlockedExchange(&g_mag.size, size);
    InterlockedExchange(&g_mag.ammo, ammo);
    InterlockedExchange(&g_mag.disposable, Wp().set.magDisposable && size > 0 ? 1 : 0);
    {
        LONG total = size > 0 ? EffAmmoTotal(g_modelIdx) : 0;
        LONG res = -1;
        if (total > 0 && g_infAmmo) {
            g_log("viewmodel: ammo total %ld per battle -> reserve infinite (MCT)", total);
        } else if (total > 0) {
            LONG kept = g_modelIdx < kMaxModels ? g_mag.reserveByModel[g_modelIdx] : -2;
            if (same && g_mag.reserve >= 0) kept = g_mag.reserve;
            res = kept >= 0 ? kept : (total > ammo ? total - ammo : 0);
            g_log("viewmodel: ammo total %ld per battle -> reserve %ld", total, res);
        }
        InterlockedExchange(&g_mag.reserve, res);
    }
    g_log("viewmodel: weapon fire %s, magazine %ld (ammo %ld), reload refill %.2f / empty %.2f s",
          g_mag.fireUs ? "custom rpm" : "[fire] interval", size, ammo, g_mag.refillUs[1] * 1e-6, g_mag.refillUs[2] * 1e-6);
    if (g_mag.fireUs) g_log("viewmodel: fire rate %.0f rpm (%.4f s)", rpm, g_mag.fireUs * 1e-6);
}
void ToggleTestLayer() {
    if (!Enabled()) return;
    if (g_testLayer) {
        g_testLayer = false;
        g_log("viewmodel: test layer off");
        return;
    }
    if (InstallPresentHook()) {
        g_failed = false;
        g_testLayer = true;
        InterlockedExchange(&g_reqDraw, 1);
        g_log("viewmodel: test layer on");
    }
}
void RequestReload() {
    if (!Enabled()) return;
    InterlockedExchange(&g_reload, 1);
    g_log("viewmodel: reload requested");
}
void SetFirstPerson(bool on) {
    if (!Enabled()) return;
    if (on && !g_fp) {
        InterlockedExchange(&g_reqRoster, 1);
        if (InstallPresentHook()) g_failed = false;
        if (!g_testLayer) InterlockedExchange(&g_reqDraw, 1);
    }
    if (!on && g_testLayer) {
        g_testLayer = false;
        g_log("viewmodel: test layer off (first person exited)");
    }
    g_fp = on;
}
bool Muzzle(float* ndcX, float* ndcY, float* aspect) {
    if (!Enabled() || !g_fp || g_failed) return false;
    for (int tries = 0; tries < 8; ++tries) {
        LONG s1 = g_muz.seq;
        if (s1 & 1) continue;
        MemoryBarrier();
        bool v = g_muz.valid;
        float x = g_muz.x, y = g_muz.y, a = g_muz.aspect;
        double t = g_muz.t;
        MemoryBarrier();
        if (g_muz.seq != s1) continue;
        if (!v || Now() - t > 0.5 || !(a > 0.2f && a < 8.f)) return false;
        *ndcX = x;
        *ndcY = y;
        *aspect = a;
        return true;
    }
    return false;
}
void PublishCamera(const float eye[3], const float right[3], const float up[3], const float fwd[3], float fovY) {
    InterlockedIncrement(&g_cam.seq);
    MemoryBarrier();
    memcpy(g_cam.eye, eye, sizeof g_cam.eye);
    memcpy(g_cam.right, right, sizeof g_cam.right);
    memcpy(g_cam.up, up, sizeof g_cam.up);
    memcpy(g_cam.fwd, fwd, sizeof g_cam.fwd);
    g_cam.fovY = fovY;
    g_cam.valid = true;
    MemoryBarrier();
    InterlockedIncrement(&g_cam.seq);
}
void HitFxStats(int* shown, int* kills) {
    *shown = g_hitFxCount;
    *kills = g_hitFxKills;
}
void HitFx(const float pos[3], const float dir[3], bool kill) {
    AcquireSRWLockExclusive(&g_hitq.lock);
    if (g_hitq.n < 16) {
        memcpy(g_hitq.p[g_hitq.n], pos, sizeof(float) * 3);
        memcpy(g_hitq.d[g_hitq.n], dir, sizeof(float) * 3);
        g_hitq.kill[g_hitq.n] = kill;
        ++g_hitq.n;
    }
    ReleaseSRWLockExclusive(&g_hitq.lock);
}
void Tracer(const float from[3], const float to[3]) {
    AcquireSRWLockExclusive(&g_trq.lock);
    if (g_trq.n < 16) {
        memcpy(g_trq.a[g_trq.n], from, sizeof(float) * 3);
        memcpy(g_trq.b[g_trq.n], to, sizeof(float) * 3);
        ++g_trq.n;
    }
    ReleaseSRWLockExclusive(&g_trq.lock);
}
void SelectWeapon(int n) {
    if (!Enabled() || !g_fp) return;
    LONG idx;
    if (g_rosterAll) {
        LONG cnt = g_rosterN;
        if (n < 1 || n > cnt) return;
        idx = g_roster[n - 1];
    } else {
        if (n < 1 || n > kSlots) return;
        idx = g_slot[n - 1];
        if (idx < 0) return;
    }
    if (g_spent[idx]) return;
    InterlockedExchange(&g_wantIdx, idx);
    InterlockedExchange(&g_reqWeapon, idx + 1);
}
void CycleWeapon(int dir) {
    if (!Enabled() || !g_fp) return;
    LONG cnt = g_rosterN;
    if (cnt < 1) return;
    LONG want = g_wantIdx, pos = -1;
    for (LONG k = 0; k < cnt; ++k)
        if (g_roster[k] == want) pos = k;
    if (pos < 0) pos = dir > 0 ? cnt - 1 : 0;
    LONG idx = want;
    for (LONG step = 0; step < cnt; ++step) {
        pos = (pos + (dir > 0 ? 1 : cnt - 1)) % cnt;
        idx = g_roster[pos];
        if (!g_spent[idx]) break;
    }
    if (idx == want || g_spent[idx]) return;
    InterlockedExchange(&g_wantIdx, idx);
    InterlockedExchange(&g_reqWeapon, idx + 1);
}
bool Busy() { return g_switching != 0 || Reloading(); }
void SetTrigger(bool held) {
    if (!g_spin.mode || !Enabled() || !g_fp) return;
    LONG64 now = static_cast<LONG64>(Now() * 1e6);
    LONG st = g_spin.state;
    LONG64 t0 = g_spin.tUs;
    bool can = held && !g_switching && !Reloading() && !(g_mag.size > 0 && g_mag.ammo <= 0);
    auto go = [&](LONG s) {
        InterlockedExchange64(&g_spin.tUs, now);
        InterlockedExchange(&g_spin.state, s);
    };
    if (can) {
        if (st == 0)
            go(g_spin.upUs > 0 ? 1 : 2);
        else if (st == 3)
            go(now - t0 < g_spin.keepUs ? 2 : 1);
        else if (st == 1 && now - t0 >= g_spin.upUs)
            go(2);
    } else {
        if (st == 1 || st == 2)
            go(3);
        else if (st == 3 && now - t0 >= g_spin.keepUs)
            go(0);
    }
}
int CurrentWeapon() { return g_modelIdx; }
void SetHudCover(float x0, float y0, float x1, float y1) {
    auto q = [](float f) { return static_cast<LONG64>((f < 0.f ? 0.f : f > 1.f ? 1.f : f) * 65535.f + 0.5f); };
    LONG64 v = 0;
    if (x1 > x0 && y1 > y0) v = q(x0) | (q(y0) << 16) | (q(x1) << 32) | (q(y1) << 48);
    InterlockedExchange64(&g_hudCover, v);
}
void GetKick(kick::Params* out) {
    int idx = g_modelIdx;
    if (idx < 0 || idx >= kMaxModels) idx = 0;
    AcquireSRWLockShared(&g_kickLock);
    *out = g_kickP[idx];
    ReleaseSRWLockShared(&g_kickLock);
    const float f[3] = {g_sh.kickMilli[0] * 1e-3f, g_sh.kickMilli[1] * 1e-3f, g_sh.kickMilli[2] * 1e-3f};
    OneHandKick(*out, g_sh.wUs * 1e-6f, f);
}
UINT ShieldKey() { return g_shieldKey; }
UINT ShieldHitKey() { return g_shieldHitKey; }
void ToggleShield() {
    if (!Enabled() || !g_fp) return;
    LONG v = InterlockedXor(&g_sh.want, 1) ^ 1;
    g_log("shield: key -> want %ld (allowed %ld)", static_cast<long>(v), static_cast<long>(g_sh.allowed));
}
void ShieldHit() { InterlockedIncrement(&g_sh.hitN); }
float ShieldBlockFraction() { return static_cast<float>(g_sh.blockMilli) * 0.001f; }
void DebugShieldHit() {
    if (Enabled() && g_fp) ShieldHit();
}
bool ShieldWanted() { return g_sh.want != 0 && g_sh.allowed != 0; }
void ShieldGame(float* weight, float* moveScale, float* zoomKeep) {
    float w = g_sh.wUs * 1e-6f;
    w = w < 0.f ? 0.f : (w > 1.f ? 1.f : w);
    *weight = w;
    *moveScale = 1.f - (1.f - g_sh.moveMilli * 1e-3f) * w;
    *zoomKeep = 1.f - (1.f - g_sh.zoomMilli * 1e-3f) * w;
}
float ShieldCamKick() { return InterlockedExchange(&g_sh.camKickMdeg, 0) * 1e-3f; }
void MoveImpulse(float jump, float land) {
    if (jump > 0.f) InterlockedExchangeAdd(&g_jlJumpMilli, static_cast<LONG>(jump * 1000.f + 0.5f));
    if (land > 0.f) InterlockedExchangeAdd(&g_jlLandMilli, static_cast<LONG>(land * 1000.f + 0.5f));
}
bool LauncherConfigured() { return Enabled() && g_lc.configured; }
UINT LauncherFireKey() { return g_lc.fireKey; }
UINT LauncherSwitchKey() { return g_lc.switchKey; }
UINT LauncherFlameKey() { return g_lc.flameKey; }
int LauncherSelected() { return static_cast<int>(g_lc.selected); }
void LauncherSwitch() {
    LONG v = InterlockedXor(&g_lc.selected, 1) ^ 1;
    InterlockedExchange(&g_lc.switchReq, 1);
    g_log("launcher: selected %s", v == kLauncherIce ? "ice" : "frag");
}
const char* LauncherAbility(int kind) { return kind >= 0 && kind < kLauncherKinds ? g_lc.ability[kind] : ""; }
float LauncherWindup(int kind) { return kind >= 0 && kind < kLauncherKinds ? g_lc.windup[kind] : 1.f; }
bool LauncherBusy() { return g_lc.busy && static_cast<LONG>(Now() * 1000.0) < g_lc.busyUntilMs; }
void LauncherCast(int kind, bool driven, float windup) {
    if (!g_lc.configured || kind < 0 || kind >= kLauncherKinds) return;
    double now = Now();
    if (!(windup > 0.f && windup < 10.f)) windup = g_lc.windup[kind];
    InterlockedExchange(&g_lc.windupMs, static_cast<LONG>(windup * 1000.f + 0.5f));
    InterlockedExchange(&g_lc.driven, driven ? 1 : 0);
    InterlockedExchange64(&g_lc.fireUs, 0);
    InterlockedExchange64(&g_lc.abortUs, 0);
    InterlockedExchange(&g_lc.busyUntilMs, static_cast<LONG>((now + kLcMaxHold + 1.0) * 1000.0));
    InterlockedExchange(&g_lc.busy, 1);
    InterlockedExchange64(&g_lc.reqUs, static_cast<LONG64>(now * 1e6));
    MemoryBarrier();
    InterlockedExchange(&g_lc.req, kind + 1);
}
void LauncherFire() { InterlockedExchange64(&g_lc.fireUs, static_cast<LONG64>(Now() * 1e6)); }
void LauncherAbort() { InterlockedExchange64(&g_lc.abortUs, static_cast<LONG64>(Now() * 1e6)); }
void LadderPublish(const LadderView* v, int n, bool gunDown) {
    if (n < 0) n = 0;
    if (n > kLadderMax) n = kLadderMax;
    InterlockedIncrement(&g_ld.seq);
    MemoryBarrier();
    if (n) memcpy(g_ld.v, v, sizeof(LadderView) * static_cast<size_t>(n));
    g_ld.n = n;
    MemoryBarrier();
    InterlockedIncrement(&g_ld.seq);
    InterlockedExchange(&g_ld.gunDown, gunDown ? 1 : 0);
}
void LadderSound(int kind) {
    if (kind >= 0 && kind < 3) InterlockedIncrement(&g_ld.sndReq[kind]);
}
bool LadderReady() { return Enabled() && g_ld.configured; }
bool MeleeActive() { return g_meleeOn != 0; }
void MeleeInput(bool attack, bool block) { InterlockedExchange(&g_meleeIn, (attack ? 1 : 0) | (block ? 2 : 0)); }
int TakeMeleeStrike() { return static_cast<int>(InterlockedExchange(&g_meleeStrike, 0)) - 1; }
void MeleeHitFeedback(int hits) {
    LONG t = g_meleeHitQTail;
    if (t - g_meleeHitQHead >= 32) return;
    g_meleeHitQ[t & 31] = hits;
    MemoryBarrier();
    InterlockedExchange(&g_meleeHitQTail, t + 1);
}
const char* WeaponProjectile(int idx) { return idx >= 0 && idx < g_modelCount ? g_w[idx].set.projectile : ""; }
float WeaponFireRange(int idx) { return idx >= 0 && idx < g_modelCount ? g_w[idx].set.fireRange : 0.f; }
float CurrentAdsTime(int kind) {
    LONG us = kind >= 0 && kind < 3 ? g_curAdsUs[kind] : 0;
    return us > 0 ? us * 1e-6f : 0.f;
}
void PublishAdsTime(float normal, float sprint, float tac) {
    const float v[3] = {normal, sprint, tac};
    for (int k = 0; k < 3; ++k)
        if (v[k] > 0.f) InterlockedExchange(&g_adsTimeUs[k], static_cast<LONG>(v[k] * 1e6f + 0.5f));
}
bool CurveAdsFovScaling() { return g_curveFovScaling != 0; }
float HipSpreadCone() { return g_spreadMicro * 1e-6f; }
void SpreadSeed(unsigned seed) { g_spreadRng = seed ? seed : 1u; }
void SpreadDir(const float fwd[3], const float right[3], const float up[3], float out[3]) {
    float cone = HipSpreadCone();
    for (int i = 0; i < 3; ++i) out[i] = fwd[i];
    if (!(cone > 0.f)) return;
    auto rnd = [] {
        g_spreadRng = g_spreadRng * 1664525u + 1013904223u;
        return static_cast<float>((g_spreadRng >> 8) & 0xffffff) / 16777216.f;
    };
    float r = std::tan(cone * rnd()), th = 6.2831853f * rnd();
    float a = r * std::cos(th), b = r * std::sin(th), n = 0.f;
    for (int i = 0; i < 3; ++i) {
        out[i] = fwd[i] + right[i] * a + up[i] * b;
        n += out[i] * out[i];
    }
    n = 1.f / std::sqrt(n);
    for (int i = 0; i < 3; ++i) out[i] *= n;
}
bool CurveAdsState(float* frac, float* adsFovAlt) {
    LONG f = g_curveFracMicro;
    if (f < 0) return false;
    *frac = f * 1e-6f;
    *adsFovAlt = g_curveFovMilli * 1e-3f;
    return true;
}
void PublishStance(bool ads, bool sprint, bool tac, int adsKind) {
    InterlockedExchange(&g_stance, (ads ? 1 : 0) | (sprint ? 2 : 0) | (tac ? 4 : 0) | (ads ? (adsKind & 3) << 3 : 0));
}
void PublishView(float yaw, float pitch, float speed) {
    InterlockedIncrement(&g_pub.seq);
    MemoryBarrier();
    g_pub.yaw = yaw;
    g_pub.pitch = pitch;
    g_pub.speed = speed;
    MemoryBarrier();
    InterlockedIncrement(&g_pub.seq);
}
}
