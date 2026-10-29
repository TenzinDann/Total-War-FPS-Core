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
constexpr uint32_t kProto = 1;
enum : uint32_t { kHello = 1, kFpsOn = 2, kFpsOff = 3, kKeep = 4 };
const char* MsgName(uint32_t k) { return k == kHello ? "HELLO" : k == kFpsOn ? "FPS_ON" : k == kFpsOff ? "FPS_OFF" : k == kKeep ? "KEEP" : "?"; }
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
SRWLOCK g_peerLock = SRWLOCK_INIT;
struct Peer {
    uint32_t nonce, proto, digest, exe, features;
} g_peer[16];
uintptr_t g_simWorld = 0;
struct Hero {
    uintptr_t unit;
    uint32_t from;
    int32_t orig[2][2];
} g_hero[16];
int g_nHero = 0;
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
    Log("mp-digest: step %u %s from %08x #%u unit %d ammo %d/%d no-take %d/%d hp %d pos %.3f,%.3f,%.3f heroes %d", step, what, from, seq,
        id, a[0], a[1], f[0], f[1], hp, p[0], p[1], p[2], g_nHero);
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
        g_simWorld = world;
        g_nHero = 0;
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
        if (i == n && n < 16) {
            g_peer[n] = {from, proto, dg, exe, feat};
            InterlockedExchange(&g_peers, n + 1);
        }
        ReleaseSRWLockExclusive(&g_peerLock);
        if (!same) InterlockedExchange(&g_mismatch, 1);
        if (from == g_nonce) InterlockedExchange(&g_helloBack, 1);
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
    int i = FindHero(unit);
    if (kind == kFpsOn || kind == kKeep) {
        if (i < 0 && g_nHero < 16) {
            i = g_nHero++;
            g_hero[i].unit = unit;
            g_hero[i].from = from;
            for (int w = 0; w < 2; ++w) {
                Rd(unit + oUnitAmmo + w * kAmmoSlot, g_hero[i].orig[w][0]);
                Rd(unit + oUnitAmmo + w * kAmmoSlot + oAmmoNoTake, g_hero[i].orig[w][1]);
            }
        }
        if (i >= 0) ApplyAmmo(unit);
    } else if (kind == kFpsOff && i >= 0) {
        for (int w = 0; w < 2; ++w) {
            uintptr_t slot = unit + oUnitAmmo + w * kAmmoSlot;
            Wr(slot + oAmmoNoTake, g_hero[i].orig[w][1]);
            int32_t n = 0;
            Rd(slot, n);
            if (g_hero[i].orig[w][0] == 0 && n == 1) Wr(slot, g_hero[i].orig[w][0]);
        }
        g_hero[i] = g_hero[--g_nHero];
    }
    Digest(MsgName(kind), from, seq, unit);
}
bool Send(uint32_t kind, uintptr_t unit) {
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
        }
    });
    if (ok) {
        AcquireSRWLockExclusive(&g_sentLock);
        g_sent[g_sentHead++ % 64] = {seq, Now()};
        ReleaseSRWLockExclusive(&g_sentLock);
    }
    if (kind != kKeep) Log("mp: sent %s #%u unit %d - %s", MsgName(kind), seq, uid, ok ? "queued" : "NOT queued (paused?) - retrying");
    return ok;
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
        "features %x, force_mp_mode=%d",
        type, (void*)desc, (unsigned long long)kFnProfilerNop904, g_nonce, g_digest, g_exeStamp, g_features, cfg.mpForce);
}
void Frame() {
    uintptr_t battle = ScriptBattle();
    if (battle != g_battle) {
        g_battle = battle;
        g_tHello = -1;
        g_nPend = 0;
        InterlockedExchange(&g_helloBack, 0);
        InterlockedExchange(&g_mismatch, 0);
        AcquireSRWLockExclusive(&g_peerLock);
        InterlockedExchange(&g_peers, 0);
        ReleaseSRWLockExclusive(&g_peerLock);
        InterlockedExchange(&g_lagN, 0);
        InterlockedExchange(&g_lagMaxMs, 0);
        if (battle && g_ok) Log("mp: new battle %p - multiplayer path %s", (void*)battle, Mode(battle) ? (cfg.mpForce ? "ON (force_mp_mode)" : "ON") : "off");
    }
    if (!battle || !g_ok || !Mode(battle) || !CmdQueue(nullptr)) return;
    unsigned char replay = 0;
    if (Rd(battle + oBattleIsReplay, replay) && replay) return;
    const double now = Now();
    if (!g_helloBack && (g_tHello < 0 || now - g_tHello > 5.0)) {
        if (Send(kHello, 0)) g_tHello = now;
    }
    while (g_nPend && Send(g_pend[0].kind, g_pend[0].unit)) {
        memmove(g_pend, g_pend + 1, sizeof g_pend[0] * (g_nPend - 1));
        --g_nPend;
    }
    if (S.active && S.mpMode && S.unit && !g_nPend && now - g_tKeep >= 1.0) {
        if (Send(kKeep, S.unit)) g_tKeep = now;
    }
}
bool EnterAllowed() {
    if (!Mode(ScriptBattle())) return true;
    const char* why = !g_ok ? "channel not available on this game version" : g_mismatch ? "a player's core / game version / checks differ from ours"
                      : !g_helloBack ? "handshake not finished yet (our HELLO has not come back)" : nullptr;
    if (!why) return true;
    Log("enter refused: multiplayer path - %s (players heard %ld)", why, g_peers);
    return false;
}
}
