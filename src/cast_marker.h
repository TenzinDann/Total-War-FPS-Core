#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
namespace cm {
constexpr int kMaxPts = 768;
constexpr int kMaxLines = 12;
constexpr int kMaxTris = 512;
enum Tone : unsigned char { kWhite = 0, kPurple, kOrange, kRed, kGrey, kTones };
enum Shape { kNone = 0, kRing = 1, kLane = 2, kLaneFromHero = 3, kPoint = 4, kSelfRing = 5 };
constexpr float kCastMargin = 0.95f;
constexpr float kCastFar = 300.f;
struct Line {
    int first, count;
    bool closed;
    unsigned char tone;
};
struct Frame {
    bool on = false;
    int nPts = 0;
    float pts[kMaxPts][3];
    int nLines = 0;
    Line lines[kMaxLines];
    int nTris = 0;
    uint16_t tris[kMaxTris][3];
    unsigned char triTone[kMaxTris];
    bool pillar = false;
    float pillarAt[3] = {};
    float pillarH = 0;
    unsigned char pillarTone = kWhite;
    bool chevron = false;
    float chevronAt[3] = {};
    unsigned char chevronTone = kRed;
};
typedef bool (*HeightFn)(void* ctx, float x, float z, float* h);
struct Aim {
    bool ok = false;
    float p[3] = {};
    float dist = 0;
    bool edge = false;
    int queries = 0;
};
inline Aim AimGround(const float eye[3], const float fwd[3], const float hero[3], float range, HeightFn hf, void* ctx) {
    Aim a;
    const float reach = range > 0.f ? range * kCastMargin : kCastFar;
    auto height = [&](float x, float z, float* h) {
        ++a.queries;
        return hf(ctx, x, z, h);
    };
    auto below = [&](float t) {
        float h;
        return height(eye[0] + fwd[0] * t, eye[2] + fwd[2] * t, &h) && eye[1] + fwd[1] * t <= h;
    };
    float lo = 0.f, hit = -1.f;
    for (float t = 2.f; t < reach + 80.f;) {
        if (below(t)) {
            hit = t;
            break;
        }
        lo = t;
        t += t < 50.f ? 2.f : t * 0.04f;
    }
    float x, z;
    if (hit > 0.f) {
        for (int k = 0; k < 7; ++k) {
            float mid = (lo + hit) * 0.5f;
            if (below(mid))
                hit = mid;
            else
                lo = mid;
        }
        x = eye[0] + fwd[0] * hit, z = eye[2] + fwd[2] * hit;
    } else {
        float hl = std::sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        if (hl < 1e-3f) return a;
        x = hero[0] + fwd[0] / hl * reach, z = hero[2] + fwd[2] / hl * reach;
    }
    bool edge = hit <= 0.f;
    float y = 0.f;
    for (int k = 0; k < 4; ++k) {
        if (!height(x, z, &y)) y = k == 0 ? hero[1] : y;
        float dx = x - hero[0], dy = y - hero[1], dz = z - hero[2];
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d <= reach + 0.01f) {
            a.ok = true, a.p[0] = x, a.p[1] = y, a.p[2] = z, a.dist = d, a.edge = edge;
            return a;
        }
        float s = reach / d * 0.995f;
        x = hero[0] + dx * s, z = hero[2] + dz * s, edge = true;
    }
    if (!height(x, z, &y)) y = hero[1];
    float dx = x - hero[0], dy = y - hero[1], dz = z - hero[2];
    a.ok = true, a.p[0] = x, a.p[1] = y, a.p[2] = z, a.dist = std::sqrt(dx * dx + dy * dy + dz * dz), a.edge = true;
    return a;
}
struct Req {
    int shape = kNone;
    float r0 = 0, r1 = 0, len = 0;
    float range = 0;
    bool over = false;
    bool unit = false;
    float unitCentre[3] = {};
    float unitSpread = 0;
    float unitTop = 0;
    unsigned char unitTone = kRed;
    float unitRing = 0;
};
struct Style {
    float pillarH = 6.f;
    float centerMark = 0.7f;
    float chevronLift = 2.4f;
};
inline int RingSegs(float r) {
    int n = static_cast<int>(r * 4.f);
    return n < 48 ? 48 : n > 128 ? 128 : n;
}
class Builder {
public:
    Builder(Frame& f, HeightFn hf, void* ctx) : f_(f), hf_(hf), ctx_(ctx) {}
    int queries = 0;
    float H(float x, float z, float fallback) {
        float h = fallback;
        ++queries;
        if (!hf_(ctx_, x, z, &h) || !std::isfinite(h)) h = fallback;
        return h;
    }
    int PtH(float x, float h, float z) {
        if (f_.nPts >= kMaxPts) return -1;
        float* p = f_.pts[f_.nPts];
        p[0] = x, p[1] = h + 0.05f, p[2] = z;
        return f_.nPts++;
    }
    int Pt(float x, float z, float fallback) { return f_.nPts < kMaxPts ? PtH(x, H(x, z, fallback), z) : -1; }
    int PtAt(const float* q) {
        if (f_.nPts >= kMaxPts) return -1;
        memcpy(f_.pts[f_.nPts], q, sizeof(float) * 3);
        return f_.nPts++;
    }
    void LineOf(int first, int count, bool closed, unsigned char tone) {
        if (count < 2 || first < 0 || f_.nLines >= kMaxLines) return;
        f_.lines[f_.nLines++] = {first, count, closed, tone};
    }
    void Tri(int a, int b, int c, unsigned char tone) {
        if (a < 0 || b < 0 || c < 0 || f_.nTris >= kMaxTris) return;
        f_.tris[f_.nTris][0] = static_cast<uint16_t>(a), f_.tris[f_.nTris][1] = static_cast<uint16_t>(b), f_.tris[f_.nTris][2] = static_cast<uint16_t>(c);
        f_.triTone[f_.nTris++] = tone;
    }
    static constexpr int kRingHeights = 24;
    void Ring(const float c[3], float r, unsigned char tone, bool fill = true) {
        if (!(r > 0.05f)) return;
        const int n = RingSegs(r);
        if (f_.nPts + n + 2 > kMaxPts) return;
        const int nh = n < kRingHeights ? n : kRingHeights;
        float hs[kRingHeights];
        for (int j = 0; j < nh; ++j) {
            float ang = 6.2831853f * static_cast<float>(j) / static_cast<float>(nh);
            hs[j] = H(c[0] + r * std::sin(ang), c[2] + r * std::cos(ang), c[1]);
        }
        int centre = fill ? PtAt(c) : -1;
        int first = f_.nPts;
        for (int i = 0; i < n; ++i) {
            float ang = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
            float u = static_cast<float>(i) * nh / n;
            int j0 = static_cast<int>(u);
            float fr = u - static_cast<float>(j0);
            j0 %= nh;
            PtH(c[0] + r * std::sin(ang), hs[j0] + (hs[(j0 + 1) % nh] - hs[j0]) * fr, c[2] + r * std::cos(ang));
        }
        LineOf(first, n, true, tone);
        if (fill)
            for (int i = 0; i < n; ++i) Tri(centre, first + i, first + (i + 1) % n, tone);
    }
    void Diamond(const float c[3], float s, unsigned char tone) {
        if (f_.nPts + 5 > kMaxPts) return;
        int centre = PtAt(c);
        int first = f_.nPts;
        const float off[4][2] = {{0, s}, {s, 0}, {0, -s}, {-s, 0}};
        for (const auto& o : off) Pt(c[0] + o[0], c[2] + o[1], c[1]);
        LineOf(first, 4, true, tone);
        for (int i = 0; i < 4; ++i) Tri(centre, first + i, first + (i + 1) % 4, tone);
    }
    void Lane(const float s[3], float dx, float dz, float len, float r0, float r1, unsigned char tone) {
        const int m = len < 20.f ? 10 : len < 60.f ? 16 : 24;
        if (f_.nPts + 2 * (m + 1) + 8 > kMaxPts) return;
        const float px = dz, pz = -dx;
        auto halfw = [&](float k) {
            float w = r0 + (r1 - r0) * k;
            return w < 0.6f ? 0.6f : w;
        };
        int left = f_.nPts;
        for (int i = 0; i <= m; ++i) {
            float k = static_cast<float>(i) / m, t = len * k, w = halfw(k);
            Pt(s[0] + dx * t - px * w, s[2] + dz * t - pz * w, s[1]);
        }
        int right = f_.nPts;
        for (int i = 0; i <= m; ++i) {
            float k = static_cast<float>(i) / m, t = len * k, w = halfw(k);
            Pt(s[0] + dx * t + px * w, s[2] + dz * t + pz * w, s[1]);
        }
        float we = halfw(1.f), wa = we * 1.6f + 0.5f, tipLen = we * 1.4f + 1.f;
        float ex = s[0] + dx * len, ez = s[2] + dz * len;
        int aL = Pt(ex - px * wa, ez - pz * wa, s[1]);
        int tip = Pt(ex + dx * tipLen, ez + dz * tipLen, s[1]);
        int aR = Pt(ex + px * wa, ez + pz * wa, s[1]);
        for (int i = 0; i < m; ++i) {
            Tri(left + i, left + i + 1, right + i, tone);
            Tri(right + i, left + i + 1, right + i + 1, tone);
        }
        Tri(aL, tip, aR, tone);
        if (f_.nPts + 2 * (m + 1) + 3 > kMaxPts) return;
        int first = f_.nPts;
        for (int i = 0; i <= m; ++i) PtAt(f_.pts[left + i]);
        PtAt(f_.pts[aL]), PtAt(f_.pts[tip]), PtAt(f_.pts[aR]);
        for (int i = m; i >= 0; --i) PtAt(f_.pts[right + i]);
        LineOf(first, f_.nPts - first, true, tone);
    }
private:
    Frame& f_;
    HeightFn hf_;
    void* ctx_;
};
inline bool Build(Frame& f, const Req& q, const Aim& aim, const float hero[3], const Style& st, HeightFn hf, void* ctx, int* queries = nullptr) {
    f.on = false, f.nPts = f.nLines = f.nTris = 0, f.pillar = f.chevron = false;
    Builder b(f, hf, ctx);
    const unsigned char tone = q.over ? kPurple : kWhite;
    const bool ground = q.shape == kRing || q.shape == kLane || q.shape == kLaneFromHero || q.shape == kPoint;
    if (ground && aim.ok) {
        const unsigned char gt = aim.edge ? static_cast<unsigned char>(kOrange) : tone;
        float dx = aim.p[0] - hero[0], dz = aim.p[2] - hero[2], dl = std::sqrt(dx * dx + dz * dz);
        if (dl > 1e-3f)
            dx /= dl, dz /= dl;
        else
            dx = 0.f, dz = 1.f;
        if (q.shape == kRing) {
            b.Ring(aim.p, q.r0, gt);
        } else if (q.shape == kLane) {
            if (q.r0 >= 1.f) b.Ring(aim.p, q.r0, gt);
            b.Lane(aim.p, dx, dz, q.len, q.r0, q.r1, gt);
        } else if (q.shape == kLaneFromHero) {
            float s[3] = {hero[0] + dx * 1.5f, hero[1], hero[2] + dz * 1.5f};
            b.Lane(s, dx, dz, q.len, q.r0, q.r1, gt);
        }
        b.Diamond(aim.p, st.centerMark, gt);
        f.pillar = true, f.pillarTone = gt, f.pillarH = st.pillarH;
        memcpy(f.pillarAt, aim.p, sizeof f.pillarAt);
    } else if (q.shape == kSelfRing && q.r0 > 0.f) {
        b.Ring(hero, q.r0, tone);
    }
    if (q.unit) {
        if (q.unitRing > 0.f)
            b.Ring(q.unitCentre, q.unitRing, tone);
        else
            b.Ring(q.unitCentre, q.unitSpread + 1.5f, q.unitTone, false);
        f.chevron = true, f.chevronTone = q.unitTone;
        f.chevronAt[0] = q.unitCentre[0], f.chevronAt[1] = q.unitTop + st.chevronLift, f.chevronAt[2] = q.unitCentre[2];
    }
    if (queries) *queries = b.queries;
    f.on = f.nLines > 0 || f.pillar || f.chevron;
    return f.on;
}
}
