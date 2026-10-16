#include "vm_game.h"
#include <d3d11.h>
#include <dxgi.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "vm_audio.h"
#include "vm_render.h"
namespace vmgame {
void MagApply();
namespace {
RenderAddrs g_ra = {};
const unsigned char kScCreateBytes[] = {0x4C, 0x8D, 0x83, 0x50, 0x13, 0x00, 0x00};
constexpr size_t kScSlots = 128;
LogFn g_log = nullptr;
uintptr_t g_base = 0;
bool g_staticOk = false;
bool g_enabled = false;
UINT g_testKey = VK_F7, g_reloadKey = VK_F8, g_animKey = VK_F9, g_modelKey = VK_F10;
wchar_t g_ini[MAX_PATH];
constexpr int kMaxModels = 8;
wchar_t g_models[kMaxModels][MAX_PATH * 2];
int g_modelCount = 0;
int g_modelIdx = 0;
volatile bool g_testLayer = false, g_fp = false, g_failed = false;
volatile LONG g_reload = 0, g_reqModelNext = 0, g_reqAnimNext = 0, g_reqFire = 0, g_reqDraw = 0;
struct MagState {
    volatile LONG size = 0;
    volatile LONG ammo = 0;
    volatile LONG reload = 0;
    volatile LONG reqAnim = 0;
    volatile LONG64 t0Us = 0;
    volatile LONG refillUs[3] = {};
    volatile LONG fireUs = 0;
    LONG ammoByModel[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int loadedModel = -1;
} g_mag;
volatile LONG g_stance = 0;
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
void LoadAllSettings(int idx, vm::Settings& s) {
    vm::CrosshairSettings cs;
    vm::LoadCrosshair(g_ini, cs);
    g_crossSet = cs;
    s = vm::Settings();
    vm::LoadSettings(g_ini, s);
    wchar_t side[MAX_PATH * 2];
    SidecarIni(idx, side);
    vm::LoadSettings(side, s);
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
};
constexpr int kMaxAnimSounds = 48;
struct Weapon {
    vm::Renderer rend;
    vm::Settings set;
    vm::GunSound gun, gunAds, mech, mechAds, lfe, atmo, tail;
    vm::GunSound stepFoot, stepRun, stepSprint, stepRattle;
    AnimSound anim[kMaxAnimSounds];
    int nAnim = 0;
    vm::MuzzleFlash flash;
    bool fxDirty = true;
    double lastShotAt = 0;
    bool tailPending = false;
};
Weapon g_w[kMaxModels];
inline Weapon& Wp() { return g_w[g_modelIdx]; }
volatile LONG g_roster[kMaxModels] = {};
volatile LONG g_rosterN = 0;
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
void StepSounds(double now, float speed);
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
struct Motion {
    bool init = false, haveView = false;
    double tPrev = 0;
    float lastYaw = 0, lastPitch = 0, swayX = 0, swayY = 0, bobPhase = 0, bobAmount = 0;
    float adsW = 0, sprintW = 0;
} g_mo;
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
    g_mo.bobPhase += dt * (2.0f + speed * 1.6f) * (g_mo.bobAmount > 0.02f ? 1.f : 0.f);
    if (g_mo.bobPhase > 6283.f) g_mo.bobPhase = std::fmod(g_mo.bobPhase, 6.2831853f);
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
    Wp().flash.BeginFrame(now, Wp().set);
    l.flashLight = g_fp ? Wp().flash.LightLevel(now) : 0.f;
    l.time = static_cast<float>(now);
    {
        LONG st = g_fp ? g_stance : 0;
        float k = 1 - std::exp(-dt * 12.0f);
        g_mo.adsW += (((st & 1) ? 1.f : 0.f) - g_mo.adsW) * k;
        g_mo.sprintW += (((st & 6) ? 1.f : 0.f) - g_mo.sprintW) * k;
    }
    float swayK = 1.f - 0.7f * g_mo.adsW, bobK = (1.f - 0.85f * g_mo.adsW) * (1.f - g_mo.sprintW);
    l.swayX = g_mo.swayX * swayK;
    l.swayY = g_mo.swayY * swayK;
    l.bobPhase = g_mo.bobPhase;
    l.bobAmount = g_mo.bobAmount * bobK;
    return l;
}
struct Anim {
    int cur = -1, prev = -1, idle = -1, fireNext = 0, cycleNext = 0, stance = 0;
    int reloadKind = 0;
    double reloadAt = 0;
    int evClip = -1;
    double evStart = 0, evLast = -1;
    double stepK = -1;
    float stepFrac = -1;
    double curStart = 0, prevStart = 0, fadeStart = 0, fadeLen = 0.15;
} g_an;
void Play(int clip, double now, double fade) {
    if (clip < 0) return;
    g_an.prev = g_an.cur;
    g_an.prevStart = g_an.curStart;
    g_an.cur = clip;
    g_an.curStart = now;
    g_an.fadeStart = now;
    g_an.fadeLen = g_an.prev >= 0 ? fade : 0;
}
void AnimReset(double now) {
    g_an = Anim();
    g_an.idle = Wp().rend.FindClip("idle");
    int draw = Wp().rend.FindClip("draw");
    Play(draw >= 0 ? draw : g_an.idle, now, 0);
}
int StanceIdle() {
    int c = g_an.stance == 1 ? Wp().rend.FindClip("ads_idle") : g_an.stance == 2 ? Wp().rend.FindClip("sprint") : -1;
    if (g_an.stance == 3) {
        c = Wp().rend.FindClip("tac_sprint");
        if (c < 0) c = Wp().rend.FindClip("sprint");
    }
    return c >= 0 ? c : g_an.idle;
}
void AnimStep(vm::Look& l, double now) {
    bool fired = InterlockedExchange(&g_reqFire, 0) != 0;
    if (fired) {
        bool ads = (g_stance & 1) != 0;
        if (ads && Wp().gunAds.Loaded())
            Wp().gunAds.Play(Wp().set.fireVolume);
        else
            Wp().gun.Play(Wp().set.fireVolume);
        if (ads && Wp().mechAds.Loaded())
            Wp().mechAds.Play(Wp().set.fireMechVolume);
        else
            Wp().mech.Play(Wp().set.fireMechVolume);
        Wp().lfe.Play(Wp().set.fireLfeVolume);
        Wp().atmo.Play(Wp().set.fireAtmoVolume);
        Wp().lastShotAt = now;
        Wp().tailPending = Wp().tail.Loaded();
        Wp().flash.Trigger(g_mo.adsW);
    }
    if (Wp().tailPending && now - Wp().lastShotAt >= Wp().set.fireTailGap) {
        Wp().tailPending = false;
        Wp().tail.Play(Wp().set.fireTailVolume);
    }
    for (int i = 0; i < g_modelCount; ++i) {
        Weapon& w = g_w[i];
        vm::GunSound* all[11] = {&w.gun, &w.gunAds, &w.mech, &w.mechAds, &w.lfe, &w.atmo, &w.tail,
                                 &w.stepFoot, &w.stepRun, &w.stepSprint, &w.stepRattle};
        for (vm::GunSound* gs : all) gs->Update();
        for (int k = 0; k < w.nAnim; ++k) w.anim[k].snd.Update();
    }
    if (!Wp().rend.Clips()) return;
    {
        LONG raw = g_fp ? g_stance : 0;
        int st = (raw & 4) ? 3 : (raw & 2) ? 2 : (raw & 1) ? 1 : 0;
        bool reloadAnim = g_mag.reload != 0 && g_an.cur >= 0 &&
                          (g_an.cur == Wp().rend.FindClip("reload") || g_an.cur == Wp().rend.FindClip("reload_empty"));
        if (st != g_an.stance && reloadAnim) {
            g_an.stance = st;
        } else if (st != g_an.stance) {
            int from = g_an.stance;
            g_an.stance = st;
            if (st == 1) {
                int c = Wp().rend.FindClip("ads_up");
                Play(c >= 0 ? c : StanceIdle(), now, from >= 2 ? 0.2 : 0.06);
            } else if (st >= 2) {
                Play(StanceIdle(), now, 0.25);
            } else if (from == 1) {
                int c = Wp().rend.FindClip("ads_down");
                Play(c >= 0 ? c : g_an.idle, now, 0.06);
            } else {
                Play(g_an.idle, now, from == 3 ? 0.3 : 0.2);
            }
        }
    }
    if (InterlockedExchange(&g_reqDraw, 0)) Play(Wp().rend.FindClip("draw") >= 0 ? Wp().rend.FindClip("draw") : g_an.idle, now, 0.1);
    if (fired) {
        static const char* fires[3] = {"fire1", "fire2", "fire3"};
        int c = g_an.stance == 1 ? Wp().rend.FindClip("ads_fire") : -1;
        for (int k = 0; k < 3 && c < 0; ++k) c = Wp().rend.FindClip(fires[(g_an.fireNext + k) % 3]);
        g_an.fireNext = (g_an.fireNext + 1) % 3;
        Play(c, now, 0.04);
    }
    if (LONG ra = InterlockedExchange(&g_mag.reqAnim, 0)) {
        g_an.reloadKind = static_cast<int>(ra);
        g_an.reloadAt = fired ? now + 0.12 : now;
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
        double t = now - g_an.curStart;
        Weapon& w = Wp();
        for (int i = 0; i < w.nAnim; ++i) {
            AnimSound& a = w.anim[i];
            if (a.clip == g_an.cur && a.t > g_an.evLast && a.t <= t && a.t > t - 0.2) {
                a.snd.Play(a.vol * w.set.animSoundVolume);
                static int logs = 0;
                if (logs < 80) {
                    ++logs;
                    g_log("anim sound: clip %d at %.2f s (event %.2f s)", a.clip, t, a.t);
                }
            }
        }
        g_an.evLast = t;
    }
    if (g_an.cur >= 0 && !Wp().rend.ClipLoops(g_an.cur) && g_an.cur != StanceIdle() &&
        now - g_an.curStart >= Wp().rend.ClipDuration(g_an.cur))
        Play(StanceIdle(), now, g_an.stance == 1 ? 0.05 : 0.15);
    l.clip = g_an.cur;
    l.clipTime = static_cast<float>(now - g_an.curStart);
    l.clipPrev = g_an.prev;
    l.clipTimePrev = static_cast<float>(now - g_an.prevStart);
    l.blend = g_an.fadeLen > 0 ? static_cast<float>((now - g_an.fadeStart) / g_an.fadeLen) : 1.f;
    if (l.blend >= 1.f) {
        l.blend = 1.f;
        l.clipPrev = -1;
    }
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
        float k = gain * (sprinting ? s.stepSprintGain : 1.f);
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
        float prev = g_an.stepFrac;
        if (prev >= 0.f)
            for (int i = 0; i < nfr; ++i) {
                float x = fr[i];
                bool crossed = f >= prev ? (x > prev && x <= f) : (x > prev || x <= f);
                if (crossed) step(true, 1.f);
            }
        g_an.stepFrac = f;
        g_an.stepK = -1;
        return;
    }
    g_an.stepFrac = -1;
    if (speed < 1.f || g_mo.bobAmount < 0.25f) {
        g_an.stepK = -1;
        return;
    }
    double k = std::floor(g_mo.bobPhase / 3.14159265358979);
    if (g_an.stepK >= 0 && k != g_an.stepK) step(false, (st & 1) ? s.stepAdsGain : 1.f);
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
    } layers[7] = {{&w.gun, w.set.fireSound, w.set.fireVolume, w.set.fireMaxVoices, "fire sound"},
                   {&w.gunAds, w.set.fireSoundAds, w.set.fireVolume, w.set.fireMaxVoices, "fire sound (ads)"},
                   {&w.mech, w.set.fireMech, w.set.fireMechVolume, 24, "fire mech"},
                   {&w.mechAds, w.set.fireMechAds, w.set.fireMechVolume, 24, "fire mech (ads)"},
                   {&w.lfe, w.set.fireLfe, w.set.fireLfeVolume, 24, "fire lfe"},
                   {&w.atmo, w.set.fireAtmo, w.set.fireAtmoVolume, w.set.fireAtmoMaxVoices, "fire atmo"},
                   {&w.tail, w.set.fireTail, w.set.fireTailVolume, 2, "fire tail"}};
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
        } steps[4] = {{&w.stepFoot, w.set.stepFoot, "step foot"}, {&w.stepRun, w.set.stepClothRun, "step cloth run"},
                      {&w.stepSprint, w.set.stepClothSprint, "step cloth sprint"}, {&w.stepRattle, w.set.stepRattle, "step rattle"}};
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
                    wchar_t wf[256];
                    MultiByteToWideChar(CP_UTF8, 0, file, -1, wf, 256);
                    err[0] = 0;
                    if (a.clip >= 0 && a.snd.Load(dir, wf, 2, err, sizeof err))
                        ++w.nAnim;
                    else
                        ++bad;
                }
                fclose(f);
                g_log("viewmodel: [%d] anim sounds %ls: %d loaded, %d skipped", idx, w.set.animSounds, w.nAnim, bad);
            } else {
                g_log("viewmodel: [%d] anim sounds %ls not found", idx, path);
            }
        }
    }
    w.flash.Release();
    if (w.set.flashFx[0]) {
        err[0] = 0;
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, w.set.flashFx);
        wchar_t gas[MAX_PATH * 2] = L"";
        if (w.set.flashGas && w.set.flashGasFx[0]) swprintf(gas, MAX_PATH * 2, L"%ls\\%ls", dir, w.set.flashGasFx);
        if (w.flash.Load(dev, path, gas, err, sizeof err))
            g_log("viewmodel: [%d] muzzle flash = ref AK first-person layers from %ls (scale %.2f, intensity %.2f, light %.2f, "
                  "smoke %d, flame %d); ref gas %d from %ls (scale %.2f, alpha %.2f)",
                  idx, w.set.flashFx, w.set.flashScale, w.set.flashIntensity, w.set.flashLight, w.set.flashSmoke, w.set.flashFlame,
                  w.flash.GasLoaded(), w.set.flashGasFx, w.set.flashGasScale, w.set.flashGasAlpha);
        else
            g_log("viewmodel: [%d] muzzle flash %ls failed: %s", idx, w.set.flashFx, err);
    }
    g_log("viewmodel: [%d] sounds + muzzle fx loaded in %.1f ms", idx, (Now() - tFx) * 1000.0);
}
bool EnsureWeapon(int idx, ID3D11Device* dev) {
    if (idx < 0 || idx >= g_modelCount) return false;
    Weapon& w = g_w[idx];
    if (!w.rend.Ready()) {
        double t0 = Now();
        LoadAllSettings(idx, w.set);
        char err[512] = {};
        if (!w.rend.Init(dev, g_models[idx], err, sizeof err)) {
            g_log("viewmodel: init failed (%ls): %s", g_models[idx], err);
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
    if (w.fxDirty) LoadWeaponFx(idx, dev);
    return true;
}
void RosterStep(ID3D11Device* dev) {
    if (!InterlockedExchange(&g_reqRoster, 0)) return;
    wchar_t path[MAX_PATH * 2];
    wcsncpy(path, g_ini, MAX_PATH * 2 - 1);
    path[MAX_PATH * 2 - 1] = 0;
    wchar_t* slash = wcsrchr(path, L'\\');
    wcscpy(slash ? slash + 1 : path, L"hero_fps_armory.txt");
    char mode[32] = "", owned[1024] = "";
    if (FILE* f = _wfopen(path, L"r")) {
        char line[1100];
        while (fgets(line, sizeof line, f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
            if (!strncmp(line, "mode=", 5)) snprintf(mode, sizeof mode, "%s", line + 5);
            if (!strncmp(line, "owned=", 6)) snprintf(owned, sizeof owned, "%s", line + 6);
        }
        fclose(f);
    }
    bool campaign = !strcmp(mode, "campaign");
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
    LONG n = 0;
    for (int i = 0; i < g_modelCount; ++i) {
        const vm::Settings& s = g_w[i].set;
        if (s.weaponId[0] && (!campaign || s.weaponDefault || ownedHas(s.weaponId))) g_roster[n++] = i;
    }
    if (!n) g_roster[n++] = g_modelIdx;
    InterlockedExchange(&g_rosterN, n);
    g_log("weapon: roster %ld (armory file %s, mode '%s', owned '%s')", n, mode[0] || owned[0] ? "found" : "missing", mode, owned);
    bool curOk = false;
    for (LONG k = 0; k < n; ++k) {
        int i = g_roster[k];
        g_log("weapon:   %ld = model %d %ls (%ls)", k + 1, i, g_models[i], g_w[i].set.weaponId);
        if (i == g_modelIdx) curOk = true;
    }
    g_preloadNext = 0;
    if (!curOk && g_sw.phase == 0 && EnsureWeapon(g_roster[0], dev)) {
        g_modelIdx = g_roster[0];
        InterlockedExchange(&g_wantIdx, g_modelIdx);
        AnimReset(Now());
        MagApply();
        g_log("weapon: current model not usable here -> %ls", g_models[g_modelIdx]);
    }
}
void PreloadStep(ID3D11Device* dev) {
    if (g_preloadNext < 0 || g_sw.phase) return;
    while (g_preloadNext < g_rosterN) {
        int i = g_roster[g_preloadNext++];
        if (!g_w[i].rend.Ready() || g_w[i].fxDirty) {
            EnsureWeapon(i, dev);
            return;
        }
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
            InterlockedExchange(&g_mag.reload, 0);
            InterlockedExchange(&g_mag.reqAnim, 0);
            g_sw.target = k;
            if (g_sw.phase != 1) {
                int c = Wp().rend.FindClip("drop");
                if (c >= 0) Play(c, now, 0.08);
                g_sw.until = now + (c >= 0 ? Wp().rend.ClipDuration(c) * kDropFrac : 0.05);
                g_sw.phase = 1;
            }
        }
    }
    if (g_sw.phase == 1 && now >= g_sw.until) {
        int k = g_sw.target;
        if (EnsureWeapon(k, dev)) {
            g_modelIdx = k;
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
void DrawLayer(IDXGISwapChain* sc) {
    if (g_failed || !(g_testLayer || g_fp)) return;
    double t0 = Now();
    if (InterlockedExchange(&g_reqModelNext, 0) && g_modelCount > 1) {
        int next = ((g_sw.phase == 1 ? g_sw.target : g_modelIdx) + 1) % g_modelCount;
        InterlockedExchange(&g_wantIdx, next);
        InterlockedExchange(&g_reqWeapon, next + 1);
    }
    if (InterlockedExchange(&g_reload, 0)) {
        LoadAllSettings(g_modelIdx, Wp().set);
        Wp().fxDirty = true;
        InterlockedExchange(&g_reqRoster, 1);
        if (Wp().rend.Ready()) MagApply();
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
    if (Wp().rend.Ready() && Wp().rend.Device() != dev) {
        g_log("viewmodel: device changed (%p -> %p); reloading", (void*)Wp().rend.Device(), (void*)dev);
        for (int i = 0; i < g_modelCount; ++i) {
            g_w[i].rend.Release();
            g_w[i].fxDirty = true;
        }
        InterlockedExchange(&g_reqRoster, 1);
    }
    if (!EnsureWeapon(g_modelIdx, dev)) {
        g_failed = true;
        dev->Release();
        return;
    }
    RosterStep(dev);
    SwitchStep(dev);
    PreloadStep(dev);
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
                    vm::Look look = ComputeLook();
                    if (Wp().rend.Draw(ctx, rtv, bb, td.Width, td.Height, hw, Wp().set, look)) ++g_st.draws;
                    float mx = 0, my = 0, ma = 1;
                    bool mv = Wp().rend.Muzzle(&mx, &my, &ma);
                    vm::MuzzleFrame mf;
                    vm::WorldCam wc;
                    bool hasMf = g_fp && Wp().rend.MuzzleView(&mf), hasCam = g_fp && ReadCam(&wc);
                    if (hasMf || hasCam)
                        Wp().flash.Draw(ctx, rtv, Wp().rend.Depth(td.Width, td.Height), td.Width, td.Height, hw, Now(), hasMf ? &mf : nullptr,
                                     hasCam ? &wc : nullptr, Wp().set);
                    if (g_fp && !(g_stance & 1)) g_cross.Draw(ctx, rtv, td.Width, td.Height, hw, g_crossSet);
                    ctx->Release();
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
    ++g_st.frames;
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
        if (!(flags & DXGI_PRESENT_TEST)) DrawLayer(sc);
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
    return ok;
}
void Setup(uintptr_t exeBase, const wchar_t* dllDir, LogFn log) {
    g_base = exeBase;
    g_log = log;
    InitializeCriticalSection(&g_setLock);
    InitializeCriticalSection(&g_hookLock);
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_t0);
    swprintf(g_ini, MAX_PATH, L"%ls\\hero_fps.ini", dllDir);
    g_enabled = GetPrivateProfileIntW(L"viewmodel", L"enabled", 1, g_ini) != 0;
    g_testKey = IniKey(L"test_key", 0);
    g_reloadKey = IniKey(L"reload_key", VK_F8);
    g_animKey = IniKey(L"anim_key", VK_F9);
    g_modelKey = IniKey(L"model_key", VK_F10);
    wchar_t list[MAX_PATH * 4];
    GetPrivateProfileStringW(L"viewmodel", L"models", L"", list, MAX_PATH * 4, g_ini);
    if (!list[0]) GetPrivateProfileStringW(L"viewmodel", L"model", L"hero_fps_vm\\m7\\m7.hvm", list, MAX_PATH * 4, g_ini);
    g_modelCount = 0;
    for (wchar_t* tok = list; *tok && g_modelCount < kMaxModels;) {
        wchar_t* end = wcschr(tok, L';');
        if (end) *end = 0;
        while (*tok == L' ') ++tok;
        if (*tok) swprintf(g_models[g_modelCount++], MAX_PATH * 2, L"%ls\\%ls", dllDir, tok);
        if (!end) break;
        tok = end + 1;
    }
    if (!g_modelCount) swprintf(g_models[g_modelCount++], MAX_PATH * 2, L"%ls\\hero_fps_vm\\m7\\m7.hvm", dllDir);
    for (int i = 0; i < g_modelCount; ++i) LoadAllSettings(i, g_w[i].set);
    log("viewmodel: enabled=%d static=%d keys test=0x%02X reload=0x%02X anim=0x%02X model=0x%02X models=%d first=%ls "
        "fov=%.1f offset=%.3f,%.3f,%.3f rot=%.1f,%.1f,%.1f",
        g_enabled, g_staticOk, g_testKey, g_reloadKey, g_animKey, g_modelKey, g_modelCount, g_models[0], Wp().set.fovDeg,
        Wp().set.offset[0], Wp().set.offset[1], Wp().set.offset[2], Wp().set.rotDeg[0], Wp().set.rotDeg[1], Wp().set.rotDeg[2]);
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
void Fire() {
    if (!Enabled() || !g_fp) return;
    InterlockedExchange(&g_reqFire, 1);
    if (g_mag.size > 0 && InterlockedDecrement(&g_mag.ammo) <= 0) {
        InterlockedExchange(&g_mag.ammo, 0);
        StartMagReload();
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
    if (InterlockedCompareExchange(&g_mag.reload, 0, r) == r) InterlockedExchange(&g_mag.ammo, g_mag.size);
}
bool Reloading() {
    MagTick();
    return g_mag.reload != 0;
}
bool StartMagReload() {
    if (!Enabled() || !g_fp) return false;
    LONG size = g_mag.size;
    if (size <= 0 || g_mag.reload != 0 || g_mag.ammo >= size) return false;
    LONG kind = g_mag.ammo > 0 ? 1 : 2;
    InterlockedExchange64(&g_mag.t0Us, static_cast<LONG64>(Now() * 1e6));
    if (InterlockedCompareExchange(&g_mag.reload, kind, 0) != 0) return false;
    InterlockedExchange(&g_mag.reqAnim, kind);
    return true;
}
bool CanFire() {
    if (g_switching) return false;
    if (g_mag.size <= 0) return true;
    MagTick();
    if (g_mag.reload != 0) return false;
    if (g_mag.ammo <= 0) {
        StartMagReload();
        return false;
    }
    return true;
}
void MagApply() {
    if (g_mag.loadedModel >= 0 && g_mag.loadedModel < 8 && g_mag.size > 0)
        g_mag.ammoByModel[g_mag.loadedModel] = g_mag.reload ? (g_mag.reload == 2 ? 0 : g_mag.ammo) : g_mag.ammo;
    InterlockedExchange(&g_mag.reload, 0);
    InterlockedExchange(&g_mag.reqAnim, 0);
    float rpm = Wp().set.fireRpm;
    InterlockedExchange(&g_mag.fireUs, rpm >= 10.f && rpm <= 2000.f ? static_cast<LONG>(60e6 / rpm) : 0);
    LONG size = Wp().set.magSize > 0 && Wp().set.magSize <= 1000 ? Wp().set.magSize : 0;
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
    LONG keep = g_modelIdx < 8 ? g_mag.ammoByModel[g_modelIdx] : -1;
    LONG ammo = size > 0 ? (keep >= 0 && keep <= size ? keep : size) : 0;
    if (same && size > 0 && g_mag.ammo >= 0 && g_mag.ammo <= size) ammo = g_mag.ammo;
    InterlockedExchange(&g_mag.size, size);
    InterlockedExchange(&g_mag.ammo, ammo);
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
    LONG cnt = g_rosterN;
    if (n < 1 || n > cnt) return;
    LONG idx = g_roster[n - 1];
    InterlockedExchange(&g_wantIdx, idx);
    InterlockedExchange(&g_reqWeapon, idx + 1);
}
void CycleWeapon(int dir) {
    if (!Enabled() || !g_fp) return;
    LONG cnt = g_rosterN;
    if (cnt < 2) return;
    LONG want = g_wantIdx, pos = 0;
    for (LONG k = 0; k < cnt; ++k)
        if (g_roster[k] == want) pos = k;
    pos = (pos + (dir > 0 ? 1 : cnt - 1)) % cnt;
    LONG idx = g_roster[pos];
    InterlockedExchange(&g_wantIdx, idx);
    InterlockedExchange(&g_reqWeapon, idx + 1);
}
bool Busy() { return g_switching != 0 || Reloading(); }
int CurrentWeapon() { return g_modelIdx; }
const char* WeaponProjectile(int idx) { return idx >= 0 && idx < g_modelCount ? g_w[idx].set.projectile : ""; }
void PublishStance(bool ads, bool sprint, bool tac) {
    InterlockedExchange(&g_stance, (ads ? 1 : 0) | (sprint ? 2 : 0) | (tac ? 4 : 0));
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
