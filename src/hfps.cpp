#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" {
void* p_GetFileVersionInfoA;
void* p_GetFileVersionInfoByHandle;
void* p_GetFileVersionInfoExA;
void* p_GetFileVersionInfoExW;
void* p_GetFileVersionInfoSizeA;
void* p_GetFileVersionInfoSizeExA;
void* p_GetFileVersionInfoSizeExW;
void* p_GetFileVersionInfoSizeW;
void* p_GetFileVersionInfoW;
void* p_VerFindFileA;
void* p_VerFindFileW;
void* p_VerInstallFileA;
void* p_VerInstallFileW;
void* p_VerLanguageNameA;
void* p_VerLanguageNameW;
void* p_VerQueryValueA;
void* p_VerQueryValueW;
void* g_orig_cam_update;
void hfps_cam_update_thunk();
void hfps_after_cam_update(uintptr_t mgr);
}
namespace {
constexpr DWORD kTimeDateStamp = 0x6ac6d907;
constexpr DWORD kSizeOfImage = 0xf2be000;
constexpr uintptr_t kGlobalUi = 0x4479e50;
constexpr uintptr_t kVtCamMgr = 0x3ab9918;
constexpr uintptr_t kFnCamMgrUpdate = 0x2f18c64;
constexpr uintptr_t kVtFpCam = 0x3aac9a8;
constexpr uintptr_t kFnCamSetPos = 0x181bc44;
constexpr uintptr_t kFnCamSetTarget = 0x181bcb4;
constexpr uintptr_t kFnCamSetUp = 0x181bd28;
constexpr uintptr_t kFnCamSetFov = 0x8d5ba0;
constexpr uintptr_t kFnCamSetNear = 0x8d5b40;
constexpr uintptr_t kFnCamSetFar = 0x8d5b10;
const unsigned char kUpdatePrologue[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68,
                                         0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x30, 0x03};
constexpr uintptr_t oUiBattle = 0x418, oUiSelection = 0x13b8, oSelCount = 0x1bc, oSelArray = 0x1c0,
                    oUiUnitBattleUnit = 0x758;
constexpr uintptr_t oBattleCamMgr = 0x64558, oBattleFpCtrl = 0x647a0;
constexpr uintptr_t oMgrBattle = 0x3e0, oMgrActive = 0xc30, oMgrTransition = 0xc10, oMgrSpecial = 0xc48;
constexpr uintptr_t oFpCtrlCamera = 0x60, oFpCtrlEntity = 0x518;
constexpr uintptr_t oCamPos = 0x8, oCamTarget = 0x14, oCamFov = 0x24, oCamNear = 0x34, oCamFar = 0x38,
                    oCamDone = 0x3f4;
constexpr uintptr_t oUnitEntCount = 0x134, oUnitEntArray = 0x138, oEntPos = 0x88;
constexpr uintptr_t vtSetPos = 0xb8, vtSetTarget = 0xc0, vtSetUp = 0xc8, vtSetFov = 0xd0, vtSetNear = 0x90,
                    vtSetFar = 0x98, vtMgrUpdate = 0x298;
constexpr size_t kVtCopySlots = 0x100;
constexpr float kPi = 3.14159265358979f;
struct Config {
    bool enabled = true;
    UINT toggleKey = VK_F6;
    float eyeHeight = 1.75f;
    float eyeForward = 0.12f;
    float nearClip = 0.1f;
    float fovOverride = 0.0f;
    float sensitivity = 0.07f;
    bool invertY = false;
    int logIntervalMs = 1000;
} cfg;
wchar_t g_dir[MAX_PATH];
uintptr_t g_base = 0;
bool g_coreOk = false;
CRITICAL_SECTION g_logLock;
FILE* g_log = nullptr;
void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d t%lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
            GetCurrentThreadId());
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}
void OpenLog() {
    InitializeCriticalSection(&g_logLock);
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
    if (n && n < MAX_PATH - 80) {
        wcscat(path, L"\\The Creative Assembly\\Warhammer3\\logs\\hero_fps_native.log");
        g_log = _wfopen(path, L"a");
    }
    if (!g_log) {
        wcscpy(path, g_dir);
        wcscat(path, L"\\hero_fps_native.log");
        g_log = _wfopen(path, L"a");
    }
}
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
template <class T>
bool Wr(uintptr_t a, const T& v) {
    if (a < 0x10000) return false;
    SIZE_T n = 0;
    return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(a), &v, sizeof(T), &n) &&
           n == sizeof(T);
}
bool InExe(uintptr_t p) { return p >= g_base && p < g_base + kSizeOfImage; }
float IniF(const wchar_t* sec, const wchar_t* key, float def, const wchar_t* ini) {
    wchar_t buf[64];
    GetPrivateProfileStringW(sec, key, L"", buf, 64, ini);
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    return (end != buf && std::isfinite(v)) ? static_cast<float>(v) : def;
}
void LoadConfig() {
    wchar_t ini[MAX_PATH];
    wcscpy(ini, g_dir);
    wcscat(ini, L"\\hero_fps.ini");
    cfg.enabled = GetPrivateProfileIntW(L"general", L"enabled", 1, ini) != 0;
    UINT key = GetPrivateProfileIntW(L"general", L"toggle_key", VK_F6, ini);
    cfg.toggleKey = (key > 0 && key < 0xFF) ? key : VK_F6;
    cfg.eyeHeight = IniF(L"camera", L"eye_height", cfg.eyeHeight, ini);
    cfg.eyeForward = IniF(L"camera", L"eye_forward", cfg.eyeForward, ini);
    cfg.nearClip = IniF(L"camera", L"near_clip", cfg.nearClip, ini);
    cfg.fovOverride = IniF(L"camera", L"fov_override", cfg.fovOverride, ini);
    cfg.sensitivity = IniF(L"mouse", L"sensitivity", cfg.sensitivity, ini);
    cfg.invertY = GetPrivateProfileIntW(L"mouse", L"invert_y", 0, ini) != 0;
    cfg.logIntervalMs = GetPrivateProfileIntW(L"debug", L"log_interval_ms", 1000, ini);
    if (cfg.eyeHeight < 0.2f || cfg.eyeHeight > 20.f) cfg.eyeHeight = 1.75f;
    if (cfg.nearClip < 0.01f || cfg.nearClip > 5.f) cfg.nearClip = 0.1f;
    if (cfg.sensitivity <= 0.f || cfg.sensitivity > 5.f) cfg.sensitivity = 0.07f;
    if (cfg.logIntervalMs < 200) cfg.logIntervalMs = 200;
    Log("config enabled=%d toggle=0x%02X eye_height=%.2f eye_forward=%.2f near=%.3f fov_override=%.3f sens=%.3f invert_y=%d",
        cfg.enabled, cfg.toggleKey, cfg.eyeHeight, cfg.eyeForward, cfg.nearClip, cfg.fovOverride,
        cfg.sensitivity, cfg.invertY);
}
using FnSetVec = void (*)(uintptr_t self, const float* v);
using FnSetF = void (*)(uintptr_t self, float v);
bool FpCamOk(uintptr_t cam) { return cam && Ptr(cam) == g_base + kVtFpCam; }
void CamSetVec(uintptr_t cam, uintptr_t slot, const float* v) {
    reinterpret_cast<FnSetVec>(Ptr(g_base + kVtFpCam + slot))(cam, v);
}
void CamSetF(uintptr_t cam, uintptr_t slot, float v) {
    reinterpret_cast<FnSetF>(Ptr(g_base + kVtFpCam + slot))(cam, v);
}
uintptr_t SelectedBattleUnit(int* countOut) {
    uintptr_t ui = Ptr(g_base + kGlobalUi);
    uintptr_t sel = ui ? Ptr(ui + oUiSelection) : 0;
    int count = 0;
    if (!sel || !Rd(sel + oSelCount, count)) return 0;
    if (countOut) *countOut = count;
    if (count < 1) return 0;
    uintptr_t arr = Ptr(sel + oSelArray);
    uintptr_t uiUnit = arr ? Ptr(arr) : 0;
    return uiUnit ? Ptr(uiUnit + oUiUnitBattleUnit) : 0;
}
uintptr_t FirstEntity(uintptr_t unit, unsigned* countOut) {
    unsigned count = 0;
    if (!unit || !Rd(unit + oUnitEntCount, count) || count == 0 || count > 2000) return 0;
    if (countOut) *countOut = count;
    uintptr_t arr = Ptr(unit + oUnitEntArray);
    return arr ? Ptr(arr) : 0;
}
bool ReadVec(uintptr_t a, float v[3]) {
    if (!Rd(a, v[0]) || !Rd(a + 4, v[1]) || !Rd(a + 8, v[2])) return false;
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && std::fabs(v[0]) < 1e6f &&
           std::fabs(v[1]) < 1e6f && std::fabs(v[2]) < 1e6f;
}
struct State {
    volatile bool active = false;
    uintptr_t mgr = 0, battle = 0, fpCtrl = 0, cam = 0, prevActive = 0, unit = 0, entity = 0;
    float yaw = 0, pitch = 0;
    float outPos[3] = {}, from[3] = {}, to[3] = {}, lastRaw[3] = {};
    double tChange = 0, tickDur = 0.1, tLastFrame = 0;
    bool havePos = false, useRaw = false, rawRegistered = false, skipFirstCursor = true;
    unsigned entCount = 0;
    double tStat = 0;
    unsigned frames = 0, writes = 0, yields = 0;
    long sumDx = 0, sumDy = 0;
} S;
volatile LONG g_reqToggle = 0, g_reqExit = 0;
volatile LONG g_rawDx = 0, g_rawDy = 0;
HWND g_hwnd = nullptr;
WNDPROC g_prevProc = nullptr;
DWORD g_windowThread = 0;
uintptr_t* g_vtCopy = nullptr;
uintptr_t g_installedMgr = 0;
bool g_loggedUpdateThread = false;
LARGE_INTEGER g_qpf;
double Now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(g_qpf.QuadPart);
}
bool Foreground() { return g_hwnd && GetForegroundWindow() == g_hwnd; }
POINT ClientCenterScreen() {
    RECT r{};
    GetClientRect(g_hwnd, &r);
    POINT c{(r.right - r.left) / 2, (r.bottom - r.top) / 2};
    ClientToScreen(g_hwnd, &c);
    return c;
}
void SetupMouse() {
    S.useRaw = false;
    UINT n = 0;
    GetRegisteredRawInputDevices(nullptr, &n, sizeof(RAWINPUTDEVICE));
    bool mouseTaken = false;
    if (n > 0 && n < 64) {
        RAWINPUTDEVICE list[64];
        UINT m = n;
        if (GetRegisteredRawInputDevices(list, &m, sizeof(RAWINPUTDEVICE)) != static_cast<UINT>(-1))
            for (UINT i = 0; i < m; ++i)
                if (list[i].usUsagePage == 1 && list[i].usUsage == 2) mouseTaken = true;
    }
    if (mouseTaken) {
        Log("mouse: raw input already registered in process (%u devices) -> cursor-delta fallback", n);
        return;
    }
    RAWINPUTDEVICE rid{1, 2, 0, g_hwnd};
    if (RegisterRawInputDevices(&rid, 1, sizeof rid)) {
        S.useRaw = true;
        S.rawRegistered = true;
        Log("mouse: raw input registered on hwnd=%p", g_hwnd);
    } else {
        Log("mouse: RegisterRawInputDevices failed err=%lu -> cursor-delta fallback", GetLastError());
    }
}
void ReleaseMouse() {
    if (S.rawRegistered) {
        RAWINPUTDEVICE rid{1, 2, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&rid, 1, sizeof rid);
        S.rawRegistered = false;
    }
    S.useRaw = false;
    InterlockedExchange(&g_rawDx, 0);
    InterlockedExchange(&g_rawDy, 0);
}
void Exit(uintptr_t mgr, const char* why) {
    if (!S.active) return;
    S.active = false;
    uintptr_t active = Ptr(mgr + oMgrActive);
    bool restored = false;
    if (active == S.cam && S.prevActive && InExe(Ptr(S.prevActive))) restored = Wr(mgr + oMgrActive, S.prevActive);
    ReleaseMouse();
    Log("FPS exit reason=%s restoredPrev=%d active=%p prev=%p", why, restored, (void*)active,
        (void*)S.prevActive);
}
void Enter(uintptr_t mgr) {
    uintptr_t battle = Ptr(mgr + oMgrBattle);
    uintptr_t ui = Ptr(g_base + kGlobalUi);
    uintptr_t battle2 = ui ? Ptr(ui + oUiBattle) : 0;
    uintptr_t fpCtrl = battle ? Ptr(battle + oBattleFpCtrl) : 0;
    uintptr_t cam = fpCtrl ? fpCtrl + oFpCtrlCamera : 0;
    if (!battle || battle != battle2 || !fpCtrl) {
        Log("enter refused: battle=%p ui.battle=%p fpCtrl=%p", (void*)battle, (void*)battle2, (void*)fpCtrl);
        return;
    }
    if (Ptr(fpCtrl + oFpCtrlEntity)) {
        Log("enter refused: native artillery first-person mode is active");
        return;
    }
    if (!FpCamOk(cam)) {
        Log("enter refused: camera vtable mismatch %p", (void*)Ptr(cam));
        return;
    }
    int selCount = 0;
    uintptr_t unit = SelectedBattleUnit(&selCount);
    unsigned entCount = 0;
    uintptr_t entity = FirstEntity(unit, &entCount);
    float pos[3];
    if (!unit || !entity || !ReadVec(entity + oEntPos, pos)) {
        Log("enter refused: selection count=%d unit=%p entity=%p (select one unit first)", selCount, (void*)unit,
            (void*)entity);
        return;
    }
    uintptr_t prev = Ptr(mgr + oMgrActive);
    if (!prev || !InExe(Ptr(prev))) {
        Log("enter refused: active camera %p looks invalid", (void*)prev);
        return;
    }
    float cp[3], ct[3];
    S.yaw = 0;
    if (ReadVec(prev + oCamPos, cp) && ReadVec(prev + oCamTarget, ct)) {
        float dx = ct[0] - cp[0], dz = ct[2] - cp[2];
        if (dx * dx + dz * dz > 1e-6f) S.yaw = std::atan2(dx, dz);
    }
    S.pitch = 0;
    float fov = 0, farClip = 0, prevNear = 0;
    Rd(prev + oCamFov, fov);
    Rd(prev + oCamFar, farClip);
    Rd(prev + oCamNear, prevNear);
    if (cfg.fovOverride > 0) fov = cfg.fovOverride;
    if (std::isfinite(fov) && fov > 0) CamSetF(cam, vtSetFov, fov);
    CamSetF(cam, vtSetNear, cfg.nearClip);
    if (std::isfinite(farClip) && farClip > 10.f) CamSetF(cam, vtSetFar, farClip);
    unsigned char done = 0;
    Rd(cam + oCamDone, done);
    if (done) Wr<unsigned char>(cam + oCamDone, 0);
    S.mgr = mgr;
    S.battle = battle;
    S.fpCtrl = fpCtrl;
    S.cam = cam;
    S.prevActive = prev;
    S.unit = unit;
    S.entity = entity;
    S.entCount = entCount;
    S.havePos = false;
    S.skipFirstCursor = true;
    S.tStat = Now();
    S.frames = S.writes = S.yields = 0;
    S.sumDx = S.sumDy = 0;
    InterlockedExchange(&g_rawDx, 0);
    InterlockedExchange(&g_rawDy, 0);
    SetupMouse();
    S.active = true;
    Log("FPS enter mgr=%p battle=%p fpCtrl=%p cam=%p prevActive=%p(vt %p) unit=%p entities=%u entity=%p "
        "pos=%.2f,%.2f,%.2f yaw=%.1f fov=%.4f near=%.3f(prev %.3f) far=%.1f camDone=%u selCount=%d",
        (void*)mgr, (void*)battle, (void*)fpCtrl, (void*)cam, (void*)prev, (void*)Ptr(prev), (void*)unit,
        entCount, (void*)entity, pos[0], pos[1], pos[2], S.yaw * 180.f / kPi, fov, cfg.nearClip, prevNear,
        farClip, done, selCount);
}
void Apply(uintptr_t mgr) {
    double now = Now();
    unsigned cnt = 0;
    uintptr_t ent = FirstEntity(S.unit, &cnt);
    float raw[3];
    if (ent != S.entity || !ReadVec(S.entity + oEntPos, raw) || Ptr(mgr + oMgrBattle) != S.battle) {
        Exit(mgr, "unit-invalid");
        return;
    }
    if (Ptr(S.fpCtrl + oFpCtrlEntity)) {
        Exit(mgr, "artillery-fp-started");
        return;
    }
    if (!FpCamOk(S.cam)) {
        Exit(mgr, "camera-invalid");
        return;
    }
    if (!S.havePos) {
        memcpy(S.outPos, raw, sizeof raw);
        memcpy(S.from, raw, sizeof raw);
        memcpy(S.to, raw, sizeof raw);
        memcpy(S.lastRaw, raw, sizeof raw);
        S.tChange = now;
        S.havePos = true;
    } else if (raw[0] != S.lastRaw[0] || raw[1] != S.lastRaw[1] || raw[2] != S.lastRaw[2]) {
        double interval = now - S.tChange;
        if (interval > 0.02 && interval < 0.5) S.tickDur = S.tickDur * 0.7 + interval * 0.3;
        memcpy(S.from, S.outPos, sizeof raw);
        memcpy(S.to, raw, sizeof raw);
        memcpy(S.lastRaw, raw, sizeof raw);
        S.tChange = now;
    }
    double k = (now - S.tChange) / (S.tickDur > 0.03 ? S.tickDur : 0.03);
    if (k > 1) k = 1;
    for (int i = 0; i < 3; ++i) S.outPos[i] = S.from[i] + static_cast<float>((S.to[i] - S.from[i]) * k);
    long dx = 0, dy = 0;
    if (Foreground()) {
        if (S.useRaw) {
            dx = InterlockedExchange(&g_rawDx, 0);
            dy = InterlockedExchange(&g_rawDy, 0);
        } else {
            POINT c = ClientCenterScreen(), p{};
            if (GetCursorPos(&p) && !S.skipFirstCursor) {
                dx = p.x - c.x;
                dy = p.y - c.y;
            }
            S.skipFirstCursor = false;
            SetCursorPos(c.x, c.y);
        }
        if (S.useRaw) {
            POINT c = ClientCenterScreen();
            SetCursorPos(c.x, c.y);
        }
    } else {
        InterlockedExchange(&g_rawDx, 0);
        InterlockedExchange(&g_rawDy, 0);
        S.skipFirstCursor = true;
    }
    float rad = cfg.sensitivity * kPi / 180.f;
    S.yaw += dx * rad;
    S.pitch += (cfg.invertY ? dy : -dy) * rad;
    const float lim = 85.f * kPi / 180.f;
    if (S.pitch > lim) S.pitch = lim;
    if (S.pitch < -lim) S.pitch = -lim;
    if (S.yaw > kPi) S.yaw -= 2 * kPi;
    if (S.yaw < -kPi) S.yaw += 2 * kPi;
    S.sumDx += dx;
    S.sumDy += dy;
    float sy = std::sin(S.yaw), cy = std::cos(S.yaw), sp = std::sin(S.pitch), cpi = std::cos(S.pitch);
    float eye[3] = {S.outPos[0] + sy * cfg.eyeForward, S.outPos[1] + cfg.eyeHeight, S.outPos[2] + cy * cfg.eyeForward};
    float target[3] = {eye[0] + sy * cpi * 10.f, eye[1] + sp * 10.f, eye[2] + cy * cpi * 10.f};
    const float up[3] = {0.f, 1.f, 0.f};
    CamSetVec(S.cam, vtSetUp, up);
    CamSetVec(S.cam, vtSetPos, eye);
    CamSetVec(S.cam, vtSetTarget, target);
    uintptr_t active = Ptr(mgr + oMgrActive);
    uintptr_t trans = Ptr(mgr + oMgrTransition), special = Ptr(mgr + oMgrSpecial);
    if (active != S.cam) {
        if (active && (active == trans || active == special)) {
            ++S.yields;
        } else if (Wr(mgr + oMgrActive, S.cam)) {
            ++S.writes;
        }
    }
    ++S.frames;
    if ((now - S.tStat) * 1000.0 >= cfg.logIntervalMs) {
        Log("sec frames=%u activeWrites=%u yields=%u ent=%.2f,%.2f,%.2f eye=%.2f,%.2f,%.2f yaw=%.1f pitch=%.1f "
            "dx=%ld dy=%ld input=%s tick=%.3f active=%p",
            S.frames, S.writes, S.yields, raw[0], raw[1], raw[2], eye[0], eye[1], eye[2], S.yaw * 180.f / kPi,
            S.pitch * 180.f / kPi, S.sumDx, S.sumDy, S.useRaw ? "raw" : "cursor", S.tickDur,
            (void*)Ptr(mgr + oMgrActive));
        S.tStat = now;
        S.frames = S.writes = S.yields = 0;
        S.sumDx = S.sumDy = 0;
    }
}
bool InstallHook() {
    uintptr_t ui = Ptr(g_base + kGlobalUi);
    uintptr_t battle = ui ? Ptr(ui + oUiBattle) : 0;
    uintptr_t mgr = battle ? Ptr(battle + oBattleCamMgr) : 0;
    if (!mgr) {
        Log("toggle ignored: no battle camera manager (ui=%p battle=%p)", (void*)ui, (void*)battle);
        return false;
    }
    uintptr_t vt = Ptr(mgr);
    uintptr_t ours = g_vtCopy ? reinterpret_cast<uintptr_t>(g_vtCopy + 2) : 0;
    if (ours && vt == ours) return true;
    if (vt != g_base + kVtCamMgr) {
        Log("camera manager %p has unexpected vtable %p; not hooking", (void*)mgr, (void*)vt);
        return false;
    }
    if (!g_vtCopy) {
        g_vtCopy = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (kVtCopySlots + 2) * sizeof(uintptr_t),
                                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!g_vtCopy) return false;
        for (size_t i = 0; i < kVtCopySlots + 2; ++i) g_vtCopy[i] = Ptr(g_base + kVtCamMgr - 16 + i * 8);
        g_orig_cam_update = reinterpret_cast<void*>(g_vtCopy[2 + vtMgrUpdate / 8]);
        g_vtCopy[2 + vtMgrUpdate / 8] = reinterpret_cast<uintptr_t>(&hfps_cam_update_thunk);
        DWORD old;
        VirtualProtect(g_vtCopy, (kVtCopySlots + 2) * sizeof(uintptr_t), PAGE_READONLY, &old);
        ours = reinterpret_cast<uintptr_t>(g_vtCopy + 2);
    }
    if (!Wr(mgr, ours)) {
        Log("hook write failed on camera manager %p", (void*)mgr);
        return false;
    }
    g_installedMgr = mgr;
    Log("hooked battle camera manager %p (battle %p) via vtable copy %p", (void*)mgr, (void*)battle, (void*)ours);
    return true;
}
LRESULT CALLBACK HfpsWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INPUT:
        if (S.active && S.useRaw) {
            RAWINPUT ri;
            UINT size = sizeof ri;
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) !=
                    static_cast<UINT>(-1) &&
                ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                InterlockedExchangeAdd(&g_rawDx, ri.data.mouse.lLastX);
                InterlockedExchangeAdd(&g_rawDy, ri.data.mouse.lLastY);
            }
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (w == cfg.toggleKey) {
            if (!(l & (1L << 30)) && (S.active || InstallHook())) InterlockedExchange(&g_reqToggle, 1);
            return 0;
        }
        if (w == VK_ESCAPE && S.active) InterlockedExchange(&g_reqExit, 1);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (w == cfg.toggleKey) return 0;
        break;
    case WM_SETCURSOR:
        if (S.active && LOWORD(l) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_ACTIVATEAPP:
        if (!w && S.active) InterlockedExchange(&g_reqExit, 1);
        break;
    default:
        break;
    }
    return CallWindowProcW(g_prevProc, h, m, w, l);
}
struct FindCtx {
    DWORD pid;
    HWND best;
    long area;
};
BOOL CALLBACK FindMain(HWND h, LPARAM p) {
    auto* c = reinterpret_cast<FindCtx*>(p);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT r{};
    GetClientRect(h, &r);
    long a = (r.right - r.left) * (r.bottom - r.top);
    if (a > c->area) {
        c->area = a;
        c->best = h;
    }
    return TRUE;
}
bool HostIsWh3() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(GetModuleHandleW(nullptr), path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"Warhammer3.exe") == 0;
}
bool VerifyGame() {
    HMODULE exe = GetModuleHandleW(nullptr);
    g_base = reinterpret_cast<uintptr_t>(exe);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g_base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kTimeDateStamp || nt->OptionalHeader.SizeOfImage != kSizeOfImage) {
        Log("Warhammer3.exe version mismatch: stamp=0x%08lX size=0x%lX (expected 0x%08lX/0x%lX); module inactive",
            nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage, kTimeDateStamp, kSizeOfImage);
        return false;
    }
    unsigned char pro[sizeof kUpdatePrologue];
    bool ok = true;
    for (size_t i = 0; i < sizeof pro; ++i) ok = ok && Rd(g_base + kFnCamMgrUpdate + i, pro[i]);
    ok = ok && !memcmp(pro, kUpdatePrologue, sizeof pro);
    struct {
        uintptr_t vt, slot, fn;
    } checks[] = {{kVtCamMgr, vtMgrUpdate, kFnCamMgrUpdate}, {kVtFpCam, vtSetPos, kFnCamSetPos},
                  {kVtFpCam, vtSetTarget, kFnCamSetTarget}, {kVtFpCam, vtSetUp, kFnCamSetUp},
                  {kVtFpCam, vtSetFov, kFnCamSetFov}, {kVtFpCam, vtSetNear, kFnCamSetNear},
                  {kVtFpCam, vtSetFar, kFnCamSetFar}};
    for (auto& c : checks) {
        uintptr_t got = Ptr(g_base + c.vt + c.slot);
        if (got != g_base + c.fn) {
            Log("signature check failed: vt %llx +%llx = %llx (expected %llx)", (unsigned long long)c.vt,
                (unsigned long long)c.slot, (unsigned long long)(got - g_base), (unsigned long long)c.fn);
            ok = false;
        }
    }
    if (!ok) Log("Warhammer3.exe code/vtable signatures do not match; module inactive");
    return ok;
}
DWORD WINAPI InitThread(LPVOID) {
    if (!HostIsWh3()) return 0;
    OpenLog();
    Log("==== hero_fps native M1 loaded (version.dll proxy) pid=%lu", GetCurrentProcessId());
    QueryPerformanceFrequency(&g_qpf);
    if (!VerifyGame()) return 0;
    LoadConfig();
    if (!cfg.enabled) {
        Log("disabled by hero_fps.ini");
        return 0;
    }
    g_coreOk = true;
    FindCtx ctx{GetCurrentProcessId(), nullptr, 0};
    for (int i = 0; i < 6000 && !ctx.best; ++i) {
        Sleep(100);
        ctx.area = 0;
        EnumWindows(FindMain, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.best && ctx.area < 320 * 200) ctx.best = nullptr;
    }
    if (!ctx.best) {
        Log("game window not found; module idle");
        return 0;
    }
    Sleep(1500);
    wchar_t cls[128] = {}, title[128] = {};
    GetClassNameW(ctx.best, cls, 128);
    GetWindowTextW(ctx.best, title, 128);
    g_windowThread = GetWindowThreadProcessId(ctx.best, nullptr);
    g_hwnd = ctx.best;
    g_prevProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(ctx.best, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HfpsWndProc)));
    Log("window hwnd=%p class=%ls title=%ls thread=%lu prevProc=%p", ctx.best, cls, title, g_windowThread,
        (void*)g_prevProc);
    if (!g_prevProc) {
        Log("subclass failed err=%lu", GetLastError());
        return 0;
    }
    Log("ready: select one unit in battle and press toggle key 0x%02X", cfg.toggleKey);
    return 0;
}
bool LoadRealVersion() {
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (!n || n > MAX_PATH - 20) return false;
    wcscat(sys, L"\\version.dll");
    HMODULE real = LoadLibraryW(sys);
    if (!real) return false;
#define RESOLVE(name) p_ ##name = reinterpret_cast<void*>(GetProcAddress(real, #name))
    RESOLVE(GetFileVersionInfoA);
    RESOLVE(GetFileVersionInfoByHandle);
    RESOLVE(GetFileVersionInfoExA);
    RESOLVE(GetFileVersionInfoExW);
    RESOLVE(GetFileVersionInfoSizeA);
    RESOLVE(GetFileVersionInfoSizeExA);
    RESOLVE(GetFileVersionInfoSizeExW);
    RESOLVE(GetFileVersionInfoSizeW);
    RESOLVE(GetFileVersionInfoW);
    RESOLVE(VerFindFileA);
    RESOLVE(VerFindFileW);
    RESOLVE(VerInstallFileA);
    RESOLVE(VerInstallFileW);
    RESOLVE(VerLanguageNameA);
    RESOLVE(VerLanguageNameW);
    RESOLVE(VerQueryValueA);
    RESOLVE(VerQueryValueW);
#undef RESOLVE
    return true;
}
}
extern "C" void hfps_after_cam_update(uintptr_t mgr) {
    if (!g_loggedUpdateThread) {
        g_loggedUpdateThread = true;
        Log("camera update runs on thread %lu (window thread %lu)", GetCurrentThreadId(), g_windowThread);
    }
    if (S.active && S.mgr != mgr) {
        S.active = false;
        ReleaseMouse();
        Log("FPS state dropped: camera manager changed %p -> %p", (void*)S.mgr, (void*)mgr);
    }
    if (InterlockedExchange(&g_reqToggle, 0)) {
        if (S.active)
            Exit(mgr, "toggle-key");
        else
            Enter(mgr);
    }
    if (InterlockedExchange(&g_reqExit, 0)) Exit(mgr, "escape-or-focus");
    if (S.active) Apply(mgr);
}
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, g_dir, MAX_PATH);
        wchar_t* slash = wcsrchr(g_dir, L'\\');
        if (slash) *slash = 0;
        if (!LoadRealVersion()) return FALSE;
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
