#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace gamevol {
constexpr uintptr_t oChannels = 0x548;
constexpr int kStride = 12, kChannels = 8, kChMaster = 7, kChSfx = 1;
struct Records {
    unsigned char b[kChannels * kStride];
};
struct Channel {
    int on, volume, scale;
};
inline Channel At(const Records& r, int ch) {
    const unsigned char* p = r.b + ch * kStride;
    Channel c;
    c.on = p[0];
    memcpy(&c.volume, p + 4, 4);
    memcpy(&c.scale, p + 8, 4);
    return c;
}
inline bool Plausible(const Channel& c) {
    return (c.on == 0 || c.on == 1) && c.volume >= 0 && c.volume <= 100 && c.scale >= 0 && c.scale <= 1000;
}
inline float Gain(const Channel& c) {
    if (!c.on) return 0.f;
    float v = static_cast<float>(c.volume) * (static_cast<float>(c.scale) * 0.01f);
    return v <= 0.f ? 0.f : v >= 100.f ? 1.f : v * 0.01f;
}
inline int Read(const Records& r, Channel* m, Channel* s, float* factor) {
    *m = At(r, kChMaster);
    *s = At(r, kChSfx);
    if (!Plausible(*m) || !Plausible(*s)) return 4;
    *factor = Gain(*m) * Gain(*s);
    return 0;
}
inline const char* FailName(int fail) {
    switch (fail) {
    case 0: return "ok";
    case 1: return "audio manager not created";
    case 2: return "audio manager vtable mismatch";
    case 3: return "channel records unreadable";
    case 4: return "channel values out of range";
    default: return "?";
    }
}
inline float Db(float g) { return g > 0.f ? 20.f * std::log10(g) : -999.f; }
inline void Describe(const Channel& m, const Channel& s, float factor, float baseDb, char* out, size_t n) {
    char a[48], b[48];
    const Channel* cs[2] = {&m, &s};
    char* bufs[2] = {a, b};
    for (int i = 0; i < 2; ++i) {
        const Channel& c = *cs[i];
        int k = snprintf(bufs[i], 48, "%d", c.volume);
        if (!c.on) k += snprintf(bufs[i] + k, 48 - k, " (muted)");
        if (c.scale != 100) snprintf(bufs[i] + k, 48 - k, " (script %d)", c.scale);
    }
    if (factor > 0.f)
        snprintf(out, n, "game volume master %s sfx %s -> x%.3f (%.1f dB), first-person total %.1f dB", a, b, factor, Db(factor),
                 baseDb + Db(factor));
    else
        snprintf(out, n, "game volume master %s sfx %s -> x0 (silent), first-person sounds muted", a, b);
}
}
