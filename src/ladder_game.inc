namespace ladder {
constexpr float kModelLen = 17.875f;
constexpr float kRung0 = 0.377f, kRungStep = 0.4204f;
constexpr float kRaise[25] = {-74.8743f, -74.5236f, -73.6747f, -72.0758f, -69.9739f, -67.1308f, -63.6889f, -59.3931f, -54.4203f,
                              -48.4106f, -41.5197f, -33.2648f, -23.9437f, -12.4163f, 0.3548f, 12.4733f, 15.8209f, 15.6668f,
                              15.5718f, 15.5413f, 15.5476f, 15.5937f, 15.7333f, 15.8719f, 15.9298f};
constexpr float kRaiseFps = 20.f, kRaiseSecs = 1.2f;
constexpr float kLean = 15.9298f;
constexpr float kCreakAt = 0.005f, kImpactAt = 0.792f;
constexpr float kOverTop = 1.2f;
constexpr float kDeg = 3.14159265f / 180.f;
float RaiseAngle(float t) {
    if (!(t > 0.f)) return kRaise[0];
    float f = t * kRaiseFps;
    int i = static_cast<int>(f);
    if (i >= 24) return kRaise[24];
    return kRaise[i] + (kRaise[i + 1] - kRaise[i]) * (f - static_cast<float>(i));
}
bool Fit(const float W[2], const float dir[2], float ground, float top, float maxH, float base[2], float* len, float* scaleY) {
    float reach = top - ground + kOverTop;
    if (!(top - ground >= 3.f) || !(top - ground <= maxH)) return false;
    float c = std::cos(kLean * kDeg), s = std::sin(kLean * kDeg);
    *len = reach / c;
    *scaleY = *len / kModelLen;
    float d = reach * s / c;
    base[0] = W[0] - dir[0] * d;
    base[1] = W[1] - dir[1] * d;
    return true;
}
void Along(const float base[3], const float dir[2], float s, float out[3]) {
    float c = std::cos(kLean * kDeg), sn = std::sin(kLean * kDeg);
    out[0] = base[0] + dir[0] * sn * s;
    out[1] = base[1] + c * s;
    out[2] = base[2] + dir[1] * sn * s;
}
int RungAt(float s, float scaleY) {
    float y = s / (scaleY > 1e-3f ? scaleY : 1.f);
    return static_cast<int>(std::floor((y - kRung0) / kRungStep + 0.5f));
}
}
struct LadderRec {
    bool used = false;
    float base[3] = {};
    float dir[2] = {};
    float W[2] = {};
    float len = 0, scaleY = 1;
    float topH = 0, topPt[2] = {};
    double t0 = 0;
    bool creak = false, impact = false;
    float clipLo = 0, clipHi = 1e9f;
    double tVis = 0;
};
LadderRec g_lad[vmgame::kLadderMax];
LadderRec g_ladSpot;
bool g_ladSpotOk = false;
uintptr_t g_ladBattle = 0;
bool LadderFindSpot(const float p[2], const float f[2], float foot, LadderRec* out) {
    uint32_t t = 0;
    float hp[3] = {};
    const float q[2] = {p[0] + f[0] * 1.2f, p[1] + f[1] * 1.2f};
    if (!TallWallBetween(p, q, foot, &t, hp)) return false;
    const float W[2] = {hp[0], hp[2]};
    for (float s = 0.3f; s <= 8.f; s += 0.25f) {
        float x = W[0] + f[0] * s, z = W[1] + f[1] * s;
        Surf sf[kMaxSurf];
        int n = SurfacesAt(x, z, sf), bi = -1;
        for (int i = 0; i < n; ++i)
            if (sf[i].blk && sf[i].h > foot + 3.f && sf[i].h < foot + cfg.ladderMaxH && (bi < 0 || sf[i].h < sf[bi].h)) bi = i;
        if (bi < 0) continue;
        for (int j = 0; j < n; ++j)
            if (sf[j].h > sf[bi].h + 2.f) return false;
        float top = sf[bi].h, tx = x + f[0] * 0.8f, tz = z + f[1] * 0.8f;
        {
            Surf s2[kMaxSurf];
            int n2 = SurfacesAt(tx, tz, s2);
            bool same = false;
            for (int j = 0; j < n2; ++j) same = same || (s2[j].blk && std::fabs(s2[j].h - top) < 0.4f);
            if (!same) tx = x, tz = z;
        }
        if (InsideBuilding(tx, tz, top)) return false;
        float base[2], len = 0, scaleY = 1;
        if (!ladder::Fit(W, f, foot, top, cfg.ladderMaxH, base, &len, &scaleY)) return false;
        float ground = foot;
        if (SurfaceNear(base[0], base[1], foot, &ground, nullptr) && std::fabs(ground - foot) < 3.f &&
            !ladder::Fit(W, f, ground, top, cfg.ladderMaxH, base, &len, &scaleY))
            return false;
        uint32_t t2 = 0;
        float h2[3] = {};
        if (TallWallBetween(p, base, foot, &t2, h2)) return false;
        out->base[0] = base[0], out->base[1] = ground, out->base[2] = base[1];
        out->dir[0] = f[0], out->dir[1] = f[1];
        out->W[0] = W[0], out->W[1] = W[1];
        out->len = len, out->scaleY = scaleY;
        out->topH = top, out->topPt[0] = tx, out->topPt[1] = tz;
        return true;
    }
    return false;
}
void LadderVisibility(LadderRec& L, const float eye[3]) {
    float sn = std::sin(ladder::kLean * ladder::kDeg), c = std::cos(ladder::kLean * ladder::kDeg);
    float lo = -1.f, hi = -1.f;
    const int K = 10;
    for (int k = 0; k < K; ++k) {
        float y = ladder::kModelLen * (static_cast<float>(k) + 0.5f) / K, s = y * L.scaleY;
        float pt[3] = {L.base[0] + L.dir[0] * sn * s, L.base[1] + c * s, L.base[2] + L.dir[1] * sn * s};
        float d[3] = {pt[0] - eye[0], pt[1] - eye[1], pt[2] - eye[2]}, dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        bool vis = true;
        if (dl > 0.5f) {
            float k2 = (dl - 0.35f) / dl;
            const float b[3] = {eye[0] + d[0] * k2, eye[1] + d[1] * k2, eye[2] + d[2] * k2};
            uint32_t t = 0;
            float hp[3];
            vis = !SegHitsBuilding(eye, b, &t, hp);
        }
        if (vis) {
            if (lo < 0.f) lo = y - 0.5f * ladder::kModelLen / K;
            hi = y + 0.5f * ladder::kModelLen / K;
        }
    }
    if (lo < 0.f) {
        L.clipLo = L.clipHi = 0.f;
    } else {
        L.clipLo = lo <= 0.f ? 0.f : lo;
        L.clipHi = hi >= ladder::kModelLen - 1e-3f ? 1e9f : hi;
    }
}
void LadderPublish(double now, bool gunDown) {
    vmgame::LadderView v[vmgame::kLadderMax];
    int n = 0;
    for (const LadderRec& L : g_lad) {
        if (!L.used) continue;
        vmgame::LadderView& o = v[n++];
        memcpy(o.base, L.base, sizeof o.base);
        memcpy(o.dir, L.dir, sizeof o.dir);
        o.tiltDeg = ladder::RaiseAngle(static_cast<float>(now - L.t0));
        o.scaleY = L.scaleY;
        bool raising = now - L.t0 < ladder::kRaiseSecs;
        o.clipLo = raising ? 0.f : L.clipLo;
        o.clipHi = raising ? 1e9f : L.clipHi;
    }
    vmgame::LadderPublish(v, n, gunDown);
}
void LadderLetGo(double now, const char* why) {
    S.ladMode = 0;
    S.air = true;
    S.vy = 0;
    S.airStartY = S.footY;
    S.airGap = 0;
    S.tAir = now;
    S.dropActive = false;
    S.dropPolyN = 0;
    S.footBlk = 0;
    S.kinVel[0] = S.kinVel[1] = 0;
    if (S.ladLogs++ < 40) Log("ladder: let go at %.2f,%.2f foot %.2f (%s)", S.kinPos[0], S.kinPos[1], S.footY, why);
}
void LadderFrame(double now, float dt, bool fg, bool paused, bool pinned, float sy, float cy, float eyeH) {
    if (!cfg.ladder || !g_buildOk || !g_wallOk || !vmgame::LadderReady()) {
        S.prompt = 0;
        S.ladMode = 0;
        return;
    }
    uintptr_t battle = 0;
    CmdQueue(&battle);
    if (battle && battle != g_ladBattle) {
        for (LadderRec& L : g_lad) L.used = false;
        g_ladBattle = battle;
    }
    bool e = fg && KeyDown(static_cast<int>(cfg.ladderKey));
    bool press = e && !S.ePrev && !paused;
    S.ePrev = e;
    for (LadderRec& L : g_lad) {
        if (!L.used) continue;
        float t = static_cast<float>(now - L.t0);
        if (!L.creak && t >= ladder::kCreakAt) L.creak = true, vmgame::LadderSound(0);
        if (!L.impact && t >= ladder::kImpactAt) L.impact = true, vmgame::LadderSound(1);
    }
    if (pinned && S.ladMode) {
        S.ladMode = 0;
        if (S.ladLogs++ < 40) Log("ladder: engaged while on the ladder - camera back onto the hero");
    }
    const float f[2] = {sy, cy};
    S.prompt = 0;
    if (S.ladMode == 1) {
        LadderRec& L = g_lad[S.ladIdx];
        float c = std::cos(ladder::kLean * ladder::kDeg);
        float sTop = (L.topH - L.base[1]) / c;
        float in = fg ? (KeyDown('W') ? 1.f : 0.f) - (KeyDown('S') ? 1.f : 0.f) : 0.f;
        if (!paused && dt > 0.f) S.ladS += in * cfg.ladderSpeed * dt;
        if (S.ladS < 0.f) S.ladS = 0.f;
        if (S.ladS > sTop) S.ladS = sTop;
        int rung = ladder::RungAt(S.ladS, L.scaleY);
        if (rung != S.ladRung) {
            if (in != 0.f) {
                vmgame::LadderSound(2);
                S.landV += kick::Impulse(-0.015f, 5.f, 0.55f);
            }
            S.ladRung = rung;
        }
        float feet[3];
        ladder::Along(L.base, L.dir, S.ladS, feet);
        S.kinPos[0] = feet[0] - L.dir[0] * cfg.ladderBack;
        S.kinPos[1] = feet[2] - L.dir[1] * cfg.ladderBack;
        S.footY = feet[1];
        S.footBlk = 0;
        S.kinVel[0] = S.kinVel[1] = 0;
        S.air = false;
        S.prompt = 4;
        if (press) {
            LadderLetGo(now, "E on the ladder");
        } else if (in > 0.f && S.ladS >= sTop - 0.01f) {
            S.ladMode = 2;
            S.ladFrom[0] = S.kinPos[0], S.ladFrom[1] = S.footY, S.ladFrom[2] = S.kinPos[1];
            S.ladTo[0] = L.topPt[0], S.ladTo[1] = L.topH, S.ladTo[2] = L.topPt[1];
            S.tLadMount = now;
            if (S.ladLogs++ < 40) Log("ladder: top reached at foot %.2f - climbing over onto the walkway at %.2f,%.2f h %.2f", S.footY, L.topPt[0],
                                      L.topPt[1], L.topH);
        } else if (in < 0.f && S.ladS <= 0.f) {
            S.ladMode = 0;
            float g = S.footY;
            if (SurfaceNear(S.kinPos[0], S.kinPos[1], S.footY + 0.5f, &g, &S.footBlk)) S.footY = g;
            if (S.ladLogs++ < 40) Log("ladder: stepped off at the bottom %.2f,%.2f foot %.2f", S.kinPos[0], S.kinPos[1], S.footY);
        }
    } else if (S.ladMode == 2) {
        float k = static_cast<float>((now - S.tLadMount) / 0.6), ks = k >= 1.f ? 1.f : k * k * (3.f - 2.f * k);
        S.kinPos[0] = S.ladFrom[0] + (S.ladTo[0] - S.ladFrom[0]) * ks;
        S.kinPos[1] = S.ladFrom[2] + (S.ladTo[2] - S.ladFrom[2]) * ks;
        S.footY = S.ladFrom[1] + (S.ladTo[1] - S.ladFrom[1]) * ks + 0.6f * std::sin(3.14159265f * (k > 1.f ? 1.f : k));
        S.kinVel[0] = S.kinVel[1] = 0;
        S.air = false;
        if (k >= 1.f) {
            S.ladMode = 0;
            S.footY = S.ladTo[1];
            float g = S.footY;
            uintptr_t blk = 0;
            if (SurfaceNear(S.kinPos[0], S.kinPos[1], S.footY + 0.3f, &g, &blk) && std::fabs(g - S.footY) < 0.6f) S.footY = g, S.footBlk = blk;
            S.tLastTp = 0;
            S.landV += kick::Impulse(-0.05f, 5.f, 0.55f);
            vmgame::LadderSound(2);
            if (S.ladLogs++ < 40) Log("ladder: on the walkway at %.2f,%.2f foot %.2f on %s %p", S.kinPos[0], S.kinPos[1], S.footY,
                                      S.footBlk ? "block" : "terrain", (void*)S.footBlk);
        }
    } else if (!S.air && !pinned && !paused) {
        const float p[2] = {S.kinPos[0], S.kinPos[1]};
        int climb = -1, down = -1;
        float best = 1e9f;
        for (int i = 0; i < vmgame::kLadderMax; ++i) {
            const LadderRec& L = g_lad[i];
            if (!L.used || now - L.t0 < ladder::kRaiseSecs) continue;
            float bx = p[0] - L.base[0], bz = p[1] - L.base[2], db = std::sqrt(bx * bx + bz * bz);
            if (db < 2.2f && db < best && std::fabs(S.footY - L.base[1]) < 1.5f && f[0] * L.dir[0] + f[1] * L.dir[1] > 0.3f) best = db, climb = i;
            float tx = p[0] - L.topPt[0], tz = p[1] - L.topPt[1], dt2 = std::sqrt(tx * tx + tz * tz);
            if (dt2 < 2.5f && std::fabs(S.footY - L.topH) < 1.f && -(f[0] * L.dir[0] + f[1] * L.dir[1]) > 0.2f && down < 0) down = i;
        }
        if (climb >= 0) {
            S.prompt = 2;
            if (press) {
                S.ladMode = 1, S.ladIdx = climb, S.ladS = 0.f, S.ladRung = -1;
                if (S.ladLogs++ < 40) Log("ladder: climbing ladder %d (wall top %.2f, %.1f m long)", climb, g_lad[climb].topH, g_lad[climb].len);
            }
        } else if (down >= 0) {
            S.prompt = 3;
            if (press) {
                const LadderRec& L = g_lad[down];
                S.ladMode = 1, S.ladIdx = down, S.ladS = (L.topH - L.base[1]) / std::cos(ladder::kLean * ladder::kDeg) - 0.05f, S.ladRung = -1;
                if (S.ladLogs++ < 40) Log("ladder: climbing down ladder %d from the walkway", down);
            }
        } else {
            if (now - S.tLadScan > 0.15) {
                S.tLadScan = now;
                g_ladSpotOk = LadderFindSpot(p, f, S.footY, &g_ladSpot);
                if (g_ladSpotOk)
                    for (const LadderRec& L : g_lad) {
                        float x = L.base[0] - g_ladSpot.base[0], z = L.base[2] - g_ladSpot.base[2];
                        if (L.used && x * x + z * z < 2.5f * 2.5f) g_ladSpotOk = false;
                    }
            }
            if (g_ladSpotOk) {
                S.prompt = 1;
                if (press) {
                    int slot = 0;
                    for (int i = 0; i < vmgame::kLadderMax; ++i) {
                        if (!g_lad[i].used) {
                            slot = i;
                            break;
                        }
                        if (g_lad[i].t0 < g_lad[slot].t0) slot = i;
                    }
                    g_lad[slot] = g_ladSpot;
                    g_lad[slot].used = true;
                    g_lad[slot].t0 = now;
                    g_lad[slot].creak = g_lad[slot].impact = false;
                    g_lad[slot].clipLo = 0.f, g_lad[slot].clipHi = 1e9f, g_lad[slot].tVis = 0;
                    g_ladSpotOk = false;
                    S.tLadScan = now + 1.0;
                    if (S.ladLogs++ < 40)
                        Log("ladder: raised ladder %d at %.2f,%.2f,%.2f towards %.2f,%.2f: wall face %.2f,%.2f, walkway %.2f (%.1f m above the foot), "
                            "%.1f m long (x%.2f)", slot, g_lad[slot].base[0], g_lad[slot].base[1], g_lad[slot].base[2], f[0], f[1], g_lad[slot].W[0],
                            g_lad[slot].W[1], g_lad[slot].topH, g_lad[slot].topH - g_lad[slot].base[1], g_lad[slot].len, g_lad[slot].scaleY);
                }
            }
        }
    }
    const float eye[3] = {S.kinPos[0], S.footY + eyeH, S.kinPos[1]};
    for (LadderRec& L : g_lad)
        if (L.used && now - L.tVis > 0.1) {
            L.tVis = now;
            float x = L.base[0] - eye[0], z = L.base[2] - eye[2];
            if (x * x + z * z > 80.f * 80.f)
                L.clipLo = L.clipHi = 0.f;
            else if (S.ladMode && &L == &g_lad[S.ladIdx])
                L.clipLo = 0.f, L.clipHi = 1e9f;
            else
                LadderVisibility(L, eye);
        }
    LadderPublish(now, S.ladMode != 0);
}
