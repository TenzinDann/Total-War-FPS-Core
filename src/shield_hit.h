#pragma once
#include <cmath>
#include <cstdint>
namespace shieldhit {
inline bool Full(float f) { return f >= 0.999f; }
inline bool Frontal(float yaw, float dx, float dz, float halfDeg, float* angleDeg = nullptr) {
    float len = std::sqrt(dx * dx + dz * dz);
    float ang = 0.f;
    if (len > 1e-4f) {
        float c = (dx * std::sin(yaw) + dz * std::cos(yaw)) / len;
        c = c < -1.f ? -1.f : (c > 1.f ? 1.f : c);
        ang = std::acos(c) * 57.29578f;
    }
    if (angleDeg) *angleDeg = ang;
    return halfDeg >= 180.f || len <= 1e-4f || ang <= halfDeg;
}
enum MeleeKind { kMiss = 0, kHit = 1, kNone = 2, kKnockBack = 3, kKnockDown = 4, kKnockFly = 5 };
struct MeleeRewrite {
    int kind;
    uint32_t damage;
    bool feedback;
    bool partial;
};
inline MeleeRewrite RewriteMelee(int kind, uint32_t damage, float f) {
    bool known = kind == kMiss || kind == kHit || kind == kKnockBack || kind == kKnockDown || kind == kKnockFly;
    if (!(f > 0.f) || !known) return {kind, damage, false, false};
    if (Full(f)) return {kNone, 0u, true, false};
    if (kind == kMiss) return {kind, damage, true, false};
    double d = static_cast<double>(damage) * (1.0 - f);
    return {kind, static_cast<uint32_t>(std::lround(d < 0 ? 0 : d)), true, true};
}
inline void ScaleProjectile(float* m4, float f) {
    float k = 1.f - (f < 0.f ? 0.f : (f > 1.f ? 1.f : f));
    for (int i = 0; i < 4; ++i) m4[i] *= k;
}
inline uint32_t Refund(uint32_t hpPrev, uint32_t hpNow, uint32_t exempt, float f, uint32_t hpMax) {
    if (!(f > 0.f) || hpNow == 0 || hpNow >= hpPrev) return 0;
    uint32_t drop = hpPrev - hpNow;
    drop = drop > exempt ? drop - exempt : 0;
    if (!drop) return 0;
    double ff = f > 1.f ? 1.0 : static_cast<double>(f);
    uint32_t r = static_cast<uint32_t>(std::lround(drop * ff));
    uint32_t room = hpMax > hpNow ? hpMax - hpNow : 0;
    return r < room ? r : room;
}
inline bool EatStagger(float f, bool staggerOnPartial) { return Full(f) || (f > 0.f && !staggerOnPartial); }
}
