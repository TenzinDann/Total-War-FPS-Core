struct LadderGfx {
    volatile LONG seq = 0;
    vmgame::LadderView v[vmgame::kLadderMax] = {};
    int n = 0;
    volatile LONG gunDown = 0;
    volatile LONG sndReq[3] = {0, 0, 0};
    volatile LONG stepVolMilli = 450;
    bool configured = false, loadFailed = false, sndTried = false;
    wchar_t model[MAX_PATH * 2] = {};
    vm::Renderer rend;
    vm::Settings set;
    vm::GunSound creak, impact, rung;
    float volume = 1.f;
    int logs = 0;
} g_ld;
void LadderSetup(const wchar_t* dllDir) {
    LadderGfx& G = g_ld;
    G.configured = false;
    if (!GetPrivateProfileIntW(L"ladder", L"enabled", 1, g_ini)) return;
    wchar_t rel[MAX_PATH];
    GetPrivateProfileStringW(L"ladder", L"model", L"tenzin_fps_vm\\_ladder\\ladder.hvm", rel, MAX_PATH, g_ini);
    LauncherResolve(dllDir, rel, G.model);
    wchar_t b[32];
    GetPrivateProfileStringW(L"ladder", L"volume", L"1", b, 32, g_ini);
    G.volume = static_cast<float>(_wtof(b));
    if (!(G.volume >= 0.f && G.volume <= 2.f)) G.volume = 1.f;
    G.configured = GetFileAttributesW(G.model) != INVALID_FILE_ATTRIBUTES;
    g_log("ladder: %s, model %ls, volume %.2f", G.configured ? "on" : "OFF (model not found)", G.model, G.volume);
}
void LadderSettings() {
    vm::Settings s;
    vm::LoadSettings(g_ini, s);
    s.offset[0] = s.offset[1] = s.offset[2] = 0.f;
    for (int k = 0; k < 3; ++k) s.rotDeg[k] = s.tiltDeg[k] = 0.f;
    s.scale = 1.f;
    s.mirrorX = false;
    s.muzzleBone = -2;
    s.adsSightBone = -1;
    s.handIk = false;
    s.sceneLight = 0.f;
    g_ld.set = s;
}
bool LadderEnsure(ID3D11Device* dev) {
    LadderGfx& G = g_ld;
    if (G.rend.Ready() && G.rend.Device() == dev) return true;
    if (G.loadFailed || !G.configured) return false;
    G.rend.Release();
    LadderSettings();
    char err[256] = "";
    double t = Now();
    if (!G.rend.Init(dev, G.model, err, sizeof err)) {
        G.loadFailed = true;
        g_log("ladder: load %ls failed: %s", G.model, err);
        return false;
    }
    g_log("ladder: loaded %ls in %.1f ms (%u tris)", G.model, (Now() - t) * 1000.0, G.rend.Triangles());
    return true;
}
void LadderLoadSounds() {
    LadderGfx& G = g_ld;
    if (G.sndTried || !G.configured) return;
    G.sndTried = true;
    wchar_t dir[MAX_PATH * 2];
    wcscpy(dir, G.model);
    if (wchar_t* sl = wcsrchr(dir, L'\\')) *sl = 0;
    char err[256] = "";
    bool a = G.creak.Load(dir, L"creak_1.wav;creak_2.wav;creak_3.wav;creak_4.wav;creak_5.wav", 2, err, sizeof err);
    if (!a) g_log("ladder: raise creak sounds: %s", err);
    bool b = G.impact.Load(dir, L"impact_1.wav;impact_2.wav;impact_3.wav;impact_4.wav;impact_5.wav", 2, err, sizeof err);
    if (!b) g_log("ladder: raise impact sounds: %s", err);
    bool c = G.rung.Load(dir, L"rung_1.wav;rung_2.wav;rung_3.wav;rung_4.wav", 3, err, sizeof err);
    if (!c) g_log("ladder: rung sounds: %s (falls back to the gun's footstep)", err);
    g_log("ladder: sounds creak %d / impact %d / rung %d variants", G.creak.Variants(), G.impact.Variants(), G.rung.Variants());
}
void LadderRelease() {
    g_ld.rend.Release();
    g_ld.loadFailed = false;
}
bool LadderGunDown() { return g_ld.gunDown != 0; }
void LadderWorld(const vmgame::LadderView& v, float* m) {
    float fx = v.dir[0], fz = v.dir[1];
    float t = v.tiltDeg * 3.14159265f / 180.f, c = std::cos(t), s = std::sin(t);
    const float X[3] = {fz, 0.f, -fx};
    const float Y[3] = {s * fx, c, s * fz};
    const float Z[3] = {c * fx, -s, c * fz};
    for (int r = 0; r < 3; ++r) {
        m[r * 4 + 0] = X[r];
        m[r * 4 + 1] = Y[r] * v.scaleY;
        m[r * 4 + 2] = Z[r];
        m[r * 4 + 3] = v.base[r];
    }
}
void LadderDraw(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, UINT w, UINT h, bool hw) {
    LadderGfx& G = g_ld;
    if (!G.configured) return;
    LONG rq[3];
    for (int k = 0; k < 3; ++k) rq[k] = InterlockedExchange(&G.sndReq[k], 0);
    if ((rq[0] || rq[1] || rq[2]) && !G.sndTried) LadderLoadSounds();
    if (rq[0] && G.creak.Loaded()) G.creak.Play(0.8f * G.volume);
    if (rq[1] && G.impact.Loaded()) G.impact.Play(0.55f * G.volume);
    if (rq[2]) {
        float sv = static_cast<float>(G.stepVolMilli) * 1e-3f;
        if (G.rung.Loaded())
            G.rung.Play(sv * G.volume, false, 0.92f + 0.16f * static_cast<float>(rq[2] & 1));
        else if (Wp().stepFoot.Loaded())
            Wp().stepFoot.Play(Wp().set.stepFootVolume * sv);
    }
    G.creak.Update(), G.impact.Update(), G.rung.Update();
    vmgame::LadderView v[vmgame::kLadderMax];
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        LONG s1 = G.seq;
        if (s1 & 1) continue;
        MemoryBarrier();
        n = G.n;
        if (n < 0 || n > vmgame::kLadderMax) n = 0;
        memcpy(v, G.v, sizeof v);
        MemoryBarrier();
        if (G.seq == s1) break;
        n = 0;
    }
    if (!n || !g_fp) return;
    vm::WorldCam wc;
    if (!ReadCam(&wc) || !LadderEnsure(dev)) return;
    for (int i = 0; i < n; ++i) {
        if (!(v[i].clipHi > v[i].clipLo)) continue;
        float wm[12];
        LadderWorld(v[i], wm);
        const float* ax[3] = {wc.right, wc.up, wc.fwd};
        vm::Look l;
        l.worldPose = true;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c)
                l.worldModel[r * 4 + c] = ax[r][0] * wm[c] + ax[r][1] * wm[4 + c] + ax[r][2] * wm[8 + c];
            l.worldModel[r * 4 + 3] -= ax[r][0] * wc.eye[0] + ax[r][1] * wc.eye[1] + ax[r][2] * wc.eye[2];
        }
        l.fovDeg = wc.fovY * 180.f / 3.14159265f;
        static const float kSun[3] = {0.35f, 0.82f, 0.25f};
        for (int r = 0; r < 3; ++r) l.worldLight[r] = ax[r][0] * kSun[0] + ax[r][1] * kSun[1] + ax[r][2] * kSun[2];
        const float* M = l.worldModel;
        float Yc[3] = {M[1], M[5], M[9]}, O[3] = {M[3], M[7], M[11]};
        float y2 = Yc[0] * Yc[0] + Yc[1] * Yc[1] + Yc[2] * Yc[2];
        if (y2 > 1e-8f && (v[i].clipLo > 0.f || v[i].clipHi < 1e8f)) {
            float a[3] = {Yc[0] / y2, Yc[1] / y2, Yc[2] / y2}, d = -(a[0] * O[0] + a[1] * O[1] + a[2] * O[2]);
            l.clip0[0] = a[0], l.clip0[1] = a[1], l.clip0[2] = a[2], l.clip0[3] = d - v[i].clipLo;
            l.clip1[0] = -a[0], l.clip1[1] = -a[1], l.clip1[2] = -a[2], l.clip1[3] = -d + v[i].clipHi;
        }
        l.time = static_cast<float>(Now());
        vm::AnimStatePush();
        G.rend.Draw(ctx, rtv, nullptr, w, h, hw, G.set, l);
        vm::AnimStatePop();
    }
    if (G.logs < 6) ++G.logs, g_log("ladder: drew %d ladder(s), first at %.1f,%.1f,%.1f tilt %.1f scale %.2f visible %.1f..%.1f", n, v[0].base[0],
                                     v[0].base[1], v[0].base[2], v[0].tiltDeg, v[0].scaleY, v[0].clipLo, v[0].clipHi);
}
