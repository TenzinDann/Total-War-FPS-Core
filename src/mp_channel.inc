bool CheckRegStub(uintptr_t stub, uintptr_t factory, uintptr_t type, const char* name);
bool PrintsMatch(const PrintSet& s, const char* what);
unsigned UnitEntities(uintptr_t u, uintptr_t* ents, unsigned cap);
bool EntPos(uintptr_t e, float* q);
namespace mp {
constexpr uintptr_t kTypeProfiler904 = 0x406cc80, kRegProfiler904 = 0x4adca0, kFnProfilerNop904 = 0x2ece760, kFnRdBool904 = 0x1b9fc58,
                    kFnRdField904 = 0x1bab24c, kFnRdUnit904 = 0x2eaf754;
constexpr uintptr_t oRdError = 0x8, oBattleStep = 0x64720, oUnitNetId = 0x8, oEntHp = 0xb1c;
constexpr uintptr_t oBattleIsReplay = 0x645e0;
const CodePrint kPrints904[] = {{0x2ece760, 0x5b, 0x7b0bf0edb8cc1942ull},
                                {0x1b9fc58, 0x40, 0xb2aed5fc173f7fc7ull},
                                {0x1bab24c, 0x5f, 0xad321101fda22522ull},
                                {0x2eaf754, 0x90, 0x2e660c50610378f1ull},
                                {0x2f041c0, 0x9a, 0x763166299b328116ull},
                                {0x668ad4, 0x8b, 0x6767db014f05baaaull},
                                {0x2eeb29c, 0x53, 0x3fe38d7eb43d316eull},
                                {0x2eeedd8, 0x38, 0x1e40299fca8b7e54ull}};
constexpr uint32_t kMagic = 0x53504648;
constexpr uint32_t kProto = 4;
enum : uint32_t { kHello = 1, kFpsOn = 2, kFpsOff = 3, kKeep = 4, kShield = 5, kMelee = 6 };
const char* MsgName(uint32_t k) {
    return k == kHello ? "HELLO" : k == kFpsOn ? "FPS_ON" : k == kFpsOff ? "FPS_OFF" : k == kKeep ? "KEEP" : k == kShield ? "SHIELD"
           : k == kMelee ? "MELEE" : "?";
}
enum : uint32_t { kMfDamage = 1, kMfAlwaysHit = 2, kMfReact = 4, kMfEffects = 8 };
constexpr int kMeleeFields = 18;
enum : uint32_t { kFlKnock = 1, kFlShield = 2, kFlArmor = 4, kFlRefund = 8, kFlStaggerPartial = 16, kFlEarly = 32 };
constexpr uint32_t kFlHooks = kFlKnock | kFlShield | kFlArmor;
bool g_ok = false;
uint32_t g_nonce = 0;
uint32_t g_digest = 0;
uint32_t g_exeStamp = 0;
uint32_t g_features = 0;
volatile LONG g_seq = 0;
SRWLOCK g_sentLock = SRWLOCK_INIT;
struct Sent {
    uint32_t seq;
    double t;
} g_sent[64];
unsigned g_sentHead = 0;
volatile LONG g_lagMs = 0, g_lagMaxMs = 0, g_lagN = 0;
uintptr_t g_battle = 0;
double g_tHello = -1;
volatile LONG g_helloBack = 0, g_mismatch = 0, g_peers = 0;
volatile LONG g_helloAgain = 0;
SRWLOCK g_peerLock = SRWLOCK_INIT;
struct Peer {
    uint32_t nonce, proto, digest, exe, features;
} g_peer[16];
uintptr_t g_simWorld = 0;
struct Hero {
    uintptr_t unit;
    uint32_t from;
    int32_t orig[2][2];
    uint32_t want, flags;
    uintptr_t ent, origVt;
    uintptr_t* copy;
    uintptr_t triedVt;
    int32_t milli;
    float yaw, angle;
    uint32_t hpPrev, exempt, bracketHp, refundHp;
    bool hpInit, bracket;
    bool early;
    int32_t cnt[kCnt];
} g_hero[16];
int g_nHero = 0;
SRWLOCK g_heroLock = SRWLOCK_INIT;
struct VtCopy {
    uintptr_t of;
    uint32_t flags;
    uintptr_t* c;
} g_vtc[8];
int g_nVtc = 0;
uintptr_t g_psWorld = 0, g_psObj = 0;
LONG64 g_psFxQpc = 0;
struct MeleeMsg {
    uintptr_t unit;
    uint32_t from, seq;
    int kind, cat, flags;
    float o[3], d[2], rmax;
    MeleeCatCfg cc;
} g_mm[16];
int g_nMm = 0;
struct ShieldSent {
    int32_t milli;
    float yaw, angle;
    double t;
} g_shSent = {0, 0.f, 180.f, 0.0};
struct Pending {
    uint32_t kind;
    uintptr_t unit;
} g_pend[8];
int g_nPend = 0;
double g_tKeep = 0;
uint32_t Fnv(const unsigned char* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
uint32_t SelfDigest() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&SelfDigest), &self) || !self)
        return 0;
    auto* b = reinterpret_cast<const unsigned char*>(self);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(b);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(b + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].Characteristics & IMAGE_SCN_CNT_CODE) return Fnv(b + sec[i].VirtualAddress, sec[i].Misc.VirtualSize);
    return 0;
}
uintptr_t ScriptBattle() {
    uintptr_t sb = Ptr(g_base + kGlobalScript);
    return sb ? Ptr(sb + oScriptBattle) : 0;
}
bool Mode(uintptr_t battle) {
    if (cfg.mpForce) return true;
    uint64_t v = 1;
    if (!(g_mpFlagOk && battle && Rd(battle + oBattleIsMp, v))) v = 1;
    return v != 0;
}
bool RealMp(uintptr_t battle) {
    uint64_t v = 1;
    if (!(g_mpFlagOk && battle && Rd(battle + oBattleIsMp, v))) v = 1;
    return v != 0;
}
volatile LONG g_wantPlayers = 0, g_listAi = 0, g_listWatch = 0, g_listCalls = 0;
volatile uintptr_t g_listBattle = 0;
volatile LONG g_mePassive = 0;
uintptr_t g_passiveLogBattle = 0;
void SetPlayers(int n, int ai, int watch, int mePassive) {
    const uintptr_t b = ScriptBattle();
    const bool changed = b != g_listBattle || n != g_wantPlayers || ai != g_listAi || watch != g_listWatch || (mePassive != 0) != (g_mePassive != 0);
    InterlockedExchange(&g_wantPlayers, n);
    InterlockedExchange(&g_listAi, ai);
    InterlockedExchange(&g_listWatch, watch);
    InterlockedExchange(&g_mePassive, mePassive ? 1 : 0);
    g_listBattle = b;
    InterlockedIncrement(&g_listCalls);
    if (changed)
        Log("mp: player list from the battle script (battle %p): %d playing human(s), %d AI slot(s), %d watching%s%s", (void*)b, n, ai, watch,
            n < 0 ? " - read error" : n == 0 ? " - no list (single player, or not ready yet)" : "",
            mePassive ? "; this PC is a passive participant (plays units gifted by allies)" : "");
}
bool PassiveCanCommand(uintptr_t battle) {
    if (!g_mePassive || !battle || g_listBattle != battle || !RealMp(battle)) return false;
    unsigned char replay = 0;
    if (Rd(battle + oBattleIsReplay, replay) && replay) return false;
    if (g_passiveLogBattle != battle) {
        g_passiveLogBattle = battle;
        Log("mp: is_spectator is set on this PC but it is a passive participant - commands go out anyway, as the game's own unit orders do");
    }
    return true;
}
float Lag() {
    LONG n = g_lagN, ms = g_lagMs, mx = g_lagMaxMs;
    if (!n) return 0.3f;
    float s = 0.001f * static_cast<float>(ms > mx / 2 ? ms : mx / 2);
    return s < 0.05f ? 0.05f : s > 3.f ? 3.f : s;
}
using FnRdBool = uintptr_t (*)(uintptr_t rd, unsigned char* out);
using FnRdField = unsigned char (*)(uintptr_t rd, char tag, void* out, unsigned n);
using FnRdUnit = uintptr_t (*)(uintptr_t rd, uintptr_t* out);
int FindHero(uintptr_t unit) {
    for (int i = 0; i < g_nHero; ++i)
        if (g_hero[i].unit == unit) return i;
    return -1;
}
void ApplyAmmo(uintptr_t unit) {
    for (int w = 0; w < 2; ++w) {
        uintptr_t slot = unit + oUnitAmmo + w * kAmmoSlot;
        int32_t f = 1, n = 1;
        Rd(slot + oAmmoNoTake, f);
        Rd(slot, n);
        const int32_t one = 1;
        if (f != 1) Wr(slot + oAmmoNoTake, one);
        if (n == 0) Wr(slot, one);
    }
}
uint32_t LocalFlags() {
    return (cfg.noKnock && g_noKnockOk ? kFlKnock : 0u) | (cfg.shieldGame && g_shieldGameOk ? kFlShield : 0u) |
           (cfg.superArmor && g_armorOk ? kFlArmor : 0u) | (cfg.shieldRefund ? kFlRefund : 0u) | (cfg.shieldStagger ? kFlStaggerPartial : 0u) |
           (cfg.earlyFlight && g_shotOk && cfg.fireShooter ? kFlEarly : 0u);
}
bool EarlyFlightOn() { return S.active && S.mpMode && (LocalFlags() & kFlEarly) && g_ef.siteState > 0; }
uint32_t SupportedFlags(uintptr_t vt, uint32_t want, size_t* slotsOut) {
    uintptr_t lo = 0, hi = 0;
    *slotsOut = 0;
    if (!vt || vt < g_base || vt >= g_base + g_imageSize || !CodeRange(&lo, &hi)) return 0;
    size_t slots = 0;
    while (slots < 4096) {
        uintptr_t f = Ptr(vt + 8 * slots);
        if (f < lo || f >= hi) break;
        ++slots;
    }
    *slotsOut = slots;
    uint32_t f = want & (kFlRefund | kFlStaggerPartial);
    if ((want & kFlKnock) && g_noKnockOk && slots > kVtKnockFlying / 8) f |= kFlKnock;
    if ((want & kFlShield) && g_shieldGameOk && vt == g_base + kVtSoldier904 && Ptr(vt + 0x310) == g_base + kFnProjHit904 &&
        Ptr(vt + 0x1c8) == g_base + kFnRet0Of1c8_904 && Ptr(vt + 0x3a8) == g_base + kFnHitReact904 && Ptr(vt + 0x558) == g_base + kFnStagger904)
        f |= kFlShield;
    if ((want & kFlArmor) && g_armorOk && slots > 0x558 / 8 && Ptr(vt + 0x558) == g_base + kFnStagger904) f |= kFlArmor;
    return f;
}
uintptr_t* CopyFor(uintptr_t vt, uint32_t f, size_t slots) {
    const uint32_t key = f & kFlHooks;
    for (int i = 0; i < g_nVtc; ++i)
        if (g_vtc[i].of == vt && g_vtc[i].flags == key) return g_vtc[i].c;
    if (g_nVtc == 8) return nullptr;
    size_t n = slots + 64;
    auto* c = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (n + 1) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!c) return nullptr;
    c[0] = Ptr(vt - 8);
    for (size_t i = 0; i < n; ++i) c[1 + i] = Ptr(vt + 8 * i);
    if (key & kFlKnock) {
        c[1 + kVtKnockBack / 8] = reinterpret_cast<uintptr_t>(&KnockBackStub);
        c[1 + kVtKnockDown / 8] = reinterpret_cast<uintptr_t>(&KnockDownStub);
        c[1 + kVtKnockFlying / 8] = reinterpret_cast<uintptr_t>(&KnockFlyingStub);
    }
    if (key & kFlShield) {
        g_sb.orig310 = c[1 + 0x310 / 8];
        g_sb.orig3a8 = c[1 + 0x3a8 / 8];
        g_sb.orig558 = c[1 + 0x558 / 8];
        g_orig_hero_1c8 = reinterpret_cast<void*>(c[1 + 0x1c8 / 8]);
        c[1 + 0x310 / 8] = reinterpret_cast<uintptr_t>(&HeroProjHit);
        c[1 + 0x1c8 / 8] = reinterpret_cast<uintptr_t>(&hfps_hero_1c8_thunk);
        c[1 + 0x3a8 / 8] = reinterpret_cast<uintptr_t>(&HeroHitReact);
        c[1 + 0x558 / 8] = reinterpret_cast<uintptr_t>(&HeroStagger);
    } else if (key & kFlArmor) {
        g_sb.orig558 = c[1 + 0x558 / 8];
        c[1 + 0x558 / 8] = reinterpret_cast<uintptr_t>(&HeroStagger);
    }
    g_vtc[g_nVtc++] = {vt, key, c};
    return c;
}
void Hook(Hero& h, uint32_t want, const char* why) {
    uintptr_t e = 0;
    if (!UnitEntities(h.unit, &e, 1) || !e) return;
    const uintptr_t mine = h.copy ? reinterpret_cast<uintptr_t>(h.copy + 1) : 0;
    if (h.ent == e && h.want == want && mine && Ptr(e) == mine) return;
    bool rehook = h.ent == e && h.want == want && mine && Ptr(e) != mine;
    if (h.ent && mine && Ptr(h.ent) == mine) Wr(h.ent, h.origVt);
    uintptr_t vt = Ptr(e);
    if (h.ent != e) h.hpInit = false, h.bracket = false, h.exempt = 0, h.triedVt = 0;
    h.ent = e;
    if (!rehook && h.want == want && !h.copy && h.triedVt == vt) return;
    h.want = want;
    h.triedVt = vt;
    size_t slots = 0;
    uint32_t f = (want & kFlHooks) ? SupportedFlags(vt, want, &slots) : 0;
    uintptr_t* c = (f & kFlHooks) ? CopyFor(vt, f, slots) : nullptr;
    bool ok = c && Wr(e, reinterpret_cast<uintptr_t>(c + 1));
    h.origVt = vt;
    h.copy = ok ? c : nullptr;
    h.flags = ok ? f : 0;
    Log("mp: hero unit %p entity %p %s%s: vtable +0x%llx -> %s (wanted %x, hooked knock %d shield %d armor %d, refund %d, stagger on partial %d)",
        (void*)h.unit, (void*)e, why, rehook ? " (vtable was changed by the game - re-hooked)" : "",
        (unsigned long long)(vt > g_base ? vt - g_base : vt),
        ok ? "our copy"
        : !(want & kFlHooks) ? "not hooked (no-knockdown / shield / super armor all switched off by the sender)"
        : (f & kFlHooks) ? "WRITE FAILED / no copy"
                                 : "not hooked (class not supported)",
        want, (h.flags & kFlKnock) != 0, (h.flags & kFlShield) != 0, (h.flags & kFlArmor) != 0, (h.flags & kFlRefund) != 0,
        (h.flags & kFlStaggerPartial) != 0);
}
void Unhook(Hero& h) {
    if (h.ent && h.copy && Ptr(h.ent) == reinterpret_cast<uintptr_t>(h.copy + 1)) Wr(h.ent, h.origVt);
    h.copy = nullptr;
    h.flags = 0;
}
Hero* ByEnt(uintptr_t self) {
    if (!self) return nullptr;
    for (int i = 0; i < g_nHero; ++i)
        if (g_hero[i].ent == self && g_hero[i].copy && Ptr(self) == reinterpret_cast<uintptr_t>(g_hero[i].copy + 1)) return &g_hero[i];
    return nullptr;
}
bool HitState(uintptr_t self, HitView* v) {
    if (!g_nHero) return false;
    AcquireSRWLockShared(&g_heroLock);
    const Hero* h = ByEnt(self);
    if (h) {
        v->f = (h->flags & kFlShield) ? static_cast<float>(h->milli) * 0.001f : 0.f;
        v->yaw = h->yaw;
        v->angle = h->angle;
        v->armor = (h->flags & kFlArmor) != 0;
        v->staggerOnPartial = (h->flags & kFlStaggerPartial) != 0;
        v->local = h->from == g_nonce && S.active && S.mpMode && S.entity == self;
    }
    ReleaseSRWLockShared(&g_heroLock);
    return h != nullptr;
}
void Count(uintptr_t self, int what) {
    if (!g_nHero || what < 0 || what >= kCnt) return;
    AcquireSRWLockExclusive(&g_heroLock);
    if (Hero* h = ByEnt(self)) ++h->cnt[what];
    ReleaseSRWLockExclusive(&g_heroLock);
}
void AddExempt(uintptr_t self, uint32_t hp) {
    AcquireSRWLockExclusive(&g_heroLock);
    if (Hero* h = ByEnt(self)) h->exempt += hp;
    ReleaseSRWLockExclusive(&g_heroLock);
}
void BracketOpen(uintptr_t self, uint32_t hp) {
    AcquireSRWLockExclusive(&g_heroLock);
    if (Hero* h = ByEnt(self)) h->bracket = true, h->bracketHp = hp;
    ReleaseSRWLockExclusive(&g_heroLock);
}
void CloseLocked(Hero& h) {
    if (!h.bracket) return;
    h.bracket = false;
    uint32_t hp = 0;
    if (Rd(h.ent + oEntHp, hp) && hp < h.bracketHp) h.exempt += h.bracketHp - hp;
}
bool BracketClose(uintptr_t self) {
    if (!g_nHero) return false;
    AcquireSRWLockExclusive(&g_heroLock);
    Hero* h = ByEnt(self);
    if (h) CloseLocked(*h);
    ReleaseSRWLockExclusive(&g_heroLock);
    return h != nullptr;
}
void HookPostStep() {
    uintptr_t battle = ScriptBattle(), world = battle ? Ptr(battle + 0x440) : 0;
    uintptr_t y = world ? Ptr(world + 0xf0) : 0, obj = y ? y + 0x58 : 0, vt = obj ? Ptr(obj) : 0;
    if (!vt) return;
    const uintptr_t ours = g_ef.vtCopy ? reinterpret_cast<uintptr_t>(g_ef.vtCopy + 1) : 0;
    if (world == g_psWorld && obj == g_psObj && vt == ours) return;
    if (g_ef.siteState == 0) EarlyFlightSiteCheck();
    if (g_ef.siteState < 0) return;
    if (vt != ours) {
        uintptr_t lo = 0, hi = 0;
        size_t slots = 0;
        if (!CodeRange(&lo, &hi)) return;
        while (slots < 1024) {
            uintptr_t f = Ptr(vt + 8 * slots);
            if (f < lo || f >= hi) break;
            ++slots;
        }
        if (slots < 3 || ((!g_ef.vtCopy || g_ef.vtCopyOf != vt) && !EarlyFlightCopy(vt, slots))) return;
        if (!Wr(obj, reinterpret_cast<uintptr_t>(g_ef.vtCopy + 1))) return;
        if (!g_ef.origVt) g_ef.origVt = vt;
    }
    g_psWorld = world;
    g_psObj = obj;
    Log("mp: post_step dispatcher %p hooked for shield refunds (world %p, vtable +0x%llx)", (void*)obj, (void*)world,
        (unsigned long long)(vt != ours && vt > g_base ? vt - g_base : 0));
}
void PostStep(uintptr_t world) {
    if (!world || world != g_psWorld || !g_nHero) return;
    bool fx = false;
    AcquireSRWLockExclusive(&g_heroLock);
    for (int i = 0; i < g_nHero; ++i) {
        Hero& h = g_hero[i];
        if (!h.copy || !(h.flags & kFlShield) || Ptr(h.ent) != reinterpret_cast<uintptr_t>(h.copy + 1)) continue;
        CloseLocked(h);
        uint32_t hp = 0, mx = 0;
        uint8_t alive = 0;
        if (!Rd(h.ent + oEntHp, hp) || !Rd(h.ent + 0xb18, mx) || !Rd(h.ent + 0x108, alive)) continue;
        if (!h.hpInit) {
            h.hpInit = true;
            h.hpPrev = hp;
            h.exempt = 0;
            continue;
        }
        float f = static_cast<float>(h.milli) * 0.001f;
        uint32_t add = (h.flags & kFlRefund) && (alive & 0x10) ? shieldhit::Refund(h.hpPrev, hp, h.exempt, f, mx) : 0;
        if (add) {
            Wr(h.ent + oEntHp, hp + add);
            ++h.cnt[kCntRefund];
            h.refundHp += add;
            if (h.from == g_nonce && S.active && S.entity == h.ent) fx = true;
        }
        h.hpPrev = hp + add;
        h.exempt = 0;
    }
    MeleeMsg mm[16];
    const int nm = g_nMm;
    memcpy(mm, g_mm, sizeof(MeleeMsg) * nm);
    g_nMm = 0;
    ReleaseSRWLockExclusive(&g_heroLock);
    if (fx) {
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        if (q.QuadPart - g_psFxQpc > g_qpf.QuadPart / 4) {
            g_psFxQpc = q.QuadPart;
            vmgame::ShieldHit();
        }
    }
    for (int i = 0; i < nm; ++i) {
        uintptr_t e = 0;
        if (!UnitEntities(mm[i].unit, &e, 1) || !e) continue;
        MeleeReq r = {mm[i].kind, mm[i].cat, mm[i].o[0], mm[i].o[1], mm[i].o[2], mm[i].d[0], mm[i].d[1], e, Now()};
        r.cc = &mm[i].cc;
        r.rmax = mm[i].rmax;
        r.mpFlags = mm[i].flags;
        r.quiet = !(mm[i].from == g_nonce && S.active && S.mpMode && S.entity == e);
        r.mpFrom = mm[i].from;
        r.mpSeq = mm[i].seq;
        r.mpUnit = mm[i].unit;
        MeleeEngineRun(world, e, &r, 1);
    }
    uintptr_t heroes[16];
    int nh = 0;
    AcquireSRWLockShared(&g_heroLock);
    for (int i = 0; i < g_nHero; ++i)
        if (g_hero[i].early && g_hero[i].ent) heroes[nh++] = g_hero[i].ent;
    ReleaseSRWLockShared(&g_heroLock);
    for (int j = 0; j < nh; ++j) {
        uintptr_t ours[64];
        const unsigned k = PromoteHeroBullets(world, ours, heroes[j]);
        if (!k) continue;
        AcquireSRWLockExclusive(&g_heroLock);
        for (int i = 0; i < g_nHero; ++i)
            if (g_hero[i].ent == heroes[j]) g_hero[i].cnt[kCntEarly] += static_cast<int32_t>(k);
        ReleaseSRWLockExclusive(&g_heroLock);
    }
}
void MeleeResult(uint32_t from, uint32_t seq, uintptr_t unit, int kind, int hits, int kills, int sHits, int sKills, const char* dl, const char* sl) {
    uintptr_t battle = ScriptBattle();
    uint32_t step = 0;
    if (battle) Rd(battle + oBattleStep, step);
    int32_t id = 0;
    Rd(unit + oUnitNetId, id);
    Log("mp-digest: step %u MELEE-HIT from %08x #%u unit %d kind %d | hit %d killed %d [%s] | splash hit %d killed %d [%s]", step, from, seq, id, kind,
        hits, kills, dl, sHits, sKills, sl);
}
void Digest(const char* what, uint32_t from, uint32_t seq, uintptr_t unit) {
    uintptr_t battle = ScriptBattle();
    uint32_t step = 0;
    if (battle) Rd(battle + oBattleStep, step);
    int32_t id = 0, a[2] = {0, 0}, f[2] = {0, 0}, hp = 0;
    float p[3] = {0, 0, 0};
    if (unit) {
        Rd(unit + oUnitNetId, id);
        for (int w = 0; w < 2; ++w) {
            Rd(unit + oUnitAmmo + w * kAmmoSlot, a[w]);
            Rd(unit + oUnitAmmo + w * kAmmoSlot + oAmmoNoTake, f[w]);
        }
        uintptr_t e = 0;
        if (UnitEntities(unit, &e, 1)) {
            Rd(e + oEntHp, hp);
            EntPos(e, p);
        }
    }
    char sh[256] = "";
    AcquireSRWLockShared(&g_heroLock);
    int i = unit ? FindHero(unit) : -1;
    if (i >= 0) {
        const Hero& h = g_hero[i];
        snprintf(sh, sizeof sh, " | hooks %x shield %d yaw %.3f half %.0f | missile %d/%d melee %d/%d side %d stagger %d armor %d knock %d refund %d +%u"
                 " | early %d %d",
                 h.flags, h.milli, h.yaw, h.angle, h.cnt[kCntMissileFull], h.cnt[kCntMissilePart], h.cnt[kCntMeleeFull], h.cnt[kCntMeleePart],
                 h.cnt[kCntSide], h.cnt[kCntStagger], h.cnt[kCntArmor], h.cnt[kCntKnock], h.cnt[kCntRefund], h.refundHp, h.early ? 1 : 0,
                 h.cnt[kCntEarly]);
    }
    ReleaseSRWLockShared(&g_heroLock);
    Log("mp-digest: step %u %s from %08x #%u unit %d ammo %d/%d no-take %d/%d hp %d pos %.3f,%.3f,%.3f heroes %d%s", step, what, from, seq,
        id, a[0], a[1], f[0], f[1], hp, p[0], p[1], p[2], g_nHero, sh);
}
void Handler(uintptr_t rd, uintptr_t world) {
    auto rdBool = reinterpret_cast<FnRdBool>(g_base + kFnRdBool904);
    auto rdField = reinterpret_cast<FnRdField>(g_base + kFnRdField904);
    unsigned char b = 0;
    uint32_t magic = 0, kind = 0, from = 0, seq = 0;
    rdBool(rd, &b);
    rdField(rd, 4, &magic, 4);
    rdField(rd, 4, &kind, 4);
    rdField(rd, 4, &from, 4);
    unsigned char err = 1;
    Rd(rd + oRdError, err);
    if (err || magic != kMagic) return;
    rdField(rd, 4, &seq, 4);
    const uint32_t proto = kind >> 16;
    kind &= 0xffff;
    if (world != g_simWorld) {
        AcquireSRWLockExclusive(&g_heroLock);
        g_simWorld = world;
        g_nHero = 0;
        g_nMm = 0;
        ReleaseSRWLockExclusive(&g_heroLock);
    }
    if (from == g_nonce) {
        double t = -1;
        AcquireSRWLockExclusive(&g_sentLock);
        for (Sent& s : g_sent)
            if (s.seq == seq && s.t > 0) t = s.t, s.t = 0;
        ReleaseSRWLockExclusive(&g_sentLock);
        if (t > 0) {
            LONG ms = static_cast<LONG>((Now() - t) * 1000.0);
            InterlockedExchange(&g_lagMs, ms);
            if (ms > g_lagMaxMs) InterlockedExchange(&g_lagMaxMs, ms);
            InterlockedIncrement(&g_lagN);
        }
    }
    if (proto != kProto) {
        InterlockedExchange(&g_mismatch, 1);
        Log("mp: message from %08x has protocol %u (ours %u) - ignored; first person stays off in this battle", from, proto, kProto);
        return;
    }
    if (kind == kHello) {
        uint32_t dg = 0, exe = 0, feat = 0;
        rdField(rd, 4, &dg, 4);
        rdField(rd, 4, &exe, 4);
        rdField(rd, 4, &feat, 4);
        Rd(rd + oRdError, err);
        if (err) return;
        bool same = dg == g_digest && exe == g_exeStamp && feat == g_features;
        AcquireSRWLockExclusive(&g_peerLock);
        LONG n = g_peers, i = 0;
        while (i < n && g_peer[i].nonce != from) ++i;
        const bool fresh = i == n && n < 16;
        if (fresh) {
            g_peer[n] = {from, proto, dg, exe, feat};
            InterlockedExchange(&g_peers, n + 1);
        }
        ReleaseSRWLockExclusive(&g_peerLock);
        if (!same) InterlockedExchange(&g_mismatch, 1);
        if (from == g_nonce) InterlockedExchange(&g_helloBack, 1);
        else if (fresh) InterlockedExchange(&g_helloAgain, 1);
        Log("mp: HELLO from %08x%s: core %08x exe %08x features %x - %s (players heard %ld, round trip %ld ms)", from,
            from == g_nonce ? " (us)" : "", dg, exe, feat, same ? "same as ours" : "DIFFERENT from ours - first person off in this battle",
            g_peers, g_lagMs);
        return;
    }
    uintptr_t unit = 0;
    reinterpret_cast<FnRdUnit>(g_base + kFnRdUnit904)(rd, &unit);
    Rd(rd + oRdError, err);
    if (err || !unit) {
        Log("mp: %s from %08x #%u - unit not found (read error %d)", MsgName(kind), from, seq, err);
        return;
    }
    uint32_t want = 0;
    int32_t milli = 0;
    float yaw = 0.f, half = 180.f;
    uint32_t mf[kMeleeFields] = {};
    if (kind == kFpsOn || kind == kKeep) {
        rdField(rd, 4, &want, 4);
    } else if (kind == kShield) {
        rdField(rd, 4, &milli, 4);
        rdField(rd, 4, &yaw, 4);
        rdField(rd, 4, &half, 4);
    } else if (kind == kMelee) {
        for (uint32_t& x : mf) rdField(rd, 4, &x, 4);
    }
    Rd(rd + oRdError, err);
    if (err) {
        Log("mp: %s from %08x #%u - fields could not be read", MsgName(kind), from, seq);
        return;
    }
    bool wantSteps = false;
    AcquireSRWLockExclusive(&g_heroLock);
    int i = FindHero(unit);
    if (kind == kFpsOn || kind == kKeep) {
        if (i < 0 && g_nHero < 16) {
            i = g_nHero++;
            g_hero[i] = Hero{};
            g_hero[i].unit = unit;
            g_hero[i].from = from;
            g_hero[i].angle = 180.f;
            for (int w = 0; w < 2; ++w) {
                Rd(unit + oUnitAmmo + w * kAmmoSlot, g_hero[i].orig[w][0]);
                Rd(unit + oUnitAmmo + w * kAmmoSlot + oAmmoNoTake, g_hero[i].orig[w][1]);
            }
        }
        if (i >= 0) {
            ApplyAmmo(unit);
            g_hero[i].early = (want & kFlEarly) && g_ef.siteState > 0;
            Hook(g_hero[i], want, MsgName(kind));
            wantSteps = true;
        }
    } else if (kind == kMelee && i >= 0) {
        auto f = [&](int k) {
            float v;
            memcpy(&v, &mf[k], 4);
            return v;
        };
        const int mk = static_cast<int>(mf[0]), mc = static_cast<int>(mf[1]);
        if (mk < 0 || mk >= vmgame::kStrikeKinds || mc < 0 || mc >= vmgame::kMeleeCats || g_nMm >= 16) {
            Log("mp: MELEE from %08x #%u dropped (kind %d cat %d, queue %d)", from, seq, mk, mc, g_nMm);
        } else {
            MeleeMsg& m = g_mm[g_nMm++];
            memset(&m, 0, sizeof m);
            m.unit = unit, m.from = from, m.seq = seq, m.kind = mk, m.cat = mc, m.flags = static_cast<int>(mf[17]);
            m.o[0] = f(2), m.o[1] = f(3), m.o[2] = f(4), m.d[0] = f(5), m.d[1] = f(6), m.rmax = f(16);
            MeleeKindCfg& k = m.cc.k[mk];
            k.reach = f(7), k.halfDeg = f(8), k.dmg = f(9), k.maxHit = static_cast<int>(mf[10]), k.damage = (mf[17] & kMfDamage) != 0;
            k.splashR = f(11), k.splashDmg = f(12), k.splashMax = static_cast<int>(mf[13]);
            m.cc.damageScale = f(14), m.cc.fallback = f(15), m.cc.alwaysHit = (mf[17] & kMfAlwaysHit) != 0, m.cc.blockAngle = -1.f;
        }
    } else if (kind == kShield && i >= 0) {
        g_hero[i].milli = milli < 0 ? 0 : milli > 1000 ? 1000 : milli;
        g_hero[i].yaw = yaw;
        g_hero[i].angle = half >= 0.f && half <= 180.f ? half : 180.f;
    } else if (kind == kFpsOff && i >= 0) {
        for (int w = 0; w < 2; ++w) {
            uintptr_t slot = unit + oUnitAmmo + w * kAmmoSlot;
            Wr(slot + oAmmoNoTake, g_hero[i].orig[w][1]);
            int32_t n = 0;
            Rd(slot, n);
            if (g_hero[i].orig[w][0] == 0 && n == 1) Wr(slot, g_hero[i].orig[w][0]);
        }
        Unhook(g_hero[i]);
        g_hero[i] = g_hero[--g_nHero];
    }
    ReleaseSRWLockExclusive(&g_heroLock);
    if (wantSteps) HookPostStep();
    Digest(MsgName(kind), from, seq, unit);
}
bool Send(uint32_t kind, uintptr_t unit, const uint32_t* extra = nullptr, int nExtra = 0) {
    if (!g_ok) return false;
    const uint32_t seq = static_cast<uint32_t>(InterlockedIncrement(&g_seq));
    const unsigned char b0 = 0;
    const uint32_t k = kProto << 16 | kind;
    int32_t uid = 0;
    if (unit && (!Rd(unit + oUnitNetId, uid) || !uid)) return false;
    bool ok = IssueOverride(kTypeProfiler904, [&](void* w) {
        BsWrite(w, 0x01, &b0, 1);
        BsWrite(w, 0x04, &kMagic, 4);
        BsWrite(w, 0x04, &k, 4);
        BsWrite(w, 0x04, &g_nonce, 4);
        BsWrite(w, 0x04, &seq, 4);
        if (kind == kHello) {
            BsWrite(w, 0x04, &g_digest, 4);
            BsWrite(w, 0x04, &g_exeStamp, 4);
            BsWrite(w, 0x04, &g_features, 4);
        } else {
            BsWrite(w, 0x28, &uid, 4);
            for (int j = 0; j < nExtra; ++j) BsWrite(w, 0x04, &extra[j], 4);
        }
    });
    if (ok) {
        AcquireSRWLockExclusive(&g_sentLock);
        g_sent[g_sentHead++ % 64] = {seq, Now()};
        ReleaseSRWLockExclusive(&g_sentLock);
    }
    if (kind != kKeep && kind != kShield) Log("mp: sent %s #%u unit %d - %s", MsgName(kind), seq, uid, ok ? "queued" : "NOT queued (paused?) - retrying");
    return ok;
}
bool SendHero(uint32_t kind, uintptr_t unit) {
    const uint32_t fl = LocalFlags();
    return Send(kind, unit, &fl, 1);
}
bool MeleeReady() {
    return g_ok && g_helloBack && !g_nPend && cfg.meleeEngine && g_meleeHitOk && g_ef.siteState > 0 && S.active && S.mpMode && S.unit;
}
bool SendMelee(int kind, int cat, const float eye[3], const float fwd[3]) {
    if (cat < 0 || cat >= vmgame::kMeleeCats) cat = vmgame::kMeleeCatSword;
    float hx = fwd[0], hz = fwd[2], hl = std::sqrt(hx * hx + hz * hz);
    if (kind < 0 || kind >= vmgame::kStrikeKinds || hl < 1e-3f) return false;
    const MeleeCatCfg cc = MeleeCatEff(cat);
    const MeleeKindCfg& k = cc.k[kind];
    uint32_t x[kMeleeFields] = {};
    auto put = [&](int i, float v) { memcpy(&x[i], &v, 4); };
    x[0] = static_cast<uint32_t>(kind);
    x[1] = static_cast<uint32_t>(cat);
    put(2, eye[0]), put(3, eye[1]), put(4, eye[2]), put(5, hx / hl), put(6, hz / hl);
    put(7, k.reach), put(8, k.halfDeg), put(9, k.dmg);
    x[10] = static_cast<uint32_t>(k.maxHit);
    put(11, k.splashR), put(12, k.splashDmg);
    x[13] = static_cast<uint32_t>(k.splashMax);
    put(14, cc.damageScale), put(15, cc.fallback), put(16, g_meleeTargetRadiusMax);
    x[17] = (k.damage ? kMfDamage : 0u) | (cc.alwaysHit ? kMfAlwaysHit : 0u) | (cfg.meleeReact ? kMfReact : 0u) | (cfg.meleeEffects ? kMfEffects : 0u);
    return Send(kMelee, S.unit, x, kMeleeFields);
}
void Queue(uint32_t kind, uintptr_t unit) {
    if (!g_ok) return;
    if (g_nPend == 8) {
        Log("mp: pending queue full - dropped %s", MsgName(g_pend[0].kind));
        memmove(g_pend, g_pend + 1, sizeof g_pend[0] * 7);
        --g_nPend;
    }
    g_pend[g_nPend++] = {kind, unit};
}
void QueueOn(uintptr_t unit) {
    Queue(kFpsOn, unit);
    g_tKeep = Now();
    g_shSent = {0, 0.f, 180.f, 0.0};
}
void QueueOff(uintptr_t unit) { Queue(kFpsOff, unit); }
void Init() {
    g_ok = false;
    g_digest = SelfDigest();
    {
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
        g_exeStamp = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew)->FileHeader.TimeDateStamp;
    }
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    g_nonce = Fnv(reinterpret_cast<const unsigned char*>(&q), sizeof q) ^ (GetCurrentProcessId() * 2654435761u);
    if (!g_nonce) g_nonce = 1;
    g_features = (g_tpOk ? 1u : 0) | (g_ovrOk ? 2u : 0) | (g_shotOk ? 4u : 0) | (g_fireOk ? 8u : 0) | (g_mpFlagOk ? 16u : 0);
    if (g_ef.siteState == 0) EarlyFlightSiteCheck();
    g_features |= (g_noKnockOk ? 32u : 0) | (g_shieldGameOk ? 64u : 0) | (g_armorOk ? 128u : 0) | (g_ef.siteState > 0 ? 256u : 0);
    g_features |= g_meleeHitOk ? 512u : 0;
    if (!PrintsMatch(Prints(kPrints904), "mp channel") ||
        !CheckRegStub(kRegProfiler904, kFnProfilerNop904, kTypeProfiler904, "BCQ_COMMAND_PROFILER_TAG")) {
        Log("mp: channel OFF (fingerprints / registration stub) - first person stays off in multiplayer battles");
        return;
    }
    uint32_t type = 0;
    if (!Rd(g_base + kTypeProfiler904, type) || !type) {
        Log("mp: channel OFF - PROFILER_TAG type id not registered yet");
        return;
    }
    uintptr_t desc = reinterpret_cast<FnCmdDesc>(g_base + kFnCmdDesc)(&type);
    uintptr_t h = desc ? Ptr(desc + 0x18) : 0, mine = reinterpret_cast<uintptr_t>(&Handler);
    if (h != mine && (h != g_base + kFnProfilerNop904 || !Wr(desc + 0x18, mine))) {
        Log("mp: channel OFF - PROFILER_TAG descriptor %p handler +0x%llx is not the vanilla no-op (another mod?)", (void*)desc,
            (unsigned long long)(h > g_base ? h - g_base : h));
        return;
    }
    g_ok = true;
    Log("mp: channel ready - PROFILER_TAG type %08x descriptor %p handler swapped (vanilla no-op +0x%llx); us %08x, core %08x, exe %08x, "
        "features %x, protocol %u",
        type, (void*)desc, (unsigned long long)kFnProfilerNop904, g_nonce, g_digest, g_exeStamp, g_features, kProto);
}
void Frame() {
    uintptr_t battle = ScriptBattle();
    if (battle != g_battle) {
        g_battle = battle;
        g_tHello = -1;
        g_nPend = 0;
        InterlockedExchange(&g_helloBack, 0);
        InterlockedExchange(&g_helloAgain, 0);
        InterlockedExchange(&g_mismatch, 0);
        AcquireSRWLockExclusive(&g_peerLock);
        InterlockedExchange(&g_peers, 0);
        ReleaseSRWLockExclusive(&g_peerLock);
        InterlockedExchange(&g_lagN, 0);
        InterlockedExchange(&g_lagMaxMs, 0);
        AcquireSRWLockExclusive(&g_heroLock);
        g_nHero = 0;
        g_nMm = 0;
        g_simWorld = 0;
        ReleaseSRWLockExclusive(&g_heroLock);
        g_psWorld = g_psObj = 0;
        if (battle && g_ok) Log("mp: new battle %p - multiplayer path %s", (void*)battle, Mode(battle) ? (cfg.mpForce ? "ON (force_mp_mode)" : "ON") : "off");
    }
    if (!battle || !g_ok || !Mode(battle) || !CmdQueue(nullptr)) return;
    unsigned char replay = 0;
    if (Rd(battle + oBattleIsReplay, replay) && replay) return;
    const double now = Now();
    if (!g_helloBack && (g_tHello < 0 || now - g_tHello > 5.0)) {
        InterlockedExchange(&g_helloAgain, 0);
        if (Send(kHello, 0)) g_tHello = now;
    } else if (g_helloAgain) {
        InterlockedExchange(&g_helloAgain, 0);
        if (Send(kHello, 0))
            Log("mp: heard a new player's HELLO (players heard %ld) - sent ours again so they hear us too", g_peers);
        else
            InterlockedExchange(&g_helloAgain, 1);
    }
    while (g_nPend && (g_pend[0].kind == kFpsOn ? SendHero(kFpsOn, g_pend[0].unit) : Send(g_pend[0].kind, g_pend[0].unit))) {
        memmove(g_pend, g_pend + 1, sizeof g_pend[0] * (g_nPend - 1));
        --g_nPend;
    }
    if (!(S.active && S.mpMode && S.unit && !g_nPend)) return;
    if (now - g_tKeep >= 1.0) {
        if (SendHero(kKeep, S.unit)) g_tKeep = now;
    }
    const int32_t milli = cfg.shieldGame ? static_cast<int32_t>(vmgame::ShieldBlockFraction() * 20.f + 0.5f) * 50 : 0;
    const float half = ShieldAngleNow(), yaw = S.yaw;
    float dy = yaw - g_shSent.yaw;
    while (dy > 3.14159265f) dy -= 6.2831853f;
    while (dy < -3.14159265f) dy += 6.2831853f;
    bool dirty = milli != g_shSent.milli || half != g_shSent.angle || (milli > 0 && half < 180.f && std::fabs(dy) > 0.14f);
    if (dirty && now - g_shSent.t >= 0.1) {
        uint32_t x[3] = {static_cast<uint32_t>(milli), 0, 0};
        memcpy(&x[1], &yaw, 4);
        memcpy(&x[2], &half, 4);
        if (Send(kShield, S.unit, x, 3)) g_shSent = {milli, yaw, half, now};
    }
}
bool EnterAllowed() {
    const uintptr_t battle = ScriptBattle();
    if (!Mode(battle)) return true;
    const bool real = RealMp(battle);
    const LONG listed = g_listBattle == battle ? g_wantPlayers : 0;
    const bool mePassive = real && g_listBattle == battle && g_mePassive;
    const LONG need = real ? listed + (mePassive ? 1 : 0) : 1;
    char buf[200];
    const char* why = !g_ok ? "channel not available on this game version" : g_mismatch ? "a player's core / game version / checks differ from ours"
                      : !g_helloBack ? "handshake not finished yet (our HELLO has not come back)"
                      : real && listed < 0 ? "the multiplayer player list could not be read (battle script)"
                      : real && listed == 0 ? "the multiplayer player list is not reported yet (battle script tenzin_fps_access.lua)"
                                            : nullptr;
    if (!why && g_peers < need) {
        snprintf(buf, sizeof buf, "heard HELLO from %ld of %ld playing human(s) - waiting (a player without this build or with another version never says HELLO)",
                 g_peers, need);
        why = buf;
    }
    if (!why) return true;
    Log("enter refused: multiplayer path - %s (players heard %ld, list %ld playing / %ld AI / %ld watching, list reports %ld)", why, g_peers, listed,
        g_listAi, g_listWatch, g_listCalls);
    return false;
}
}
