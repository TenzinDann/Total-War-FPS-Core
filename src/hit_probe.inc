namespace hitprobe {
constexpr uintptr_t oBulRecord = 0x90, oBulState = 0xc0, oBulPos = 0xf4, oBulVel = 0x100, oBulAlive = 0x11c, oBulDist = 0x124,
                    oBulHits = 0x140;
constexpr uintptr_t oEntHp = 0xb1c, oEntHpMax = 0xb18, oEntSide = 0x80;
constexpr float kSimStep = 0.1f;
constexpr float kNearH = 1.2f;
constexpr int kMaxShots = 64, kMaxBul = 48, kMaxCand = 64, kMaxVic = 1024, kMaxUpd = 12;
constexpr float kTopDef = 4.f, kTopSlack = 0.5f;
struct Shot {
    double tClick;
    uintptr_t aim;
    float aimDist;
    char what;
};
struct Bul {
    uintptr_t p;
    unsigned id;
    double tClick, tExec;
    uintptr_t aim;
    float aimDist;
    char what;
    int upd;
    int hits;
    int firstHitUpd;
    int hpVics;
    bool done;
    uintptr_t cand[kMaxCand];
    float cd[kMaxCand];
    int nc;
};
struct Vic {
    uintptr_t e;
    uint32_t hp0, hpMax, hpLast;
    double tFirstShot, tFirstHit, tTouch;
    int shots, hits, nd;
    int dmg[8];
    bool dead;
};
struct State {
    SRWLOCK lock = SRWLOCK_INIT;
    Shot q[kMaxShots];
    int nq = 0;
    Bul b[kMaxBul];
    int nb = 0;
    Vic v[kMaxVic];
    int nv = 0;
    uintptr_t world = 0, hero = 0;
    double tPrev = 0, tCur = 0;
    unsigned shotId = 0, shotLogs = 0, killLogs = 0, hitLogs = 0, survLogs = 0;
    unsigned markLogs = 0;
    bool announced = false, recordChecked = false;
    double tStat = 0;
    unsigned sShots = 0, sUnpaired = 0, sHitUpd[4] = {}, sNoHit = 0, sHitNoHp = 0, sDrops = 0, sKills = 0, sOther = 0, sOtherKills = 0;
    unsigned sHitsPerKill[5] = {}, sDmgN = 0, sScanN = 0, sExecN = 0, sKillN = 0;
    double sExecSum = 0, sExecMax = 0, sKillClickSum = 0, sKillClickMax = 0, sKillFirstSum = 0, sDmgSum = 0, sScanSum = 0;
    int sDmgMin = 0, sDmgMax = 0;
    unsigned bShots = 0, bHits = 0, bLate = 0, bWhite = 0, bRed = 0, bCandEvict = 0, bCandDrop = 0, bVicDrop = 0, bVicEvict = 0, bBulDrop = 0, bShotDrop = 0;
    unsigned fShots = 0, fNew = 0, fOld = 0, fOnlyNew = 0, fOnlyOld = 0, fFriends = 0, fDead = 0, fBlocked = 0, fKd = 0;
} g;
double Ms(double a, double b) { return (a - b) * 1000.0; }
int FindVic(uintptr_t e) {
    for (int i = 0; i < g.nv; ++i)
        if (g.v[i].e == e) return i;
    return -1;
}
void Touch(Bul& b, uintptr_t e, uint32_t hp, float score) {
    int ci = -1;
    for (int i = 0; i < b.nc && ci < 0; ++i)
        if (b.cand[i] == e) ci = i;
    const bool known = ci >= 0;
    if (known) {
        if (score < b.cd[ci]) b.cd[ci] = score;
    } else if (b.nc < kMaxCand) {
        b.cand[b.nc] = e;
        b.cd[b.nc] = score;
        ++b.nc;
    } else {
        int w = 0;
        for (int i = 1; i < b.nc; ++i)
            if (b.cd[i] > b.cd[w]) w = i;
        if (score < b.cd[w]) {
            b.cand[w] = e;
            b.cd[w] = score;
            ++g.bCandEvict;
        } else {
            ++g.bCandDrop;
        }
    }
    int vi = FindVic(e);
    if (vi < 0) {
        if (g.nv >= kMaxVic) {
            for (int i = 0; i < g.nv; ++i)
                if ((g.v[i].dead || (!g.v[i].hits && g.tCur - g.v[i].tTouch > 0.5)) && (vi < 0 || g.v[i].tTouch < g.v[vi].tTouch)) vi = i;
            if (vi < 0) {
                ++g.bVicDrop;
                return;
            }
            ++g.bVicEvict;
        } else {
            vi = g.nv++;
        }
        Vic& v = g.v[vi];
        memset(&v, 0, sizeof v);
        v.e = e;
        v.hp0 = v.hpLast = hp;
        Rd(e + oEntHpMax, v.hpMax);
    }
    Vic& v = g.v[vi];
    v.tTouch = g.tCur;
    if (!known) {
        ++v.shots;
        if (b.tClick > 0 && (v.tFirstShot <= 0 || b.tClick < v.tFirstShot)) v.tFirstShot = b.tClick;
    }
}
void Summary(bool force) {
    if (!cfg.hitProbe) return;
    if (!g.sShots && !g.sDrops && !g.sOther) return;
    if (!force && g.tCur - g.tStat < 3.0) return;
    Log("hit-probe: %.1f s: shots %u (click->exec avg %.0f max %.0f ms; unpaired %u) | bullets that hit someone: on update 1 (the step it was fired) %u, "
        "update 2 %u, update 3+ %u, never %u; hit but nobody lost hp %u | hp hits by our bullets %u (damage avg %.1f min %d max %d) | "
        "kills %u (hits per kill 1:%u 2:%u 3:%u 4+:%u; first shot click -> death avg %.0f ms; killing shot click -> death avg %.0f max %.0f ms) | "
        "hp loss next to our bullets not from them %u (deaths %u) | scan avg %.2f ms",
        g.tCur - g.tStat, g.sShots, g.sExecN ? g.sExecSum / g.sExecN : 0.0, g.sExecMax, g.sUnpaired, g.sHitUpd[1], g.sHitUpd[2],
        g.sHitUpd[3], g.sNoHit, g.sHitNoHp, g.sDrops, g.sDmgN ? g.sDmgSum / g.sDmgN : 0.0, g.sDmgMin, g.sDmgMax, g.sKills, g.sHitsPerKill[1],
        g.sHitsPerKill[2], g.sHitsPerKill[3], g.sHitsPerKill[4], g.sKillN ? g.sKillFirstSum / g.sKillN : 0.0,
        g.sKillN ? g.sKillClickSum / g.sKillN : 0.0, g.sKillClickMax, g.sOther, g.sOtherKills, g.sScanN ? g.sScanSum / g.sScanN : 0.0);
    g.sShots = g.sUnpaired = g.sNoHit = g.sHitNoHp = g.sDrops = g.sKills = g.sOther = g.sOtherKills = 0;
    g.sDmgN = g.sScanN = g.sExecN = g.sKillN = 0;
    memset(g.sHitUpd, 0, sizeof g.sHitUpd);
    memset(g.sHitsPerKill, 0, sizeof g.sHitsPerKill);
    g.sExecSum = g.sExecMax = g.sKillClickSum = g.sKillClickMax = g.sKillFirstSum = g.sDmgSum = g.sScanSum = 0;
    g.sDmgMin = g.sDmgMax = 0;
    g.tStat = g.tCur;
}
void Finish(Bul& b, const char* why) {
    if (b.done) return;
    b.done = true;
    if (b.firstHitUpd)
        ++g.sHitUpd[b.firstHitUpd >= 3 ? 3 : b.firstHitUpd];
    else
        ++g.sNoHit;
    if (b.hits > 0 && !b.hpVics) ++g.sHitNoHp;
    if (cfg.hitProbe && g.shotLogs++ < 150) {
        char res[96];
        if (b.firstHitUpd)
            snprintf(res, sizeof res, "hit someone on update %d%s, %d enemies lost hp", b.firstHitUpd,
                     b.firstHitUpd == 1 ? " (same step it was fired)" : "", b.hpVics);
        else
            snprintf(res, sizeof res, "no hit after %d updates", b.upd);
        float dist = 0.f;
        Rd(b.p + oBulDist, dist);
        Log("hit-probe: shot #%u click->exec %s%.0f ms | aimed %s %p at %.1f m | %s, flew %.0f m (%s; %d enemies passed)", b.id,
            b.tClick > 0 ? "" : "? ", b.tClick > 0 ? Ms(b.tExec, b.tClick) : 0.0, b.what == 's' ? "soldier" : b.what == 'g' ? "ground" : "range",
            (void*)b.aim, b.aimDist, res, dist, why, b.nc);
    }
}
void LogSurvivor(const Vic& v) {
    if (!cfg.hitProbe || !v.hits || v.dead || g.survLogs++ >= 60) return;
    char dl[64] = "";
    int n = 0;
    for (int i = 0; i < v.nd && n < 56; ++i) n += snprintf(dl + n, sizeof dl - n, "%s%d", i ? " " : "", v.dmg[i]);
    Log("hit-probe: %p took %d hits [%s] from our bullets, hp %u -> %u (max %u), %d of our bullets passed it, still alive", (void*)v.e, v.hits, dl,
        v.hp0, v.hpLast, v.hpMax, v.shots);
}
void BattleSummary(const char* why) {
    if (!g.bShots && !g.bHits && !g.fShots) return;
    Log("hitmark: summary (%s): bullets %u, probe hits %u (seen by the after-step check %u - lost before this fix), probe white %u red %u; "
        "dropped by limits: candidates evicted %u / not taken %u, victims evicted %u / not taken %u, bullets %u, shots %u | shot-frame feedback: shots %u, "
        "white %u (old rule %u; only new %u, only old %u, knocked aim %u), passed friends %u, passed dead or no side %u, blocked by a live friend %u",
        why, g.bShots, g.bHits, g.bLate, g.bWhite, g.bRed, g.bCandEvict, g.bCandDrop, g.bVicEvict, g.bVicDrop, g.bBulDrop, g.bShotDrop, g.fShots, g.fNew,
        g.fOld, g.fOnlyNew, g.fOnlyOld, g.fKd, g.fFriends, g.fDead, g.fBlocked);
    g.bShots = g.bHits = g.bLate = g.bWhite = g.bRed = g.bCandEvict = g.bCandDrop = g.bVicDrop = g.bVicEvict = g.bBulDrop = g.bShotDrop = 0;
    g.fShots = g.fNew = g.fOld = g.fOnlyNew = g.fOnlyOld = g.fFriends = g.fDead = g.fBlocked = g.fKd = 0;
}
void Flush(const char* why) {
    for (int i = 0; i < g.nb; ++i) Finish(g.b[i], why);
    for (int i = 0; i < g.nv; ++i) LogSurvivor(g.v[i]);
    Summary(true);
    BattleSummary(why);
    g.nb = g.nv = g.nq = 0;
}
void OnShot(const AimHit& h, double tClick) {
    if (!cfg.hitProbe && !vmgame::HitMarkOn()) return;
    AcquireSRWLockExclusive(&g.lock);
    if (g.nq < kMaxShots)
        g.q[g.nq++] = {tClick, h.ent, h.dist, h.what ? h.what[0] : 'r'};
    else
        ++g.bShotDrop;
    ReleaseSRWLockExclusive(&g.lock);
}
void NoteFb(bool oldWhite, bool newWhite, unsigned friends, unsigned dead, bool blocked, bool kdAim) {
    AcquireSRWLockExclusive(&g.lock);
    ++g.fShots;
    if (newWhite) ++g.fNew;
    if (oldWhite) ++g.fOld;
    if (newWhite && !oldWhite) ++g.fOnlyNew;
    if (oldWhite && !newWhite) ++g.fOnlyOld;
    g.fFriends += friends;
    g.fDead += dead;
    if (blocked) ++g.fBlocked;
    if (kdAim) ++g.fKd;
    ReleaseSRWLockExclusive(&g.lock);
}
void OnExit(const char* why) {
    AcquireSRWLockExclusive(&g.lock);
    if (g.world) {
        g.tCur = Now();
        Flush(why);
    }
    g.world = g.hero = 0;
    ReleaseSRWLockExclusive(&g.lock);
}
void Observe(uintptr_t world, uintptr_t hero, bool late = false) {
    if (!cfg.hitProbe && !vmgame::HitMarkOn()) return;
    AcquireSRWLockExclusive(&g.lock);
    g.tCur = Now();
    if (world != g.world || hero != g.hero) {
        if (g.world) Flush("battle or hero changed");
        g.world = world;
        g.hero = hero;
        g.tPrev = 0;
        g.tStat = g.tCur;
        g.recordChecked = false;
        if (cfg.hitProbe && !g.announced) {
            g.announced = true;
            Log("hit-probe: on (read-only; [fire] hit_probe=0 turns it off): per shot click->exec, which update hits, hp loss, hits per kill");
        }
    }
    if (g.nb || g.nv) {
        static uintptr_t fl[16384];
        uintptr_t pm = Ptr(world + 0x1c8), fa = pm ? Ptr(pm + 0x18) : 0;
        uint32_t fc = 0;
        if (pm) Rd(pm + 0x14, fc);
        if (fc > 16384) fc = 16384;
        SIZE_T got = 0;
        if (!fc || !fa || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(fa), fl, fc * 8, &got) || got != fc * 8) fc = 0;
        int hitters[kMaxBul], nh = 0;
        for (int i = 0; i < g.nb; ++i) {
            Bul& b = g.b[i];
            if (b.done) continue;
            bool in = false;
            for (uint32_t j = 0; j < fc && !in; ++j) in = fl[j] == b.p;
            if (!in) {
                Finish(b, "removed from the flight list");
                continue;
            }
            float alive = 0.f;
            int32_t hits = 0, st = 0;
            Rd(b.p + oBulAlive, alive);
            Rd(b.p + oBulHits, hits);
            Rd(b.p + oBulState, st);
            b.upd = static_cast<int>(alive / kSimStep + 0.5f);
            if (hits > b.hits) {
                if (!b.firstHitUpd) b.firstHitUpd = b.upd > 0 ? b.upd : 1;
                b.hits = hits;
                hitters[nh++] = i;
            }
        }
        for (int i = 0; i < g.nv; ++i) {
            Vic& v = g.v[i];
            uint32_t hp = 0;
            if (v.dead || !Rd(v.e + oEntHp, hp)) continue;
            if (hp < v.hpLast) {
                int d = static_cast<int>(v.hpLast - hp);
                int by = -1;
                for (int h = 0; h < nh && by < 0; ++h) {
                    const Bul& b = g.b[hitters[h]];
                    for (int c = 0; c < b.nc; ++c)
                        if (b.cand[c] == v.e) by = hitters[h];
                }
                if (by >= 0) {
                    Bul& b = g.b[by];
                    ++b.hpVics;
                    ++g.bHits;
                    if (late) ++g.bLate;
                    if (hp == 0)
                        vmgame::HitMark(true), ++g.bRed;
                    else if (v.e != b.aim)
                        vmgame::HitMark(false), ++g.bWhite;
                    if (g.markLogs++ < 40)
                        Log("hit-mark: our bullet #%u hit enemy %p for %d hp%s%s", b.id, (void*)v.e, d, hp == 0 ? " - dead -> kill marker" : "",
                            hp != 0 && v.e == b.aim ? " (aimed at it: white already shown at the shot)" : hp != 0 ? " -> hit marker" : "");
                    ++v.hits;
                    if (v.nd < 8) v.dmg[v.nd++] = d;
                    if (v.tFirstHit <= 0) v.tFirstHit = g.tPrev;
                    ++g.sDrops;
                    g.sDmgSum += d;
                    if (!g.sDmgN || d < g.sDmgMin) g.sDmgMin = d;
                    if (!g.sDmgN || d > g.sDmgMax) g.sDmgMax = d;
                    ++g.sDmgN;
                    if (hp == 0) {
                        v.dead = true;
                        ++g.sKills;
                        ++g.sHitsPerKill[v.hits >= 4 ? 4 : v.hits];
                        double kc = b.tClick > 0 ? Ms(g.tPrev, b.tClick) : -1, kf = v.tFirstShot > 0 ? Ms(g.tPrev, v.tFirstShot) : -1;
                        if (kc >= 0) {
                            g.sKillClickSum += kc;
                            if (kc > g.sKillClickMax) g.sKillClickMax = kc;
                            g.sKillFirstSum += kf >= 0 ? kf : kc;
                            ++g.sKillN;
                        }
                        if (cfg.hitProbe && g.killLogs++ < 120) {
                            char dl[64] = "";
                            int n = 0;
                            for (int k = 0; k < v.nd && n < 56; ++k) n += snprintf(dl + n, sizeof dl - n, "%s%d", k ? " " : "", v.dmg[k]);
                            Log("hit-probe: KILL %p (hp %u, max %u) by %d hits [%s], %d of our bullets passed it; first shot click -> death %.0f ms, "
                                "killing shot #%u click -> death %.0f ms (its update %d), first hit -> death %.0f ms",
                                (void*)v.e, v.hp0, v.hpMax, v.hits, dl, v.shots, kf, b.id, kc, b.firstHitUpd, Ms(g.tPrev, v.tFirstHit));
                        }
                    } else if (cfg.hitProbe && g.hitLogs++ < 60) {
                        Log("hit-probe: hit %p by shot #%u: -%d hp (%u -> %u), update %d, click -> hit %.0f ms", (void*)v.e, b.id, d, v.hpLast, hp,
                            b.firstHitUpd, b.tClick > 0 ? Ms(g.tPrev, b.tClick) : -1.0);
                    }
                } else {
                    ++g.sOther;
                    if (hp == 0) ++g.sOtherKills;
                }
            }
            v.hpLast = hp;
            if (!hp) v.dead = true;
        }
        int w = 0;
        for (int i = 0; i < g.nb; ++i) {
            Bul& b = g.b[i];
            if (!b.done) {
                int32_t st = 0;
                Rd(b.p + oBulState, st);
                if (st == 7) Finish(b, "stopped");
                else if (b.upd >= kMaxUpd) Finish(b, "still flying, stopped watching");
            }
            if (!b.done) g.b[w++] = b;
        }
        g.nb = w;
        w = 0;
        for (int i = 0; i < g.nv; ++i) {
            Vic& v = g.v[i];
            bool keep = !v.dead && g.tCur - v.tTouch < 3.0;
            if (!keep && !v.dead) LogSurvivor(v);
            if (keep) g.v[w++] = v;
        }
        g.nv = w;
    }
    Summary(false);
    ReleaseSRWLockExclusive(&g.lock);
}
float SegNear(const float q[3], const float a[3], const float b[3], float* t) {
    float ux = b[0] - a[0], uz = b[2] - a[2], wx = q[0] - a[0], wz = q[2] - a[2], uu = ux * ux + uz * uz;
    float k = uu > 1e-6f ? (wx * ux + wz * uz) / uu : 0.f;
    if (k < 0.f) k = 0.f;
    if (k > 1.f) k = 1.f;
    *t = k;
    float dx = wx - ux * k, dz = wz - uz * k;
    return std::sqrt(dx * dx + dz * dz);
}
void Arm(uintptr_t world, uintptr_t hero, const uintptr_t* ours, unsigned k) {
    if ((!cfg.hitProbe && !vmgame::HitMarkOn()) || world != g.world) return;
    AcquireSRWLockExclusive(&g.lock);
    LARGE_INTEGER q0, q1;
    QueryPerformanceCounter(&q0);
    for (unsigned i = 0; i < k; ++i) {
        uintptr_t p = ours[i];
        if (cfg.hitProbe && !g.recordChecked) {
            g.recordChecked = true;
            uintptr_t rec = Ptr(p + oBulRecord);
            Log("hit-probe: first bullet %p record %p, first-person gun projectile record %p (%s)", (void*)p, (void*)rec, (void*)S.fpProj,
                rec == S.fpProj ? "match" : "DIFFERENT");
        }
        while (g.nq && g.tCur - g.q[0].tClick > 2.0) {
            ++g.sUnpaired;
            memmove(g.q, g.q + 1, sizeof(Shot) * --g.nq);
        }
        Shot s = {0, 0, 0, 'r'};
        if (g.nq) {
            s = g.q[0];
            memmove(g.q, g.q + 1, sizeof(Shot) * --g.nq);
        } else {
            ++g.sUnpaired;
        }
        if (g.nb >= kMaxBul) {
            Finish(g.b[0], "probe table full");
            ++g.bBulDrop;
            memmove(g.b, g.b + 1, sizeof(Bul) * --g.nb);
        }
        Bul& b = g.b[g.nb++];
        memset(&b, 0, sizeof b);
        b.p = p;
        b.id = ++g.shotId;
        b.tClick = s.tClick;
        b.tExec = g.tCur;
        b.aim = s.aim;
        b.aimDist = s.aimDist;
        b.what = s.what;
        int32_t h0 = 0;
        Rd(p + oBulHits, h0);
        b.hits = h0;
        ++g.sShots;
        ++g.bShots;
        if (s.tClick > 0) {
            double e = Ms(g.tCur, s.tClick);
            g.sExecSum += e;
            if (e > g.sExecMax) g.sExecMax = e;
            ++g.sExecN;
        }
    }
    struct Seg {
        float a[3], b[3];
        int bi;
    } seg[kMaxBul];
    int ns = 0;
    for (int i = 0; i < g.nb; ++i) {
        Bul& b = g.b[i];
        float p[3], v[3];
        if (b.done || !Rd(b.p + oBulPos, p) || !Rd(b.p + oBulVel, v)) continue;
        float sp = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(sp > 0.1f && sp < 1e5f)) continue;
        Seg& s = seg[ns++];
        for (int j = 0; j < 3; ++j) {
            s.a[j] = p[j] - v[j] / sp;
            s.b[j] = p[j] + v[j] * kSimStep + v[j] / sp;
        }
        s.bi = i;
        if (b.aim && !b.upd) {
            uint32_t hp = 0;
            if (Rd(b.aim + oEntHp, hp) && hp) Touch(b, b.aim, hp, -1.f);
        }
    }
    if (ns) {
        uintptr_t heroSide = Ptr(hero + oEntSide), heroUnit = Ptr(hero + 0x2e8);
        static uintptr_t ents[1024];
        uintptr_t list = Ptr(world + oWorldAlliances);
        uint32_t na = 0;
        if (list && Rd(list + oAlliancesCount, na) && na <= 16) {
            uintptr_t alliances = Ptr(list + oAlliancesArray);
            for (uint32_t ai = 0; ai < na && alliances; ++ai) {
                uintptr_t al = Ptr(alliances + 8 * ai);
                uint32_t nArmies = 0;
                if (!al || !Rd(al + oAllianceArmyCount, nArmies) || nArmies > 64) continue;
                uintptr_t armies = Ptr(al + oAllianceArmyArray);
                for (uint32_t j = 0; j < nArmies && armies; ++j) {
                    uintptr_t army = Ptr(armies + 8 * j);
                    uint32_t nu = 0;
                    if (!army || !Rd(army + oArmyUnitCount, nu) || nu > 1024) continue;
                    uintptr_t units = Ptr(army + oArmyUnitArray);
                    for (uint32_t ui = 0; ui < nu && units; ++ui) {
                        uintptr_t u = Ptr(units + 8 * ui);
                        if (!u || u == heroUnit) continue;
                        unsigned ne = UnitEntities(u, ents, 1024);
                        if (!ne) continue;
                        float q[3], t = 0.f;
                        if (!EntPos(ents[0], q)) continue;
                        bool nearUnit = false;
                        for (int s = 0; s < ns && !nearUnit; ++s) nearUnit = SegNear(q, seg[s].a, seg[s].b, &t) < 60.f;
                        if (!nearUnit) continue;
                        for (unsigned m = 0; m < ne; ++m) {
                            uintptr_t e = ents[m];
                            if (e == hero || !Plausible(e) || !EntPos(e, q)) continue;
                            for (int s = 0; s < ns; ++s) {
                                float dh = SegNear(q, seg[s].a, seg[s].b, &t);
                                if (dh > 4.f) continue;
                                float r = 0.f;
                                if (dh > kNearH && (!Rd(e + 0xa0, r) || !(r > 0.f && r < 10.f) || dh > r + 0.5f)) continue;
                                float y = seg[s].a[1] + (seg[s].b[1] - seg[s].a[1]) * t;
                                float top = kTopDef, eh = 0.f;
                                if (y > q[1] + top && Rd(e + 0x9c, eh) && eh > 0.3f && eh < 60.f && eh + kTopSlack > top) top = eh + kTopSlack;
                                if (y < q[1] - 1.f || y > q[1] + top) continue;
                                uint32_t hp = 0;
                                if (!Rd(e + oEntHp, hp) || !hp || Ptr(e + oEntSide) == heroSide) break;
                                Touch(g.b[seg[s].bi], e, hp, r > 0.f ? (dh > r ? dh - r : 0.f) : dh);
                            }
                        }
                    }
                }
            }
        }
    }
    for (int i = 0; i < g.nv; ++i) {
        uint32_t hp = 0;
        if (!g.v[i].dead && Rd(g.v[i].e + oEntHp, hp)) {
            g.v[i].hpLast = hp;
            if (!hp) g.v[i].dead = true;
        }
    }
    QueryPerformanceCounter(&q1);
    if (ns) {
        g.sScanSum += (q1.QuadPart - q0.QuadPart) * 1000.0 / static_cast<double>(g_qpf.QuadPart);
        ++g.sScanN;
    }
    g.tPrev = g.tCur;
    ReleaseSRWLockExclusive(&g.lock);
}
}
