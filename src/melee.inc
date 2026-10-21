volatile LONG g_meleeIn = 0;
bool MeleeWeapon() { return Wp().rend.Clips() && Wp().rend.FindClip("m_light_stab") >= 0; }
volatile LONG g_meleeOn = 0, g_meleeStrike = 0;
volatile LONG g_meleeHitQ[32] = {};
volatile LONG g_meleeHitQHead = 0, g_meleeHitQTail = 0;
namespace melee {
enum Act { kIdle, kDraw, kDefault, kDefaultLeft, kDefaultRight, kDefaultLeftHeavy, kDefaultStabHeavy,
           kLight1, kLight2, kLight3, kHeavy1, kHeavy2, kHeavy3, kPush, kPushStab, kBlock, kActs };
const float kInf = 1e9f;
struct Def {
    const char* name;
    const char* clip;
    float enter;
    float scale;
    float total;
    float chainStart, chainEnd;
    int chainTo;
    float blockFrom;
    int lightTo, heavyTo;
    float heavyStart;
    int strike;
    float hitAt;
    float swingAt;
};
const Def kDef[kActs] = {
    {"idle", "idle", 0.2f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"draw", "draw", 0.1f, 1.f, 0.8f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"default", "m_charge_left", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy1, 0.4f, -1, 0, 0},
    {"default_left", "m_charge_left", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight2, kHeavy1, 0.5f, -1, 0, 0},
    {"default_right", "m_charge_left", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight3, kHeavy1, 0.5f, -1, 0, 0},
    {"default_left_heavy", "m_charge_diag", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight2, kHeavy2, 0.5f, -1, 0, 0},
    {"default_stab_heavy", "m_charge_stab", 0.3f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy3, 0.5f, -1, 0, 0},
    {"light_attack_left", "m_light_stab", 0.1f, 1.17f, 2.1f, 0.5f, 1.2f, kDefaultLeft, 0.f, -1, -1, 0, kStrikeStab, 0.38f, 0.2455f},
    {"light_attack_right", "m_light_stab_lh", 0.05f, 1.17f, 2.1f, 0.5f, 1.2f, kDefaultRight, 0.f, -1, -1, 0, kStrikeStab, 0.38f, 0.326f},
    {"light_attack_last", "m_sweep", 0.05f, 1.125f, 1.1f, 0.65f, kInf, kDefault, 0.f, -1, -1, 0, kStrikeSweep, 0.27f, 0.2675f},
    {"heavy_attack", "m_sweep", 0.05f, 1.035f, 1.0f, 0.5f, 1.0f, kDefaultLeftHeavy, 0.5f, -1, -1, 0, kStrikeHeavySweep, 0.27f, 0.2675f},
    {"heavy_attack_right", "m_heavy_down_right", 0.1f, 1.0f, 2.25f, 0.5f, 1.05f, kDefaultStabHeavy, 0.4f, -1, -1, 0, kStrikeHeavy, 0.30f, 0.2188f},
    {"heavy_attack_stab", "m_heavy_stab", 0.1f, 1.035f, 2.25f, 0.5f, 1.05f, kDefault, 0.4f, -1, -1, 0, kStrikeHeavy, 0.25f, 0.2403f},
    {"push", "m_push", 0.0f, 1.0f, 0.8f, 0.2f, kInf, kDefault, 0.5f, -1, -1, 0, kStrikeBash, 0.05f, 0.f},
    {"light_attack_stab_postpush", "m_push_stab", 0.1f, 1.17f, 1.1f, 0.4f, kInf, kDefaultLeftHeavy, 0.45f, -1, -1, 0, kStrikeStab, 0.30f, 0.0667f},
    {"block", "m_parry_pose", 0.2f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
};
double StrikeLead() { return Wp().set.meleeStrikeLead; }
struct Exit {
    const char* from;
    const char* to;
    float blend;
};
const Exit kExit[] = {{"draw", "idle", 0.2f}, {"m_light_stab", "idle", 0.2f}, {"m_light_stab_lh", "idle", 0.2f},
                      {"m_sweep", "m_swing_finished", 0.1f}, {"m_heavy_down_right", "m_swing_finished", 0.1f},
                      {"m_heavy_stab", "m_swing_finished", 0.1f}, {"m_push_stab", "m_swing_finished", 0.1f},
                      {"m_swing_finished", "idle", 0.2f}, {"m_push", "idle", 0.2f}, {"m_parry_pose", "m_parry_loop", 0.2f}};
bool IsCharge(int a) { return a >= kDefault && a <= kDefaultStabHeavy; }
bool IsAttack(int a) { return (a >= kLight1 && a <= kHeavy3) || a == kPushStab; }
struct State {
    int act = kIdle;
    double start = 0;
    bool released = false;
    double relAt = 0;
    bool prevAtk = false, pending = false;
    double pressAt = -1;
    int model = -1;
    bool struck = false;
} g;
int g_trace[256];
int g_traceN = 0;
int g_strikeTrace[64];
int g_strikeN = 0;
struct Snap {
    bool on = false, captured = false;
} g_snap;
const bool kSnapOn = getenv("HFPS_MELEE_NOSNAP") == nullptr;
int g_snapCount = 0;
void MPlay(int clip, double now, double fade, double rate = 1.0) {
    double w = g_an.fadeLen > 0 ? (now - g_an.fadeStart) / g_an.fadeLen : 1.0;
    bool interrupted = (g_an.prev >= 0 || g_snap.on) && w < 1.0;
    Play(clip, now, fade, rate);
    g_snap.on = kSnapOn && interrupted && fade > 0;
    g_snap.captured = false;
    if (g_snap.on) ++g_snapCount;
}
struct Loco {
    int want = 0;
    int stance = 0;
    float speed = 0;
    double t = -1;
} g_loco;
const float kWalkOn = 0.25f, kWalkOff = 0.15f, kRunOn = 2.1f, kRunOff = 1.8f;
const float kVt2RunSpeed = 4.f, kRunRateMin = 0.75f, kRunRateMax = 1.6f;
struct LocoRec {
    int kind;
    double t;
    float rate, speed;
};
LocoRec g_locoTrace[64];
int g_locoTraceN = 0;
float g_runRateMax = 0;
int LocoClip(int k) {
    int c = k == 2 ? Wp().rend.FindClip("run") : k == 1 ? Wp().rend.FindClip("walk") : -1;
    return c >= 0 ? c : g_an.idle;
}
int RestClip() {
    if (g_loco.stance == 3) {
        int c = Wp().rend.FindClip("tac_sprint");
        if (c < 0) c = Wp().rend.FindClip("sprint");
        if (c >= 0) return c;
    } else if (g_loco.stance == 2) {
        int c = Wp().rend.FindClip("sprint");
        if (c >= 0) return c;
    }
    return LocoClip(g_loco.want);
}
bool IsRestClip(int c) {
    return c >= 0 && (c == g_an.idle || c == Wp().rend.FindClip("run") || c == Wp().rend.FindClip("walk") || c == Wp().rend.FindClip("sprint") ||
                      c == Wp().rend.FindClip("tac_sprint"));
}
bool IsSprintClip(int c) { return c >= 0 && (c == Wp().rend.FindClip("sprint") || c == Wp().rend.FindClip("tac_sprint")); }
double LocoRate(int clip) {
    if (clip < 0 || clip != Wp().rend.FindClip("run")) return 1.0;
    float r = std::sqrt(g_loco.speed / kVt2RunSpeed);
    return r < kRunRateMin ? kRunRateMin : r > kRunRateMax ? kRunRateMax : r;
}
void PlayRest(double now, double fade) {
    int c = RestClip();
    MPlay(c, now, fade, LocoRate(c));
}
void LocoUpdate(double now) {
    float raw = 0;
    if (g_fp) {
        for (int tries = 0; tries < 8; ++tries) {
            LONG s1 = g_pub.seq;
            if (s1 & 1) continue;
            MemoryBarrier();
            raw = g_pub.speed;
            MemoryBarrier();
            if (g_pub.seq == s1) break;
        }
    }
    double dt = g_loco.t < 0 ? 0 : now - g_loco.t;
    g_loco.t = now;
    if (dt > 0.1) dt = 0.1;
    g_loco.speed += (raw - g_loco.speed) * static_cast<float>(1 - std::exp(-dt / 0.1));
    const float v = g_loco.speed;
    int w = g_loco.want;
    if (w == 2) w = v < kRunOff ? (v < kWalkOff ? 0 : 1) : 2;
    else if (w == 1) w = v > kRunOn ? 2 : v < kWalkOff ? 0 : 1;
    else w = v > kRunOn ? 2 : v > kWalkOn ? 1 : 0;
    g_loco.want = w;
    const LONG st = g_fp ? g_stance : 0;
    g_loco.stance = (st & 4) ? 3 : (st & 2) ? 2 : 0;
}
enum Sfx { kSfxSwing, kSfxSwingLayer, kSfxPush, kSfxHitStab, kSfxHitStabHeavy, kSfxHitSlash, kSfxHitSlashHeavy, kSfxHitBash, kSfxHitBody,
           kSfxHitGore, kSfxTailStab, kSfxTailSlash, kSfxBlock, kSfxBlockTail, kSfxHitSlashLight, kSfxHitL4, kSfxHitHeavy };
static_assert(kSfxHitHeavy + 1 == vm::kMeleeSfx, "melee sfx layers");
const float kSfxDefVol[vm::kMeleeSfx] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
int g_sfxCount[vm::kMeleeSfx] = {};
struct Impact {
    int main, tail;
};
const Impact kImpact[kActs] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
                               {kSfxHitSlash, kSfxTailSlash},
                               {kSfxHitSlash, kSfxTailSlash},
                               {kSfxHitSlashLight, kSfxTailSlash},
                               {kSfxHitSlashLight, kSfxTailSlash},
                               {kSfxHitSlashHeavy, kSfxTailSlash},
                               {kSfxHitStab, kSfxTailStab},
                               {kSfxHitBash, -1},
                               {kSfxHitSlash, kSfxTailSlash},
                               {-1, -1}};
struct ImpactRec {
    Impact im;
    double t, contact;
    unsigned sw;
    int act;
};
ImpactRec g_impQ[32];
int g_impHead = 0, g_impTail = 0;
void PushImpact(const ImpactRec& r) {
    if (g_impTail - g_impHead >= 32) ++g_impHead;
    g_impQ[g_impTail++ & 31] = r;
}
bool PopImpact(double now, ImpactRec& out) {
    while (g_impHead < g_impTail && now - g_impQ[g_impHead & 31].t > 2.0) ++g_impHead;
    if (g_impHead == g_impTail) return false;
    out = g_impQ[g_impHead++ & 31];
    return true;
}
struct Swing {
    unsigned id = 0;
    int act = 0;
    double start = 0, due = 0;
};
Swing g_sw[4];
unsigned g_swNext = 1, g_swCur = 0;
unsigned g_swPlayed = 0;
struct SwingTrace {
    int act;
    float at, due;
};
SwingTrace g_swTrace[64], g_hitTrace[64];
int g_swTraceN = 0, g_hitTraceN = 0, g_swCutN = 0;
Swing* FindSwing(unsigned id) {
    for (Swing& s : g_sw)
        if (id && s.id == id) return &s;
    return nullptr;
}
void ClearSwings() {
    for (Swing& s : g_sw) s.id = 0;
    g_swCur = 0;
}
struct HitRec {
    bool on;
    double at;
    int hits;
    ImpactRec r;
};
HitRec g_hitQ[8];
unsigned g_pitchRng = 0x2f6b9e1du;
void PlaySfx(int i, float gain = 1.f) {
    vm::GunSound& s = Wp().meleeSfx[i];
    if (!s.Loaded()) return;
    const vm::Settings& st = Wp().set;
    float v = st.meleeSfxVolume[i];
    float pitch = 1.f;
    const float lo = st.meleeSfxPitch[i][0], hi = st.meleeSfxPitch[i][1];
    if (hi > lo) {
        g_pitchRng = g_pitchRng * 1664525u + 1013904223u;
        float c = lo + (hi - lo) * static_cast<float>(g_pitchRng >> 8) / 16777216.f;
        pitch = std::exp2(c / 1200.f);
    } else if (lo != 0.f) {
        pitch = std::exp2(lo / 1200.f);
    }
    s.Play((v >= 0.f ? v : kSfxDefVol[i]) * st.meleeSfxMaster * gain, false, pitch);
    ++g_sfxCount[i];
}
void PlayHit(int hits, const ImpactRec& r, double now) {
    if (r.im.main < 0) return;
    float gain = hits > 1 ? (hits >= 4 ? 1.45f : 1.f + 0.15f * (hits - 1)) : 1.f;
    const bool heavy = r.act == kHeavy1 || r.act == kHeavy2 || r.act == kHeavy3;
    PlaySfx(heavy && Wp().meleeSfx[kSfxHitHeavy].Loaded() ? kSfxHitHeavy : r.im.main, gain);
    PlaySfx(kSfxHitBody);
    PlaySfx(kSfxHitL4);
    PlaySfx(kSfxHitGore);
    if (r.im.tail >= 0) PlaySfx(r.im.tail);
    if (!Wp().set.meleeSfxSwingCut) {
    } else if (Swing* s = FindSwing(r.sw); s && r.act != kPush) {
        s->id = 0;
        if (g_swCur == r.sw) g_swCur = 0;
    } else if (r.sw && r.sw == g_swPlayed && r.act != kPush) {
        Wp().meleeSfx[kSfxSwing].FadeAll();
        Wp().meleeSfx[kSfxSwingLayer].FadeAll();
        ++g_swCutN;
        g_swPlayed = 0;
    }
    if (g_hitTraceN < 64) g_hitTrace[g_hitTraceN++] = {r.act, static_cast<float>(now - r.t), static_cast<float>(r.contact - r.t)};
}
void BlockSound() {
    PlaySfx(kSfxBlock);
    PlaySfx(kSfxBlockTail);
}
void SwingStep(double now) {
    for (Swing& s : g_sw) {
        if (!s.id || now < s.due) continue;
        if (s.act == kPush) {
            PlaySfx(kSfxPush);
        } else {
            PlaySfx(kSfxSwing);
            PlaySfx(kSfxSwingLayer);
            g_swPlayed = s.id;
        }
        if (g_swTraceN < 64) g_swTrace[g_swTraceN++] = {s.act, static_cast<float>(now - s.start), static_cast<float>(s.due - s.start)};
        if (g_swCur == s.id) g_swCur = 0;
        s.id = 0;
    }
    for (HitRec& h : g_hitQ) {
        if (!h.on || now < h.at) continue;
        h.on = false;
        if (g_fp) PlayHit(h.hits, h.r, now);
    }
}
void HitResult(int hits, const ImpactRec& r, double now) {
    if (hits <= 0 || r.im.main < 0) return;
    HitRec* slot = &g_hitQ[0];
    for (HitRec& h : g_hitQ)
        if (!h.on || (slot->on && h.at < slot->at)) slot = &h;
    if (slot->on && g_fp) PlayHit(slot->hits, slot->r, now);
    *slot = {true, r.contact > now ? r.contact : now, hits, r};
}
void Go(int next, double now, const char* why) {
    if (g_log) g_log("melee: %.3f %s -> %s (%s)", now - g.start, kDef[g.act].name, kDef[next].name, why);
    if (g_traceN < 256) g_trace[g_traceN++] = next;
    if (Swing* s = FindSwing(g_swCur); s && now < s->due) s->id = 0;
    g_swCur = 0;
    g.act = next;
    g.start = now;
    g.released = false;
    g.struck = false;
    int c = Wp().rend.FindClip(kDef[next].clip);
    MPlay(c >= 0 ? c : g_an.idle, now, kDef[next].enter, kDef[next].scale);
    if (IsAttack(next) || next == kPush) {
        Swing* s = &g_sw[0];
        for (Swing& x : g_sw)
            if (!x.id || (s->id && x.start < s->start)) s = &x;
        *s = {g_swNext++, next, now, now + kDef[next].swingAt / kDef[next].scale};
        if (!g_swNext) g_swNext = 1;
        g_swCur = s->id;
    }
}
void TryStrike(double now) {
    const Def& cd = kDef[g.act];
    if (cd.strike < 0 || g.struck || now - g.start < cd.hitAt / cd.scale - StrikeLead()) return;
    g.struck = true;
    PushImpact({kImpact[g.act], now, g.start + cd.hitAt / cd.scale, g_swCur, g.act});
    InterlockedExchange(&g_meleeStrike, cd.strike + 1);
    if (g_strikeN < 64) g_strikeTrace[g_strikeN++] = cd.strike;
    if (g_log) g_log("melee: %.3f %s strike %d", now - g.start, cd.name, cd.strike);
}
}
void MeleeStep(vm::Look& l, double now) {
    using namespace melee;
    if (g.model != g_modelIdx) {
        g.model = g_modelIdx;
        g.act = kDraw;
        g.start = now;
        g.pending = false;
        ClearSwings();
        for (HitRec& h : g_hitQ) h.on = false;
    }
    if (InterlockedExchange(&g_reqDraw, 0)) {
        g.act = kDraw;
        g.start = now;
        int c = Wp().rend.FindClip("draw");
        MPlay(c >= 0 ? c : g_an.idle, now, 0.1);
        ClearSwings();
    }
    LocoUpdate(now);
    while (g_meleeHitQHead != g_meleeHitQTail) {
        LONG hn = g_meleeHitQ[g_meleeHitQHead & 31];
        MemoryBarrier();
        InterlockedIncrement(&g_meleeHitQHead);
        ImpactRec r;
        if (PopImpact(now, r)) HitResult(static_cast<int>(hn), r, now);
    }
    LONG in = g_fp && !g_switching ? g_meleeIn : 0;
    bool atk = (in & 1) != 0, blk = (in & 2) != 0;
    if (atk && !g.prevAtk) {
        g.pressAt = now;
        g.pending = true;
    }
    g.prevAtk = atk;
    if (g.pending && now - g.pressAt > 0.5) g.pending = false;
    bool press = g.pending;
    TryStrike(now);
    const Def& d = kDef[g.act];
    double t = now - g.start;
    const double s = d.scale;
    if (g.act == kIdle) {
        if (blk) Go(kBlock, now, "block held");
        else if (press) g.pending = false, Go(kDefault, now, "press");
    } else if (g.act == kDraw) {
        if (t >= d.total) g.act = kIdle, g.start = now;
    } else if (IsCharge(g.act)) {
        if (blk) {
            Go(kBlock, now, "block during charge");
        } else {
            if (!atk && !g.released) g.released = true, g.relAt = t;
            if (g.released && g.relAt <= 0.3) Go(d.lightTo, now, "released <= 0.3 s");
            else if (g.released && t >= d.heavyStart) Go(d.heavyTo, now, "released late");
            else if (!g.released && t >= 1.0) Go(d.heavyTo, now, "held 1.0 s");
        }
    } else if (g.act == kPush) {
        if (atk && blk && t >= 0.2 && t <= 0.8) Go(kPushStab, now, "attack + block held");
        else if (blk && t >= d.blockFrom) Go(kBlock, now, "back to block");
        else if (!blk && press && t >= d.chainStart) g.pending = false, Go(kDefault, now, "press after push");
        else if (t >= d.total) g.act = kIdle, g.start = now;
    } else if (g.act == kBlock) {
        if (!blk) {
            if (g_log) g_log("melee: %.3f block -> idle (block released)", t);
            if (g_traceN < 256) g_trace[g_traceN++] = kIdle;
            g.act = kIdle;
            g.start = now;
            PlayRest(now, 0.2);
        } else if (press && t >= 0.2) {
            g.pending = false;
            Go(kPush, now, "press while blocking");
        }
    } else if (IsAttack(g.act)) {
        if (blk && t >= d.blockFrom / s) {
            Go(kBlock, now, "block");
        } else if (press && !blk && t >= d.chainStart / s) {
            g.pending = false;
            Go(t < d.chainEnd / s ? d.chainTo : kDefault, now, t < d.chainEnd / s ? "chain" : "press after chain window");
        } else if (d.total > 0 && t >= d.total / s) {
            if (g_log) g_log("melee: %.3f %s done", t, d.name);
            g.act = kIdle;
            g.start = now;
        }
    }
    TryStrike(now);
    SwingStep(now);
    if (g_an.cur >= 0 && g_an.cur != g_an.idle && !Wp().rend.ClipLoops(g_an.cur)) {
        float len = Wp().rend.ClipDuration(g_an.cur) - 1.f / 60.f;
        for (const Exit& e : kExit) {
            int from = Wp().rend.FindClip(e.from);
            if (from == g_an.cur && ClipT(now) >= len - e.blend) {
                if (!strcmp(e.to, "idle")) {
                    PlayRest(now, e.blend);
                } else {
                    int to = Wp().rend.FindClip(e.to);
                    MPlay(to >= 0 ? to : g_an.idle, now, e.blend);
                }
                break;
            }
        }
    }
    if (g.act == kIdle && IsRestClip(g_an.cur)) {
        int want = RestClip();
        if (want != g_an.cur) {
            const int sprint = Wp().rend.FindClip("sprint"), tac = Wp().rend.FindClip("tac_sprint");
            double rate = LocoRate(want);
            double fade = want == tac ? 0.3 : want == sprint ? 0.25 : (IsSprintClip(g_an.cur) || g_loco.want == 0) ? 0.2 : 0.3;
            MPlay(want, now, fade, rate);
            const int kind = want == tac ? 4 : want == sprint ? 3 : g_loco.want;
            if (g_locoTraceN < 64) g_locoTrace[g_locoTraceN++] = {kind, now, static_cast<float>(rate), g_loco.speed};
            if (g_log)
                g_log("melee: loco -> %s (speed %.2f, rate %.2f, fade %.2f)",
                      kind == 4 ? "tac_sprint" : kind == 3 ? "sprint" : kind == 2 ? "run" : kind == 1 ? "walk" : "idle", g_loco.speed, rate, fade);
        } else if (want == Wp().rend.FindClip("run")) {
            double rate = LocoRate(want);
            if (std::fabs(rate - g_an.curRate) > 0.01) {
                double ct = ClipT(now);
                g_an.curRate = rate;
                g_an.curStart = now - ct / rate;
            }
        }
        if (g_an.cur == Wp().rend.FindClip("run") && static_cast<float>(g_an.curRate) > g_runRateMax) g_runRateMax = static_cast<float>(g_an.curRate);
    }
    l.clip = g_an.cur;
    l.clipTime = static_cast<float>(ClipT(now));
    l.clipPrev = g_an.prev;
    l.clipTimePrev = static_cast<float>((now - g_an.prevStart) * g_an.prevRate);
    l.blend = g_an.fadeLen > 0 ? static_cast<float>((now - g_an.fadeStart) / g_an.fadeLen) : 1.f;
    if (l.blend >= 1.f) {
        l.blend = 1.f;
        l.clipPrev = -1;
        g_snap.on = false;
    } else if (g_snap.on) {
        l.clipPrev = g_snap.captured ? -2 : -3;
        g_snap.captured = true;
    }
}
