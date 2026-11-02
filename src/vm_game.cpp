#include "vm_game.h"
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <d3d11_4.h>
#include <tlhelp32.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "vm_audio.h"
#include "vm_render.h"
namespace vmgame {
void MagApply();
void RefillForNewBattle();
void AmmoRefillStep();
bool CsvHas(const char* list, const char* key) {
    size_t lk = strlen(key);
    for (const char* p = list; lk && *p;) {
        const char* e = p;
        while (*e && *e != ',' && *e != ';') ++e;
        const char* a = p;
        while (a < e && *a == ' ') ++a;
        const char* b = e;
        while (b > a && b[-1] == ' ') --b;
        if (static_cast<size_t>(b - a) == lk && !strncmp(a, key, lk)) return true;
        p = *e ? e + 1 : e;
    }
    return false;
}
bool CsvHasW(const wchar_t* list, const char* key) {
    char a[512];
    snprintf(a, sizeof a, "%ls", list);
    return CsvHas(a, key);
}
void CsvAppend(char* list, size_t cap, const wchar_t* add) {
    char a[512];
    snprintf(a, sizeof a, "%ls", add);
    for (char* p = a; *p;) {
        char* e = p;
        while (*e && *e != ',' && *e != ';') ++e;
        char c = *e;
        *e = 0;
        while (*p == ' ') ++p;
        for (char* t = p + strlen(p); t > p && t[-1] == ' ';) *--t = 0;
        if (*p && !CsvHas(list, p) && strlen(list) + strlen(p) + 2 < cap) {
            if (list[0]) strcat(list, ",");
            strcat(list, p);
        }
        p = c ? e + 1 : e;
    }
}
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
const wchar_t* const kMeleeCatSec[kMeleeCats] = {L"melee_shield_spear", L"melee_sword", L"melee_hammer", L"melee_halberd"};
float g_catBlock[kMeleeCats] = {-1.f, -1.f, -1.f, -1.f};
static_assert(vm::Settings::kMeleeOwnN == kMeleeOwnN, "vm_render.h 和 vm_game.h 的 kMeleeOwnN 要一样");
struct MeleeOwnPub {
    SRWLOCK lock = SRWLOCK_INIT;
    float v[kMeleeOwnN] = {};
    bool any = false;
} g_meleeOwnPub;
void LoadCatBlocks() {
    for (int c = 0; c < kMeleeCats; ++c) {
        wchar_t buf[64];
        GetPrivateProfileStringW(kMeleeCatSec[c], L"block", L"", buf, 64, g_ini);
        wchar_t* end = nullptr;
        double v = buf[0] ? wcstod(buf, &end) : -1.0;
        g_catBlock[c] = (buf[0] && end != buf && v >= 0.0 && v <= 1.0) ? static_cast<float>(v) : -1.f;
    }
}
constexpr int kMaxModels = 200;
wchar_t g_models[kMaxModels][MAX_PATH * 2];
int g_modelCount = 0;
int g_modelIdx = 0;
volatile bool g_testLayer = false, g_fp = false, g_failed = false;
volatile LONG g_reload = 0, g_reqModelNext = 0, g_reqAnimNext = 0, g_reqFire = 0, g_reqDraw = 0;
volatile LONG g_reqFireLast = 0;
struct ShotQ {
    struct E {
        LONG burst, k;
        double t;
    } e[16];
    volatile LONG w = 0;
    LONG r = 0;
} g_shotQ;
struct ShotChain {
    double lastIdeal = 0, lastT = 0;
    LONG burst = 0, k = 0;
} g_chain;
struct MagState {
    volatile LONG size = 0;
    volatile LONG ammo = 0;
    volatile LONG reload = 0;
    volatile LONG reqAnim = 0;
    volatile LONG64 t0Us = 0;
    volatile LONG refillUs[3] = {};
    volatile LONG fireUs = 0;
    volatile LONG perShot = 1;
    volatile LONG disposable = 0;
    volatile LONG reserve = -1;
    volatile LONG shellMode = 0;
    volatile LONG shellStop = 0;
    volatile LONG shellWait = 0;
    volatile LONG trigUp = 1;
    volatile LONG64 firePendUs = 0;
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
volatile LONG g_jlCrouchMilli = 0;
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
vm::CastMarkerStyle g_cmStyle;
vm::CastMarker g_cmR;
struct CastMarkerPub {
    SRWLOCK lock = SRWLOCK_INIT;
    bool on = false;
    LONG seq = 0;
    cm::Frame f;
} g_cmPub;
cm::Frame g_cmDraw;
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
wchar_t g_gunShield[520] = L"";
volatile LONG g_gunShieldOn = 0;
LONG g_gunShieldGen = 1;
LONG g_setShieldGen[kMaxModels] = {};
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
void LoadShieldIni(int idx, vm::Settings& s, ULONGLONG* size, FILETIME* time) {
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
    StampFile(shIni, size, time);
    vm::LoadSettings(shIni, s);
    wcsncpy(s.shieldModel, full, 519);
}
void ApplyGunShield(int idx, vm::Settings& s) {
    if (idx < 0 || idx >= g_modelCount || g_setShieldGen[idx] == g_gunShieldGen) return;
    g_setShieldGen[idx] = g_gunShieldGen;
    if (!s.shieldModel[0] || !s.shieldAllow || !g_gunShield[0]) return;
    wcsncpy(s.shieldModel, g_gunShield, 519);
    s.shieldModel[519] = 0;
    LoadShieldIni(idx, s, &g_setStamp[idx].size[1], &g_setStamp[idx].time[1]);
    wchar_t side[MAX_PATH * 2];
    SidecarIni(idx, side);
    vm::LoadSettings(side, s);
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
    vm::LoadCastMarkerStyle(g_ini, g_cmStyle);
    s = vm::Settings();
    vm::LoadSettings(g_ini, s);
    LoadCatBlocks();
    if (s.shieldModel[0] && g_gunShield[0]) wcsncpy(s.shieldModel, g_gunShield, 519), s.shieldModel[519] = 0;
    g_setShieldGen[idx] = g_gunShieldGen;
    if (s.shieldModel[0]) LoadShieldIni(idx, s, &st.size[1], &st.time[1]);
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
struct HitMarkState {
    SRWLOCK lock = SRWLOCK_INIT;
    double tHit = -1.0, tKill = -1.0;
    int hits = 0, kills = 0;
} g_mark;
int g_markHitFrames = 0, g_markKillFrames = 0;
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
using FnResize = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using FnResize1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
FnResize g_origResize = nullptr;
FnResize1 g_origResize1 = nullptr;
const GUID kIidSwapChain3 = {0x94d99bdb, 0xf1f8, 0x4ab0, {0xb2, 0x36, 0x7d, 0xa0, 0x17, 0x0e, 0xda, 0xb1}};
uintptr_t* g_scVtCopy = nullptr;
uintptr_t g_scVtOrig = 0;
CRITICAL_SECTION g_hookLock;
struct AnimSound {
    int clip;
    float t, vol;
    vm::GunSound snd;
    bool shell = false;
    bool ammo = false;
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
    long long lastSyncTarget = -1;
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
volatile LONG g_showSelMark = 0;
volatile LONG g_kbAzerty = 0;
volatile LONG g_mouseSensPct = 100;
LONG ReadMouseSensPct(const wchar_t* armoryPath, char* raw, size_t rawSize) {
    wchar_t path[MAX_PATH * 2];
    wcsncpy(path, armoryPath, MAX_PATH * 2 - 1);
    path[MAX_PATH * 2 - 1] = 0;
    wchar_t* slash = wcsrchr(path, L'\\');
    wcscpy(slash ? slash + 1 : path, L"tenzin_fps_mouse.txt");
    raw[0] = 0;
    if (FILE* f = _wfopen(path, L"r")) {
        char line[64];
        while (fgets(line, sizeof line, f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
            if (!strncmp(line, "sens=", 5)) snprintf(raw, rawSize, "%s", line + 5);
        }
        fclose(f);
    }
    double v = 0.0;
    char tail = 0;
    if (sscanf(raw, "%lf%c", &v, &tail) != 1 || !(v >= 10.0 && v <= 300.0)) return 100;
    return static_cast<LONG>(std::floor(v + 0.5));
}
volatile LONG g_wpOn = 0;
volatile LONG g_wDmgPct[kMaxModels] = {};
volatile LONG g_wPenSet[kMaxModels] = {};
volatile LONG g_catDmgPct[kMeleeCats] = {};
volatile LONG g_catReachCm[kMeleeCats] = {};
volatile LONG g_mpBattle = 1;
inline bool WpLive() { return g_wpOn && !g_mpBattle; }
float EffRpm(int i) {
    if (i < 0 || i >= kMaxModels) return 0.f;
    return g_w[i].set.fireRpm;
}
void RefreshFireRate() {
    int m = g_mag.loadedModel;
    if (m < 0 || m != g_modelIdx) return;
    float rpm = EffRpm(m);
    InterlockedExchange(&g_mag.fireUs, rpm >= 10.f && rpm <= 2000.f ? static_cast<LONG>(60e6 / rpm) : 0);
}
FILETIME g_armoryTime = {};
volatile LONG g_reqRoster = 0;
wchar_t g_mainIniName[64] = L"tenzin_fps.ini";
bool g_fullMod = true;
wchar_t g_addonIds[1024] = L"";
char g_allowKeys[2048] = "";
char g_unitKeys[4096] = "";
char g_heroKey[64] = "";
SRWLOCK g_heroKeyLock = SRWLOCK_INIT;
volatile LONG g_newBattle = 0;
struct CharCfg {
    char units[256];
    wchar_t arms[MAX_PATH * 6];
    float eyeHeight;
};
constexpr int kMaxChars = 8;
CharCfg g_chars[kMaxChars];
int g_charN = 0;
char g_charUnits[2048] = "";
volatile LONG g_armsWant = -1;
int g_armsApplied = -1;
size_t g_armsLogShown = 0;
int CharFor(const char* unitKey) {
    if (!unitKey || !unitKey[0]) return -1;
    for (int i = 0; i < g_charN; ++i)
        if (CsvHas(g_chars[i].units, unitKey)) return i;
    return -1;
}
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
HRESULT STDMETHODCALLTYPE HkResizeBuffers(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl);
HRESULT STDMETHODCALLTYPE HkResizeBuffers1(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl, const UINT* mask, IUnknown* const* q);
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
            g_origResize = reinterpret_cast<FnResize>(copy[2 + 13]);
            copy[2 + 13] = reinterpret_cast<uintptr_t>(&HkResizeBuffers);
            IUnknown* s3 = nullptr;
            if (n > 39 && SUCCEEDED(reinterpret_cast<IUnknown*>(sc)->QueryInterface(kIidSwapChain3, reinterpret_cast<void**>(&s3))) && s3) {
                if (reinterpret_cast<uintptr_t>(s3) == sc) {
                    g_origResize1 = reinterpret_cast<FnResize1>(copy[2 + 39]);
                    copy[2 + 39] = reinterpret_cast<uintptr_t>(&HkResizeBuffers1);
                }
                s3->Release();
            }
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
bool LayerNoCreateHere();
ID3D11Texture2D* LayerHudCopy(const D3D11_TEXTURE2D_DESC& td);
ID3D11Texture2D* g_hudSavedTex = nullptr;
bool g_hudSavedBorrowed = false;
constexpr int kUiCoverMax = 32;
SRWLOCK g_uiCoverLock = SRWLOCK_INIT;
float g_uiCover[kUiCoverMax][4];
int g_uiCoverN = 0;
ID3D11Texture2D* g_hudTex = nullptr;
UINT g_hudTexW = 0, g_hudTexH = 0;
DXGI_FORMAT g_hudTexFmt = DXGI_FORMAT_UNKNOWN;
D3D11_BOX g_hudBox[kUiCoverMax + 1] = {};
int g_hudBoxN = 0;
int g_hudBoxLogged = -1, g_hudBoxLogs = 0;
bool g_hudSaved = false;
void HudCoverSave(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* bb, const D3D11_TEXTURE2D_DESC& td) {
    g_hudSaved = false;
    g_hudBoxN = 0;
    if (!g_fp) return;
    auto add = [&](float fx0, float fy0, float fx1, float fy1) {
        auto px = [](float f, UINT size) { return f <= 0.f ? 0u : f >= 1.f ? size : static_cast<UINT>(f * size + 0.5f); };
        UINT x0 = px(fx0, td.Width), y0 = px(fy0, td.Height), x1 = px(fx1, td.Width), y1 = px(fy1, td.Height);
        if (x1 > x0 + 1 && y1 > y0 + 1 && g_hudBoxN < kUiCoverMax + 1) g_hudBox[g_hudBoxN++] = {x0, y0, 0, x1, y1, 1};
    };
    if (const LONG64 v = g_hudCover) {
        auto q = [](LONG64 x) { return static_cast<float>(x & 0xffff) / 65535.f; };
        add(q(v), q(v >> 16), q(v >> 32), q(v >> 48));
    }
    AcquireSRWLockShared(&g_uiCoverLock);
    for (int i = 0; i < g_uiCoverN; ++i) add(g_uiCover[i][0], g_uiCover[i][1], g_uiCover[i][2], g_uiCover[i][3]);
    ReleaseSRWLockShared(&g_uiCoverLock);
    if (g_hudBoxN != g_hudBoxLogged && g_hudBoxLogs < 20 && g_log) {
        ++g_hudBoxLogs;
        g_hudBoxLogged = g_hudBoxN;
        char s[400] = "";
        int k = 0;
        for (int i = 0; i < g_hudBoxN && i < 8 && k < 340; ++i)
            k += snprintf(s + k, sizeof s - k, " %u,%u-%u,%u", g_hudBox[i].left, g_hudBox[i].top, g_hudBox[i].right, g_hudBox[i].bottom);
        g_log("ui-cover: %d interface rect(s) kept above the gun drawn at Present (%ux%u):%s%s", g_hudBoxN, td.Width, td.Height, s,
              g_hudBoxN > 8 ? " ..." : "");
    }
    if (!g_hudBoxN) return;
    if (g_hudSavedBorrowed && g_hudSavedTex) g_hudSavedTex->Release();
    g_hudSavedTex = nullptr, g_hudSavedBorrowed = false;
    if (ID3D11Texture2D* lt = LayerHudCopy(td)) {
        ctx->CopyResource(lt, bb);
        g_hudSavedTex = lt, g_hudSavedBorrowed = true;
        g_hudSaved = true;
        return;
    }
    if (LayerNoCreateHere()) {
        static bool told = false;
        if (!told && g_log) g_log("ui-cover: no interface copy from the view maker yet - interface stays under the gun this frame");
        told = true;
        return;
    }
    if (!g_hudTex || g_hudTexW != td.Width || g_hudTexH != td.Height || g_hudTexFmt != td.Format) {
        if (g_hudTex) g_hudTex->Release();
        g_hudTex = nullptr;
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = td.Width;
        d.Height = td.Height;
        d.MipLevels = d.ArraySize = 1;
        d.Format = td.Format;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &g_hudTex)) || !g_hudTex) {
            g_hudTex = nullptr;
            static bool told = false;
            if (!told && g_log) g_log("viewmodel: interface cover copy %ux%u format %d failed - interface stays under the gun", td.Width, td.Height, td.Format);
            told = true;
            return;
        }
        g_hudTexW = td.Width;
        g_hudTexH = td.Height;
        g_hudTexFmt = td.Format;
    }
    ctx->CopyResource(g_hudTex, bb);
    g_hudSavedTex = g_hudTex, g_hudSavedBorrowed = false;
    g_hudSaved = true;
}
void HudCoverRestore(ID3D11DeviceContext* ctx, ID3D11Texture2D* bb) {
    if (!g_hudSaved || !g_hudSavedTex) return;
    for (int i = 0; i < g_hudBoxN; ++i) ctx->CopySubresourceRegion(bb, 0, g_hudBox[i].left, g_hudBox[i].top, 0, g_hudSavedTex, 0, &g_hudBox[i]);
    if (g_hudSavedBorrowed) g_hudSavedTex->Release();
    g_hudSavedTex = nullptr, g_hudSavedBorrowed = false;
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
            const float er = EffRpm(g_modelIdx);
            float full = (er > 0 ? er : 1200.f) / 60.f / s.spinBarrels * kTwoPi;
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
        float crouch = InterlockedExchange(&g_jlCrouchMilli, 0) * 1e-3f;
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
            if (crouch != 0.f) {
                g_jlV[0] += kick::Impulse(-0.008f * k * crouch, kJlSpring[0], kJlSpring[1]);
                g_jlV[1] += kick::Impulse(-0.5f * k * crouch, kJlSpring[0], kJlSpring[1]);
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
    int shellSeq = -1;
    LONG shellTarget = 0;
    int shellLoops = 0;
    bool shellEmpty = false;
    int shellEnd = -1;
    double shellEndStart = 0;
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
bool ShellReload() {
    for (int i = 0; i < 4; ++i)
        if (!Wp().set.reloadShellClips[i][0] || Wp().rend.FindClip(Wp().set.reloadShellClips[i]) < 0) return false;
    return true;
}
int ShellEndClip() {
    const auto& rs = Wp().set.reloadShellClips;
    int c = g_an.shellEmpty && rs[5][0] ? Wp().rend.FindClip(rs[5]) : -1;
    return c >= 0 ? c : Wp().rend.FindClip(rs[3]);
}
constexpr double kShellFireAt = 0.65, kShellFinish = 0.15, kShellCutFade = 0.15, kFirePendSec = 1.5;
void ShellAmmoEvent() {
    if (g_mag.reload == 0 || g_mag.size <= 0) return;
    LONG res = g_mag.reserve, have = g_mag.ammo < 0 ? 0 : g_mag.ammo;
    if (res != 0 && have < g_mag.size) {
        InterlockedExchange(&g_mag.ammo, ++have);
        if (res > 0) InterlockedExchange(&g_mag.reserve, res - 1);
    }
    const bool done = have >= g_mag.size || have >= g_an.shellTarget || g_mag.reserve == 0;
    if (done) InterlockedExchange(&g_mag.reload, 0);
    static int logs = 0;
    if (logs++ < 4) g_log("reload: shell in -> %ld / %ld (reserve %ld)%s", have, g_mag.size, g_mag.reserve, done ? ", done" : "");
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
struct SyncState {
    LONG burst = -1;
    long long base = 0;
    double emaDt = 1.0 / 60.0, lastNow = 0;
    double extra = 0;
    int late = 0, shots = 0;
} g_sync;
int SyncTargets(long long* out, int cap, bool use, double interval) {
    LONG w = g_shotQ.w;
    MemoryBarrier();
    if (w - g_shotQ.r > 16) g_shotQ.r = w - 16;
    long long clk = use ? vm::GunSound::ClockSamples() : -1;
    unsigned rate = vm::GunSound::ClockRate();
    if (clk < 0 || !rate || !(interval > 0.0)) {
        g_shotQ.r = w;
        return 0;
    }
    double lat = 1.25 * g_sync.emaDt + 0.012;
    lat = (lat < 0.015 ? 0.015 : (lat > 0.06 ? 0.06 : lat)) + g_sync.extra;
    const long long ivs = llround(interval * rate);
    const double now = Now();
    int n = 0;
    while (g_shotQ.r != w) {
        ShotQ::E e = g_shotQ.e[g_shotQ.r & 15];
        ++g_shotQ.r;
        if (now - e.t > 0.25) continue;
        if (e.burst != g_sync.burst) {
            g_sync.burst = e.burst;
            g_sync.base = clk + llround(lat * rate) - static_cast<long long>(e.k) * ivs;
        }
        long long t = g_sync.base + static_cast<long long>(e.k) * ivs;
        long long minT = clk + llround(0.002 * rate);
        if (t < minT) {
            double by = static_cast<double>(minT - t) / rate + 0.002;
            g_sync.extra = g_sync.extra + by > 0.03 ? 0.03 : g_sync.extra + by;
            g_sync.base += minT - t;
            t = minT;
            ++g_sync.late;
        }
        g_sync.extra *= 0.99;
        ++g_sync.shots;
        if (n < cap) out[n++] = t;
    }
    return n;
}
void QueueShot() {
    double iv = FireInterval(), t = Now();
    if (iv > 0.0 && g_chain.lastT > 0.0 && t - (g_chain.lastIdeal + iv) < iv && t - g_chain.lastT < 4.0 * iv) {
        g_chain.lastIdeal += iv;
        ++g_chain.k;
    } else {
        g_chain.lastIdeal = t;
        ++g_chain.burst;
        g_chain.k = 0;
    }
    g_chain.lastT = t;
    LONG w = g_shotQ.w;
    g_shotQ.e[w & 15] = {g_chain.burst, g_chain.k, t};
    MemoryBarrier();
    InterlockedExchange(&g_shotQ.w, w + 1);
}
void AnimStep(vm::Look& l, double now) {
    {
        double dt = g_sync.lastNow > 0 ? now - g_sync.lastNow : 0.0;
        if (dt > 0.0 && dt < 0.2) g_sync.emaDt = g_sync.emaDt * 0.9 + dt * 0.1;
        g_sync.lastNow = now;
    }
    bool fired = InterlockedExchange(&g_reqFire, 0) != 0;
    const bool lastFired = fired && InterlockedExchange(&g_reqFireLast, 0) != 0;
    if (fired) {
        bool ads = (g_stance & 1) != 0;
        const vm::Settings& fs = Wp().set;
        long long tg[8];
        int nq = SyncTargets(tg, 8, fs.fireSync, FireInterval());
        if (nq <= 0) {
            nq = 1;
            tg[0] = -1;
        }
        for (int q = 0; q < nq; ++q) {
            const long long t = tg[q];
            auto play = [t](vm::GunSound& g, float v) {
                if (t >= 0)
                    g.PlayAt(v, t);
                else
                    g.Play(v);
            };
            const bool lastQ = lastFired && q == nq - 1;
            if (lastQ && Wp().lastShot.Loaded()) {
                play(Wp().lastShot, fs.fireLastVolume < 0.f ? fs.fireVolume : fs.fireLastVolume);
                ++g_fxStat.lastShots;
            } else if (ads && Wp().gunAds.Loaded()) {
                play(Wp().gunAds, fs.fireVolume);
            } else {
                play(Wp().gun, fs.fireVolume);
            }
            if (ads && Wp().casingAds.Loaded()) {
                play(Wp().casingAds, fs.fireCasingVolume);
                ++g_fxStat.casingAds;
            } else if (Wp().casing.Loaded()) {
                play(Wp().casing, fs.fireCasingVolume);
                ++g_fxStat.casings;
            }
            if (ads && Wp().mechAds.Loaded())
                play(Wp().mechAds, Wp().set.fireMechVolume);
            else
                play(Wp().mech, Wp().set.fireMechVolume);
            play(Wp().lfe, Wp().set.fireLfeVolume);
            play(Wp().atmo, Wp().set.fireAtmoVolume);
            Wp().lastSyncTarget = t;
        }
        if (lastFired) ++g_fxStat.lastFired;
        Wp().lastShotAt = now;
        Wp().tailPending = Wp().tail.Loaded();
        Wp().flash.Trigger(g_mo.adsW);
        if (!Wp().set.shellEjectAnim) Wp().shells.Trigger(g_mo.adsW);
        g_mo.crossKick = std::fmin(g_mo.crossKick + g_crossSet.spreadShot, g_crossSet.spreadMax);
        g_mo.crossLastShot = now;
    }
    if (Wp().tailPending && now - Wp().lastShotAt >= Wp().set.fireTailGap) {
        Wp().tailPending = false;
        unsigned rate = vm::GunSound::ClockRate();
        if (Wp().set.fireSync && Wp().lastSyncTarget >= 0 && rate)
            Wp().tail.PlayAt(Wp().set.fireTailVolume, Wp().lastSyncTarget + llround(Wp().set.fireTailGap * rate));
        else
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
    InterlockedExchange(&g_meleeCat, MeleeWeapon() ? melee::CatOf(melee::Ms()) : 0);
    {
        const float* o = MeleeWeapon() ? Wp().set.meleeOwn : nullptr;
        bool any = false;
        for (int i = 0; o && i < kMeleeOwnN; ++i) any = any || o[i] >= 0.f;
        AcquireSRWLockExclusive(&g_meleeOwnPub.lock);
        const bool changed = any != g_meleeOwnPub.any || (any && memcmp(o, g_meleeOwnPub.v, sizeof g_meleeOwnPub.v) != 0);
        if (any) memcpy(g_meleeOwnPub.v, o, sizeof g_meleeOwnPub.v);
        g_meleeOwnPub.any = any;
        ReleaseSRWLockExclusive(&g_meleeOwnPub.lock);
        if (changed && any && g_log)
            g_log("melee-own: model %d uses its own melee numbers (viewmodel.ini): stab x%.2f sweep x%.2f heavy_sweep x%.2f heavy x%.2f bash x%.2f | scale %.2f block %.2f angle %.0f", g_modelIdx, o[2], o[9], o[16], o[23], o[30], o[35], o[38], o[39]);
    }
    if (!Wp().rend.Clips()) return;
    if (MeleeWeapon()) {
        MeleeStep(l, now);
        return;
    }
    {
        LONG raw = g_fp ? g_stance : 0;
        int st = (raw & 4) ? 3 : (raw & 2) ? 2 : (raw & 1) ? 1 : 0;
        bool reloadAnim = g_mag.reload != 0 && g_an.cur >= 0 &&
                          (g_an.cur == Wp().rend.FindClip("reload") || g_an.cur == Wp().rend.FindClip("reload_empty") ||
                           g_an.cur == g_an.shellSeq);
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
            int c = -1;
            if (ShellReload()) {
                const auto& rs = Wp().set.reloadShellClips;
                const auto& r2 = Wp().set.reloadShellClips2;
                c = Wp().rend.FindClip(rs[g_an.reloadKind == 2 ? 1 : 0]);
                LONG size = g_mag.size, have = g_mag.ammo < 0 ? 0 : g_mag.ammo, res = g_mag.reserve;
                g_an.shellTarget = res < 0 || have + res >= size ? size : have + res;
                if (g_an.reloadKind == 1 && g_an.shellTarget - have >= 2 && r2[0][0] && Wp().rend.FindClip(r2[0]) >= 0)
                    c = Wp().rend.FindClip(r2[0]);
                g_an.shellSeq = c;
                g_an.shellLoops = 0;
                g_an.shellEmpty = g_an.reloadKind == 2;
                static int logs = 0;
                if (logs++ < 6) g_log("reload: shell by shell %ld -> %ld (clip %d)", have, g_an.shellTarget, c);
            } else {
                c = g_an.reloadKind == 2 ? Wp().rend.FindClip("reload_empty") : -1;
                if (c < 0) c = Wp().rend.FindClip("reload");
            }
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
    if (InterlockedExchange(&g_mag.shellStop, 0) && g_mag.reload != 0) {
        int how = 0;
        if (g_an.shellSeq >= 0 && g_an.cur == g_an.shellSeq && !g_an.reloadKind) {
            const double left = (Wp().rend.ClipDuration(g_an.cur) - ClipT(now)) / (g_an.curRate > 0 ? g_an.curRate : 1.0);
            how = g_an.shellTarget == g_mag.ammo ? 3 : 1;
            g_an.shellTarget = g_mag.ammo;
            if (left > kShellFinish) {
                InterlockedExchange(&g_mag.reload, 0);
                const int end = ShellEndClip();
                Play(end, now, kShellCutFade);
                g_an.shellSeq = -1;
                g_an.shellEnd = end, g_an.shellEndStart = g_an.curStart;
                how = 2;
            }
        } else {
            g_an.reloadKind = 0;
            InterlockedExchange(&g_mag.reload, 0);
        }
        static int logs = 0;
        if (how != 3 && logs++ < 8)
            g_log("reload: fire pressed -> stop shell reload at %ld / %ld (%s)", g_mag.ammo, g_mag.size,
                  how == 2 ? "fade into end" : how == 1 ? "finish this segment, then end" : "reload not started, cancelled");
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
            if (a.clip == g_an.cur && a.t > g_an.evLast && a.t <= t && (a.ammo || a.t > t - 0.2)) {
                if (a.ammo) {
                    ShellAmmoEvent();
                } else if (a.shell) {
                    if (w.set.shellEject) w.shells.Trigger(g_mo.adsW);
                } else {
                    a.snd.Play(a.vol * w.set.animSoundVolume);
                }
                static int logs = 0;
                if (!a.ammo && logs < 24) {
                    ++logs;
                    g_log("anim %s: clip %d at %.2f s (event %.2f s)", a.shell ? "shell" : "sound", a.clip, t, a.t);
                }
            }
        }
        g_an.evLast = t;
    }
    if (g_an.shellSeq >= 0) {
        if (g_an.cur != g_an.shellSeq) {
            g_an.shellSeq = -1;
            if (g_mag.reload != 0 && g_an.reloadKind == 0) InterlockedExchange(&g_mag.reload, 0);
        } else {
            const double t = ClipT(now), dur = Wp().rend.ClipDuration(g_an.cur);
            if (t >= dur) {
                const auto& rs = Wp().set.reloadShellClips;
                const auto& r2 = Wp().set.reloadShellClips2;
                auto find = [](const char* n) { return n[0] ? Wp().rend.FindClip(n) : -1; };
                int loop = -1;
                if (g_an.shellTarget - g_mag.ammo >= 2) {
                    if (g_an.shellEmpty) loop = find(r2[2]);
                    if (loop < 0) loop = find(r2[1]);
                }
                if (loop < 0 && g_an.shellEmpty) loop = find(rs[4]);
                if (loop < 0) loop = find(rs[2]);
                const int end = ShellEndClip();
                const double over = (t - dur) / (g_an.curRate > 0 ? g_an.curRate : 1.0);
                if (g_mag.reload != 0 && g_mag.ammo < g_an.shellTarget && g_an.shellLoops < g_mag.size + 2 && loop >= 0) {
                    Play(loop, now - over, 0.0);
                    g_an.shellSeq = loop;
                    ++g_an.shellLoops;
                } else {
                    if (g_mag.reload != 0) InterlockedExchange(&g_mag.reload, 0);
                    Play(end, now - over, 0.0);
                    g_an.shellSeq = -1;
                    g_an.shellEnd = end, g_an.shellEndStart = g_an.curStart;
                }
            }
        }
    }
    if (g_an.cur >= 0 && !Wp().rend.ClipLoops(g_an.cur) && g_an.cur != StanceIdle() &&
        ClipT(now) >= Wp().rend.ClipDuration(g_an.cur))
        Play(StanceIdle(), now, g_an.stance == 1 ? 0.05 : 0.15);
    {
        bool wait = g_an.shellSeq >= 0 && g_an.cur == g_an.shellSeq;
        if (!wait && g_an.shellEnd >= 0) {
            if (g_an.cur == g_an.shellEnd && g_an.curStart == g_an.shellEndStart)
                wait = ClipT(now) < Wp().rend.ClipDuration(g_an.cur) * kShellFireAt;
            else
                g_an.shellEnd = -1;
        }
        InterlockedExchange(&g_mag.shellWait, wait ? 1 : 0);
        if (g_mag.firePendUs && (g_an.stance >= 2 || g_sw.phase != 0)) InterlockedExchange64(&g_mag.firePendUs, 0);
    }
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
    bool allowed = g_fp && s.shieldAllow && g_gunShieldOn && Wp().rend.ArmRig() && !s.mirrorX;
    bool meleeBlock = g_fp && MeleeWeapon() && (melee::g.act == melee::kBlock || melee::Guarding(now));
    g_sh.meleeBlock = meleeBlock ? 1 : 0;
    auto blockNow = [&]() {
        if (meleeBlock) {
            float b = s.meleeOwn[38] >= 0.f ? s.meleeOwn[38] : g_catBlock[melee::CatOf(melee::Ms())];
            return b >= 0.f ? b : s.shieldBlockAds;
        }
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
        w.spinLoop.SetLoopBegin(w.set.spinLoopStart);
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
                    a.ammo = !strcmp(file, "!ammo");
                    if (a.shell || a.ammo) {
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
    if (w.set.flashFx[0] || w.set.effectFx[0] || (w.set.flashGas && w.set.flashGasFx[0]) || w.set.flameFx[0]) {
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
        if (w.flash.Loaded() && w.set.flameFx[0]) {
            wchar_t fl[MAX_PATH * 2];
            swprintf(fl, MAX_PATH * 2, L"%ls\\%ls", dir, w.set.flameFx);
            w.flash.LoadFlame(fl, w.set);
            g_log("viewmodel: [%d] flame fx from %ls: layers %ls / %ls / %ls / %ls", idx, w.set.flameFx, w.set.flame[0].tex, w.set.flame[1].tex,
                  w.set.flame[2].tex, w.set.flame[3].tex);
        }
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
constexpr int kShieldPreTag = kMaxModels + 1;
struct PrefetchJob {
    int state = 0;
    bool cancel = false;
    bool upgrade = false;
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
volatile LONG g_warmedThisBattle = 0;
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
    float mv = vm::GunSound::MasterVolume();
    g_log("warm: audio engine %s in %.1f ms, master volume %.1f dB", audio ? "ready" : err, (Now() - t0) * 1000.0,
          mv > 0.f ? 20.f * std::log10(mv) : -999.f);
    InterlockedExchange(&g_reqRoster, 1);
    InterlockedExchange(&g_warm, 1);
    InterlockedExchange(&g_warmedThisBattle, 1);
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
    int sf = 0;
    size_t sb = 0;
    if (s.shieldAllow && g_gunShieldOn && s.shieldModel[0]) vm::prefetch::ShieldModel(kShieldPreTag, s.shieldModel, &sf, &sb);
    long built = 0, taken = 0;
    vm::prefetch::Counters(&built, &taken);
    g_log("prefetch: [%d] model %s (%d files, %.1f MB) %.1f ms, %d sounds %.1f ms%s (textures built in the background so far %ld, used %ld)", idx,
          ok ? "ok" : err, files, bytes / 1048576.0, (t1 - t0) * 1000.0, *nPcm, (Now() - t1) * 1000.0, sf ? ", + shield textures" : "", built, taken);
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
        bool upgrade = false;
        for (int k = 0; k < g_pfOrderN && ready < kPrefetchAhead; ++k) {
            PrefetchJob& j = g_pfJob[g_pfOrder[k]];
            if (j.state != 1) continue;
            idx = g_pfOrder[k];
            j.state = 2;
            j.cancel = false;
            upgrade = j.upgrade;
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
        if (upgrade) {
            double tu = Now();
            int n = vm::prefetch::Upgrade(idx);
            g_log("prefetch: [%d] %d textures built in the background after the device arrived (%.1f ms)", idx, n, (Now() - tu) * 1000.0);
            AcquireSRWLockExclusive(&g_pfLock);
            PrefetchJob& j = g_pfJob[idx];
            j.upgrade = false;
            bool drop = j.cancel;
            j.state = drop ? 0 : 3;
            void** pcm = drop ? j.pcm : nullptr;
            int nPcm = drop ? j.nPcm : 0;
            if (drop) j.pcm = nullptr, j.nPcm = 0;
            ReleaseSRWLockExclusive(&g_pfLock);
            InterlockedExchange64(&g_pfProgressUs, static_cast<LONG64>(Now() * 1e6));
            if (drop) {
                for (int k = 0; k < nPcm; ++k) vm::GunSound::Unprefetch(pcm[k]);
                delete[] pcm;
                vm::prefetch::Drop(idx);
            }
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
    if (j.state == 1 && j.upgrade) {
        pcm = j.pcm;
        nPcm = j.nPcm;
        j.pcm = nullptr;
        j.nPcm = 0;
        j.state = 0;
        j.upgrade = false;
        drop = true;
    } else if (j.state == 1) {
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
bool g_bgTextures = true;
ID3D11Device* g_pfDev = nullptr;
void PrefetchDevice(ID3D11Device* dev) {
    if (!g_bgTextures || dev == g_pfDev || !g_pfRunning) return;
    g_pfDev = dev;
    bool bg = vm::prefetch::SetDevice(dev);
    int n = 0;
    if (bg) {
        AcquireSRWLockExclusive(&g_pfLock);
        for (int k = 0; k < g_pfOrderN; ++k) {
            PrefetchJob& j = g_pfJob[g_pfOrder[k]];
            if (j.state == 3 && !g_w[g_pfOrder[k]].rend.Ready()) j.upgrade = true, j.state = 1, ++n;
        }
        ReleaseSRWLockExclusive(&g_pfLock);
        if (n) SetEvent(g_pfWake);
    }
    g_log("prefetch: weapon textures are built %s (device %p, creation flags 0x%x); %d already-read weapons queued to build theirs", bg ?
          "on the background thread" : "on the render thread (device is single-threaded)", (void*)dev, dev->GetCreationFlags(), n);
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
bool g_initOnly = false;
bool g_shieldPreDone = false;
bool g_rosterNoLoad = false;
void ArmsLogFlush() {
    const char* s = vm::ArmsOverrideLog();
    size_t n = strlen(s);
    if (n < g_armsLogShown) g_armsLogShown = 0;
    for (const char* p = s + g_armsLogShown; *p;) {
        const char* e = strchr(p, '\n');
        size_t l = e ? static_cast<size_t>(e - p) : strlen(p);
        g_log("arms: %.*s", static_cast<int>(l), p);
        p += l + (e ? 1 : 0);
    }
    g_armsLogShown = n;
}
bool EnsureWeapon(int idx, ID3D11Device* dev) {
    if (idx < 0 || idx >= g_modelCount) return false;
    Weapon& w = g_w[idx];
    if (!w.rend.Ready()) {
        double t0 = Now();
        if (!SettingsUnchanged(idx, w.set)) LoadAllSettings(idx, w.set);
        char err[512] = {};
        w.rend.ShareScreenBuffers(true);
        if (!w.rend.Init(dev, g_models[idx], err, sizeof err)) {
            g_log("viewmodel: init failed (%ls): %s", g_models[idx], err);
            PrefetchDiscard(idx);
            return false;
        }
        g_log("viewmodel: [%d] ready on device %p: %u triangles, %u bones, %u clips, model %ls (init %.1f ms)", idx, (void*)dev,
              w.rend.Triangles(), w.rend.Bones(), w.rend.Clips(), g_models[idx], (Now() - t0) * 1000.0);
        ArmsLogFlush();
        w.fxDirty = true;
        if (idx == g_modelIdx) {
            AnimReset(Now());
            MagApply();
        }
    }
    ApplyGunShield(idx, w.set);
    if (w.fxDirty && !g_initOnly) {
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
struct WpEntry {
    char id[64];
    float dmg, pen, reach;
    bool hasDmg, hasPen, hasReach;
};
const char* WpNum(const char* p, double* v) {
    bool neg = *p == '-';
    if (*p == '-' || *p == '+') ++p;
    double x = 0, scale = 1;
    bool digit = false, dot = false;
    for (;; ++p) {
        if (*p >= '0' && *p <= '9') {
            digit = true;
            if (dot) {
                scale /= 10;
                x += (*p - '0') * scale;
            } else {
                x = x * 10 + (*p - '0');
            }
        } else if (*p == '.' && !dot) {
            dot = true;
        } else {
            break;
        }
    }
    if (!digit) return nullptr;
    *v = neg ? -x : x;
    return p;
}
int ParseWeaponParams(const char* s, WpEntry* out, int cap) {
    int n = 0;
    const char* p = s;
    while (*p && n < cap) {
        if (p[0] == '-' && p[1] == '-') {
            while (*p && *p != '\n') ++p;
            continue;
        }
        if (p[0] != '[' || p[1] != '"') {
            ++p;
            continue;
        }
        p += 2;
        WpEntry e = {};
        size_t k = 0;
        while (*p && *p != '"' && *p != '\n' && k + 1 < sizeof e.id) e.id[k++] = *p++;
        e.id[k] = 0;
        if (!k || p[0] != '"' || p[1] != ']') continue;
        p += 2;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p != '=') continue;
        ++p;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p != '{') continue;
        ++p;
        while (*p && *p != '}' && *p != '\n') {
            while (*p == ' ' || *p == '\t' || *p == ',' || *p == '\r') ++p;
            char key[16];
            size_t kl = 0;
            while (((*p >= 'a' && *p <= 'z') || *p == '_') && kl + 1 < sizeof key) key[kl++] = *p++;
            key[kl] = 0;
            while (*p == ' ' || *p == '\t') ++p;
            double v = 0;
            const char* end = nullptr;
            if (kl && *p == '=') {
                const char* q = p + 1;
                while (*q == ' ' || *q == '\t') ++q;
                end = WpNum(q, &v);
            }
            if (!end) {
                while (*p && *p != ',' && *p != '}' && *p != '\n') ++p;
                continue;
            }
            p = end;
            if (!strcmp(key, "damage"))
                e.dmg = static_cast<float>(v), e.hasDmg = true;
            else if (!strcmp(key, "pen"))
                e.pen = static_cast<float>(v), e.hasPen = true;
            else if (!strcmp(key, "reach"))
                e.reach = static_cast<float>(v), e.hasReach = true;
        }
        if (*p == '}') {
            ++p;
            out[n++] = e;
        }
    }
    return n;
}
float WpClamp(float v, float lo, float hi) {
    if (!(v >= lo)) v = lo;
    if (!(v <= hi)) v = hi;
    return v;
}
void LoadWeaponParams(const wchar_t* armoryPath, const char* flag) {
    static WpEntry ent[kMaxModels + kMeleeCats + 32];
    LONG dmg[kMaxModels] = {}, pen[kMaxModels] = {}, cdmg[kMeleeCats] = {}, creach[kMeleeCats] = {};
    const bool want = !strcmp(flag, "1");
    bool read = false;
    int nEnt = 0, nW = 0, nC = 0;
    if (want) {
        wchar_t path[MAX_PATH * 2];
        wcsncpy(path, armoryPath, MAX_PATH * 2 - 1);
        path[MAX_PATH * 2 - 1] = 0;
        wchar_t* slash = wcsrchr(path, L'\\');
        wcscpy(slash ? slash + 1 : path, L"tenzin_fps_weapon_params.txt");
        if (FILE* f = _wfopen(path, L"rb")) {
            static char buf[256 * 1024];
            size_t len = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[len] = 0;
            nEnt = ParseWeaponParams(buf, ent, static_cast<int>(sizeof ent / sizeof ent[0]));
            read = true;
        }
    }
    char line[720];
    int ln = 0;
    line[0] = 0;
    for (int e = 0; e < nEnt; ++e) {
        const WpEntry& w = ent[e];
        int cat = -1;
        for (int c = 0; c < kMeleeCats && cat < 0; ++c) {
            char sec[48];
            snprintf(sec, sizeof sec, "%ls", kMeleeCatSec[c]);
            if (!strcmp(sec, w.id)) cat = c;
        }
        if (cat >= 0) {
            if (w.hasDmg) cdmg[cat] = lround(WpClamp(w.dmg, 10.f, 300.f));
            if (w.hasReach) creach[cat] = lround(WpClamp(w.reach, -1.f, 2.f) * 100.f);
            ++nC;
            if (ln >= 0 && ln < static_cast<int>(sizeof line))
                ln += snprintf(line + ln, sizeof line - ln, " | %s dmg %ld%% reach %+.2fm", w.id, cdmg[cat] ? cdmg[cat] : 100L, creach[cat] / 100.0);
            continue;
        }
        for (int i = 0; i < g_modelCount && i < kMaxModels; ++i) {
            char a[64];
            snprintf(a, sizeof a, "%ls", g_w[i].set.weaponId);
            if (!a[0] || strcmp(a, w.id)) continue;
            if (w.hasDmg) dmg[i] = lround(WpClamp(w.dmg, 10.f, 300.f));
            if (w.hasPen) pen[i] = lround(WpClamp(w.pen, 0.f, 20.f)) + 1;
            ++nW;
            if (ln >= 0 && ln < static_cast<int>(sizeof line)) {
                char pn[16];
                if (pen[i])
                    snprintf(pn, sizeof pn, "%ld", pen[i] - 1);
                else
                    snprintf(pn, sizeof pn, "default");
                ln += snprintf(line + ln, sizeof line - ln, " | %s dmg %ld%% pen %s", w.id, dmg[i] ? dmg[i] : 100L, pn);
            }
        }
    }
    for (int i = 0; i < kMaxModels; ++i) {
        InterlockedExchange(&g_wDmgPct[i], dmg[i]);
        InterlockedExchange(&g_wPenSet[i], pen[i]);
    }
    for (int c = 0; c < kMeleeCats; ++c) {
        InterlockedExchange(&g_catDmgPct[c], cdmg[c]);
        InterlockedExchange(&g_catReachCm[c], creach[c]);
    }
    LONG on = want && read ? 1 : 0;
    LONG was = InterlockedExchange(&g_wpOn, on);
    if (want || was)
        g_log("weapon-params: custom weapon stats %s (wparams='%s', file %s, %d entries: %d weapons, %d melee classes%s)%s", on ? "ON" : "off", flag,
              read ? "read" : "missing", nEnt, nW, nC, g_mpBattle ? "; multiplayer / not verified - ignored this battle" : "", line);
}
void ArmsSync() {
    const int want = static_cast<int>(g_armsWant);
    if (want == g_armsApplied) return;
    g_armsApplied = want;
    vm::SetArmsOverride(want >= 0 ? g_chars[want].arms : L"");
    g_armsLogShown = 0;
    int n = 0;
    for (int i = 0; i < g_modelCount; ++i)
        if (g_w[i].rend.Ready()) UnloadWeapon(i), ++n;
    g_log("arms: %s -> %d arms kits loaded, %d loaded weapons reload with them", want >= 0 ? g_chars[want].units : "weapon's own arms",
          vm::ArmsOverrideCount(), n);
    ArmsLogFlush();
    InterlockedExchange(&g_reqRoster, 1);
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
    char wparams[16] = "";
    char selMark[16] = "";
    char kbAzerty[16] = "";
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
            if (!strncmp(line, "wparams=", 8)) snprintf(wparams, sizeof wparams, "%s", line + 8);
            if (!strncmp(line, "sel_marker=", 11)) snprintf(selMark, sizeof selMark, "%s", line + 11);
            if (!strncmp(line, "kb_azerty=", 10)) snprintf(kbAzerty, sizeof kbAzerty, "%s", line + 10);
        }
        fclose(f);
    }
    if (!g_fullMod) ammoUp[0] = 0, infAmmo[0] = 0, infTac[0] = 0, wparams[0] = 0, selMark[0] = 0, kbAzerty[0] = 0;
    {
        bool camp = g_fullMod && !strcmp(mode, "campaign");
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
    LoadWeaponParams(path, wparams);
    {
        LONG sm = !strcmp(selMark, "1") ? 1 : 0;
        if (InterlockedExchange(&g_showSelMark, sm) != sm || sm)
            g_log("ui: selection markers in first person %s (MCT sel_marker='%s')", sm ? "SHOWN" : "hidden", selMark);
    }
    {
        LONG az = !strcmp(kbAzerty, "1") ? 1 : 0;
        if (InterlockedExchange(&g_kbAzerty, az) != az || az)
            g_log("keys: %s layout (MCT kb_azerty='%s')%s", az ? "AZERTY" : "QWERTY", kbAzerty,
                  az ? " - move Z/Q/S/D, gun shield A, Alt+) / Alt+= cast slots 11 / 12" : "");
    }
    {
        char sensRaw[32];
        sensRaw[0] = 0;
        const LONG pct = g_fullMod ? ReadMouseSensPct(path, sensRaw, sizeof sensRaw) : 100;
        if (InterlockedExchange(&g_mouseSensPct, pct) != pct || pct != 100)
            g_log("mouse: sensitivity %ld%% (MCT sens='%s')", static_cast<long>(pct), sensRaw);
    }
    RefreshFireRate();
    WIN32_FILE_ATTRIBUTE_DATA fa;
    bool fresh = InterlockedExchange(&g_newBattle, 0) != 0;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fa) &&
        CompareFileTime(&fa.ftLastWriteTime, &g_armoryTime) != 0) {
        g_armoryTime = fa.ftLastWriteTime;
        fresh = true;
    }
    if (fresh) RefillForNewBattle();
    if (!g_fullMod) mode[0] = 0, owned[0] = 0, loadout[0] = 0;
    char hero[64];
    AcquireSRWLockShared(&g_heroKeyLock);
    memcpy(hero, g_heroKey, sizeof hero);
    ReleaseSRWLockShared(&g_heroKeyLock);
    bool iniAll = g_fullMod && GetPrivateProfileIntW(L"viewmodel", L"loadout_all", 0, g_ini) != 0;
    bool all = g_fullMod && (g_loadoutAll || iniAll);
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
        bool own = hero[0] && CsvHasW(s.weaponOwner, hero);
        return s.weaponId[0] && (own || (g_fullMod && (!campaign || s.weaponDefault || ownedHas(s.weaponId))));
    };
    LONG n = 0;
    LONG slot[kSlots] = {-1, -1, -1, -1};
    const char* from = iniAll ? "ini loadout_all (test: every weapon)" : "all usable";
    auto own = [&](int i) {
        const vm::Settings& s = g_w[i].set;
        return hero[0] && s.weaponId[0] && CsvHasW(s.weaponOwner, hero);
    };
    auto guest = [&](int i) {
        const vm::Settings& s = g_w[i].set;
        return hero[0] && s.weaponId[0] && !CsvHasW(s.weaponOwner, hero) && CsvHasW(s.weaponGuest, hero);
    };
    bool ownAll = false;
    for (int i = 0; i < g_modelCount && !ownAll; ++i) ownAll = own(i);
    if (ownAll && !g_fullMod) {
        InterlockedExchange(&g_rosterAll, 1);
        from = "core only: own weapons + guest weapons (weapon_owner / weapon_guest)";
        for (int i = 0; i < g_modelCount; ++i)
            if (own(i)) g_roster[n++] = i;
        for (int i = 0; i < g_modelCount; ++i)
            if (guest(i)) g_roster[n++] = i;
    } else if (all) {
        for (int i = 0; i < g_modelCount; ++i)
            if (usable(i)) g_roster[n++] = i;
    } else {
        if (!loadout[0]) {
            wchar_t w[kSlots * 64 + 8] = L"";
            from = "ini loadout_default";
            if (!campaign) {
                GetPrivateProfileStringW(L"viewmodel", L"loadout_custom", L"", w, kSlots * 64 + 8, g_ini);
                if (w[0]) from = "ini loadout_custom";
            }
            if (!w[0]) GetPrivateProfileStringW(L"viewmodel", L"loadout_default", L"", w, kSlots * 64 + 8, g_ini);
            snprintf(loadout, sizeof loadout, "%ls", w);
        } else {
            from = campaign ? "armory file" : "armory file (custom battle loadout)";
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
    if (ownAll && g_fullMod) {
        static char ownFrom[200];
        bool anyGuest = false;
        for (int i = 0; i < g_modelCount && !anyGuest; ++i) anyGuest = guest(i);
        snprintf(ownFrom, sizeof ownFrom, anyGuest ? "hero's own weapons + the other hero's weapons + %s" : "hero's own weapons + %s", from);
        from = ownFrom;
        static LONG merged[kMaxModels];
        LONG m = 0;
        for (int i = 0; i < g_modelCount && m < kMaxModels; ++i)
            if (own(i)) merged[m++] = i;
        for (int i = 0; i < g_modelCount && m < kMaxModels; ++i)
            if (guest(i)) merged[m++] = i;
        for (LONG k = 0; k < n && m < kMaxModels; ++k)
            if (!own(g_roster[k]) && !guest(g_roster[k])) merged[m++] = g_roster[k];
        for (LONG k = 0; k < m; ++k) g_roster[k] = merged[k];
        n = m;
        InterlockedExchange(&g_rosterAll, 1);
    }
    if (!n) g_roster[n++] = g_modelIdx;
    for (int k = 0; k < kSlots; ++k) InterlockedExchange(&g_slot[k], slot[k]);
    InterlockedExchange(&g_rosterN, n);
    {
        int sp = slot[3];
        for (LONG k = 0; all && sp < 0 && k < n; ++k)
            if (g_w[g_roster[k]].set.gunShieldModel[0]) sp = g_roster[k];
        const wchar_t* gs = sp >= 0 ? g_w[sp].set.gunShieldModel : L"";
        if (wcscmp(gs, g_gunShield)) {
            wcsncpy(g_gunShield, gs, 519);
            g_gunShield[519] = 0;
            ++g_gunShieldGen;
        }
        InterlockedExchange(&g_gunShieldOn, g_gunShield[0] ? 1 : 0);
        g_log("shield: gun shield %ls (slot 4 = model %d %ls)", g_gunShield[0] ? g_gunShield : L"off - no shield-spear in slot 4", sp,
              sp >= 0 ? g_w[sp].set.weaponId : L"-");
        for (LONG k = 0; k < n; ++k) ApplyGunShield(g_roster[k], g_w[g_roster[k]].set);
    }
    g_log("weapon: roster %ld from %s (armory file %s, mode '%s', owned '%s', loadout '%s'; hero '%s', %s)", n, from,
          mode[0] || owned[0] ? "found" : "missing", mode, owned, loadout, hero, g_fullMod ? "full mod" : "core only: own weapons");
    bool curOk = false;
    for (LONG k = 0; k < n; ++k) {
        int i = g_roster[k];
        g_log("weapon:   %ld = model %d %ls (%ls)", k + 1, i, g_models[i], g_w[i].set.weaponId);
        if (i == g_modelIdx) curOk = true;
    }
    if (!all)
        g_log("weapon: slots 1 primary %d / 2 primary %d / 3 pistol %d / 4 melee %d (model index, -1 = empty)", slot[0], slot[1], slot[2], slot[3]);
    g_preloadNext = 0;
    g_shieldPreDone = false;
    PrefetchQueue(g_roster, n);
    if (!curOk && g_sw.phase == 0 && (g_rosterNoLoad || EnsureWeapon(g_roster[0], dev))) {
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
void ShieldPreload(ID3D11Device* dev) {
    if (g_shieldPreDone) return;
    g_shieldPreDone = true;
    if (!g_gunShieldOn) {
        vm::prefetch::Drop(kShieldPreTag);
        return;
    }
    const vm::Settings* s = Wp().set.shieldAllow ? &Wp().set : nullptr;
    for (LONG k = 0; !s && k < g_rosterN; ++k)
        if (g_w[g_roster[k]].set.shieldAllow) s = &g_w[g_roster[k]].set;
    if (s && !g_shieldR.BuiltFor(*s)) {
        double t0 = Now();
        char err[256] = {};
        bool ok = g_shieldR.Load(dev, *s, err, sizeof err);
        g_log("viewmodel: shield preloaded for %ls in %.1f ms (%s)%s%s", s->weaponId, (Now() - t0) * 1000.0,
              !ok ? "failed" : g_shieldR.HasModel() ? "model" : "built-in mesh", err[0] ? ": " : "", err);
    }
    vm::prefetch::Drop(kShieldPreTag);
}
void PreloadStep(ID3D11Device* dev) {
    UnloadStep();
    if (g_preloadNext < 0 || g_sw.phase) return;
    while (g_preloadNext < g_rosterN) {
        int i = g_roster[g_preloadNext];
        if (!g_w[i].rend.Ready() || g_w[i].fxDirty) {
            if (!PrefetchReady(i)) return;
            if (!g_w[i].rend.Ready() && i != g_modelIdx) {
                g_initOnly = true;
                bool ok = EnsureWeapon(i, dev);
                g_initOnly = false;
                if (!ok) ++g_preloadNext;
                return;
            }
            ++g_preloadNext;
            EnsureWeapon(i, dev);
            return;
        }
        ++g_preloadNext;
    }
    g_preloadNext = -1;
    ShieldPreload(dev);
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
    vm::ReleaseSharedScreenBuffers();
    g_shieldR.Release();
    InterlockedExchange(&g_reqRoster, 1);
}
double g_slowFrameMs = 20.0;
struct FrameProf {
    static constexpr int kSegs = 24;
    const char* name[kSegs];
    double t[kSegs];
    int n = 0;
    double t0 = 0;
    void Start(double now) { n = 0, t0 = now; }
    void Mark(const char* nm) {
        if (n < kSegs) name[n] = nm, t[n++] = Now();
    }
    void Format(char* out, size_t cap) const {
        size_t k = 0;
        out[0] = 0;
        double prev = t0;
        for (int i = 0; i < n && k + 32 < cap; ++i) {
            double ms = (t[i] - prev) * 1000.0;
            prev = t[i];
            if (ms >= 0.1) k += snprintf(out + k, cap - k, "%s%s %.1f", k ? ", " : "", name[i], ms);
        }
    }
};
FrameProf g_fprof;
bool g_profWasFp = false;
bool g_gpuTimer = true;
struct GpuTimer {
    static constexpr int kSlots = 8;
    ID3D11Device* dev = nullptr;
    ID3D11Query *dj[kSlots] = {}, *t0[kSlots] = {}, *t1[kSlots] = {};
    bool pending[kSlots] = {};
    int head = 0;
    double sum = 0, max = 0;
    unsigned n = 0, busy = 0;
    bool broken = false;
    void Release() {
        for (int k = 0; k < kSlots; ++k) {
            if (dj[k]) dj[k]->Release();
            if (t0[k]) t0[k]->Release();
            if (t1[k]) t1[k]->Release();
            dj[k] = t0[k] = t1[k] = nullptr;
            pending[k] = false;
        }
        dev = nullptr;
    }
    bool Ensure(ID3D11Device* d) {
        if (broken) return false;
        if (d == dev && dj[0]) return true;
        if (LayerNoCreateHere()) return false;
        Release();
        D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, qt = {D3D11_QUERY_TIMESTAMP, 0};
        for (int k = 0; k < kSlots; ++k)
            if (FAILED(d->CreateQuery(&qd, &dj[k])) || FAILED(d->CreateQuery(&qt, &t0[k])) || FAILED(d->CreateQuery(&qt, &t1[k]))) {
                Release();
                broken = true;
                return false;
            }
        dev = d;
        return true;
    }
    void Collect(ID3D11DeviceContext* c) {
        for (int k = 0; k < kSlots; ++k) {
            if (!pending[k]) continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dd;
            if (c->GetData(dj[k], &dd, sizeof dd, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
            pending[k] = false;
            UINT64 a = 0, b = 0;
            if (dd.Disjoint || !dd.Frequency || c->GetData(t0[k], &a, sizeof a, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                c->GetData(t1[k], &b, sizeof b, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK || b < a)
                continue;
            double ms = static_cast<double>(b - a) * 1000.0 / static_cast<double>(dd.Frequency);
            sum += ms;
            if (ms > max) max = ms;
            ++n;
        }
    }
    int Begin(ID3D11Device* d, ID3D11DeviceContext* c) {
        if (!g_gpuTimer || !Ensure(d)) return -1;
        Collect(c);
        int k = head;
        if (pending[k]) {
            ++busy;
            return -1;
        }
        c->Begin(dj[k]);
        c->End(t0[k]);
        return k;
    }
    void End(ID3D11DeviceContext* c, int k) {
        if (k < 0) return;
        c->End(t1[k]);
        c->End(dj[k]);
        pending[k] = true;
        head = (head + 1) % kSlots;
    }
} g_gpu;
SRWLOCK g_gtLock = SRWLOCK_INIT;
double g_gtSum = 0, g_gtMax = 0;
unsigned g_gtN = 0;
void GameThreadAdd(double ms) {
    AcquireSRWLockExclusive(&g_gtLock);
    g_gtSum += ms;
    if (ms > g_gtMax) g_gtMax = ms;
    ++g_gtN;
    ReleaseSRWLockExclusive(&g_gtLock);
}
int g_profLogs = 0, g_warmProfLogs = 0;
vm::NearDof g_dof;
float g_dofW = 0.f;
double g_dofT = -1.0;
int g_dofDebugForce = -1;
bool g_dofOff = false;
void DofStep(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11Texture2D* bb, UINT w, UINT h, bool hw, bool gunDrawn) {
    const vm::Settings& s = Wp().set;
    const double now = Now();
    const float dt = g_dofT < 0.0 ? 0.f : static_cast<float>(std::fmin(now - g_dofT, 0.25));
    g_dofT = now;
    if (!s.dof) {
        g_dofW = 0.f;
        return;
    }
    float target = 0.f;
    if (!g_dofOff && g_fp && Wp().lastShotAt > 0.0 && now - Wp().lastShotAt < s.dofHold) target = 1.f - g_mo.adsW * (1.f - s.dofAds);
    g_dofW += (target - g_dofW) * (1.f - std::exp(-s.dofRate * dt));
    const bool debug = (g_dofDebugForce >= 0 ? g_dofDebugForce : s.dofDebug) != 0;
    if (!gunDrawn || g_dofW < 0.004f) return;
    bool dg = false;
    ID3D11ShaderResourceView* col = Wp().rend.CopyForDistort(ctx, bb, w, h, &dg);
    ID3D11ShaderResourceView* dep = Wp().rend.DistortDepth();
    if (!col || !dep) return;
    const float a = g_mo.adsW;
    auto adsMix = [a](float hip, float ads) { return ads > 0.f ? hip + (ads - hip) * a : hip; };
    const vm::DofParams p = {adsMix(s.dofFocus, s.dofAdsFocus), adsMix(s.dofRamp, s.dofAdsRamp),
                             s.dofBlur * adsMix(s.dofPx, s.dofAdsPx) * static_cast<float>(h) / 1080.f, g_dofW, debug};
    g_dof.Apply(ctx, rtv, col, dg, dep, w, h, hw, p);
}
void LayerPrewarm(IDXGISwapChain* sc, ID3D11Device* dev) {
    const double t0 = Now();
    ID3D11Texture2D* bb = nullptr;
    D3D11_TEXTURE2D_DESC td = {};
    if (LayerBackBuffer(sc, &bb)) {
        bb->GetDesc(&td);
        bb->Release();
    }
    const bool gpu = g_gpuTimer && g_gpu.Ensure(dev);
    const bool cross = g_cross.Prewarm(dev), cm = g_cmR.Prewarm(dev);
    const bool screen = td.Width && Wp().rend.Ready() && Wp().rend.PrewarmScreen(td);
    const bool dof = td.Width && g_dof.Prewarm(dev, td.Width, td.Height);
    g_log("warm: first-person GPU objects made before first person in %.1f ms: GPU timer %s, crosshair %s, cast marker %s, screen buffers %ux%u %s, alt dof %s",
          (Now() - t0) * 1000.0, gpu ? "ok" : g_gpuTimer ? "failed" : "off", cross ? "ok" : "failed", cm ? "ok" : "failed", td.Width, td.Height,
          screen ? "ok" : "not made", dof ? "ok" : "not made");
}
void WarmStep(IDXGISwapChain* sc) {
    ID3D11Device* dev = nullptr;
    if (!LayerDevice(sc, &dev)) {
        InterlockedExchange(&g_warm, 0);
        return;
    }
    if (InterlockedCompareExchange(&g_warm, 2, 1) == 1) g_warmT0 = Now(), g_warmProfLogs = 0;
    PrefetchDevice(dev);
    {
        const double ta = Now();
        ArmsSync();
        const double ams = (Now() - ta) * 1000.0;
        if (g_slowFrameMs > 0 && ams >= g_slowFrameMs) g_log("warm-prof: arms swap %.1f ms (before first person)", ams);
    }
    if (g_reqRoster) {
        g_rosterNoLoad = true;
        RosterStep(dev);
        g_rosterNoLoad = false;
    }
    if (!g_w[g_modelIdx].rend.Ready() && !PrefetchReady(g_modelIdx)) {
        dev->Release();
        return;
    }
    FrameProf wp;
    wp.Start(Now());
    CheckDevice(dev);
    wp.Mark("device");
    bool ok = EnsureWeapon(g_modelIdx, dev);
    wp.Mark("current");
    if (ok) {
        RosterStep(dev);
        wp.Mark("roster");
        PreloadStep(dev);
        wp.Mark("preload");
    }
    double wms = (Now() - wp.t0) * 1000.0;
    if (g_slowFrameMs > 0 && wms >= g_slowFrameMs && g_warmProfLogs < 40) {
        char segs[512];
        wp.Format(segs, sizeof segs);
        ++g_warmProfLogs;
        g_log("warm-prof: slow preload frame %.1f ms (before first person): %s", wms, segs);
    }
    if (!ok || (g_preloadNext < 0 && !g_reqRoster)) {
        int ready = 0;
        for (LONG k = 0; k < g_rosterN; ++k) ready += g_w[g_roster[k]].rend.Ready();
        g_log("warm: %s in %.2f s before first person (%d of %ld weapons ready)", ok ? "preload done" : "stopped (current weapon failed)",
              Now() - g_warmT0, ready, static_cast<long>(g_rosterN));
        if (ok) LayerPrewarm(sc, dev);
        InterlockedExchange(&g_warm, 0);
    }
    dev->Release();
}
void CastMarkerDraw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, UINT w, UINT h, bool hw) {
    if (!g_cmStyle.enabled) return;
    bool on;
    AcquireSRWLockShared(&g_cmPub.lock);
    on = g_cmPub.on;
    if (on) g_cmDraw = g_cmPub.f;
    ReleaseSRWLockShared(&g_cmPub.lock);
    vm::WorldCam wc;
    if (!on || !ReadCam(&wc)) return;
    static int logs = 0;
    bool drawn = g_cmR.Draw(ctx, rtv, w, h, hw, wc, g_cmDraw, g_cmStyle);
    if (logs < 6 && drawn) ++logs, g_log("cast-marker: drawn %d vertices (%d lines, %d fill tris%s%s) on %ux%u", g_cmR.Verts(), g_cmDraw.nLines,
                                          g_cmDraw.nTris, g_cmDraw.pillar ? ", pillar" : "", g_cmDraw.chevron ? ", chevron" : "", w, h);
}
double g_spinSilenceUntil = 0;
void SpinSilenceStep() {
    if (g_modelIdx < 0 || g_modelIdx >= g_modelCount) return;
    const double now = Now();
    if (g_spin.state != 0 || g_an.spinSeen != 0) {
        const LONG st = InterlockedExchange(&g_spin.state, 0);
        g_an.spinSeen = 0;
        Wp().spinUp.FadeAll();
        Wp().spinLoop.FadeAll();
        Wp().spinDown.FadeAll();
        g_spinSilenceUntil = now + 0.3;
        g_log("viewmodel: first person off while spinning (state %ld) - spin up / loop / down sounds faded out", static_cast<long>(st));
    }
    if (g_spinSilenceUntil > 0) {
        Wp().spinUp.Update();
        Wp().spinLoop.Update();
        Wp().spinDown.Update();
        if (now >= g_spinSilenceUntil) g_spinSilenceUntil = 0;
    }
}
void DrawLayer(IDXGISwapChain* sc, int part) {
    if (g_failed || !(g_testLayer || g_fp)) {
        LayerTargetDrop();
        if (!g_failed && g_warm && part == kPartAll) WarmStep(sc);
        g_profWasFp = false;
        if (part == kPartAll) SpinSilenceStep();
        return;
    }
    double t0 = Now();
    g_fprof.Start(t0);
    vm::DrawProf gunProf;
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
        g_log("viewmodel: settings reloaded (fov %.1f offset %.3f,%.3f,%.3f rot %.1f,%.1f,%.1f scale %.3f mirror %d)",
              Wp().set.fovDeg, Wp().set.offset[0], Wp().set.offset[1], Wp().set.offset[2], Wp().set.rotDeg[0], Wp().set.rotDeg[1],
              Wp().set.rotDeg[2], Wp().set.scale, Wp().set.mirrorX);
    }
    g_fprof.Mark("settings");
    ID3D11Device* dev = nullptr;
    LayerUse lu;
    const int lr = LayerNoCreateHere() ? LayerTargetFor(sc, false, &lu) : 2;
    if (lr == 0) return;
    if (lr == -1) {
        if (!g_st.loggedFmt) g_log("viewmodel: %s; layer off", lu.fail);
        g_st.loggedFmt = true;
        g_failed = true;
        return;
    }
    LayerGuard guard(lr == 1 ? lu.cs : nullptr);
    if (!guard.Ok()) {
        LayerGuardSkip(part == kPartGun);
        return;
    }
    if (lr == 1) dev = lu.dev, lu.dev = nullptr;
    if (!dev && !LayerDevice(sc, &dev)) {
        if (!g_st.loggedDev) g_log("viewmodel: swapchain has no D3D11 device; layer off");
        g_st.loggedDev = true;
        g_failed = true;
        return;
    }
    PrefetchDevice(dev);
    if (part != kPartCross) {
        CheckDevice(dev);
        ArmsSync();
        g_fprof.Mark("device");
        if (!EnsureWeapon(g_modelIdx, dev)) {
            g_failed = true;
            dev->Release();
            return;
        }
        g_fprof.Mark("current");
        RosterStep(dev);
        g_fprof.Mark("roster");
        AmmoRefillStep();
        SwitchStep(dev);
        g_fprof.Mark("switch");
        PreloadStep(dev);
        g_fprof.Mark("preload");
    }
    ID3D11Texture2D* bb = lu.bb;
    lu.bb = nullptr;
    if (bb || LayerBackBuffer(sc, &bb)) {
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
            ID3D11RenderTargetView* rtv = lu.rtv;
            lu.rtv = nullptr;
            if (rtv || (SUCCEEDED(dev->CreateRenderTargetView(bb, &rd, &rtv)) && rtv)) {
                ID3D11DeviceContext* ctx = nullptr;
                dev->GetImmediateContext(&ctx);
                if (ctx) {
                    g_fprof.Mark("rtv");
                    const int gpuSlot = (part == kPartGun && !g_uiMidGpuTimer) ? -1 : g_gpu.Begin(dev, ctx);
                    if (part != kPartGun) UiLayerOnPresent(ctx, bb, td.Width, td.Height);
                    g_fprof.Mark("ui");
                    float mx = 0, my = 0, ma = 1;
                    bool mv = false, gun = part != kPartCross;
                    if (gun) {
                        vm::Look look = ComputeLook();
                        g_fprof.Mark("look");
                        if (part == kPartAll) HudCoverSave(dev, ctx, bb, td);
                        g_fprof.Mark("hud-save");
                        if (g_fp) CastMarkerDraw(ctx, rtv, td.Width, td.Height, hw);
                        g_fprof.Mark("cast-marker");
                        Wp().rend.SetCounterValue(g_mag.size > 0 ? static_cast<int>(g_mag.ammo) : -1);
                        const bool gunDrawn = Wp().rend.Draw(ctx, rtv, bb, td.Width, td.Height, hw, Wp().set, look);
                        if (gunDrawn) ++g_st.draws;
                        g_fprof.Mark("gun");
                        gunProf = vm::g_drawProf;
                        mv = Wp().rend.Muzzle(&mx, &my, &ma);
                        vm::MuzzleFrame mf;
                        vm::WorldCam wc;
                        bool hasMf = g_fp && Wp().rend.MuzzleView(&mf), hasCam = g_fp && ReadCam(&wc);
                        if (g_fp) ShieldDraw(dev, ctx, rtv, td.Width, td.Height, hw, hasCam ? &wc : nullptr);
                        g_fprof.Mark("shield");
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
                        g_fprof.Mark("shells");
                        DofStep(ctx, rtv, bb, td.Width, td.Height, hw, gunDrawn);
                        {
                            static const bool preGun = getenv("HFPS_DISTORT_PRE_GUN") != nullptr;
                            bool dg = false;
                            ID3D11ShaderResourceView* ds = Wp().flash.HasDistortion() ? Wp().rend.CopyForDistort(ctx, bb, td.Width, td.Height, &dg) : nullptr;
                            if (preGun) ds = Wp().rend.SceneSrv(), dg = Wp().rend.SceneGamma();
                            Wp().flash.SetScene(ds, dg, preGun ? nullptr : Wp().rend.DistortDepth());
                        }
                        g_fprof.Mark("distort");
                        if (hasMf || hasCam)
                            Wp().flash.Draw(ctx, rtv, Wp().rend.Depth(td.Width, td.Height), td.Width, td.Height, hw, Now(), hasMf ? &mf : nullptr,
                                         hasCam ? &wc : nullptr, Wp().set);
                        g_fprof.Mark("flash");
                        LauncherDraw(dev, ctx, rtv, bb, td.Width, td.Height, hw, look);
                        g_fprof.Mark("launcher");
                        if (part == kPartAll) HudCoverRestore(ctx, bb);
                    }
                    if (part != kPartGun && g_fp) {
                        vm::CrosshairSettings cs;
                        float cspread, calpha;
                        if (CrossFrame(cs, cspread, calpha)) g_cross.Draw(ctx, rtv, td.Width, td.Height, hw, cs, cspread, calpha);
                    }
                    if (part != kPartGun && g_fp) {
                        AcquireSRWLockShared(&g_mark.lock);
                        const double th = g_mark.tHit, tk = g_mark.tKill;
                        ReleaseSRWLockShared(&g_mark.lock);
                        const double nm = Now();
                        const int drew = g_cross.DrawHitMarker(ctx, rtv, td.Width, td.Height, hw, g_crossSet, th >= 0.0 ? static_cast<float>(nm - th) : -1.f,
                                                               tk >= 0.0 ? static_cast<float>(nm - tk) : -1.f);
                        if (drew & 1) ++g_markHitFrames;
                        if (drew & 2) ++g_markKillFrames;
                    }
                    g_fprof.Mark("cross");
                    g_gpu.End(ctx, gpuSlot);
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
    lu.Release();
    double ms = (Now() - t0) * 1000.0;
    {
        bool first = g_fp && !g_profWasFp;
        if (g_fp) g_profWasFp = true;
        if (first) g_profLogs = 0;
        if (g_slowFrameMs > 0 && (first || (ms >= g_slowFrameMs && g_profLogs < 40))) {
            ++g_profLogs;
            char segs[640];
            g_fprof.Format(segs, sizeof segs);
            const vm::DrawProf& dp = gunProf;
            g_log("frame-prof: %s %.1f ms (part %d, weapon %d): %s | gun draw: depth %.1f, scene copy %.1f, wh3 material %.1f, total %.1f",
                  first ? "first first-person frame" : "slow frame", ms, part, g_modelIdx, segs, dp.depth, dp.scene, dp.wh3, dp.total);
        }
    }
    g_st.msSum += ms;
    if (ms > g_st.msMax) g_st.msMax = ms;
    if (part != kPartGun) ++g_st.frames;
    double now = Now();
    if (now - g_st.tStat >= 5.0) {
        AcquireSRWLockExclusive(&g_gtLock);
        double gtSum = g_gtSum, gtMax = g_gtMax;
        unsigned gtN = g_gtN;
        g_gtSum = g_gtMax = 0;
        g_gtN = 0;
        ReleaseSRWLockExclusive(&g_gtLock);
        if (g_st.frames)
            g_log("viewmodel: 5s frames=%u draws=%u cpu avg %.3f ms max %.3f ms fp=%d test=%d | gpu avg %.3f ms max %.3f ms (%u timed, %u skipped) | "
                  "game thread avg %.3f ms max %.3f ms (%u frames)",
                  g_st.frames, g_st.draws, g_st.msSum / g_st.frames, g_st.msMax, (int)g_fp, (int)g_testLayer, g_gpu.n ? g_gpu.sum / g_gpu.n : 0.0,
                  g_gpu.max, g_gpu.n, g_gpu.busy, gtN ? gtSum / gtN : 0.0, gtMax, gtN);
        g_st.tStat = now;
        g_st.frames = g_st.draws = 0;
        g_st.msSum = g_st.msMax = 0;
        g_gpu.sum = g_gpu.max = 0;
        g_gpu.n = g_gpu.busy = 0;
    }
}
volatile LONG g_foreignPresents = 0;
__attribute__((noinline)) HRESULT STDMETHODCALLTYPE HkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    uintptr_t ret = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    if (ret == g_base + g_ra.presentReturn) {
        if (!(flags & DXGI_PRESENT_TEST)) {
            g_lastSc = sc, g_presentTid = GetCurrentThreadId();
            WdPresentBeat(sc);
            UiLayerPickTarget(sc);
            int part = UiLayerPresentPart();
            g_probeMute = true;
            InterlockedExchange(&g_rtPhase, 2);
            UiLayerPresentDraw(sc, part);
            InterlockedExchange(&g_rtPhase, 0);
            g_probeMute = false;
            UiLayerBbStep();
            UiLayerCompStep();
            UiLayerViewStep();
        }
    } else if (InterlockedIncrement(&g_foreignPresents) <= 3) {
        g_log("viewmodel: Present from elsewhere (return %p, thread %lu) - forwarded without drawing", (void*)ret,
              GetCurrentThreadId());
    }
    const bool own = ret == g_base + g_ra.presentReturn;
    if (own) InterlockedExchange(&g_rtPhase, 3);
    const HRESULT hr = g_origPresent(sc, sync, flags);
    if (own) InterlockedExchange(&g_rtPhase, 0);
    return hr;
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
    g_compRouteOk = ok && CompRouteVerify(exeBase);
    log("ui-layer: composite step route (vtable e+%llx, frame graph +0x%llx, pass list +0x%llx, anti-aliasing +0x%llx) %s",
        static_cast<unsigned long long>(g_ra.vtComposite), static_cast<unsigned long long>(g_ra.oFrameGraph),
        static_cast<unsigned long long>(g_ra.oPassList), static_cast<unsigned long long>(g_ra.oAaSetting),
        g_compRouteOk ? "ok" : "does not match this exe - without an upscaler the gun stays at Present (drawn over the UI)");
    g_viewRouteOk = ok && ViewRouteVerify(exeBase);
    log("ui-layer: frame view route (vtable e+%llx, sub-object e+%llx, UI slot +0x%llx) %s", static_cast<unsigned long long>(g_ra.vtView),
        static_cast<unsigned long long>(g_ra.vtViewSub), static_cast<unsigned long long>(kViewUiSlot),
        g_viewRouteOk ? "ok" : "does not match this exe - with anti-aliasing off the gun stays at Present (drawn over the UI)");
    return ok;
}
void SetPayloads(const wchar_t* mainIni, bool fullMod, const wchar_t* addonIds) {
    swprintf(g_mainIniName, 64, L"%ls", mainIni && mainIni[0] ? mainIni : L"tenzin_fps.ini");
    g_fullMod = fullMod;
    swprintf(g_addonIds, 1024, L"%ls", addonIds ? addonIds : L"");
}
const char* UnitKeys() { return g_unitKeys; }
bool UnitAllowed(const char* unitKey) { return g_fullMod || (unitKey && unitKey[0] && CsvHas(g_allowKeys, unitKey)); }
float EyeHeightFor(const char* unitKey, float def) {
    int ci = CharFor(unitKey);
    return ci >= 0 && g_chars[ci].eyeHeight > 0.f ? g_chars[ci].eyeHeight : def;
}
volatile LONG g_armsOnly = -1;
volatile LONG g_armsHinted = 0;
volatile LONG g_heroEntered = 0;
void HintHeroKey(const char* unitKey) {
    if (g_fp || g_heroEntered || g_armsHinted) return;
    const int ci = CharFor(unitKey);
    if (ci < 0) return;
    InterlockedExchange(&g_armsHinted, 1);
    const LONG was = InterlockedExchange(&g_armsWant, ci);
    const bool again = was != ci && g_preloadOnBattle && g_warmedThisBattle && InterlockedCompareExchange(&g_warm, 1, 0) == 0;
    if (again) InterlockedExchange(&g_reqRoster, 1);
    g_log("arms: hero '%s' reported before first person -> preload with %s%s", unitKey, g_chars[ci].units,
          was == ci ? " (already these arms)" : again ? " (preload runs again)" : "");
}
void NewBattleArms() {
    InterlockedExchange(&g_armsHinted, 0);
    InterlockedExchange(&g_heroEntered, 0);
    InterlockedExchange(&g_warmedThisBattle, 0);
    if (!g_fp) InterlockedExchange(&g_armsWant, g_armsOnly);
}
void SetHeroKey(const char* unitKey) {
    InterlockedExchange(&g_heroEntered, 1);
    AcquireSRWLockExclusive(&g_heroKeyLock);
    bool changed = strncmp(g_heroKey, unitKey ? unitKey : "", sizeof g_heroKey - 1) != 0;
    snprintf(g_heroKey, sizeof g_heroKey, "%s", unitKey ? unitKey : "");
    ReleaseSRWLockExclusive(&g_heroKeyLock);
    int ci = CharFor(unitKey);
    if (ci >= 0 || g_fullMod) InterlockedExchange(&g_armsWant, ci);
    if (changed) {
        InterlockedExchange(&g_reqRoster, 1);
        g_log("weapon: hero unit key '%s'", g_heroKey);
    }
}
void NoteNewBattle() { InterlockedExchange(&g_newBattle, 1); }
void Setup(uintptr_t exeBase, const wchar_t* dllDir, LogFn log) {
    g_base = exeBase;
    g_log = log;
    InitializeCriticalSection(&g_setLock);
    InitializeCriticalSection(&g_hookLock);
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_t0);
    swprintf(g_ini, MAX_PATH, L"%ls\\%ls", dllDir, g_mainIniName);
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
    if (!list[0] && !g_addonIds[0]) GetPrivateProfileStringW(L"viewmodel", L"model", L"tenzin_fps_vm\\m7\\m7.hvm", list, kListLen, g_ini);
    g_modelCount = 0;
    auto addModels = [&](wchar_t* tok, const wchar_t* from) {
        int before = g_modelCount;
        while (*tok && g_modelCount < kMaxModels) {
            wchar_t* end = wcschr(tok, L';');
            if (end) *end = 0;
            while (*tok == L' ') ++tok;
            if (*tok) swprintf(g_models[g_modelCount++], MAX_PATH * 2, L"%ls\\%ls", dllDir, tok);
            if (!end) {
                tok += wcslen(tok);
                break;
            }
            tok = end + 1;
        }
        if (*tok && g_modelCount >= kMaxModels) log("viewmodel: weapon limit %d reached - the rest of %ls is ignored", kMaxModels, from);
        return g_modelCount - before;
    };
    addModels(list, g_mainIniName);
    g_allowKeys[0] = 0;
    g_charN = 0;
    g_charUnits[0] = 0;
    wchar_t ids[1024];
    wcscpy(ids, g_addonIds);
    for (wchar_t* id = ids; *id;) {
        wchar_t* end = wcschr(id, L';');
        if (end) *end = 0;
        wchar_t addonIni[MAX_PATH * 2], units[512];
        swprintf(addonIni, MAX_PATH * 2, L"%ls\\tenzin_fps_addons\\%ls.ini", dllDir, id);
        GetPrivateProfileStringW(L"addon", L"models", L"", list, kListLen, addonIni);
        int got = addModels(list, id);
        GetPrivateProfileStringW(L"addon", L"units", L"", units, 512, addonIni);
        CsvAppend(g_allowKeys, sizeof g_allowKeys, units);
        wchar_t cu[256], ca[MAX_PATH * 2], ce[32];
        GetPrivateProfileStringW(L"character", L"units", L"", cu, 256, addonIni);
        if (cu[0] && g_charN < kMaxChars) {
            CharCfg& c = g_chars[g_charN++];
            WideCharToMultiByte(CP_UTF8, 0, cu, -1, c.units, sizeof c.units, nullptr, nullptr);
            GetPrivateProfileStringW(L"character", L"arms", L"", ca, MAX_PATH * 2, addonIni);
            c.arms[0] = 0;
            wchar_t* actx = nullptr;
            for (wchar_t* tok = wcstok(ca, L";", &actx); tok; tok = wcstok(nullptr, L";", &actx)) {
                while (*tok == L' ') ++tok;
                size_t n = wcslen(c.arms);
                if (*tok) swprintf(c.arms + n, MAX_PATH * 6 - n, L"%ls%ls\\%ls", n ? L";" : L"", dllDir, tok);
            }
            GetPrivateProfileStringW(L"character", L"eye_height", L"", ce, 32, addonIni);
            c.eyeHeight = ce[0] ? static_cast<float>(_wtof(ce)) : 0.f;
            if (!(c.eyeHeight >= 0.2f && c.eyeHeight <= 20.f)) c.eyeHeight = 0.f;
            log("viewmodel: weapon pack %ls: character units '%s', eye height %.3f, arms %ls", id, c.units, c.eyeHeight,
                c.arms[0] ? c.arms : L"(weapon's own)");
        }
        log("viewmodel: weapon pack %ls: %d weapons, units '%ls' (%ls)", id, got, units, addonIni);
        if (!end) break;
        id = end + 1;
    }
    {
        for (int ci = 1; ci <= kMaxChars && g_charN < kMaxChars; ++ci) {
            wchar_t sec[16], cu[256], ca[MAX_PATH * 2];
            if (ci == 1)
                wcscpy(sec, L"character");
            else
                swprintf(sec, 16, L"character%d", ci);
            GetPrivateProfileStringW(sec, L"units", L"", cu, 256, g_ini);
            GetPrivateProfileStringW(sec, L"arms", L"", ca, MAX_PATH * 2, g_ini);
            if (!cu[0] || !ca[0]) continue;
            CharCfg& c = g_chars[g_charN++];
            char u8[256];
            WideCharToMultiByte(CP_UTF8, 0, cu, -1, u8, sizeof u8, nullptr, nullptr);
            size_t n = 0;
            for (const char* p = u8; *p && n + 1 < sizeof c.units; ++p)
                if (*p != ' ' && *p != '\t') c.units[n++] = *p;
            c.units[n] = 0;
            c.arms[0] = 0;
            wchar_t* actx = nullptr;
            for (wchar_t* tok = wcstok(ca, L";", &actx); tok; tok = wcstok(nullptr, L";", &actx)) {
                while (*tok == L' ') ++tok;
                const size_t m = wcslen(c.arms);
                if (*tok) swprintf(c.arms + m, MAX_PATH * 6 - m, L"%ls%ls\\%ls", m ? L";" : L"", dllDir, tok);
            }
            wchar_t ce2[32];
            GetPrivateProfileStringW(sec, L"eye_height", L"", ce2, 32, g_ini);
            c.eyeHeight = ce2[0] ? static_cast<float>(_wtof(ce2)) : 0.f;
            if (!(c.eyeHeight >= 0.2f && c.eyeHeight <= 20.f)) c.eyeHeight = 0.f;
            const size_t ul = strlen(g_charUnits);
            snprintf(g_charUnits + ul, sizeof g_charUnits - ul, "%s%s", ul ? "," : "", c.units);
            log("viewmodel: character %d [%ls]: units '%s', arms %ls", g_charN, sec, c.units, c.arms);
        }
    }
    if (!g_fullMod && g_allowKeys[0]) {
        int only = -1;
        for (const char* p = g_allowKeys; *p && only != -2;) {
            const char* e = p;
            while (*e && *e != ',') ++e;
            char k[64];
            snprintf(k, sizeof k, "%.*s", static_cast<int>(e - p), p);
            int ci = CharFor(k);
            only = ci < 0 ? -2 : only == -1 || only == ci ? ci : -2;
            p = *e ? e + 1 : e;
        }
        if (only >= 0) {
            InterlockedExchange(&g_armsWant, only);
            InterlockedExchange(&g_armsOnly, only);
            log("viewmodel: core only, every allowed hero is character '%s' -> its arms from the start", g_chars[only].units);
        }
    }
    if (!g_modelCount) swprintf(g_models[g_modelCount++], MAX_PATH * 2, L"%ls\\tenzin_fps_vm\\m7\\m7.hvm", dllDir);
    for (int i = 0; i < g_modelCount; ++i) LoadAllSettings(i, g_w[i].set);
    snprintf(g_unitKeys, sizeof g_unitKeys, "%s", g_allowKeys);
    for (int i = 0; i < g_modelCount; ++i) CsvAppend(g_unitKeys, sizeof g_unitKeys, g_w[i].set.weaponOwner);
    if (g_charUnits[0]) {
        const size_t ul = strlen(g_unitKeys);
        snprintf(g_unitKeys + ul, sizeof g_unitKeys - ul, "%s%s", ul ? "," : "", g_charUnits);
    }
    log("viewmodel: main config %ls (%s), weapon packs '%ls', allowed units '%s', unit keys '%s'", g_mainIniName,
        g_fullMod ? "full mod - every hero may enter" : "core only - only the weapon packs' heroes may enter", g_addonIds, g_allowKeys, g_unitKeys);
    log("viewmodel: enabled=%d static=%d keys test=0x%02X reload=0x%02X anim=0x%02X model=0x%02X models=%d first=%ls "
        "fov=%.1f offset=%.3f,%.3f,%.3f rot=%.1f,%.1f,%.1f",
        g_enabled, g_staticOk, g_testKey, g_reloadKey, g_animKey, g_modelKey, g_modelCount, g_models[0], Wp().set.fovDeg,
        Wp().set.offset[0], Wp().set.offset[1], Wp().set.offset[2], Wp().set.rotDeg[0], Wp().set.rotDeg[1], Wp().set.rotDeg[2]);
    LauncherSetup(dllDir);
    g_slowFrameMs = static_cast<double>(GetPrivateProfileIntW(L"viewmodel", L"slow_frame_ms", 20, g_ini));
    if (g_enabled && (g_staticOk || !exeBase)) {
        g_preloadOnBattle = GetPrivateProfileIntW(L"viewmodel", L"preload_on_battle", 1, g_ini) != 0;
        g_bgTextures = GetPrivateProfileIntW(L"viewmodel", L"bg_textures", 1, g_ini) != 0;
        g_gpuTimer = GetPrivateProfileIntW(L"viewmodel", L"gpu_timer", 1, g_ini) != 0;
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
    LONG per = g_mag.perShot > 1 ? g_mag.perShot : 1;
    bool empty = g_mag.size > 0 && !(g_infAmmo && g_mag.disposable) && InterlockedExchangeAdd(&g_mag.ammo, -per) <= per;
    if (empty) InterlockedExchange(&g_reqFireLast, 1);
    QueueShot();
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
static volatile LONG g_refillMilli = 0;
void RefillAmmo(float frac) {
    if (!(frac > 0.f)) return;
    LONG m = static_cast<LONG>(std::lround((frac > 4.f ? 4.f : frac) * 1000.f));
    if (m > 0) InterlockedExchangeAdd(&g_refillMilli, m);
}
void AmmoRefillStep() {
    LONG m = InterlockedExchange(&g_refillMilli, 0);
    if (m <= 0) return;
    int done = 0;
    for (int i = 0; i < g_modelCount && i < kMaxModels; ++i) {
        LONG tot = EffAmmoTotal(i);
        if (tot <= 0) continue;
        LONG add = static_cast<LONG>((static_cast<LONG64>(tot) * m + 500) / 1000);
        if (i == g_mag.loadedModel && g_mag.size > 0) {
            for (;;) {
                LONG r = g_mag.reserve, a = g_mag.ammo;
                if (r < 0) break;
                LONG cap = tot - (a > 0 ? a : 0);
                if (cap < 0) cap = 0;
                LONG nr = r + add < cap ? r + add : cap;
                if (nr <= r) break;
                if (InterlockedCompareExchange(&g_mag.reserve, nr, r) == r) {
                    g_log("weapon: ability ammo refill -> %ls reserve %ld -> %ld (current)", g_w[i].set.weaponId, r, nr);
                    ++done;
                    break;
                }
            }
        } else {
            LONG r = g_mag.reserveByModel[i];
            if (r < 0) continue;
            LONG a = g_mag.ammoByModel[i];
            LONG cap = tot - (a >= 0 ? a : EffMagSize(i));
            if (cap < 0) cap = 0;
            LONG nr = r + add < cap ? r + add : cap;
            if (nr > r) {
                g_mag.reserveByModel[i] = nr;
                g_log("weapon: ability ammo refill -> %ls reserve %ld -> %ld (stowed)", g_w[i].set.weaponId, r, nr);
                ++done;
            }
        }
    }
    g_log("weapon: ability ammo refill %.3f of total -> %d weapon(s) topped up", m / 1000.0, done);
}
volatile LONG g_scopeLvlP1[kMaxModels] = {};
int g_scopeWheelAcc = 0;
int ScopeLevelIdx(int i) {
    if (i < 0 || i >= g_modelCount || i >= kMaxModels) return -1;
    const auto& s = g_w[i].set;
    if (s.scopeLevelCount < 1) return -1;
    const LONG v = g_scopeLvlP1[i];
    const int k = v > 0 ? static_cast<int>(v) - 1 : s.scopeLevelDefault;
    return k < 0 ? 0 : k >= s.scopeLevelCount ? s.scopeLevelCount - 1 : k;
}
void RefillForNewBattle() {
    for (int i = 0; i < kMaxModels; ++i) InterlockedExchange(&g_scopeLvlP1[i], 0);
    for (int i = 0; i < g_modelCount && i < kMaxModels; ++i) {
        const auto& s = g_w[i].set;
        if (s.scopeLevelCount > 0)
            g_log("scope: %ls zoom levels %d (%.1fx - %.1fx), default %.1fx, wheel while aiming", s.weaponId, s.scopeLevelCount, s.scopeLevels[0],
                  s.scopeLevels[s.scopeLevelCount - 1], s.scopeLevels[s.scopeLevelDefault]);
    }
    InterlockedExchange(&g_refillMilli, 0);
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
bool ShowSelMarkers() { return g_showSelMark != 0; }
bool KbAzerty() { return g_kbAzerty != 0; }
float MouseSensScale() { return static_cast<float>(g_mouseSensPct) * 0.01f; }
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
    InterlockedExchange(&g_mag.trigUp, 0);
    InterlockedExchange64(&g_mag.firePendUs, 0);
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
    if (g_mag.shellMode && g_mag.ammo > 0 && ((g_mag.reload != 0 && g_mag.trigUp) || (g_mag.reload == 0 && g_mag.shellWait))) {
        if (g_mag.reload != 0) InterlockedExchange(&g_mag.shellStop, 1);
        if (!g_mag.firePendUs) InterlockedExchange64(&g_mag.firePendUs, static_cast<LONG64>(Now() * 1e6));
        return false;
    }
    if (g_mag.reload != 0) return false;
    if (g_mag.ammo <= 0) {
        if (!g_mag.disposable) StartMagReload();
        return false;
    }
    if (g_mag.firePendUs) InterlockedExchange64(&g_mag.firePendUs, 0);
    return true;
}
bool FirePending() {
    const LONG64 t = g_mag.firePendUs;
    if (!t) return false;
    if (static_cast<LONG64>(Now() * 1e6) - t < static_cast<LONG64>(kFirePendSec * 1e6)) return true;
    InterlockedCompareExchange64(&g_mag.firePendUs, 0, t);
    return false;
}
void MagApply() {
    if (g_mag.loadedModel >= 0 && g_mag.loadedModel < kMaxModels && g_mag.size > 0) {
        g_mag.ammoByModel[g_mag.loadedModel] = g_mag.reload ? (g_mag.reload == 2 ? 0 : g_mag.ammo) : g_mag.ammo;
        g_mag.reserveByModel[g_mag.loadedModel] = g_mag.reserve;
    }
    InterlockedExchange(&g_mag.reload, 0);
    InterlockedExchange(&g_mag.reqAnim, 0);
    InterlockedExchange(&g_mag.shellMode, ShellReload() ? 1 : 0);
    InterlockedExchange(&g_mag.shellStop, 0);
    InterlockedExchange(&g_mag.shellWait, 0);
    InterlockedExchange64(&g_mag.firePendUs, 0);
    g_an.shellEnd = -1;
    InterlockedExchange(&g_spin.mode, Wp().set.fireMode == 1 ? 1 : 0);
    InterlockedExchange(&g_spin.state, 0);
    InterlockedExchange(&g_spin.upUs, static_cast<LONG>((Wp().set.spinUpTime > 0.f ? Wp().set.spinUpTime : 0.f) * 1e6f));
    InterlockedExchange(&g_spin.keepUs, static_cast<LONG>((Wp().set.spinKeep > 0.f ? Wp().set.spinKeep : 0.f) * 1e6f));
    g_an.spinSeen = 0;
    float rpm = EffRpm(g_modelIdx);
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
    if (ShellReload()) {
        const auto& rs = Wp().set.reloadShellClips;
        const float d = Wp().rend.ClipDuration(Wp().rend.FindClip(rs[0])) + Wp().rend.ClipDuration(Wp().rend.FindClip(rs[1])) +
                        static_cast<float>(size > 0 ? size : 1) * Wp().rend.ClipDuration(Wp().rend.FindClip(rs[2])) +
                        Wp().rend.ClipDuration(Wp().rend.FindClip(rs[3])) + 3.f;
        InterlockedExchange(&g_mag.refillUs[1], static_cast<LONG>(d * 1e6f));
        InterlockedExchange(&g_mag.refillUs[2], static_cast<LONG>(d * 1e6f));
        g_log("viewmodel: shell-by-shell reload (%s / %s / %s / %s), fallback refill %.1f s", rs[0], rs[1], rs[2], rs[3], d);
    }
    bool same = g_mag.loadedModel == g_modelIdx;
    g_mag.loadedModel = g_modelIdx;
    LONG keep = g_modelIdx < kMaxModels ? g_mag.ammoByModel[g_modelIdx] : -1;
    LONG ammo = size > 0 ? (keep >= 0 && keep <= size ? keep : size) : 0;
    if (same && size > 0 && g_mag.ammo >= 0 && g_mag.ammo <= size) ammo = g_mag.ammo;
    if (g_modelIdx < kMaxModels && g_spent[g_modelIdx]) ammo = 0;
    InterlockedExchange(&g_mag.size, size);
    InterlockedExchange(&g_mag.ammo, ammo);
    InterlockedExchange(&g_mag.perShot, Wp().set.ammoPerShot > 1 ? Wp().set.ammoPerShot : 1);
    if (g_mag.perShot > 1) g_log("viewmodel: %ld rounds per shot (ammo_per_shot)", g_mag.perShot);
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
void CastMarkerSet(const cm::Frame* f) {
    AcquireSRWLockExclusive(&g_cmPub.lock);
    g_cmPub.on = f && f->on;
    if (g_cmPub.on) g_cmPub.f = *f;
    ++g_cmPub.seq;
    ReleaseSRWLockExclusive(&g_cmPub.lock);
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
void GameThreadTime(double ms) { GameThreadAdd(ms); }
void HitMark(bool kill) {
    const double now = Now();
    AcquireSRWLockExclusive(&g_mark.lock);
    if (kill) {
        g_mark.tKill = now;
        ++g_mark.kills;
    } else {
        g_mark.tHit = now;
        ++g_mark.hits;
    }
    ReleaseSRWLockExclusive(&g_mark.lock);
}
bool HitMarkOn() { return g_crossSet.hitMarker; }
void HitMarkStats(int* hits, int* kills, int* hitFrames, int* killFrames) {
    AcquireSRWLockShared(&g_mark.lock);
    *hits = g_mark.hits;
    *kills = g_mark.kills;
    ReleaseSRWLockShared(&g_mark.lock);
    *hitFrames = g_markHitFrames;
    *killFrames = g_markKillFrames;
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
    if (!held && !g_mag.trigUp) InterlockedExchange(&g_mag.trigUp, 1);
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
void SetUiCovers(const float* r, int n) {
    AcquireSRWLockExclusive(&g_uiCoverLock);
    g_uiCoverN = 0;
    for (int i = 0; i < n && g_uiCoverN < kUiCoverMax; ++i) {
        float c[4];
        for (int k = 0; k < 4; ++k) c[k] = r[i * 4 + k] < 0.f ? 0.f : r[i * 4 + k] > 1.f ? 1.f : r[i * 4 + k];
        if (c[2] > c[0] && c[3] > c[1]) memcpy(g_uiCover[g_uiCoverN++], c, sizeof c);
    }
    ReleaseSRWLockExclusive(&g_uiCoverLock);
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
bool GunShieldOn() { return g_gunShieldOn != 0; }
bool ShieldWanted() { return g_sh.want != 0 && g_sh.allowed != 0; }
void ShieldGame(float* weight, float* moveScale, float* zoomKeep) {
    float w = g_sh.wUs * 1e-6f;
    w = w < 0.f ? 0.f : (w > 1.f ? 1.f : w);
    *weight = w;
    *moveScale = 1.f - (1.f - g_sh.moveMilli * 1e-3f) * w;
    *zoomKeep = 1.f - (1.f - g_sh.zoomMilli * 1e-3f) * w;
}
float ShieldCamKick() { return InterlockedExchange(&g_sh.camKickMdeg, 0) * 1e-3f; }
void CrouchImpulse(float s) {
    InterlockedExchangeAdd(&g_jlCrouchMilli, static_cast<LONG>(s * 1000.f + (s < 0.f ? -0.5f : 0.5f)));
}
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
bool MeleeActive() { return g_meleeOn != 0; }
void MeleeInput(bool attack, bool block) { InterlockedExchange(&g_meleeIn, (attack ? 1 : 0) | (block ? 2 : 0)); }
int TakeMeleeStrike(int* cat) {
    LONG s = InterlockedExchange(&g_meleeStrike, 0) - 1;
    if (s < 0) return -1;
    if (cat) *cat = static_cast<int>(s >> 4);
    return static_cast<int>(s & 15);
}
int CurrentMeleeCat() { return static_cast<int>(g_meleeCat); }
bool MeleeOwnNow(float* out) {
    AcquireSRWLockShared(&g_meleeOwnPub.lock);
    const bool any = g_meleeOwnPub.any;
    if (any) memcpy(out, g_meleeOwnPub.v, sizeof g_meleeOwnPub.v);
    ReleaseSRWLockShared(&g_meleeOwnPub.lock);
    return any;
}
const wchar_t* MeleeCatSection(int cat) { return cat >= 0 && cat < kMeleeCats ? kMeleeCatSec[cat] : L""; }
int WeaponDamagePct(int idx) {
    LONG p = (WpLive() && idx >= 0 && idx < kMaxModels) ? g_wDmgPct[idx] : 0;
    return p > 0 ? static_cast<int>(p) : 100;
}
int WeaponPenetration(int idx) {
    LONG p = (WpLive() && idx >= 0 && idx < kMaxModels) ? g_wPenSet[idx] : 0;
    return p > 0 ? static_cast<int>(p - 1) : -1;
}
float MeleeCatDamageMul(int cat) {
    LONG p = (WpLive() && cat >= 0 && cat < kMeleeCats) ? g_catDmgPct[cat] : 0;
    return p > 0 ? static_cast<float>(p) / 100.f : 1.f;
}
float MeleeCatReachAdd(int cat) {
    return (WpLive() && cat >= 0 && cat < kMeleeCats) ? static_cast<float>(g_catReachCm[cat]) / 100.f : 0.f;
}
void SetMpBattle(bool mp) {
    LONG v = mp ? 1 : 0;
    if (InterlockedExchange(&g_mpBattle, v) != v) {
        g_log("weapon-params: this battle is %s", mp ? "multiplayer (or the flag is not verified) - custom weapon stats off" : "single player");
        RefreshFireRate();
    }
}
void MeleeHitFeedback(int hits) {
    LONG t = g_meleeHitQTail;
    if (t - g_meleeHitQHead >= 32) return;
    g_meleeHitQ[t & 31] = hits;
    MemoryBarrier();
    InterlockedExchange(&g_meleeHitQTail, t + 1);
}
const char* WeaponProjectile(int idx) { return idx >= 0 && idx < g_modelCount ? g_w[idx].set.projectile : ""; }
float WeaponFireRange(int idx) { return idx >= 0 && idx < g_modelCount ? g_w[idx].set.fireRange : 0.f; }
const char* WeaponProjectileAds(int idx) { return idx >= 0 && idx < g_modelCount ? g_w[idx].set.projectileAds : ""; }
bool ScopeWheel(int delta) {
    const int i = g_modelIdx;
    const int k = ScopeLevelIdx(i);
    if (!Enabled() || !g_fp || !(g_stance & 1) || k < 0) {
        g_scopeWheelAcc = 0;
        return false;
    }
    g_scopeWheelAcc += delta;
    int n = k;
    while (g_scopeWheelAcc >= 120) g_scopeWheelAcc -= 120, ++n;
    while (g_scopeWheelAcc <= -120) g_scopeWheelAcc += 120, --n;
    const auto& s = g_w[i].set;
    if (n < 0) n = 0;
    if (n >= s.scopeLevelCount) n = s.scopeLevelCount - 1;
    if (n != k) {
        InterlockedExchange(&g_scopeLvlP1[i], n + 1);
        g_log("scope: %ls zoom %.1fx -> %.1fx (wheel)", s.weaponId, s.scopeLevels[k], s.scopeLevels[n]);
    }
    return true;
}
float ScopeCameraZoom() {
    const int i = g_modelIdx;
    const int k = ScopeLevelIdx(i);
    if (k < 0) return 0.f;
    const auto& s = g_w[i].set;
    const float mag = s.scopeLevels[k];
    return s.scopeZoom > 1.f ? mag / s.scopeZoom : mag;
}
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
int ShotPattern(float adsW, const float fwd[3], const float right[3], const float up[3], float (*out)[3], int maxOut, float* range, bool* focus) {
    const int idx = CurrentWeapon();
    const bool foc = adsW >= 0.5f;
    int n = 1;
    float fan = 0.f, rg = 0.f, cone = 0.f;
    if (idx >= 0 && idx < g_modelCount) {
        const auto& s = g_w[idx].set;
        n = foc ? s.pelletsAds : s.pellets;
        const float aw = std::fmin(std::fmax(adsW, 0.f), 1.f);
        cone = (s.pelletSpread + (s.pelletSpreadAds - s.pelletSpread) * aw) * 0.01745329f;
        fan = (foc ? s.pelletFanAds : s.pelletFan) * 0.01745329f;
        rg = foc && s.fireRangeAds > 0.f ? s.fireRangeAds : s.fireRange;
    }
    if (n > maxOut) n = maxOut;
    if (n < 1) n = 1;
    for (int k = 0; k < n; ++k) {
        g_spreadRng = g_spreadRng * 1664525u + 1013904223u;
        const float u = static_cast<float>((g_spreadRng >> 8) & 0xffffff) / 16777216.f;
        const float yw = fan > 0.f ? -fan + 2.f * fan * (static_cast<float>(k) + u) / static_cast<float>(n) : 0.f;
        const float c = std::cos(yw), sn = std::sin(yw);
        float d[3], r[3];
        for (int i = 0; i < 3; ++i) d[i] = fwd[i] * c + right[i] * sn, r[i] = right[i] * c - fwd[i] * sn;
        SpreadDir(d, r, up, out[k]);
        if (cone > 0.f) {
            g_spreadRng = g_spreadRng * 1664525u + 1013904223u;
            const float u1 = static_cast<float>((g_spreadRng >> 8) & 0xffffff) / 16777216.f;
            g_spreadRng = g_spreadRng * 1664525u + 1013904223u;
            const float th = 6.2831853f * static_cast<float>((g_spreadRng >> 8) & 0xffffff) / 16777216.f;
            const float rr = std::tan(cone * std::sqrt(u1)), a = rr * std::cos(th), b = rr * std::sin(th);
            float o[3], len = 0.f;
            for (int i = 0; i < 3; ++i) o[i] = out[k][i] + r[i] * a + up[i] * b, len += o[i] * o[i];
            len = 1.f / std::sqrt(len);
            for (int i = 0; i < 3; ++i) out[k][i] = o[i] * len;
        }
    }
    if (range) *range = rg;
    if (focus) *focus = foc;
    return n;
}
bool WeaponBurst(int idx, float* minDist, float* scatterAds, int* pierceAds, float* reachAds) {
    if (idx < 0 || idx >= g_modelCount || !g_w[idx].set.burst) return false;
    if (minDist) *minDist = g_w[idx].set.burstMin;
    if (scatterAds) *scatterAds = g_w[idx].set.burstScatterAds;
    if (pierceAds) *pierceAds = g_w[idx].set.burstPierceAds;
    if (reachAds) *reachAds = g_w[idx].set.burstReachAds;
    return true;
}
bool WeaponBurstCover(int idx, float* tick, int* cap, float* fanDeg, int* clip, float* lead, float* direct, float* react, float* fx, float* margin,
                      float* visFan) {
    if (idx < 0 || idx >= g_modelCount || !g_w[idx].set.burst || !(g_w[idx].set.burstCover > 0.f)) return false;
    *tick = g_w[idx].set.burstCover;
    *cap = g_w[idx].set.burstCoverMax;
    *fanDeg = g_w[idx].set.pelletFan;
    if (clip) *clip = g_w[idx].set.burstCoverClip;
    if (lead) *lead = g_w[idx].set.burstCoverLead;
    if (direct) *direct = g_w[idx].set.burstCoverDirect;
    if (react) *react = g_w[idx].set.burstCoverDirectReact;
    if (fx) *fx = g_w[idx].set.burstCoverDirectFx;
    if (margin) *margin = g_w[idx].set.burstCoverMargin;
    if (visFan) *visFan = g_w[idx].set.flameFan;
    return true;
}
bool WeaponBurstFocusDirect(int idx, float* dmg, float* react, float* fx) {
    if (idx < 0 || idx >= g_modelCount || !g_w[idx].set.burst || !(g_w[idx].set.burstDirectAds > 0.f)) return false;
    *dmg = g_w[idx].set.burstDirectAds;
    if (react) *react = g_w[idx].set.burstCoverDirectReact;
    if (fx) *fx = g_w[idx].set.burstCoverDirectFx;
    return true;
}
float BurstDistance(float minDist, float range) {
    if (!(range > minDist)) return range;
    g_spreadRng = g_spreadRng * 1664525u + 1013904223u;
    const float u = static_cast<float>((g_spreadRng >> 8) & 0xffffff) / 16777216.f;
    return std::sqrt(minDist * minDist + u * (range * range - minDist * minDist));
}
void BurstScatter(float radius, float* dx, float* dz) {
    g_spreadRng = g_spreadRng * 1664525u + 1013904223u;
    const float u = static_cast<float>((g_spreadRng >> 8) & 0xffffff) / 16777216.f;
    g_spreadRng = g_spreadRng * 1664525u + 1013904223u;
    const float th = 6.2831853f * static_cast<float>((g_spreadRng >> 8) & 0xffffff) / 16777216.f;
    const float r = radius > 0.f ? radius * std::sqrt(u) : 0.f;
    *dx = r * std::cos(th);
    *dz = r * std::sin(th);
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
