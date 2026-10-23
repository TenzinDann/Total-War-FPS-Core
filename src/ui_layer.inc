constexpr int kCtxSlots = 200;
constexpr int kProbeWaitFrames = 120;
constexpr int kProbeRecordFrames = 2;
constexpr int kProbeMaxSegs = 1500, kProbeMaxRuns = 600, kProbeMaxCopies = 64, kProbeStack = 10;
using FnDrawIndexed = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using FnDraw = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using FnDrawIndexedInst = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using FnDrawInst = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
using FnOMSet = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
using FnOMSetUav = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*,
                                            UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
using FnIndirect = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
using FnDispatch = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
using FnCopyRegion = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT,
                                              const D3D11_BOX*);
using FnCopy = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
using FnClearRtv = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, const FLOAT[4]);
using FnResolve = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT);
using FnExec = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);
uintptr_t g_ctxLive = 0;
template <class F>
F Live(int idx) {
    return reinterpret_cast<F>(*reinterpret_cast<uintptr_t*>(g_ctxLive + 8 * idx));
}
struct ProbeSeg {
    uintptr_t rt;
    UINT w, h, fmt, nRtv;
    bool bb, dsv, uavBind;
    UINT draws[6];
    UINT maxVerts, clears;
    void* bind[kProbeStack];
};
struct ProbeRun {
    int seg;
    UINT count, kinds, firstVerts;
    float vpX, vpY, vpW, vpH;
    void* stack[kProbeStack];
};
struct ProbeCopy {
    int seg, kind;
    bool dstBb, srcBb;
    UINT srcW, srcH, srcFmt, dstW, dstH, dstFmt;
    void* stack[kProbeStack];
};
struct ProbeFrame {
    ProbeSeg segs[kProbeMaxSegs];
    int nSegs, droppedSegs;
    ProbeRun runs[kProbeMaxRuns];
    int nRuns, droppedRuns;
    ProbeCopy copies[kProbeMaxCopies];
    int nCopies;
    UINT execs, dispatches, drawsTotal, drawsNoSeg;
};
ProbeFrame* g_pf = nullptr;
uintptr_t* g_ctxVtCopy = nullptr;
uintptr_t g_ctxVtOrig = 0;
ID3D11DeviceContext* g_ctxHooked = nullptr;
uintptr_t g_probeBb = 0;
UINT g_probeBbW = 0, g_probeBbH = 0;
volatile bool g_probeRec = false, g_probeMute = false;
int g_probeState = 0, g_probeFrames = 0, g_probeDone = 0;
int g_probeEnabled = -1;
constexpr int kPartAll = 0, kPartGun = 1, kPartCross = 2;
void DrawLayer(IDXGISwapChain* sc, int part = kPartAll);
int g_uiLayerMode = -1;
IDXGISwapChain* g_lastSc = nullptr;
DWORD g_presentTid = 0;
bool g_midDone = false;
void (*g_uiLayerGunFn)(IDXGISwapChain*) = nullptr;
ID3D11Device* g_stateDev = nullptr;
ID3D11DeviceContext1* g_ctx1 = nullptr;
ID3DDeviceContextState* g_ourState = nullptr;
struct UiLayerStat {
    UINT mid, fallback, skipThread, skipState;
    double t;
    bool loggedMid, loggedFallback, loggedThread, loggedState;
} g_uls = {};
void UiLayerAfterCopy(ID3D11DeviceContext* c, ID3D11Resource* dst, void* ret);
constexpr uintptr_t kBbCopySlot = 0x28, kBbTexOff = 0x28, kRctxCtxOff = 0x18;
const unsigned char kBbCopyBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x51, 0x28, 0x48,
                                      0x85, 0xD2, 0x74, 0x20, 0x4D, 0x8B, 0x40, 0x28, 0x4D, 0x85, 0xC0, 0x74, 0x17, 0x48,
                                      0x8B, 0x4B, 0x18, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0x78, 0x01, 0x00, 0x00};
