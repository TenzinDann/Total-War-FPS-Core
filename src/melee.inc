volatile LONG g_meleeIn = 0;
bool MeleeWeapon() {
    return Wp().rend.Clips() && (Wp().rend.FindClip("m_light_stab") >= 0 || Wp().rend.FindClip("m_gs_light_left") >= 0 ||
                                 Wp().rend.FindClip("m_gh_light_left") >= 0 || Wp().rend.FindClip("m_hb_light_left") >= 0);
}
volatile LONG g_meleeOn = 0, g_meleeStrike = 0;
volatile LONG g_meleeCat = 0;
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
    float blockEarly = 0;
    float swingPeak = 0;
    float chainSplit = 0;
    int chainEarlyTo = -1;
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
    bool blkFree = false;
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
           kSfxHitGore, kSfxTailStab, kSfxTailSlash, kSfxBlock, kSfxBlockTail, kSfxHitSlashLight, kSfxHitL4, kSfxHitHeavy,
           kSfxSwingHeavy };
static_assert(kSfxSwingHeavy + 1 == vm::kMeleeSfx, "melee sfx layers");
const float kSfxDefVol[vm::kMeleeSfx] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
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
struct Moveset {
    const char* detect;
    const Def* def;
    const Exit* exit;
    int exits;
    const Impact* impact;
    float pushStabFrom, pushStabTo;
    float heldHeavy;
    const char* blockEnd;
    float blockEndFade;
    float lightUntil = 0.3f;
    const char* pushToBlock = nullptr;
    float pushToBlockFade = 0.2f;
    int finishVariants = 1;
    float lightGuard = 0.f;
};
const Moveset kSpear = {"m_light_stab", kDef, kExit, static_cast<int>(sizeof kExit / sizeof *kExit), kImpact, 0.2f, 0.8f, 1.0f, nullptr, 0.2f};
const Def kGsDef[kActs] = {
    {"idle", "idle", 0.2f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"draw", "draw", 0.1f, 1.f, 0.8f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"default", "m_gs_charge_chop", 0.3f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy3, 0.65f, -1, 0, 0},
    {"default_left", "m_gs_charge_left", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy1, 0.65f, -1, 0, 0},
    {"default_right", "m_gs_charge_right", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight2, kHeavy2, 0.65f, -1, 0, 0},
    {"default_chop", "m_gs_charge_chop", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy3, 0.65f, -1, 0, 0},
    {"(unused)", "m_gs_charge", 0.3f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy1, 0.65f, -1, 0, 0},
    {"light_attack_left", "m_gs_light_left", 0.15f, 1.215f, 1.8f, 0.85f, 1.3f, kDefaultRight, 0.65f, -1, -1, 0, kStrikeSweep, 0.53f, 0.5f, 0.3f, 0.65f},
    {"light_attack_right", "m_gs_light_right", 0.15f, 1.215f, 1.86f, 0.85f, 1.15f, kDefaultLeft, 0.7f, -1, -1, 0, kStrikeSweep, 0.5f, 0.5f, 0.3f, 0.6333f},
    {"(unused)", "m_gs_light_left", 0.15f, 1.215f, 1.8f, 0.85f, 1.3f, kDefaultRight, 0.65f, -1, -1, 0, kStrikeSweep, 0.53f, 0.5f, 0.3f, 0.65f},
    {"heavy_attack_left", "m_gs_heavy_left", 0.1f, 1.f, 2.33f, 0.65f, kInf, kDefaultRight, 0.5f, -1, -1, 0, kStrikeHeavySweep, 0.1157f, 0.f, 0.f, 0.1393f},
    {"heavy_attack_right", "m_gs_heavy_right", 0.1f, 1.f, 2.33f, 0.65f, kInf, kDefaultLeftHeavy, 0.5f, -1, -1, 0, kStrikeHeavySweep, 0.12f, 0.f, 0.f, 0.1445f},
    {"heavy_attack_chop", "m_gs_heavy_chop", 0.1f, 1.f, 2.33f, 0.65f, kInf, kDefaultLeft, 0.5f, -1, -1, 0, kStrikeHeavySweep, 0.18f, 0.f, 0.f, 0.2333f},
    {"push", "m_gs_push", 0.0f, 1.f, 0.8f, 0.4f, kInf, kDefault, 0.4f, -1, -1, 0, kStrikeBash, 0.05f, 0.0333f},
    {"light_attack_bopp", "m_gs_bopp", 0.2f, 1.215f, 1.86f, 0.85f, kInf, kDefault, 0.75f, -1, -1, 0, kStrikeSweep, 0.32f, 0.2333f, 0.f, 0.35f},
    {"block", "m_gs_parry_pose", 0.1f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
};
const Exit kGsExit[] = {{"draw", "idle", 0.2f},
                        {"m_gs_light_left", "m_gs_swing_finished", 0.2f},
                        {"m_gs_light_right", "m_gs_swing_finished", 0.2f},
                        {"m_gs_heavy_left", "m_gs_swing_finished", 0.2f},
                        {"m_gs_heavy_right", "m_gs_swing_finished", 0.2f},
                        {"m_gs_heavy_chop", "m_gs_swing_finished", 0.2f},
                        {"m_gs_bopp", "m_gs_swing_finished", 0.2f},
                        {"m_gs_swing_finished", "idle", 0.2f},
                        {"m_gs_push", "idle", 0.2f},
                        {"m_gs_parry_end", "idle", 0.2f},
                        {"m_gs_parry_end_f2", "idle", 0.2f},
                        {"m_gs_parry_end_f3", "idle", 0.2f},
                        {"m_gs_parry_end_f4", "idle", 0.2f}};
const Impact kGsImpact[kActs] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
                                 {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash},
                                 {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash},
                                 {kSfxHitBash, -1}, {kSfxHitSlash, kSfxTailSlash}, {-1, -1}};
