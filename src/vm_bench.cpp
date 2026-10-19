#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <xaudio2.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>
namespace benchclock {
constexpr LONG64 kHz = 1LL << 20;
LARGE_INTEGER g_realHz = {}, g_real0 = {};
LONG64 g_base = kHz;
LONG64 g_frame = 0;
bool g_replay = false;
LONG64 Live() {
    if (!g_realHz.QuadPart) {
        ::QueryPerformanceFrequency(&g_realHz);
        ::QueryPerformanceCounter(&g_real0);
    }
    LARGE_INTEGER c;
    ::QueryPerformanceCounter(&c);
    return g_base + static_cast<LONG64>(static_cast<__int128>(c.QuadPart - g_real0.QuadPart) * kHz / g_realHz.QuadPart);
}
BOOL WINAPI Qpc(LARGE_INTEGER* c) {
    c->QuadPart = g_replay ? g_frame : Live();
    return TRUE;
}
BOOL WINAPI Qpf(LARGE_INTEGER* f) {
    f->QuadPart = kHz;
    return TRUE;
}
void ToLive() {
    g_replay = false;
    g_base += g_frame - Live();
}
}
#define QueryPerformanceCounter benchclock::Qpc
#define QueryPerformanceFrequency benchclock::Qpf
#include "../src/vm_game.cpp"
namespace vm {
bool DebugBoneWorld(unsigned b, float m12[12]);
}
int g_benchStrikes = 0;
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency
#include <d3dcompiler.h>
#include <clocale>
#include <cstdarg>
#include <cstdlib>
namespace benchpeek {
template <class Tag, typename Tag::Type M>
struct Peek {
    friend typename Tag::Type Member(Tag) { return M; }
};
struct Voices {
    using Type = IXAudio2SourceVoice* (vm::GunSound::*)[24];
    friend Type Member(Voices);
};
struct NVoices {
    using Type = int vm::GunSound::*;
    friend Type Member(NVoices);
};
struct Started {
    using Type = double (vm::GunSound::*)[24];
    friend Type Member(Started);
};
struct Rng {
    using Type = unsigned vm::GunSound::*;
    friend Type Member(Rng);
};
struct Last {
    using Type = int vm::GunSound::*;
    friend Type Member(Last);
};
template struct Peek<Voices, &vm::GunSound::voices_>;
template struct Peek<NVoices, &vm::GunSound::nVoices_>;
template struct Peek<Started, &vm::GunSound::started_>;
template struct Peek<Rng, &vm::GunSound::rng_>;
template struct Peek<Last, &vm::GunSound::last_>;
}
namespace bench {
constexpr float kPi = 3.14159265358979f;
FILE* g_logFile = nullptr;
void BLog(const char* fmt, ...) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char body[2048];
    va_list a;
    va_start(a, fmt);
    vsnprintf(body, sizeof body, fmt, a);
    va_end(a);
    printf("%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);
    fflush(stdout);
    if (g_logFile) {
        fprintf(g_logFile, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);
        fflush(g_logFile);
    }
}
struct Cfg {
    float eyeHeight = 1.75f, eyeForward = 0.12f, nearClip = 0.1f, fovOverride = 0.f;
    float sensitivity = 0.07f;
    bool invertY = false;
    bool fireEnabled = true;
    float fireInterval = 0.1f, fireRange = 120.f, fireForward = 0.6f, muzzleDepth = 1.2f;
    float sprintToFire = 0.2f, tacSprintToFire = 0.3f;
    float adsTimeSprint = 0.35f, adsTimeTac = 0.55f;
    float runSpeed = 6.f, sprintSpeed = 10.f, tacSprintSpeed = 12.5f, adsMoveScale = 0.6f, accelTau = 0.06f;
    float adsZoom = 1.25f, adsTime = 0.25f;
} cfg;
float IniF(const wchar_t* sec, const wchar_t* key, float def, const wchar_t* ini) {
    wchar_t buf[64];
    GetPrivateProfileStringW(sec, key, L"", buf, 64, ini);
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    return (end != buf && std::isfinite(v)) ? static_cast<float>(v) : def;
}
void LoadCfg(const wchar_t* ini) {
    Cfg c = cfg;
    c.eyeHeight = IniF(L"camera", L"eye_height", c.eyeHeight, ini);
    c.eyeForward = IniF(L"camera", L"eye_forward", c.eyeForward, ini);
    c.nearClip = IniF(L"camera", L"near_clip", c.nearClip, ini);
    c.fovOverride = IniF(L"camera", L"fov_override", c.fovOverride, ini);
    c.sensitivity = IniF(L"mouse", L"sensitivity", c.sensitivity, ini);
    c.invertY = GetPrivateProfileIntW(L"mouse", L"invert_y", 0, ini) != 0;
    c.fireEnabled = GetPrivateProfileIntW(L"fire", L"enabled", 1, ini) != 0;
    c.fireInterval = IniF(L"fire", L"interval", c.fireInterval, ini);
    c.fireRange = IniF(L"fire", L"range", c.fireRange, ini);
    c.fireForward = IniF(L"fire", L"launch_forward", c.fireForward, ini);
    if (c.fireInterval < 0.03f || c.fireInterval > 5.f) c.fireInterval = 0.1f;
    if (c.fireRange < 5.f || c.fireRange > 2000.f) c.fireRange = 120.f;
    if (c.fireForward < 0.f || c.fireForward > 5.f) c.fireForward = 0.6f;
    c.muzzleDepth = IniF(L"fire", L"muzzle_depth", c.muzzleDepth, ini);
    if (c.muzzleDepth < 0.2f || c.muzzleDepth > 5.f) c.muzzleDepth = 1.2f;
    c.runSpeed = IniF(L"move", L"run_speed", c.runSpeed, ini);
    c.sprintSpeed = IniF(L"move", L"sprint_speed", c.sprintSpeed, ini);
    c.adsMoveScale = IniF(L"move", L"ads_move_scale", c.adsMoveScale, ini);
    c.sprintToFire = IniF(L"fire", L"sprint_to_fire", c.sprintToFire, ini);
    c.tacSprintSpeed = IniF(L"move", L"tac_sprint_speed", c.tacSprintSpeed, ini);
    c.tacSprintToFire = IniF(L"fire", L"tac_sprint_to_fire", c.tacSprintToFire, ini);
    if (c.tacSprintSpeed < 0.5f || c.tacSprintSpeed > 40.f) c.tacSprintSpeed = 12.5f;
    if (c.tacSprintToFire < 0.f || c.tacSprintToFire > 2.f) c.tacSprintToFire = 0.3f;
    c.adsZoom = IniF(L"camera", L"ads_zoom", c.adsZoom, ini);
    c.adsTime = IniF(L"camera", L"ads_time", c.adsTime, ini);
    c.adsTimeSprint = IniF(L"camera", L"ads_time_sprint", c.adsTimeSprint, ini);
    c.adsTimeTac = IniF(L"camera", L"ads_time_tac", c.adsTimeTac, ini);
    if (c.adsTimeSprint < 0.01f || c.adsTimeSprint > 2.f) c.adsTimeSprint = 0.35f;
    if (c.adsTimeTac < 0.01f || c.adsTimeTac > 2.f) c.adsTimeTac = 0.55f;
    if (c.sprintSpeed < 0.5f || c.sprintSpeed > 40.f) c.sprintSpeed = 10.f;
    if (c.adsMoveScale < 0.05f || c.adsMoveScale > 1.f) c.adsMoveScale = 0.6f;
    if (c.sprintToFire < 0.f || c.sprintToFire > 2.f) c.sprintToFire = 0.2f;
    if (c.adsZoom < 1.f || c.adsZoom > 8.f) c.adsZoom = 1.25f;
    if (c.adsTime < 0.01f || c.adsTime > 2.f) c.adsTime = 0.25f;
    c.accelTau = IniF(L"move", L"accel", c.accelTau, ini);
    if (c.runSpeed < 0.5f || c.runSpeed > 40.f) c.runSpeed = 6.f;
    if (c.accelTau < 0.f || c.accelTau > 1.f) c.accelTau = 0.06f;
    if (c.nearClip < 0.01f || c.nearClip > 5.f) c.nearClip = 0.1f;
    if (c.eyeHeight < 0.2f || c.eyeHeight > 20.f) c.eyeHeight = 1.75f;
    if (c.sensitivity <= 0.f || c.sensitivity > 5.f) c.sensitivity = 0.07f;
    cfg = c;
    vmgame::PublishAdsTime(cfg.adsTime, cfg.adsTimeSprint, cfg.adsTimeTac);
    BLog("bench: config sens=%.3f invert_y=%d ads_zoom=%.2f ads_time=%.2f eye=%.2f fire=%d interval=%.3f run=%.1f sprint=%.1f tac=%.1f",
         cfg.sensitivity, cfg.invertY, cfg.adsZoom, cfg.adsTime, cfg.eyeHeight, cfg.fireEnabled, cfg.fireInterval, cfg.runSpeed,
         cfg.sprintSpeed, cfg.tacSprintSpeed);
}
float BaseFov() { return cfg.fovOverride > 0.2f && cfg.fovOverride < 2.8f ? cfg.fovOverride : 1.1345f; }
struct Box {
    float lo[3], hi[3], col[3];
    int kind;
    float cx, cy;
};
constexpr int kMaxBoxes = 64;
Box g_boxes[kMaxBoxes];
int g_nBoxes = 0;
void AddBox(float x0, float y0, float z0, float x1, float y1, float z1, float r, float g, float b, int kind = 0, float cx = 0, float cy = 0) {
    if (g_nBoxes >= kMaxBoxes) return;
    Box& o = g_boxes[g_nBoxes++];
    o = {{x0, y0, z0}, {x1, y1, z1}, {r, g, b}, kind, cx, cy};
}
void BuildRange() {
    struct T {
        float x, z, size;
    } t[3] = {{-2.5f, 10.f, 1.2f}, {2.5f, 25.f, 1.6f}, {0.f, 50.f, 2.4f}};
    for (const T& b : t) {
        float h = b.size * 0.5f, cy = 1.5f;
        AddBox(b.x - h, cy - h, b.z, b.x + h, cy + h, b.z + 0.05f, 0.93f, 0.92f, 0.88f, 1, b.x, cy);
        AddBox(b.x - 0.04f, 0.f, b.z + 0.05f, b.x + 0.04f, cy, b.z + 0.13f, 0.45f, 0.33f, 0.20f);
    }
    AddBox(-30.f, 0.f, 80.f, 30.f, 16.f, 81.f, 0.60f, 0.55f, 0.46f, 2);
    for (int z = 5; z <= 60; z += 5)
        for (int s = -1; s <= 1; s += 2) {
            float x = 6.f * s, hgt = z % 10 ? 0.3f : 0.6f;
            AddBox(x - 0.15f, 0.f, z - 0.15f, x + 0.15f, hgt, z + 0.15f, 0.75f, 0.70f, 0.55f);
        }
}
struct Hit {
    bool hit;
    float dist, n[3];
    int what;
};
Hit Trace(const float o[3], const float d[3], float maxDist) {
    Hit h = {false, maxDist, {0.f, 1.f, 0.f}, -1};
    if (d[1] < -1e-6f) {
        float t = -o[1] / d[1];
        if (t > 0.f && t < h.dist) h = {true, t, {0.f, 1.f, 0.f}, 0};
    }
    for (int i = 0; i < g_nBoxes; ++i) {
        const Box& b = g_boxes[i];
        float tmin = -1e30f, tmax = 1e30f, sign = 0.f;
        int axis = -1;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
            if (std::fabs(d[a]) < 1e-9f) {
                if (o[a] < b.lo[a] || o[a] > b.hi[a]) miss = true;
                continue;
            }
            float inv = 1.f / d[a], t1 = (b.lo[a] - o[a]) * inv, t2 = (b.hi[a] - o[a]) * inv, sg = -1.f;
            if (t1 > t2) {
                float x = t1;
                t1 = t2;
                t2 = x;
                sg = 1.f;
            }
            if (t1 > tmin) tmin = t1, axis = a, sign = sg;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) miss = true;
        }
        if (miss || axis < 0 || tmin <= 0.f || tmin >= h.dist) continue;
        h.hit = true;
        h.dist = tmin;
        h.n[0] = h.n[1] = h.n[2] = 0.f;
        h.n[axis] = sign;
        h.what = b.kind == 1 ? 1 : b.kind == 2 ? 2 : 3;
    }
    return h;
}
struct Hole {
    float p[3], n[3], r;
    int burst;
};
constexpr int kMaxHoles = 200;
Hole g_holes[kMaxHoles];
int g_holeN = 0, g_holeHead = 0;
struct Player {
    float yaw = 0, pitch = 0;
    float pos[2] = {0, 0}, vel[2] = {0, 0};
    float adsT = 0, camSpeed = 0;
    bool ads = false, sprint = false, tac = false, shiftPrev = true;
    double fireBlock = 0, nextShot = 0, tLastFrame = 0;
    int adsKind = 0;
    bool exitTac = false;
    kick::CamKick camKick;
} P;
struct ViewState {
    float eye[3];
    kick::View kv;
    float fovY;
} g_vs;
struct Burst {
    int id = 0, shots = 0;
    double tStart = 0, tLast = -1;
    float refPitch = 0;
    float kickPitch = 0, kickYaw = 0;
    float climbPeak = 0, netPeak = 0;
    float punchMax = 0, backMax = 0, gunPitchMax = 0, rollMax = 0;
    bool open = false;
} g_burst, g_lastBurst;
struct Sample {
    double t;
    float net, kickP, punch, back, gunPitch, roll;
};
constexpr int kHist = 2048;
Sample g_hist[kHist];
int g_histHead = 0, g_histN = 0;
LONG64 RealQpc() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}
double RealMs(LONG64 d) {
    return static_cast<double>(d) * 1000.0 / static_cast<double>(benchclock::g_realHz.QuadPart);
}
enum { kSnd, kCam, kGun, kFx, kSway, kSus, kIk, kJit, kBlend, kLayers };
const wchar_t* const kLayerName[kLayers] = {L"枪声", L"镜头踢动", L"枪身后坐", L"火焰/气体/拖光/抛壳", L"晃动/起伏/呼吸", L"连射持续曲线", L"双手跟枪",
                                            L"两段回稳抖动", L"两套权重S曲线"};
