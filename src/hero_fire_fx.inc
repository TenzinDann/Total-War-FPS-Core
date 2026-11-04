namespace herofx {
using FnBroadcast = void (*)(void* obj, void* ev);
struct State {
    uintptr_t obj = 0, origVt = 0, hero = 0;
    static constexpr int kMaxProj = 64;
    volatile uintptr_t projs[kMaxProj] = {};
    volatile LONG nProjs = 0;
    uintptr_t* vtCopy = nullptr;
    uintptr_t vtCopyOf = 0;
    FnBroadcast orig = nullptr;
    int keepFx = -1;
    volatile LONG seen = 0, muted = 0, mismatch = 0;
} g;
uintptr_t kVtDispatcher904 = 0x3ab1c08, kFnBroadcast904 = 0x2dd0f60;
constexpr uintptr_t oEvRecord = 0x00, oEvShooter = 0x08, oEvByEntity = 0x34;
bool Ours(uintptr_t rec) {
    LONG n = g.nProjs;
    for (LONG i = 0; i < n && i < State::kMaxProj; ++i)
        if (g.projs[i] == rec) return true;
    return false;
}
void Track(uintptr_t rec) {
    if (!rec || Ours(rec) || g.nProjs >= State::kMaxProj) return;
    g.projs[g.nProjs] = rec;
    MemoryBarrier();
    InterlockedIncrement(&g.nProjs);
}
void Broadcast(void* obj, void* ev) {
    auto e = reinterpret_cast<uintptr_t>(ev);
    InterlockedIncrement(&g.seen);
    uintptr_t rec = 0, shooter = 0;
    unsigned char by = 0;
    if (g.hero && Rd(e + oEvRecord, rec) && Ours(rec) && Rd(e + oEvShooter, shooter) && shooter == g.hero &&
        Rd(e + oEvByEntity, by)) {
        if (by == 1) {
            *reinterpret_cast<uintptr_t*>(e + oEvShooter) = 0;
            *reinterpret_cast<unsigned char*>(e + oEvByEntity) = 0;
            InterlockedIncrement(&g.muted);
            g.orig(obj, ev);
            *reinterpret_cast<uintptr_t*>(e + oEvShooter) = shooter;
            *reinterpret_cast<unsigned char*>(e + oEvByEntity) = by;
            return;
        }
        InterlockedIncrement(&g.mismatch);
    }
    g.orig(obj, ev);
}
void LogListeners(uintptr_t lis) {
    uint32_t n = 0;
    uintptr_t arr = 0;
    Rd(lis + 0xc, n);
    Rd(lis + 0x10, arr);
    auto rva = [](uintptr_t a) { return (unsigned long long)(a > g_base ? a - g_base : 0); };
    Log("hero-fx: launch-event dispatcher %p has %u listeners", (void*)lis, n);
    for (uint32_t i = 0; i < n && i < 24; ++i) {
        uintptr_t l = Ptr(arr + 8 * i), vt = l ? Ptr(l) : 0;
        Log("hero-fx:   listener %u %p vt +%llx  [+0x8] = +%llx  [+0x10] = +%llx", i, (void*)l, rva(vt), rva(vt ? Ptr(vt + 8) : 0),
            rva(vt ? Ptr(vt + 0x10) : 0));
    }
}
bool Install(uintptr_t battle, uintptr_t heroEntity, uintptr_t projRecord) {
    if (g.keepFx < 0) {
        wchar_t ini[MAX_PATH];
        swprintf(ini, MAX_PATH, L"%ls\\tenzin_fps.ini", g_dir);
        g.keepFx = GetPrivateProfileIntW(L"fire", L"hero_weapon_fx", 0, ini) != 0;
    }
    if (g.keepFx) {
        Log("hero-fx: [fire] hero_weapon_fx=1 - the hero's own weapon fire sound / muzzle effect left on");
        return false;
    }
    if (!battle || !heroEntity) return false;
    uintptr_t w440 = Ptr(battle + 0x440), yf0 = w440 ? Ptr(w440 + 0xf0) : 0, lis = yf0 ? yf0 + 0x3d8 : 0, vt = lis ? Ptr(lis) : 0;
    uintptr_t lo = 0, hi = 0;
    if (!vt || !CodeRange(&lo, &hi)) {
        Log("hero-fx: launch-event dispatcher not found (world %p [+0xf0] %p vt %p)", (void*)w440, (void*)yf0, (void*)vt);
        return false;
    }
    bool ours = g.vtCopy && vt == reinterpret_cast<uintptr_t>(g.vtCopy + 1);
    uintptr_t realVt = ours ? g.vtCopyOf : vt;
    if (realVt != g_base + kVtDispatcher904 || Ptr(realVt + 0x10) != g_base + kFnBroadcast904) {
        Log("hero-fx: dispatcher vtable +%llx / [+0x10] +%llx not the 9.0.4 ones (+%llx / +%llx) - not hooking",
            (unsigned long long)(realVt - g_base), (unsigned long long)(Ptr(realVt + 0x10) - g_base), (unsigned long long)kVtDispatcher904,
            (unsigned long long)kFnBroadcast904);
        return false;
    }
    g.hero = heroEntity;
    Track(projRecord);
    if (ours) {
        g.obj = lis;
        return true;
    }
    size_t slots = 0;
    while (slots < 1024) {
        uintptr_t f = Ptr(vt + 8 * slots);
        if (f < lo || f >= hi) break;
        ++slots;
    }
    if (slots < 3) {
        Log("hero-fx: dispatcher %p vtable %p has only %zu slots - not hooking", (void*)lis, (void*)vt, slots);
        return false;
    }
    if (!g.vtCopy || g.vtCopyOf != vt) {
        size_t copyN = slots + 16 > 64 ? slots + 16 : 64;
        auto* c = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (copyN + 1) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!c) return false;
        c[0] = Ptr(vt - 8);
        for (size_t i = 0; i < copyN; ++i) c[1 + i] = i < slots ? Ptr(vt + 8 * i) : 0;
        g.orig = reinterpret_cast<FnBroadcast>(c[1 + 2]);
        c[1 + 2] = reinterpret_cast<uintptr_t>(&Broadcast);
        DWORD old;
        VirtualProtect(c, (copyN + 1) * sizeof(uintptr_t), PAGE_READONLY, &old);
        g.vtCopy = c;
        g.vtCopyOf = vt;
    }
    g.seen = g.muted = g.mismatch = 0;
    if (!Wr(lis, reinterpret_cast<uintptr_t>(g.vtCopy + 1))) {
        Log("hero-fx: vtable write failed on %p", (void*)lis);
        return false;
    }
    g.obj = lis;
    g.origVt = vt;
    Log("hero-fx: hooked launch-event dispatcher %p (vt +%llx, %zu slots) via vtable copy %p: our bullets (first record %p, shooter = hero %p) "
        "are announced without the shooter, so the hero's own weapon fire sound / muzzle effect stay quiet",
        (void*)lis, (unsigned long long)(vt - g_base), slots, (void*)(g.vtCopy + 1), (void*)projRecord, (void*)heroEntity);
    LogListeners(lis);
    return true;
}
void Uninstall(const char* why) {
    if (!g.obj) return;
    if (g.vtCopy && Ptr(g.obj) == reinterpret_cast<uintptr_t>(g.vtCopy + 1)) Wr(g.obj, g.origVt);
    Log("hero-fx: unhooked (%s): launch events %ld, our bullets announced without the shooter %ld, ours without the by-entity flag %ld, "
        "projectile kinds tracked %ld", why, g.seen, g.muted, g.mismatch, g.nProjs);
    g.obj = 0;
    g.hero = 0;
    g.nProjs = 0;
}
}