const Moveset kGreatsword = {"m_gs_light_left", kGsDef, kGsExit, static_cast<int>(sizeof kGsExit / sizeof *kGsExit), kGsImpact,
                             0.4f, 0.8f, 1.2f, "m_gs_parry_end", 0.0f};
const Def kGhDef[kActs] = {
    {"idle", "idle", 0.2f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"draw", "draw", 0.1f, 1.f, 0.8f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"default", "m_gh_charge", 0.3f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy1, 0.5f, -1, 0, 0},
    {"default_left", "m_gh_charge", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight2, kHeavy1, 0.5f, -1, 0, 0},
    {"default_right", "m_gh_charge_right", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight3, kHeavy1, 0.5f, -1, 0, 0},
    {"(unused)", "m_gh_charge_left", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight2, kHeavy2, 0.5f, -1, 0, 0},
    {"(unused)", "m_gh_charge", 0.3f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy1, 0.5f, -1, 0, 0},
    {"light_attack_left", "m_gh_light_left", 0.2f, 0.6f, 2.5f, 0.4167f, 1.8f, kDefaultLeft, 0.4167f, -1, -1, 0, kStrikeStab, 0.1f, 0.0833f, 0.3f, 0.1667f},
    {"light_attack_right", "m_gh_light_right", 0.2f, 0.6f, 2.5f, 0.5333f, 1.8f, kDefault, 0.5333f, -1, -1, 0, kStrikeStab, 0.1833f, 0.1667f, 0.3f, 0.25f},
    {"light_attack_left_up", "m_gh_light_left_up", 0.15f, 0.9f, 2.5f, 0.75f, 1.8f, kDefault, 0.7f, -1, -1, 0, kStrikeStab, 0.3f, 0.1667f, 0.3f, 0.35f},
    {"heavy_attack_left", "m_gh_heavy_left", 0.2f, 1.f, 2.25f, 0.65f, kInf, kDefaultLeft, 0.7f, -1, -1, 0, kStrikeHeavySweep, 0.1633f, 0.0667f, 0.3f, 0.2167f},
    {"heavy_attack_right", "m_gh_heavy_right", 0.2f, 1.f, 2.25f, 0.65f, kInf, kDefault, 0.7f, -1, -1, 0, kStrikeHeavySweep, 0.35f, 0.2333f, 0.3f, 0.4167f},
    {"(unused)", "m_gh_heavy_left", 0.2f, 1.f, 2.25f, 0.65f, kInf, kDefaultLeft, 0.7f, -1, -1, 0, kStrikeHeavySweep, 0.3f, 0.2f, 0.3f, 0.35f},
    {"push", "m_gh_push", 0.0f, 1.f, 0.8f, 0.3f, kInf, kDefault, 0.45f, -1, -1, 0, kStrikeBash, 0.05f, 0.0333f},
    {"light_attack_push_left_up", "m_gh_push_left_up", 0.15f, 0.9f, 2.5f, 0.55f, 1.8f, kDefaultLeft, 0.7f, -1, -1, 0, kStrikeSweep, 0.33f, 0.2667f, 0.3f, 0.35f},
    {"block", "m_gh_parry_pose", 0.1f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
};
const Exit kGhExit[] = {{"draw", "idle", 0.2f},
                        {"m_gh_light_left", "m_gh_swing_finished", 0.3f},
                        {"m_gh_light_right", "m_gh_swing_finished", 0.3f},
                        {"m_gh_light_left_up", "m_gh_swing_finished", 0.3f},
                        {"m_gh_heavy_left", "m_gh_swing_finished", 0.3f},
                        {"m_gh_heavy_right", "m_gh_swing_finished", 0.3f},
                        {"m_gh_push_left_up", "m_gh_swing_finished", 0.3f},
                        {"m_gh_swing_finished", "idle", 0.3f},
                        {"m_gh_swing_finished_02", "idle", 0.3f},
                        {"m_gh_swing_finished_03", "idle", 0.3f},
                        {"m_gh_push", "idle", 0.3f},
                        {"m_gh_push_to_parry", "m_gh_parry_pose", 0.1f}};
const Impact kGhImpact[kActs] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
                                 {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash},
                                 {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash},
                                 {kSfxHitBash, -1}, {kSfxHitSlash, kSfxTailSlash}, {-1, -1}};
