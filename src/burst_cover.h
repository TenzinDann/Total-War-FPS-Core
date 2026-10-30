#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
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
    if (cut) {
        float u = fan > 0.f ? (ang + fan) / (2.f * fan) * static_cast<float>(kRays - 1) : 0.5f * static_cast<float>(kRays - 1);
        if (u < 0.f) u = 0.f;
        if (u > static_cast<float>(kRays - 1)) u = static_cast<float>(kRays - 1);
        const int i0 = static_cast<int>(u), i1 = i0 + 1 < kRays ? i0 + 1 : i0;
        const float c = cut[i0] + (cut[i1] - cut[i0]) * (u - static_cast<float>(i0));
        if (hd > c + kMargin) return kBlocked;
    }
    const float yc = hd * slope, band = kBand0 + kBandK * hd;
    if (yc < vy - band || yc > vy + kBody + band) return kHeight;
    return kIn;
}
struct BurnLog {
    static constexpr int kSlots = 4096, kProbe = 32;
    static constexpr double kStale = 5.0, kNever = -1e9;
    uintptr_t key[kSlots] = {};
    double t[kSlots] = {};
    static unsigned Hash(uintptr_t e) { return static_cast<unsigned>(((e >> 4) * 0x9E3779B97F4A7C15ull) >> 40) & (kSlots - 1); }
    double Last(uintptr_t e, double now) const {
        for (unsigned i = Hash(e), n = 0; n < kProbe; ++n, i = (i + 1) & (kSlots - 1)) {
            if (!key[i]) break;
            if (key[i] == e) return now - t[i] < kStale ? t[i] : kNever;
        }
        return kNever;
    }
    void Mark(uintptr_t e, double now) {
        int spare = -1;
        for (unsigned i = Hash(e), n = 0; n < kProbe; ++n, i = (i + 1) & (kSlots - 1)) {
            if (key[i] == e) {
                t[i] = now;
                return;
            }
            if (spare < 0 && (!key[i] || now - t[i] >= kStale)) spare = static_cast<int>(i);
            if (!key[i]) break;
        }
        if (spare >= 0) key[spare] = e, t[spare] = now;
    }
    void Clear() {
        for (int i = 0; i < kSlots; ++i) key[i] = 0, t[i] = 0.0;
    }
};
inline int PickDue(const double* last, int n, double now, double due, int cap, int* out, int* nDue) {
    int k = 0;
    for (int i = 0; i < n; ++i)
        if (now - last[i] >= due) out[k++] = i;
    if (nDue) *nDue = k;
    if (cap < 0) cap = 0;
    const auto older = [last](int a, int b) { return last[a] < last[b] || (last[a] == last[b] && a < b); };
    if (k > cap) {
        std::partial_sort(out, out + cap, out + k, older);
        return cap;
    }
    std::sort(out, out + k, older);
    return k;
}
}
