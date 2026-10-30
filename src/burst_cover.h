#pragma once
#include <cmath>
namespace cover {
constexpr int kRays = 7;
constexpr float kMargin = 1.0f;
constexpr float kBand0 = 1.0f, kBandK = 0.05f;
constexpr float kBody = 2.1f;
enum Result { kIn = 0, kBehind, kTooFar, kOutOfFan, kBlocked, kHeight };
inline Result Test(float cx, float cz, float rx, float rz, float slope, float fan, float hmax, const float* cut, float vx, float vy, float vz) {
    const float hd = std::sqrt(vx * vx + vz * vz);
    const float along = vx * cx + vz * cz;
    if (along <= 0.f || hd < 0.3f) return kBehind;
    if (hd > hmax + kMargin) return kTooFar;
    const float ang = std::atan2(vx * rx + vz * rz, along);
    const float slack = hd > kMargin ? std::asin(kMargin / hd) : 1.5707963f;
    if (std::fabs(ang) > fan + slack) return kOutOfFan;
    float u = fan > 0.f ? (ang + fan) / (2.f * fan) * static_cast<float>(kRays - 1) : 0.5f * static_cast<float>(kRays - 1);
    if (u < 0.f) u = 0.f;
    if (u > static_cast<float>(kRays - 1)) u = static_cast<float>(kRays - 1);
    const int i0 = static_cast<int>(u), i1 = i0 + 1 < kRays ? i0 + 1 : i0;
    const float c = cut[i0] + (cut[i1] - cut[i0]) * (u - static_cast<float>(i0));
    if (hd > c + kMargin) return kBlocked;
    const float yc = hd * slope, band = kBand0 + kBandK * hd;
    if (yc < vy - band || yc > vy + kBody + band) return kHeight;
    return kIn;
}
inline int Rotate(int n, int cap, unsigned* rot, int* out) {
    if (n <= 0 || cap <= 0) return 0;
    if (n <= cap) {
        for (int i = 0; i < n; ++i) out[i] = i;
        return n;
    }
    const int s = static_cast<int>(*rot % static_cast<unsigned>(n));
    for (int i = 0; i < cap; ++i) out[i] = (s + i) % n;
    *rot += static_cast<unsigned>(cap);
    return cap;
}
}