const Moveset kGreathammer = {"m_gh_light_left", kGhDef, kGhExit, static_cast<int>(sizeof kGhExit / sizeof *kGhExit), kGhImpact,
                              0.35f, 0.8f, 1.5f, nullptr, 0.2f, 0.35f, "m_gh_push_to_parry", 0.2f, 3};
const Def kHbDef[kActs] = {
    {"idle", "idle", 0.2f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"draw", "draw", 0.1f, 1.f, 0.8f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"default", "m_hb_charge_l", 0.5f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy1, 0.5f, -1, 0, 0},
    {"default_light_2", "m_hb_charge_l", 0.5f, 1.f, 0.f, 0, 0, -1, 0, kLight2, kHeavy1, 0.5f, -1, 0, 0},
    {"default_right", "m_hb_charge_r", 0.5f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy2, 0.5f, -1, 0, 0},
    {"default_light_3", "m_hb_charge_l", 0.5f, 1.f, 0.f, 0, 0, -1, 0, kLight3, kHeavy1, 0.5f, -1, 0, 0},
    {"(unused)", "m_hb_charge", 0.5f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy3, 0.5f, -1, 0, 0},
    {"light_swing_left", "m_hb_light_left", 0.2f, 1.f, 1.35f, 0.55f, 1.125f, kDefaultLeft, 0.5f, -1, -1, 0, kStrikeStab, 0.15f, 0.06f, 0.15f, 0.22f},
    {"light_swing_right", "m_hb_light_mid", 0.2f, 1.f, 1.35f, 0.55f, kInf, kDefault, 0.5f, -1, -1, 0, kStrikeStab, 0.15f, 0.06f, 0.15f, 0.22f},
    {"light_stab_long", "m_hb_light_long", 0.2f, 0.84f, 1.5f, 0.55f, kInf, kDefault, 0.5f, -1, -1, 0, kStrikeStab, 0.26f, 0.12f, 0.15f, 0.2333f},
    {"heavy_swing_left", "m_hb_heavy_l", 0.2f, 1.f, 2.0f, 0.55f, kInf, kDefaultRight, 0.5f, -1, -1, 0, kStrikeHeavySweep, 0.15f, 0.06f, 0.15f, 0.22f},
    {"heavy_swing_right", "m_hb_heavy_r", 0.2f, 1.f, 2.0f, 0.55f, kInf, kDefault, 0.5f, -1, -1, 0, kStrikeHeavySweep, 0.15f, 0.06f, 0.15f, 0.22f},
    {"heavy_attack_stab", "m_hb_heavy_stab", 0.1f, 1.04f, 2.25f, 0.5f, kInf, kDefault, 0.45f, -1, -1, 0, kStrikeHeavy, 0.12f, 0.1667f, 0.15f, 0.15f},
    {"push", "m_hb_push", 0.0f, 1.f, 0.8f, 0.25f, kInf, kDefault, 0.45f, -1, -1, 0, kStrikeBash, 0.05f, 0.0667f, 0.f, 0.1167f},
    {"light_attack_down", "m_hb_light_last", 0.15f, 0.8f, 2.5f, 0.5f, 1.8f, kDefaultRight, 0.45f, -1, -1, 0, kStrikeStab, 0.33f, 0.2f, 0.f, 0.35f},
    {"block", "m_hb_parry_pose", 0.2f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
};
const Exit kHbExit[] = {{"draw", "idle", 0.2f}, {"m_hb_light_left", "idle", 0.1f}, {"m_hb_light_long", "idle", 0.1f},
                        {"m_hb_light_mid", "idle", 0.1f}, {"m_hb_heavy_l", "idle", 0.1f}, {"m_hb_heavy_r", "idle", 0.1f},
                        {"m_hb_heavy_stab", "idle", 0.1f}, {"m_hb_light_last", "idle", 0.1f}, {"m_hb_push", "idle", 0.1f},
                        {"m_hb_light_left_v2", "idle", 0.1f}, {"m_hb_light_left_v3", "idle", 0.1f}, {"m_hb_light_mid_v2", "idle", 0.1f},
                        {"m_hb_light_mid_v3", "idle", 0.1f}, {"m_hb_light_long_v2", "idle", 0.1f}, {"m_hb_light_long_v3", "idle", 0.1f}};
const Impact kHbImpact[kActs] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
                                 {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash},
                                 {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash},
                                 {kSfxHitBash, -1}, {kSfxHitSlash, kSfxTailSlash}, {-1, -1}};