bool g_bbRouteOk = false;
using FnBbCopy = char (*)(void* obj, void* rctx, void* src, uintptr_t a4);
FnBbCopy g_bbCopyOrig = nullptr;
uintptr_t g_bbObj = 0, g_bbVtOrig = 0;
uintptr_t* g_bbVtCopy = nullptr;
bool g_bbLoggedMissing = false, g_bbLoggedSlot = false;
uintptr_t g_probeOwn[24];
int g_probeOwnN = 0;
void ProbeStack(void** out) {
    void* raw[kProbeStack + 8] = {};
    USHORT got = RtlCaptureStackBackTrace(0, kProbeStack + 8, raw, nullptr);
    int k = 0;
    for (; k < got; ++k) {
        DWORD64 base = 0;
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(raw[k]), &base, nullptr);
        uintptr_t start = f ? static_cast<uintptr_t>(base + f->BeginAddress) : 0;
        bool own = false;
        for (int i = 0; i < g_probeOwnN; ++i) own |= start == g_probeOwn[i];
        if (!own) break;
    }
    for (int i = 0; i < kProbeStack; ++i) out[i] = k + i < got ? raw[k + i] : nullptr;
}
void ProbeTexInfo(ID3D11Resource* r, UINT* w, UINT* h, UINT* fmt) {
    *w = *h = *fmt = 0;
    if (!r) return;
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        *fmt = 1000 + static_cast<UINT>(dim);
        return;
    }
    ID3D11Texture2D* t = nullptr;
    if (SUCCEEDED(r->QueryInterface(IID_ID3D11Texture2D, reinterpret_cast<void**>(&t))) && t) {
        D3D11_TEXTURE2D_DESC d;
        t->GetDesc(&d);
        *w = d.Width, *h = d.Height, *fmt = d.Format;
        t->Release();
    }
}
ProbeSeg* ProbeCur() { return g_pf && g_pf->nSegs > 0 ? &g_pf->segs[g_pf->nSegs - 1] : nullptr; }
__attribute__((noinline, noipa)) void ProbeBind(UINT n, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv, bool uav) {
    if (!g_probeRec || g_probeMute || !g_pf) return;
    if (g_pf->nSegs >= kProbeMaxSegs) {
        ++g_pf->droppedSegs;
        return;
    }
    ProbeSeg& s = g_pf->segs[g_pf->nSegs++];
    memset(&s, 0, sizeof s);
    s.nRtv = n, s.dsv = dsv != nullptr, s.uavBind = uav;
    if (n && rtvs && rtvs[0]) {
        ID3D11Resource* r = nullptr;
        rtvs[0]->GetResource(&r);
        if (r) {
            s.rt = reinterpret_cast<uintptr_t>(r);
            s.bb = s.rt == g_probeBb;
            ProbeTexInfo(r, &s.w, &s.h, &s.fmt);
            r->Release();
        }
    }
    ProbeStack(s.bind);
}
__attribute__((noinline, noipa)) void ProbeDraw(int kind, UINT verts) {
    if (!g_probeRec || g_probeMute || !g_pf) return;
    ++g_pf->drawsTotal;
    ProbeSeg* s = ProbeCur();
    if (!s) {
        ++g_pf->drawsNoSeg;
        return;
    }
    ++s->draws[kind];
    if (verts > s->maxVerts) s->maxVerts = verts;
    if (!s->bb) return;
    void* st[kProbeStack] = {};
    ProbeStack(st);
    int seg = g_pf->nSegs - 1;
    if (g_pf->nRuns) {
        ProbeRun& last = g_pf->runs[g_pf->nRuns - 1];
        if (last.seg == seg && !memcmp(last.stack, st, 6 * sizeof(void*))) {
            ++last.count;
            last.kinds |= 1u << kind;
            return;
        }
    }
    if (g_pf->nRuns >= kProbeMaxRuns) {
        ++g_pf->droppedRuns;
        return;
    }
    ProbeRun& r = g_pf->runs[g_pf->nRuns++];
    memset(&r, 0, sizeof r);
    r.seg = seg, r.count = 1, r.kinds = 1u << kind, r.firstVerts = verts;
    memcpy(r.stack, st, sizeof st);
    UINT nv = 1;
    D3D11_VIEWPORT vp = {};
    g_ctxHooked->RSGetViewports(&nv, &vp);
    if (nv) r.vpX = vp.TopLeftX, r.vpY = vp.TopLeftY, r.vpW = vp.Width, r.vpH = vp.Height;
}
__attribute__((noinline, noipa)) void ProbeCopyEv(int kind, ID3D11Resource* dst, ID3D11Resource* src) {
    if (!g_probeRec || g_probeMute || !g_pf) return;
    bool db = reinterpret_cast<uintptr_t>(dst) == g_probeBb, sb = reinterpret_cast<uintptr_t>(src) == g_probeBb;
    UINT sw, sh, sf, dw, dh, df;
    ProbeTexInfo(src, &sw, &sh, &sf);
    ProbeTexInfo(dst, &dw, &dh, &df);
    bool full = (sw == g_probeBbW && sh == g_probeBbH) || (dw == g_probeBbW && dh == g_probeBbH);
    if (!db && !sb && !full) return;
    if (g_pf->nCopies >= kProbeMaxCopies) return;
    ProbeCopy& c = g_pf->copies[g_pf->nCopies++];
    memset(&c, 0, sizeof c);
    c.seg = g_pf->nSegs - 1, c.kind = kind, c.dstBb = db, c.srcBb = sb;
    c.srcW = sw, c.srcH = sh, c.srcFmt = sf, c.dstW = dw, c.dstH = dh, c.dstFmt = df;
    ProbeStack(c.stack);
}
void STDMETHODCALLTYPE PkDrawIndexed(ID3D11DeviceContext* c, UINT n, UINT a, INT b) {
    ProbeDraw(0, n);
    Live<FnDrawIndexed>(12)(c, n, a, b);
}
void STDMETHODCALLTYPE PkDraw(ID3D11DeviceContext* c, UINT n, UINT a) {
    ProbeDraw(1, n);
    Live<FnDraw>(13)(c, n, a);
}
void STDMETHODCALLTYPE PkDrawIndexedInst(ID3D11DeviceContext* c, UINT n, UINT i, UINT a, INT b, UINT d) {
    ProbeDraw(2, n);
    Live<FnDrawIndexedInst>(20)(c, n, i, a, b, d);
}
void STDMETHODCALLTYPE PkDrawInst(ID3D11DeviceContext* c, UINT n, UINT i, UINT a, UINT d) {
    ProbeDraw(3, n);
    Live<FnDrawInst>(21)(c, n, i, a, d);
}
void STDMETHODCALLTYPE PkDrawIndexedIndirect(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o) {
    ProbeDraw(4, 0);
    Live<FnIndirect>(39)(c, b, o);
}
void STDMETHODCALLTYPE PkDrawIndirect(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o) {
    ProbeDraw(4, 0);
    Live<FnIndirect>(40)(c, b, o);
}
void STDMETHODCALLTYPE PkOMSet(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* r, ID3D11DepthStencilView* d) {
    ProbeBind(n, r, d, false);
    Live<FnOMSet>(33)(c, n, r, d);
}
void STDMETHODCALLTYPE PkOMSetUav(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* r, ID3D11DepthStencilView* d,
                                   UINT us, UINT un, ID3D11UnorderedAccessView* const* u, const UINT* ui) {
    if (n != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) ProbeBind(n, r, d, true);
    Live<FnOMSetUav>(34)(c, n, r, d, us, un, u, ui);
}
void STDMETHODCALLTYPE PkDispatch(ID3D11DeviceContext* c, UINT x, UINT y, UINT z) {
    if (g_probeRec && !g_probeMute && g_pf) ++g_pf->dispatches;
    Live<FnDispatch>(41)(c, x, y, z);
}
void STDMETHODCALLTYPE PkCopyRegion(ID3D11DeviceContext* c, ID3D11Resource* d, UINT ds, UINT x, UINT y, UINT z, ID3D11Resource* s,
                                    UINT ss, const D3D11_BOX* b) {
    ProbeCopyEv(1, d, s);
    Live<FnCopyRegion>(46)(c, d, ds, x, y, z, s, ss, b);
}
void STDMETHODCALLTYPE PkCopy(ID3D11DeviceContext* c, ID3D11Resource* d, ID3D11Resource* s) {
    ProbeCopyEv(0, d, s);
    Live<FnCopy>(47)(c, d, s);
}
void STDMETHODCALLTYPE PkClearRtv(ID3D11DeviceContext* c, ID3D11RenderTargetView* v, const FLOAT col[4]) {
    if (g_probeRec && !g_probeMute && g_pf) {
        ProbeSeg* s = ProbeCur();
        if (s) ++s->clears;
    }
    Live<FnClearRtv>(50)(c, v, col);
}
void STDMETHODCALLTYPE PkResolve(ID3D11DeviceContext* c, ID3D11Resource* d, UINT ds, ID3D11Resource* s, UINT ss, DXGI_FORMAT f) {
    ProbeCopyEv(2, d, s);
    Live<FnResolve>(57)(c, d, ds, s, ss, f);
}
void STDMETHODCALLTYPE PkExec(ID3D11DeviceContext* c, ID3D11CommandList* l, BOOL r) {
    if (g_probeRec && !g_probeMute && g_pf) ++g_pf->execs;
    Live<FnExec>(58)(c, l, r);
}
void ProbeFmtAddr(void* p, char* out, size_t n) {
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    static uintptr_t exeEnd = 0;
    if (!exeEnd && g_base) {
        uint32_t peOff = 0, size = 0;
        if (Rd(g_base + 0x3c, peOff) && Rd(g_base + peOff + 24 + 56, size)) exeEnd = g_base + size;
    }
    if (a >= g_base && a < exeEnd) {
        snprintf(out, n, "e+%llx", static_cast<unsigned long long>(a - g_base));
        return;
    }
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (a && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(a), &m) && m) {
        GetModuleFileNameA(m, name, MAX_PATH);
        const char* b = strrchr(name, '\\');
        snprintf(out, n, "%s+%llx", b ? b + 1 : name, static_cast<unsigned long long>(a - reinterpret_cast<uintptr_t>(m)));
        return;
    }
    snprintf(out, n, "%p", p);
}
void ProbeFmtStack(void* const* st, int depth, char* out, size_t n) {
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < depth && st[i] && used + 40 < n; ++i) {
        char one[96];
        ProbeFmtAddr(st[i], one, sizeof one);
        used += snprintf(out + used, n - used, "%s%s", i ? " < " : "", one);
    }
}
const char* kProbeKinds[6] = {"DI", "D", "DII", "DIn", "ind", "?"};
void ProbeDump(int frameNo) {
    ProbeFrame& f = *g_pf;
    UINT bbSegs = 0, bbDraws = 0;
    for (int i = 0; i < f.nSegs; ++i)
        if (f.segs[i].bb) {
            ++bbSegs;
            for (UINT d : f.segs[i].draws) bbDraws += d;
        }
    g_log("ui-probe: frame %d: back buffer %p %ux%u; %d render-target bindings (%d dropped), %u draws (%u before any binding), "
          "%u bindings / %u draws on the back buffer, %d draw runs on it (%d dropped), %d full-size copies, %u command lists, %u dispatches",
          frameNo, (void*)g_probeBb, g_probeBbW, g_probeBbH, f.nSegs, f.droppedSegs, f.drawsTotal, f.drawsNoSeg, bbSegs, bbDraws, f.nRuns,
          f.droppedRuns, f.nCopies, f.execs, f.dispatches);
    char stk[1400];
    for (int i = 0; i < f.nSegs; ++i) {
        ProbeSeg& s = f.segs[i];
        UINT nd = 0;
        for (UINT d : s.draws) nd += d;
        if (!nd && !s.clears && !s.bb) continue;
        bool full = s.bb || (s.w == g_probeBbW && s.h == g_probeBbH);
        ProbeFmtStack(s.bind, full ? kProbeStack : 4, stk, sizeof stk);
        g_log("ui-probe:  seg %d rt=%s %ux%u fmt %u rtv %u dsv %d%s draws %u [DI %u D %u DII %u DIn %u ind %u] maxv %u clear %u bind: %s", i,
              s.bb ? "BACKBUFFER" : (s.rt ? "tex" : "none"), s.w, s.h, s.fmt, s.nRtv, (int)s.dsv, s.uavBind ? " +uav" : "", nd, s.draws[0],
              s.draws[1], s.draws[2], s.draws[3], s.draws[4], s.maxVerts, s.clears, stk);
    }
    for (int i = 0; i < f.nRuns; ++i) {
        ProbeRun& r = f.runs[i];
        char kinds[32] = "";
        for (int k = 0; k < 6; ++k)
            if (r.kinds & (1u << k)) strcat(kinds, kProbeKinds[k]), strcat(kinds, " ");
        ProbeFmtStack(r.stack, kProbeStack, stk, sizeof stk);
        g_log("ui-probe:  bb run %d seg %d: %u draws (%s) first verts %u viewport %.0fx%.0f@%.0f,%.0f stack: %s", i, r.seg, r.count, kinds,
              r.firstVerts, r.vpW, r.vpH, r.vpX, r.vpY, stk);
    }
    for (int i = 0; i < f.nCopies; ++i) {
        ProbeCopy& c = f.copies[i];
        ProbeFmtStack(c.stack, kProbeStack, stk, sizeof stk);
        g_log("ui-probe:  copy %d after seg %d: %s %s%ux%u f%u -> %s%ux%u f%u stack: %s", i, c.seg,
              c.kind == 0 ? "CopyResource" : c.kind == 1 ? "CopySubresourceRegion" : "ResolveSubresource", c.srcBb ? "BACKBUFFER " : "",
              c.srcW, c.srcH, c.srcFmt, c.dstBb ? "BACKBUFFER " : "", c.dstW, c.dstH, c.dstFmt, stk);
    }
}
struct ProbeSlot {
    int idx;
    void* ours;
};
const ProbeSlot kProbeSlots[] = {{12, (void*)&PkDrawIndexed}, {13, (void*)&PkDraw}, {20, (void*)&PkDrawIndexedInst},
                                 {21, (void*)&PkDrawInst}, {33, (void*)&PkOMSet}, {34, (void*)&PkOMSetUav},
                                 {39, (void*)&PkDrawIndexedIndirect}, {40, (void*)&PkDrawIndirect}, {41, (void*)&PkDispatch},
                                 {46, (void*)&PkCopyRegion}, {47, (void*)&PkCopy}, {50, (void*)&PkClearRtv},
                                 {57, (void*)&PkResolve}, {58, (void*)&PkExec}};
