struct LauncherState {
    bool configured = false;
    wchar_t model[2][MAX_PATH * 2] = {};
    wchar_t side[2][MAX_PATH * 2] = {};
    UINT fireKey = 'G', switchKey = 'X', flameKey = 'F';
    char ability[kLauncherKinds][64] = {};
    float windup[kLauncherKinds] = {1.f, 1.f, 1.f};
    float deployTime = 0.12f, postHold = 0.25f, fireFade = 0.03f, deactFade = 0.05f;
    volatile LONG selected = kLauncherFrag;
    volatile LONG req = 0;
    volatile LONG64 reqUs = 0;
    volatile LONG driven = 0;
    volatile LONG windupMs = 1000;
    volatile LONG64 fireUs = 0, abortUs = 0;
    volatile LONG busy = 0;
    volatile LONG busyUntilMs = 0;
    vm::Renderer rend[2];
    vm::Settings set[2];
    bool loadFailed[2] = {false, false};
    int clip[2][5];
    int active = -1;
    int kind = 0;
    double t0 = 0;
    int logs = 0;
    struct Snd {
        int clip;
        float t, vol;
        vm::GunSound snd;
    } snd[2][24];
    int nSnd[2] = {0, 0};
    int sndClip = -2;
    float sndLast = -1.f;
    volatile LONG switchReq = 0;
} g_lc;
enum { kLcDormant, kLcActive, kLcFire, kLcDeact, kLcActivate };
constexpr int kLcSndSwitch = -10;
void LauncherResolve(const wchar_t* dllDir, const wchar_t* rel, wchar_t* out) {
    swprintf(out, MAX_PATH * 2, L"%ls\\%ls", dllDir, rel);
    if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return;
    wchar_t alt[MAX_PATH * 2];
    swprintf(alt, MAX_PATH * 2, L"%ls\\assets\\%ls", dllDir, rel);
    if (GetFileAttributesW(alt) != INVALID_FILE_ATTRIBUTES) wcscpy(out, alt);
}
float LcIniFloat(const wchar_t* k, float def) {
    wchar_t b[64];
    GetPrivateProfileStringW(L"launcher", k, L"", b, 64, g_ini);
    return b[0] ? static_cast<float>(_wtof(b)) : def;
}
void LauncherReadTimes() {
    LauncherState& L = g_lc;
    const wchar_t* wk[kLauncherKinds] = {L"windup_frag", L"windup_ice", L"windup_flame"};
    for (int k = 0; k < kLauncherKinds; ++k) {
        L.windup[k] = LcIniFloat(wk[k], 1.f);
        if (!(L.windup[k] >= 0.f && L.windup[k] < 10.f)) L.windup[k] = 1.f;
    }
    L.deployTime = LcIniFloat(L"deploy_time", 0.12f);
    L.postHold = LcIniFloat(L"post_hold", 0.25f);
    L.fireFade = LcIniFloat(L"fire_fade", 0.03f);
    L.deactFade = LcIniFloat(L"deactivate_fade", 0.05f);
}
void LauncherSetup(const wchar_t* dllDir) {
    LauncherState& L = g_lc;
    const wchar_t* sec = L"launcher";
    L.configured = false;
    if (!GetPrivateProfileIntW(sec, L"enabled", 0, g_ini)) return;
    const wchar_t* mk[2] = {L"model_frag", L"model_ice"};
    for (int i = 0; i < 2; ++i) {
        wchar_t rel[MAX_PATH];
        GetPrivateProfileStringW(sec, mk[i], L"", rel, MAX_PATH, g_ini);
        if (!rel[0]) {
            g_log("launcher: [launcher] %ls not set - off", mk[i]);
            return;
        }
        LauncherResolve(dllDir, rel, L.model[i]);
        wcscpy(L.side[i], L.model[i]);
        if (wchar_t* sl = wcsrchr(L.side[i], L'\\')) wcscpy(sl + 1, L"viewmodel.ini");
    }
    auto key = [&](const wchar_t* k, UINT def) {
        UINT v = GetPrivateProfileIntW(sec, k, def, g_ini);
        return (v > 0 && v < 0xFF) ? v : def;
    };
    L.fireKey = key(L"fire_key", 'G');
    L.switchKey = key(L"switch_key", 'X');
    L.flameKey = key(L"flame_key", 'F');
    const wchar_t* ak[kLauncherKinds] = {L"ability_frag", L"ability_ice", L"ability_flame"};
    for (int k = 0; k < kLauncherKinds; ++k) {
        wchar_t b[64];
        GetPrivateProfileStringW(sec, ak[k], L"", b, 64, g_ini);
        WideCharToMultiByte(CP_UTF8, 0, b, -1, L.ability[k], sizeof L.ability[k], nullptr, nullptr);
    }
    LauncherReadTimes();
    L.configured = true;
    g_log("launcher: on, keys fire=0x%02X switch=0x%02X flame=0x%02X, abilities %s / %s / %s, windup %.2f / %.2f / %.2f s, deploy %.2f s hold %.2f s, "
          "models %ls | %ls",
          L.fireKey, L.switchKey, L.flameKey, L.ability[0], L.ability[1], L.ability[2], L.windup[0], L.windup[1], L.windup[2], L.deployTime,
          L.postHold, L.model[0], L.model[1]);
}
void LauncherSettings(int v) {
    vm::Settings s;
    vm::LoadSettings(g_ini, s);
    s.offset[0] = s.offset[1] = s.offset[2] = 0.f;
    for (int k = 0; k < 3; ++k) s.rotDeg[k] = s.tiltDeg[k] = 0.f;
    s.scale = 1.f;
    s.mirrorX = false;
    s.muzzleBone = -2;
    s.adsSightBone = -1;
    s.handIk = false;
    vm::LoadSettings(g_lc.side[v], s);
    g_lc.set[v] = s;
}
bool LauncherEnsure(int v, ID3D11Device* dev) {
    LauncherState& L = g_lc;
    if (L.rend[v].Ready() && L.rend[v].Device() == dev) return true;
    if (L.loadFailed[v]) return false;
    L.rend[v].Release();
    LauncherSettings(v);
    char err[256] = "";
    double t = Now();
    if (!L.rend[v].Init(dev, L.model[v], err, sizeof err)) {
        L.loadFailed[v] = true;
        g_log("launcher: load %ls failed: %s", L.model[v], err);
        return false;
    }
    const char* names[5] = {"idle_dormant", "idle_active", "fire", "deactivate", "activate"};
    for (int c = 0; c < 5; ++c) L.clip[v][c] = L.rend[v].FindClip(names[c]);
    g_log("launcher: loaded %ls in %.1f ms (%u tris, %u bones, clips dormant %d active %d fire %d deactivate %d) fov %.1f offset %.3f,%.3f,%.3f",
          L.model[v], (Now() - t) * 1000.0, L.rend[v].Triangles(), L.rend[v].Bones(), L.clip[v][0], L.clip[v][1], L.clip[v][2], L.clip[v][3],
          L.set[v].fovDeg, L.set[v].offset[0], L.set[v].offset[1], L.set[v].offset[2]);
    for (int i = 0; i < L.nSnd[v]; ++i) L.snd[v][i].snd.Release();
    L.nSnd[v] = 0;
    if (L.set[v].animSounds[0]) {
        wchar_t dir[MAX_PATH * 2], path[MAX_PATH * 2];
        wcscpy(dir, L.model[v]);
        if (wchar_t* sl = wcsrchr(dir, L'\\')) *sl = 0;
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, L.set[v].animSounds);
        FILE* f = _wfopen(path, L"r");
        int bad = 0;
        if (f) {
            char line[512];
            while (fgets(line, sizeof line, f) && L.nSnd[v] < 24) {
                char clip[64], file[256];
                float tt = 0, vol = 1;
                if (line[0] == '#' || sscanf(line, "%63s %f %255s %f", clip, &tt, file, &vol) < 3) continue;
                LauncherState::Snd& a = L.snd[v][L.nSnd[v]];
                a.clip = !strcmp(clip, "switch") ? kLcSndSwitch : L.rend[v].FindClip(clip);
                a.t = tt, a.vol = vol;
                wchar_t wf[256];
                MultiByteToWideChar(CP_UTF8, 0, file, -1, wf, 256);
                err[0] = 0;
                if ((a.clip >= 0 || a.clip == kLcSndSwitch) && a.snd.Load(dir, wf, 2, err, sizeof err))
                    ++L.nSnd[v];
                else if (bad++ < 4)
                    g_log("launcher: anim sound '%s' %s: %s", clip, file, a.clip == -1 ? "no such clip" : err);
            }
            fclose(f);
            g_log("launcher: anim sounds %ls: %d loaded, %d skipped", L.set[v].animSounds, L.nSnd[v], bad);
        } else {
            g_log("launcher: anim sounds %ls not found", path);
        }
    }
    return L.clip[v][kLcActive] >= 0;
}
void LauncherRelease() {
    for (int v = 0; v < 2; ++v) {
        g_lc.rend[v].Release(), g_lc.loadFailed[v] = false;
        for (int i = 0; i < g_lc.nSnd[v]; ++i) g_lc.snd[v][i].snd.Release();
        g_lc.nSnd[v] = 0;
    }
}
void LauncherSounds(ID3D11Device* dev, const vm::Look* l, int v) {
    LauncherState& L = g_lc;
    if (InterlockedExchange(&L.switchReq, 0) && g_fp && LauncherEnsure(0, dev))
        for (int i = 0; i < L.nSnd[0]; ++i)
            if (L.snd[0][i].clip == kLcSndSwitch) L.snd[0][i].snd.Play(L.snd[0][i].vol * L.set[0].animSoundVolume);
    if (l && v >= 0) {
        if (l->clip != L.sndClip) L.sndClip = l->clip, L.sndLast = -1.f;
        for (int i = 0; i < L.nSnd[v]; ++i) {
            LauncherState::Snd& a = L.snd[v][i];
            if (a.clip == l->clip && a.t > L.sndLast && a.t <= l->clipTime && a.t > l->clipTime - 0.2f)
                a.snd.Play(a.vol * L.set[v].animSoundVolume);
        }
        L.sndLast = l->clipTime;
    } else {
        L.sndClip = -2, L.sndLast = -1.f;
    }
    for (int u = 0; u < 2; ++u)
        for (int i = 0; i < L.nSnd[u]; ++i) L.snd[u][i].snd.Update();
}
float LcSmooth(float x) {
    x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
    return x * x * (3.f - 2.f * x);
}
constexpr float kLcMaxHold = 4.f;
bool LauncherPose(int v, float t, float fireAt, float abortAt, vm::Look& l) {
    const LauncherState& L = g_lc;
    const int* c = L.clip[v];
    float fireDur = c[kLcFire] >= 0 ? L.rend[v].ClipDuration(c[kLcFire]) : 0.f;
    float deactDur = c[kLcDeact] >= 0 ? L.rend[v].ClipDuration(c[kLcDeact]) : 0.f;
    float hold = L.postHold > fireDur ? L.postHold : fireDur;
    l.clipPrev = -1;
    l.blend = 1.f;
    if (t < 0.f) return false;
    if (abortAt >= 0.f && (fireAt < 0.f || abortAt < fireAt)) fireAt = -1.f;
    else abortAt = -1.f;
    if (fireAt < 0.f && abortAt < 0.f && t >= kLcMaxHold) abortAt = kLcMaxHold;
    float actDur = c[kLcActivate] >= 0 ? L.rend[v].ClipDuration(c[kLcActivate]) : 0.f;
    const bool artic = actDur > 0.15f;
    auto upPose = [&](float tt, int* clip, float* time) {
        if (artic && tt < actDur) *clip = c[kLcActivate], *time = tt;
        else *clip = c[kLcActive], *time = artic ? tt - actDur : tt;
    };
    float upTo = fireAt >= 0.f ? fireAt : (abortAt >= 0.f ? abortAt : 1e9f);
    if (t < upTo) {
        upPose(t, &l.clip, &l.clipTime);
        if (!artic && t < L.deployTime && c[kLcDormant] >= 0) l.clipPrev = c[kLcDormant], l.clipTimePrev = 0.f, l.blend = LcSmooth(t / L.deployTime);
        return true;
    }
    float deactFrom;
    int prevClip;
    float prevTime;
    if (fireAt >= 0.f) {
        float tf = t - fireAt;
        if (tf < hold) {
            l.clip = c[kLcFire] >= 0 ? c[kLcFire] : c[kLcActive];
            l.clipTime = tf;
            if (L.fireFade > 0.f && tf < L.fireFade) upPose(fireAt, &l.clipPrev, &l.clipTimePrev), l.blend = tf / L.fireFade;
            return true;
        }
        deactFrom = fireAt + hold, prevClip = c[kLcFire] >= 0 ? c[kLcFire] : c[kLcActive], prevTime = hold;
    } else {
        deactFrom = abortAt;
        upPose(abortAt, &prevClip, &prevTime);
    }
    float td = t - deactFrom;
    if (c[kLcDeact] >= 0 && td < deactDur) {
        l.clip = c[kLcDeact];
        l.clipTime = td;
        if (L.deactFade > 0.f && td < L.deactFade) l.clipPrev = prevClip, l.clipTimePrev = prevTime, l.blend = td / L.deactFade;
        return true;
    }
    return false;
}
void LauncherDraw(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11Texture2D* bb, UINT w, UINT h, bool hw,
                  const vm::Look& gunLook) {
    LauncherState& L = g_lc;
    if (!L.configured) return;
    LONG r = InterlockedExchange(&L.req, 0);
    if (r > 0 && r <= kLauncherKinds) {
        L.kind = r - 1;
        int sel = static_cast<int>(L.selected);
        L.active = (L.kind == kLauncherIce || (L.kind == kLauncherFlame && sel == kLauncherIce)) ? 1 : 0;
        if (!_wcsicmp(L.model[0], L.model[1])) L.active = 0;
        L.t0 = static_cast<double>(L.reqUs) * 1e-6;
        if (L.logs < 40) ++L.logs, g_log("launcher: cast %d -> model %s, %.0f ms after the command", L.kind, L.active ? "ice" : "frag", (Now() - L.t0) * 1000.0);
    }
    if (L.active >= 0 && !g_fp) L.active = -1, InterlockedExchange(&L.busy, 0);
    if (L.active < 0) return LauncherSounds(dev, nullptr, -1);
    int v = L.active;
    if (!LauncherEnsure(v, dev)) {
        L.active = -1;
        InterlockedExchange(&L.busy, 0);
        return LauncherSounds(dev, nullptr, -1);
    }
    vm::Look l = gunLook;
    for (int k = 0; k < 3; ++k) l.kickPos[k] = l.kickRot[k] = l.kickPivot[k] = l.gunShift[k] = 0.f;
    l.kick = 0.f;
    l.adsAlign = 0.f;
    l.fovDeg = 0.f;
    l.spinBone = -1;
    l.shieldArmW = 0.f;
    l.flashLight = 0.f;
    float t = static_cast<float>(Now() - L.t0);
    LONG64 fu = L.fireUs, au = L.abortUs;
    float fireAt = L.driven ? (fu ? static_cast<float>(static_cast<double>(fu) * 1e-6 - L.t0) : -1.f) : static_cast<float>(L.windupMs) * 1e-3f;
    float abortAt = au ? static_cast<float>(static_cast<double>(au) * 1e-6 - L.t0) : -1.f;
    if (!LauncherPose(v, t, fireAt, abortAt, l)) {
        if (L.logs < 40) ++L.logs, g_log("launcher: done (%s at %.2f s)", fireAt >= 0.f ? "fired" : "folded without firing", fireAt >= 0.f ? fireAt : abortAt);
        L.active = -1;
        InterlockedExchange(&L.busy, 0);
        return LauncherSounds(dev, nullptr, -1);
    }
    LauncherSounds(dev, &l, v);
    vm::AnimStatePush();
    L.rend[v].Draw(ctx, rtv, bb, w, h, hw, L.set[v], l);
    vm::AnimStatePop();
}