struct HbLive {
    Def d[kActs];
    HbLive() {
        for (int i = 0; i < kActs; ++i) d[i] = kHbDef[i];
    }
} g_hbLive;
const Moveset kHalberd = {"m_hb_light_left", g_hbLive.d, kHbExit, static_cast<int>(sizeof kHbExit / sizeof *kHbExit), kHbImpact,
                          0.25f, 0.8f, 1.5f, nullptr, 0.2f};
const Def kSp2Def[kActs] = {
    {"idle", "idle", 0.2f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"draw", "draw", 0.1f, 1.f, 0.8f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
    {"default", "m_sp_charge", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy1, 0.4f, -1, 0, 0},
    {"(unused)", "m_sp_charge", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy1, 0.4f, -1, 0, 0},
    {"(unused)", "m_sp_charge", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy1, 0.4f, -1, 0, 0},
    {"default_left_heavy", "m_charge_diag", 0.2f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy2, 0.5f, -1, 0, 0},
    {"default_stab_heavy", "m_charge_stab", 0.3f, 1.f, 0.f, 0, 0, -1, 0, kLight1, kHeavy3, 0.5f, -1, 0, 0},
    {"thrust", "m_sp_thrust", 0.05f, 1.f, 0.8f, 0.36f, kInf, kDefault, 0.f, -1, -1, 0, kStrikeStab, 0.27f, 0.20f, 0, 0.29f},
    {"(unused)", "m_sp_thrust", 0.05f, 1.f, 0.8f, 0.36f, kInf, kDefault, 0.f, -1, -1, 0, kStrikeStab, 0.27f, 0.20f, 0, 0.29f},
    {"(unused)", "m_sp_thrust", 0.05f, 1.f, 0.8f, 0.36f, kInf, kDefault, 0.f, -1, -1, 0, kStrikeStab, 0.27f, 0.20f, 0, 0.29f},
    {"heavy_attack", "m_sweep", 0.05f, 1.035f, 1.0f, 0.5f, 1.0f, kDefaultLeftHeavy, 0.5f, -1, -1, 0, kStrikeHeavySweep, 0.27f, 0.2675f},
    {"heavy_attack_right", "m_heavy_down_right", 0.1f, 1.0f, 2.25f, 0.5f, 1.05f, kDefaultStabHeavy, 0.4f, -1, -1, 0, kStrikeHeavy, 0.30f, 0.2188f},
    {"heavy_attack_stab", "m_heavy_stab", 0.1f, 1.035f, 2.25f, 0.5f, 1.05f, kDefault, 0.4f, -1, -1, 0, kStrikeHeavy, 0.25f, 0.2403f},
    {"push", "m_push", 0.0f, 1.0f, 0.8f, 0.2f, kInf, kDefault, 0.5f, -1, -1, 0, kStrikeBash, 0.05f, 0.f},
    {"light_attack_stab_postpush", "m_push_stab", 0.1f, 1.17f, 1.1f, 0.4f, kInf, kDefaultLeftHeavy, 0.45f, -1, -1, 0, kStrikeStab, 0.30f, 0.0667f},
    {"block", "m_parry_pose", 0.2f, 1.f, 0.f, 0, 0, -1, 0, -1, -1, 0, -1, 0, 0},
};
const Exit kSp2Exit[] = {{"draw", "idle", 0.2f}, {"m_sp_thrust", "idle", 0.25f}, {"m_sp_thrust_v2", "idle", 0.25f},
                         {"m_sp_thrust_v3", "idle", 0.25f}, {"m_sp_thrust_v4", "idle", 0.25f},
                         {"m_sweep", "m_swing_finished", 0.1f}, {"m_heavy_down_right", "m_swing_finished", 0.1f},
                         {"m_heavy_stab", "m_swing_finished", 0.1f}, {"m_push_stab", "m_swing_finished", 0.1f},
                         {"m_swing_finished", "idle", 0.2f}, {"m_push", "idle", 0.2f}, {"m_parry_pose", "m_parry_loop", 0.2f}};
const Impact kSp2Impact[kActs] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1},
                                  {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash}, {kSfxHitSlash, kSfxTailSlash},
                                  {kSfxHitSlashLight, kSfxTailSlash}, {kSfxHitSlashHeavy, kSfxTailSlash}, {kSfxHitStab, kSfxTailStab},
                                  {kSfxHitBash, -1}, {kSfxHitSlash, kSfxTailSlash}, {-1, -1}};