bool ProbeHook(ID3D11DeviceContext* ctx) {
    uintptr_t obj = reinterpret_cast<uintptr_t>(ctx);
    uintptr_t vt = Ptr(obj);
    if (!vt) return false;
    int n = 0;
    for (; n < 115; ++n) {
        uintptr_t v = 0;
        if (!Rd(vt + 8 * n, v) || !InModule(v)) break;
    }
    if (n < 115) {
        g_log("ui-probe: context %p vtable %p: slot %d does not point into a module; not probing", (void*)ctx, (void*)vt, n);
        return false;
    }
    auto* thunks = static_cast<unsigned char*>(VirtualAlloc(nullptr, kCtxSlots * 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* table = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (kCtxSlots + 2) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!thunks || !table) return false;
    table[0] = Ptr(vt - 16);
    table[1] = Ptr(vt - 8);
    for (int i = 0; i < kCtxSlots; ++i) {
        unsigned char* th = thunks + 16 * i;
        uintptr_t slot = vt + 8 * i;
        th[0] = 0x48, th[1] = 0xB8;
        memcpy(th + 2, &slot, 8);
        th[10] = 0xFF, th[11] = 0x20;
        th[12] = th[13] = th[14] = th[15] = 0xCC;
        table[2 + i] = reinterpret_cast<uintptr_t>(th);
    }
    for (const ProbeSlot& s : kProbeSlots) table[2 + s.idx] = reinterpret_cast<uintptr_t>(s.ours);
    DWORD old;
    VirtualProtect(thunks, kCtxSlots * 16, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), thunks, kCtxSlots * 16);
    VirtualProtect(table, (kCtxSlots + 2) * sizeof(uintptr_t), PAGE_READONLY, &old);
    g_probeOwnN = 0;
    for (const ProbeSlot& s : kProbeSlots) g_probeOwn[g_probeOwnN++] = reinterpret_cast<uintptr_t>(s.ours);
    g_probeOwn[g_probeOwnN++] = reinterpret_cast<uintptr_t>(&ProbeBind);
    g_probeOwn[g_probeOwnN++] = reinterpret_cast<uintptr_t>(&ProbeDraw);
    g_probeOwn[g_probeOwnN++] = reinterpret_cast<uintptr_t>(&ProbeCopyEv);
    g_probeOwn[g_probeOwnN++] = reinterpret_cast<uintptr_t>(&ProbeStack);
    g_ctxLive = vt;
    uintptr_t ours = reinterpret_cast<uintptr_t>(table + 2);
    SIZE_T w = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(obj), &ours, sizeof ours, &w) || w != sizeof ours) {
        g_log("ui-probe: vptr write failed on context %p", (void*)ctx);
        return false;
    }
    g_ctxVtCopy = table + 2, g_ctxVtOrig = vt, g_ctxHooked = ctx;
    char vtName[160], omName[160];
    ProbeFmtAddr(reinterpret_cast<void*>(vt), vtName, sizeof vtName);
    ProbeFmtAddr(reinterpret_cast<void*>(Ptr(vt + 8 * 33)), omName, sizeof omName);
    g_log("ui-probe: hooked immediate context %p (live table %s%s, our table %p with %d forwarding thunks; OMSetRenderTargets now %s)", (void*)ctx,
          vtName, InModule(vt) ? "" : " inside the object", (void*)ours, kCtxSlots, omName);
    return true;
}
void ProbeUnhook() {
    if (!g_ctxHooked) return;
    uintptr_t obj = reinterpret_cast<uintptr_t>(g_ctxHooked);
    if (Ptr(obj) == reinterpret_cast<uintptr_t>(g_ctxVtCopy)) {
        SIZE_T w = 0;
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(obj), &g_ctxVtOrig, sizeof g_ctxVtOrig, &w);
        g_log("ui-probe: context %p vtable restored", (void*)g_ctxHooked);
    } else {
        g_log("ui-probe: context %p vtable changed by someone else (%p); leaving it", (void*)g_ctxHooked, (void*)Ptr(obj));
    }
    g_ctxHooked = nullptr;
}
bool InExe(void* p) {
    static uintptr_t base = 0, exeEnd = 0;
    if (base != g_base) {
        uint32_t peOff = 0, size = 0;
        base = g_base, exeEnd = 0;
        if (g_base && Rd(g_base + 0x3c, peOff) && Rd(g_base + peOff + 24 + 56, size)) exeEnd = g_base + size;
    }
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    return a >= g_base && a < exeEnd;
}
void ProbeStep() {
    if (!g_probeEnabled || g_probeState >= 3 || !g_ctxHooked) return;
    if (g_probeState == 0) {
        g_probeState = 1, g_probeFrames = 0;
        return;
    }
    if (g_probeState == 1) {
        if (++g_probeFrames < kProbeWaitFrames) return;
        if (!g_pf) g_pf = static_cast<ProbeFrame*>(VirtualAlloc(nullptr, sizeof(ProbeFrame), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!g_pf) {
            g_probeState = 3;
            return;
        }
        memset(g_pf, 0, sizeof(ProbeFrame));
        g_probeRec = true, g_probeState = 2;
        return;
    }
    g_probeRec = false;
    ProbeDump(g_probeDone + 1);
    if (++g_probeDone >= kProbeRecordFrames) {
        g_probeState = 3;
        ProbeUnhook();
        g_log("ui-probe: done (%d frames logged)", g_probeDone);
        return;
    }
    memset(g_pf, 0, sizeof(ProbeFrame));
    g_probeRec = true;
}
void UiLayerReadIni() {
    if (g_uiLayerMode >= 0) return;
    g_uiLayerMode = GetPrivateProfileIntW(L"ui_layer", L"mode", 1, g_ini) == 1 ? 1 : 0;
    g_probeEnabled = GetPrivateProfileIntW(L"ui_layer", L"probe", 0, g_ini) != 0;
    g_log("ui-layer: mode %d (%s), probe %d", g_uiLayerMode,
          g_uiLayerMode == 1 ? "gun drawn right after the game copies the scene into the back buffer (its back-buffer object), UI on top"
                             : "old: everything at Present",
          g_probeEnabled);
}
void UiLayerOnPresent(ID3D11DeviceContext* ctx, ID3D11Texture2D* bb, UINT bbW, UINT bbH) {
    UiLayerReadIni();
    g_probeBb = reinterpret_cast<uintptr_t>(bb), g_probeBbW = bbW, g_probeBbH = bbH;
    bool want = g_probeEnabled && g_probeState < 3 && (g_fp || g_testLayer);
    if (want) {
        uintptr_t obj = reinterpret_cast<uintptr_t>(ctx), cur = Ptr(obj);
        if (g_ctxHooked != ctx) {
            if (g_ctxHooked) g_log("ui-layer: immediate context changed (%p -> %p); hooking the new one", (void*)g_ctxHooked, (void*)ctx);
            g_ctxHooked = nullptr;
            if (!ProbeHook(ctx)) g_probeEnabled = 0;
        } else if (cur != reinterpret_cast<uintptr_t>(g_ctxVtCopy)) {
            static int rehooks = 0;
            if (cur == g_ctxVtOrig) {
                SIZE_T w = 0;
                uintptr_t ours = reinterpret_cast<uintptr_t>(g_ctxVtCopy);
                WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(obj), &ours, sizeof ours, &w);
                if (++rehooks <= 3) g_log("ui-layer: context vtable pointer was reset to the runtime's; put ours back (%d)", rehooks);
            } else {
                g_log("ui-layer: context vtable pointer changed to %p by someone else; hooking again", (void*)cur);
                g_ctxHooked = nullptr;
                if (!ProbeHook(ctx)) g_probeEnabled = 0;
            }
        }
    }
    if (g_probeEnabled) ProbeStep();
    double now = Now();
    if (g_uiLayerMode == 1 && g_fp && now - g_uls.t >= 5.0) {
        if (g_uls.mid || g_uls.fallback)
            g_log("ui-layer: 5s gun drawn under the UI %u frames, at Present (fallback, no back-buffer copy seen) %u frames; skipped: other thread %u, no state %u",
                  g_uls.mid, g_uls.fallback, g_uls.skipThread, g_uls.skipState);
        g_uls.t = now, g_uls.mid = g_uls.fallback = g_uls.skipThread = g_uls.skipState = 0;
    }
}
int UiLayerPresentPart() {
    int part = g_midDone ? kPartCross : kPartAll;
    if (!g_midDone && g_uiLayerMode == 1 && g_bbObj && g_fp) {
        ++g_uls.fallback;
        if (!g_uls.loggedFallback) {
            g_uls.loggedFallback = true;
            g_log("ui-layer: a frame without a back-buffer copy from the game: gun drawn at Present (over the UI) for that frame");
        }
    }
    g_midDone = false;
    return part;
}
bool UiLayerStateReady(ID3D11DeviceContext* c) {
    ID3D11Device* dev = nullptr;
    c->GetDevice(&dev);
    if (!dev) return false;
    if (g_ourState && g_stateDev == dev) {
        dev->Release();
        return true;
    }
    if (g_ourState) g_ourState->Release(), g_ourState = nullptr;
    if (g_ctx1) g_ctx1->Release(), g_ctx1 = nullptr;
    ID3D11Device1* d1 = nullptr;
    bool ok = false;
    if (SUCCEEDED(dev->QueryInterface(IID_ID3D11Device1, reinterpret_cast<void**>(&d1))) && d1 &&
        SUCCEEDED(c->QueryInterface(IID_ID3D11DeviceContext1, reinterpret_cast<void**>(&g_ctx1))) && g_ctx1) {
        D3D_FEATURE_LEVEL fl = dev->GetFeatureLevel();
        UINT flags = (dev->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        HRESULT hr = d1->CreateDeviceContextState(flags, &fl, 1, D3D11_SDK_VERSION, IID_ID3D11Device1, nullptr, &g_ourState);
        ok = SUCCEEDED(hr) && g_ourState;
        g_log("ui-layer: own pipeline state for mid-frame drawing %s (feature level 0x%x, flags 0x%x, hr 0x%08lx)", ok ? "created" : "FAILED",
              (unsigned)fl, flags, hr);
    }
    if (d1) d1->Release();
    if (!ok && g_ctx1) g_ctx1->Release(), g_ctx1 = nullptr;
    g_stateDev = dev;
    dev->Release();
    return ok;
}
void UiLayerAfterCopy(ID3D11DeviceContext* c, ID3D11Resource* dst, void* ret) {
    if (g_probeMute || g_uiLayerMode != 1 || g_midDone || !g_lastSc || !g_probeBb) return;
    if (reinterpret_cast<uintptr_t>(dst) != g_probeBb || !InExe(ret)) return;
    if (g_failed || !(g_fp || g_testLayer)) return;
    if (GetCurrentThreadId() != g_presentTid) {
        ++g_uls.skipThread;
        if (!g_uls.loggedThread) {
            g_uls.loggedThread = true;
            g_log("ui-layer: back-buffer copy on thread %lu, Present runs on %lu; not drawing there", GetCurrentThreadId(), g_presentTid);
        }
        return;
    }
    if (!UiLayerStateReady(c)) {
        ++g_uls.skipState;
        if (!g_uls.loggedState) {
            g_uls.loggedState = true;
            g_log("ui-layer: no ID3D11DeviceContext1 state object; gun stays at Present");
        }
        return;
    }
    ID3DDeviceContextState* prev = nullptr;
    g_ctx1->SwapDeviceContextState(g_ourState, &prev);
    g_probeMute = true;
    if (g_uiLayerGunFn)
        g_uiLayerGunFn(g_lastSc);
    else
        DrawLayer(g_lastSc, kPartGun);
    g_probeMute = false;
    g_ctx1->SwapDeviceContextState(prev, nullptr);
    if (prev) prev->Release();
    g_midDone = true;
    ++g_uls.mid;
    if (!g_uls.loggedMid) {
        char who[96];
        ProbeFmtAddr(ret, who, sizeof who);
        g_uls.loggedMid = true;
        g_log("ui-layer: gun drawn right after the game copied into the back buffer (caller %s); UI now draws on top", who);
    }
}
__attribute__((noinline)) char HkBbCopy(void* obj, void* rctx, void* src, uintptr_t a4) {
    char r = g_bbCopyOrig(obj, rctx, src, a4);
    if (r) {
        auto* c = reinterpret_cast<ID3D11DeviceContext*>(Ptr(reinterpret_cast<uintptr_t>(rctx) + kRctxCtxOff));
        auto* d = reinterpret_cast<ID3D11Resource*>(Ptr(reinterpret_cast<uintptr_t>(obj) + kBbTexOff));
        if (c && d) UiLayerAfterCopy(c, d, __builtin_return_address(0));
    }
    return r;
}
void BbUnhook(const char* why) {
    if (!g_bbObj) return;
    if (Ptr(g_bbObj) == reinterpret_cast<uintptr_t>(g_bbVtCopy)) {
        SIZE_T w = 0;
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(g_bbObj), &g_bbVtOrig, sizeof g_bbVtOrig, &w);
        g_log("ui-layer: back-buffer object %p vtable restored (%s)", (void*)g_bbObj, why);
    }
    g_bbObj = 0;
}
void UiLayerBbStep() {
    UiLayerReadIni();
    bool want = g_uiLayerMode == 1 && g_bbRouteOk && (g_fp || g_testLayer) && !g_failed;
    if (!want) {
        BbUnhook(!(g_fp || g_testLayer) ? "first person off" : "mid-frame route off");
        return;
    }
    uintptr_t r = Ptr(g_base + g_ra.globalRenderer), rvt = r ? Ptr(r) : 0;
    uintptr_t t = rvt && Ptr(rvt + g_ra.vtGetBbSlot) == g_base + g_ra.fnGetBb ? Ptr(r + g_ra.oBackBufferObj) : 0;
    uintptr_t vt = t ? Ptr(t) : 0;
    if (!vt) {
        if (!g_bbLoggedMissing) {
            g_bbLoggedMissing = true;
            g_log("ui-layer: back-buffer object not found (renderer %p vtable %p object %p); gun stays at Present this frame", (void*)r,
                  (void*)rvt, (void*)t);
        }
        return;
    }
    if (t == g_bbObj && vt == reinterpret_cast<uintptr_t>(g_bbVtCopy)) return;
    if (g_bbObj && g_bbObj != t) BbUnhook("back-buffer object changed");
    if (vt != reinterpret_cast<uintptr_t>(g_bbVtCopy)) {
        if (Ptr(vt + kBbCopySlot) != g_base + g_ra.fnBbCopy) {
            if (!g_bbLoggedSlot) {
                g_bbLoggedSlot = true;
                g_log("ui-layer: back-buffer object %p vtable %p copy slot is %p, not e+%llx; gun stays at Present", (void*)t, (void*)vt,
                      (void*)Ptr(vt + kBbCopySlot), static_cast<unsigned long long>(g_ra.fnBbCopy));
            }
            return;
        }
        if (!g_bbVtCopy || g_bbVtOrig != vt) {
            constexpr int kMax = 128;
            int n = 0;
            for (; n < kMax; ++n) {
                uintptr_t v = 0;
                if (!Rd(vt + 8 * n, v) || !InExe(reinterpret_cast<void*>(v))) break;
            }
            if (n <= static_cast<int>(kBbCopySlot / 8)) return;
            auto* copy = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (n + 2) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!copy) return;
            copy[0] = Ptr(vt - 16);
            copy[1] = Ptr(vt - 8);
            for (int i = 0; i < n; ++i) copy[2 + i] = Ptr(vt + 8 * i);
            g_bbCopyOrig = reinterpret_cast<FnBbCopy>(copy[2 + kBbCopySlot / 8]);
            copy[2 + kBbCopySlot / 8] = reinterpret_cast<uintptr_t>(&HkBbCopy);
            DWORD old;
            VirtualProtect(copy, (n + 2) * sizeof(uintptr_t), PAGE_READONLY, &old);
            g_bbVtCopy = copy + 2, g_bbVtOrig = vt;
            g_log("ui-layer: back-buffer object vtable %p copied (%d slots), slot +0x%llx -> our copy hook", (void*)vt, n,
                  static_cast<unsigned long long>(kBbCopySlot));
        }
        uintptr_t ours = reinterpret_cast<uintptr_t>(g_bbVtCopy);
        SIZE_T w = 0;
        if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(t), &ours, sizeof ours, &w) || w != sizeof ours) {
            g_log("ui-layer: vptr write failed on back-buffer object %p", (void*)t);
            return;
        }
    }
    g_bbObj = t;
    g_log("ui-layer: hooked the game's back-buffer object %p (renderer %p +0x%llx); D3D context untouched", (void*)t, (void*)r,
          static_cast<unsigned long long>(g_ra.oBackBufferObj));
}
