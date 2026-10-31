#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <initializer_list>
#include <cstdio>
#include <cstring>
namespace kick {
constexpr float kDeg = 3.14159265f / 180.f;
struct Set {
    float climbPitch[2];
    float climbYaw[2];
    float punch[3];
    float gunMove[3];
    float gunRot[3];
};
struct Params {
    bool enabled = true;
    float camScale = 1.f, gunScale = 1.f;
    Set hip = {{0.26f, 0.36f}, {-0.08f, 0.14f}, {0.30f, 0.10f, 0.f}, {0.014f, 0.0020f, 0.0012f}, {1.1f, 0.30f, 1.0f}};
    Set ads = {{0.20f, 0.28f}, {-0.05f, 0.09f}, {0.16f, 0.05f, 0.f}, {0.009f, 0.0004f, 0.0003f}, {0.22f, 0.06f, 0.35f}};
    float firstShot = 0.7f;
    int rampShots = 4;
    float burstReset = 0.25f;
    float recover[3] = {0.4f, 0.10f, 18.f};
    float punchSpring[2] = {8.f, 0.6f};
    float punchAlt = 0.f;
    float gunPosSpring[2] = {11.f, 0.75f};
    float gunRotSpring[2] = {7.f, 0.45f};
    float gunPivot[3] = {0.07f, -0.12f, 0.13f};
    float sustainScale = 1.f;
    float sustainMove[3] = {0.006f, 0.0015f, 0.0015f};
    float sustainRot[3] = {0.6f, 0.35f, 0.6f};
    float sustainRise = 0.35f;
    float sustainWander = 1.3f;
    float sustainFade[2] = {0.15f, 0.25f};
    float sustainReset = 0.4f;
    float sustainAds = 0.35f;
    float onsetScale = 1.f;
    float onsetHip[6] = {0.0035f, -0.0025f, 0.0015f, 0.5f, 0.25f, -1.4f};
    float onsetAds[6] = {0.0010f, -0.0008f, 0.0010f, 0.12f, 0.06f, -0.5f};
    float onsetPeak = 0.06f;
    float onsetEcho[6] = {0.32f, 0.20f, 0.34f, 0.88f, 0.26f, 0.18f};
    float onsetEnd[2] = {1.6f, 2.4f};
    float onsetPosSpring[2] = {4.5f, 0.32f};
    float onsetFade[2] = {0.03f, 0.2f};
    float onsetReset = 0.35f;
    float jitterScale = 1.f;
    float jitterHip[3] = {0.35f, 0.30f, 0.55f};
    float jitterAds[3] = {0.08f, 0.07f, 0.18f};
    float jitterUp = 0.35f;
    float jitterRadial = 0.5f;
    float jitterSnap = 0.6f;
    float jitterDecay = 14.f;
    float jitterSpring[2] = {6.f, 0.35f};
    int blendMode = 1;
    float blendTime = 0.25f;
    float shakeScale = 1.f;
    float shakeHip[3] = {0.15f, 0.12f, 0.30f};
    float shakeAds[3] = {0.07f, 0.06f, 0.15f};
    float shakeFreq = 21.f;
    float shakeRatio[4] = {1.07f, 1.17f, 1.26f, 1.36f};
    float shakeTime = 0.11f;
    float shakeBlend = 0.02f;
    float refScale = 0.f;
    float refHip = 0.f;
    float refReplace = 1.f;
};
inline float SCurve(float t) { return t <= 0.f ? 0.f : t >= 1.f ? 1.f : 0.5f + 0.5f * std::sin((t - 0.5f) * 3.14159265f); }
inline int ReadFloats(const wchar_t* ini, const wchar_t* key, float* out, int n) {
    wchar_t buf[256];
    GetPrivateProfileStringW(L"kick", key, L"", buf, 256, ini);
    int got = 0;
    for (wchar_t* p = buf; *p && got < n;) {
        while (*p == L' ' || *p == L',' || *p == L'\t') ++p;
        if (!*p) break;
        wchar_t* end = nullptr;
        float v = std::wcstof(p, &end);
        if (end == p) break;
        if (std::isfinite(v)) out[got] = v;
        ++got;
        p = end;
    }
    return got;
}
inline void LoadSet(const wchar_t* ini, const wchar_t* prefix, Set& s) {
    wchar_t k[64];
    swprintf(k, 64, L"%ls_climb_pitch", prefix);
    ReadFloats(ini, k, s.climbPitch, 2);
    swprintf(k, 64, L"%ls_climb_yaw", prefix);
    ReadFloats(ini, k, s.climbYaw, 2);
    swprintf(k, 64, L"%ls_punch", prefix);
    ReadFloats(ini, k, s.punch, 3);
    swprintf(k, 64, L"%ls_gun_move", prefix);
    ReadFloats(ini, k, s.gunMove, 3);
    swprintf(k, 64, L"%ls_gun_rot", prefix);
    ReadFloats(ini, k, s.gunRot, 3);
}
inline void Load(const wchar_t* ini, Params& p) {
    float v[1];
    if (ReadFloats(ini, L"enabled", v, 1)) p.enabled = v[0] != 0.f;
    ReadFloats(ini, L"cam_scale", &p.camScale, 1);
    ReadFloats(ini, L"gun_scale", &p.gunScale, 1);
    LoadSet(ini, L"hip", p.hip);
    LoadSet(ini, L"ads", p.ads);
    ReadFloats(ini, L"first_shot_scale", &p.firstShot, 1);
    if (ReadFloats(ini, L"ramp_shots", v, 1)) p.rampShots = static_cast<int>(v[0]);
    ReadFloats(ini, L"burst_reset", &p.burstReset, 1);
    ReadFloats(ini, L"recover", p.recover, 3);
    ReadFloats(ini, L"punch_spring", p.punchSpring, 2);
    ReadFloats(ini, L"punch_alternate", &p.punchAlt, 1);
    ReadFloats(ini, L"gun_pos_spring", p.gunPosSpring, 2);
    ReadFloats(ini, L"gun_rot_spring", p.gunRotSpring, 2);
    ReadFloats(ini, L"gun_pivot", p.gunPivot, 3);
    ReadFloats(ini, L"sustain_scale", &p.sustainScale, 1);
    ReadFloats(ini, L"sustain_gun_move", p.sustainMove, 3);
    ReadFloats(ini, L"sustain_gun_rot", p.sustainRot, 3);
    ReadFloats(ini, L"sustain_rise", &p.sustainRise, 1);
    ReadFloats(ini, L"sustain_wander", &p.sustainWander, 1);
    ReadFloats(ini, L"sustain_fade", p.sustainFade, 2);
    ReadFloats(ini, L"sustain_reset", &p.sustainReset, 1);
    ReadFloats(ini, L"sustain_ads", &p.sustainAds, 1);
    ReadFloats(ini, L"onset_scale", &p.onsetScale, 1);
    ReadFloats(ini, L"onset_hip", p.onsetHip, 6);
    ReadFloats(ini, L"onset_ads", p.onsetAds, 6);
    ReadFloats(ini, L"onset_peak", &p.onsetPeak, 1);
    ReadFloats(ini, L"onset_echo", p.onsetEcho, 6);
    ReadFloats(ini, L"onset_end", p.onsetEnd, 2);
    ReadFloats(ini, L"onset_pos_spring", p.onsetPosSpring, 2);
    ReadFloats(ini, L"onset_fade", p.onsetFade, 2);
    ReadFloats(ini, L"onset_reset", &p.onsetReset, 1);
    ReadFloats(ini, L"jitter_scale", &p.jitterScale, 1);
    ReadFloats(ini, L"jitter_hip", p.jitterHip, 3);
    ReadFloats(ini, L"jitter_ads", p.jitterAds, 3);
    ReadFloats(ini, L"jitter_up", &p.jitterUp, 1);
    ReadFloats(ini, L"jitter_radial", &p.jitterRadial, 1);
    ReadFloats(ini, L"jitter_snap", &p.jitterSnap, 1);
    ReadFloats(ini, L"jitter_decay", &p.jitterDecay, 1);
    ReadFloats(ini, L"jitter_spring", p.jitterSpring, 2);
    if (ReadFloats(ini, L"blend_mode", v, 1)) p.blendMode = v[0] != 0.f ? 1 : 0;
    ReadFloats(ini, L"blend_time", &p.blendTime, 1);
    ReadFloats(ini, L"shake_scale", &p.shakeScale, 1);
    ReadFloats(ini, L"shake_hip", p.shakeHip, 3);
    ReadFloats(ini, L"shake_ads", p.shakeAds, 3);
    ReadFloats(ini, L"shake_freq", &p.shakeFreq, 1);
    ReadFloats(ini, L"shake_ratio", p.shakeRatio, 4);
    ReadFloats(ini, L"shake_time", &p.shakeTime, 1);
    ReadFloats(ini, L"shake_blend", &p.shakeBlend, 1);
    ReadFloats(ini, L"ref_scale", &p.refScale, 1);
    ReadFloats(ini, L"ref_hip", &p.refHip, 1);
    ReadFloats(ini, L"ref_replace", &p.refReplace, 1);
    auto clamp = [](float& x, float lo, float hi) { x = x < lo ? lo : x > hi ? hi : x; };
    clamp(p.camScale, 0.f, 5.f);
    clamp(p.gunScale, 0.f, 5.f);
    clamp(p.firstShot, 0.f, 2.f);
    if (p.rampShots < 0) p.rampShots = 0;
    if (p.rampShots > 30) p.rampShots = 30;
    clamp(p.burstReset, 0.01f, 3.f);
    clamp(p.recover[0], 0.f, 1.f);
    clamp(p.recover[1], 0.f, 3.f);
    clamp(p.recover[2], 0.1f, 360.f);
    for (float* s : {p.punchSpring, p.gunPosSpring, p.gunRotSpring}) {
        clamp(s[0], 0.5f, 40.f);
        clamp(s[1], 0.05f, 0.95f);
    }
    clamp(p.sustainScale, 0.f, 5.f);
    for (int i = 0; i < 3; ++i) clamp(p.sustainMove[i], -0.05f, 0.05f), clamp(p.sustainRot[i], -10.f, 10.f);
    clamp(p.sustainRise, 0.02f, 5.f);
    clamp(p.sustainWander, 0.1f, 10.f);
    clamp(p.sustainFade[0], 0.01f, 3.f);
    clamp(p.sustainFade[1], 0.01f, 3.f);
    clamp(p.sustainReset, 0.f, 5.f);
    clamp(p.sustainAds, 0.f, 1.f);
    clamp(p.onsetScale, 0.f, 5.f);
    for (float* s : {p.onsetHip, p.onsetAds})
        for (int i = 0; i < 6; ++i) i < 3 ? clamp(s[i], -0.05f, 0.05f) : clamp(s[i], -10.f, 10.f);
    clamp(p.onsetPeak, 0.01f, 1.f);
    for (int i = 0; i < 6; i += 3) clamp(p.onsetEcho[i], 0.f, 5.f), clamp(p.onsetEcho[i + 1], 0.01f, 2.f), clamp(p.onsetEcho[i + 2], 0.f, 2.f);
    clamp(p.onsetEnd[0], 0.05f, 10.f);
    clamp(p.onsetEnd[1], p.onsetEnd[0] + 0.01f, 10.01f);
    clamp(p.onsetPosSpring[0], 0.5f, 40.f);
    clamp(p.onsetPosSpring[1], 0.05f, 0.95f);
    clamp(p.onsetFade[0], 0.005f, 3.f);
    clamp(p.onsetFade[1], 0.005f, 3.f);
    clamp(p.onsetReset, 0.f, 5.f);
    clamp(p.jitterScale, 0.f, 5.f);
    for (int i = 0; i < 3; ++i) clamp(p.jitterHip[i], 0.f, 10.f), clamp(p.jitterAds[i], 0.f, 10.f);
    clamp(p.jitterUp, 0.f, 1.f);
    clamp(p.jitterRadial, 0.f, 1.f);
    clamp(p.jitterSnap, 0.f, 1.f);
    clamp(p.jitterDecay, 0.f, 100.f);
    clamp(p.jitterSpring[0], 0.5f, 40.f);
    clamp(p.jitterSpring[1], 0.05f, 2.f);
    clamp(p.blendTime, 0.02f, 2.f);
    clamp(p.shakeScale, 0.f, 5.f);
    for (int i = 0; i < 3; ++i) clamp(p.shakeHip[i], 0.f, 5.f), clamp(p.shakeAds[i], 0.f, 5.f);
    clamp(p.shakeFreq, 1.f, 60.f);
    for (int i = 0; i < 4; i += 2) {
        clamp(p.shakeRatio[i], 0.25f, 4.f);
        clamp(p.shakeRatio[i + 1], p.shakeRatio[i], 4.f);
    }
    clamp(p.shakeTime, 0.02f, 2.f);
    clamp(p.shakeBlend, 0.f, 0.5f);
    clamp(p.refScale, 0.f, 5.f);
    clamp(p.refHip, 0.f, 1.f);
    clamp(p.refReplace, 0.f, 1.f);
}
inline Set Mix(const Set& a, const Set& b, float w) {
    Set r;
    auto mix = [w](const float* x, const float* y, float* out, int n) {
        for (int i = 0; i < n; ++i) out[i] = x[i] + (y[i] - x[i]) * w;
    };
    mix(a.climbPitch, b.climbPitch, r.climbPitch, 2);
    mix(a.climbYaw, b.climbYaw, r.climbYaw, 2);
    mix(a.punch, b.punch, r.punch, 3);
    mix(a.gunMove, b.gunMove, r.gunMove, 3);
    mix(a.gunRot, b.gunRot, r.gunRot, 3);
    return r;
}
struct Rng {
    uint32_t s = 0x9e3779b9u;
    void Seed(uint32_t x) { s = x ? x : 1u; }
    float U() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(s >> 8) * (1.f / 16777216.f);
    }
    float Range(const float r[2]) { return r[0] + (r[1] - r[0]) * U(); }
    float Sym(float a) { return a * (2.f * U() - 1.f); }
};
inline float Impulse(float peak, float freqHz, float zeta) {
    float w = 2.f * 3.14159265f * freqHz;
    float wd = w * std::sqrt(1.f - zeta * zeta);
    float tp = std::atan2(wd, zeta * w) / wd;
    float g = std::exp(-zeta * w * tp) * std::sin(wd * tp) / wd;
    return g > 1e-9f ? peak / g : 0.f;
}
inline float PeakTime(float freqHz, float zeta) {
    float w = 2.f * 3.14159265f * freqHz;
    float wd = w * std::sqrt(1.f - zeta * zeta);
    return std::atan2(wd, zeta * w) / wd;
}
inline void StepSprings(float* x, float* v, int n, const float spring[2], float dt) {
    if (!(dt > 0.f)) return;
    float w = 2.f * 3.14159265f * spring[0], a = spring[1] * w;
    float wd = w * std::sqrt(1.f - spring[1] * spring[1]);
    float e = std::exp(-a * dt), c = std::cos(wd * dt), s = std::sin(wd * dt);
    for (int i = 0; i < n; ++i) {
        float x0 = x[i], v0 = v[i];
        x[i] = e * (x0 * c + (v0 + a * x0) / wd * s);
        v[i] = e * (v0 * c - (a * v0 + w * w * x0) / wd * s);
    }
}
struct CamKick {
    float x[3] = {}, v[3] = {};
    double lastShot = -1;
    int shotIdx = 0;
    bool burst = false;
    float burstClimb = 0, burstStart = 0, recoverLeft = 0;
    unsigned shots = 0;
    Rng rng;
    float shOut[3] = {};
    float shSnap[3] = {}, shAmp[3] = {};
    float shRatio[2] = {1.f, 1.f};
    double shClock = 0;
    float shAge = 1e9f;
    Rng shRng{0x6a09e667u};
    void OnShot(const Params& P, float adsW, double now, float* pitch, float* yaw, float pitchLimit) {
        if (!P.enabled || P.camScale <= 0.f) return;
        if (!burst || now - lastShot > P.burstReset) {
            shotIdx = 0;
            burstClimb = 0;
            burstStart = *pitch;
            burst = true;
        }
        recoverLeft = 0;
        float ramp = P.rampShots > 0 ? (shotIdx >= P.rampShots ? 1.f : static_cast<float>(shotIdx) / P.rampShots) : 1.f;
        float scale = (P.firstShot + (1.f - P.firstShot) * ramp) * P.camScale;
        Set s = Mix(P.hip, P.ads, adsW);
        float before = *pitch;
        *pitch += rng.Range(s.climbPitch) * scale * kDeg;
        if (*pitch > pitchLimit) *pitch = pitchLimit;
        if (*pitch < -pitchLimit) *pitch = -pitchLimit;
        burstClimb += *pitch - before;
        *yaw += rng.Range(s.climbYaw) * scale * kDeg;
        v[0] += Impulse(s.punch[0] * scale * (0.85f + 0.3f * rng.U()), P.punchSpring[0], P.punchSpring[1]);
        if (P.punchAlt != 0.f) {
            float sgn = (shots & 1u) ? 1.f : -1.f;
            float side = sgn * s.punch[1] * (0.85f + 0.3f * rng.U());
            float rl = sgn * (P.punchAlt > 0.f ? 1.f : -1.f) * s.punch[2] * (0.85f + 0.3f * rng.U());
            v[1] += Impulse(side * scale, P.punchSpring[0], P.punchSpring[1]);
            v[2] += Impulse(rl * scale, P.punchSpring[0], P.punchSpring[1]);
        } else {
            v[1] += Impulse(rng.Sym(s.punch[1]) * scale, P.punchSpring[0], P.punchSpring[1]);
            v[2] += Impulse(rng.Sym(s.punch[2]) * scale, P.punchSpring[0], P.punchSpring[1]);
        }
        ++shotIdx;
        ++shots;
        lastShot = now;
        if (P.shakeScale > 0.f) {
            if (shAge >= P.shakeTime) {
                shRatio[0] = shRng.Range(P.shakeRatio);
                shRatio[1] = shRng.Range(P.shakeRatio + 2);
            }
            float g = P.shakeScale * P.camScale;
            for (int i = 0; i < 3; ++i) {
                shSnap[i] = shOut[i];
                shAmp[i] = (P.shakeHip[i] + (P.shakeAds[i] - P.shakeHip[i]) * adsW) * g;
            }
            shAge = 0.f;
        }
    }
    void Step(const Params& P, double now, float dt, float* pitch) {
        StepSprings(x, v, 3, P.punchSpring, dt);
        for (int i = 0; i < 3; ++i) {
            if (x[i] > 20.f) x[i] = 20.f, v[i] = 0;
            if (x[i] < -20.f) x[i] = -20.f, v[i] = 0;
        }
        if (burst && now - lastShot >= P.recover[1]) {
            recoverLeft = burstClimb * P.recover[0];
            burst = false;
            burstClimb = 0;
        }
        if (recoverLeft > 0.f && dt > 0.f) {
            float d = P.recover[2] * kDeg * dt;
            if (d > recoverLeft) d = recoverLeft;
            float room = *pitch - burstStart;
            if (d > room) d = room > 0.f ? room : 0.f;
            *pitch -= d;
            recoverLeft = d > 0.f ? recoverLeft - d : 0.f;
        }
        StepShake(P, dt);
    }
    void StepShake(const Params& P, float dt) {
        if (!(P.shakeScale > 0.f) || !P.enabled || !(P.camScale > 0.f)) {
            ShakeClear();
            return;
        }
        if (dt > 0.f) {
            shClock += dt;
            shAge += dt;
        }
        if (shAge >= P.shakeTime && shAge >= P.shakeBlend) {
            shOut[0] = shOut[1] = shOut[2] = 0.f;
            return;
        }
        float u = shAge < P.shakeTime ? shAge / P.shakeTime : 1.f;
        float env = 1.f - std::sin(1.57079633f * u);
        float b = P.shakeBlend > 0.f && shAge < P.shakeBlend ? shAge / P.shakeBlend : 1.f;
        double w = 2.0 * 3.14159265358979 * P.shakeFreq * shClock;
        const float c[3] = {static_cast<float>(std::sin(w)), static_cast<float>(std::sin(w * shRatio[0])),
                            static_cast<float>(std::sin(w * shRatio[1]))};
        for (int i = 0; i < 3; ++i) {
            float target = env * c[i] * shAmp[i];
            float o = shSnap[i] + (target - shSnap[i]) * b;
            shOut[i] = o > 5.f ? 5.f : o < -5.f ? -5.f : o;
        }
    }
    void ShakeClear() {
        for (int i = 0; i < 3; ++i) shOut[i] = shSnap[i] = shAmp[i] = 0.f;
        shAge = 1e9f;
    }
    float PitchRad() const { return (x[0] + shOut[0]) * kDeg; }
    float YawRad() const { return (x[1] + shOut[1]) * kDeg; }
    float RollRad() const { return (x[2] + shOut[2]) * kDeg; }
};
struct View {
    float pitch, yaw, roll;
    float fwd[3], right[3], up[3];
};
inline View CamView(const CamKick& k, float pitch, float yaw, float pitchLimit) {
    View v;
    v.pitch = pitch + k.PitchRad();
    if (v.pitch > pitchLimit) v.pitch = pitchLimit;
    if (v.pitch < -pitchLimit) v.pitch = -pitchLimit;
    v.yaw = yaw + k.YawRad();
    v.roll = k.RollRad();
    float sy = std::sin(v.yaw), cy = std::cos(v.yaw), sp = std::sin(v.pitch), cp = std::cos(v.pitch);
    v.fwd[0] = sy * cp, v.fwd[1] = sp, v.fwd[2] = cy * cp;
    v.right[0] = cy, v.right[1] = 0.f, v.right[2] = -sy;
    v.up[0] = -sy * sp, v.up[1] = cp, v.up[2] = -cy * sp;
    if (v.roll != 0.f) {
        float c = std::cos(v.roll), s = std::sin(v.roll);
        for (int i = 0; i < 3; ++i) {
            float u = v.up[i], r = v.right[i];
            v.up[i] = u * c + r * s;
            v.right[i] = r * c - u * s;
        }
    }
    return v;
}
struct GunSpring {
    float pos[3] = {}, vpos[3] = {}, rot[3] = {}, vrot[3] = {};
    float pos2[3] = {}, vpos2[3] = {}, rot2[3] = {}, vrot2[3] = {};
    float rollSign = 1.f;
    unsigned shots = 0;
    Rng rng;
    void OnShot(const Params& P, float adsW) {
        if (!P.enabled || P.gunScale <= 0.f) return;
        float g = P.gunScale;
        const float* ps = P.gunPosSpring;
        const float* rs = P.gunRotSpring;
        float r0 = 0.9f + 0.2f * rng.U(), r1 = 0.8f + 0.4f * rng.U(), r2 = 2.f * rng.U() - 1.f, r3 = 0.85f + 0.3f * rng.U(),
              r4 = 2.f * rng.U() - 1.f;
        if (rng.U() < 0.7f) rollSign = -rollSign;
        float r5 = 0.6f + 0.4f * rng.U();
        if (P.punchAlt != 0.f) {
            float sgn = (shots & 1u) ? 1.f : -1.f;
            r4 = sgn * (0.85f + 0.3f * std::fabs(r4));
            rollSign = sgn * (P.punchAlt > 0.f ? 1.f : -1.f);
            r5 = 0.85f + 0.75f * (r5 - 0.6f);
        }
        ++shots;
        auto add = [&](const Set& s, float* vp, float* vr) {
            vp[2] += Impulse(s.gunMove[0] * g * r0, ps[0], ps[1]);
            vp[1] += Impulse(s.gunMove[1] * g * r1, ps[0], ps[1]);
            vp[0] += Impulse(s.gunMove[2] * r2 * g, ps[0], ps[1]);
            vr[0] += Impulse(s.gunRot[0] * g * r3, rs[0], rs[1]);
            vr[1] += Impulse(s.gunRot[1] * r4 * g, rs[0], rs[1]);
            vr[2] += Impulse(rollSign * s.gunRot[2] * g * r5, rs[0], rs[1]);
        };
        if (P.blendMode) {
            add(P.hip, vpos, vrot);
            add(P.ads, vpos2, vrot2);
        } else {
            add(Mix(P.hip, P.ads, adsW), vpos, vrot);
        }
    }
    void Step(const Params& P, float dt) {
        StepSprings(pos, vpos, 3, P.gunPosSpring, dt);
        StepSprings(rot, vrot, 3, P.gunRotSpring, dt);
        StepSprings(pos2, vpos2, 3, P.gunPosSpring, dt);
        StepSprings(rot2, vrot2, 3, P.gunRotSpring, dt);
        auto clampTo = [](float* x, float* v, float lim) {
            for (int i = 0; i < 3; ++i)
                if (std::fabs(x[i]) > lim) x[i] = x[i] > 0 ? lim : -lim, v[i] = 0;
        };
        clampTo(pos, vpos, 0.08f);
        clampTo(rot, vrot, 15.f);
        clampTo(pos2, vpos2, 0.08f);
        clampTo(rot2, vrot2, 15.f);
    }
    void Out(const Params& P, float w, float op[3], float orot[3]) const {
        for (int i = 0; i < 3; ++i) {
            if (!P.blendMode || w <= 0.f) op[i] = pos[i], orot[i] = rot[i];
            else if (w >= 1.f) op[i] = pos2[i], orot[i] = rot2[i];
            else op[i] = pos[i] + (pos2[i] - pos[i]) * w, orot[i] = rot[i] + (rot2[i] - rot[i]) * w;
        }
    }
    void Reset() {
        for (int i = 0; i < 3; ++i) pos[i] = vpos[i] = rot[i] = vrot[i] = pos2[i] = vpos2[i] = rot2[i] = vrot2[i] = 0.f;
    }
};
struct Sustain {
    float t = 0, w = 0, idle = 0;
    void Step(const Params& P, bool firing, float dt) {
        if (!(dt >= 0.f)) return;
        if (!P.enabled || P.sustainScale <= 0.f) {
            Reset();
            return;
        }
        if (firing) {
            t += dt;
            idle = 0.f;
            w += dt / P.sustainFade[0];
            if (w > 1.f) w = 1.f;
        } else {
            idle += dt;
            w -= dt / P.sustainFade[1];
            if (w < 0.f) w = 0.f;
            if (w <= 0.f && idle >= P.sustainReset) t = 0.f;
        }
    }
    void Out(const Params& P, float adsW, float pos[3], float rot[3]) const {
        float k = w * P.sustainScale * P.gunScale * (1.f - adsW * (1.f - P.sustainAds));
        float rise = 1.f - std::exp(-t / P.sustainRise);
        float ph = 6.2831853f * t / P.sustainWander;
        pos[2] = k * P.sustainMove[0] * rise;
        pos[1] = k * P.sustainMove[1] * rise;
        pos[0] = k * P.sustainMove[2] * rise * std::sin(ph);
        rot[0] = k * P.sustainRot[0] * rise;
        rot[1] = k * P.sustainRot[1] * rise * std::sin(ph + 0.9f);
        rot[2] = k * P.sustainRot[2] * rise * std::sin(ph * 1.6f + 0.4f);
    }
    void Reset() { t = w = idle = 0.f; }
};
struct Onset {
    float t = 0, w = 0, idle = 0;
    void Step(const Params& P, bool firing, float dt) {
        if (!(dt >= 0.f)) return;
        if (!P.enabled || P.onsetScale <= 0.f) {
            Reset();
            return;
        }
        if (firing) {
            t += dt;
            idle = 0.f;
            w += dt / P.onsetFade[0];
            if (w > 1.f) w = 1.f;
        } else {
            idle += dt;
            w -= dt / P.onsetFade[1];
            if (w < 0.f) w = 0.f;
            if (w <= 0.f && idle >= P.onsetReset) t = 0.f;
        }
    }
    static float Smooth(float a, float b, float x) {
        float u = (x - a) / (b - a);
        u = u < 0.f ? 0.f : u > 1.f ? 1.f : u;
        return u * u * (3.f - 2.f * u);
    }
    static float Window(const Params& P, float t) { return 1.f - Smooth(P.onsetEnd[0], P.onsetEnd[1], t); }
    static float Rot(const Params& P, float t) {
        if (!(t > 0.f) || t >= P.onsetEnd[1]) return 0.f;
        float x = t / P.onsetPeak;
        float v = x * x * std::exp(2.f * (1.f - x));
        for (int i = 0; i < 6; i += 3) {
            float u = (t - P.onsetEcho[i]) / P.onsetEcho[i + 1];
            if (u > -1.f && u < 1.f) v += P.onsetEcho[i + 2] * 0.5f * (1.f + std::cos(3.14159265f * u));
        }
        v += 0.10f * std::exp(-t / 0.7f) * Smooth(P.onsetPeak, 3.f * P.onsetPeak, t);
        return v * Window(P, t);
    }
    static float Pos(const Params& P, float t) {
        if (!(t > 0.f) || t >= P.onsetEnd[1]) return 0.f;
        float w0 = 2.f * 3.14159265f * P.onsetPosSpring[0], a = P.onsetPosSpring[1] * w0;
        float wd = w0 * std::sqrt(1.f - P.onsetPosSpring[1] * P.onsetPosSpring[1]);
        float tp = std::atan2(wd, a) / wd;
        float norm = std::exp(-a * tp) * std::sin(wd * tp);
        return norm > 1e-6f ? std::exp(-a * t) * std::sin(wd * t) / norm * Window(P, t) : 0.f;
    }
    void Out(const Params& P, float adsW, float pos[3], float rot[3]) const {
        float ws = w * w * (3.f - 2.f * w);
        float k = ws * P.onsetScale * P.gunScale;
        float er = k > 0.f ? Rot(P, t) : 0.f, ep = k > 0.f ? Pos(P, t) : 0.f;
        for (int i = 0; i < 3; ++i) {
            pos[i] = k * ep * (P.onsetHip[i] + (P.onsetAds[i] - P.onsetHip[i]) * adsW);
            rot[i] = k * er * (P.onsetHip[i + 3] + (P.onsetAds[i + 3] - P.onsetHip[i + 3]) * adsW);
        }
    }
    void Reset() { t = w = idle = 0.f; }
};
struct Jitter {
    float p[3] = {}, x[3] = {}, v[3] = {};
    Rng rng;
    Jitter() { rng.Seed(0x2545f491u); }
    void OnShot(const Params& P, float adsW) {
        if (!P.enabled || P.jitterScale <= 0.f || P.gunScale <= 0.f) return;
        float g = P.jitterScale * P.gunScale, amp[3];
        for (int i = 0; i < 3; ++i) amp[i] = (P.jitterHip[i] + (P.jitterAds[i] - P.jitterHip[i]) * adsW) * g;
        float th = rng.U() * 6.2831853f, u = P.jitterRadial + (1.f - P.jitterRadial) * rng.U();
        float t[3] = {amp[0] * u * (P.jitterUp + (1.f - P.jitterUp) * std::cos(th)), amp[1] * u * std::sin(th), amp[2] * u * (2.f * rng.U() - 1.f)};
        for (int i = 0; i < 3; ++i) {
            p[i] = t[i];
            x[i] += (t[i] - x[i]) * P.jitterSnap;
            v[i] = 0.f;
        }
    }
    void Step(const Params& P, float dt) {
        if (!(dt > 0.f)) return;
        if (!P.enabled || P.jitterScale <= 0.f) {
            Reset();
            return;
        }
        float w = 6.2831853f * P.jitterSpring[0], k = w * w, c = 2.f * P.jitterSpring[1] * w;
        int n = static_cast<int>(std::ceil(dt / 0.004f));
        if (n < 1) n = 1;
        if (n > 64) n = 64;
        float h = dt / n, dec = std::exp(-P.jitterDecay * h);
        for (int s = 0; s < n; ++s)
            for (int i = 0; i < 3; ++i) {
                p[i] *= dec;
                float pv = -P.jitterDecay * p[i];
                v[i] = (v[i] - h * k * (x[i] - p[i]) + h * c * pv) / (1.f + h * c + h * h * k);
                x[i] += h * v[i];
                if (x[i] > 10.f) x[i] = 10.f, v[i] = 0.f;
                if (x[i] < -10.f) x[i] = -10.f, v[i] = 0.f;
            }
    }
    void Reset() {
        for (int i = 0; i < 3; ++i) p[i] = x[i] = v[i] = 0.f;
    }
};
constexpr int kRefMax = 160;
struct RefCurve {
    int n = 0;
    float step = 0.f;
    float v[kRefMax][3];
    void At(float t, float out[3]) const {
        if (n <= 0 || step <= 0.f) {
            out[0] = out[1] = out[2] = 0.f;
            return;
        }
        float x = t / step;
        int a = x <= 0.f ? 0 : (x >= n - 1 ? n - 1 : static_cast<int>(x));
        int b = a + 1 < n ? a + 1 : a;
        float f = (x <= 0.f || b == a) ? 0.f : x - a;
        for (int i = 0; i < 3; ++i) out[i] = v[a][i] + (v[b][i] - v[a][i]) * f;
    }
};
struct RefData {
    bool ok = false;
    float paUnit = 0.f;
    int paTracks = 0;
    RefCurve paPos[3], paRot[3];
    float paScale[4][8];
    int paScaleN[4] = {0, 0, 0, 0};
    float gkEveryDur = 0.f;
    RefCurve gkEvery;
    int gkFirstN = 0;
    float gkFirst[4][3];
    RefCurve gkFirstC[4];
    float rk[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    RefCurve rkRec;
    float gkPivotFwd = 0.f, rkDist = 0.f;
    void Clear() {
        ok = false, paUnit = 0.f, paTracks = 0, gkEveryDur = 0.f, gkFirstN = 0, gkPivotFwd = rkDist = 0.f;
        for (int i = 0; i < 4; ++i) paScaleN[i] = 0;
        for (float& x : rk) x = 0.f;
        for (RefCurve* c : {&paPos[0], &paPos[1], &paPos[2], &paRot[0], &paRot[1], &paRot[2], &gkEvery, &gkFirstC[0], &gkFirstC[1], &gkFirstC[2],
                           &gkFirstC[3], &rkRec})
            c->n = 0, c->step = 0.f;
    }
};
inline bool LoadRef(const wchar_t* path, RefData& d) {
    d.Clear();
    FILE* f = _wfopen(path, L"rb");
    if (!f) return false;
    char line[512];
    RefCurve* cur = nullptr;
    int need = 0;
    bool bad = false;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        if (cur && cur->n < need) {
            float a, b, c;
            if (sscanf(line, "%f %f %f", &a, &b, &c) != 3) {
                bad = true;
                break;
            }
            cur->v[cur->n][0] = a, cur->v[cur->n][1] = b, cur->v[cur->n][2] = c;
            ++cur->n;
            continue;
        }
        char key[32] = {}, name[32] = {};
        if (sscanf(line, "%31s", key) != 1) continue;
        if (!strcmp(key, "curve")) {
            float step = 0.f;
            int n = 0, idx = -1;
            if (sscanf(line, "curve %31s %f %d", name, &step, &n) != 3 || n < 2 || n > kRefMax || step <= 0.f) {
                bad = true;
                break;
            }
            cur = nullptr;
            if (sscanf(name, "pa_pos%d", &idx) == 1 && idx >= 0 && idx < 3) cur = &d.paPos[idx];
            else if (sscanf(name, "pa_rot%d", &idx) == 1 && idx >= 0 && idx < 3) cur = &d.paRot[idx];
            else if (!strcmp(name, "gk_every")) cur = &d.gkEvery;
            else if (sscanf(name, "gk_first%d", &idx) == 1 && idx >= 0 && idx < 4) cur = &d.gkFirstC[idx];
            else if (!strcmp(name, "rk_rec")) cur = &d.rkRec;
            if (!cur) {
                bad = true;
                break;
            }
            cur->n = 0, cur->step = step, need = n;
        } else if (!strcmp(key, "pa_unit")) {
            sscanf(line, "pa_unit %f", &d.paUnit);
        } else if (!strcmp(key, "pa_tracks")) {
            sscanf(line, "pa_tracks %d", &d.paTracks);
        } else if (!strcmp(key, "pa_scale")) {
            char dim[16] = {};
            int off = 0;
            if (sscanf(line, "pa_scale %15s%n", dim, &off) < 1) continue;
            int k = !strcmp(dim, "pitch") ? 0 : !strcmp(dim, "yaw") ? 1 : !strcmp(dim, "roll") ? 2 : !strcmp(dim, "right") ? 3 : -1;
            if (k < 0) continue;
            const char* p = line + off;
            int got = 0, used = 0;
            float x;
            while (got < 8 && sscanf(p, "%f%n", &x, &used) == 1) d.paScale[k][got++] = x, p += used;
            d.paScaleN[k] = got;
        } else if (!strcmp(key, "gk_every_dur")) {
            sscanf(line, "gk_every_dur %f", &d.gkEveryDur);
        } else if (!strcmp(key, "gk_first_n")) {
            sscanf(line, "gk_first_n %d", &d.gkFirstN);
        } else if (!strcmp(key, "gk_first")) {
            int i = -1;
            float a, b, c;
            if (sscanf(line, "gk_first %d %f %f %f", &i, &a, &b, &c) == 4 && i >= 0 && i < 4) d.gkFirst[i][0] = a, d.gkFirst[i][1] = b, d.gkFirst[i][2] = c;
        } else if (!strcmp(key, "rk")) {
            sscanf(line, "rk %f %f %f %f %f %f", &d.rk[0], &d.rk[1], &d.rk[2], &d.rk[3], &d.rk[4], &d.rk[5]);
        } else if (!strcmp(key, "gk_pivot_fwd")) {
            sscanf(line, "gk_pivot_fwd %f", &d.gkPivotFwd);
        } else if (!strcmp(key, "rk_dist")) {
            sscanf(line, "rk_dist %f", &d.rkDist);
        }
    }
    fclose(f);
    d.gkFirstN = d.gkFirstN < 0 ? 0 : (d.gkFirstN > 4 ? 4 : d.gkFirstN);
    d.ok = !bad && d.paTracks >= 1 && d.paTracks <= 3 && (!cur || cur->n == need);
    for (int i = 0; i < d.paTracks && d.ok; ++i) d.ok = d.paPos[i].n >= 2 && d.paRot[i].n >= 2;
    return d.ok;
}
struct RefState {
    double last = -1, burst0 = -1;
    int k = 0, shots = 0, track = 0;
    struct Kick {
        double t;
        float yaw, pitch;
    } rkI[32];
    int rkN = 0;
    Rng rng;
    void Reset() { last = burst0 = -1, k = shots = 0, rkN = 0; }
    static double Gap(double gap) { return gap * 1.5 > 0.08 ? gap * 1.5 : 0.08; }
    void OnShot(const RefData& d, double now, double gap) {
        if (last < 0 || now - last > Gap(gap)) burst0 = now, k = 0, shots = 1;
        else ++k, ++shots;
        last = now;
        track = d.paTracks > 0 ? static_cast<int>(rng.U() * d.paTracks) % d.paTracks : 0;
        if (rkN == 32) {
            for (int i = 1; i < 32; ++i) rkI[i - 1] = rkI[i];
            --rkN;
        }
        const float yr[2] = {d.rk[0], d.rk[1]}, pr[2] = {d.rk[2], d.rk[3]};
        float yaw = rng.Range(yr), pitch = rng.Range(pr);
        rkI[rkN++] = {now, yaw, pitch};
    }
    static float Scale(const RefData& d, int dim, int k) {
        int n = d.paScaleN[dim];
        return n <= 0 ? 1.f : d.paScale[dim][k < n ? k : n - 1];
    }
    void Out(const RefData& d, double now, double gap, float paPos[3], float paRot[3], float gk[3], float rkOut[3]) {
        for (int i = 0; i < 3; ++i) paPos[i] = paRot[i] = gk[i] = rkOut[i] = 0.f;
        if (last < 0) return;
        float unit = d.paUnit > 0.f ? d.paUnit : static_cast<float>(gap > 0 ? gap : 0.1);
        float v[3];
        d.paPos[track].At(static_cast<float>((now - last) / unit), paPos);
        d.paRot[track].At(static_cast<float>((now - last) / unit), paRot);
        paPos[0] *= Scale(d, 3, k);
        for (int i = 0; i < 3; ++i) paRot[i] *= Scale(d, i, k);
        float a = static_cast<float>(now - last);
        if (d.gkEveryDur > 0.f && a <= d.gkEveryDur) {
            d.gkEvery.At(a / d.gkEveryDur, v);
            for (int i = 0; i < 3; ++i) gk[i] += v[i];
        }
        if (shots >= 2 && burst0 >= 0) {
            double stop = now - last <= Gap(gap) ? 1e30 : last + gap;
            for (int j = 0; j < d.gkFirstN; ++j) {
                float dur = d.gkFirst[j][0], bin = d.gkFirst[j][1], bout = d.gkFirst[j][2];
                if (dur <= 0.f) continue;
                double t = now - burst0, end = burst0 + dur < stop ? burst0 + dur : stop;
                float w = bin > 0.f ? static_cast<float>(t / bin) : 1.f;
                w = w > 1.f ? 1.f : w;
                if (now > end) w *= bout > 0.f ? 1.f - static_cast<float>((now - end) / bout) : 0.f;
                if (w <= 0.f) continue;
                float u = static_cast<float>(t / dur);
                d.gkFirstC[j].At(u > 1.f ? 1.f : u, v);
                for (int i = 0; i < 3; ++i) gk[i] += w * v[i];
            }
        }
        float ly = d.rk[4] > 0.f ? d.rk[4] : 0.4f, lp = d.rk[5] > 0.f ? d.rk[5] : 0.4f;
        int keep = 0;
        for (int i = 0; i < rkN; ++i) {
            float age = static_cast<float>(now - rkI[i].t);
            if (age > (ly > lp ? ly : lp)) continue;
            if (age <= ly) {
                d.rkRec.At(age / ly, v);
                rkOut[1] += rkI[i].yaw * v[0];
            }
            if (age <= lp) {
                d.rkRec.At(age / lp, v);
                rkOut[0] += rkI[i].pitch * v[1];
            }
            rkI[keep++] = rkI[i];
        }
        rkN = keep;
    }
};
}
