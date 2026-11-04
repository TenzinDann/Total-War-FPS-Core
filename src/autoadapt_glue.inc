#include "autoadapt.inc"
bool g_rxAuto = false;
int g_rxMode = -1;
rx::Img* g_rxImg = nullptr;
rx::Resolver* g_rxRes = nullptr;
int RxMode() {
    if (g_rxMode >= 0) return g_rxMode;
    wchar_t ini[MAX_PATH];
    wcscpy(ini, g_dir);
    wcscat(ini, L"\\tenzin_fps.ini");
    g_rxMode = static_cast<int>(GetPrivateProfileIntW(L"core", L"auto_adapt", 1, ini));
    if (g_rxMode < 0 || g_rxMode > 2) g_rxMode = 1;
    return g_rxMode;
}
int RxIndex(uintptr_t old) {
    size_t lo = 0, hi = rx::kN;
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        if (rx::kSyms[m].old < old) lo = m + 1;
        else hi = m;
    }
    return lo < rx::kN && rx::kSyms[lo].old == old ? static_cast<int>(lo) : -1;
}
uintptr_t RxAddr(uintptr_t old) {
    if (!g_rxAuto || !g_rxRes) return old;
    int i = RxIndex(old);
    if (i < 0) return old;
    return g_rxRes->state[i] == 1 ? g_rxRes->res[i] : 0;
}
void RxTranslatePrintArr(CodePrint* p, size_t n) {
    for (size_t i = 0; i < n; ++i) p[i].rva = RxAddr(p[i].rva);
}
#include "autoadapt_tables.inc"
bool RxPrintsMatch(const PrintSet& s, const char* what) {
    for (size_t i = 0; i < s.n; ++i)
        if (!s.p[i].rva) {
            Log("%s: code fingerprint %zu/%zu not found on this game version (auto-adapt) - this feature stays off", what, i + 1, s.n);
            return false;
        }
    return s.n > 0;
}
bool RxResolve(uintptr_t base, const char* why) {
    LARGE_INTEGER f, t0, t1, t2;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    auto* im = new rx::Img();
    if (!im->Load(base)) {
        Log("auto-adapt (%s): could not read the game image - off", why);
        delete im;
        return false;
    }
    QueryPerformanceCounter(&t1);
    auto* r = new rx::Resolver(*im);
    r->Run();
    QueryPerformanceCounter(&t2);
    delete im;
    std::unordered_map<uint32_t, std::vector<uint32_t>>().swap(r->strRefs);
    std::unordered_map<uint32_t, uint32_t>().swap(r->memo);
    std::unordered_map<uint32_t, std::vector<uint32_t>>().swap(r->candMemo);
    g_rxImg = nullptr, g_rxRes = r;
    int ok = 0, loose = 0, srcOk = 0, srcN = 0;
    for (size_t i = 0; i < rx::kN; ++i) {
        ok += r->state[i] == 1, loose += r->loose[i];
        if (!(rx::kSyms[i].flags & 4)) ++srcN, srcOk += r->state[i] == 1;
    }
    Log("auto-adapt (%s): recipes for exe %08X/%X (hash %016llX), %d of %zu addresses found (%d of %d from our code, %d located through an unchanged caller only), "
        "read %.0f ms + resolve %.0f ms",
        why, rx::kRecipeStamp, rx::kRecipeSize, static_cast<unsigned long long>(rx::kRecipeHash), ok, rx::kN, srcOk, srcN, loose,
        (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart, (t2.QuadPart - t1.QuadPart) * 1000.0 / f.QuadPart);
    for (size_t i = 0; i < rx::kN; ++i)
        if (r->state[i] != 1 && !(rx::kSyms[i].flags & 4))
            Log("auto-adapt: +%X not found (%s)", rx::kSyms[i].old, r->why[i] ? r->why[i] : "?");
    return true;
}
GameAddrs g_rxGame;
unsigned char g_rxPresentBytes[64], g_rxGlobalWriteBytes[16];
const GameAddrs* RxTryAuto(DWORD stamp, DWORD size) {
    const int mode = RxMode();
    if (mode == 0) return nullptr;
    if (rx::kRecipeStamp != kGames[0].stamp || rx::kRecipeSize != kGames[0].size) {
        Log("auto-adapt: recipes are for exe %08X/%X but this build's table is %s %08lX/%lX - off", rx::kRecipeStamp, rx::kRecipeSize, kGames[0].name,
            kGames[0].stamp, kGames[0].size);
        return nullptr;
    }
    if (!RxResolve(g_base, mode == 1 ? "shadow, unknown game version" : "unknown game version")) return nullptr;
    int missing = 0;
    for (uintptr_t old : kRxRequired) {
        int i = RxIndex(old);
        if (i < 0 || g_rxRes->state[i] != 1) {
            if (missing++ < 12) Log("auto-adapt: required +%llX (called / read by our code) not found", static_cast<unsigned long long>(old));
        }
    }
    if (mode == 1) {
        Log("auto-adapt (shadow): with auto_adapt=2 first person would %s (%d required addresses missing)",
            missing ? "stay off" : "start; features whose fingerprints were not found switch off", missing);
        return nullptr;
    }
    if (missing) {
        Log("auto-adapt: %d required addresses not found - first person stays off until the mod is updated", missing);
        return nullptr;
    }
    g_rxAuto = true;
    g_rxGame = kGames[0];
    g_rxGame.name = "auto-adapted";
    g_rxGame.stamp = stamp, g_rxGame.size = size;
    RxTranslateGame(g_rxGame);
    if (g_rxGame.render.presentHelperLen <= sizeof g_rxPresentBytes && Rd(g_base + g_rxGame.render.fnPresentHelper, g_rxPresentBytes))
        g_rxGame.render.presentHelperBytes = g_rxPresentBytes;
    if (g_rxGame.render.globalWriteLen <= sizeof g_rxGlobalWriteBytes && Rd(g_base + g_rxGame.render.insGlobalWrite, g_rxGlobalWriteBytes))
        g_rxGame.render.globalWriteBytes = g_rxGlobalWriteBytes;
    RxTranslateVars();
    RxTranslatePrints();
    Log("auto-adapt: game version not in this build's table - using the recognised addresses (feature checks follow)");
    return &g_rxGame;
}
void RxShadowRun() {
    if (!RxResolve(g_base, "shadow, known game version")) return;
    int same = 0, diff = 0;
    for (size_t i = 0; i < rx::kN; ++i) {
        if (rx::kSyms[i].flags & 4) continue;
        if (g_rxRes->state[i] == 1 && g_rxRes->res[i] == rx::kSyms[i].old) ++same;
        else if (diff++ < 12) Log("auto-adapt (shadow): +%X -> %X (state %d)", rx::kSyms[i].old, g_rxRes->res[i], g_rxRes->state[i]);
    }
    Log("auto-adapt (shadow): %d addresses agree with the table, %d differ", same, diff);
}
DWORD WINAPI RxShadowThread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    RxShadowRun();
    return 0;
}
void RxShadowStart() {
    if (g_rxAuto || RxMode() != 1) return;
    if (rx::kRecipeStamp != kGames[0].stamp) return;
    HANDLE t = CreateThread(nullptr, 0, RxShadowThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}