const Moveset kSpear2 = {"m_sp_thrust", kSp2Def, kSp2Exit, static_cast<int>(sizeof kSp2Exit / sizeof *kSp2Exit), kSp2Impact,
                         0.2f, 0.8f, 1.0f, nullptr, 0.2f, 0.3f, nullptr, 0.2f, 1, 0.30f};
void HbLiveRefresh() {
    static const void* last = nullptr;
    static float lt = -9.f, le = -9.f, ls = -9.f, lw = -9.f;
    const auto& st = Wp().set;
    float wsum = 0.f;
    for (int i = 0; i < 8; ++i) wsum += st.meleeLight1Times[i] * (i + 1) + st.meleeLight2Times[i] * (i + 11);
    if (last == &Wp() && lt == st.meleeLightTrim && le == st.meleeLightEnter && ls == st.meleeLightScale && lw == wsum) return;
    last = &Wp(), lt = st.meleeLightTrim, le = st.meleeLightEnter, ls = st.meleeLightScale, lw = wsum;
    for (int i = 0; i < kActs; ++i) g_hbLive.d[i] = kHbDef[i];
    for (int a : {static_cast<int>(kLight1), static_cast<int>(kLight2)}) {
        Def& d = g_hbLive.d[a];
        const float k = st.meleeLightTrim;
        const float* tt = a == kLight1 ? st.meleeLight1Times : st.meleeLight2Times;
        if (tt[0] >= 0.f) {
            d.hitAt = tt[0], d.swingAt = tt[1], d.swingPeak = tt[2], d.chainStart = tt[3], d.blockFrom = tt[5], d.blockEarly = tt[6], d.total = tt[7];
            if (tt[4] >= 0.f) d.chainEnd = tt[4];
        } else if (k > 0.f) {
            auto sh = [k](float& v, float lo) {
                if (v < kInf) v = std::max(lo, v - k);
            };
            sh(d.total, 0.05f), sh(d.chainStart, 0.f), sh(d.chainEnd, 0.f), sh(d.blockFrom, 0.f), sh(d.hitAt, 0.f), sh(d.swingAt, 0.f);
            if (d.blockEarly > 0.f) d.blockEarly = std::max(0.f, d.blockEarly - k);
            if (d.swingPeak > 0.f) d.swingPeak = std::max(0.001f, d.swingPeak - k);
        }
        if (st.meleeLightEnter >= 0.f) d.enter = st.meleeLightEnter;
        if (st.meleeLightScale > 0.f) d.scale = st.meleeLightScale;
    }
    if (g_log) g_log("melee: halberd lights trim %.3f s, enter %.3f s, scale %.3f", static_cast<double>(st.meleeLightTrim > 0.f ? st.meleeLightTrim : 0.f), static_cast<double>(g_hbLive.d[kLight1].enter), static_cast<double>(g_hbLive.d[kLight1].scale));
}
const Moveset& Ms() {
    if (Wp().rend.FindClip(kSpear2.detect) >= 0) return kSpear2;
    if (Wp().rend.FindClip(kHalberd.detect) >= 0) return HbLiveRefresh(), kHalberd;
    if (Wp().rend.FindClip(kGreathammer.detect) >= 0) return kGreathammer;
    return Wp().rend.FindClip(kGreatsword.detect) >= 0 ? kGreatsword : kSpear;
}
int CatOf(const Moveset& m) {
    if (&m == &kSpear2 || &m == &kSpear) return kMeleeCatShieldSpear;
    if (&m == &kGreathammer) return kMeleeCatHammer;
    if (&m == &kHalberd) return kMeleeCatHalberd;
    return kMeleeCatSword;
}
bool Guarding(double now) {
    const float lg = Ms().lightGuard;
    if (lg <= 0.f) return false;
    return (g.act >= kLight1 && g.act <= kLight3) || (g.act == kDefault && now - g.start < lg);
}
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
    int kills;
    ImpactRec r;
};
HitRec g_hitQ[8];
unsigned g_pitchRng = 0x2f6b9e1du;
unsigned g_finishRng = 0x6d2b79f5u;
int g_finishTrace[64];
int g_finishTraceN = 0;
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
        Wp().meleeSfx[kSfxSwingHeavy].FadeAll();
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
        } else if ((s.act == kHeavy1 || s.act == kHeavy2 || s.act == kHeavy3) && Wp().meleeSfx[kSfxSwingHeavy].Loaded()) {
            PlaySfx(kSfxSwingHeavy);
            g_swPlayed = s.id;
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
        if (g_fp) {
            PlayHit(h.hits, h.r, now);
            HitMark(h.kills > 0);
        }
    }
}
void HitResult(int hits, int kills, const ImpactRec& r, double now) {
    if (hits <= 0) return;
    HitRec* slot = &g_hitQ[0];
    for (HitRec& h : g_hitQ)
        if (!h.on || (slot->on && h.at < slot->at)) slot = &h;
    if (slot->on && g_fp) {
        PlayHit(slot->hits, slot->r, now);
        HitMark(slot->kills > 0);
    }
    *slot = {true, r.contact > now ? r.contact : now, hits, kills, r};
}
unsigned g_variantRng = 0x9e3779b9u;
int g_lastVariant[kActs] = {};
int g_variantTrace[64];
int g_variantTraceN = 0;
int PickVariant(int act, int base) {
    int idx[6] = {base};
    int n = 1;
    char nm[64];
    for (int v = 2; v <= 6; ++v) {
        snprintf(nm, sizeof nm, "%s_v%d", Ms().def[act].clip, v);
        int c = Wp().rend.FindClip(nm);
        if (c < 0) break;
        idx[n++] = c;
    }
    if (n < 2) return base;
    g_variantRng = g_variantRng * 1664525u + 1013904223u;
    const int last = g_lastVariant[act] - 1;
    int k = static_cast<int>((g_variantRng >> 8) % static_cast<unsigned>(last >= 0 ? n - 1 : n));
    if (last >= 0 && k >= last) ++k;
    g_lastVariant[act] = k + 1;
    if (g_variantTraceN < 64) g_variantTrace[g_variantTraceN++] = act * 10 + k;
    if (g_log) g_log("melee: %s variant %d of %d", Ms().def[act].name, k, n);
    return idx[k];
}
int g_blockVar = -1;
int g_blockForms = 0;
float g_enterOverride = -1.f;
constexpr float kBlockPushFade = 0.1f;
int CycleBlock(int base) {
    int idx[4] = {base};
    int n = 1;
    char nm[64];
    for (int v = 2; v <= 4; ++v) {
        snprintf(nm, sizeof nm, "%s_f%d", Ms().def[kBlock].clip, v);
        int c = Wp().rend.FindClip(nm);
        if (c < 0) break;
        idx[n++] = c;
    }
    g_blockVar = n > 1 ? (g_blockVar + 1) % n : 0;
    g_blockForms = n;
    if (g_log) g_log("melee: block form %d of %d", g_blockVar + 1, n);
    return idx[g_blockVar];
}
int BlockEndClip() {
    if (!Ms().blockEnd) return -1;
    if (g_blockVar > 0) {
        char nm[64];
        snprintf(nm, sizeof nm, "%s_f%d", Ms().blockEnd, g_blockVar + 1);
        int c = Wp().rend.FindClip(nm);
        if (c >= 0) return c;
    }
    return Wp().rend.FindClip(Ms().blockEnd);
}
bool IsBlockEnd(int clip) {
    if (clip < 0 || !Ms().blockEnd) return false;
    if (clip == Wp().rend.FindClip(Ms().blockEnd)) return true;
    char nm[64];
    for (int v = 2; v <= 4; ++v) {
        snprintf(nm, sizeof nm, "%s_f%d", Ms().blockEnd, v);
        if (clip == Wp().rend.FindClip(nm)) return true;
    }
    return false;
}
const int kMatchBones[3] = {23, 27, 117};
void BlockMatchStart(int c, double now) {
    const int prev = g_an.prev;
    if (c < 0 || !IsBlockEnd(prev) || g_an.curRate <= 0) return;
    const float tPrev = static_cast<float>((now - g_an.prevStart) * g_an.prevRate);
    float cur[9], m[12];
    for (int i = 0; i < 3; ++i) {
        if (!Wp().rend.BoneAt(prev, tPrev, kMatchBones[i], m)) return;
        cur[i * 3] = m[3], cur[i * 3 + 1] = m[7], cur[i * 3 + 2] = m[11];
    }
    const float dur = Wp().rend.ClipDuration(c);
    float best = 1e30f, bt = 0.f;
    for (float t = 0.f; t <= dur + 1e-4f; t += 1.f / 60.f) {
        float d = 0.f;
        for (int i = 0; i < 3; ++i) {
            if (!Wp().rend.BoneAt(c, t, kMatchBones[i], m)) return;
            const float dx = m[3] - cur[i * 3], dy = m[7] - cur[i * 3 + 1], dz = m[11] - cur[i * 3 + 2];
            d += dx * dx + dy * dy + dz * dz;
        }
        if (d < best) best = d, bt = t;
    }
    g_an.curStart = now - bt / g_an.curRate;
    if (g_log) g_log("melee: block form %d starts at %.3f s (closest to the lowering pose, %.1f cm)", g_blockVar + 1, bt, std::sqrt(best / 3.f) * 100.f);
}
void Go(int next, double now, const char* why, const char* clip = nullptr, float fade = -1.f) {
    if (g_log) g_log("melee: %.3f %s -> %s (%s)", now - g.start, Ms().def[g.act].name, Ms().def[next].name, why);
    if (g_traceN < 256) g_trace[g_traceN++] = next;
    if (Swing* s = FindSwing(g_swCur); s && now < s->due) s->id = 0;
    g_swCur = 0;
    g.act = next;
    g.start = now;
    g.released = false;
    g.struck = false;
    g.blkFree = false;
    const Def& nd = Ms().def[next];
    const float enter = g_enterOverride >= 0.f ? g_enterOverride : nd.enter;
    g_enterOverride = -1.f;
    int c = clip ? Wp().rend.FindClip(clip) : -1;
    if (c >= 0) {
        MPlay(c, now, fade >= 0.f ? fade : nd.enter, nd.scale);
    } else {
        c = Wp().rend.FindClip(nd.clip);
        if (c >= 0) c = next == kBlock ? CycleBlock(c) : PickVariant(next, c);
        MPlay(c >= 0 ? c : g_an.idle, now, enter, nd.scale);
        if (next == kBlock) BlockMatchStart(c, now);
    }
    if (IsAttack(next) || next == kPush) {
        Swing* s = &g_sw[0];
        for (Swing& x : g_sw)
            if (!x.id || (s->id && x.start < s->start)) s = &x;
        float due = nd.swingAt / nd.scale;
        const float pre = Wp().set.meleeSfxSwingPre;
        if (next != kPush && pre >= 0.f && nd.swingPeak > 0.f) due = std::max(0.f, nd.swingPeak / nd.scale - pre);
        *s = {g_swNext++, next, now, now + due};
        if (!g_swNext) g_swNext = 1;
        g_swCur = s->id;
    }
}
void TryStrike(double now) {
    const Def& cd = Ms().def[g.act];
    if (cd.strike < 0 || g.struck || now - g.start < cd.hitAt / cd.scale - StrikeLead()) return;
    g.struck = true;
    PushImpact({Ms().impact[g.act], now, g.start + cd.hitAt / cd.scale, g_swCur, g.act});
    InterlockedExchange(&g_meleeStrike, (CatOf(Ms()) << 4 | cd.strike) + 1);
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
        if (PopImpact(now, r)) HitResult(static_cast<int>(hn & 0xffff), static_cast<int>(hn >> 16), r, now);
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
    if (!blk) g.blkFree = true;
    TryStrike(now);
    const Def& d = Ms().def[g.act];
    const Moveset& ms = Ms();
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
            if (g.released && g.relAt <= ms.lightUntil) Go(d.lightTo, now, "released early");
            else if (g.released && t >= d.heavyStart) Go(d.heavyTo, now, "released late");
            else if (!g.released && t >= ms.heldHeavy) Go(d.heavyTo, now, "held");
        }
    } else if (g.act == kPush) {
        if (atk && blk && t >= ms.pushStabFrom && t <= ms.pushStabTo) Go(kPushStab, now, "attack + block held");
        else if (blk && t >= d.blockFrom) Go(kBlock, now, "back to block", ms.pushToBlock, ms.pushToBlockFade);
        else if (!blk && press && t >= d.chainStart) g.pending = false, Go(kDefault, now, "press after push");
        else if (t >= d.total) g.act = kIdle, g.start = now;
    } else if (g.act == kBlock) {
        if (!blk) {
            if (g_log) g_log("melee: %.3f block -> idle (block released)", t);
            if (g_traceN < 256) g_trace[g_traceN++] = kIdle;
            g.act = kIdle;
            g.start = now;
            int be = BlockEndClip();
            if (be >= 0) MPlay(be, now, ms.blockEndFade);
            else PlayRest(now, 0.2);
        } else if (press && t >= 0.2) {
            g.pending = false;
            if (g_blockForms > 1 && Ms().def[kPush].enter < kBlockPushFade) {
                g_enterOverride = kBlockPushFade;
                if (g_log) g_log("melee: push from block form %d, fade %.2f s", g_blockVar + 1, kBlockPushFade);
            }
            Go(kPush, now, "press while blocking");
        }
    } else if (IsAttack(g.act)) {
        if (blk && (t >= d.blockFrom / s || (t < d.blockEarly / s && g.blkFree))) {
            Go(kBlock, now, "block");
        } else if (press && !blk && t >= d.chainStart / s) {
            g.pending = false;
            const int to = t >= d.chainEnd / s ? kDefault : (d.chainEarlyTo >= 0 && t < d.chainSplit / s) ? d.chainEarlyTo : d.chainTo;
            Go(to, now, t < d.chainEnd / s ? "chain" : "press after chain window");
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
        for (int ei = 0; ei < ms.exits; ++ei) {
            const Exit& e = ms.exit[ei];
            int from = Wp().rend.FindClip(e.from);
            if (from == g_an.cur && ClipT(now) >= len - e.blend) {
                if (!strcmp(e.to, "idle")) {
                    PlayRest(now, e.blend);
                } else {
                    int to = Wp().rend.FindClip(e.to);
                    if (ms.finishVariants > 1 && to >= 0 && strstr(e.to, "swing_finished")) {
                        g_finishRng = g_finishRng * 1664525u + 1013904223u;
                        int k = static_cast<int>((g_finishRng >> 8) % static_cast<unsigned>(ms.finishVariants));
                        if (k > 0) {
                            char nm[64];
                            snprintf(nm, sizeof nm, "%s_%02d", e.to, k + 1);
                            int v = Wp().rend.FindClip(nm);
                            if (v >= 0) to = v;
                        }
                        if (g_finishTraceN < 64) g_finishTrace[g_finishTraceN++] = k;
                    }
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
