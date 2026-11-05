namespace grip {
constexpr float kGap = 0.0015f, kMaxJoint = 50.f, kMaxReach = 65.f, kMaxOut = -30.f, kFarFull = 0.02f, kFar = 0.03f;
constexpr float kSeatMax = 0.045f, kSink = 0.003f, kHoldFull = 0.005f, kHoldFar = 0.012f, kOutMax = 0.015f;
constexpr float kPenTol = 0.005f;
constexpr float kCapD = 0.0317f;
constexpr float kTau = 0.04f, kReset = 0.25f, kShapeHyst = 0.003f;
inline void Xf(const Aff& M, const float* p, float* o) {
    for (int r = 0; r < 3; ++r) o[r] = M.m[r * 4] * p[0] + M.m[r * 4 + 1] * p[1] + M.m[r * 4 + 2] * p[2] + M.m[r * 4 + 3];
}
inline void Rot(const Aff& M, const float* p, float* o) {
    for (int r = 0; r < 3; ++r) o[r] = M.m[r * 4] * p[0] + M.m[r * 4 + 1] * p[1] + M.m[r * 4 + 2] * p[2];
}
inline float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void Cross(const float* a, const float* b, float* o) {
    o[0] = a[1] * b[2] - a[2] * b[1], o[1] = a[2] * b[0] - a[0] * b[2], o[2] = a[0] * b[1] - a[1] * b[0];
}
inline bool Norm(float* v) {
    float l = std::sqrt(Dot(v, v));
    if (l < 1e-9f) return false;
    v[0] /= l, v[1] /= l, v[2] /= l;
    return true;
}
inline float Smooth(float x) {
    x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
    return x * x * (3.f - 2.f * x);
}
inline void Ema(bool have, float* x1, float* x2, const float* t, int n, float al) {
    for (int i = 0; i < n; ++i)
        if (!have)
            x1[i] = x2[i] = t[i];
        else
            x1[i] += (t[i] - x1[i]) * al, x2[i] += (x1[i] - x2[i]) * al;
}
struct WPrim {
    const Prim* p = nullptr;
    float A[3], B[3], c[3], ax[3][3];
    Aff M, inv;
};
inline void ToWorld(const Prim& p, WPrim& w) {
    const Aff& M = g_world[p.bone];
    w.p = &p;
    if (p.box == 0) {
        Xf(M, p.a, w.A), Xf(M, p.b, w.B);
    } else if (p.box == 1) {
        Xf(M, p.c, w.c);
        for (int k = 0; k < 3; ++k) Rot(M, p.ax + k * 3, w.ax[k]);
    } else {
        w.M = M, w.inv = AffInverse(M);
    }
}
inline float Sdf(const WPrim& w, const float* q, float* cl) {
    float tmp[3];
    if (!cl) cl = tmp;
    if (w.p->box == 2) {
        const Prim& p = *w.p;
        float lq[3], g[3];
        Xf(w.inv, q, lq);
        float fx = (lq[0] - p.o[0]) / p.step, fy = (lq[1] - p.o[1]) / p.step, fz = (lq[2] - p.o[2]) / p.step;
        cl[0] = q[0], cl[1] = q[1], cl[2] = q[2];
        if (fx < 0.f || fy < 0.f || fz < 0.f || fx >= p.n[0] - 1 || fy >= p.n[1] - 1 || fz >= p.n[2] - 1) return kCapD;
        int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
        float tx = fx - ix, ty = fy - iy, tz = fz - iz, v[8];
        for (int k = 0; k < 8; ++k)
            v[k] = p.sdf[(static_cast<size_t>(iz + (k >> 2)) * p.n[1] + iy + ((k >> 1) & 1)) * p.n[0] + ix + (k & 1)] * 0.00025f;
        auto L = [](float a, float b, float t) { return a + (b - a) * t; };
        float x00 = L(v[0], v[1], tx), x10 = L(v[2], v[3], tx), x01 = L(v[4], v[5], tx), x11 = L(v[6], v[7], tx);
        float y0 = L(x00, x10, ty), y1 = L(x01, x11, ty), d = L(y0, y1, tz);
        g[0] = L(L(v[1] - v[0], v[3] - v[2], ty), L(v[5] - v[4], v[7] - v[6], ty), tz);
        g[1] = L(x10 - x00, x11 - x01, tz);
        g[2] = y1 - y0;
        if (v[0] >= 0.0317f && v[1] >= 0.0317f && v[2] >= 0.0317f && v[3] >= 0.0317f && v[4] >= 0.0317f && v[5] >= 0.0317f && v[6] >= 0.0317f &&
            v[7] >= 0.0317f)
            return kCapD;
        float gw[3];
        Rot(w.M, g, gw);
        if (Norm(gw)) cl[0] = q[0] - gw[0] * d, cl[1] = q[1] - gw[1] * d, cl[2] = q[2] - gw[2] * d;
        return d;
    }
    if (!w.p->box) {
        float ab[3] = {w.B[0] - w.A[0], w.B[1] - w.A[1], w.B[2] - w.A[2]}, ap[3] = {q[0] - w.A[0], q[1] - w.A[1], q[2] - w.A[2]};
        float l2 = Dot(ab, ab), t = l2 > 1e-12f ? Dot(ap, ab) / l2 : 0.f;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        for (int r = 0; r < 3; ++r) cl[r] = w.A[r] + ab[r] * t;
        float d[3] = {q[0] - cl[0], q[1] - cl[1], q[2] - cl[2]};
        return std::sqrt(Dot(d, d)) - w.p->r;
    }
    float rel[3] = {q[0] - w.c[0], q[1] - w.c[1], q[2] - w.c[2]}, out2 = 0.f, mx = -1e9f;
    cl[0] = w.c[0], cl[1] = w.c[1], cl[2] = w.c[2];
    for (int k = 0; k < 3; ++k) {
        float lq = Dot(rel, w.ax[k]), h = w.p->h[k], d = std::fabs(lq) - h;
        if (d > 0.f) out2 += d * d;
        if (d > mx) mx = d;
        float cq = lq < -h ? -h : (lq > h ? h : lq);
        for (int r = 0; r < 3; ++r) cl[r] += w.ax[k][r] * cq;
    }
    return std::sqrt(out2) + (mx < 0.f ? mx : 0.f);
}
inline void Recompute(const State& s, unsigned boneCount, int from) {
    std::vector<char> d(boneCount, 0);
    d[from] = 1;
    for (unsigned b = static_cast<unsigned>(from); b < boneCount; ++b) {
        int p = s.parent[b];
        if (!d[b] && (p < 0 || !d[p])) continue;
        d[b] = 1;
        g_world[b] = p >= 0 ? AffMul(g_world[p], g_local[b]) : g_local[b];
    }
}
inline void Curl(const void* who, const int armBone[2][3], const int handTag[2], unsigned boneCount) {
    std::unique_lock<std::mutex> lk(Mx(), std::try_to_lock);
    if (!lk.owns_lock()) return;
    auto it = All().find(who);
    if (it == All().end() || !it->second.ready || it->second.parent.size() < boneCount) return;
    State& s = it->second;
    LARGE_INTEGER qc, qf;
    QueryPerformanceCounter(&qc), QueryPerformanceFrequency(&qf);
    const double now = static_cast<double>(qc.QuadPart) / static_cast<double>(qf.QuadPart);
    const float dt = s.fixedDt > 0.f ? s.fixedDt : (s.lastT >= 0.0 ? static_cast<float>(now - s.lastT) : 0.f);
    s.lastT = now;
    const bool cont = dt > 0.f && dt < kReset;
    const float al = cont ? 1.f - std::exp(-dt / kTau) : 1.f;
    FILE* log = nullptr;
    if (s.logLeft > 0) {
        --s.logLeft;
        log = _wfopen(Env(L"HFPS_GRIP_LOG").c_str(), L"ab");
        if (log) fprintf(log, "@ dt=%.4f\n", dt);
    }
    const bool gun = handTag[0] >= 0;
    std::vector<WPrim> wp(s.prims.size());
    for (int sd = 0; sd < 2; ++sd) {
        Hist& H = s.hist[sd];
        if (!cont) H.have = false;
        const int sh = armBone[sd][0], el = armBone[sd][1], wr = armBone[sd][2];
        const int i1 = s.finger[sd][0].bone[0], m1 = s.finger[sd][1].bone[0], p1 = s.finger[sd][3].bone[0];
        if (sh < 0 || el < 0 || wr < 0) continue;
        float pw[3];
        Pos(g_world[wr], pw);
        float hold = 1.f;
        if (handTag[sd] >= 0) {
            float pt[3];
            Pos(g_world[handTag[sd]], pt);
            float dv[3] = {pw[0] - pt[0], pw[1] - pt[1], pw[2] - pt[2]};
            hold = Smooth((0.030f - std::sqrt(Dot(dv, dv))) / 0.025f);
        }
        if (!s.pose[sd].empty()) {
            H.have = false;
            float wt[3] = {0, 0, 0};
            bool wristMoved = false;
            for (const PoseFix& p : s.pose[sd]) {
                if (p.bone >= static_cast<int>(boneCount)) continue;
                if (p.ang != 0.f) g_local[p.bone] = AffMul(g_local[p.bone], AffAxisAngle(p.axis, p.ang * hold));
                if (p.bone == wr) {
                    for (int r = 0; r < 3; ++r) wt[r] = p.t[r] * hold;
                    wristMoved = Dot(wt, wt) > 1e-12f;
                } else {
                    g_local[p.bone].m[3] += p.t[0] * hold, g_local[p.bone].m[7] += p.t[1] * hold, g_local[p.bone].m[11] += p.t[2] * hold;
                }
            }
            if (wristMoved && s.parent[wr] >= 0) {
                Aff loc = g_local[wr];
                loc.m[3] += wt[0], loc.m[7] += wt[1], loc.m[11] += wt[2];
                const Aff want = AffMul(g_world[s.parent[wr]], loc);
                float target[3] = {want.m[3], want.m[7], want.m[11]};
                Aff S = g_world[sh], E = g_world[el], Wr = g_world[wr];
                TwoBoneIk(S, E, Wr, target);
                for (int r = 0; r < 3; ++r)
                    for (int k = 0; k < 3; ++k) Wr.m[r * 4 + k] = want.m[r * 4 + k];
                const int ps = s.parent[sh];
                g_local[sh] = ps >= 0 ? AffMul(AffInverse(g_world[ps]), S) : S;
                g_local[el] = AffMul(AffInverse(S), E);
                g_local[wr] = AffMul(AffInverse(E), Wr);
                Recompute(s, boneCount, sh);
            } else {
                Recompute(s, boneCount, wr);
            }
            if (log) fprintf(log, "%s %s pose fix %zu bones hold=%.2f\n", s.weaponDir.c_str(), sd ? "le" : "ri", s.pose[sd].size(), hold);
            continue;
        }
        if (i1 < 0 || m1 < 0 || p1 < 0 || s.prims.empty()) continue;
        const bool have = H.have;
        const bool onGrip = handTag[sd] < 0 || hold >= 0.5f;
        int ci = -1;
        float best = 1e9f, gap = 1e9f, pq[3] = {0, 0, 0}, prevRank = 1e9f, prevGap = 1e9f, prevPq[3] = {0, 0, 0};
        std::vector<float> ds;
        for (size_t k = 0; k < s.prims.size(); ++k) {
            const Prim& p = s.prims[k];
            if (p.bone >= static_cast<int>(boneCount) || p.grip != onGrip) continue;
            ToWorld(p, wp[k]);
            float dk = Sdf(wp[k], pw, nullptr), qk[3] = {pw[0], pw[1], pw[2]}, rank = dk;
            ds.clear();
            for (size_t i = 0; i < s.palmBone[sd].size(); ++i) {
                float q[3];
                Xf(g_world[s.palmBone[sd][i]], &s.palmP[sd][i * 3], q);
                float dd = Sdf(wp[k], q, nullptr);
                ds.push_back(dd);
                if (i == 0 || dd < dk) dk = dd, qk[0] = q[0], qk[1] = q[1], qk[2] = q[2];
            }
            if (!ds.empty()) {
                std::nth_element(ds.begin(), ds.begin() + ds.size() * 3 / 10, ds.end());
                rank = ds[ds.size() * 3 / 10];
            }
            if (static_cast<int>(k) == H.shape) prevRank = rank, prevGap = dk, prevPq[0] = qk[0], prevPq[1] = qk[1], prevPq[2] = qk[2];
            if (rank < best) best = rank, gap = dk, ci = static_cast<int>(k), pq[0] = qk[0], pq[1] = qk[1], pq[2] = qk[2];
        }
        if (have && prevRank < 1e8f && prevRank <= best + kShapeHyst)
            ci = H.shape, gap = prevGap, pq[0] = prevPq[0], pq[1] = prevPq[1], pq[2] = prevPq[2];
        H.shape = ci;
        float tO[3] = {0, 0, 0}, tA[4][3] = {}, pre[4] = {1e9f, 1e9f, 1e9f, 1e9f}, weight = 0.f, seatT = 0.f;
        float deep = 1e9f, p30 = 1e9f, contact = 1e9f;
        if (ci >= 0) {
            const WPrim& W = wp[ci];
            weight = W.p->grip ? hold : (handTag[sd] >= 0 ? 1.f - hold : 1.f);
            float pc[3] = {pq[0], pq[1], pq[2]};
            contact = gap, p30 = gap;
            for (int f = 0; f < 4; ++f) {
                const Finger& F = s.finger[sd][f];
                for (size_t i = 0; F.bone[0] >= 0 && i < F.seg.size(); ++i) {
                    float q[3];
                    Xf(g_world[F.bone[F.seg[i]]], &F.p[i * 3], q);
                    float dd = Sdf(W, q, nullptr);
                    if (dd < pre[f]) pre[f] = dd;
                }
                if (!(gun && sd == 0 && f == 0) && pre[f] < contact) contact = pre[f];
            }
            if (!s.palmBone[sd].empty()) {
                ds.clear();
                pc[0] = pc[1] = pc[2] = 0.f;
                for (size_t i = 0; i < s.palmBone[sd].size(); ++i) {
                    float q[3];
                    Xf(g_world[s.palmBone[sd][i]], &s.palmP[sd][i * 3], q);
                    ds.push_back(Sdf(W, q, nullptr));
                    for (int r = 0; r < 3; ++r) pc[r] += q[r] / s.palmBone[sd].size();
                }
                std::nth_element(ds.begin(), ds.begin() + ds.size() * 3 / 10, ds.end());
                p30 = ds[ds.size() * 3 / 10];
            }
            float dq[3] = {pq[0], pq[1], pq[2]};
            deep = gap;
            for (size_t i = 0; i < s.chkBone[sd].size(); ++i) {
                float q[3];
                Xf(g_world[s.chkBone[sd][i]], &s.chkP[sd][i * 3], q);
                float dd = Sdf(W, q, nullptr);
                if (dd < deep) deep = dd, dq[0] = q[0], dq[1] = q[1], dq[2] = q[2];
            }
            const float holding = Smooth((kHoldFar - contact) / (kHoldFar - kHoldFull));
            float dir[3] = {0, 0, 0}, cl[3];
            if (deep < -kSink) {
                Sdf(W, dq, cl);
                for (int r = 0; r < 3; ++r) dir[r] = cl[r] - dq[r];
                if (Norm(dir)) seatT = -(-deep - kSink < kOutMax ? -deep - kSink : kOutMax) * weight;
            } else if (p30 > kGap && holding > 0.f) {
                const float dc = Sdf(W, pc, cl);
                for (int r = 0; r < 3; ++r) dir[r] = cl[r] - pc[r];
                if (Norm(dir)) {
                    float lim = deep + kSink < kSeatMax ? deep + kSink : kSeatMax, amt = p30 - kGap < lim ? p30 - kGap : lim;
                    if (amt > 0.f) seatT = amt * holding * weight * Smooth((kCapD - dc) / 0.004f);
                }
            }
            for (int r = 0; r < 3; ++r) tO[r] = dir[r] * (seatT < 0.f ? -seatT : seatT);
            H.keep = -1;
            for (int b = W.p->bone; b >= 0; b = s.parent[b])
                if (s.parent[b] == wr) {
                    H.keep = b;
                    break;
                }
        }
        Ema(have, H.o1, H.o2, tO, 3, al);
        if (Dot(H.o2, H.o2) > 1e-12f) {
            const int keep = H.keep;
            const Aff keepW = keep >= 0 ? g_world[keep] : Aff();
            float target[3] = {pw[0] + H.o2[0], pw[1] + H.o2[1], pw[2] + H.o2[2]};
            Aff S = g_world[sh], E = g_world[el], Wr = g_world[wr];
            const Aff rot = Wr;
            TwoBoneIk(S, E, Wr, target);
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k) Wr.m[r * 4 + k] = rot.m[r * 4 + k];
            const int ps = s.parent[sh];
            g_local[sh] = ps >= 0 ? AffMul(AffInverse(g_world[ps]), S) : S;
            g_local[el] = AffMul(AffInverse(S), E);
            g_local[wr] = AffMul(AffInverse(E), Wr);
            if (keep >= 0) g_local[keep] = AffMul(AffInverse(Wr), keepW);
            Recompute(s, boneCount, sh);
            if (ci >= 0) ToWorld(*wp[ci].p, wp[ci]);
        }
        Pos(g_world[wr], pw);
        float pi[3], pm[3], pp[3], n[3], L[3], e1[3], e2[3], fwd[3], t3[3];
        Pos(g_world[i1], pi), Pos(g_world[m1], pm), Pos(g_world[p1], pp);
        for (int k = 0; k < 3; ++k) e1[k] = pi[k] - pw[k], e2[k] = pp[k] - pw[k], L[k] = pi[k] - pp[k], fwd[k] = pm[k] - pw[k];
        Cross(e1, e2, n);
        const bool axOk = Norm(n) && Norm(L) && Norm(fwd);
        if (axOk) {
            for (int r = 0; r < 3; ++r) n[r] *= s.palmSign[sd];
            Cross(L, fwd, t3);
            if (Dot(t3, n) < 0) L[0] = -L[0], L[1] = -L[1], L[2] = -L[2];
        }
        if (log && ci >= 0)
            fprintf(log, "%s %s palm gap=%.1fmm deep=%.1fmm p30=%.1fmm contact=%.1fmm seat=%.1fmm applied=%.1fmm (%zu pts) shape=%s:%s hold=%.2f\n",
                    s.weaponDir.c_str(), sd ? "le" : "ri", gap * 1000.f, deep * 1000.f, p30 * 1000.f, contact * 1000.f, seatT * 1000.f,
                    std::sqrt(Dot(H.o2, H.o2)) * 1000.f, s.palmBone[sd].size(), wp[ci].p->box == 2 ? "grid" : (wp[ci].p->box ? "box" : "cap"),
                    s.names[wp[ci].p->bone].c_str(), hold);
        float axL[4][3][3];
        for (int f = 0; f < 4 && axOk; ++f) {
            const Finger& F = s.finger[sd][f];
            if (F.bone[0] < 0) continue;
            for (int k = 0; k < 3; ++k) {
                const float* m = g_world[F.bone[k]].m;
                for (int r = 0; r < 3; ++r) axL[f][k][r] = m[0 * 4 + r] * L[0] + m[1 * 4 + r] * L[1] + m[2 * 4 + r] * L[2];
                Norm(axL[f][k]);
            }
        }
        for (int f = 0; f < 4 && axOk && ci >= 0; ++f) {
            const Finger& F = s.finger[sd][f];
            if (F.bone[0] < 0 || F.seg.empty() || (gun && sd == 0 && f == 0)) continue;
            const WPrim& W = wp[ci];
            const Aff base = g_world[s.parent[F.bone[0]]];
            auto evalJ = [&](const float* a, int kmin) {
                Aff Wc = base;
                float best = 1e9f;
                for (int k = 0; k < 3; ++k) {
                    Wc = AffMul(Wc, AffMul(g_local[F.bone[k]], AffAxisAngle(axL[f][k], a[k] * 0.0174532925f)));
                    if (k < kmin) continue;
                    for (size_t i = 0; i < F.seg.size(); ++i)
                        if (F.seg[i] == k) {
                            float q[3];
                            Xf(Wc, &F.p[i * 3], q);
                            float dd = Sdf(W, q, nullptr);
                            if (dd < best) best = dd;
                        }
                }
                return best;
            };
            float th[3] = {0.f, 0.f, 0.f};
            const float d0 = evalJ(th, 0);
            std::vector<float> frac(F.seg.size(), 1.f);
            {
                float o[3][3];
                for (int k = 0; k < 3; ++k) Pos(g_world[F.bone[k]], o[k]);
                for (size_t i = 0; i < F.seg.size(); ++i) {
                    const int k = F.seg[i];
                    const float* a = o[k < 2 ? k : 1];
                    const float* b = k < 2 ? o[k + 1] : o[2];
                    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, q[3];
                    Xf(g_world[F.bone[k]], &F.p[i * 3], q);
                    float l2 = Dot(ab, ab);
                    float aq[3] = {q[0] - o[k][0], q[1] - o[k][1], q[2] - o[k][2]};
                    frac[i] = l2 > 1e-10f ? Dot(aq, ab) / l2 : 1.f;
                }
            }
            float tgt = pre[f] < kGap ? pre[f] : kGap;
            if (tgt < -kPenTol) tgt = -kPenTol;
            if (d0 < kFar) {
                const float fade = Smooth((kFar - d0) / (kFar - kFarFull));
                for (int k = 0; k < 3; ++k) {
                    auto own = [&](float deg) {
                        float a[3] = {th[0], th[1], th[2]};
                        a[k] = deg;
                        Aff Wc = base;
                        float best = 1e9f;
                        for (int j = 0; j < 3 && j <= k + 1; ++j) {
                            Wc = AffMul(Wc, AffMul(g_local[F.bone[j]], AffAxisAngle(axL[f][j], a[j] * 0.0174532925f)));
                            if (j != k && j != k + 1) continue;
                            for (size_t i = 0; i < F.seg.size(); ++i) {
                                if (F.seg[i] != j) continue;
                                const bool nearJoint = frac[i] < 0.3f;
                                if ((j == k && nearJoint && k > 0) || (j == k + 1 && !nearJoint)) continue;
                                float q[3];
                                Xf(Wc, &F.p[i * 3], q);
                                float dd = Sdf(W, q, nullptr);
                                if (dd < best) best = dd;
                            }
                        }
                        return best;
                    };
                    auto rest = [&](float deg) {
                        float a[3] = {th[0], th[1], th[2]};
                        a[k] = deg;
                        return evalJ(a, k);
                    };
                    const float o0 = own(0.f);
                    if (o0 > 1e8f) continue;
                    if (o0 < tgt) {
                        if (own(kMaxOut) < tgt) {
                            th[k] = kMaxOut;
                        } else {
                            float lo = kMaxOut, hi = 0.f;
                            for (int i = 0; i < 14; ++i) {
                                float m = 0.5f * (lo + hi);
                                (own(m) < tgt ? hi : lo) = m;
                            }
                            th[k] = lo;
                        }
                    } else if (rest(0.f) > kGap && rest(kMaxReach) <= kGap) {
                        float lo = 0.f, hi = kMaxReach;
                        for (int i = 0; i < 14; ++i) {
                            float m = 0.5f * (lo + hi);
                            (rest(m) > kGap ? lo : hi) = m;
                        }
                        th[k] = (hi < kMaxJoint ? hi : kMaxJoint) * fade * Smooth((kMaxReach - hi) / (kMaxReach - kMaxJoint));
                    }
                }
            }
            for (int k = 0; k < 3; ++k) tA[f][k] = th[k] * weight;
            if (log) fprintf(log, "%s %s f%d d0=%.1fmm th=%.1f/%.1f/%.1f\n", s.weaponDir.c_str(), sd ? "le" : "ri", f, d0 * 1000.f, th[0], th[1], th[2]);
        }
        bool bent = false;
        for (int f = 0; f < 4; ++f) {
            const Finger& F = s.finger[sd][f];
            Ema(have, H.a1[f], H.a2[f], tA[f], 3, al);
            if (!axOk || F.bone[0] < 0) continue;
            const float* a = H.a2[f];
            if (std::fabs(a[0]) < 1e-3f && std::fabs(a[1]) < 1e-3f && std::fabs(a[2]) < 1e-3f) continue;
            for (int k = 0; k < 3; ++k) g_local[F.bone[k]] = AffMul(g_local[F.bone[k]], AffAxisAngle(axL[f][k], a[k] * 0.0174532925f));
            bent = true;
        }
        if (log)
            for (int f = 0; f < 4; ++f)
                fprintf(log, "%s %s a%d %.1f/%.1f/%.1f\n", s.weaponDir.c_str(), sd ? "le" : "ri", f, H.a2[f][0], H.a2[f][1], H.a2[f][2]);
        if (bent) Recompute(s, boneCount, wr);
        H.have = true;
    }
    if (log) fclose(log);
}
}