const wchar_t kLayerTag[kLayers + 1] = L"声镜枪焰晃续手抖混";
bool g_layer[kLayers] = {true, true, true, true, true, true, true, true, true};
int g_gasStyle = -1;
int g_shellFollow = -1;
int GasStyleNow() { return g_gasStyle >= 0 ? g_gasStyle : vmgame::Wp().set.flashGasStyle; }
bool g_onsetOn = true;
bool g_shakeOn = true;
float g_shView[2] = {};
int LayerKeyLabel(int k, wchar_t* out, size_t cap) {
    return k == kJit ? swprintf(out, cap, L"J") : k == kBlend ? swprintf(out, cap, L"B") : swprintf(out, cap, L"F%d", k + 1);
}
bool AnyLayerOff() {
    for (bool b : g_layer)
        if (!b) return true;
    return false;
}
bool OverridesActive() { return AnyLayerOff() || !g_onsetOn; }
void LayerTags(const bool* on, wchar_t* out) {
    for (int k = 0; k < kLayers; ++k) out[k] = on[k] ? kLayerTag[k] : L'·';
    out[kLayers] = 0;
}
const int kFpsModes[4] = {0, 30, 60, 120};
int g_fpsMode = 0;
int g_refreshHz = 60;
LONG64 StepTicks(int mode) {
    int hz = kFpsModes[mode] ? kFpsModes[mode] : g_refreshHz;
    return (benchclock::kHz + hz - 1) / hz;
}
void FpsLabel(int mode, wchar_t* out, size_t n) {
    if (kFpsModes[mode]) swprintf(out, n, L"%d 帧", kFpsModes[mode]);
    else swprintf(out, n, L"不限（垂直同步 %d Hz）", g_refreshHz);
}
struct ShotRec {
    double tFire, tDue;
    double gunAt, camAt, sndAt;
    double sndReal;
    LONG64 qpcFire;
    float hit[3];
    int what;
    float aimPitch, aimYaw;
    float climb, punch;
    float gunBack, gunPitch;
    long ammoAfter;
    bool gunPending, camPending;
};
struct RecSamp {
    double t;
    float climb, punch, total, back, pitch, roll;
    float kBack, kPitch;
    float ikR, ikL, ikResR, ikResL;
    float jPitch, jRoll;
    float sPitch;
    float blendW;
};
struct DenseSamp {
    float punch, back, pitch;
};
constexpr double kDenseStep = 0.0005;
constexpr int kRecShots = 256, kRecSamps = 4096, kDense = 16384;
struct Rec {
    bool on, replay, ads, released, truncated;
    int id;
    wchar_t label[40], weapon[64];
    bool layers[kLayers];
    int fpsMode;
    LONG64 step;
    double t0, tPress, tRelease, tLastShot;
    float kick;
    int nShot, nSamp, tracers, swayMismatch;
    long ammo0, magSize;
    double lastR;
    float prevSus[2];
    float prevJit;
    bool stalled;
    LONG64 lastFrameQpc;
    double frameMsSum, frameMsMax;
    int frameN;
    int nDense;
    ShotRec shots[kRecShots];
    RecSamp samps[kRecSamps];
    DenseSamp dense[kDense];
};
Rec g_rec, g_recRef;
struct RecSum {
    bool valid, truncated, stalled;
    int id, shots, tracers, swayMismatch;
    wchar_t label[40], weapon[64], layerTags[kLayers + 1];
    bool replay, ads;
    int fpsMode;
    double stepMs;
    long ammoUsed;
    double pressToFirst, dueAvg, dueMax, gunFirst, camFirst, sndSubmit;
    double sndRealAvg, sndRealMin, sndRealMax;
    int sndRealN;
    double climbPeak, punchPeak, totalPeak, backPeak, pitchPeak, rollPeak;
    double punchSettle, climbSettle, backSettle, pitchSettle;
    double punchPeakT, backPeakT, pitchPeakT, punchSettleT, backSettleT, pitchSettleT;
    double susBackPeak, susPitchPeak;
    double jitPitchPeak, jitRollPeak;
    int ikFrames;
    double ikShiftPeak[2], ikResMax;
    double frameMsAvg, frameMsMax;
    uint64_t hAll, hHoles, hCam, hGun, hSpring;
    wchar_t same[96];
};
RecSum g_lastSum = {};
RecSum g_hist8[8] = {};
int g_hist8N = 0;
enum { kRpHip, kRpAds, kRpSwitch };
struct Replay {
    bool armed, active, ads;
    int mode;
    LONG64 step;
    double t0, pre, hold, tail;
    double adsOn, adsOff;
    wchar_t label[40];
} g_rp = {};
double g_fireOrigin = 0;
constexpr unsigned kSeedCam = 1234u, kSeedGun = 4321u, kSeedFlash = 777u, kSeedSnd = 0x13579bdu, kSeedJit = 2468u;
struct Gfx {
    HWND hwnd = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* depthTex = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    UINT w = 0, h = 0;
    ID3D11VertexShader *vsWorld = nullptr, *vsSky = nullptr, *vsHud = nullptr;
    ID3D11PixelShader *psWorld = nullptr, *psSky = nullptr, *psHud = nullptr;
    ID3D11InputLayout *ilWorld = nullptr, *ilHud = nullptr;
    ID3D11Buffer *cb = nullptr, *vbStatic = nullptr, *vbHoles = nullptr, *vbHud = nullptr;
    UINT staticCount = 0;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState *dsWorld = nullptr, *dsDecal = nullptr, *dsOff = nullptr;
    ID3D11RasterizerState* rs = nullptr;
    ID3D11SamplerState* samp = nullptr;
    ID3D11Texture2D* textTex = nullptr;
    ID3D11ShaderResourceView* textSrv = nullptr;
} G;
template <class T>
void SafeRelease(T*& p) {
    if (p) p->Release();
    p = nullptr;
}
const char kHlsl[] = R"(cbuffer Frame : register(b0) {
    row_major float4x4 vp;
    float3 eye;  float fogDist;
    float3 camR; float tanH;
    float3 camU; float aspect;
    float3 camF; float pad0;
    float2 screen; float2 pad1;
};
float3 SkyCol(float3 d) {
    float3 hor = float3(0.80, 0.84, 0.88), zen = float3(0.32, 0.50, 0.78), gnd = float3(0.50, 0.52, 0.46);
    if (d.y >= 0) return lerp(hor, zen, pow(saturate(d.y), 0.55));
    return lerp(hor, gnd, saturate(-d.y * 6));
}
struct VIn { float3 pos : POSITION; float4 col : COLOR; float2 uv : TEXCOORD0; float mode : TEXCOORD1; };
struct VOut { float4 sv : SV_Position; float3 wp : TEXCOORD2; float4 col : COLOR; float2 uv : TEXCOORD0; nointerpolation float mode : TEXCOORD1; };
VOut VSWorld(VIn i) {
    VOut o;
    o.sv = mul(float4(i.pos, 1), vp);
    o.wp = i.pos; o.col = i.col; o.uv = i.uv; o.mode = i.mode;
    return o;
}
float LineAA(float dist, float halfW, float fw) { return saturate(1 - (dist - halfW) / max(fw, 1e-5)); }
float Grid(float2 p, float2 fw, float spacing, float halfW) {
    float2 g = abs(frac(p / spacing + 0.5) - 0.5) * spacing;
    return max(LineAA(g.x, halfW, fw.x), LineAA(g.y, halfW, fw.y));
}
float4 PSWorld(VOut i) : SV_Target {
    float3 fwP = fwidth(i.wp);
    float2 fwU = fwidth(i.uv);
    float r = length(i.uv);
    float fwR = fwidth(r);
    float3 c = i.col.rgb;
    float a = 1;
    int m = (int)(i.mode + 0.5);
    if (m == 1) {
        float2 p = i.wp.xz, fw = fwP.xz;
        c *= 1 - 0.28 * Grid(p, fw, 1.0, 0.015);
        c *= 1 - 0.35 * Grid(p, fw, 5.0, 0.04);
        c = lerp(c, float3(1.00, 0.86, 0.25), LineAA(abs(i.wp.z - 10), 0.08, fwP.z));
        c = lerp(c, float3(1.00, 0.55, 0.15), LineAA(abs(i.wp.z - 25), 0.10, fwP.z));
        c = lerp(c, float3(0.95, 0.20, 0.15), LineAA(abs(i.wp.z - 50), 0.14, fwP.z));
    } else if (m == 2) {
        float ring = LineAA(abs(frac(r / 0.1 + 0.5) - 0.5) * 0.1, 0.003, fwR);
        if (r > 0.505) ring = 0;
        c = lerp(c, float3(0.06, 0.06, 0.06), ring);
        float cross = max(LineAA(abs(i.uv.x), 0.0015, fwU.x), LineAA(abs(i.uv.y), 0.0015, fwU.y));
        c = lerp(c, float3(0.15, 0.15, 0.15), cross * 0.5);
        c = lerp(c, float3(0.86, 0.10, 0.08), LineAA(r, 0.05, fwR));
    } else if (m == 3) {
        a = LineAA(r, 1.0, fwR) * i.col.a;
        c = r < 0.55 ? float3(0.03, 0.03, 0.03) : i.col.rgb;
    } else if (m == 4) {
        float2 p = i.wp.xy, fw = fwP.xy;
        c *= 1 - 0.25 * Grid(p, fw, 1.0, 0.02);
        c *= 1 - 0.35 * Grid(p, fw, 5.0, 0.05);
    }
    float3 v = i.wp - eye;
    float d = length(v);
    c = lerp(c, SkyCol(v / max(d, 1e-4)), (1 - exp(-d / fogDist)) * 0.8);
    return float4(c * a, a);
}
struct SOut { float4 sv : SV_Position; float2 ndc : TEXCOORD0; };
SOut VSSky(uint id : SV_VertexID) {
    SOut o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.ndc = float2(t.x * 2 - 1, 1 - t.y * 2);
    o.sv = float4(o.ndc, 0.5, 1);
    return o;
}
float4 PSSky(SOut i) : SV_Target {
    float3 d = normalize(camF + camR * (i.ndc.x * tanH * aspect) + camU * (i.ndc.y * tanH));
    return float4(SkyCol(d), 1);
}
struct HIn { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };
struct HOut { float4 sv : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR; };
HOut VSHud(HIn i) {
    HOut o;
    o.sv = float4(i.pos.x / screen.x * 2 - 1, 1 - i.pos.y / screen.y * 2, 0, 1);
    o.uv = i.uv; o.col = i.col;
    return o;
}
Texture2D txt : register(t0);
SamplerState pt : register(s0);
float4 PSHud(HOut i) : SV_Target {
    if (i.uv.x < 0) return float4(i.col.rgb * i.col.a, i.col.a);
    float3 t = txt.SampleLevel(pt, i.uv, 0).rgb;
    return float4(t, max(t.r, max(t.g, t.b))) * i.col.a;
}
)";
struct FrameCB {
    float vp[16];
    float eye[3], fogDist;
    float camR[3], tanH;
    float camU[3], aspect;
    float camF[3], pad0;
    float screen[2], pad1[2];
};
struct WV {
    float p[3], c[4], uv[2], mode;
};
struct HV {
    float p[2], uv[2], c[4];
};
constexpr int kHudVerts = 16384;
float g_ui = 1.f;
constexpr int kLines = 13;
UINT g_textW = 1240, g_textH = kLines * 19 + 8;
int g_lineH = 19;
int Px(float v) { return static_cast<int>(v * g_ui + 0.5f); }
using FnCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                   ID3DBlob**, ID3DBlob**);
ID3DBlob* Compile(const char* entry, const char* target) {
    static FnCompile fn = nullptr;
    if (!fn) {
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        fn = m ? reinterpret_cast<FnCompile>(reinterpret_cast<void*>(GetProcAddress(m, "D3DCompile"))) : nullptr;
    }
    if (!fn) {
        BLog("bench: d3dcompiler_47.dll / D3DCompile not available");
        return nullptr;
    }
    ID3DBlob *code = nullptr, *err = nullptr;
    HRESULT hr = fn(kHlsl, sizeof kHlsl - 1, "vm_bench", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) BLog("bench: shader %s failed (0x%08lx): %s", entry, static_cast<unsigned long>(hr),
                         err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
    SafeRelease(err);
    return SUCCEEDED(hr) ? code : nullptr;
}
void Quad(WV* v, int& n, const float a[3], const float b[3], const float c[3], const float d[3], const float col[4], const float uv[4][2],
          float mode) {
    const float* p[4] = {a, b, c, d};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k : idx) {
        WV& o = v[n++];
        memcpy(o.p, p[k], sizeof o.p);
        memcpy(o.c, col, sizeof o.c);
        o.uv[0] = uv[k][0];
        o.uv[1] = uv[k][1];
        o.mode = mode;
    }
}
int BuildStatic(WV* v) {
    int n = 0;
    const float zero[4][2] = {};
    {
        const float a[3] = {-200, 0, -150}, b[3] = {200, 0, -150}, c[3] = {200, 0, 300}, d[3] = {-200, 0, 300};
        const float col[4] = {0.42f, 0.47f, 0.34f, 1};
        Quad(v, n, a, b, c, d, col, zero, 1);
    }
    for (int i = 0; i < g_nBoxes; ++i) {
        const Box& B = g_boxes[i];
        const float *lo = B.lo, *hi = B.hi;
        float P8[8][3];
        for (int k = 0; k < 8; ++k) {
            P8[k][0] = k & 1 ? hi[0] : lo[0];
            P8[k][1] = k & 2 ? hi[1] : lo[1];
            P8[k][2] = k & 4 ? hi[2] : lo[2];
        }
        struct F {
            int c[4];
            float shade;
        } faces[6] = {{{0, 1, 3, 2}, 0.92f}, {{4, 5, 7, 6}, 0.70f}, {{0, 4, 6, 2}, 0.80f},
                      {{1, 5, 7, 3}, 0.75f}, {{2, 3, 7, 6}, 1.00f}, {{0, 1, 5, 4}, 0.50f}};
        for (int f = 0; f < 6; ++f) {
            float col[4] = {B.col[0] * faces[f].shade, B.col[1] * faces[f].shade, B.col[2] * faces[f].shade, 1};
            float uv[4][2] = {};
            float mode = 0;
            if (f == 0 && B.kind == 1) {
                mode = 2;
                for (int k = 0; k < 4; ++k) {
                    uv[k][0] = P8[faces[f].c[k]][0] - B.cx;
                    uv[k][1] = P8[faces[f].c[k]][1] - B.cy;
                }
                for (int k = 0; k < 3; ++k) col[k] = B.col[k];
            } else if (f == 0 && B.kind == 2) {
                mode = 4;
            }
            Quad(v, n, P8[faces[f].c[0]], P8[faces[f].c[1]], P8[faces[f].c[2]], P8[faces[f].c[3]], col, uv, mode);
        }
    }
    return n;
}
bool CreateTargets() {
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(G.sc->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&bb))) || !bb) return false;
    D3D11_TEXTURE2D_DESC td;
    bb->GetDesc(&td);
    G.w = td.Width;
    G.h = td.Height;
    HRESULT hr = G.dev->CreateRenderTargetView(bb, nullptr, &G.rtv);
    bb->Release();
    if (FAILED(hr)) return false;
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = G.w;
    dd.Height = G.h;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(G.dev->CreateTexture2D(&dd, nullptr, &G.depthTex))) return false;
    return SUCCEEDED(G.dev->CreateDepthStencilView(G.depthTex, nullptr, &G.dsv));
}
void Resize(UINT w, UINT h) {
    G.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    G.ctx->ClearState();
    G.ctx->Flush();
    SafeRelease(G.rtv);
    SafeRelease(G.dsv);
    SafeRelease(G.depthTex);
    HRESULT hr = G.sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) BLog("bench: ResizeBuffers %ux%u failed 0x%08lx", w, h, static_cast<unsigned long>(hr));
    if (!CreateTargets()) BLog("bench: render targets after resize failed");
}
bool InitGfx() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    RECT rc;
    GetClientRect(G.hwnd, &rc);
    sd.BufferDesc.Width = rc.right > 0 ? rc.right : 1280;
    sd.BufferDesc.Height = rc.bottom > 0 ? rc.bottom : 720;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = G.hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &G.sc,
                                               &G.dev, nullptr, &G.ctx);
    if (FAILED(hr)) {
        sd.BufferCount = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &G.sc, &G.dev,
                                           nullptr, &G.ctx);
    }
    if (FAILED(hr)) {
        BLog("bench: D3D11CreateDeviceAndSwapChain failed 0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }
    IDXGIFactory* fac = nullptr;
    if (SUCCEEDED(G.sc->GetParent(IID_IDXGIFactory, reinterpret_cast<void**>(&fac))) && fac) {
        fac->MakeWindowAssociation(G.hwnd, DXGI_MWA_NO_ALT_ENTER);
        fac->Release();
    }
    if (!CreateTargets()) return false;
    ID3DBlob *vsW = Compile("VSWorld", "vs_5_0"), *psW = Compile("PSWorld", "ps_5_0"), *vsS = Compile("VSSky", "vs_5_0"),
             *psS = Compile("PSSky", "ps_5_0"), *vsH = Compile("VSHud", "vs_5_0"), *psH = Compile("PSHud", "ps_5_0");
    bool ok = vsW && psW && vsS && psS && vsH && psH;
    if (ok) {
        G.dev->CreateVertexShader(vsW->GetBufferPointer(), vsW->GetBufferSize(), nullptr, &G.vsWorld);
        G.dev->CreatePixelShader(psW->GetBufferPointer(), psW->GetBufferSize(), nullptr, &G.psWorld);
        G.dev->CreateVertexShader(vsS->GetBufferPointer(), vsS->GetBufferSize(), nullptr, &G.vsSky);
        G.dev->CreatePixelShader(psS->GetBufferPointer(), psS->GetBufferSize(), nullptr, &G.psSky);
        G.dev->CreateVertexShader(vsH->GetBufferPointer(), vsH->GetBufferSize(), nullptr, &G.vsHud);
        G.dev->CreatePixelShader(psH->GetBufferPointer(), psH->GetBufferSize(), nullptr, &G.psHud);
        const D3D11_INPUT_ELEMENT_DESC ilw[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        const D3D11_INPUT_ELEMENT_DESC ilh[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        G.dev->CreateInputLayout(ilw, 4, vsW->GetBufferPointer(), vsW->GetBufferSize(), &G.ilWorld);
        G.dev->CreateInputLayout(ilh, 3, vsH->GetBufferPointer(), vsH->GetBufferSize(), &G.ilHud);
    }
    for (ID3DBlob* b : {vsW, psW, vsS, psS, vsH, psH}) {
        if (b) b->Release();
    }
    if (!ok || !G.ilWorld || !G.ilHud) return false;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(FrameCB);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    G.dev->CreateBuffer(&bd, nullptr, &G.cb);
    static WV verts[6 + kMaxBoxes * 36];
    G.staticCount = static_cast<UINT>(BuildStatic(verts));
    bd = {};
    bd.ByteWidth = sizeof(WV) * G.staticCount;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init = {verts, 0, 0};
    G.dev->CreateBuffer(&bd, &init, &G.vbStatic);
    bd.ByteWidth = sizeof(WV) * 6 * kMaxHoles;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    G.dev->CreateBuffer(&bd, nullptr, &G.vbHoles);
    bd.ByteWidth = sizeof(HV) * kHudVerts;
    G.dev->CreateBuffer(&bd, nullptr, &G.vbHud);
    D3D11_BLEND_DESC bl = {};
    bl.RenderTarget[0].BlendEnable = TRUE;
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    G.dev->CreateBlendState(&bl, &G.blend);
    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    G.dev->CreateDepthStencilState(&ds, &G.dsWorld);
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    G.dev->CreateDepthStencilState(&ds, &G.dsDecal);
    ds.DepthEnable = FALSE;
    G.dev->CreateDepthStencilState(&ds, &G.dsOff);
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    G.dev->CreateRasterizerState(&rd, &G.rs);
    D3D11_SAMPLER_DESC smp = {};
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxLOD = D3D11_FLOAT32_MAX;
    G.dev->CreateSamplerState(&smp, &G.samp);
    D3D11_TEXTURE2D_DESC tt = {};
    tt.Width = g_textW;
    tt.Height = g_textH;
    tt.MipLevels = 1;
    tt.ArraySize = 1;
    tt.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tt.SampleDesc.Count = 1;
    tt.Usage = D3D11_USAGE_DYNAMIC;
    tt.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tt.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(G.dev->CreateTexture2D(&tt, nullptr, &G.textTex))) G.dev->CreateShaderResourceView(G.textTex, nullptr, &G.textSrv);
    return G.cb && G.vbStatic && G.vbHoles && G.vbHud && G.blend && G.dsWorld && G.dsDecal && G.dsOff && G.rs && G.samp && G.textSrv;
}
void MatMul(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = s;
        }
}
void Norm(float v[3]) {
    float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-9f)
        for (int i = 0; i < 3; ++i) v[i] /= l;
}
void Cross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
void DrawScene() {
    ID3D11DeviceContext* c = G.ctx;
    float f[3] = {g_vs.kv.fwd[0], g_vs.kv.fwd[1], g_vs.kv.fwd[2]}, upHint[3] = {0.f, 1.f, 0.f}, r[3], u[3];
    if (g_vs.kv.roll != 0.f) memcpy(upHint, g_vs.kv.up, sizeof upHint);
    Norm(f);
    Cross(upHint, f, r);
    Norm(r);
    Cross(f, r, u);
    const float* e = g_vs.eye;
    float view[16] = {r[0], u[0], f[0], 0, r[1], u[1], f[1], 0, r[2], u[2], f[2], 0,
                      -(r[0] * e[0] + r[1] * e[1] + r[2] * e[2]), -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]),
                      -(f[0] * e[0] + f[1] * e[1] + f[2] * e[2]), 1};
    float aspect = G.h ? static_cast<float>(G.w) / G.h : 1.f, tanH = std::tan(g_vs.fovY * 0.5f), zn = cfg.nearClip, zf = 1500.f;
    float proj[16] = {1.f / (tanH * aspect), 0, 0, 0, 0, 1.f / tanH, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    FrameCB cb = {};
    MatMul(view, proj, cb.vp);
    memcpy(cb.eye, e, sizeof cb.eye);
    cb.fogDist = 350.f;
    memcpy(cb.camR, r, sizeof cb.camR);
    memcpy(cb.camU, u, sizeof cb.camU);
    memcpy(cb.camF, f, sizeof cb.camF);
    cb.tanH = tanH;
    cb.aspect = aspect;
    cb.screen[0] = static_cast<float>(G.w);
    cb.screen[1] = static_cast<float>(G.h);
    c->UpdateSubresource(G.cb, 0, nullptr, &cb, 0, 0);
    c->OMSetRenderTargets(1, &G.rtv, G.dsv);
    c->ClearDepthStencilView(G.dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    D3D11_VIEWPORT vp = {0, 0, static_cast<float>(G.w), static_cast<float>(G.h), 0, 1};
    c->RSSetViewports(1, &vp);
    c->RSSetState(G.rs);
    const float bf[4] = {0, 0, 0, 0};
    c->OMSetBlendState(G.blend, bf, 0xffffffff);
    c->VSSetConstantBuffers(0, 1, &G.cb);
    c->PSSetConstantBuffers(0, 1, &G.cb);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->IASetInputLayout(nullptr);
    c->OMSetDepthStencilState(G.dsOff, 0);
    c->VSSetShader(G.vsSky, nullptr, 0);
    c->PSSetShader(G.psSky, nullptr, 0);
    c->Draw(3, 0);
    UINT stride = sizeof(WV), off = 0;
    c->IASetInputLayout(G.ilWorld);
    c->IASetVertexBuffers(0, 1, &G.vbStatic, &stride, &off);
    c->OMSetDepthStencilState(G.dsWorld, 0);
    c->VSSetShader(G.vsWorld, nullptr, 0);
    c->PSSetShader(G.psWorld, nullptr, 0);
    c->Draw(G.staticCount, 0);
    D3D11_MAPPED_SUBRESOURCE m;
    if (g_holeN > 0 && SUCCEEDED(c->Map(G.vbHoles, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        WV* v = static_cast<WV*>(m.pData);
        int n = 0;
        for (int k = 0; k < g_holeN; ++k) {
            const Hole& h = g_holes[k];
            float t[3], b[3];
            const float ax[3] = {1, 0, 0}, ay[3] = {0, 1, 0};
            Cross(h.n, std::fabs(h.n[1]) > 0.9f ? ax : ay, t);
            Norm(t);
            Cross(h.n, t, b);
            float dx = h.p[0] - e[0], dy = h.p[1] - e[1], dz = h.p[2] - e[2];
            float lift = 0.002f + 0.0008f * std::sqrt(dx * dx + dy * dy + dz * dz);
            float q[4][3];
            const float sgn[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            float uv[4][2];
            for (int i = 0; i < 4; ++i) {
                for (int a = 0; a < 3; ++a) q[i][a] = h.p[a] + h.n[a] * lift + (t[a] * sgn[i][0] + b[a] * sgn[i][1]) * h.r;
                uv[i][0] = sgn[i][0];
                uv[i][1] = sgn[i][1];
            }
            bool last = h.burst == g_burst.id;
            const float colLast[4] = {1.f, 0.55f, 0.10f, 1.f}, colOld[4] = {0.92f, 0.92f, 0.82f, 0.9f};
            Quad(v, n, q[0], q[1], q[2], q[3], last ? colLast : colOld, uv, 3);
        }
        c->Unmap(G.vbHoles, 0);
        c->IASetVertexBuffers(0, 1, &G.vbHoles, &stride, &off);
        c->OMSetDepthStencilState(G.dsDecal, 0);
        c->Draw(n, 0);
    }
}
struct TextBmp {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HFONT font = nullptr;
    void* bits = nullptr;
    wchar_t lines[kLines][320] = {};
    COLORREF colors[kLines] = {};
    int width[kLines] = {};
    double tLast = -1;
} g_text;
bool InitText() {
    g_text.dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_textW;
    bi.bmiHeader.biHeight = -static_cast<LONG>(g_textH);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_text.bmp = CreateDIBSection(g_text.dc, &bi, DIB_RGB_COLORS, &g_text.bits, nullptr, 0);
    if (!g_text.dc || !g_text.bmp || !g_text.bits) return false;
    SelectObject(g_text.dc, g_text.bmp);
    g_text.font = CreateFontW(-Px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    if (g_text.font) SelectObject(g_text.dc, g_text.font);
    SetBkMode(g_text.dc, TRANSPARENT);
    return true;
}
void UploadText() {
    memset(g_text.bits, 0, static_cast<size_t>(g_textW) * g_textH * 4);
    for (int i = 0; i < kLines; ++i) {
        int n = static_cast<int>(wcslen(g_text.lines[i]));
        SetTextColor(g_text.dc, g_text.colors[i]);
        TextOutW(g_text.dc, Px(6), Px(3) + i * g_lineH, g_text.lines[i], n);
        SIZE sz = {};
        GetTextExtentPoint32W(g_text.dc, g_text.lines[i], n, &sz);
        g_text.width[i] = Px(6) + sz.cx;
    }
    GdiFlush();
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(G.ctx->Map(G.textTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        for (UINT y = 0; y < g_textH; ++y)
            memcpy(static_cast<unsigned char*>(m.pData) + static_cast<size_t>(y) * m.RowPitch,
                   static_cast<unsigned char*>(g_text.bits) + static_cast<size_t>(y) * g_textW * 4, g_textW * 4);
        G.ctx->Unmap(G.textTex, 0);
    }
}
HV g_hv[kHudVerts];
int g_hvTri = 0, g_hvLine = 0;
void HudRect(float x0, float y0, float x1, float y1, const float col[4], float u1 = -1.f, float v1 = -1.f) {
    if (g_hvTri + 6 > kHudVerts / 2) return;
    bool tex = u1 >= 0.f;
    const float px[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, uv[4][2] = {{0, 0}, {u1, 0}, {u1, v1}, {0, v1}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k : idx) {
        HV& v = g_hv[g_hvTri++];
        v.p[0] = px[k][0];
        v.p[1] = px[k][1];
        v.uv[0] = tex ? uv[k][0] : -1.f;
        v.uv[1] = tex ? uv[k][1] : -1.f;
        memcpy(v.c, col, sizeof v.c);
    }
}
void HudLine(float x0, float y0, float x1, float y1, const float col[4]) {
    if (kHudVerts / 2 + g_hvLine + 2 > kHudVerts) return;
    for (int k = 0; k < 2; ++k) {
        HV& v = g_hv[kHudVerts / 2 + g_hvLine++];
        v.p[0] = (k ? x1 : x0) + 0.5f;
        v.p[1] = (k ? y1 : y0) + 0.5f;
        v.uv[0] = v.uv[1] = -1.f;
        memcpy(v.c, col, sizeof v.c);
    }
}
void Graph(float x, float y, float w, float h, double now, float lo, float hi, float grid, const int* series, const float (*cols)[4],
           int nSeries) {
    const float frame[4] = {0.8f, 0.8f, 0.8f, 0.5f}, gridc[4] = {0.8f, 0.8f, 0.8f, 0.18f}, zero[4] = {1.f, 1.f, 1.f, 0.45f};
    HudLine(x, y, x + w, y, frame);
    HudLine(x, y + h, x + w, y + h, frame);
    HudLine(x, y, x, y + h, frame);
    HudLine(x + w, y, x + w, y + h, frame);
    auto Y = [&](float v) {
        float k = (v - lo) / (hi - lo);
        k = k < 0 ? 0 : k > 1 ? 1 : k;
        return y + h - k * h;
    };
    for (float g = std::ceil(lo / grid) * grid; g <= hi + 1e-4f; g += grid) HudLine(x, Y(g), x + w, Y(g), std::fabs(g) < 1e-4f ? zero : gridc);
    const double span = 3.0;
    for (int s = 0; s < nSeries; ++s) {
        float px = 0, py = 0;
        bool have = false;
        for (int k = g_histN - 1; k >= 0; --k) {
            const Sample& sm = g_hist[(g_histHead - 1 - k + kHist * 2) % kHist];
            if (sm.t < now - span) continue;
            const float vals[6] = {sm.net, sm.kickP, sm.punch, sm.back, sm.gunPitch, sm.roll};
            float cx = x + static_cast<float>((sm.t - (now - span)) / span) * w, cy = Y(vals[series[s]]);
            if (have && cx - px < 1.f && k > 0) continue;
            if (have) HudLine(px, py, cx, cy, cols[s]);
            px = cx;
            py = cy;
            have = true;
        }
    }
}
const wchar_t* ModelName(int i) {
    const wchar_t* id = vmgame::g_w[i].set.weaponId;
    if (id[0]) return id;
    const wchar_t* s = wcsrchr(vmgame::g_models[i], L'\\');
    return s ? s + 1 : vmgame::g_models[i];
}
bool g_captured = false, g_hudOn = true, g_mute = false, g_scriptMode = false;
bool g_scriptSealed = false;
float g_cfS[32], g_cfA[32];
int g_cfN = 0;
wchar_t g_reloadNote[160] = L"（还没有）";
int g_fps = 0;
double g_workMs = 0;
const wchar_t* FmtMs(double v, int slot, bool settle = false) {
    static wchar_t buf[12][32];
    wchar_t* b = buf[slot % 12];
    if (v <= -1.5) swprintf(b, 32, L"—");
    else if (v < 0) swprintf(b, 32, settle ? L"未回稳" : L"—");
    else swprintf(b, 32, L"%.1f", v);
    return b;
}
void BuildText(double now) {
    using namespace vmgame;
    TextBmp& T = g_text;
    for (COLORREF& c : T.colors) c = RGB(255, 255, 255);
    LONG rn = g_rosterN;
    int rosterPos = 0;
    for (LONG k = 0; k < rn; ++k)
        if (g_roster[k] == g_modelIdx) rosterPos = static_cast<int>(k) + 1;
    swprintf(T.lines[0], 320, L"试枪台  %ls（模型 %d/%d%ls）  %ls%ls", ModelName(g_modelIdx), g_modelIdx + 1, g_modelCount,
             rosterPos ? (rn > 1 ? L"，数字键 / 滚轮切换" : L"") : L"，不在拥有清单里：F10 换来的", g_captured ? L"鼠标已锁定（Esc 放开）"
                                                                                                          : L"【点窗口锁定鼠标】",
             g_failed ? L"   枪模层失败，看控制台 / vm_bench.log" : L"");
    T.colors[0] = g_captured ? RGB(255, 255, 255) : RGB(255, 230, 90);
    wchar_t mag[96];
    if (g_mag.size > 0) {
        const wchar_t* st = g_switching ? L"  切枪中" : g_mag.reload == 1 ? L"  换弹中" : g_mag.reload == 2 ? L"  空仓换弹中"
                          : g_mag.disposable && g_spent[g_modelIdx] ? L"  这场已用光（N 补满）" : L"";
        swprintf(mag, 96, L"弹匣 %ld/%ld%ls", g_mag.ammo, g_mag.size, st);
    } else {
        swprintf(mag, 96, L"不用弹匣%ls", g_switching ? L"  切枪中" : L"");
    }
    double gap = FireInterval() > 0 ? FireInterval() : cfg.fireInterval;
    wchar_t stance[64];
    if (P.tac) swprintf(stance, 64, L"战术奔跑");
    else if (P.sprint) swprintf(stance, 64, L"冲刺");
    else if (now < P.fireBlock) swprintf(stance, 64, L"抬枪中");
    else if (P.adsT > 0.f) swprintf(stance, 64, L"举镜 %.0f%%", P.adsT * 100.f);
    else swprintf(stance, 64, L"腰射");
    const wchar_t* spin = !g_spin.mode ? L"" : g_spin.state == 1 ? L"  转管 预转" : g_spin.state == 2 ? L"  转管 转动" : g_spin.state == 3 ? L"  转管 停转" : L"  转管 停";
    wchar_t fpsName[48];
    FpsLabel(g_fpsMode, fpsName, 48);
    swprintf(T.lines[1], 320, L"%ls  %.0f 发/分  %ls%ls  移速 %.1f 米/秒  |  帧率 L：%ls  实测 %d 帧/秒  一帧 %.1f 毫秒%ls", mag, 60.0 / gap, stance, spin,
             P.camSpeed, fpsName, g_fps, g_workMs, g_mute ? L"  静音" : L"");
    const Burst& b = g_burst;
    swprintf(T.lines[2], 320, L"本串 %d 发  踢动上抬 %.2f°（峰值 %.2f°）  净上抬 %.2f°（含压枪）  左右漂 %+.2f°  瞬时上跳 %.2f°  |  上一串 %d 发 上抬 %.2f°",
             b.shots, b.kickPitch / kick::kDeg, b.climbPeak, (P.pitch - b.refPitch) / kick::kDeg, b.kickYaw / kick::kDeg, P.camKick.x[0],
             g_lastBurst.shots, g_lastBurst.climbPeak);
    T.colors[2] = RGB(255, 235, 120);
    kick::Params kp;
    GetKick(&kp);
    {
        const float *gp = g_kickOut.gunPos, *gr = g_kickOut.gunRot, *sp = g_kickOut.susPos, *sr = g_kickOut.susRot;
        wchar_t ik[120];
        const vm::Renderer& rd = Wp().rend;
        float rr = rd.IkResidualMm(0), rl = rd.IkResidualMm(1);
        if (!g_layer[kIk]) swprintf(ik, 120, L"双手跟枪 关（试枪台 F7）");
        else if (!Wp().set.handIk) swprintf(ik, 120, L"双手跟枪 关（viewmodel.ini hand_ik=0）");
        else if (!rd.ArmRig()) swprintf(ik, 120, L"双手跟枪 —（这个模型没有 ref 手臂骨架，整体推）");
        else if (rr < 0 && rl < 0) swprintf(ik, 120, L"双手跟枪 这一帧没做（没有后坐）");
        else
            swprintf(ik, 120, L"双手跟枪 右腕 %ls 左腕 %ls 毫米 残差 %ls / %ls", FmtMs(rr < 0 ? -1.0 : rd.IkShiftMm(0), 0),
                     FmtMs(rl < 0 ? -1.0 : rd.IkShiftMm(1), 1), FmtMs(rr, 2), FmtMs(rl, 3));
        const float* jx = g_jitter.x;
        swprintf(T.lines[3], 320, L"枪身 后推 %.2f 厘米（弹簧 %.2f + 持续 %.2f）上扬 %+.2f°（弹簧 %+.2f 持续 %+.2f 抖动 %+.2f）滚转 %+.2f°（抖动 %+.2f）| 弹簧本串峰值 后推 %.2f 上扬 %.2f | %ls",
                 (gp[2] + sp[2]) * 100.f, gp[2] * 100.f, sp[2] * 100.f, gr[0] + sr[0] + jx[0], gr[0], sr[0], jx[0], gr[2] + sr[2] + jx[2], jx[2],
                 b.backMax, b.gunPitchMax, ik);
    }
    T.colors[3] = RGB(255, 180, 90);
    swprintf(T.lines[4], 320, L"[kick] %ls  腰射 上抬 %.2f–%.2f° 上跳 %.2f° 后推 %.1f 厘米 上扬 %.2f° | 举镜 上抬 %.2f–%.2f° 上跳 %.2f° 后推 %.1f 厘米 上扬 %.2f° | 镜头×%.2f 枪身×%.2f 持续×%.2f",
             kp.enabled ? L"开" : L"关", kp.hip.climbPitch[0], kp.hip.climbPitch[1], kp.hip.punch[0], kp.hip.gunMove[0] * 100.f, kp.hip.gunRot[0],
             kp.ads.climbPitch[0], kp.ads.climbPitch[1], kp.ads.punch[0], kp.ads.gunMove[0] * 100.f, kp.ads.gunRot[0], kp.camScale, kp.gunScale,
             kp.sustainScale);
    T.colors[4] = RGB(170, 255, 170);
    {
        int n = swprintf(T.lines[5], 320, L"逐层");
        for (int k = 0; k < kLayers; ++k) {
            const wchar_t* st = g_layer[k] ? L" 开" : L"【关】";
            if ((k == kSus || k == kJit) && g_layer[k] && !g_layer[kGun]) st = L"（随枪身关）";
            if (k == kBlend && g_layer[k] && !kp.blendMode) st = L"（ini blend_mode=0）";
            wchar_t key[8];
            LayerKeyLabel(k, key, 8);
            n += swprintf(T.lines[5] + n, 320 - n, L"  %ls %ls%ls", key, kLayerName[k], st);
        }
        n += swprintf(T.lines[5] + n, 320 - n, L"  | G 枪口气体 %ls", GasStyleNow() == 1 ? L"新（曲线式 + 侧喷）" : L"【旧版】");
        n += swprintf(T.lines[5] + n, 320 - n, L"  | K 开头冲击%ls（右 %+.1f 毫米 滚转 %+.2f°）",
                      !g_onsetOn ? L"【关】" : !g_layer[kGun] ? L"（随枪身关）" : kp.onsetScale <= 0.f ? L"（ini 关）" : L" 开",
                      g_kickOut.onPos[0] * 1000.f, g_kickOut.onRot[2]);
        n += swprintf(T.lines[5] + n, 320 - n, L"  | Y 镜头细抖%ls（上下 %+.3f° 滚转 %+.3f°）",
                      !g_shakeOn ? L"【关】" : !g_layer[kCam] ? L"（随镜头关）" : kp.shakeScale <= 0.f ? L"（ini 关）" : L" 开", P.camKick.shOut[0],
                      P.camKick.shOut[2]);
        if (!g_layer[kCam]) swprintf(T.lines[5] + n, 320 - n, L"   ← 镜头踢动关：弹着点会变");
        T.colors[5] = !g_layer[kCam] ? RGB(255, 140, 90) : AnyLayerOff() ? RGB(255, 215, 90) : RGB(220, 220, 220);
    }
    {
        const RecSum& S = g_lastSum;
        wchar_t fpsS[48], tags[kLayers + 1];
        T.lines[8][0] = 0;
        if (g_rp.armed || g_rp.active) {
            LayerTags(g_layer, tags);
            FpsLabel(g_fpsMode, fpsS, 48);
            double t = g_rp.active ? now - g_rp.t0 : 0.0;
            swprintf(T.lines[6], 320, L"重播中  %ls  %ls  %ls  %ls  %.2f / %.2f 秒（输入已接管；逐层 / 帧率 / 换枪等按键等它结束）", g_rp.label,
                     g_rp.mode == kRpSwitch ? L"腰射→举镜→腰射" : g_rp.ads ? L"举镜" : L"腰射", tags, fpsS, t, g_rp.pre + g_rp.hold + g_rp.tail);
            swprintf(T.lines[7], 320, L"（结果在重播结束后显示在这里，同时写 vm_bench_shots.csv）");
        } else if (S.valid) {
            FpsLabel(S.fpsMode, fpsS, 48);
            swprintf(T.lines[6], 320, L"最近一串 #%d %ls %ls %ls %ls：%d 发 用弹 %ld | 按键→首发 %ls  到期→开火 平均 %ls 最大 %ls | 开火→枪身 %ls 镜头 %ls 枪声（实测）%ls 毫秒",
                     S.id, S.label, S.ads ? L"举镜" : L"腰射", S.layerTags, S.replay ? fpsS : L"实时", S.shots, S.ammoUsed, FmtMs(S.pressToFirst, 0),
                     FmtMs(S.dueAvg, 1), FmtMs(S.dueMax, 2), FmtMs(S.gunFirst, 3), FmtMs(S.camFirst, 4), FmtMs(S.sndRealAvg, 5));
            swprintf(T.lines[7], 320, L"峰值 上抬 %.2f° 上跳 %.2f° 后推 %.2f 厘米 上扬 %.2f°（逐帧 %.2f / %.2f / %.2f）| 回稳 上跳 %ls 后推 %ls 上扬 %ls 上抬回正 %ls 毫秒",
                     S.climbPeak, S.punchPeakT, S.backPeakT, S.pitchPeakT, S.punchPeak, S.backPeak, S.pitchPeak, FmtMs(S.punchSettleT, 6, true),
                     FmtMs(S.backSettleT, 7, true), FmtMs(S.pitchSettleT, 8, true), FmtMs(S.climbSettle, 9, true));
            wchar_t ikS[96];
            if (S.ikFrames)
                swprintf(ikS, 96, L"右腕最大 %.1f 左腕 %.1f 毫米 残差最大 %.2f 毫米（%d 帧）", S.ikShiftPeak[0], S.ikShiftPeak[1], S.ikResMax, S.ikFrames);
            else
                swprintf(ikS, 96, L"没做（关着 / 没有后坐 / 没有手臂骨架）");
            swprintf(T.lines[8], 320, L"其中连射持续曲线 后推 %.2f 厘米 上扬 %.2f° | 双手跟枪 %ls | 一帧 %.1f / %.1f 毫秒%ls%ls", S.susBackPeak, S.susPitchPeak, ikS,
                     S.frameMsAvg, S.frameMsMax, S.same[0] || S.truncated || S.stalled ? L" | " : L"",
                     S.truncated ? L"按住太久，记录满了：只算了前面一段" : S.stalled ? L"中间卡过（一帧隔了 0.1 秒以上），按时间那套只是近似" : S.same);
        } else {
            swprintf(T.lines[6], 320, L"最近一串：还没有（左键打一串，或按 Space 重播：复位种子和瞄准、对着 25 米靶自动按住扳机 1 秒）");
            T.lines[7][0] = 0;
        }
        T.colors[6] = T.colors[7] = T.colors[8] = RGB(150, 235, 255);
    }
    {
        wchar_t bl[120];
        if (g_kickOut.mode)
            swprintf(bl, 120, L"混合 新：举镜进度 %.0f%% → 权重 %.2f（S 曲线，两套逐帧混）", g_mo.blendT * 100.f, g_kickOut.w);
        else
            swprintf(bl, 120, L"混合 旧：举镜程度 %.2f（开火时线性混参数）", g_kickOut.w);
        swprintf(T.lines[9], 320, L"弹孔 %d/%d（C 清空）  热重载：%ls  |  %ls", g_holeN, kMaxHoles, g_reloadNote, bl);
    }
    T.colors[9] = RGB(200, 220, 255);
    swprintf(T.lines[10], 320, L"折线（近 3 秒）左：绿 净上抬  黄 踢动上抬  青 瞬时上跳（格 1°）    右（枪身 = 弹簧 + 持续曲线）：橙 后推（格 0.5 厘米）  品红 上扬  蓝 滚转（格 0.5°）");
    T.colors[10] = RGB(210, 210, 210);
    swprintf(T.lines[11], 320, L"左键开火  右键举镜  WASD 走（按住 W 点 Shift 冲刺，再点战术奔跑）  R 换弹  1–9 / 滚轮 选枪  F8 重读  F9 轮播动作  F10 换模型");
    swprintf(T.lines[12], 320, L"F1–F7 / J / B 逐层（Shift = 只开这一层，0 = 全开）  G 气体新/旧  Space 重播（Shift+Space 举镜，Ctrl+Space 切换）  L 帧率  C 清弹孔  N 补满  F12 截图  H 藏字  Esc 放开鼠标");
    T.colors[11] = T.colors[12] = RGB(190, 190, 190);
}
void DrawHud(double now) {
    if (!g_hudOn) return;
    if (now - g_text.tLast >= 0.1) {
        g_text.tLast = now;
        BuildText(now);
        UploadText();
    }
    g_hvTri = g_hvLine = 0;
    const float panel[4] = {0.f, 0.f, 0.f, 0.45f}, white[4] = {1.f, 1.f, 1.f, 1.f};
    int nLines = g_captured ? kLines - 2 : kLines, textW = 0;
    for (int i = 0; i < nLines; ++i) textW = g_text.width[i] > textW ? g_text.width[i] : textW;
    textW += Px(6);
    float x = 10.f * g_ui, y = 10.f * g_ui, gw = 300.f * g_ui, gh = 80.f * g_ui, gap = 16.f * g_ui;
    float textH = static_cast<float>(nLines * g_lineH + Px(6)), gy = y + textH + 4.f * g_ui;
    float panelW = textW > 2 * gw + gap + 16.f * g_ui ? textW : 2 * gw + gap + 16.f * g_ui;
    if (panelW > g_textW) panelW = static_cast<float>(g_textW);
    HudRect(x, y, x + panelW, gy + gh + 8.f * g_ui, panel);
    HudRect(x, y, x + panelW, y + textH, white, panelW / g_textW, textH / g_textH);
    const int s1[3] = {0, 1, 2}, s2[3] = {3, 4, 5};
    const float c1[3][4] = {{0.35f, 1.f, 0.35f, 1.f}, {1.f, 0.9f, 0.3f, 1.f}, {0.3f, 0.9f, 1.f, 1.f}};
    const float c2[3][4] = {{1.f, 0.6f, 0.2f, 1.f}, {1.f, 0.35f, 0.95f, 1.f}, {0.45f, 0.6f, 1.f, 1.f}};
    Graph(x + 8.f * g_ui, gy, gw, gh, now, -1.f, 7.f, 1.f, s1, c1, 3);
    Graph(x + 8.f * g_ui + gw + gap, gy, gw, gh, now, -1.5f, 2.5f, 0.5f, s2, c2, 3);
    ID3D11DeviceContext* c = G.ctx;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(c->Map(G.vbHud, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, g_hv, sizeof(HV) * g_hvTri);
    memcpy(static_cast<HV*>(m.pData) + kHudVerts / 2, g_hv + kHudVerts / 2, sizeof(HV) * g_hvLine);
    c->Unmap(G.vbHud, 0);
    FrameCB cb = {};
    cb.screen[0] = static_cast<float>(G.w);
    cb.screen[1] = static_cast<float>(G.h);
    c->UpdateSubresource(G.cb, 0, nullptr, &cb, 0, 0);
    c->OMSetRenderTargets(1, &G.rtv, nullptr);
    D3D11_VIEWPORT vp = {0, 0, static_cast<float>(G.w), static_cast<float>(G.h), 0, 1};
    c->RSSetViewports(1, &vp);
    c->RSSetState(G.rs);
    const float bf[4] = {0, 0, 0, 0};
    c->OMSetBlendState(G.blend, bf, 0xffffffff);
    c->OMSetDepthStencilState(G.dsOff, 0);
    UINT stride = sizeof(HV), off = 0;
    c->IASetInputLayout(G.ilHud);
    c->IASetVertexBuffers(0, 1, &G.vbHud, &stride, &off);
    c->VSSetShader(G.vsHud, nullptr, 0);
    c->PSSetShader(G.psHud, nullptr, 0);
    c->VSSetConstantBuffers(0, 1, &G.cb);
    c->PSSetShaderResources(0, 1, &G.textSrv);
    c->PSSetSamplers(0, 1, &G.samp);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->Draw(g_hvTri, 0);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    c->Draw(g_hvLine, kHudVerts / 2);
    ID3D11ShaderResourceView* none = nullptr;
    c->PSSetShaderResources(0, 1, &none);
}
bool SaveBmp(const wchar_t* path) {
    ID3D11Texture2D* bb = nullptr;
    HRESULT hr = G.sc->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&bb));
    if (FAILED(hr)) {
        BLog("bench: screenshot GetBuffer failed 0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }
    D3D11_TEXTURE2D_DESC td;
    bb->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    hr = G.dev->CreateTexture2D(&td, nullptr, &st);
    bool ok = SUCCEEDED(hr);
    if (!ok) BLog("bench: screenshot staging texture failed 0x%08lx", static_cast<unsigned long>(hr));
    if (ok) {
        G.ctx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        hr = G.ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
        ok = SUCCEEDED(hr);
        if (!ok) BLog("bench: screenshot Map failed 0x%08lx", static_cast<unsigned long>(hr));
        if (ok) {
            FILE* f = _wfopen(path, L"wb");
            ok = f != nullptr;
            if (!ok) BLog("bench: screenshot cannot write %ls", path);
            if (f) {
                unsigned w = td.Width, h = td.Height, row = w * 3, pad = (4 - row % 4) % 4, size = 54 + (row + pad) * h;
                unsigned char hd[54] = {'B', 'M'};
                memcpy(hd + 2, &size, 4);
                unsigned offBits = 54, dib = 40, planes = 1 | (24 << 16);
                memcpy(hd + 10, &offBits, 4);
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
            G.ctx->Unmap(st, 0);
        }
        st->Release();
    }
    bb->Release();
    return ok;
}
struct Input {
    long dx = 0, dy = 0;
    bool fg = false, lmb = false, rmb = false, shift = false, w = false, a = false, s = false, d = false;
};
volatile LONG g_rawDx = 0, g_rawDy = 0;
bool g_ignoreLmb = false, g_resize = false, g_minimized = false, g_shotReq = false;
wchar_t g_exeDir[MAX_PATH * 2];
void Capture(bool on) {
    if (on == g_captured) return;
    g_captured = on;
    if (on) {
        RECT rc;
        GetClientRect(G.hwnd, &rc);
        POINT a = {rc.left, rc.top}, b = {rc.right, rc.bottom};
        ClientToScreen(G.hwnd, &a);
        ClientToScreen(G.hwnd, &b);
        RECT clip = {a.x, a.y, b.x, b.y};
        ClipCursor(&clip);
        SetCursorPos((a.x + b.x) / 2, (a.y + b.y) / 2);
        InterlockedExchange(&g_rawDx, 0);
        InterlockedExchange(&g_rawDy, 0);
        SetCursor(nullptr);
    } else {
        ClipCursor(nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    }
}
void ClearHoles() {
    g_holeN = g_holeHead = 0;
    BLog("bench: bullet holes cleared");
}
void NewBattle() {
    using namespace vmgame;
    for (int i = 0; i < g_modelCount && i < kMaxModels; ++i) {
        if (!g_w[i].set.magDisposable) continue;
        InterlockedExchange(&g_spent[i], 0);
        if (i < 8) g_mag.ammoByModel[i] = -1;
        if (i == g_mag.loadedModel) InterlockedExchange(&g_mag.ammo, g_mag.size);
        BLog("bench: new battle -> %ls refilled", g_w[i].set.weaponId);
    }
}
void ReloadAll(const char* why);
void ToggleLayer(int k);
void SoloLayer(int k);
void AllLayers();
bool RequestReplay(int mode, const wchar_t* label);
void CycleFps();
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INPUT: {
        RAWINPUT ri;
        UINT sz = sizeof ri;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
            ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && g_captured) {
            InterlockedExchangeAdd(&g_rawDx, ri.data.mouse.lLastX);
            InterlockedExchangeAdd(&g_rawDy, ri.data.mouse.lLastY);
        }
        break;
    }
    case WM_LBUTTONDOWN:
        if (!g_captured && !g_scriptMode) {
            Capture(true);
            g_ignoreLmb = true;
        }
        return 0;
    case WM_SETCURSOR:
        if (g_captured && LOWORD(l) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (g_scriptSealed) return 0;
        bool first = !(l & (1L << 30));
        if (w == VK_ESCAPE) Capture(false);
        if (!first) return 0;
        bool shift = GetKeyState(VK_SHIFT) < 0;
        bool locked = (g_rp.armed || g_rp.active) &&
                      (w == vmgame::ReloadKey() || w == vmgame::AnimKey() || w == vmgame::ModelKey() || w == 'R' || w == 'N' || w == 'L' ||
                       w == VK_SPACE || (w >= '1' && w <= '9') || (w >= VK_F1 && w <= VK_F7) || w == 'J' || w == 'B' || w == '0' ||
                       w == VK_NUMPAD0 || w == vmgame::ShieldKey() || w == 'V' || w == 'K' || w == 'Y');
        if (locked) BLog("bench: replay running - key 0x%02X ignored", static_cast<unsigned>(w));
        else if (vmgame::ShieldKey() && w == vmgame::ShieldKey()) vmgame::ToggleShield();
        else if (w == 'V') vmgame::DebugShieldHit();
        else if (w == vmgame::ReloadKey()) ReloadAll("F8");
        else if (w == vmgame::AnimKey()) vmgame::CycleAnim();
        else if (w == vmgame::ModelKey()) vmgame::CycleModel();
        else if (w == 'R') vmgame::StartMagReload();
        else if (w >= '1' && w <= '9') vmgame::SelectWeapon(static_cast<int>(w - '0'));
        else if (w >= VK_F1 && w <= VK_F7) shift ? SoloLayer(static_cast<int>(w - VK_F1)) : ToggleLayer(static_cast<int>(w - VK_F1));
        else if (w == 'J') shift ? SoloLayer(kJit) : ToggleLayer(kJit);
        else if (w == 'B') shift ? SoloLayer(kBlend) : ToggleLayer(kBlend);
        else if (w == '0' || w == VK_NUMPAD0) AllLayers();
        else if (w == VK_SPACE && GetKeyState(VK_CONTROL) < 0) RequestReplay(kRpSwitch, L"切换重播");
        else if (w == VK_SPACE) RequestReplay(shift ? kRpAds : kRpHip, shift ? L"举镜重播" : L"重播");
        else if (w == 'L') CycleFps();
        else if (w == 'C') ClearHoles();
        else if (w == 'N') NewBattle();
        else if (w == 'H') g_hudOn = !g_hudOn;
        else if (w == 'K') {
            g_onsetOn = !g_onsetOn;
            BLog("bench: onset (burst start kick) %s", g_onsetOn ? "on" : "off");
        }
        else if (w == 'Y') {
            g_shakeOn = !g_shakeOn;
            BLog("bench: camera shake %s", g_shakeOn ? "on" : "off");
        }
        else if (w == 'G') {
            g_gasStyle = GasStyleNow() == 1 ? 0 : 1;
            BLog("bench: muzzle gas style %d (%s)", g_gasStyle, g_gasStyle ? "curve" : "old");
        }
        else if (w == VK_F12) g_shotReq = true;
        else if (m == WM_SYSKEYDOWN) break;
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_MOUSEWHEEL:
        if (!g_rp.armed && !g_rp.active && !g_scriptSealed) vmgame::CycleWeapon(GET_WHEEL_DELTA_WPARAM(w) < 0 ? 1 : -1);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) Capture(false);
        break;
    case WM_KILLFOCUS:
        Capture(false);
        break;
    case WM_SIZE:
        g_minimized = w == SIZE_MINIMIZED;
        if (!g_minimized) g_resize = true;
        if (g_captured) {
            Capture(false);
            Capture(true);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(h, m, w, l);
}
struct Watch {
    wchar_t path[MAX_PATH * 2] = {};
    FILETIME t = {};
    double changedAt = -1;
};
Watch g_wMain, g_wSide;
int g_wSideModel = -1;
double g_tWatch = 0;
FILETIME FileTime(const wchar_t* p) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    FILETIME z = {};
    return GetFileAttributesExW(p, GetFileExInfoStandard, &fa) ? fa.ftLastWriteTime : z;
}
void ReloadAll(const char* why) {
    vmgame::RequestReload();
    LoadCfg(vmgame::g_ini);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t w[64];
    MultiByteToWideChar(CP_UTF8, 0, why, -1, w, 64);
    swprintf(g_reloadNote, 160, L"%02d:%02d:%02d %ls", st.wHour, st.wMinute, st.wSecond, w);
    BLog("bench: reload (%s)", why);
}
void WatchStep(double now) {
    using namespace vmgame;
    if (now - g_tWatch < 0.05) return;
    g_tWatch = now;
    if (g_wSideModel != g_modelIdx) {
        g_wSideModel = g_modelIdx;
        SidecarIni(g_modelIdx, g_wSide.path);
        g_wSide.t = FileTime(g_wSide.path);
        g_wSide.changedAt = -1;
    }
    bool mainDue = false, sideDue = false;
    for (Watch* w : {&g_wMain, &g_wSide}) {
        FILETIME t = FileTime(w->path);
        if (CompareFileTime(&t, &w->t) != 0) {
            w->t = t;
            w->changedAt = now;
        } else if (w->changedAt >= 0 && now - w->changedAt >= 0.15) {
            w->changedAt = -1;
            (w == &g_wMain ? mainDue : sideDue) = true;
        }
    }
    if (mainDue) {
        for (int i = 0; i < g_modelCount; ++i) {
            if (i == g_modelIdx) continue;
            LoadAllSettings(i, g_w[i].set);
            if (g_w[i].rend.Ready()) g_w[i].fxDirty = true;
        }
        ReloadAll("hero_fps.ini changed");
    } else if (sideDue) {
        char why[160];
        snprintf(why, sizeof why, "%ls viewmodel.ini changed", ModelName(g_modelIdx));
        ReloadAll(why);
    }
}
namespace probe {
struct Item {
    vm::GunSound* gs;
    double after;
    LONG64 qpcFire;
    int rec, shot;
};
struct Result {
    int rec, shot;
    double ms;
};
CRITICAL_SECTION g_cs;
SRWLOCK g_voices = SRWLOCK_INIT;
HANDLE g_ev = nullptr, g_th = nullptr;
volatile LONG g_quit = 0, g_gen = 0;
Item g_q[32];
int g_qn = 0;
Result g_res[64];
int g_resN = 0;
double LatestStart(vm::GunSound* gs) {
    using namespace benchpeek;
    const double* st = gs->*Member(Started{});
    int n = gs->*Member(NVoices{});
    double m = 0;
    for (int i = 0; i < n; ++i) m = st[i] > m ? st[i] : m;
    return m;
}
DWORD WINAPI Thread(void*) {
    using namespace benchpeek;
    struct Work {
        Item it;
        int voice;
        UINT64 base, fresh;
        bool haveBase;
    } work[32];
    int nw = 0;
    LONG seen = 0;
    while (!g_quit) {
        AcquireSRWLockShared(&g_voices);
        if (g_gen != seen) {
            seen = g_gen;
            nw = 0;
        }
        EnterCriticalSection(&g_cs);
        while (g_qn && nw < 32) work[nw++] = {g_q[--g_qn], -1, 0, 0, false};
        LeaveCriticalSection(&g_cs);
        Result done[32];
        int nd = 0;
        for (int k = 0; k < nw;) {
            Work& w = work[k];
            vm::GunSound* gs = w.it.gs;
            IXAudio2SourceVoice* const* v = gs->*Member(Voices{});
            const volatile double* st = gs->*Member(Started{});
            int nv = gs->*Member(NVoices{});
            for (int i = 0; i < nv && w.voice < 0; ++i)
                if (v[i] && st[i] > w.it.after) w.voice = i;
            bool started = false;
            if (w.voice >= 0) {
                XAUDIO2_VOICE_STATE vs = {};
                v[w.voice]->GetState(&vs, 0);
                if (!w.haveBase) {
                    XAUDIO2_VOICE_DETAILS det = {};
                    v[w.voice]->GetVoiceDetails(&det);
                    w.haveBase = true;
                    w.base = vs.SamplesPlayed;
                    w.fresh = det.InputSampleRate / 50;
                    started = w.base > 0 && w.base <= w.fresh;
                } else {
                    started = vs.SamplesPlayed != w.base;
                }
            }
            double ms = RealMs(RealQpc() - w.it.qpcFire);
            if (started || ms > 500.0) {
                done[nd++] = {w.it.rec, w.it.shot, started ? ms : -1.0};
                work[k] = work[--nw];
            } else {
                ++k;
            }
        }
        ReleaseSRWLockShared(&g_voices);
        if (nd) {
            EnterCriticalSection(&g_cs);
            for (int d = 0; d < nd && g_resN < 64; ++d) g_res[g_resN++] = done[d];
            LeaveCriticalSection(&g_cs);
        }
        if (nw) SwitchToThread();
        else WaitForSingleObject(g_ev, 50);
    }
    return 0;
}
void Start() {
    InitializeCriticalSection(&g_cs);
    g_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_th = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr);
}
void Stop() {
    if (!g_th) return;
    InterlockedExchange(&g_quit, 1);
    SetEvent(g_ev);
    WaitForSingleObject(g_th, 2000);
    CloseHandle(g_th);
    g_th = nullptr;
}
void Arm(vm::GunSound* gs, LONG64 qpcFire, int rec, int shot) {
    if (!g_th) return;
    double after = LatestStart(gs);
    EnterCriticalSection(&g_cs);
    if (g_qn < 32) g_q[g_qn++] = {gs, after, qpcFire, rec, shot};
    LeaveCriticalSection(&g_cs);
    SetEvent(g_ev);
}
void CancelAll() {
    if (!g_th) return;
    AcquireSRWLockExclusive(&g_voices);
    EnterCriticalSection(&g_cs);
    g_qn = 0;
    LeaveCriticalSection(&g_cs);
    InterlockedIncrement(&g_gen);
    ReleaseSRWLockExclusive(&g_voices);
}
int Collect(Result* out, int max) {
    if (!g_th) return 0;
    EnterCriticalSection(&g_cs);
    int n = g_resN < max ? g_resN : max;
    memcpy(out, g_res, sizeof(Result) * n);
    memmove(g_res, g_res + n, sizeof(Result) * (g_resN - n));
    g_resN -= n;
    LeaveCriticalSection(&g_cs);
    return n;
}
}
int g_recCounter = 0;
LONG64 g_probeArmQpc[vmgame::kMaxModels] = {};
bool ProbeRecent(int i) { return g_probeArmQpc[i] && RealMs(RealQpc() - g_probeArmQpc[i]) < 600.0; }
bool g_trigPrev = false;
bool g_csvOn = true;
void EndBurst();
uint64_t Fnv(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}
template <class T>
uint64_t FnvV(uint64_t h, const T& v) {
    return Fnv(h, &v, sizeof v);
}
void RecBegin(double now, bool replay, bool ads, const wchar_t* label) {
    Rec& R = g_rec;
    R.on = true;
    R.replay = replay;
    R.ads = ads;
    R.released = R.truncated = R.stalled = false;
    R.lastR = 0;
    R.magSize = vmgame::g_mag.size;
    R.prevSus[0] = R.prevSus[1] = 0.f;
    R.prevJit = 0.f;
    R.id = ++g_recCounter;
    swprintf(R.label, 40, L"%ls", label);
    swprintf(R.weapon, 64, L"%ls", ModelName(vmgame::g_modelIdx));
    memcpy(R.layers, g_layer, sizeof R.layers);
    R.fpsMode = g_fpsMode;
    R.step = replay ? g_rp.step : 0;
    R.t0 = now;
    R.tPress = R.tRelease = R.tLastShot = -1;
    R.kick = 0;
    R.nShot = R.nSamp = R.tracers = R.swayMismatch = 0;
    R.ammo0 = vmgame::g_mag.ammo;
    R.lastFrameQpc = 0;
    R.frameMsSum = R.frameMsMax = 0;
    R.frameN = 0;
    R.nDense = 0;
}
void RecFinish(double now);
void RecTrigger(double now, bool held) {
    Rec& R = g_rec;
    if (held && !g_trigPrev) {
        if (R.on && !R.replay && R.released) RecFinish(now);
        if (!R.on) RecBegin(now, false, P.adsT > 0.5f, L"手动");
        if (R.tPress < 0) R.tPress = now - R.t0;
    } else if (!held && g_trigPrev && R.on && !R.released) {
        R.released = true;
        R.tRelease = now - R.t0;
    }
    g_trigPrev = held;
}
void RecShot(double now, const kick::View& kv, const Hit& h, const float aim[3], bool camHit, bool gunWill, double fireGap) {
    Rec& R = g_rec;
    if (!R.on) return;
    R.tLastShot = now - R.t0;
    if (R.nShot >= kRecShots) {
        R.truncated = true;
        return;
    }
    ShotRec& s = R.shots[R.nShot++];
    s.tFire = now - R.t0;
    double dueRel = P.nextShot + (g_fireOrigin - R.t0);
    s.tDue = R.nShot == 1 || dueRel < R.tPress ? R.tPress : dueRel;
    if (R.nShot > 1 && s.tFire - s.tDue >= fireGap) s.tDue = s.tFire;
    s.gunAt = s.camAt = s.sndAt = s.sndReal = -1;
    s.qpcFire = RealQpc();
    for (int i = 0; i < 3; ++i) s.hit[i] = h.hit ? aim[i] : 0.f;
    s.what = h.hit ? h.what : -1;
    s.aimPitch = kv.pitch / kick::kDeg;
    s.aimYaw = kv.yaw / kick::kDeg;
    s.climb = R.kick / kick::kDeg;
    s.punch = P.camKick.x[0];
    s.gunBack = s.gunPitch = 0.f;
    s.ammoAfter = vmgame::g_mag.ammo;
    s.gunPending = gunWill;
    s.camPending = camHit;
}
void RecCamFrame(double now) {
    Rec& R = g_rec;
    if (!R.on) return;
    for (int i = 0; i < R.nShot; ++i)
        if (R.shots[i].camPending) {
            R.shots[i].camAt = now - R.t0;
            R.shots[i].camPending = false;
        }
}
vm::GunSound* FireLayer() {
    using namespace vmgame;
    Weapon& w = Wp();
    bool ads = (g_stance & 1) != 0;
    vm::GunSound* order[4] = {ads && w.gunAds.Loaded() ? &w.gunAds : &w.gun, ads && w.mechAds.Loaded() ? &w.mechAds : &w.mech, &w.lfe, &w.atmo};
    for (vm::GunSound* gs : order)
        if (gs->Loaded()) return gs;
    return nullptr;
}
void RecPreDraw(double now) {
    using namespace vmgame;
    Rec& R = g_rec;
    if (!R.on) return;
    double r = now - R.t0;
    bool gunLive = g_layer[kGun] && g_kickShots > 0;
    for (int i = 0; i < R.nShot; ++i)
        if (R.shots[i].gunPending) {
            if (gunLive) R.shots[i].gunAt = r;
            R.shots[i].gunPending = false;
        }
    if (g_reqFire && g_layer[kSnd] && R.nShot > 0) {
        ShotRec& s = R.shots[R.nShot - 1];
        if (s.sndAt < 0 && s.tFire == r)
            if (vm::GunSound* gs = FireLayer()) {
                s.sndAt = r;
                probe::Arm(gs, s.qpcFire, R.id, R.nShot - 1);
                g_probeArmQpc[g_modelIdx] = RealQpc();
            }
    }
}
float SpringAt(float x0, float v0, const float spring[2], float t) {
    float w = 2.f * 3.14159265f * spring[0], a = spring[1] * w, wd = w * std::sqrt(1.f - spring[1] * spring[1]);
    return std::exp(-a * t) * (x0 * std::cos(wd * t) + (v0 + a * x0) / wd * std::sin(wd * t));
}
float g_camStepX = 0.f, g_camStepV = 0.f;
void RecPostDraw(double now) {
    Rec& R = g_rec;
    if (!R.on) return;
    double r = now - R.t0;
    const kick::GunSpring& gk = vmgame::g_gunKick;
    const vmgame::KickOut& ko = vmgame::g_kickOut;
    kick::Params kp;
    vmgame::GetKick(&kp);
    const float *gp = ko.gunPos, *gr = ko.gunRot, *sr = ko.susRot;
    float susBack = ko.susPos[2] * 100.f, susPitch = sr[0];
    float jitPitch = vmgame::g_jitter.x[0], jitRoll = vmgame::g_jitter.x[2];
    {
        if (R.nDense >= kDense) R.truncated = true;
        double gap = R.nDense ? r - R.lastR : 0.0;
        if (gap > 0.1) R.stalled = true;
        float dtGun = gap > 0.1 ? 0.1f : static_cast<float>(gap), dtCam = gap > 0.1 ? 0.f : static_cast<float>(gap);
        for (; R.nDense < kDense && R.nDense * kDenseStep <= r; ++R.nDense) {
            float back = 0.f, pitch = 0.f, punch = 0.f, tau = static_cast<float>(R.nDense * kDenseStep - r);
            float tauGun = tau < -dtGun ? -dtGun : tau, tauCam = tau < -dtCam ? -dtCam : tau;
            auto at = [&](float x1, float v1, float x2, float v2, const float* spr) {
                float a = (x1 != 0.f || v1 != 0.f) ? SpringAt(x1, v1, spr, tauGun) : 0.f;
                if (!ko.mode || ko.w <= 0.f) return a;
                float c = (x2 != 0.f || v2 != 0.f) ? SpringAt(x2, v2, spr, tauGun) : 0.f;
                return ko.w >= 1.f ? c : a + (c - a) * ko.w;
            };
            back = at(gk.pos[2], gk.vpos[2], gk.pos2[2], gk.vpos2[2], kp.gunPosSpring) * 100.f;
            pitch = at(gk.rot[0], gk.vrot[0], gk.rot2[0], gk.vrot2[0], kp.gunRotSpring);
            if (g_camStepX != 0.f || g_camStepV != 0.f) punch = SpringAt(g_camStepX, g_camStepV, kp.punchSpring, tauCam);
            double f = gap > 0 ? (R.nDense * kDenseStep - R.lastR) / gap : 1.0;
            float fs = static_cast<float>(f < 0 ? 0 : f > 1 ? 1 : f);
            back += R.prevSus[0] + (susBack - R.prevSus[0]) * fs;
            pitch += R.prevSus[1] + (susPitch - R.prevSus[1]) * fs + R.prevJit + (jitPitch - R.prevJit) * fs;
            R.dense[R.nDense] = {punch, back, pitch};
        }
        R.lastR = r;
        R.prevSus[0] = susBack;
        R.prevSus[1] = susPitch;
        R.prevJit = jitPitch;
    }
    for (int i = R.nShot - 1; i >= 0 && R.shots[i].tFire >= r - 0.5; --i)
        if (R.shots[i].gunAt == r) {
            R.shots[i].gunBack = gp[2] * 100.f + susBack;
            R.shots[i].gunPitch = gr[0] + susPitch + jitPitch;
        }
    if (R.nSamp >= kRecSamps) R.truncated = true;
    if (R.nSamp < kRecSamps) {
        float climb = R.kick / kick::kDeg;
        const vm::Renderer& rd = vmgame::Wp().rend;
        R.samps[R.nSamp++] = {r, climb, P.camKick.x[0], climb + P.camKick.x[0], gp[2] * 100.f + susBack, gr[0] + susPitch + jitPitch,
                              gr[2] + sr[2] + jitRoll, gp[2] * 100.f, gr[0], rd.IkShiftMm(0), rd.IkShiftMm(1), rd.IkResidualMm(0),
                              rd.IkResidualMm(1), jitPitch, jitRoll, susPitch, ko.w};
    }
}
void ProbeCollect() {
    probe::Result res[64];
    int n = probe::Collect(res, 64);
    for (int i = 0; i < n; ++i)
        if (res[i].rec == g_rec.id && res[i].shot >= 0 && res[i].shot < g_rec.nShot) g_rec.shots[res[i].shot].sndReal = res[i].ms;
}
double Settle(const Rec& R, float RecSamp::*f, double peak, double fin) {
    if (!(peak > 1e-6)) return -2;
    double thr = 0.05 * peak;
    int lastOut = -1, first = -1;
    for (int i = 0; i < R.nSamp; ++i) {
        if (R.samps[i].t < R.tLastShot) continue;
        if (first < 0) first = i;
        if (std::fabs(R.samps[i].*f - fin) > thr) lastOut = i;
    }
    if (first < 0) return -1;
    if (lastOut < 0) return 0;
    if (lastOut == R.nSamp - 1) return -1;
    return (R.samps[lastOut + 1].t - R.tLastShot) * 1000.0;
}
double DenseSettle(const Rec& R, float DenseSamp::*f, double peak) {
    if (!(peak > 1e-6)) return -2;
    double thr = 0.05 * peak;
    int j0 = static_cast<int>(std::ceil(R.tLastShot / kDenseStep - 1e-9)), lastOut = -1;
    if (j0 < 0) j0 = 0;
    if (j0 >= R.nDense) return -1;
    for (int j = j0; j < R.nDense; ++j)
        if (std::fabs(R.dense[j].*f) > thr) lastOut = j;
    if (lastOut < 0) return 0;
    if (lastOut == R.nDense - 1) return -1;
    return ((lastOut + 1) * kDenseStep - R.tLastShot) * 1000.0;
}
RecSum Summarize(const Rec& R) {
    RecSum S = {};
    S.valid = true;
    S.truncated = R.truncated;
    S.stalled = R.stalled;
    S.id = R.id;
    S.shots = R.nShot;
    S.tracers = R.tracers;
    S.swayMismatch = R.swayMismatch;
    swprintf(S.label, 40, L"%ls", R.label);
    swprintf(S.weapon, 64, L"%ls", R.weapon);
    LayerTags(R.layers, S.layerTags);
    S.replay = R.replay;
    S.ads = R.ads;
    S.fpsMode = R.fpsMode;
    S.stepMs = R.step * 1000.0 / benchclock::kHz;
    S.ammoUsed = R.magSize > 0 ? R.nShot : 0;
    S.pressToFirst = R.tPress >= 0 && R.nShot ? (R.shots[0].tFire - R.tPress) * 1000.0 : -1;
    auto avg = [&](double ShotRec::*at, double* outMax) {
        double sum = 0, mx = -1;
        int n = 0;
        for (int i = 0; i < R.nShot; ++i)
            if (R.shots[i].*at >= 0) {
                double d = (R.shots[i].*at - R.shots[i].tFire) * 1000.0;
                sum += d;
                mx = d > mx ? d : mx;
                ++n;
            }
        if (outMax) *outMax = n ? mx : -1;
        return n ? sum / n : -1.0;
    };
    S.gunFirst = avg(&ShotRec::gunAt, nullptr);
    S.camFirst = avg(&ShotRec::camAt, nullptr);
    S.sndSubmit = avg(&ShotRec::sndAt, nullptr);
    {
        double sum = 0, mx = -1;
        int n = 0;
        for (int i = 1; i < R.nShot; ++i) {
            double d = (R.shots[i].tFire - R.shots[i].tDue) * 1000.0;
            sum += d;
            mx = d > mx ? d : mx;
            ++n;
        }
        S.dueAvg = n ? sum / n : -1;
        S.dueMax = n ? mx : -1;
    }
    {
        double sum = 0, mn = 1e30, mx = -1;
        int n = 0;
        for (int i = 0; i < R.nShot; ++i)
            if (R.shots[i].sndReal >= 0) {
                double d = R.shots[i].sndReal;
                sum += d;
                mn = d < mn ? d : mn;
                mx = d > mx ? d : mx;
                ++n;
            }
        S.sndRealN = n;
        S.sndRealAvg = n ? sum / n : -1;
        S.sndRealMin = n ? mn : -1;
        S.sndRealMax = n ? mx : -1;
    }
    for (int i = 0; i < R.nSamp; ++i) {
        const RecSamp& m = R.samps[i];
        S.climbPeak = m.climb > S.climbPeak ? m.climb : S.climbPeak;
        S.punchPeak = m.punch > S.punchPeak ? m.punch : S.punchPeak;
        S.totalPeak = m.total > S.totalPeak ? m.total : S.totalPeak;
        S.backPeak = m.back > S.backPeak ? m.back : S.backPeak;
        S.pitchPeak = m.pitch > S.pitchPeak ? m.pitch : S.pitchPeak;
        S.rollPeak = std::fabs(m.roll) > S.rollPeak ? std::fabs(m.roll) : S.rollPeak;
    }
    S.punchSettle = Settle(R, &RecSamp::punch, S.punchPeak, 0.0);
    S.climbSettle = Settle(R, &RecSamp::climb, S.climbPeak, R.nSamp ? R.samps[R.nSamp - 1].climb : 0.0);
    S.backSettle = Settle(R, &RecSamp::back, S.backPeak, 0.0);
    S.pitchSettle = Settle(R, &RecSamp::pitch, S.pitchPeak, 0.0);
    for (int j = 0; j < R.nDense; ++j) {
        const DenseSamp& m = R.dense[j];
        S.punchPeakT = m.punch > S.punchPeakT ? m.punch : S.punchPeakT;
        S.backPeakT = m.back > S.backPeakT ? m.back : S.backPeakT;
        S.pitchPeakT = m.pitch > S.pitchPeakT ? m.pitch : S.pitchPeakT;
    }
    S.punchSettleT = DenseSettle(R, &DenseSamp::punch, S.punchPeakT);
    S.backSettleT = DenseSettle(R, &DenseSamp::back, S.backPeakT);
    S.pitchSettleT = DenseSettle(R, &DenseSamp::pitch, S.pitchPeakT);
    S.ikResMax = -1;
    for (int i = 0; i < R.nSamp; ++i) {
        const RecSamp& m = R.samps[i];
        double sb = m.back - m.kBack, sq = m.sPitch;
        S.susBackPeak = sb > S.susBackPeak ? sb : S.susBackPeak;
        S.susPitchPeak = sq > S.susPitchPeak ? sq : S.susPitchPeak;
        double jp = std::fabs(m.jPitch), jr = std::fabs(m.jRoll);
        S.jitPitchPeak = jp > S.jitPitchPeak ? jp : S.jitPitchPeak;
        S.jitRollPeak = jr > S.jitRollPeak ? jr : S.jitRollPeak;
        if (m.ikResR >= 0 || m.ikResL >= 0) ++S.ikFrames;
        const float sh[2] = {m.ikR, m.ikL}, rs[2] = {m.ikResR, m.ikResL};
        for (int k = 0; k < 2; ++k)
            if (rs[k] >= 0) {
                S.ikShiftPeak[k] = sh[k] > S.ikShiftPeak[k] ? sh[k] : S.ikShiftPeak[k];
                S.ikResMax = rs[k] > S.ikResMax ? rs[k] : S.ikResMax;
            }
    }
    S.frameMsAvg = R.frameN ? R.frameMsSum / R.frameN : 0;
    S.frameMsMax = R.frameMsMax;
    uint64_t hH = 1469598103934665603ull, hC = hH, hG = hH, hA = hH, hS = hH;
    for (int i = 0; i < R.nShot; ++i) {
        const ShotRec& s = R.shots[i];
        hH = FnvV(FnvV(FnvV(FnvV(FnvV(hH, s.tFire), s.hit), s.what), s.aimPitch), s.aimYaw);
        hC = FnvV(FnvV(FnvV(hC, s.climb), s.punch), s.camAt);
        hG = FnvV(FnvV(FnvV(hG, s.gunBack), s.gunPitch), s.gunAt);
        hA = FnvV(FnvV(FnvV(FnvV(hA, s.tDue), s.sndAt), s.ammoAfter), R.tPress);
    }
    for (int i = 0; i < R.nSamp; ++i) {
        const RecSamp& m = R.samps[i];
        hC = FnvV(FnvV(FnvV(hC, m.climb), m.punch), m.total);
        hG = FnvV(FnvV(FnvV(hG, m.back), m.pitch), m.roll);
        hS = FnvV(FnvV(hS, m.kBack), m.kPitch);
        hA = FnvV(hA, m.t);
    }
    for (int j = 0; j < R.nDense; ++j) {
        hC = FnvV(hC, R.dense[j].punch);
        hG = FnvV(FnvV(hG, R.dense[j].back), R.dense[j].pitch);
    }
    const double det[] = {S.pressToFirst, S.dueAvg, S.dueMax, S.gunFirst, S.camFirst, S.sndSubmit, S.climbPeak, S.punchPeak, S.totalPeak,
                          S.backPeak, S.pitchPeak, S.rollPeak, S.punchSettle, S.climbSettle, S.backSettle, S.pitchSettle,
                          S.punchPeakT, S.backPeakT, S.pitchPeakT, S.punchSettleT, S.backSettleT, S.pitchSettleT, S.susBackPeak,
                          S.susPitchPeak, S.jitPitchPeak, S.jitRollPeak};
    hA = FnvV(FnvV(FnvV(FnvV(FnvV(FnvV(FnvV(hA, det), hH), hC), hG), S.shots), S.ammoUsed), S.tracers);
    S.hHoles = hH;
    S.hCam = hC;
    S.hGun = hG;
    S.hSpring = hS;
    hA = FnvV(hA, hS);
    S.hAll = hA;
    return S;
}
int CompareSum(const RecSum& a, const RecSum& b, wchar_t* names, size_t cap) {
    struct It {
        const wchar_t* name;
        const void *x, *y;
        size_t n;
    } items[] = {
        {L"发数", &a.shots, &b.shots, sizeof a.shots}, {L"用弹", &a.ammoUsed, &b.ammoUsed, sizeof a.ammoUsed},
        {L"拖光数", &a.tracers, &b.tracers, sizeof a.tracers}, {L"按键→首发", &a.pressToFirst, &b.pressToFirst, 8},
        {L"到期→开火平均", &a.dueAvg, &b.dueAvg, 8}, {L"到期→开火最大", &a.dueMax, &b.dueMax, 8},
        {L"开火→枪身", &a.gunFirst, &b.gunFirst, 8}, {L"开火→镜头", &a.camFirst, &b.camFirst, 8},
        {L"开火→提交枪声", &a.sndSubmit, &b.sndSubmit, 8}, {L"上抬峰值", &a.climbPeak, &b.climbPeak, 8},
        {L"上跳峰值", &a.punchPeak, &b.punchPeak, 8}, {L"镜头总偏移峰值", &a.totalPeak, &b.totalPeak, 8},
        {L"后推峰值", &a.backPeak, &b.backPeak, 8}, {L"上扬峰值", &a.pitchPeak, &b.pitchPeak, 8},
        {L"滚转峰值", &a.rollPeak, &b.rollPeak, 8}, {L"上跳回稳", &a.punchSettle, &b.punchSettle, 8},
        {L"上抬回正", &a.climbSettle, &b.climbSettle, 8}, {L"后推回稳", &a.backSettle, &b.backSettle, 8},
        {L"上扬回稳", &a.pitchSettle, &b.pitchSettle, 8}, {L"上跳峰值（按时间）", &a.punchPeakT, &b.punchPeakT, 8},
        {L"后推峰值（按时间）", &a.backPeakT, &b.backPeakT, 8}, {L"上扬峰值（按时间）", &a.pitchPeakT, &b.pitchPeakT, 8},
        {L"上跳回稳（按时间）", &a.punchSettleT, &b.punchSettleT, 8}, {L"后推回稳（按时间）", &a.backSettleT, &b.backSettleT, 8},
        {L"上扬回稳（按时间）", &a.pitchSettleT, &b.pitchSettleT, 8}, {L"持续曲线后推峰值", &a.susBackPeak, &b.susBackPeak, 8},
        {L"持续曲线上扬峰值", &a.susPitchPeak, &b.susPitchPeak, 8}, {L"抖动上扬峰值", &a.jitPitchPeak, &b.jitPitchPeak, 8},
        {L"抖动滚转峰值", &a.jitRollPeak, &b.jitRollPeak, 8}, {L"枪身弹簧曲线", &a.hSpring, &b.hSpring, 8},
        {L"弹孔与开火方向", &a.hHoles, &b.hHoles, 8},
        {L"镜头逐发与曲线", &a.hCam, &b.hCam, 8}, {L"枪身逐发与曲线", &a.hGun, &b.hGun, 8}, {L"全部", &a.hAll, &b.hAll, 8},
    };
    int diff = 0;
    size_t len = 0;
    if (names && cap) names[0] = 0;
    for (const It& it : items)
        if (memcmp(it.x, it.y, it.n)) {
            ++diff;
            if (names && len + 16 < cap) len += swprintf(names + len, cap - len, L"%ls%ls", len ? L"、" : L"", it.name);
        }
    return diff;
}
constexpr int kCompareItems = 34;
const char* HitName(const ShotRec& s) {
    switch (s.what) {
    case 0: return "地面";
    case 1: return s.hit[2] < 17.f ? "10米靶" : s.hit[2] < 37.f ? "25米靶" : "50米靶";
    case 2: return "挡弹墙";
    case 3: return "其它";
    default: return "无";
    }
}
void Utf8(const wchar_t* w, char* out, int n) {
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, nullptr, nullptr)) out[0] = 0;
}
FILE* OpenCsv(const wchar_t* name, const char* header) {
    wchar_t p[MAX_PATH * 2];
    swprintf(p, MAX_PATH * 2, L"%ls\\%ls", g_exeDir, name);
    FILE* f = _wfopen(p, L"ab");
    if (!f) {
        BLog("bench: cannot open %ls for append (open in Excel?)", p);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) {
        fputs("\xEF\xBB\xBF", f);
        fputs(header, f);
    }
    return f;
}
void Cell(FILE* f, double v, const char* fmt = "%.3f") {
    fputc(',', f);
    if (v >= 0) fprintf(f, fmt, v);
}
void SettleCell(FILE* f, double v) {
    if (v <= -1.5) fputc(',', f);
    else if (v < 0) fputs(",-1", f);
    else fprintf(f, ",%.1f", v);
}
void WriteCsv(const Rec& R, const RecSum& S) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char when[32], label[128], weapon[128], tags[32], fps[32];
    snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    Utf8(R.label, label, sizeof label);
    Utf8(R.weapon, weapon, sizeof weapon);
    Utf8(S.layerTags, tags, sizeof tags);
    if (kFpsModes[R.fpsMode]) snprintf(fps, sizeof fps, "%d", kFpsModes[R.fpsMode]);
    else snprintf(fps, sizeof fps, "vsync%d", g_refreshHz);
    const char* stance = R.ads ? "举镜" : "腰射";
    if (FILE* f = OpenCsv(L"vm_bench_shots.csv",
                          "time,run,label,weapon,stance,layers,fps,step_ms,shot,t_fire_ms,press_to_fire_ms,due_lag_ms,snd_submit_ms,"
                          "snd_start_real_ms,gun_first_ms,cam_first_ms,hit_x,hit_y,hit_z,hit_on,aim_pitch_deg,aim_yaw_deg,cam_climb_deg,"
                          "cam_punch_deg,gun_back_cm,gun_pitch_deg,ammo_after\r\n")) {
        for (int i = 0; i < R.nShot; ++i) {
            const ShotRec& s = R.shots[i];
            fprintf(f, "%s,%d,%s,%s,%s,%s,%s,%.4f,%d,%.3f,%.3f,%.3f", when, R.id, label, weapon, stance, tags, fps, S.stepMs, i + 1,
                    s.tFire * 1000.0, R.tPress >= 0 ? (s.tFire - R.tPress) * 1000.0 : 0.0, (s.tFire - s.tDue) * 1000.0);
            Cell(f, s.sndAt >= 0 ? (s.sndAt - s.tFire) * 1000.0 : -1);
            Cell(f, s.sndReal);
            Cell(f, s.gunAt >= 0 ? (s.gunAt - s.tFire) * 1000.0 : -1);
            Cell(f, s.camAt >= 0 ? (s.camAt - s.tFire) * 1000.0 : -1);
            fprintf(f, ",%.4f,%.4f,%.4f,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%ld\r\n", s.hit[0], s.hit[1], s.hit[2], HitName(s), s.aimPitch, s.aimYaw,
                    s.climb, s.punch, s.gunBack, s.gunPitch, s.ammoAfter);
        }
        fclose(f);
    }
    if (FILE* f = OpenCsv(L"vm_bench_runs.csv",
                          "time,run,label,weapon,stance,layers,fps,step_ms,shots,ammo_used,press_to_first_ms,due_lag_avg_ms,due_lag_max_ms,"
                          "gun_first_avg_ms,cam_first_avg_ms,snd_submit_avg_ms,snd_start_real_avg_ms,snd_start_real_min_ms,snd_start_real_max_ms,"
                          "cam_climb_peak_deg,cam_punch_peak_deg,cam_total_peak_deg,cam_punch_settle_ms,cam_climb_settle_ms,gun_back_peak_cm,"
                          "gun_pitch_peak_deg,gun_roll_peak_deg,gun_back_settle_ms,gun_pitch_settle_ms,cam_punch_peak_t_deg,gun_back_peak_t_cm,"
                          "gun_pitch_peak_t_deg,cam_punch_settle_t_ms,gun_back_settle_t_ms,gun_pitch_settle_t_ms,sustain_back_peak_cm,"
                          "sustain_pitch_peak_deg,hand_ik_frames,hand_ik_shift_r_max_mm,hand_ik_shift_l_max_mm,hand_ik_residual_max_mm,"
                          "frame_real_avg_ms,frame_real_max_ms,"
                          "tracers,truncated,stalled,hash\r\n")) {
        fprintf(f, "%s,%d,%s,%s,%s,%s,%s,%.4f,%d,%ld", when, R.id, label, weapon, stance, tags, fps, S.stepMs, S.shots, S.ammoUsed);
        for (double v : {S.pressToFirst, S.dueAvg, S.dueMax, S.gunFirst, S.camFirst, S.sndSubmit, S.sndRealAvg, S.sndRealMin, S.sndRealMax}) Cell(f, v);
        fprintf(f, ",%.4f,%.4f,%.4f", S.climbPeak, S.punchPeak, S.totalPeak);
        for (double v : {S.punchSettle, S.climbSettle}) SettleCell(f, v);
        fprintf(f, ",%.4f,%.4f,%.4f", S.backPeak, S.pitchPeak, S.rollPeak);
        for (double v : {S.backSettle, S.pitchSettle}) SettleCell(f, v);
        fprintf(f, ",%.4f,%.4f,%.4f", S.punchPeakT, S.backPeakT, S.pitchPeakT);
        for (double v : {S.punchSettleT, S.backSettleT, S.pitchSettleT}) SettleCell(f, v);
        fprintf(f, ",%.4f,%.4f,%d", S.susBackPeak, S.susPitchPeak, S.ikFrames);
        for (double v : {S.ikShiftPeak[0], S.ikShiftPeak[1], S.ikResMax}) Cell(f, S.ikFrames ? v : -1.0);
        fprintf(f, ",%.3f,%.3f,%d,%d,%d,%016llx\r\n", S.frameMsAvg, S.frameMsMax, S.tracers, S.truncated ? 1 : 0, S.stalled ? 1 : 0,
                static_cast<unsigned long long>(S.hAll));
        fclose(f);
    }
}
void SumLine(const RecSum& S, char* out, size_t n) {
    char label[128], tags[32];
    Utf8(S.label, label, sizeof label);
    Utf8(S.layerTags, tags, sizeof tags);
    wchar_t fpsW[48];
    char fps[96];
    FpsLabel(S.fpsMode, fpsW, 48);
    Utf8(fpsW, fps, sizeof fps);
    snprintf(out, n,
             "#%d %s %s %s %s: shots=%d ammo=%ld | press->first %.1f ms, due lag avg %.2f max %.2f ms, fire->gun %.2f ms, fire->cam %.2f ms, "
             "fire->snd submit %.2f ms, snd start (real) avg %.2f min %.2f max %.2f ms (%d) | cam climb peak %.3f deg punch peak %.3f total %.3f, "
             "settle punch %.1f climb %.1f ms | gun back peak %.3f cm pitch %.3f deg roll %.3f, settle back %.1f pitch %.1f ms | "
             "by time: punch %.3f deg back %.3f cm pitch %.3f deg, settle punch %.1f back %.1f pitch %.1f ms | "
             "sustain back %.3f cm pitch %.3f deg | hand IK frames %d shift R %.1f L %.1f res max %.2f mm | "
             "frame %.2f / %.2f ms | tracers %d | hash %016llx%s%s",
             S.id, label, S.ads ? "ads" : "hip", tags, fps, S.shots, S.ammoUsed, S.pressToFirst, S.dueAvg, S.dueMax, S.gunFirst, S.camFirst,
             S.sndSubmit, S.sndRealAvg, S.sndRealMin, S.sndRealMax, S.sndRealN, S.climbPeak, S.punchPeak, S.totalPeak, S.punchSettle,
             S.climbSettle, S.backPeak, S.pitchPeak, S.rollPeak, S.backSettle, S.pitchSettle, S.punchPeakT, S.backPeakT, S.pitchPeakT,
             S.punchSettleT, S.backSettleT, S.pitchSettleT, S.susBackPeak, S.susPitchPeak, S.ikFrames, S.ikShiftPeak[0], S.ikShiftPeak[1],
             S.ikResMax, S.frameMsAvg, S.frameMsMax, S.tracers,
             static_cast<unsigned long long>(S.hAll), S.truncated ? " | TRUNCATED (record full: peaks / settle only cover the start)" : "",
             S.stalled ? " | STALLED (a frame gap > 0.1 s: by-time values approximate)" : "");
}
void RecFinish(double now) {
    (void)now;
    Rec& R = g_rec;
    if (!R.on) return;
    ProbeCollect();
    R.on = false;
    if (!R.nShot) return;
    RecSum S = Summarize(R);
    if (R.replay) {
        const RecSum* prev = nullptr;
        for (int i = g_hist8N - 1; i >= 0 && i >= g_hist8N - 8 && !prev; --i) {
            const RecSum& h = g_hist8[i % 8];
            if (h.ads == S.ads && h.fpsMode == S.fpsMode && !wcscmp(h.layerTags, S.layerTags) && !wcscmp(h.weapon, S.weapon)) prev = &h;
        }
        if (prev) {
            wchar_t names[256];
            int d = CompareSum(S, *prev, names, 256);
            if (!d) swprintf(S.same, 96, L"与 #%d（同组合）逐项相同", prev->id);
            else swprintf(S.same, 96, L"与 #%d（同组合）%d 项不同：%.40ls", prev->id, d, names);
        } else {
            swprintf(S.same, 96, L"同组合第一次重播");
        }
        g_hist8[g_hist8N % 8] = S;
        ++g_hist8N;
    }
    g_lastSum = S;
    if (g_csvOn) WriteCsv(R, S);
    char line[1400], same[300];
    SumLine(S, line, sizeof line);
    Utf8(S.same, same, sizeof same);
    BLog("bench: rec %s%s%s", line, same[0] ? " | " : "", same);
}
struct Saved {
    float vol[6], swayScale, bobScale, offY;
    bool flame, gas, wasReady, handIk;
    float susScale;
    float jitScale;
    int blendMode;
    float onScale;
    bool shell;
};
Saved g_saved[vmgame::kMaxModels];
bool g_overrideOn = false;
struct SwayPred {
    bool on;
    float aw;
    double t;
} g_swayPred = {};
int g_swayMismatchLog = 0;
void ApplyOverrides() {
    using namespace vmgame;
    g_swayPred.on = false;
    if (g_gasStyle >= 0)
        for (int i = 0; i < g_modelCount; ++i) g_w[i].set.flashGasStyle = g_gasStyle;
    if (g_shellFollow >= 0)
        for (int i = 0; i < g_modelCount; ++i) g_w[i].set.shellFollow = g_shellFollow;
    g_overrideOn = OverridesActive();
    if (!g_overrideOn) return;
    float aw = 0.f, awc = 0.f, swayK = 1.f, decay = 1.f, breathe = 0.f;
    double t = Now();
    if (!g_layer[kSway]) {
        float dt = g_mo.init ? static_cast<float>(t - g_mo.tPrev) : 0.f;
        if (dt < 0) dt = 0;
        if (dt > 0.1f) dt = 0.1f;
        LONG st = g_fp ? g_stance : 0;
        aw = AdsSmoothStep(g_mo.adsW, st, dt);
        awc = aw < 0.f ? 0.f : (aw > 1.f ? 1.f : aw);
        swayK = 1.f - 0.7f * aw;
        decay = std::exp(-dt * 9.0f);
        breathe = std::sin(static_cast<float>(t) * 1.7f) * 0.0018f;
    }
    for (int i = 0; i < g_modelCount; ++i) {
        vm::Settings& s = g_w[i].set;
        g_saved[i] = {{s.fireVolume, s.fireMechVolume, s.fireLfeVolume, s.fireAtmoVolume, s.fireTailVolume, s.spinVolume},
                      s.swayScale, s.bobScale, s.offset[1], s.flashFlame, s.flashGas, g_w[i].rend.Ready(), s.handIk, 0.f, 0.f, 0, 0.f};
        if (!g_layer[kIk]) s.handIk = false;
        if (!g_layer[kSnd]) s.fireVolume = s.fireMechVolume = s.fireLfeVolume = s.fireAtmoVolume = s.fireTailVolume = s.spinVolume = 0.f;
        g_saved[i].shell = s.shellEject;
        if (!g_layer[kFx]) s.flashFlame = s.flashGas = s.shellEject = false;
        if (!g_layer[kSway]) {
            s.swayScale = 1.f;
            s.bobScale = 0.f;
            s.offset[1] -= breathe * (1.f - awc);
        }
    }
    AcquireSRWLockExclusive(&g_kickLock);
    for (int i = 0; i < g_modelCount; ++i) {
        g_saved[i].susScale = g_kickP[i].sustainScale;
        if (!g_layer[kSus] || !g_layer[kGun]) g_kickP[i].sustainScale = 0.f;
        g_saved[i].jitScale = g_kickP[i].jitterScale;
        if (!g_layer[kJit] || !g_layer[kGun]) g_kickP[i].jitterScale = 0.f;
        g_saved[i].blendMode = g_kickP[i].blendMode;
        if (!g_layer[kBlend]) g_kickP[i].blendMode = 0;
        g_saved[i].onScale = g_kickP[i].onsetScale;
        if (!g_onsetOn || !g_layer[kGun]) g_kickP[i].onsetScale = 0.f;
    }
    ReleaseSRWLockExclusive(&g_kickLock);
    if (!g_layer[kSway]) {
        g_mo.haveView = false;
        g_mo.swayX = 0.f;
        g_mo.swayY = breathe * awc / 0.45f / (swayK * decay);
        g_swayPred = {true, aw, t};
    }
}
void RestoreOverrides() {
    using namespace vmgame;
    if (!g_overrideOn) return;
    g_overrideOn = false;
    for (int i = 0; i < g_modelCount; ++i) {
        vm::Settings& s = g_w[i].set;
        const Saved& v = g_saved[i];
        if (!v.wasReady && g_w[i].rend.Ready()) continue;
        s.fireVolume = v.vol[0], s.fireMechVolume = v.vol[1], s.fireLfeVolume = v.vol[2], s.fireAtmoVolume = v.vol[3], s.fireTailVolume = v.vol[4],
        s.spinVolume = v.vol[5];
        s.swayScale = v.swayScale, s.bobScale = v.bobScale, s.offset[1] = v.offY;
        s.flashFlame = v.flame, s.flashGas = v.gas, s.shellEject = v.shell;
        s.handIk = v.handIk;
    }
    AcquireSRWLockExclusive(&g_kickLock);
    for (int i = 0; i < g_modelCount; ++i)
        if (g_saved[i].wasReady || !g_w[i].rend.Ready())
            g_kickP[i].sustainScale = g_saved[i].susScale, g_kickP[i].jitterScale = g_saved[i].jitScale, g_kickP[i].blendMode = g_saved[i].blendMode,
            g_kickP[i].onsetScale = g_saved[i].onScale;
    ReleaseSRWLockExclusive(&g_kickLock);
}
void PreDrawLayer(double now) {
    using namespace vmgame;
    bool fxPending = false;
    for (int i = 0; i < g_modelCount; ++i)
        if (ProbeRecent(i) && g_w[i].rend.Ready() && (g_w[i].fxDirty || (i == g_modelIdx && g_reload))) fxPending = true;
    if (fxPending) probe::CancelAll();
    if (OverridesActive()) {
        if (InterlockedExchange(&g_reload, 0)) {
            LoadAllSettings(g_modelIdx, Wp().set);
            Wp().fxDirty = true;
            InterlockedExchange(&g_reqRoster, 1);
            if (Wp().rend.Ready()) MagApply();
            g_log("viewmodel: settings reloaded (bench: before layer switches)");
        }
        for (int i = 0; i < g_modelCount; ++i)
            if (g_w[i].rend.Ready() && g_w[i].fxDirty) LoadWeaponFx(i, g_w[i].rend.Device());
    }
    if (!g_layer[kGun]) {
        InterlockedExchange(&g_kickShots, 0);
        g_gunKick.Reset();
    }
    if (!g_layer[kGun] || !g_layer[kSus]) g_sustain.Reset();
    if (!g_layer[kGun] || !g_layer[kJit]) g_jitter.Reset();
    if (!g_layer[kGun] || !g_onsetOn) g_onset.Reset();
    RecPreDraw(now);
    ApplyOverrides();
}
void PostDrawLayer(double now) {
    using namespace vmgame;
    RestoreOverrides();
    if (g_swayPred.on && benchclock::g_replay && (g_mo.adsW != g_swayPred.aw || g_mo.tPrev != g_swayPred.t)) {
        if (g_rec.on) ++g_rec.swayMismatch;
        if (g_swayMismatchLog++ < 5)
            BLog("bench: WARNING sway-off prediction mismatch (adsW %.9g vs %.9g, t %.9f vs %.9f) - ComputeLook changed?", g_mo.adsW, g_swayPred.aw,
                 g_mo.tPrev, g_swayPred.t);
    }
    RecPostDraw(now);
}
void LayerChanged(const char* why) {
    using namespace vmgame;
    wchar_t tags[kLayers + 1];
    char t8[32];
    LayerTags(g_layer, tags);
    Utf8(tags, t8, sizeof t8);
    BLog("bench: layers %s (%s)", t8, why);
    if (g_rec.on && !g_rec.replay) RecFinish(Now());
    static bool blendWas = true;
    if (g_layer[kBlend] != blendWas) {
        blendWas = g_layer[kBlend];
        g_gunKick.Reset();
    }
    if (!g_layer[kSnd]) {
        Weapon& w = Wp();
        for (vm::GunSound* gs : {&w.gun, &w.gunAds, &w.mech, &w.mechAds, &w.lfe, &w.atmo, &w.tail, &w.spinUp, &w.spinLoop, &w.spinDown}) gs->FadeAll();
        w.tailPending = false;
    }
}
void ToggleLayer(int k) {
    g_layer[k] = !g_layer[k];
    LayerChanged("toggle");
}
void SoloLayer(int k) {
    for (int i = 0; i < kLayers; ++i) g_layer[i] = i == k;
    LayerChanged("solo");
}
void AllLayers() {
    for (bool& b : g_layer) b = true;
    LayerChanged("all on");
}
LONG64 g_nextFrameReal = 0;
void CycleFps() {
    if (g_rec.on && !g_rec.replay) RecFinish(vmgame::Now());
    g_fpsMode = (g_fpsMode + 1) % 4;
    g_nextFrameReal = 0;
    wchar_t w[48];
    char a[96];
    FpsLabel(g_fpsMode, w, 48);
    Utf8(w, a, sizeof a);
    BLog("bench: frame rate -> %s", a);
}
void LimitFrame() {
    int hz = kFpsModes[g_fpsMode];
    if (!hz) {
        g_nextFrameReal = 0;
        return;
    }
    benchclock::Live();
    LONG64 period = benchclock::g_realHz.QuadPart / hz, now = RealQpc();
    if (!g_nextFrameReal || now - g_nextFrameReal > period) g_nextFrameReal = now;
    while ((now = RealQpc()) < g_nextFrameReal) {
        if (RealMs(g_nextFrameReal - now) > 2.0) Sleep(1);
        else SwitchToThread();
    }
    g_nextFrameReal += period;
}
int QueryRefreshHz() {
    HMONITOR mon = MonitorFromWindow(G.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof mi;
    DEVMODEW dm = {};
    dm.dmSize = sizeof dm;
    if (mon && GetMonitorInfoW(mon, &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1 &&
        dm.dmDisplayFrequency < 1000)
        return static_cast<int>(dm.dmDisplayFrequency);
    return 60;
}
void AimAt25(float* yaw, float* pitch) {
    float y = 0.f, ex = 0.f, ez = 0.f;
    for (int k = 0; k < 5; ++k) {
        ex = std::sin(y) * cfg.eyeForward;
        ez = std::cos(y) * cfg.eyeForward;
        y = std::atan2(2.5f - ex, 25.f - ez);
    }
    ex = std::sin(y) * cfg.eyeForward;
    ez = std::cos(y) * cfg.eyeForward;
    *yaw = y;
    *pitch = std::atan2(1.5f - cfg.eyeHeight, std::sqrt((2.5f - ex) * (2.5f - ex) + (25.f - ez) * (25.f - ez)));
}
bool RequestReplay(int mode, const wchar_t* label) {
    using namespace vmgame;
    if (g_rp.armed || g_rp.active) return false;
    if (!g_fp || g_failed || !Wp().rend.Ready() || g_switching || g_sw.phase || g_reqWeapon || g_reqModelNext || g_reqRoster) {
        BLog("bench: replay refused (weapon not ready / switching)");
        return false;
    }
    g_rp.armed = true;
    g_rp.mode = mode;
    g_rp.ads = mode == kRpAds;
    swprintf(g_rp.label, 40, L"%ls", label);
    if (!kFpsModes[g_fpsMode]) g_refreshHz = QueryRefreshHz();
    return true;
}
void ReplayClock() {
    if (g_rp.armed) {
        LONG64 step = StepTicks(g_fpsMode), t0 = vmgame::g_t0.QuadPart;
        LONG64 n = benchclock::Live() + step - t0;
        n = (n + 16383) & ~16383LL;
        benchclock::g_frame = t0 + n;
        benchclock::g_replay = true;
        g_rp.step = step;
    } else if (g_rp.active) {
        benchclock::g_frame += g_rp.step;
    }
}
void ResetForReplay(double now) {
    using namespace vmgame;
    if (g_rec.on) RecFinish(now);
    double dt = static_cast<double>(g_rp.step) / benchclock::kHz;
    g_rp.armed = false;
    g_rp.active = true;
    g_rp.t0 = now;
    double adsT = vmgame::CurrentAdsTime(0) > 0.f ? vmgame::CurrentAdsTime(0) : cfg.adsTime;
    g_rp.pre = g_rp.ads ? adsT + 0.3 : 0.3;
    double spin = Wp().set.fireMode == 1 ? Wp().set.spinUpTime : 0.f;
    g_rp.hold = 1.0 + spin;
    g_rp.adsOn = g_rp.adsOff = -1.0;
    if (g_rp.mode == kRpSwitch) {
        g_rp.hold = 1.2 + spin;
        g_rp.adsOn = g_rp.pre + spin + 0.35;
        g_rp.adsOff = g_rp.pre + spin + 0.85;
    }
    g_rp.tail = 1.5;
    g_fireOrigin = now;
    float yaw, pitch;
    AimAt25(&yaw, &pitch);
    P.yaw = yaw;
    P.pitch = pitch;
    P.pos[0] = P.pos[1] = P.vel[0] = P.vel[1] = 0.f;
    P.adsT = P.camSpeed = 0.f;
    P.ads = P.sprint = P.tac = false;
    P.shiftPrev = true;
    P.fireBlock = 0;
    P.adsKind = 0;
    P.exitTac = false;
    P.nextShot = 0;
    P.tLastFrame = now - dt;
    P.camKick = kick::CamKick();
    P.camKick.rng.Seed(kSeedCam);
    g_trigPrev = false;
    g_gunKick = kick::GunSpring();
    g_gunKick.rng.Seed(kSeedGun);
    g_sustain.Reset();
    g_onset.Reset();
    g_sustainLastShot = -1;
    g_jitter = kick::Jitter();
    g_jitter.rng.Seed(kSeedJit);
    InterlockedExchange(&g_kickShots, 0);
    InterlockedExchange(&g_reqFire, 0);
    g_mo.init = true;
    g_mo.haveView = false;
    g_mo.tPrev = now - dt;
    g_mo.swayX = g_mo.swayY = g_mo.bobPhase = g_mo.bobAmount = g_mo.adsW = g_mo.sprintW = g_mo.blendT = 0.f;
    g_mo.barrelAngle = g_mo.barrelSpeed = 0.f;
    InterlockedExchange(&g_spin.state, 0);
    InterlockedExchange64(&g_spin.tUs, 0);
    Weapon& w = Wp();
    for (vm::GunSound* gs : {&w.gun, &w.gunAds, &w.mech, &w.mechAds, &w.lfe, &w.atmo, &w.tail, &w.spinUp, &w.spinLoop, &w.spinDown}) {
        gs->FadeAll();
        gs->*Member(benchpeek::Rng{}) = kSeedSnd;
        gs->*Member(benchpeek::Last{}) = -1;
    }
    w.tailPending = false;
    w.flash.Seed(kSeedFlash);
    InterlockedExchange(&g_mag.reload, 0);
    InterlockedExchange(&g_mag.reqAnim, 0);
    InterlockedExchange(&g_mag.ammo, g_mag.size);
    if (g_modelIdx < kMaxModels) InterlockedExchange(&g_spent[g_modelIdx], 0);
    g_an.reloadKind = 0;
    g_an.fireNext = 0;
    g_an.spinSeen = 0;
    g_an.stance = 0;
    g_an.stepK = -1;
    g_an.stepFrac = -1;
    if (g_an.idle >= 0) {
        Play(g_an.idle, now, 0.0);
        g_an.prev = -1;
    }
    AcquireSRWLockExclusive(&g_trq.lock);
    g_trq.n = 0;
    ReleaseSRWLockExclusive(&g_trq.lock);
    if (g_burst.open) EndBurst();
    RecBegin(now, true, g_rp.ads, g_rp.label);
    wchar_t tags[kLayers + 1], fpsW[48];
    char t8[32], f8[96], l8[96];
    LayerTags(g_layer, tags);
    FpsLabel(g_fpsMode, fpsW, 48);
    Utf8(tags, t8, sizeof t8);
    Utf8(fpsW, f8, sizeof f8);
    Utf8(g_rp.label, l8, sizeof l8);
    BLog("bench: replay #%d %s %s %s %s (step %.4f ms, aim yaw %.3f pitch %.3f deg, hold %.2f s%s)", g_rec.id, l8,
         g_rp.mode == kRpSwitch ? "hip->ads->hip" : g_rp.ads ? "ads" : "hip", t8, f8, dt * 1000.0, yaw / kick::kDeg, pitch / kick::kDeg, g_rp.hold,
         g_rp.mode == kRpSwitch ? ", ads held 0.35..0.85 s into the burst" : "");
}
void ReplayInput(double now, Input& in) {
    double r = now - g_rp.t0;
    in = Input();
    in.fg = true;
    in.lmb = r >= g_rp.pre && r < g_rp.pre + g_rp.hold;
    in.rmb = g_rp.ads || (g_rp.mode == kRpSwitch && r >= g_rp.adsOn && r < g_rp.adsOff);
}
bool ReplayFrameEnd(double now) {
    if (!g_rp.active || now - g_rp.t0 < g_rp.pre + g_rp.hold + g_rp.tail) return false;
    RecFinish(now);
    g_rp.active = false;
    benchclock::ToLive();
    P.nextShot += g_fireOrigin;
    g_fireOrigin = 0;
    return true;
}
void RecFrameEnd(double now) {
    ProbeCollect();
    Rec& R = g_rec;
    if (R.on && !R.replay && R.released && now - R.t0 - (R.tLastShot > R.tRelease ? R.tLastShot : R.tRelease) >= 1.5) RecFinish(now);
}
Hit Shoot(const kick::View& kv, float aimOut[3]) {
    const float* eye = g_vs.eye;
    const float *fwd = kv.fwd, *right = kv.right, *up = kv.up;
    float nx = 0, ny = 0, aspect = 1, ray[3];
    bool muzzle = vmgame::Muzzle(&nx, &ny, &aspect);
    if (muzzle) {
        float t = std::tan(g_vs.fovY * 0.5f);
        for (int i = 0; i < 3; ++i) ray[i] = fwd[i] + right[i] * (nx * t * aspect) + up[i] * (ny * t);
    } else {
        for (int i = 0; i < 3; ++i) ray[i] = fwd[i];
    }
    float d = muzzle ? cfg.muzzleDepth : cfg.fireForward;
    float vis[3] = {eye[0] + ray[0] * d, eye[1] + ray[1] * d, eye[2] + ray[2] * d};
    Hit h = Trace(eye, fwd, cfg.fireRange);
    float aim[3] = {eye[0] + fwd[0] * h.dist, eye[1] + fwd[1] * h.dist, eye[2] + fwd[2] * h.dist};
    memcpy(aimOut, aim, sizeof aim);
    if (g_layer[kFx]) {
        vmgame::Tracer(vis, aim);
        if (g_rec.on) ++g_rec.tracers;
    }
    if (h.hit) {
        Hole& o = g_holes[g_holeHead];
        memcpy(o.p, aim, sizeof o.p);
        memcpy(o.n, h.n, sizeof o.n);
        o.r = h.dist * 0.0035f > 0.02f ? h.dist * 0.0035f : 0.02f;
        o.burst = g_burst.id;
        g_holeHead = (g_holeHead + 1) % kMaxHoles;
        if (g_holeN < kMaxHoles) ++g_holeN;
    }
    return h;
}
void EndBurst() {
    g_burst.open = false;
    g_lastBurst = g_burst;
    BLog("bench: burst %d: %d shots in %.2f s, kick climb %.2f deg (yaw %+.2f), aim net %.2f deg, punch max %.2f deg, "
         "gun back max %.2f cm, gun pitch max %.2f deg, roll max %.2f deg",
         g_burst.id, g_burst.shots, g_burst.tLast - g_burst.tStart, g_burst.climbPeak, g_burst.kickYaw / kick::kDeg, g_burst.netPeak,
         g_burst.punchMax, g_burst.backMax, g_burst.gunPitchMax, g_burst.rollMax);
}
void StartBurst(double now) {
    if (g_burst.open) EndBurst();
    int id = g_burst.id + 1;
    g_burst = Burst();
    g_burst.id = id;
    g_burst.open = true;
    g_burst.tStart = now;
    g_burst.refPitch = P.pitch;
}
void GameFrame(double now, const Input& in) {
    float shieldW = 0.f, shieldMove = 1.f, shieldZoom = 1.f;
    vmgame::ShieldGame(&shieldW, &shieldMove, &shieldZoom);
    float zoomNow = 1.f + (cfg.adsZoom - 1.f) * shieldZoom * P.adsT * P.adsT * (3.f - 2.f * P.adsT);
    float rad = cfg.sensitivity * kPi / 180.f / zoomNow;
    P.yaw += in.dx * rad;
    P.pitch += (cfg.invertY ? in.dy : -in.dy) * rad;
    const float lim = 85.f * kPi / 180.f;
    if (P.pitch > lim) P.pitch = lim;
    if (P.pitch < -lim) P.pitch = -lim;
    if (P.yaw > kPi) P.yaw -= 2 * kPi;
    if (P.yaw < -kPi) P.yaw += 2 * kPi;
    float sy = std::sin(P.yaw), cy = std::cos(P.yaw);
    float dt = static_cast<float>(now - P.tLastFrame);
    P.tLastFrame = now;
    if (!(dt > 0.f) || dt > 0.1f) dt = 0.f;
    float mx = 0, mz = 0;
    if (in.fg) {
        float fwdIn = (in.w ? 1.f : 0.f) - (in.s ? 1.f : 0.f), sideIn = (in.d ? 1.f : 0.f) - (in.a ? 1.f : 0.f);
        mx = sy * fwdIn + cy * sideIn;
        mz = cy * fwdIn - sy * sideIn;
        float ml = std::sqrt(mx * mx + mz * mz);
        if (ml > 0.01f) {
            mx /= ml;
            mz /= ml;
        } else {
            mx = mz = 0;
        }
    }
    bool wantMove = mx != 0 || mz != 0;
    {
        bool rmb = in.fg && in.rmb, lmb = in.fg && in.lmb, shift = in.fg && in.shift;
        bool tap = shift && !P.shiftPrev;
        P.shiftPrev = shift;
        bool reloading = vmgame::Busy();
        bool canSprint = in.fg && wantMove && in.w && !in.s && !rmb && !lmb && !reloading;
        if (P.sprint && !canSprint) {
            P.fireBlock = now + (P.tac ? cfg.tacSprintToFire : cfg.sprintToFire);
            P.exitTac = P.tac;
            P.sprint = P.tac = false;
        } else if (tap && canSprint) {
            if (!P.sprint)
                P.sprint = true;
            else if (!vmgame::ShieldWanted())
                P.tac = true;
        }
        bool wasAds = P.ads;
        P.ads = rmb && !P.sprint && !reloading && !vmgame::MeleeWeapon();
        if (P.ads && !wasAds) P.adsKind = now < P.fireBlock ? (P.exitTac ? 2 : 1) : 0;
        if (!P.ads) P.adsKind = 0;
        float adsTime = vmgame::CurrentAdsTime(P.adsKind);
        if (!(adsTime > 0.f)) adsTime = P.adsKind == 2 ? cfg.adsTimeTac : P.adsKind == 1 ? cfg.adsTimeSprint : cfg.adsTime;
        float step = dt > 0.f ? dt / adsTime : 0.f;
        P.adsT += P.ads ? step : -step;
        if (P.adsT < 0.f) P.adsT = 0.f;
        if (P.adsT > 1.f) P.adsT = 1.f;
        vmgame::PublishStance(P.ads, P.sprint, P.tac, P.adsKind);
    }
    {
        float spd = P.tac ? cfg.tacSprintSpeed : P.sprint ? cfg.sprintSpeed : cfg.runSpeed * (1.f - (1.f - cfg.adsMoveScale) * P.adsT);
        spd *= shieldMove;
        if (dt > 0.f) {
            float a = cfg.accelTau > 0.001f ? 1.f - std::exp(-dt / cfg.accelTau) : 1.f;
            P.vel[0] += (mx * spd - P.vel[0]) * a;
            P.vel[1] += (mz * spd - P.vel[1]) * a;
            if (!wantMove && P.vel[0] * P.vel[0] + P.vel[1] * P.vel[1] < 0.01f) P.vel[0] = P.vel[1] = 0;
            P.pos[0] += P.vel[0] * dt;
            P.pos[1] += P.vel[1] * dt;
            for (float& p : P.pos) p = p < -150.f ? -150.f : p > 150.f ? 150.f : p;
        }
        P.camSpeed = std::sqrt(P.vel[0] * P.vel[0] + P.vel[1] * P.vel[1]);
    }
    kick::Params kp;
    vmgame::GetKick(&kp);
    float adsW = P.adsT * P.adsT * (3.f - 2.f * P.adsT);
    if (!g_shakeOn) kp.shakeScale = 0.f;
    bool camOn = g_layer[kCam];
    if (!camOn) {
        for (int i = 0; i < 3; ++i) P.camKick.x[i] = P.camKick.v[i] = 0.f;
        P.camKick.recoverLeft = P.camKick.burstClimb = 0.f;
        P.camKick.burst = false;
        P.camKick.ShakeClear();
    }
    float before = P.pitch;
    P.camKick.Step(kp, now, dt, &P.pitch);
    if (float hk = vmgame::ShieldCamKick())
        if (camOn) P.camKick.v[0] += kick::Impulse(hk, kp.punchSpring[0], kp.punchSpring[1]);
    g_camStepX = P.camKick.x[0];
    g_camStepV = P.camKick.v[0];
    g_burst.kickPitch += P.pitch - before;
    if (g_rec.on) g_rec.kick += P.pitch - before;
    kick::View kv = kick::CamView(P.camKick, P.pitch, P.yaw, lim);
    g_shView[0] = kv.pitch - P.pitch - P.camKick.x[0] * kick::kDeg;
    g_shView[1] = kv.roll - P.camKick.x[2] * kick::kDeg;
    RecCamFrame(now);
    vmgame::PublishView(P.yaw, P.pitch, P.camSpeed);
    g_vs.eye[0] = P.pos[0] + sy * cfg.eyeForward;
    g_vs.eye[1] = cfg.eyeHeight;
    g_vs.eye[2] = P.pos[1] + cy * cfg.eyeForward;
    g_vs.kv = kv;
    g_vs.fovY = 2.f * std::atan(std::tan(BaseFov() * 0.5f) / zoomNow);
    vmgame::PublishCamera(g_vs.eye, kv.right, kv.up, kv.fwd, g_vs.fovY);
    bool melee = vmgame::MeleeWeapon();
    if (melee) vmgame::MeleeInput(in.fg && in.lmb && !P.sprint, in.fg && in.rmb && !P.sprint);
    if (melee && vmgame::TakeMeleeStrike() >= 0) ++g_benchStrikes;
    bool held = in.fg && in.lmb && !melee;
    RecTrigger(now, held);
    double fireGap = vmgame::FireInterval() > 0 ? vmgame::FireInterval() : cfg.fireInterval, fnow = now - g_fireOrigin;
    vmgame::SetTrigger(held && cfg.fireEnabled && !P.sprint && now >= P.fireBlock);
    if (held && cfg.fireEnabled && fnow >= P.nextShot && !P.sprint && now >= P.fireBlock && vmgame::CanFire()) {
        bool kickOn = camOn && kp.enabled && kp.camScale > 0.f;
        bool newBurst = kickOn ? !P.camKick.burst || now - P.camKick.lastShot > kp.burstReset
                               : !g_burst.open || now - g_burst.tLast > kp.burstReset;
        if (newBurst) StartBurst(now);
        float aim[3];
        Hit h = Shoot(kv, aim);
        vmgame::Fire();
        float bp = P.pitch, by = P.yaw;
        unsigned camShots = P.camKick.shots;
        if (camOn) P.camKick.OnShot(kp, adsW, now, &P.pitch, &P.yaw, lim);
        g_burst.kickPitch += P.pitch - bp;
        g_burst.kickYaw += P.yaw - by;
        ++g_burst.shots;
        g_burst.tLast = now;
        if (g_rec.on) g_rec.kick += P.pitch - bp;
        RecShot(now, kv, h, aim, P.camKick.shots != camShots, g_layer[kGun] && kp.enabled && kp.gunScale > 0.f, fireGap);
        P.nextShot = fnow - P.nextShot < fireGap ? P.nextShot + fireGap : fnow + fireGap;
    }
    float climb = g_burst.kickPitch / kick::kDeg, net = (P.pitch - g_burst.refPitch) / kick::kDeg;
    const float *gp = vmgame::g_kickOut.gunPos, *gr = vmgame::g_kickOut.gunRot;
    if (g_burst.open) {
        if (climb > g_burst.climbPeak) g_burst.climbPeak = climb;
        if (net > g_burst.netPeak) g_burst.netPeak = net;
        if (P.camKick.x[0] > g_burst.punchMax) g_burst.punchMax = P.camKick.x[0];
        if (gp[2] * 100.f > g_burst.backMax) g_burst.backMax = gp[2] * 100.f;
        if (gr[0] > g_burst.gunPitchMax) g_burst.gunPitchMax = gr[0];
        if (std::fabs(gr[2]) > g_burst.rollMax) g_burst.rollMax = std::fabs(gr[2]);
        if (now - g_burst.tLast > kp.burstReset + 0.3) EndBurst();
    }
    const float *sp = vmgame::g_kickOut.susPos, *sr = vmgame::g_kickOut.susRot;
    const float* jx = vmgame::g_jitter.x;
    Sample& s = g_hist[g_histHead];
    s = {now, net, climb, P.camKick.x[0], (gp[2] + sp[2]) * 100.f, gr[0] + sr[0] + jx[0], gr[2] + sr[2] + jx[2]};
    g_histHead = (g_histHead + 1) % kHist;
    if (g_histN < kHist) ++g_histN;
}
struct Ev {
    double t;
    int what;
    int n;
    const wchar_t* shot;
};
const Ev kEvKick[] = {
    {0.5, 0, 0, nullptr}, {0.8, 1, 10, nullptr}, {1.3, 2, 0, L"bench_hip_burst.bmp"}, {2.4, 4, 0, nullptr},
    {2.6, 2, 0, L"bench_hip_settled.bmp"}, {2.7, 3, 1, nullptr}, {3.2, 1, 10, nullptr}, {3.7, 2, 0, L"bench_ads_burst.bmp"},
    {4.8, 4, 1, nullptr}, {4.9, 3, 0, nullptr}, {5.4, 2, 0, L"bench_pattern.bmp"}, {5.5, 0, 0, nullptr}, {5.6, 5, 0, nullptr},
};
const Ev kEvIdle[] = {{0.5, 0, 0, nullptr}, {8.0, 0, 0, nullptr}, {8.1, 5, 0, nullptr}};
const Ev kEvStance[] = {
    {0.5, 9, 1, nullptr}, {2.0, 1, 3, nullptr}, {2.6, 6, 1, nullptr}, {2.8, 7, 0, nullptr}, {3.8, 8, 0, nullptr},
    {4.3, 2, 0, L"stance_sprint_reload.bmp"}, {7.2, 1, 3, nullptr}, {7.8, 7, 0, nullptr}, {8.0, 7, 0, nullptr},
    {9.0, 8, 1, nullptr}, {12.5, 6, 0, nullptr}, {12.6, 9, 2, nullptr}, {15.0, 1, 20, nullptr}, {17.0, 6, 1, nullptr},
    {17.05, 7, 1, nullptr}, {17.5, 2, 0, L"stance_minigun_sprint.bmp"}, {19.5, 6, 0, nullptr}, {19.6, 10, 0, nullptr},
    {19.7, 5, 0, nullptr},
};
const Ev kEvSwap[] = {
    {0.5, 9, 1, nullptr}, {1.0, 6, 1, nullptr}, {1.2, 7, 0, nullptr}, {2.2, 12, 2, nullptr}, {2.5, 2, 0, L"swap_sprint_mid.bmp"},
    {6.0, 7, 0, nullptr}, {6.3, 7, 0, nullptr}, {7.3, 12, 1, nullptr}, {7.6, 2, 0, L"swap_tac_mid.bmp"}, {11.0, 6, 0, nullptr},
    {11.2, 11, 0, nullptr}, {11.3, 5, 0, nullptr},
};
const Ev kEvSprintAds[] = {
    {0.5, 9, 1, nullptr}, {1.0, 6, 1, nullptr}, {1.2, 7, 0, nullptr}, {2.4, 6, 0, nullptr}, {2.4, 13, 0, nullptr},
    {2.5, 2, 0, L"sprintads_mid.bmp"}, {3.6, 3, 0, nullptr}, {4.4, 6, 1, nullptr}, {4.6, 7, 0, nullptr}, {4.9, 7, 0, nullptr},
    {6.0, 6, 0, nullptr}, {6.0, 13, 1, nullptr}, {6.1, 2, 0, L"sprintads_tac_mid.bmp"}, {7.2, 3, 0, nullptr}, {8.5, 13, 2, nullptr},
    {9.3, 3, 0, nullptr}, {9.6, 9, 2, nullptr}, {12.5, 6, 1, nullptr}, {12.7, 7, 0, nullptr}, {13.7, 6, 0, nullptr},
    {13.7, 13, 3, nullptr}, {14.9, 3, 0, nullptr}, {15.5, 6, 1, nullptr}, {15.7, 7, 0, nullptr}, {16.0, 7, 0, nullptr},
    {17.0, 6, 0, nullptr}, {17.0, 13, 4, nullptr}, {18.4, 3, 0, nullptr}, {19.8, 13, 5, nullptr}, {21.0, 3, 0, nullptr},
    {21.6, 14, 0, nullptr}, {21.7, 5, 0, nullptr},
};
#define GAS_SHOTS(t,tag) {t + 0.017, 2, 0, L"gas_" tag L"_017.bmp"}, {t + 0.033, 2, 0, L"gas_" tag L"_033.bmp"}, {t + 0.05, 2, 0, L"gas_" tag L"_050.bmp"}, {t + 0.067, 2, 0, L"gas_" tag L"_067.bmp"}, {t + 0.1, 2, 0, L"gas_" tag L"_100.bmp"}, {t + 0.15, 2, 0, L"gas_" tag L"_150.bmp"}, {t + 0.25, 2, 0, L"gas_" tag L"_250.bmp"}
const Ev kEvCrossFade[] = {
    {0.5, 9, 1, nullptr}, {0.6, 17, 0, nullptr}, {1.0, 2, 0, L"cf_m4_00_rest.bmp"}, {1.2, 1, 20, nullptr},
    {1.45, 2, 0, L"cf_m4_025.bmp"}, {1.7, 2, 0, L"cf_m4_050.bmp"}, {1.95, 2, 0, L"cf_m4_075.bmp"}, {2.2, 2, 0, L"cf_m4_100.bmp"},
    {2.45, 2, 0, L"cf_m4_125.bmp"}, {2.7, 2, 0, L"cf_m4_150.bmp"}, {3.15, 2, 0, L"cf_m4_195.bmp"},
    {3.3, 2, 0, L"cf_m4_rel_010.bmp"}, {3.45, 2, 0, L"cf_m4_rel_025.bmp"}, {3.7, 2, 0, L"cf_m4_rel_050.bmp"},
    {4.2, 9, 2, nullptr}, {5.6, 1, 25, nullptr}, {6.3, 2, 0, L"cf_mg_070.bmp"}, {6.8, 2, 0, L"cf_mg_120.bmp"}, {7.3, 2, 0, L"cf_mg_170.bmp"},
    {7.9, 2, 0, L"cf_mg_230.bmp"}, {8.4, 2, 0, L"cf_mg_rel_030.bmp"}, {8.9, 17, 1, nullptr}, {9.0, 5, 0, nullptr},
};
const Ev kEvGas[] = {
    {0.5, 9, 1, nullptr}, {0.6, 17, 0, nullptr}, {0.7, 16, 1, nullptr}, {1.5, 15, 0, nullptr}, GAS_SHOTS(1.5, L"new_hip"),
    {2.5, 16, 0, nullptr}, {3.0, 15, 0, nullptr}, GAS_SHOTS(3.0, L"old_hip"), {4.0, 3, 1, nullptr}, {4.1, 16, 1, nullptr},
    {5.0, 15, 0, nullptr}, GAS_SHOTS(5.0, L"new_ads"), {6.0, 16, 0, nullptr}, {6.5, 15, 0, nullptr}, GAS_SHOTS(6.5, L"old_ads"),
    {7.5, 3, 0, nullptr}, {8.0, 16, 1, nullptr}, {8.5, 1, 6, nullptr}, {9.0, 2, 0, L"gas_new_burst.bmp"}, {10.0, 16, 0, nullptr},
    {10.5, 1, 6, nullptr}, {11.0, 2, 0, L"gas_old_burst.bmp"}, {11.7, 17, 1, nullptr}, {11.8, 5, 0, nullptr},
};
#undef GAS_SHOTS
#define SHELL_SHOTS(t,tag) {t + 0.017, 2, 0, L"shell_" tag L"_017.bmp"}, {t + 0.033, 2, 0, L"shell_" tag L"_033.bmp"}, {t + 0.05, 2, 0, L"shell_" tag L"_050.bmp"}, {t + 0.083, 2, 0, L"shell_" tag L"_083.bmp"}, {t + 0.133, 2, 0, L"shell_" tag L"_133.bmp"}, {t + 0.2, 2, 0, L"shell_" tag L"_200.bmp"}, {t + 0.3, 2, 0, L"shell_" tag L"_300.bmp"}
const Ev kEvShell[] = {
    {0.5, 9, 1, nullptr}, {0.6, 17, 0, nullptr}, {1.5, 15, 0, nullptr}, SHELL_SHOTS(1.5, L"hip"), {2.6, 3, 1, nullptr},
    {3.2, 15, 0, nullptr}, SHELL_SHOTS(3.2, L"ads"), {4.2, 3, 0, nullptr}, {4.6, 1, 5, nullptr}, {4.95, 2, 0, L"shell_m4_burst.bmp"},
    {5.8, 9, 2, nullptr}, {8.6, 1, 15, nullptr}, {9.3, 2, 0, L"shell_minigun_a.bmp"}, {9.45, 2, 0, L"shell_minigun_b.bmp"},
    {9.6, 2, 0, L"shell_minigun_c.bmp"}, {9.8, 2, 0, L"shell_minigun_burst.bmp"}, {10.6, 17, 1, nullptr}, {10.7, 5, 0, nullptr},
};
const Ev kEvShellMove[] = {
    {0.5, 9, 1, nullptr}, {0.6, 17, 0, nullptr}, {0.7, 19, 1, nullptr}, {1.0, 18, 1, nullptr}, {1.5, 20, 0, nullptr}, {1.6, 1, 8, nullptr},
    {2.0, 18, 2, nullptr}, {2.08, 2, 0, L"shellmove_new_080.bmp"}, {2.16, 2, 0, L"shellmove_new_160.bmp"}, {2.3, 2, 0, L"shellmove_new_300.bmp"},
    {2.6, 18, 0, nullptr}, {3.4, 20, 1, nullptr}, {3.5, 19, 0, nullptr}, {4.0, 18, 1, nullptr}, {4.6, 1, 8, nullptr},
    {5.0, 18, 2, nullptr}, {5.08, 2, 0, L"shellmove_old_080.bmp"}, {5.16, 2, 0, L"shellmove_old_160.bmp"}, {5.3, 2, 0, L"shellmove_old_300.bmp"},
    {5.6, 18, 0, nullptr}, {6.4, 20, -1, nullptr}, {6.5, 19, -1, nullptr}, {6.6, 17, 1, nullptr}, {6.7, 5, 0, nullptr},
};
const Ev kEvLastShot[] = {
    {0.5, 9, 1, nullptr}, {0.6, 17, 0, nullptr}, {1.0, 30, 4, nullptr}, {1.2, 1, 6, nullptr}, {1.33, 2, 0, L"lastshot_hip_a.bmp"},
    {1.45, 2, 0, L"lastshot_hip_b.bmp"}, {3.0, 31, 1, nullptr}, {5.2, 3, 1, nullptr}, {5.6, 30, 3, nullptr}, {5.8, 1, 5, nullptr},
    {6.0, 2, 0, L"lastshot_ads.bmp"}, {7.6, 31, 2, nullptr}, {8.0, 3, 0, nullptr}, {8.5, 9, 2, nullptr}, {11.0, 1, 20, nullptr},
    {14.5, 31, 3, nullptr}, {14.9, 17, 1, nullptr}, {15.0, 5, 0, nullptr},
};
#undef SHELL_SHOTS
const Ev kEvOnset[] = {
    {0.5, 9, 1, nullptr}, {0.6, 17, 0, nullptr}, {0.9, 30, 99, nullptr}, {0.95, 2, 0, L"onset_hip_rest.bmp"}, {1.0, 32, 1, nullptr},
    {1.0, 1, 30, nullptr}, {1.06, 2, 0, L"onset_hip_peak.bmp"}, {4.5, 33, 1, nullptr}, {4.6, 32, 2, nullptr}, {4.7, 30, 99, nullptr},
    {4.8, 1, 2, nullptr}, {5.5, 1, 2, nullptr}, {6.2, 1, 2, nullptr}, {6.9, 33, 2, nullptr}, {7.0, 3, 1, nullptr}, {7.6, 30, 99, nullptr},
    {7.75, 2, 0, L"onset_ads_rest.bmp"}, {7.8, 32, 3, nullptr}, {7.8, 1, 20, nullptr}, {7.86, 2, 0, L"onset_ads_peak.bmp"},
    {10.2, 33, 3, nullptr}, {10.3, 3, 0, nullptr}, {10.9, 32, 4, nullptr}, {11.0, 15, 0, nullptr}, {11.25, 15, 0, nullptr},
    {11.5, 15, 0, nullptr}, {11.75, 15, 0, nullptr}, {12.4, 33, 4, nullptr}, {12.5, 34, 0, nullptr}, {12.55, 32, 5, nullptr},
    {12.6, 1, 5, nullptr}, {13.4, 33, 5, nullptr}, {13.45, 34, 1, nullptr}, {13.5, 17, 1, nullptr}, {13.6, 5, 0, nullptr},
};
struct OnRec {
    double t;
    float ph, w, bw, pos[3], rot[3];
};
OnRec g_on[8192];
int g_onN = 0, g_onSeg0[8] = {}, g_onFails = 0, g_onStages = 0;
void OnsetRecord(double now) {
    using namespace vmgame;
    if (g_onN >= 8192) return;
    OnRec& r = g_on[g_onN++];
    r.t = now, r.ph = g_onset.t, r.w = g_onset.w, r.bw = g_kickOut.w;
    for (int i = 0; i < 3; ++i) r.pos[i] = g_kickOut.onPos[i], r.rot[i] = g_kickOut.onRot[i];
}
bool EvaluateOnset(int n) {
    using namespace vmgame;
    kick::Params kp;
    AcquireSRWLockShared(&g_kickLock);
    kp = g_kickP[g_modelIdx];
    ReleaseSRWLockShared(&g_kickLock);
    int a = g_onSeg0[n & 7], b = g_onN;
    float g = kp.gunScale * kp.onsetScale;
    float peakR = 0.f, peakS = 0.f, peakPh = -1.f, maxJump = 0.f, maxErr = 0.f, tailMax = 0.f, afterStop = 0.f;
    int edges = 0, burstPeaksOk = 0;
    float firstPeak = 0.f, laterPeak = 0.f;
    double tStop = -1;
    for (int i = a; i < b; ++i) {
        const OnRec& r = g_on[i];
        float ar = std::fabs(r.rot[2]);
        if (ar > peakR) peakR = ar, peakPh = r.ph;
        peakS = std::max(peakS, std::fabs(r.pos[0]));
        if (i > a) maxJump = std::max(maxJump, std::fabs(r.rot[2] - g_on[i - 1].rot[2]));
        if (i > 0 && g_on[i - 1].ph <= 0.f && r.ph > 0.f) ++edges;
        if (i > a && r.ph > g_on[i - 1].ph) tStop = r.t;
        float ws = r.w * r.w * (3.f - 2.f * r.w), amp = kp.onsetHip[5] + (kp.onsetAds[5] - kp.onsetHip[5]) * r.bw;
        maxErr = std::max(maxErr, std::fabs(r.rot[2] - ws * g * kick::Onset::Rot(kp, r.ph) * amp));
        if (r.ph >= kp.onsetEnd[1]) tailMax = std::max(tailMax, ar);
    }
    for (int i = a; i < b; ++i)
        if (tStop > 0 && g_on[i].t >= tStop + 0.25) afterStop = std::max(afterStop, std::fabs(g_on[i].rot[2]));
    if (n == 2 || n == 4) {
        float cur = 0.f;
        int k = 0;
        for (int i = a; i < b; ++i) {
            if (i > 0 && g_on[i - 1].ph <= 0.f && g_on[i].ph > 0.f) {
                if (k > 0 && cur >= 0.8f * g * std::fabs(kp.onsetHip[5])) ++burstPeaksOk;
                cur = 0.f, ++k;
            }
            cur = std::max(cur, std::fabs(g_on[i].rot[2]));
        }
        if (k > 0 && cur >= 0.8f * g * std::fabs(kp.onsetHip[5])) ++burstPeaksOk;
        double t1 = -1;
        for (int i = a; i < b; ++i) {
            if (t1 < 0 && g_on[i].ph > 0.f) t1 = g_on[i].t;
            if (t1 >= 0 && g_on[i].t < t1 + 0.2) firstPeak = std::max(firstPeak, std::fabs(g_on[i].rot[2]));
            if (t1 >= 0 && g_on[i].t >= t1 + 0.2) laterPeak = std::max(laterPeak, std::fabs(g_on[i].rot[2]));
        }
    }
    float expR = g * std::fabs(n == 3 ? kp.onsetAds[5] : kp.onsetHip[5]), expS = g * std::fabs(n == 3 ? kp.onsetAds[0] : kp.onsetHip[0]);
    bool ok = false;
    if (n == 1)
        ok = edges == 1 && peakPh >= 0.045f && peakPh <= 0.075f && std::fabs(peakR - expR) <= 0.15f * expR && std::fabs(peakS - expS) <= 0.15f * expS &&
             maxErr < 1e-3f && tailMax < 0.01f * expR && afterStop < 0.01f * expR && maxJump < 0.6f * expR;
    if (n == 2) ok = edges == 3 && burstPeaksOk == 3 && maxErr < 1e-3f;
    if (n == 3)
        ok = edges == 1 && std::fabs(peakR - expR) <= 0.2f * expR && std::fabs(peakS - expS) <= 0.2f * expS && maxErr < 1e-3f && afterStop < 0.01f * expR;
    if (n == 4) ok = edges == 1 && firstPeak >= 0.8f * g * std::fabs(kp.onsetHip[5]) && laterPeak < 0.7f * firstPeak && maxErr < 1e-3f;
    if (n == 5) ok = peakR < 1e-6f && peakS < 1e-9f;
    ++g_onStages;
    g_onFails += ok ? 0 : 1;
    BLog("ONSET stage %d: frames %d bursts %d | roll peak %.3f deg (want %.3f) at phase %.1f ms, right peak %.2f mm (want %.2f) | formula err %.5f deg | "
         "after end %.4f, 0.25 s after stop %.4f, max frame jump %.3f deg | bursts at full %d, first tap %.3f later %.3f -> %s",
         n, b - a, edges, peakR, n == 5 ? 0.f : expR, peakPh * 1000.f, peakS * 1000.f, n == 5 ? 0.f : expS * 1000.f, maxErr, tailMax, afterStop, maxJump,
         burstPeaksOk, firstPeak, laterPeak, ok ? "ok" : "FAIL");
    return ok;
}
void OnsetWriteCsv(const wchar_t* outDir) {
    wchar_t p[MAX_PATH * 2];
    swprintf(p, MAX_PATH * 2, L"%ls\\onset_trace.csv", outDir);
    FILE* f = _wfopen(p, L"w");
    if (!f) return;
    fprintf(f, "t,phase,weight,ads_w,right_m,up_m,back_m,pitch_deg,yaw_deg,roll_deg\n");
    for (int i = 0; i < g_onN; ++i) {
        const OnRec& r = g_on[i];
        fprintf(f, "%.5f,%.5f,%.4f,%.4f,%.6f,%.6f,%.6f,%.4f,%.4f,%.4f\n", r.t - g_on[0].t, r.ph, r.w, r.bw, r.pos[0], r.pos[1], r.pos[2], r.rot[0], r.rot[1],
                r.rot[2]);
    }
    fclose(f);
    BLog("ONSET trace %d frames -> onset_trace.csv", g_onN);
}
const Ev kEvMelee[] = {
    {0.3, 0, 0, nullptr}, {1.0, 1, 1, nullptr}, {1.42, 2, 0, L"melee_01_light1.bmp"}, {1.55, 1, 1, nullptr},
    {1.95, 2, 0, L"melee_02_light2.bmp"}, {2.10, 1, 1, nullptr}, {2.55, 2, 0, L"melee_03_light3.bmp"},
    {2.80, 1, 1, nullptr}, {3.20, 2, 0, L"melee_04_light1_again.bmp"}, {4.6, 1, 8, nullptr},
    {5.2, 2, 0, L"melee_05_charge.bmp"}, {5.75, 2, 0, L"melee_06_heavy1.bmp"}, {5.95, 1, 8, nullptr},
    {6.6, 2, 0, L"melee_07_charge_diag.bmp"}, {7.05, 2, 0, L"melee_08_heavy2.bmp"}, {7.3, 1, 8, nullptr},
    {7.9, 2, 0, L"melee_09_charge_stab.bmp"}, {8.4, 2, 0, L"melee_10_heavy3.bmp"}, {10.0, 3, 1, nullptr},
    {10.45, 2, 0, L"melee_11_block.bmp"}, {10.6, 1, 1, nullptr}, {10.75, 2, 0, L"melee_12_push.bmp"},
    {11.6, 3, 0, nullptr}, {12.5, 3, 1, nullptr}, {13.0, 1, 5, nullptr}, {13.3, 3, 0, nullptr},
    {13.5, 2, 0, L"melee_13_push_stab.bmp"}, {15.5, 2, 0, L"melee_14_idle.bmp"}, {15.6, 5, 0, nullptr},
};
const Ev kEvMeleeBlock[] = {
    {0.3, 0, 0, nullptr}, {0.9, 27, 0, nullptr}, {1.0, 22, 0, nullptr}, {1.2, 27, 1, nullptr},
    {1.5, 3, 1, nullptr}, {1.95, 2, 0, L"mblock_01_block.bmp"}, {2.0, 27, 2, nullptr}, {2.0, 22, 0, nullptr},
    {2.03, 2, 0, L"mblock_02_hit.bmp"}, {2.06, 27, 3, nullptr}, {3.2, 27, 4, nullptr}, {3.3, 3, 0, nullptr},
    {3.8, 27, 5, nullptr}, {3.9, 22, 0, nullptr}, {4.1, 27, 6, nullptr}, {4.4, 5, 0, nullptr},
};
struct MeleeBlockCp {
    bool set;
    double t;
    float frac, lastBlock, shakeMax;
    int meleeBlock, act, shaking, camKicks;
};
struct MeleeBlockMon {
    MeleeBlockCp cp[8];
    float shakeMax = 0.f;
    int camKicks = 0;
} g_mb;
void MeleeBlockMonitor() {
    float a = std::fabs(vmgame::g_sh.meleeShakeNow);
    if (a > g_mb.shakeMax) g_mb.shakeMax = a;
    if (vmgame::g_sh.camKickMdeg > 0) ++g_mb.camKicks;
}
void MeleeBlockCheckpoint(int k, double now) {
    using namespace vmgame;
    if (k < 0 || k >= 8) return;
    MeleeBlockCp& c = g_mb.cp[k];
    c.set = true, c.t = now, c.frac = ShieldBlockFraction(), c.lastBlock = g_sh.lastBlock, c.shakeMax = g_mb.shakeMax;
    c.meleeBlock = g_sh.meleeBlock, c.act = melee::g.act, c.shaking = g_sh.meleeShakeAt >= 0 ? 1 : 0, c.camKicks = g_mb.camKicks;
    BLog("MELEEBLOCK cp%d t=%.3f act %s block-state %d fraction %.3f last blocked %.3f shaking %d | since last cp: shake peak %.4f rad (%.2f deg), "
         "cam kicks %d",
         k, now, melee::kDef[c.act].name, c.meleeBlock, c.frac, c.lastBlock, c.shaking, c.shakeMax, c.shakeMax * 57.29578f, c.camKicks);
    g_mb.shakeMax = 0.f;
    g_mb.camKicks = 0;
}
int g_mbFails = 0;
bool EvaluateMeleeBlock(int shotsOk, int shotsWanted) {
    g_mbFails = 0;
    auto check = [](bool ok, const char* what) {
        BLog("BENCH MELEEBLOCK %s: %s", ok ? "ok" : "FAIL", what);
        if (!ok) ++g_mbFails;
    };
    const MeleeBlockCp* c = g_mb.cp;
    for (int k = 0; k < 7; ++k)
        if (!c[k].set) check(false, "checkpoint missing");
    float want = vmgame::Wp().set.shieldHitShake * 0.017453293f;
    check(vmgame::MeleeWeapon(), "first weapon is the spear (melee clips)");
    check(c[0].meleeBlock == 0 && c[0].frac == 0.f, "idle: not blocking, fraction 0");
    check(c[1].shaking == 0 && c[1].shakeMax == 0.f && c[1].lastBlock == 0.f && c[1].camKicks == 0, "hit while idle: not blocked, no shake, no cam kick");
    check(c[2].meleeBlock == 1 && std::fabs(c[2].frac - 1.f) < 1e-3f, "block held: block state, fraction 1.0 (shield_block_ads)");
    check(c[3].shaking == 1 && std::fabs(c[3].lastBlock - 1.f) < 1e-3f && c[3].camKicks >= 1 && c[3].shakeMax >= 0.6f * want,
          "hit while blocking: blocked 100%, whole viewmodel shakes (peak >= 0.6 x shield_hit_shake), cam kick");
    check(c[4].shaking == 0 && c[4].meleeBlock == 1, "shake settled, still blocking");
    check(c[5].meleeBlock == 0 && c[5].frac == 0.f, "block released: fraction 0");
    check(c[6].shaking == 0 && c[6].shakeMax == 0.f && c[6].camKicks == 0, "hit after release: not blocked, no shake");
    check(shotsOk == shotsWanted, "screenshots");
    return g_mbFails == 0;
}
double g_rapid[4096][4];
int g_rapidN = 0;
const Ev kEvRapid[] = {
    {0.3, 0, 0, nullptr}, {1.00, 1, 1, nullptr}, {1.15, 1, 1, nullptr}, {1.30, 1, 1, nullptr}, {1.45, 1, 1, nullptr}, {1.60, 1, 1, nullptr},
    {1.75, 1, 1, nullptr}, {1.90, 1, 1, nullptr}, {2.05, 1, 1, nullptr}, {2.20, 1, 1, nullptr}, {2.35, 1, 1, nullptr}, {2.50, 1, 1, nullptr},
    {2.65, 1, 1, nullptr}, {2.80, 1, 1, nullptr}, {2.95, 1, 1, nullptr}, {3.10, 1, 1, nullptr}, {3.25, 1, 1, nullptr}, {3.40, 1, 1, nullptr},
    {3.55, 1, 1, nullptr}, {3.70, 1, 1, nullptr}, {3.85, 1, 1, nullptr}, {4.00, 1, 1, nullptr}, {6.0, 5, 0, nullptr},
};
const Ev kEvShake[] = {
    {0.5, 9, 1, nullptr}, {0.9, 30, 99, nullptr}, {1.0, 40, 1, nullptr}, {1.0, 1, 10, nullptr}, {1.3, 2, 0, L"shake_hip_burst.bmp"},
    {2.6, 41, 1, nullptr}, {2.7, 3, 1, nullptr}, {3.4, 30, 99, nullptr}, {3.5, 40, 2, nullptr}, {3.5, 1, 10, nullptr},
    {4.0, 2, 0, L"shake_ads_burst.bmp"}, {4.9, 41, 2, nullptr}, {5.0, 3, 0, nullptr}, {5.6, 40, 3, nullptr},
    {5.7, 15, 0, nullptr}, {6.1, 15, 0, nullptr}, {6.5, 15, 0, nullptr}, {7.1, 41, 3, nullptr}, {7.2, 42, 0, nullptr},
    {7.3, 40, 4, nullptr}, {7.3, 1, 5, nullptr}, {8.1, 41, 4, nullptr}, {8.2, 42, 1, nullptr}, {8.3, 5, 0, nullptr},
};
struct ShRec {
    double t;
    float out[3], view[2], age;
    unsigned shots;
};
ShRec g_shakeRec[8192];
int g_shakeN = 0, g_shakeSeg0[8] = {}, g_shakeFails = 0, g_shakeStages = 0;
void ShakeRecord(double now) {
    if (g_shakeN >= 8192) return;
    ShRec& r = g_shakeRec[g_shakeN++];
    r.t = now;
    for (int i = 0; i < 3; ++i) r.out[i] = P.camKick.shOut[i];
    r.view[0] = g_shView[0], r.view[1] = g_shView[1];
    r.age = P.camKick.shAge;
    r.shots = P.camKick.shots;
}
bool EvaluateShake(int n) {
    using namespace vmgame;
    kick::Params kp;
    AcquireSRWLockShared(&g_kickLock);
    kp = g_kickP[g_modelIdx];
    ReleaseSRWLockShared(&g_kickLock);
    int a = g_shakeSeg0[n & 7], b = g_shakeN;
    float g = kp.camScale * kp.shakeScale, amp[3], peak[3] = {}, viewErr = 0.f, maxJump = 0.f, tail = 0.f;
    for (int k = 0; k < 3; ++k) amp[k] = (n == 2 ? kp.shakeAds[k] : kp.shakeHip[k]) * g;
    int starts = 0, toZero = 0;
    double tLast = -1;
    for (int i = a; i < b; ++i) {
        const ShRec& r = g_shakeRec[i];
        for (int k = 0; k < 3; ++k) peak[k] = std::max(peak[k], std::fabs(r.out[k]));
        viewErr = std::max(viewErr, std::max(std::fabs(r.view[0] - r.out[0] * kick::kDeg), std::fabs(r.view[1] - r.out[2] * kick::kDeg)));
        if (i > a) {
            const ShRec& q = g_shakeRec[i - 1];
            maxJump = std::max(maxJump, std::fabs(r.out[0] - q.out[0]));
            if (r.age < q.age) ++starts;
            if (r.shots != q.shots) tLast = r.t;
            if ((q.out[0] != 0.f || q.out[2] != 0.f) && r.out[0] == 0.f && r.out[1] == 0.f && r.out[2] == 0.f) ++toZero;
        }
    }
    for (int i = a; i < b; ++i)
        if (tLast > 0 && g_shakeRec[i].t >= tLast + kp.shakeTime + 0.03)
            for (int k = 0; k < 3; ++k) tail = std::max(tail, std::fabs(g_shakeRec[i].out[k]));
    bool within = true;
    for (int k = 0; k < 3; ++k) within = within && peak[k] <= amp[k] + 1e-5f;
    bool ok = false;
    if (n == 1 || n == 2) ok = within && peak[0] >= 0.3f * amp[0] && starts >= 10 && viewErr < 2e-6f && tLast > 0 && tail == 0.f;
    if (n == 3) ok = within && starts == 3 && toZero == 3 && peak[0] > 0.f && viewErr < 2e-6f && tail == 0.f;
    if (n == 4) ok = peak[0] == 0.f && peak[1] == 0.f && peak[2] == 0.f && viewErr < 1e-7f && tLast > 0;
    ++g_shakeStages;
    g_shakeFails += ok ? 0 : 1;
    BLog("SHAKE stage %d: frames %d starts %d back-to-zero %d | peak %.4f / %.4f / %.4f deg (limit %.4f / %.4f / %.4f) | view - shake %.2e rad | "
         "after shake_time %.4f | max frame jump %.4f deg -> %s",
         n, b - a, starts, toZero, peak[0], peak[1], peak[2], n == 4 ? 0.f : amp[0], n == 4 ? 0.f : amp[1], n == 4 ? 0.f : amp[2], viewErr, tail, maxJump,
         ok ? "ok" : "FAIL");
    return ok;
}
void ShakeWriteCsv(const wchar_t* outDir) {
    wchar_t p[MAX_PATH * 2];
    swprintf(p, MAX_PATH * 2, L"%ls\\shake_trace.csv", outDir);
    FILE* f = _wfopen(p, L"w");
    if (!f) return;
    fprintf(f, "t,pitch_deg,yaw_deg,roll_deg,view_pitch_extra_deg,age_s,shots\n");
    for (int i = 0; i < g_shakeN; ++i) {
        const ShRec& r = g_shakeRec[i];
        fprintf(f, "%.5f,%.5f,%.5f,%.5f,%.5f,%.4f,%u\n", r.t - g_shakeRec[0].t, r.out[0], r.out[1], r.out[2], r.view[0] / kick::kDeg, r.age > 99.f ? 99.f : r.age,
                r.shots);
    }
    fclose(f);
    BLog("SHAKE trace %d frames -> shake_trace.csv", g_shakeN);
}
const Ev kEvShield[] = {
    {0.5, 9, 1, nullptr}, {0.6, 17, 0, nullptr}, {1.0, 2, 0, L"shield_00_none.bmp"}, {1.05, 24, 11, nullptr}, {1.1, 21, 1, nullptr},
    {1.25, 2, 0, L"shield_01_raising.bmp"}, {1.6, 24, 0, nullptr}, {1.7, 2, 0, L"shield_02_up.bmp"}, {2.0, 1, 10, nullptr},
    {2.5, 2, 0, L"shield_03_fire.bmp"}, {3.5, 3, 1, nullptr}, {4.3, 24, 1, nullptr}, {4.35, 2, 0, L"shield_04_ads.bmp"},
    {4.6, 3, 0, nullptr}, {5.2, 25, 0, nullptr}, {5.26, 2, 0, L"shield_05a_lower_060ms.bmp"}, {5.35, 2, 0, L"shield_05_reload_lowering.bmp"},
    {5.42, 2, 0, L"shield_05b_lower_220ms.bmp"}, {5.9, 24, 2, nullptr},
    {6.2, 2, 0, L"shield_06_reload.bmp"}, {8.0, 24, 3, nullptr}, {8.05, 2, 0, L"shield_07_after_reload.bmp"}, {8.2, 6, 1, nullptr},
    {8.4, 7, 0, nullptr}, {8.9, 24, 4, nullptr}, {8.95, 2, 0, L"shield_08_sprint.bmp"}, {9.0, 7, 0, nullptr}, {9.3, 24, 5, nullptr},
    {9.6, 6, 0, nullptr}, {10.3, 24, 6, nullptr}, {10.5, 22, 0, nullptr}, {10.52, 2, 0, L"shield_09_hit.bmp"}, {10.7, 22, 0, nullptr},
    {10.9, 22, 0, nullptr}, {11.1, 22, 0, nullptr}, {11.3, 22, 0, nullptr}, {11.33, 2, 0, L"shield_10_hit5.bmp"}, {11.6, 24, 7, nullptr},
    {11.65, 2, 0, L"shield_11_after_hits.bmp"}, {11.7, 3, 1, nullptr}, {12.1, 22, 0, nullptr}, {12.13, 2, 0, L"shield_12_rmb_hit.bmp"},
    {12.3, 24, 12, nullptr}, {12.35, 3, 0, nullptr}, {12.7, 21, 0, nullptr}, {13.1, 22, 0, nullptr}, {13.3, 24, 8, nullptr},
    {13.4, 21, 1, nullptr}, {13.9, 24, 9, nullptr}, {13.95, 2, 0, L"shield_13_reraised.bmp"}, {14.0, 9, 2, nullptr},
    {16.9, 24, 10, nullptr}, {16.95, 2, 0, L"shield_14_minigun.bmp"}, {17.0, 21, 0, nullptr}, {17.1, 17, 1, nullptr}, {17.2, 23, 0, nullptr},
    {17.3, 5, 0, nullptr},
};
struct ShieldCp {
    bool set = false;
    double t = -1;
    float w = 0, ik = -1, adsT = 0, zoom = 1, move = 1, adsTime0 = 0, weight = 0, block = -1, fore = -1, shift = -1;
    int phase = -1, allowed = -1, stance = -1, model = -1, ammo = -1, lookDraw = -2, blocked = -1;
    float sv[3] = {}, wrist[3] = {};
};
struct ShieldMon {
    ShieldCp cp[13];
    double tR = -1, tReloadClip = -1, tW0 = -1;
    float wAtReloadClip = -1;
    int maxSparks = 0, phaseChanges = 0, lastPhase = -1;
    int blocked = 0;
    double lastShakeAt = -1;
    float blockFrac[16] = {};
    float lowerMinZ = 1e9f, lowerMinZAt = -1;
    int ammoBeforeBurst = -1;
} g_shm;
void ShieldCheckpoint(int k, double now) {
    using namespace vmgame;
    if (k < 0 || k >= 13) return;
    ShieldCp& c = g_shm.cp[k];
    c.set = true, c.t = now, c.w = g_sh.w, c.phase = g_sh.phase, c.allowed = g_sh.allowed, c.blocked = g_shm.blocked;
    c.lookDraw = g_sh.lookDraw, c.ik = Wp().rend.ShieldIkResidualMm(), c.fore = Wp().rend.ShieldForearmErrDeg(), c.shift = Wp().rend.ShieldArmShiftMm(), c.stance = g_an.stance, c.model = g_modelIdx, c.ammo = g_mag.ammo;
    c.adsT = P.adsT, c.adsTime0 = AdsTime(0), c.block = ShieldBlockFraction();
    ShieldGame(&c.weight, &c.move, &c.zoom);
    float m[12];
    if (Wp().rend.ShieldView(m)) c.sv[0] = m[3], c.sv[1] = m[7], c.sv[2] = m[11];
    Wp().rend.LeftWristView(c.wrist);
    BLog("SHIELD cp%d t=%.3f w %.3f phase %d draw %d blocked %d block %.2f allowed %d stance %d model %d ammo %d | ik %.1f mm | game weight %.2f "
         "move %.2f zoom keep %.2f adsT %.2f ads time %.3f s | shield at %.3f,%.3f,%.3f wrist %.3f,%.3f,%.3f | forearm err %.1f deg shift %.0f mm | sparks %d",
         k, now, c.w, c.phase, c.lookDraw, c.blocked, c.block, c.allowed, c.stance, c.model, c.ammo, c.ik, c.weight, c.move, c.zoom, c.adsT, c.adsTime0,
         c.sv[0], c.sv[1], c.sv[2], c.wrist[0], c.wrist[1], c.wrist[2], c.fore, c.shift, g_shieldR.LiveSparks());
}
void ShieldMonitor(double now) {
    using namespace vmgame;
    int spk = g_shieldR.LiveSparks();
    if (spk > g_shm.maxSparks) g_shm.maxSparks = spk;
    if (g_sh.shakeAt >= 0 && g_sh.shakeAt != g_shm.lastShakeAt) {
        if (g_shm.blocked < 16) g_shm.blockFrac[g_shm.blocked] = g_sh.lastBlock;
        ++g_shm.blocked, g_shm.lastShakeAt = g_sh.shakeAt;
    }
    if (g_sh.phase != g_shm.lastPhase) ++g_shm.phaseChanges, g_shm.lastPhase = g_sh.phase;
    if (g_shm.tR >= 0 && g_shm.tW0 < 0 && g_sh.w <= 0.f) g_shm.tW0 = now;
    if (g_shm.tR >= 0 && g_shm.tReloadClip < 0 && g_sh.lookDraw >= 0) {
        float m[12];
        if (Wp().rend.ShieldView(m) && m[11] < g_shm.lowerMinZ) g_shm.lowerMinZ = m[11], g_shm.lowerMinZAt = g_sh.w;
        static int lowerLogs = 0;
        float wv[3];
        Wp().rend.LeftWristView(wv);
        if (lowerLogs++ < 80)
            BLog("SHIELD lower t=%.3f w %.3f ik %.1f mm | shield %.3f,%.3f,%.3f | wrist %.3f,%.3f,%.3f", now, g_sh.w, Wp().rend.ShieldIkResidualMm(), m[3], m[7],
                 m[11], wv[0], wv[1], wv[2]);
    }
    int cur = g_an.cur;
    if (g_shm.tR >= 0 && g_shm.tReloadClip < 0 && cur >= 0 && (cur == Wp().rend.FindClip("reload") || cur == Wp().rend.FindClip("reload_empty")))
        g_shm.tReloadClip = now, g_shm.wAtReloadClip = g_sh.w;
}
int g_shieldFails = 0;
bool EvaluateShield(int shotsOk, int shotsWanted) {
    g_shieldFails = 0;
    auto check = [](bool ok, const char* what, double a, double b) {
        BLog("BENCH SHIELD %s: %s (%.4f / %.4f)", ok ? "ok" : "FAIL", what, a, b);
        if (!ok) ++g_shieldFails;
    };
    const ShieldCp* c = g_shm.cp;
    for (int k = 0; k <= 12; ++k)
        if (k != 11 && !c[k].set) check(false, "checkpoint missing", k, 0);
    check(c[11].lookDraw == -1 && c[11].w == 0.f, "before Q: no shield drawn", c[11].lookDraw, c[11].w);
    check(c[0].phase == 2 && c[0].w >= 1.f && c[0].lookDraw == 0, "raised 0.5 s after Q (phase up, drawn)", c[0].phase, c[0].w);
    check(c[0].ik >= 0.f && c[0].ik < 10.f, "left wrist reached the grip (IK residual mm)", c[0].ik, 10.0);
    check(std::fabs(c[0].move - 0.8f) < 0.01f && std::fabs(c[0].weight - 1.f) < 1e-3f, "move scale 0.8 while shield up", c[0].move, c[0].weight);
    check(c[11].ammo - c[1].ammo >= 8, "fired while holding the shield (rounds spent)", c[11].ammo - c[1].ammo, 8);
    check(std::fabs(c[1].zoom - 0.5f) < 0.01f && c[1].adsT > 0.99f, "ADS with shield: zoom keep 0.5, fully in", c[1].zoom, c[1].adsT);
    check(std::fabs(c[1].adsTime0 - c[11].adsTime0 * 1.5f) < 0.01f, "ADS time x1.5 with shield", c[1].adsTime0, c[11].adsTime0);
    check(g_shm.tReloadClip > 0 && g_shm.wAtReloadClip <= 0.001f, "reload clip started only after the shield was lowered (w at reload start)",
          g_shm.wAtReloadClip, g_shm.tReloadClip - g_shm.tR);
    check(g_shm.tReloadClip - g_shm.tR > 0.2 && g_shm.tReloadClip - g_shm.tR < 0.4, "reload start delay = lower time 0.25 s", g_shm.tReloadClip - g_shm.tR, 0.25);
    {
        float stowZ = vmgame::Wp().set.shieldStowPos[2], nearZ = std::min(c[0].sv[2], stowZ);
        BLog("SHIELD lowering for reload: closest shield origin depth %.3f m (at raise %.2f); held depth %.3f m, stow depth %.3f m", g_shm.lowerMinZ,
             g_shm.lowerMinZAt, c[0].sv[2], stowZ);
        check(g_shm.lowerMinZ > nearZ - 0.03f, "lowering for reload does not fly the shield toward the camera (closest depth vs held / stow - 3 cm)",
              g_shm.lowerMinZ, nearZ - 0.03f);
    }
    check(c[2].w == 0.f && c[2].phase == 0, "shield stays down during reload", c[2].w, c[2].phase);
    check(c[3].w >= 1.f && c[3].phase == 2, "re-raised by itself after reload", c[3].w, c[3].phase);
    check(c[4].stance == 2 && c[4].w == 0.f, "sprint lowers the shield", c[4].stance, c[4].w);
    check(c[5].stance == 2, "second Shift tap does not enter tactical sprint while shield wanted", c[5].stance, 2);
    check(c[6].w >= 1.f && c[6].stance == 0, "re-raised after sprint ended", c[6].w, c[6].stance);
    check(c[7].blocked - c[6].blocked == 5, "5 hits while raised -> all 5 blocked (shake)", c[7].blocked - c[6].blocked, 5);
    check(c[7].phase == 2 && c[7].w >= 1.f && c[7].lookDraw == 0, "no durability: still up and drawn after 5 hits", c[7].phase, c[7].lookDraw);
    check(g_shm.maxSparks > 0, "hit sparks spawned", g_shm.maxSparks, 0);
    {
        float lo = 1.f, hi = 0.f;
        for (int i = c[6].blocked; i < c[7].blocked && i < 16; ++i) lo = std::min(lo, g_shm.blockFrac[i]), hi = std::max(hi, g_shm.blockFrac[i]);
        check(lo == 0.5f && hi == 0.5f, "default (aim first): each of the 5 hits blocked 50%", lo, hi);
    }
    check(c[0].block == 0.5f && c[11].block == 0.f, "block fraction: 0.5 with shield up, 0 before Q", c[0].block, c[11].block);
    check(c[12].adsT > 0.99f && c[12].blocked - c[7].blocked == 1 && c[7].blocked < 16 && g_shm.blockFrac[c[7].blocked] == 1.f &&
              c[12].block == 1.f,
          "right mouse held (shield first): hit blocked 100%", c[7].blocked < 16 ? g_shm.blockFrac[c[7].blocked] : -1.f, c[12].block);
    check(c[12].ik >= 0.f && c[12].ik < 10.f, "left wrist reached the grip with right mouse held (IK residual mm)", c[12].ik, 10.0);
    check((c[0].fore < 0.f || c[0].fore < 2.f) && (c[12].fore < 0.f || c[12].fore < 2.f), "left forearm follows the REF grip (deg, default / right mouse)",
          c[0].fore, c[12].fore);
    check((c[0].shift < 0.f || c[0].shift < 30.f) && (c[12].shift < 0.f || c[12].shift < 30.f), "shield moved < 30 mm to close the arm (default / right mouse)",
          c[0].shift, c[12].shift);
    check(c[8].w == 0.f && c[8].lookDraw == -1 && c[8].blocked == c[12].blocked && c[8].block == 0.f, "hit while lowered is not blocked (0%)",
          c[8].blocked - c[12].blocked, c[8].block);
    check(c[9].w >= 1.f && c[9].phase == 2 && c[9].lookDraw == 0, "raised again after the hits", c[9].w, c[9].phase);
    check(c[10].allowed == 0 && c[10].w == 0.f && c[10].model == 1, "minigun cannot carry the shield", c[10].allowed, c[10].w);
    check(shotsOk == shotsWanted, "screenshots saved", shotsOk, shotsWanted);
    return g_shieldFails == 0;
}
struct StanceMon {
    int cur = -2, stance = -1;
    double curStart = -1;
    int rk = -1;
    double tR[2] = {-1, -1}, tExit[2] = {-1, -1}, tClip[2] = {-1, -1};
    int stanceAtR[2] = {-1, -1}, prevAtClip[2] = {-2, -2};
    bool armMini = false;
    double tStanceSprint = -1, tSprintClip = -1;
    long spinAtSprint = -1;
    int spinDownInSprint = 0;
    int sk = -1, nsk = 0;
    double tSel[2] = {-1, -1}, tDrop[2] = {-1, -1}, dropFade[2] = {-1, -1}, swUntil[2] = {-1, -1}, tDraw[2] = {-1, -1};
    int stanceAtSel[2] = {-1, -1}, fromModel[2] = {-1, -1}, dropClip[2] = {-1, -1}, interrupts[2] = {0, 0};
    int ak = -1;
    double tAds[6] = {-1, -1, -1, -1, -1, -1}, tExitA[6] = {-1, -1, -1, -1, -1, -1}, tAdsUp[6] = {-1, -1, -1, -1, -1, -1},
           adsUpFade[6] = {-1, -1, -1, -1, -1, -1}, tAdsW[6] = {-1, -1, -1, -1, -1, -1}, tGameAds[6] = {-1, -1, -1, -1, -1, -1},
           tAdsIdle[6] = {-1, -1, -1, -1, -1, -1}, adsUpFadeStart[6] = {-1, -1, -1, -1, -1, -1}, adsTimeAt[6] = {-1, -1, -1, -1, -1, -1};
    int stanceAtAds[6] = {-1, -1, -1, -1, -1, -1}, prevAtAdsUp[6] = {-2, -2, -2, -2, -2, -2}, sprintClipAt[6] = {-2, -2, -2, -2, -2, -2},
        idleClipAt[6] = {-2, -2, -2, -2, -2, -2};
    double tZoom[6] = {-1, -1, -1, -1, -1, -1};
    float prevAdsW = 0.f;
} g_sm;
const char* ClipTag(int c) {
    static const char* names[] = {"idle", "draw", "drop", "fire1", "fire2", "fire3", "reload", "reload_empty", "sprint", "tac_sprint", "jog",
                                  "sprint_in", "sprint_out", "ads_up", "ads_idle", "ads_down", "spin_up", "fire_loop", "spin_down", "ads_spin_up",
                                  "ads_fire_loop", "ads_spin_down", "inspect"};
    if (c < 0) return "-";
    for (const char* n : names)
        if (vmgame::Wp().rend.FindClip(n) == c) return n;
    return "?";
}
void StanceMonitor(double now, double t0) {
    using namespace vmgame;
    int cur = g_an.cur, st = g_an.stance;
    auto is = [&](const char* n) { return cur >= 0 && cur == Wp().rend.FindClip(n); };
    if (st != g_sm.stance) {
        BLog("STANCE t=%.3f %d -> %d (spin %ld)", now - t0, g_sm.stance, st, static_cast<long>(g_spin.state));
        if (g_sm.rk >= 0 && g_sm.stance >= 2 && st < 2 && g_sm.tExit[g_sm.rk] < 0) g_sm.tExit[g_sm.rk] = now;
        if (g_sm.armMini && st >= 2 && g_sm.tStanceSprint < 0) g_sm.tStanceSprint = now, g_sm.spinAtSprint = g_spin.state;
        g_sm.stance = st;
    }
    if (cur != g_sm.cur || g_an.curStart != g_sm.curStart) {
        BLog("ANIM t=%.3f %s -> %s (fade %.2f s, stance %d, spin %ld, reload %ld)", now - t0, ClipTag(g_sm.cur), ClipTag(cur), g_an.fadeLen, st,
             static_cast<long>(g_spin.state), static_cast<long>(g_mag.reload));
        g_sm.cur = cur;
        g_sm.curStart = g_an.curStart;
        if (g_sm.rk >= 0 && g_sm.tClip[g_sm.rk] < 0 && (is("reload") || is("reload_empty")))
            g_sm.tClip[g_sm.rk] = now, g_sm.prevAtClip[g_sm.rk] = g_an.prev;
    }
    if (g_sm.sk >= 0) {
        int k = g_sm.sk;
        if (g_sm.tDrop[k] < 0 && is("drop"))
            g_sm.tDrop[k] = now, g_sm.dropFade[k] = g_an.fadeLen, g_sm.swUntil[k] = g_sw.until, g_sm.fromModel[k] = g_modelIdx, g_sm.dropClip[k] = cur;
        else if (g_sm.tDrop[k] >= 0 && g_sm.tDraw[k] < 0) {
            if (g_modelIdx != g_sm.fromModel[k] && is("draw")) g_sm.tDraw[k] = now;
            else if (g_modelIdx == g_sm.fromModel[k] && cur != g_sm.dropClip[k]) ++g_sm.interrupts[k];
        }
    }
    if (g_sm.ak >= 0) {
        int k = g_sm.ak;
        if (g_sm.tExitA[k] < 0 && st < 2) g_sm.tExitA[k] = now;
        if (g_sm.tAdsUp[k] < 0 && is("ads_up"))
            g_sm.tAdsUp[k] = now, g_sm.adsUpFade[k] = g_an.fadeLen, g_sm.adsUpFadeStart[k] = g_an.fadeStart, g_sm.prevAtAdsUp[k] = g_an.prev;
        if (g_sm.tAdsUp[k] >= 0 && g_sm.tAdsIdle[k] < 0 && is("ads_idle")) g_sm.tAdsIdle[k] = now;
        if (g_sm.tGameAds[k] >= 0 && g_sm.tZoom[k] < 0 && P.adsT >= 1.f) g_sm.tZoom[k] = now;
        if (g_sm.tAdsW[k] < 0 && g_mo.adsW > g_sm.prevAdsW + 1e-6f) g_sm.tAdsW[k] = now;
        if (g_sm.tGameAds[k] < 0 && (g_stance & 1)) g_sm.tGameAds[k] = now;
    }
    g_sm.prevAdsW = g_mo.adsW;
    if (g_sm.armMini && st >= 2) {
        if (g_sm.tSprintClip < 0 && (is("sprint") || is("tac_sprint"))) g_sm.tSprintClip = now;
        if (is("spin_down") || is("ads_spin_down")) ++g_sm.spinDownInSprint;
    }
}
int g_stanceFails = 0;
bool EvaluateSprintAds() {
    g_stanceFails = 0;
    auto fail = [](const char* m, double a, double b) {
        BLog("BENCH SPRINTADS FAIL: %s (%.4f / %.4f)", m, a, b);
        ++g_stanceFails;
    };
    const char* what[6] = {"M4 sprint", "M4 tac sprint", "M4 standing", "minigun sprint", "minigun tac sprint", "minigun standing"};
    const double gate[6] = {0, 0, 0, 0, 0, 0};
    const double exitFade[6] = {0.2, 0.3, 0.06, 0.2, 0.3, 0.06};
    const int wantStance[6] = {2, 3, 0, 2, 3, 0};
    for (int k = 0; k < 6; ++k) {
        const StanceMon& m = g_sm;
        double adsT = m.adsTimeAt[k];
        BLog("BENCH SPRINTADS %s: ADS at stance %d | left sprint +%.3f s, game ADS +%.3f s (gate %.2f), adsW rises +%.3f s, ads_up starts +%.3f s "
             "(fade %.2f s from %s, started at +%.3f s), ads_idle +%.3f s / zoom done +%.3f s (= gate + ads_time %.2f -> %.2f)",
             what[k], m.stanceAtAds[k], m.tExitA[k] - m.tAds[k], m.tGameAds[k] - m.tAds[k], gate[k], m.tAdsW[k] - m.tAds[k], m.tAdsUp[k] - m.tAds[k],
             m.adsUpFade[k], ClipTag(m.prevAtAdsUp[k]), m.adsUpFadeStart[k] - m.tAds[k], m.tAdsIdle[k] - m.tAds[k], m.tZoom[k] - m.tAds[k], adsT,
             gate[k] + adsT);
        if (m.stanceAtAds[k] != wantStance[k]) fail("right button was not pressed in the intended stance", m.stanceAtAds[k], wantStance[k]);
        if (m.tGameAds[k] < 0 || m.tAdsW[k] < 0 || m.tAdsUp[k] < 0 || m.tAdsIdle[k] < 0 || (k < 2 && m.tExitA[k] < 0)) {
            fail("sprint exit / game ADS / adsW / ads_up / ads_idle never seen", m.tGameAds[k], m.tAdsIdle[k]);
            continue;
        }
        double t0 = k < 2 ? m.tExitA[k] : m.tAds[k];
        if (k < 2 && m.tExitA[k] - m.tAds[k] > 0.02) fail("did not leave sprint right away", m.tExitA[k] - m.tAds[k], 0.02);
        double wait = m.tGameAds[k] - t0;
        if (wait < gate[k] - 1e-6 || wait > gate[k] + 0.03) fail("ADS did not wait for the gate", wait, gate[k]);
        if (m.tAdsW[k] < m.tGameAds[k] - 1e-9 || m.tAdsUp[k] < m.tGameAds[k] - 1e-9)
            fail("sight alignment or ads_up started before the game entered ADS", m.tAdsW[k] - m.tGameAds[k], m.tAdsUp[k] - m.tGameAds[k]);
        if (m.tAdsUp[k] - m.tGameAds[k] > 0.02) fail("ads_up started late", m.tAdsUp[k] - m.tGameAds[k], 0.02);
        if (wantStance[k] < 2) {
            if (std::fabs(m.adsUpFade[k] - 0.06) > 1e-6 || m.prevAtAdsUp[k] != m.idleClipAt[k])
                fail("standing ADS should come in from idle (0.06 s)", m.adsUpFade[k], 0.06);
        } else if (m.prevAtAdsUp[k] == m.sprintClipAt[k]) {
            if (std::fabs(m.adsUpFadeStart[k] - m.tExitA[k]) > 0.002 || std::fabs(m.adsUpFade[k] - exitFade[k]) > 1e-6)
                fail("ads_up did not continue the sprint-exit fade", m.adsUpFadeStart[k] - m.tExitA[k], m.adsUpFade[k]);
        } else {
            if (m.prevAtAdsUp[k] != m.idleClipAt[k] || std::fabs(m.adsUpFade[k] - 0.06) > 1e-6 || m.tAdsUp[k] - m.tExitA[k] < exitFade[k] - 0.002)
                fail("ads_up should start from idle after the sprint-exit fade", m.tAdsUp[k] - m.tExitA[k], exitFade[k]);
        }
        if (m.tZoom[k] < 0 || std::fabs(m.tZoom[k] - m.tAdsIdle[k]) > 0.03)
            fail("camera zoom and ADS animation did not finish together", m.tZoom[k] - t0, m.tAdsIdle[k] - t0);
        double total = m.tAdsIdle[k] - t0, want = gate[k] + adsT;
        if (total < want - 1e-6 || total > want + 0.03) fail("ADS animation total time", total, want);
    }
    if (vmgame::g_failed) fail("viewmodel layer failed", 1, 0);
    return g_stanceFails == 0;
}
bool EvaluateSwap() {
    g_stanceFails = 0;
    auto fail = [](const char* m, double a, double b) {
        BLog("BENCH SWAP FAIL: %s (%.4f / %.4f)", m, a, b);
        ++g_stanceFails;
    };
    const char* what[2] = {"sprint", "tac sprint"};
    const double fade[2] = {0.2, 0.3};
    for (int k = 0; k < 2; ++k) {
        BLog("BENCH SWAP in %s: select at stance %d, drop started %.3f s later (fade %.2f s, want %.2f), %d frames where something else replaced "
             "the drop, new weapon draw %.3f s after drop start (switch time %.3f s)",
             what[k], g_sm.stanceAtSel[k], g_sm.tDrop[k] - g_sm.tSel[k], g_sm.dropFade[k], fade[k], g_sm.interrupts[k], g_sm.tDraw[k] - g_sm.tDrop[k],
             g_sm.swUntil[k] - g_sm.tDrop[k]);
        if (g_sm.stanceAtSel[k] != k + 2) fail("weapon was not selected in the intended sprint stance", g_sm.stanceAtSel[k], k + 2);
        if (g_sm.tDrop[k] < 0 || g_sm.tDraw[k] < 0) {
            fail("drop or new draw never seen", g_sm.tDrop[k], g_sm.tDraw[k]);
            continue;
        }
        if (g_sm.tDrop[k] - g_sm.tSel[k] > 0.05) fail("drop started late", g_sm.tDrop[k] - g_sm.tSel[k], 0.05);
        if (std::fabs(g_sm.dropFade[k] - fade[k]) > 1e-6) fail("drop fade-in from sprint", g_sm.dropFade[k], fade[k]);
        if (g_sm.interrupts[k]) fail("drop was replaced before the switch (the old freeze)", g_sm.interrupts[k], 0);
        if (g_sm.tDraw[k] < g_sm.swUntil[k] - 1e-9 || g_sm.tDraw[k] > g_sm.swUntil[k] + 0.05)
            fail("new weapon draw not at the end of the drop", g_sm.tDraw[k] - g_sm.tDrop[k], g_sm.swUntil[k] - g_sm.tDrop[k]);
    }
    if (vmgame::g_failed) fail("viewmodel layer failed", 1, 0);
    return g_stanceFails == 0;
}
bool EvaluateStance() {
    using namespace vmgame;
    g_stanceFails = 0;
    auto fail = [](const char* m, double a, double b) {
        BLog("BENCH STANCE FAIL: %s (%.4f / %.4f)", m, a, b);
        ++g_stanceFails;
    };
    const char* what[2] = {"sprint", "tac sprint"};
    const double fade[2] = {0.2, 0.3};
    int idle = Wp().rend.FindClip("idle");
    for (int k = 0; k < 2; ++k) {
        double delay = g_sm.tClip[k] - g_sm.tR[k], afterExit = g_sm.tClip[k] - g_sm.tExit[k];
        BLog("BENCH STANCE reload in %s: R at stance %d, left sprint %.3f s later, reload clip %.3f s after R (%.3f s after leaving sprint, "
             "fade %.2f), blended in from %s",
             what[k], g_sm.stanceAtR[k], g_sm.tExit[k] - g_sm.tR[k], delay, afterExit, fade[k], ClipTag(g_sm.prevAtClip[k]));
        if (g_sm.stanceAtR[k] != k + 2) fail(k ? "tac reload: R was not pressed in tac sprint" : "reload: R was not pressed in sprint", g_sm.stanceAtR[k], k + 2);
        if (g_sm.tClip[k] < 0 || g_sm.tExit[k] < 0) fail("reload clip or sprint exit never seen", g_sm.tClip[k], g_sm.tExit[k]);
        else {
            if (afterExit < fade[k] - 1e-6) fail("reload started before the gun faded back to the hip pose", afterExit, fade[k]);
            if (delay > fade[k] + 0.1) fail("reload started too late", delay, fade[k] + 0.1);
            if (g_sm.prevAtClip[k] != idle) fail("reload did not blend in from idle", g_sm.prevAtClip[k], idle);
        }
    }
    BLog("BENCH STANCE minigun sprint during spin-down: spin state %ld when the stance became sprint, sprint clip %.3f s later, spin_down frames "
         "while sprinting %d",
         g_sm.spinAtSprint, g_sm.tSprintClip - g_sm.tStanceSprint, g_sm.spinDownInSprint);
    if (g_sm.tStanceSprint < 0 || g_sm.tSprintClip < 0) fail("minigun: sprint stance / clip never seen", g_sm.tStanceSprint, g_sm.tSprintClip);
    else if (g_sm.tSprintClip - g_sm.tStanceSprint > 1e-9) fail("minigun: sprint clip did not start in the same frame", g_sm.tSprintClip - g_sm.tStanceSprint, 0);
    if (g_sm.spinAtSprint != 3) fail("minigun: barrels were not spinning down when sprint started (test did not cover the case)", g_sm.spinAtSprint, 3);
    if (g_sm.spinDownInSprint) fail("minigun: spin_down clip played while sprinting", g_sm.spinDownInSprint, 0);
    if (g_failed) fail("viewmodel layer failed", 1, 0);
    return g_stanceFails == 0;
}
void PrintState(const char* tag) {
    using namespace vmgame;
    BLog("STATE %-6s model=%d (%ls) ammo=%ld/%ld reload=%ld switching=%ld pitch=%.2f yaw=%.2f adsT=%.2f holes=%d gunKick=%.4f,%.4f,%.4f m / "
         "%.2f,%.2f,%.2f deg failed=%d draws=%u",
         tag, g_modelIdx, ModelName(g_modelIdx), g_mag.ammo, g_mag.size, g_mag.reload, g_switching, P.pitch / kick::kDeg,
         P.yaw / kick::kDeg, P.adsT, g_holeN, g_kickOut.gunPos[0], g_kickOut.gunPos[1], g_kickOut.gunPos[2], g_kickOut.gunRot[0],
         g_kickOut.gunRot[1], g_kickOut.gunRot[2], g_failed ? 1 : 0, g_st.draws);
}
struct LayerRun {
    bool layers[kLayers];
    const wchar_t* label;
    int fpsMode;
    int mode;
};
const LayerRun kLayerRuns[] = {
    {{true, true, true, true, true, true, true, true, true}, L"自测 全开", 2, kRpHip},
    {{true, true, false, true, true, true, true, true, true}, L"自测 关枪身后坐", 2, kRpHip},
    {{false, true, false, false, false, false, false, false, true}, L"自测 只开镜头踢动", 2, kRpHip},
    {{true, true, true, true, true, false, true, true, true}, L"自测 关连射持续曲线", 2, kRpHip},
    {{true, true, true, true, true, true, false, true, true}, L"自测 关双手跟枪", 2, kRpHip},
    {{true, true, true, true, true, true, true, false, true}, L"自测 关两段回稳抖动", 2, kRpHip},
    {{true, true, true, true, true, true, true, true, false}, L"自测 旧混合 腰射", 2, kRpHip},
    {{true, true, true, true, true, true, true, true, true}, L"自测 切换 新混合", 2, kRpSwitch},
    {{true, true, true, true, true, true, true, true, false}, L"自测 切换 旧混合", 2, kRpSwitch},
    {{true, true, true, true, true, true, true, true, true}, L"自测 全开（再一次）", 2, kRpHip},
    {{true, true, true, true, true, true, true, true, true}, L"自测 全开 30 帧", 1, kRpHip},
    {{true, true, true, true, true, true, true, true, true}, L"自测 全开 120 帧", 3, kRpHip},
    {{true, true, true, true, true, true, true, true, true}, L"自测 全开 举镜", 2, kRpAds},
};
enum { kRunA, kRunGunOff, kRunCamOnly, kRunSusOff, kRunIkOff, kRunJitOff, kRunBlendOffHip, kRunSwNew, kRunSwOld, kRunB, kRun30, kRun120, kRunAds };
constexpr int kLayerRunN = sizeof kLayerRuns / sizeof *kLayerRuns, kLayerRunsCounted = kRunB + 1;
struct LayerResult {
    bool done;
    RecSum sum;
    int sndSubmits, gunHits, camHits;
    double stepMs;
};
LayerResult g_lr[kLayerRunN] = {};
Rec g_recSw;
struct SwitchCmp {
    bool done;
    int preN, preDiff, postN, postDiff;
    double adsOn, adsOff;
    double pitch[2][3], back[2][3];
} g_swCmp = {};
void CompareSwitch(const Rec& n, const Rec& o, double adsOn, double adsOff) {
    SwitchCmp& c = g_swCmp;
    c = {};
    c.adsOn = adsOn, c.adsOff = adsOff;
    const double win[4] = {adsOn, adsOn + 0.2, adsOff, adsOff + 0.3};
    for (int m = 0; m < 2; ++m) {
        const Rec& R = m ? o : n;
        for (int i = 0; i < R.nSamp; ++i) {
            const RecSamp& x = R.samps[i];
            for (int w = 0; w < 3; ++w)
                if (x.t >= win[w] && x.t < win[w + 1]) {
                    if (std::fabs(x.pitch) > c.pitch[m][w]) c.pitch[m][w] = std::fabs(x.pitch);
                    if (x.back > c.back[m][w]) c.back[m][w] = x.back;
                }
        }
    }
    if (n.nSamp != o.nSamp) {
        c.preN = -1;
        c.done = true;
        return;
    }
    for (int i = 0; i < n.nSamp; ++i) {
        const RecSamp &x = n.samps[i], &y = o.samps[i];
        bool same = x.t == y.t && !memcmp(&x.back, &y.back, 4) && !memcmp(&x.pitch, &y.pitch, 4) && !memcmp(&x.roll, &y.roll, 4) &&
                    !memcmp(&x.kBack, &y.kBack, 4) && !memcmp(&x.kPitch, &y.kPitch, 4);
        if (x.t < adsOn) ++c.preN, c.preDiff += !same;
        else ++c.postN, c.postDiff += !same;
    }
    c.done = true;
}
int g_blendHipDiff = -2, g_blendHipItems = 0;
int CompareRec(const Rec& a, const Rec& b, int* items) {
    *items = 0;
    if (a.nShot != b.nShot || a.nSamp != b.nSamp) return -1;
    int diff = 0;
    auto cmp = [&](const void* x, const void* y, size_t n) {
        ++*items;
        if (memcmp(x, y, n)) ++diff;
    };
    for (int i = 0; i < a.nShot; ++i) {
        const ShotRec &x = a.shots[i], &y = b.shots[i];
        cmp(&x.tFire, &y.tFire, 8), cmp(&x.tDue, &y.tDue, 8), cmp(&x.gunAt, &y.gunAt, 8), cmp(&x.camAt, &y.camAt, 8), cmp(&x.sndAt, &y.sndAt, 8);
        cmp(x.hit, y.hit, sizeof x.hit), cmp(&x.what, &y.what, sizeof x.what), cmp(&x.aimPitch, &y.aimPitch, 4), cmp(&x.aimYaw, &y.aimYaw, 4);
        cmp(&x.climb, &y.climb, 4), cmp(&x.punch, &y.punch, 4), cmp(&x.gunBack, &y.gunBack, 4), cmp(&x.gunPitch, &y.gunPitch, 4);
        cmp(&x.ammoAfter, &y.ammoAfter, sizeof x.ammoAfter);
    }
    for (int i = 0; i < a.nSamp; ++i) {
        const RecSamp &x = a.samps[i], &y = b.samps[i];
        cmp(&x.t, &y.t, 8), cmp(&x.climb, &y.climb, 4), cmp(&x.punch, &y.punch, 4), cmp(&x.total, &y.total, 4), cmp(&x.back, &y.back, 4);
        cmp(&x.pitch, &y.pitch, 4), cmp(&x.roll, &y.roll, 4), cmp(&x.kBack, &y.kBack, 4), cmp(&x.kPitch, &y.kPitch, 4);
    }
    return diff;
}
int g_layerFails = 0;
void LayerFail(const char* fmt, ...) {
    char b[700];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    BLog("BENCH LAYERS FAIL: %s", b);
    ++g_layerFails;
}
bool EvaluateLayers(int recDiff, int recItems) {
    using namespace vmgame;
    char line[1400];
    for (int k = 0; k < kLayerRunN; ++k) {
        if (!g_lr[k].done) continue;
        SumLine(g_lr[k].sum, line, sizeof line);
        BLog("BENCH LAYERS run %d%s: %s", k + 1, k < kLayerRunsCounted ? "" : " (extra)", line);
    }
    g_layerFails = 0;
    for (int k = 0; k < kLayerRunsCounted; ++k)
        if (!g_lr[k].done || g_lr[k].sum.shots < 10) {
            char l8[96];
            Utf8(kLayerRuns[k].label, l8, sizeof l8);
            LayerFail("run %d (%s) did not finish with >= 10 shots", k + 1, l8);
        }
    if (g_layerFails) return false;
    const LayerResult &A = g_lr[kRunA], &G = g_lr[kRunGunOff], &C = g_lr[kRunCamOnly], &S = g_lr[kRunSusOff], &H = g_lr[kRunIkOff],
                      &J = g_lr[kRunJitOff], &B = g_lr[kRunB];
    wchar_t names[300];
    int d = CompareSum(A.sum, B.sum, names, 300);
    if (d) {
        char n8[700];
        Utf8(names, n8, sizeof n8);
        LayerFail("full-on #1 vs #2: %d of %d summary items differ: %s", d, kCompareItems, n8);
    }
    if (recDiff != 0) LayerFail("full-on #1 vs #2: per-shot / per-frame data differ (%d of %d items; -1 = shot or sample count differs)", recDiff, recItems);
    if (!d && !recDiff)
        BLog("BENCH LAYERS full-on #1 == #2: %d summary items + %d per-shot / per-frame items bit-identical (%d shots, hash %016llx)", kCompareItems,
             recItems, A.sum.shots, static_cast<unsigned long long>(A.sum.hAll));
    if (!(A.sum.climbPeak > 1.0 && A.sum.backPeak > 0.3 && A.sum.pitchPeak > 0.1))
        LayerFail("full-on: kick too small (climb %.3f deg, back %.3f cm, pitch %.3f deg)", A.sum.climbPeak, A.sum.backPeak, A.sum.pitchPeak);
    if (A.sum.tracers != A.sum.shots) LayerFail("full-on: %d tracers for %d shots", A.sum.tracers, A.sum.shots);
    if (A.gunHits != A.sum.shots || A.sum.gunFirst != 0.0)
        LayerFail("full-on: gun spring should take every shot in the same frame (%d/%d, %.3f ms)", A.gunHits, A.sum.shots, A.sum.gunFirst);
    if (A.camHits != A.sum.shots || std::fabs(A.sum.camFirst - A.stepMs) > 1e-9)
        LayerFail("full-on: camera should show every shot one frame later (%d/%d, %.4f vs %.4f ms)", A.camHits, A.sum.shots, A.sum.camFirst, A.stepMs);
    bool sndAvail = FireLayer() != nullptr;
    if (sndAvail && A.sndSubmits != A.sum.shots) LayerFail("full-on: fire sound submitted for %d of %d shots", A.sndSubmits, A.sum.shots);
    kick::Params kp;
    GetKick(&kp);
    bool susCfg = kp.enabled && kp.sustainScale > 0.f && kp.gunScale > 0.f;
    if (susCfg && !(A.sum.susBackPeak > 0.05)) LayerFail("full-on: sustain curve did not move (back %.4f cm)", A.sum.susBackPeak);
    bool jitCfg = kp.enabled && kp.jitterScale > 0.f && kp.gunScale > 0.f;
    if (jitCfg && !(A.sum.jitPitchPeak > 0.02 && A.sum.jitRollPeak > 0.02))
        LayerFail("full-on: two-stage jitter did not move (pitch %.4f, roll %.4f deg)", A.sum.jitPitchPeak, A.sum.jitRollPeak);
    bool ikCfg = Wp().set.handIk && Wp().rend.ArmRig() && !Wp().set.mirrorX;
    if (ikCfg) {
        if (A.sum.ikFrames <= 0) LayerFail("full-on: hand IK never ran");
        else if (A.sum.ikResMax > 1.0) LayerFail("full-on: hand IK residual %.2f mm > 1 mm", A.sum.ikResMax);
        else if (!(A.sum.ikShiftPeak[0] > 1.0)) LayerFail("full-on: right wrist did not follow the gun (%.2f mm)", A.sum.ikShiftPeak[0]);
    }
    if (G.sum.backPeak != 0.0 || G.sum.pitchPeak != 0.0 || G.sum.rollPeak != 0.0 || G.sum.backPeakT != 0.0 || G.sum.susBackPeak != 0.0 || G.gunHits)
        LayerFail("gun off: gun moved (back %.4f pitch %.4f roll %.4f, sustain %.4f, %d hits)", G.sum.backPeak, G.sum.pitchPeak, G.sum.rollPeak,
                  G.sum.susBackPeak, G.gunHits);
    if (G.sum.hHoles != A.sum.hHoles || G.sum.hCam != A.sum.hCam || G.sum.shots != A.sum.shots || G.sum.ammoUsed != A.sum.ammoUsed)
        LayerFail("gun off: holes / camera / shots / ammo differ from full-on");
    if (C.sum.backPeak != 0.0 || C.sum.pitchPeak != 0.0 || C.sum.susBackPeak != 0.0 || C.gunHits)
        LayerFail("cam only: gun moved (back %.4f pitch %.4f sustain %.4f)", C.sum.backPeak, C.sum.pitchPeak, C.sum.susBackPeak);
    if (C.sndSubmits || C.sum.tracers || C.sum.ikFrames)
        LayerFail("cam only: %d sound submits, %d tracers, %d hand IK frames (those layers are off)", C.sndSubmits, C.sum.tracers, C.sum.ikFrames);
    if (C.sum.hHoles != A.sum.hHoles || C.sum.hCam != A.sum.hCam || C.sum.shots != A.sum.shots || C.sum.ammoUsed != A.sum.ammoUsed)
        LayerFail("cam only: holes / camera / shots / ammo differ from full-on");
    if (S.sum.susBackPeak != 0.0 || S.sum.susPitchPeak != 0.0)
        LayerFail("sustain off: sustain moved (back %.4f cm, pitch %.4f deg)", S.sum.susBackPeak, S.sum.susPitchPeak);
    if (S.sum.hSpring != A.sum.hSpring || S.sum.hHoles != A.sum.hHoles || S.sum.hCam != A.sum.hCam || S.sum.shots != A.sum.shots)
        LayerFail("sustain off: gun spring / holes / camera / shots differ from full-on");
    if (J.sum.jitPitchPeak != 0.0 || J.sum.jitRollPeak != 0.0)
        LayerFail("jitter off: jitter moved (pitch %.4f, roll %.4f deg)", J.sum.jitPitchPeak, J.sum.jitRollPeak);
    if (J.sum.hSpring != A.sum.hSpring || J.sum.susBackPeak != A.sum.susBackPeak || J.sum.susPitchPeak != A.sum.susPitchPeak ||
        J.sum.hHoles != A.sum.hHoles || J.sum.hCam != A.sum.hCam || J.sum.shots != A.sum.shots)
        LayerFail("jitter off: gun spring / sustain / holes / camera / shots differ from full-on");
    if (H.sum.ikFrames) LayerFail("hand IK off: IK still ran in %d frames", H.sum.ikFrames);
    if (int d2 = CompareSum(A.sum, H.sum, names, 300)) {
        char n8[700];
        Utf8(names, n8, sizeof n8);
        LayerFail("hand IK off: %d summary items differ from full-on (it should only change the picture): %s", d2, n8);
    }
    const LayerResult &BH = g_lr[kRunBlendOffHip], &SN = g_lr[kRunSwNew], &SO = g_lr[kRunSwOld];
    if (int d3 = CompareSum(A.sum, BH.sum, names, 300)) {
        char n8[700];
        Utf8(names, n8, sizeof n8);
        LayerFail("blend old (hip replay): %d summary items differ from full-on (hip fire never blends): %s", d3, n8);
    }
    if (g_blendHipDiff != 0)
        LayerFail("blend old (hip replay): per-shot / per-frame data differ from full-on (%d of %d items)", g_blendHipDiff, g_blendHipItems);
    if (SN.sum.hHoles != SO.sum.hHoles || SN.sum.hCam != SO.sum.hCam || SN.sum.shots != SO.sum.shots || SN.sum.ammoUsed != SO.sum.ammoUsed)
        LayerFail("switch replay new vs old blend: holes / camera / shots / ammo differ (blend should only change the gun picture)");
    if (SN.sum.jitPitchPeak != SO.sum.jitPitchPeak || SN.sum.jitRollPeak != SO.sum.jitRollPeak)
        LayerFail("switch replay new vs old blend: jitter differs (%.4f vs %.4f deg) - it is not part of the blend", SN.sum.jitPitchPeak,
                  SO.sum.jitPitchPeak);
    const SwitchCmp& sc = g_swCmp;
    if (!sc.done || sc.preN < 10 || sc.preDiff != 0)
        LayerFail("switch replay new vs old blend: gun frames before the ADS press differ (%d of %d frames%s)", sc.preDiff, sc.preN,
                  sc.preN < 0 ? ", sample counts differ" : "");
    if (sc.done && sc.postDiff == 0) LayerFail("switch replay new vs old blend: gun frames after the ADS press are identical (blend did nothing)");
    for (int k = 0; k < kLayerRunN; ++k)
        if (g_lr[k].done && g_lr[k].sum.swayMismatch)
            LayerFail("run %d: sway-off prediction did not match ComputeLook in %d frames", k + 1, g_lr[k].sum.swayMismatch);
    if (g_failed) LayerFail("viewmodel layer failed");
    if (g_lr[kRun30].done && g_lr[kRun120].done) {
        const RecSum &a = g_lr[kRun30].sum, &b = A.sum, &c = g_lr[kRun120].sum;
        BLog("BENCH LAYERS 30/60/120 fps by time: shots %d/%d/%d | cam climb peak %.3f/%.3f/%.3f deg, punch %.3f/%.3f/%.3f | gun back %.3f/%.3f/%.3f cm, "
             "pitch %.3f/%.3f/%.3f deg | settle punch %.1f/%.1f/%.1f, back %.1f/%.1f/%.1f, pitch %.1f/%.1f/%.1f ms | sustain back %.3f/%.3f/%.3f cm",
             a.shots, b.shots, c.shots, a.climbPeak, b.climbPeak, c.climbPeak, a.punchPeakT, b.punchPeakT, c.punchPeakT, a.backPeakT, b.backPeakT,
             c.backPeakT, a.pitchPeakT, b.pitchPeakT, c.pitchPeakT, a.punchSettleT, b.punchSettleT, c.punchSettleT, a.backSettleT, b.backSettleT,
             c.backSettleT, a.pitchSettleT, b.pitchSettleT, c.pitchSettleT, a.susBackPeak, b.susBackPeak, c.susBackPeak);
        BLog("BENCH LAYERS 30/60/120 fps per frame (what the screen shows): punch %.3f/%.3f/%.3f deg, gun back %.3f/%.3f/%.3f cm, pitch %.3f/%.3f/%.3f deg | "
             "fire->cam %.2f/%.2f/%.2f ms, due lag avg %.2f/%.2f/%.2f ms",
             a.punchPeak, b.punchPeak, c.punchPeak, a.backPeak, b.backPeak, c.backPeak, a.pitchPeak, b.pitchPeak, c.pitchPeak, a.camFirst, b.camFirst,
             c.camFirst, a.dueAvg, b.dueAvg, c.dueAvg);
    }
    if (g_lr[kRunAds].done)
        BLog("BENCH LAYERS ads replay: climb peak %.3f deg (hip %.3f), gun back %.3f cm (hip %.3f), gun pitch %.3f deg (hip %.3f) by time, sustain back %.3f cm "
             "(hip %.3f), hand IK right wrist max %.1f mm (hip %.1f)",
             g_lr[kRunAds].sum.climbPeak, A.sum.climbPeak, g_lr[kRunAds].sum.backPeakT, A.sum.backPeakT, g_lr[kRunAds].sum.pitchPeakT, A.sum.pitchPeakT,
             g_lr[kRunAds].sum.susBackPeak, A.sum.susBackPeak, g_lr[kRunAds].sum.ikShiftPeak[0], A.sum.ikShiftPeak[0]);
    if (sc.done)
        BLog("BENCH LAYERS switch replay (ADS %.2f..%.2f s): gun frames before ADS identical %d/%d, after ADS differ %d/%d | gun pitch peak new/old: "
             "first 0.2 s of ADS %.3f/%.3f deg, in ADS %.3f/%.3f, first 0.3 s after %.3f/%.3f | back new/old %.3f/%.3f, %.3f/%.3f, %.3f/%.3f cm",
             sc.adsOn, sc.adsOff, sc.preN - sc.preDiff, sc.preN, sc.postDiff, sc.postN, sc.pitch[0][0], sc.pitch[1][0], sc.pitch[0][1], sc.pitch[1][1],
             sc.pitch[0][2], sc.pitch[1][2], sc.back[0][0], sc.back[1][0], sc.back[0][1], sc.back[1][1], sc.back[0][2], sc.back[1][2]);
    BLog("BENCH LAYERS full-on gun = spring + sustain: back peak %.3f cm (sustain part %.3f), pitch %.3f deg (sustain part %.3f) | hand IK %d frames, "
         "wrist max R %.1f / L %.1f mm, residual max %.2f mm",
         A.sum.backPeakT, A.sum.susBackPeak, A.sum.pitchPeakT, A.sum.susPitchPeak, A.sum.ikFrames, A.sum.ikShiftPeak[0], A.sum.ikShiftPeak[1],
         A.sum.ikResMax);
    BLog("BENCH LAYERS two-stage jitter: full-on pitch peak %.3f deg, roll peak %.3f deg (frame samples) | gun pitch peak by time %.3f deg with jitter, "
         "%.3f without (run \"jitter off\") | ADS %.3f / %.3f deg",
         A.sum.jitPitchPeak, A.sum.jitRollPeak, A.sum.pitchPeakT, J.sum.pitchPeakT, g_lr[kRunAds].sum.jitPitchPeak, g_lr[kRunAds].sum.jitRollPeak);
    if (A.sum.sndRealN)
        BLog("BENCH LAYERS fire->sound start (real clock, not compared): avg %.2f ms, min %.2f, max %.2f over %d shots", A.sum.sndRealAvg, A.sum.sndRealMin,
             A.sum.sndRealMax, A.sum.sndRealN);
    else
        BLog("BENCH LAYERS fire->sound start: not measured (%s)", sndAvail ? "probe saw no voice start" : "no fire sound loaded / no audio device");
    return g_layerFails == 0;
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace bench;
    setlocale(LC_ALL, ".UTF-8");
    setlocale(LC_NUMERIC, "C");
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    GetModuleFileNameW(nullptr, g_exeDir, MAX_PATH * 2);
    if (wchar_t* s = wcsrchr(g_exeDir, L'\\')) *s = 0;
    const wchar_t *dirArg = nullptr, *script = nullptr, *outDir = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--mute")) g_mute = true;
        else if (!wcscmp(argv[i], L"--script") && i + 2 < argc) script = argv[++i], outDir = argv[++i];
        else if (argv[i][0] != L'-') dirArg = argv[i];
        else {
            printf("usage: vm_bench [game dir: hero_fps.ini + hero_fps_vm\\ (or project root)] [--mute] [--script kick|idle|layers|stance|swap|sprintads|gas|shell|shellmove|shield|lastshot|onset|shake <out dir>]\n");
            return 1;
        }
    }
    if (script) g_mute = g_scriptMode = true;
    bool layersScript = script && !wcscmp(script, L"layers");
    g_scriptSealed = script && (layersScript || !wcscmp(script, L"kick") || !wcscmp(script, L"stance") || !wcscmp(script, L"swap") ||
                                !wcscmp(script, L"sprintads") || !wcscmp(script, L"gas") || !wcscmp(script, L"shell") ||
                                !wcscmp(script, L"shellmove") || !wcscmp(script, L"shield") || !wcscmp(script, L"lastshot") || !wcscmp(script, L"onset") ||
                                !wcscmp(script, L"shake") || !wcscmp(script, L"melee") || !wcscmp(script, L"rapid") || !wcscmp(script, L"crossfade") ||
                                !wcscmp(script, L"meleeblock"));
    g_csvOn = !script || layersScript;
    using FnPeriod = UINT(WINAPI*)(UINT);
    FnPeriod timeBegin = nullptr, timeEnd = nullptr;
    if (HMODULE mm = LoadLibraryW(L"winmm.dll")) {
        timeBegin = reinterpret_cast<FnPeriod>(reinterpret_cast<void*>(GetProcAddress(mm, "timeBeginPeriod")));
        timeEnd = reinterpret_cast<FnPeriod>(reinterpret_cast<void*>(GetProcAddress(mm, "timeEndPeriod")));
    }
    if (timeBegin) timeBegin(1);
    {
        HDC sdc = GetDC(nullptr);
        g_ui = sdc ? GetDeviceCaps(sdc, LOGPIXELSY) / 96.f : 1.f;
        if (sdc) ReleaseDC(nullptr, sdc);
        g_ui = g_ui < 1.f ? 1.f : g_ui > 4.f ? 4.f : g_ui;
        g_textW = Px(1240);
        g_lineH = Px(17);
        g_textH = kLines * g_lineH + Px(8);
    }
    {
        wchar_t p[MAX_PATH * 2];
        swprintf(p, MAX_PATH * 2, L"%ls\\vm_bench.log", g_exeDir);
        g_logFile = _wfopen(p, L"w");
    }
    wchar_t dir[MAX_PATH * 2];
    if (dirArg) {
        if (!GetFullPathNameW(dirArg, MAX_PATH * 2, dir, nullptr)) wcscpy(dir, dirArg);
    } else {
        swprintf(dir, MAX_PATH * 2, L"%ls\\..", g_exeDir);
        wchar_t full[MAX_PATH * 2];
        if (GetFullPathNameW(dir, MAX_PATH * 2, full, nullptr)) wcscpy(dir, full);
    }
    for (size_t n = wcslen(dir); n > 3 && (dir[n - 1] == L'\\' || dir[n - 1] == L'/'); --n) dir[n - 1] = 0;
    auto isDir = [](const wchar_t* p) {
        DWORD a = GetFileAttributesW(p);
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    };
    wchar_t probe[MAX_PATH * 2];
    swprintf(probe, MAX_PATH * 2, L"%ls\\hero_fps.ini", dir);
    if (GetFileAttributesW(probe) == INVALID_FILE_ATTRIBUTES) {
        BLog("bench: %ls not found (give a game dir with hero_fps.ini + hero_fps_vm\\, or the project root)", probe);
        return 2;
    }
    BLog("bench: game dir %ls%s", dir, g_mute ? " (muted)" : "");
    using namespace vmgame;
    vmgame::Setup(0, dir, BLog);
    g_staticOk = true;
    g_fp = true;
    InterlockedExchange(&g_reqRoster, 1);
    {
        wchar_t a[MAX_PATH * 2], b[MAX_PATH * 2];
        swprintf(a, MAX_PATH * 2, L"%ls\\hero_fps_vm", dir);
        swprintf(b, MAX_PATH * 2, L"%ls\\assets\\hero_fps_vm", dir);
        if (!isDir(a) && isDir(b)) {
            wcscat(a, L"\\");
            size_t la = wcslen(a);
            for (int i = 0; i < g_modelCount; ++i) {
                if (_wcsnicmp(g_models[i], a, la)) continue;
                wchar_t t[MAX_PATH * 2];
                swprintf(t, MAX_PATH * 2, L"%ls\\%ls", b, g_models[i] + la);
                wcscpy(g_models[i], t);
            }
            for (int i = 0; i < g_modelCount; ++i) LoadAllSettings(i, g_w[i].set);
            BLog("bench: models read from %ls", b);
        }
    }
    for (int i = 0; i < g_modelCount; ++i) BLog("bench: model %d = %ls", i + 1, g_models[i]);
    LoadCfg(g_ini);
    wcscpy(g_wMain.path, g_ini);
    g_wMain.t = FileTime(g_wMain.path);
    BuildRange();
    P.camKick.rng.Seed(script ? 1234u : GetTickCount() ^ 0x5bd1e995u);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"hfps_vm_bench";
    RegisterClassW(&wc);
    RECT r = {0, 0, Px(1280), Px(720)}, wa = {0, 0, 1920, 1080};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    if (ww > wa.right - wa.left) ww = wa.right - wa.left;
    if (wh > wa.bottom - wa.top) wh = wa.bottom - wa.top;
    G.hwnd = CreateWindowW(wc.lpszClassName, L"试枪台 vm_bench", WS_OVERLAPPEDWINDOW, wa.left + (wa.right - wa.left - ww) / 2,
                           wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, nullptr, nullptr, wc.hInstance, nullptr);
    if (!G.hwnd) {
        BLog("bench: CreateWindow failed (%lu)", GetLastError());
        return 2;
    }
    ShowWindow(G.hwnd, script ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
    ShowWindow(G.hwnd, script ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
    RAWINPUTDEVICE rid = {0x01, 0x02, 0, G.hwnd};
    if (!RegisterRawInputDevices(&rid, 1, sizeof rid)) BLog("bench: RegisterRawInputDevices failed (%lu)", GetLastError());
    if (!InitGfx() || !InitText()) {
        BLog("bench: graphics init failed");
        return 2;
    }
    BLog("bench: window %ux%u, %d boxes, %u static vertices", G.w, G.h, g_nBoxes, G.staticCount);
    g_refreshHz = QueryRefreshHz();
    probe::Start();
    BLog("bench: display %d Hz; layer keys F1-F7 + J (Shift = solo, 0 = all), Space = replay (Shift = ADS), L = frame rate", g_refreshHz);
    const Ev* ev = nullptr;
    size_t nev = 0, next = 0;
    if (script) {
        if (!wcscmp(script, L"kick")) ev = kEvKick, nev = sizeof kEvKick / sizeof *kEvKick;
        else if (!wcscmp(script, L"idle")) ev = kEvIdle, nev = sizeof kEvIdle / sizeof *kEvIdle;
        else if (!wcscmp(script, L"stance")) ev = kEvStance, nev = sizeof kEvStance / sizeof *kEvStance;
        else if (!wcscmp(script, L"swap")) ev = kEvSwap, nev = sizeof kEvSwap / sizeof *kEvSwap;
        else if (!wcscmp(script, L"sprintads")) ev = kEvSprintAds, nev = sizeof kEvSprintAds / sizeof *kEvSprintAds;
        else if (!wcscmp(script, L"gas")) ev = kEvGas, nev = sizeof kEvGas / sizeof *kEvGas;
        else if (!wcscmp(script, L"shell")) ev = kEvShell, nev = sizeof kEvShell / sizeof *kEvShell;
        else if (!wcscmp(script, L"shellmove")) ev = kEvShellMove, nev = sizeof kEvShellMove / sizeof *kEvShellMove;
        else if (!wcscmp(script, L"shield")) ev = kEvShield, nev = sizeof kEvShield / sizeof *kEvShield;
        else if (!wcscmp(script, L"lastshot")) ev = kEvLastShot, nev = sizeof kEvLastShot / sizeof *kEvLastShot;
        else if (!wcscmp(script, L"onset")) ev = kEvOnset, nev = sizeof kEvOnset / sizeof *kEvOnset;
        else if (!wcscmp(script, L"shake")) ev = kEvShake, nev = sizeof kEvShake / sizeof *kEvShake;
        else if (!wcscmp(script, L"melee")) ev = kEvMelee, nev = sizeof kEvMelee / sizeof *kEvMelee;
        else if (!wcscmp(script, L"meleeblock")) ev = kEvMeleeBlock, nev = sizeof kEvMeleeBlock / sizeof *kEvMeleeBlock;
        else if (!wcscmp(script, L"rapid")) ev = kEvRapid, nev = sizeof kEvRapid / sizeof *kEvRapid;
        else if (!wcscmp(script, L"crossfade")) ev = kEvCrossFade, nev = sizeof kEvCrossFade / sizeof *kEvCrossFade;
        else if (!layersScript) {
            BLog("bench: unknown script %ls (kick | idle | layers | stance | swap | sprintads | gas | shell | shellmove | shield | lastshot | onset | shake | "
                 "melee | rapid | crossfade | meleeblock)", script);
            return 1;
        }
        CreateDirectoryW(outDir, nullptr);
    }
    double tScript0 = -1, fireUntil = -1, tTap = -1;
    int fireTap = 0, taps = 0;
    int shellMax[vmgame::kMaxModels] = {};
    bool aHeld = false, dHeld = false;
    vmgame::FxStat lsPrev = {};
    int lsFails = 0, lsStages = 0;
    int movePhase = -1;
    float moveMinX[2] = {1e9f, 1e9f};
    bool adsHeld = false, wHeld = false, stanceOk = false;
    int shiftTap = 0;
    bool stanceScript = script && (!wcscmp(script, L"stance") || !wcscmp(script, L"swap") || !wcscmp(script, L"sprintads"));
    bool shieldScript = script && !wcscmp(script, L"shield"), shieldOk = false;
    bool onsetScript = script && !wcscmp(script, L"onset");
    bool shakeScript = script && !wcscmp(script, L"shake");
    bool meleeBlockScript = script && !wcscmp(script, L"meleeblock");
    if (shieldScript) vmgame::g_shieldR.Seed(1234u);
    int shotsOk = 0, shotsWanted = 0;
    Burst rec[2];
    bool haveRec[2] = {false, false};
    int lrNext = 0, lrRunning = -1, recDiff = -2, recItems = 0;
    double lrIdle = -1;
    bool layersOk = false;
    LONG64 fpsQ0 = RealQpc();
    int frames = 0;
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        if (g_minimized && !g_scriptSealed) {
            Sleep(30);
            continue;
        }
        if (g_resize) {
            g_resize = false;
            RECT rc;
            GetClientRect(G.hwnd, &rc);
            if (rc.right > 0 && rc.bottom > 0 && (static_cast<UINT>(rc.right) != G.w || static_cast<UINT>(rc.bottom) != G.h))
                Resize(rc.right, rc.bottom);
        }
        LONG64 frameQ = RealQpc();
        if (g_rec.on) {
            if (g_rec.lastFrameQpc) {
                double ms = RealMs(frameQ - g_rec.lastFrameQpc);
                g_rec.frameMsSum += ms;
                g_rec.frameMsMax = ms > g_rec.frameMsMax ? ms : g_rec.frameMsMax;
                ++g_rec.frameN;
            }
            g_rec.lastFrameQpc = frameQ;
        }
        ReplayClock();
        double now = Now();
        if (g_rp.armed) ResetForReplay(now);
        Input in;
        if (g_rp.active) {
            InterlockedExchange(&g_rawDx, 0);
            InterlockedExchange(&g_rawDy, 0);
            ReplayInput(now, in);
        } else if (script) {
            if (tScript0 < 0 && g_st.draws > 0) tScript0 = now;
            in.fg = true;
            in.lmb = (tScript0 >= 0 && now - tScript0 < fireUntil) || fireTap > 0;
            if (fireTap > 0) --fireTap;
            in.rmb = adsHeld;
            in.w = wHeld;
            in.a = aHeld;
            in.d = dHeld;
            in.shift = shiftTap > 0;
            if (shiftTap > 0) --shiftTap;
        } else {
            bool fg = GetForegroundWindow() == G.hwnd;
            in.dx = InterlockedExchange(&g_rawDx, 0);
            in.dy = InterlockedExchange(&g_rawDy, 0);
            if (!g_captured) in.dx = in.dy = 0;
            in.fg = fg;
            auto key = [&](int vk) { return fg && (GetAsyncKeyState(vk) & 0x8000) != 0; };
            bool lmbDown = key(VK_LBUTTON);
            if (!lmbDown) g_ignoreLmb = false;
            in.lmb = g_captured && lmbDown && !g_ignoreLmb;
            in.rmb = g_captured && key(VK_RBUTTON);
            in.shift = key(VK_SHIFT);
            in.w = key('W');
            in.a = key('A');
            in.s = key('S');
            in.d = key('D');
            if (g_captured && fg) {
                RECT rc;
                GetClientRect(G.hwnd, &rc);
                POINT c = {rc.right / 2, rc.bottom / 2};
                ClientToScreen(G.hwnd, &c);
                SetCursorPos(c.x, c.y);
            }
        }
        if (g_mute)
            for (int i = 0; i < g_modelCount; ++i) {
                vm::Settings& s = g_w[i].set;
                s.fireVolume = s.fireMechVolume = s.fireLfeVolume = s.fireAtmoVolume = s.fireTailVolume = 0.f;
                s.animSoundVolume = s.stepFootVolume = s.stepClothVolume = s.stepRattleVolume = s.spinVolume = 0.f;
            }
        GameFrame(now, in);
        if (!g_rp.active) WatchStep(now);
        DrawScene();
        PreDrawLayer(now);
        DrawLayer(G.sc);
        PostDrawLayer(now);
        if (stanceScript && tScript0 >= 0) StanceMonitor(now, tScript0);
        if (shieldScript && tScript0 >= 0) ShieldMonitor(now);
        if (onsetScript && tScript0 >= 0) OnsetRecord(now);
        if (shakeScript && tScript0 >= 0) ShakeRecord(now);
        if (meleeBlockScript && tScript0 >= 0) MeleeBlockMonitor();
        DrawHud(now);
        if (g_shotReq) {
            g_shotReq = false;
            wchar_t p[MAX_PATH * 2];
            swprintf(p, MAX_PATH * 2, L"%ls\\bench_shots", g_exeDir);
            CreateDirectoryW(p, nullptr);
            SYSTEMTIME st;
            GetLocalTime(&st);
            swprintf(p, MAX_PATH * 2, L"%ls\\bench_shots\\shot_%04d%02d%02d_%02d%02d%02d.bmp", g_exeDir, st.wYear, st.wMonth, st.wDay, st.wHour,
                     st.wMinute, st.wSecond);
            BLog("bench: screenshot %ls %s", p, SaveBmp(p) ? "ok" : "FAILED");
        }
        if (script && tScript0 >= 0) {
            double t = now - tScript0;
            if (!wcscmp(script, L"rapid") && t >= 0.8 && g_rapidN < 4096) {
                float m[12];
                if (vm::DebugBoneWorld(117, m)) {
                    g_rapid[g_rapidN][0] = t;
                    for (int k = 0; k < 3; ++k) g_rapid[g_rapidN][1 + k] = m[k * 4 + 1] * 1.317f + m[k * 4 + 3];
                    ++g_rapidN;
                }
            }
            if (vmgame::g_modelIdx >= 0 && vmgame::g_modelIdx < vmgame::kMaxModels)
                shellMax[vmgame::g_modelIdx] = std::max(shellMax[vmgame::g_modelIdx], vmgame::Wp().shells.Live());
            if (movePhase == 0 || movePhase == 1)
                moveMinX[movePhase] = std::min(moveMinX[movePhase], vmgame::Wp().shells.LastMinViewX());
            while (next < nev && t >= ev[next].t) {
                const Ev& e = ev[next++];
                char tag[32];
                snprintf(tag, sizeof tag, "t=%.2f", e.t);
                if (e.what == 0) PrintState(tag);
                if (e.what == 1) {
                    fireUntil = t + e.n * 0.1;
                    BLog("FIRE held %.1f s", e.n * 0.1);
                }
                if (e.what == 2) {
                    wchar_t p[MAX_PATH * 2];
                    swprintf(p, MAX_PATH * 2, L"%ls\\%ls", outDir, e.shot);
                    ++shotsWanted;
                    bool ok = SaveBmp(p);
                    shotsOk += ok ? 1 : 0;
                    if (script && !wcscmp(script, L"crossfade") && g_cfN < 32) {
                        vmgame::CrossLook(g_cfS[g_cfN], g_cfA[g_cfN]);
                        BLog("  crosshair %ls: spread %.1f px (fire %.1f + move %.1f) alpha %.3f", e.shot, g_cfS[g_cfN], vmgame::g_mo.crossKick,
                             vmgame::g_mo.crossMove, g_cfA[g_cfN]);
                        ++g_cfN;
                    }
                    if (tTap >= 0)
                        BLog("SHOT %ls %s (%.1f ms after tap, gas style %d, particles %d, shells %d)", e.shot, ok ? "ok" : "FAILED",
                             (now - tTap) * 1000.0, GasStyleNow(), vmgame::Wp().flash.Live(), vmgame::Wp().shells.Live());
                    else
                        BLog("SHOT %ls %s", e.shot, ok ? "ok" : "FAILED");
                    vm::MuzzleFrame bmf;
                    if (vmgame::Wp().rend.MuzzleView(&bmf) && bmf.brass && bmf.bpos[2] > 0.01f)
                        BLog("  brass ndc %.3f,%.3f depth %.3f m out %.2f,%.2f,%.2f fwd %.2f,%.2f,%.2f up %.2f,%.2f,%.2f", bmf.bpos[0] * bmf.xs / bmf.bpos[2],
                             bmf.bpos[1] * bmf.ys / bmf.bpos[2], bmf.bpos[2], bmf.bout[0], bmf.bout[1], bmf.bout[2], bmf.bfwd[0], bmf.bfwd[1], bmf.bfwd[2],
                             bmf.bup[0], bmf.bup[1], bmf.bup[2]);
                }
                if (e.what == 3) {
                    adsHeld = e.n != 0;
                    BLog("ADS %s", adsHeld ? "held" : "released");
                }
                if (e.what == 4 && e.n >= 0 && e.n < 2) {
                    rec[e.n] = g_burst.open ? g_burst : g_lastBurst;
                    haveRec[e.n] = true;
                }
                if (e.what == 5) running = false;
                if (e.what == 15) {
                    fireTap = 1;
                    tTap = now;
                    ++taps;
                    BLog("FIRE tap (one frame)");
                }
                if (e.what == 16) {
                    g_gasStyle = e.n;
                    BLog("GAS style %d", e.n);
                }
                if (e.what == 17) g_hudOn = e.n != 0;
                if (e.what == 32) g_onSeg0[e.n & 7] = g_onN;
                if (e.what == 33) EvaluateOnset(e.n);
                if (e.what == 40) g_shakeSeg0[e.n & 7] = g_shakeN;
                if (e.what == 41) EvaluateShake(e.n);
                if (e.what == 42) {
                    g_shakeOn = e.n != 0;
                    BLog("SHAKE switch %s", g_shakeOn ? "on" : "off");
                }
                if (e.what == 34) {
                    g_onsetOn = e.n != 0;
                    BLog("ONSET switch %s", g_onsetOn ? "on" : "off");
                }
                if (e.what == 30) {
                    InterlockedExchange(&vmgame::g_mag.ammo, e.n);
                    BLog("AMMO set %d", e.n);
                }
                if (e.what == 31) {
                    const vmgame::FxStat& c = vmgame::g_fxStat;
                    int dLast = c.lastFired - lsPrev.lastFired, dSnd = c.lastShots - lsPrev.lastShots, dClip = c.lastClips - lsPrev.lastClips;
                    int dCas = c.casings - lsPrev.casings, dAds = c.casingAds - lsPrev.casingAds, dLand = c.lands - lsPrev.lands;
                    int dDrop = c.landsDropped - lsPrev.landsDropped, dSet = c.settles - lsPrev.settles;
                    bool ok = false;
                    if (e.n == 1) ok = dLast == 1 && dSnd == 1 && dClip == 1 && dCas == 4 && dAds == 0 && dLand >= 3 && c.landAgeMin >= 0.4f && c.landAgeMax <= 1.0f;
                    if (e.n == 2) ok = dLast == 1 && dSnd == 1 && dClip == 1 && dAds == 3 && dCas == 0;
                    if (e.n == 3) ok = dLast == 0 && dCas >= 15 && dAds == 0 && dLand >= 3 && dLand <= 6 * 6;
                    ok = ok && dSet == 0;
                    ++lsStages;
                    lsFails += ok ? 0 : 1;
                    BLog("LASTSHOT stage %d: last %d (sound %d, clip %d) casing hip %d ads %d, land %d dropped %d, land age %.2f-%.2f s, settle %d -> %s",
                         e.n, dLast, dSnd, dClip, dCas, dAds, dLand, dDrop, c.landAgeMin, c.landAgeMax, dSet, ok ? "ok" : "FAIL");
                    lsPrev = c;
                }
                if (e.what == 18) {
                    aHeld = e.n == 1, dHeld = e.n == 2;
                    BLog("STRAFE %s", e.n == 1 ? "A (left)" : e.n == 2 ? "D (right)" : "released");
                }
                if (e.what == 19) {
                    g_shellFollow = e.n;
                    BLog("SHELL follow %d", e.n);
                }
                if (e.what == 20) {
                    movePhase = e.n;
                    if (movePhase == 1) moveMinX[1] = 1e9f;
                }
                if (e.what == 6) {
                    wHeld = e.n != 0;
                    BLog("W %s", wHeld ? "held" : "released");
                }
                if (e.what == 7) {
                    shiftTap = 1;
                    if (e.n == 1) g_sm.armMini = true;
                    BLog("SHIFT tap%s", e.n == 1 ? " (check minigun sprint)" : "");
                }
                if (e.what == 8 && e.n >= 0 && e.n < 2) {
                    g_sm.rk = e.n;
                    g_sm.tR[e.n] = now;
                    g_sm.stanceAtR[e.n] = vmgame::g_an.stance;
                    bool ok = vmgame::StartMagReload();
                    BLog("RELOAD pressed at anim stance %d: %s", vmgame::g_an.stance, ok ? "started" : "refused");
                }
                if (e.what == 9) {
                    vmgame::SelectWeapon(e.n);
                    BLog("SELECT weapon %d", e.n);
                }
                if (e.what == 10) stanceOk = EvaluateStance();
                if (e.what == 11) stanceOk = EvaluateSwap();
                if (e.what == 13 && e.n >= 0 && e.n < 6) {
                    adsHeld = true;
                    g_sm.ak = e.n;
                    g_sm.tAds[e.n] = now;
                    g_sm.stanceAtAds[e.n] = vmgame::g_an.stance;
                    g_sm.adsTimeAt[e.n] = vmgame::AdsTime(e.n % 3 == 2 ? 0 : e.n % 3 + 1);
                    g_sm.sprintClipAt[e.n] = vmgame::Wp().rend.FindClip(vmgame::g_an.stance == 3 ? "tac_sprint" : "sprint");
                    g_sm.idleClipAt[e.n] = vmgame::g_an.idle;
                    BLog("ADS held (W released same frame) at anim stance %d (check %d)", vmgame::g_an.stance, e.n);
                }
                if (e.what == 14) stanceOk = EvaluateSprintAds();
                if (e.what == 21) {
                    InterlockedExchange(&vmgame::g_sh.want, e.n ? 1 : 0);
                    BLog("SHIELD want %d", e.n);
                }
                if (e.what == 22) {
                    vmgame::ShieldHit();
                    BLog("SHIELD hit (shield w %.2f)", vmgame::g_sh.w);
                }
                if (e.what == 23) shieldOk = EvaluateShield(shotsOk, shotsWanted);
                if (e.what == 27) MeleeBlockCheckpoint(e.n, now);
                if (e.what == 24) ShieldCheckpoint(e.n, now);
                if (e.what == 25) {
                    g_shm.tR = now;
                    bool ok = vmgame::StartMagReload();
                    BLog("RELOAD pressed (shield w %.2f): %s", vmgame::g_sh.w, ok ? "started" : "refused");
                }
                if (e.what == 12 && g_sm.nsk < 2) {
                    int k = g_sm.nsk++;
                    g_sm.sk = k;
                    g_sm.tSel[k] = now;
                    g_sm.stanceAtSel[k] = vmgame::g_an.stance;
                    vmgame::SelectWeapon(e.n);
                    BLog("SELECT weapon %d at anim stance %d (check %d)", e.n, vmgame::g_an.stance, k);
                }
            }
        }
        if (!ReplayFrameEnd(now)) RecFrameEnd(now);
        if (layersScript && tScript0 >= 0 && !g_rp.armed && !g_rp.active) {
            if (lrRunning >= 0) {
                LayerResult& L = g_lr[lrRunning];
                L.done = g_lastSum.valid && g_rec.replay && g_lastSum.id == g_rec.id;
                L.sum = g_lastSum;
                L.stepMs = g_rec.step * 1000.0 / benchclock::kHz;
                L.sndSubmits = L.gunHits = L.camHits = 0;
                for (int i = 0; i < g_rec.nShot; ++i) {
                    L.sndSubmits += g_rec.shots[i].sndAt >= 0;
                    L.gunHits += g_rec.shots[i].gunAt >= 0;
                    L.camHits += g_rec.shots[i].camAt >= 0;
                }
                if (lrRunning == kRunA) memcpy(&g_recRef, &g_rec, sizeof g_rec);
                if (lrRunning == kRunB) recDiff = CompareRec(g_recRef, g_rec, &recItems);
                if (lrRunning == kRunBlendOffHip) g_blendHipDiff = CompareRec(g_recRef, g_rec, &g_blendHipItems);
                if (lrRunning == kRunSwNew) memcpy(&g_recSw, &g_rec, sizeof g_rec);
                if (lrRunning == kRunSwOld) CompareSwitch(g_recSw, g_rec, g_rp.adsOn, g_rp.adsOff);
                lrRunning = -1;
                lrIdle = now;
            }
            if (lrIdle < 0) lrIdle = now;
            if (lrNext < kLayerRunN && now - lrIdle >= 0.25) {
                const LayerRun& L = kLayerRuns[lrNext];
                memcpy(g_layer, L.layers, sizeof g_layer);
                LayerChanged("layers script");
                g_fpsMode = L.fpsMode;
                g_nextFrameReal = 0;
                if (RequestReplay(L.mode, L.label)) lrRunning = lrNext;
                ++lrNext;
            } else if (lrNext >= kLayerRunN && now - lrIdle >= 0.25) {
                wchar_t p[MAX_PATH * 2];
                swprintf(p, MAX_PATH * 2, L"%ls\\bench_layers.bmp", outDir);
                BLog("SHOT bench_layers.bmp %s", SaveBmp(p) ? "ok" : "FAILED");
                layersOk = EvaluateLayers(recDiff, recItems);
                running = false;
            }
        }
        g_workMs = RealMs(RealQpc() - frameQ);
        LimitFrame();
        G.sc->Present(kFpsModes[g_fpsMode] ? 0 : 1, 0);
        ++frames;
        LONG64 fq = RealQpc();
        if (RealMs(fq - fpsQ0) >= 500.0) {
            g_fps = static_cast<int>(frames * 1000.0 / RealMs(fq - fpsQ0) + 0.5);
            frames = 0;
            fpsQ0 = fq;
            wchar_t title[200];
            swprintf(title, 200, L"试枪台 vm_bench - %ls - %ld/%ld", ModelName(g_modelIdx), g_mag.ammo, g_mag.size);
            SetWindowTextW(G.hwnd, title);
        }
    }
    Capture(false);
    if (g_rec.on) RecFinish(Now());
    probe::Stop();
    if (timeEnd) timeEnd(1);
    int code = g_failed ? 3 : 0;
    if (layersScript) {
        if (layersOk) {
            BLog("BENCH LAYERS OK");
        } else {
            BLog("BENCH LAYERS FAIL (%d)", g_layerFails);
            code = 5;
        }
    }
    if (stanceScript) {
        bool swap = !wcscmp(script, L"swap"), sads = !wcscmp(script, L"sprintads");
        const char* nm = swap ? "SWAP" : sads ? "SPRINTADS" : "STANCE";
        if (stanceOk) BLog("BENCH %s OK", nm);
        else BLog("BENCH %s FAIL (%d)", nm, g_stanceFails);
        if (!stanceOk) code = swap ? 7 : sads ? 8 : 6;
    }
    if (shieldScript) {
        BLog("BENCH SHIELD %s (%d)", shieldOk && !g_failed ? "OK" : "FAIL", g_shieldFails);
        if (!shieldOk || g_failed) code = 12;
    }
    if (meleeBlockScript) {
        bool ok = EvaluateMeleeBlock(shotsOk, shotsWanted) && !g_failed;
        BLog("BENCH MELEEBLOCK %s (%d)", ok ? "OK" : "FAIL", g_mbFails);
        if (!ok) code = 13;
    }
    if (onsetScript) {
        OnsetWriteCsv(outDir);
        bool ok = !g_failed && shotsOk == shotsWanted && g_onStages == 5 && g_onFails == 0;
        BLog("BENCH ONSET stages %d fails %d screenshots %d/%d -> %s", g_onStages, g_onFails, shotsOk, shotsWanted, ok ? "OK" : "FAIL");
        if (!ok) code = 14;
    }
    if (shakeScript) {
        ShakeWriteCsv(outDir);
        bool ok = !g_failed && shotsOk == shotsWanted && g_shakeStages == 4 && g_shakeFails == 0;
        BLog("BENCH SHAKE stages %d fails %d screenshots %d/%d -> %s", g_shakeStages, g_shakeFails, shotsOk, shotsWanted, ok ? "OK" : "FAIL");
        if (!ok) code = 15;
    }
    if (script && !wcscmp(script, L"lastshot")) {
        bool ok = !g_failed && shotsOk == shotsWanted && lsStages == 3 && lsFails == 0;
        BLog("BENCH LASTSHOT stages %d fails %d screenshots %d/%d -> %s", lsStages, lsFails, shotsOk, shotsWanted, ok ? "OK" : "FAIL");
        if (!ok) code = 13;
    }
    if (script && !wcscmp(script, L"shellmove")) {
        bool ok = !g_failed && shotsOk == shotsWanted && moveMinX[0] > 0.f && moveMinX[0] < 1e8f && moveMinX[1] < 1e8f;
        BLog("BENCH SHELLMOVE min view x: follow %.3f m, old %.3f m, screenshots %d/%d -> %s", moveMinX[0], moveMinX[1], shotsOk, shotsWanted,
             ok ? "OK" : "FAIL");
        if (!ok) code = 11;
    }
    if (script && !wcscmp(script, L"shell")) {
        bool ok = !g_failed && shotsOk == shotsWanted && taps == 2 && shellMax[0] >= 3 && shellMax[1] >= 3;
        BLog("BENCH SHELL taps=%d holes=%d max shells m4=%d minigun=%d screenshots %d/%d -> %s", taps, g_holeN, shellMax[0], shellMax[1], shotsOk,
             shotsWanted, ok ? "OK" : "FAIL");
        if (!ok) code = 10;
    }
    if (script && !wcscmp(script, L"crossfade")) {
        const vm::CrosshairSettings& cs = vmgame::g_crossSet;
        const bool fades = cs.fadeEnd > cs.fadeStart;
        bool ok = !g_failed && shotsOk == shotsWanted && g_cfN == 16;
        bool mono = true, allVisible = true;
        for (int i = 1; i < 7 && g_cfN == 16; ++i) mono = mono && g_cfA[i + 1] <= g_cfA[i] + 1e-4f && g_cfS[i + 1] >= g_cfS[i] - 1e-3f;
        for (int i = 0; i < g_cfN; ++i) allVisible = allVisible && g_cfA[i] > 0.99f;
        ok = ok && mono && g_cfA[0] > 0.99f && g_cfA[10] > 0.99f;
        ok = ok && (fades ? (g_cfA[7] < 0.01f && g_cfA[14] < 0.01f) : (allVisible && g_cfS[7] >= cs.spreadMax - 0.01f && g_cfS[14] >= cs.spreadMax - 0.01f));
        BLog("BENCH CROSSFADE (%s) m4 alpha %.2f -> %.2f (0.5 s) -> %.2f (1.0 s) -> %.2f (1.95 s), spread %.1f -> %.1f -> %.1f px (max %.1f), "
             "release 0.25 s alpha %.2f, 0.5 s %.2f; minigun 0.7 s %.2f, 2.3 s %.2f; monotonic %d -> %s", fades ? "fades" : "no fade", g_cfA[0], g_cfA[2],
             g_cfA[4], g_cfA[7], g_cfS[0], g_cfS[4], g_cfS[7], cs.spreadMax, g_cfA[9], g_cfA[10], g_cfA[11], g_cfA[14], mono ? 1 : 0, ok ? "OK" : "FAIL");
        if (!ok) code = 16;
    }
    if (script && !wcscmp(script, L"gas")) {
        bool ok = !g_failed && shotsOk == shotsWanted && taps == 4 && g_holeN >= taps + 8;
        BLog("BENCH GAS taps=%d holes=%d screenshots %d/%d -> %s", taps, g_holeN, shotsOk, shotsWanted, ok ? "OK" : "FAIL");
        if (!ok) code = 9;
    }
    if (script && !wcscmp(script, L"kick")) {
        const char* names[2] = {"hip", "ads"};
        for (int k = 0; k < 2; ++k)
            BLog("BENCH %s: shots=%d kick climb=%.2f deg net=%.2f deg punch max=%.2f deg gun back max=%.2f cm gun pitch max=%.2f deg roll max=%.2f deg",
                 names[k], rec[k].shots, rec[k].climbPeak, rec[k].netPeak, rec[k].punchMax, rec[k].backMax, rec[k].gunPitchMax, rec[k].rollMax);
        int onBoards = 0, onWall = 0;
        for (int i = 0; i < g_holeN; ++i) {
            const Hole& h = g_holes[i];
            for (int b = 0; b < g_nBoxes; ++b) {
                const Box& B = g_boxes[b];
                bool in3 = h.p[0] >= B.lo[0] - 1e-3f && h.p[0] <= B.hi[0] + 1e-3f && h.p[1] >= B.lo[1] - 1e-3f && h.p[1] <= B.hi[1] + 1e-3f &&
                           h.p[2] >= B.lo[2] - 1e-3f && h.p[2] <= B.hi[2] + 1e-3f;
                if (in3 && B.kind == 1) ++onBoards;
                if (in3 && B.kind == 2) ++onWall;
            }
        }
        BLog("BENCH holes=%d (targets %d, backstop %d) screenshots %d/%d", g_holeN, onBoards, onWall, shotsOk, shotsWanted);
        const Burst &hip = rec[0], &ads = rec[1];
        bool ok = haveRec[0] && haveRec[1] && !g_failed && hip.shots >= 12 && hip.climbPeak > 2.f && hip.climbPeak < 8.f && ads.shots >= 10 &&
                  ads.climbPeak > 0.5f && ads.climbPeak < hip.climbPeak && hip.backMax > 0.5f && ads.backMax > 0.3f &&
                  hip.gunPitchMax > ads.gunPitchMax && g_holeN >= hip.shots + ads.shots - 2 && shotsOk == shotsWanted;
        BLog(ok ? "BENCH OK" : "BENCH FAIL");
        if (!ok) code = 4;
    }
    if (script && !wcscmp(script, L"rapid")) {
        double amax = 0, tmax = 0;
        int over = 0;
        double v0[3] = {0, 0, 0};
        bool have = false;
        for (int i = 1; i < g_rapidN; ++i) {
            double dt = g_rapid[i][0] - g_rapid[i - 1][0];
            if (dt <= 1e-6) continue;
            double v[3];
            for (int k = 0; k < 3; ++k) v[k] = (g_rapid[i][1 + k] - g_rapid[i - 1][1 + k]) / dt;
            if (have) {
                double a2 = 0;
                for (int k = 0; k < 3; ++k) a2 += (v[k] - v0[k]) * (v[k] - v0[k]);
                double acc = std::sqrt(a2) / dt;
                if (acc > amax) amax = acc, tmax = g_rapid[i][0];
                if (acc > 1500) ++over;
            }
            memcpy(v0, v, sizeof v);
            have = true;
        }
        BLog("RAPID frames %d, spear tip max accel %.0f m/s^2 at t=%.3f, frames over 1500 m/s^2: %d, snapshot fades %d (%s)", g_rapidN, amax, tmax, over,
             vmgame::melee::g_snapCount, vmgame::melee::kSnapOn ? "snapshot on" : "HFPS_MELEE_NOSNAP");
        char buf[2048] = "";
        for (int i = 0; i < vmgame::melee::g_traceN; ++i) {
            strncat(buf, vmgame::melee::kDef[vmgame::melee::g_trace[i]].name, sizeof buf - strlen(buf) - 2);
            strncat(buf, " ", sizeof buf - strlen(buf) - 1);
        }
        BLog("RAPID trace (%d): %s", vmgame::melee::g_traceN, buf);
        BLog("RAPID strikes %d (taken %d)", vmgame::melee::g_strikeN, g_benchStrikes);
        FILE* f = nullptr;
        wchar_t pth[MAX_PATH * 2];
        swprintf(pth, MAX_PATH * 2, L"%ls\\rapid_tip%ls.csv", outDir, vmgame::melee::kSnapOn ? L"" : L"_nosnap");
        if ((f = _wfopen(pth, L"w"))) {
            fprintf(f, "t,x,y,z\n");
            for (int i = 0; i < g_rapidN; ++i) fprintf(f, "%.5f,%.5f,%.5f,%.5f\n", g_rapid[i][0], g_rapid[i][1], g_rapid[i][2], g_rapid[i][3]);
            fclose(f);
        }
    }
    if (script && !wcscmp(script, L"melee")) {
        using namespace vmgame::melee;
        const int want[] = {kDefault, kLight1, kDefaultLeft, kLight2, kDefaultRight, kLight3, kDefault, kLight1, kDefault, kHeavy1,
                            kDefaultLeftHeavy, kHeavy2, kDefaultStabHeavy, kHeavy3, kBlock, kPush, kBlock, kIdle, kBlock, kPush, kPushStab};
        const int nw = sizeof want / sizeof *want;
        bool ok = g_traceN == nw && shotsOk == shotsWanted;
        for (int i = 0; i < g_traceN && i < nw; ++i) ok = ok && g_trace[i] == want[i];
        char buf[2048] = "";
        for (int i = 0; i < g_traceN; ++i) {
            strncat(buf, kDef[g_trace[i]].name, sizeof buf - strlen(buf) - 2);
            strncat(buf, " ", sizeof buf - strlen(buf) - 1);
        }
        BLog("MELEE trace (%d): %s", g_traceN, buf);
        const int wantStrike[] = {vmgame::kStrikeStab, vmgame::kStrikeStab, vmgame::kStrikeSweep, vmgame::kStrikeStab,
                                  vmgame::kStrikeHeavySweep, vmgame::kStrikeHeavy, vmgame::kStrikeHeavy, vmgame::kStrikeBash,
                                  vmgame::kStrikeBash, vmgame::kStrikeStab};
        const int ns = sizeof wantStrike / sizeof *wantStrike;
        bool sok = g_strikeN == ns && g_benchStrikes == ns;
        for (int i = 0; i < g_strikeN && i < ns; ++i) sok = sok && g_strikeTrace[i] == wantStrike[i];
        buf[0] = 0;
        for (int i = 0; i < g_strikeN; ++i) {
            char one[8];
            snprintf(one, sizeof one, "%d ", g_strikeTrace[i]);
            strncat(buf, one, sizeof buf - strlen(buf) - 1);
        }
        BLog("MELEE strikes %d (taken %d): %s%s", g_strikeN, g_benchStrikes, buf, sok ? "" : " - MISMATCH");
        ok = ok && sok;
        BLog("MELEE screenshots %d/%d", shotsOk, shotsWanted);
        BLog(ok ? "MELEE OK" : "MELEE FAIL");
        if (!ok) code = 6;
    }
    BLog("bench: exit %d", code);
    if (g_logFile) fclose(g_logFile);
    return code;
}
