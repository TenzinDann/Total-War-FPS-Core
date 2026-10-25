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
bool g_uiStateSwap = false;
bool g_uiMidGpuTimer = false;
bool g_uiPresentPartial = false;
bool g_wdOn = true;
UINT g_wdMs = 2000;
int g_uiTarget = 0;
bool g_ltOn = true;
UINT g_ltWaitMs = 4;
bool g_autoSafe = true;
bool g_ltGuardOn = true;
UINT g_ltGuardSpinUs = 1000;
bool SafeMarkerMid(char* why, size_t n);
double g_uiMidT = 0;
bool g_uiLoggedManual = false, g_uiLoggedAfterMid = false;
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
    UINT midComp, skipAa, skipRt;
    bool loggedComp, loggedAa, loggedRt;
    UINT midView, skipViewRt;
    bool loggedView, loggedViewRt;
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
bool g_compRouteOk = false;
constexpr uintptr_t kCompExecSlot = 0x8;
using FnCompExec = uintptr_t (*)(void* self, uintptr_t a2, uintptr_t a3, uintptr_t a4);
FnCompExec g_compExecOrig = nullptr;
uintptr_t g_compObj = 0;
uintptr_t* g_compVtCopy = nullptr;
bool g_compLoggedList = false, g_compLoggedMissing = false;
ID3D11DeviceContext* g_layerCtx = nullptr;
bool g_viewRouteOk = false;
constexpr uintptr_t kViewUiSlot = 0x90;
using FnViewUi = uintptr_t (*)(void* self, uintptr_t a2, uintptr_t a3, uintptr_t a4);
FnViewUi g_viewUiOrig = nullptr;
constexpr int kMaxViews = 4;
uintptr_t g_viewObjs[kMaxViews] = {};
int g_viewN = 0;
uintptr_t* g_viewVtCopy = nullptr;
bool g_viewLoggedMissing = false;
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
    wchar_t st[16] = L"";
    GetPrivateProfileStringW(L"ui_layer", L"state", L"manual", st, 16, g_ini);
    g_uiStateSwap = _wcsicmp(st, L"swap") == 0;
    g_uiMidGpuTimer = GetPrivateProfileIntW(L"ui_layer", L"mid_gpu_timer", 0, g_ini) != 0;
    g_log("ui-layer: mid-frame state %s, GPU timer on the mid-frame draw %s",
          g_uiStateSwap ? "swap (SwapDeviceContextState, the old way)" : "manual (full save / ClearState / restore)", g_uiMidGpuTimer ? "on" : "off");
    GetPrivateProfileStringW(L"ui_layer", L"present_state", L"manual", st, 16, g_ini);
    g_uiPresentPartial = _wcsicmp(st, L"partial") == 0;
    g_wdOn = GetPrivateProfileIntW(L"ui_layer", L"watchdog", 1, g_ini) != 0;
    g_wdMs = GetPrivateProfileIntW(L"ui_layer", L"watchdog_ms", 2000, g_ini);
    if (g_wdMs < 500) g_wdMs = 500;
    g_log("ui-layer: Present state %s; watchdog %s (%u ms)", g_uiPresentPartial ? "partial (the old way)" : "manual (full save / ClearState / restore in first person)",
          g_wdOn ? "on" : "off", g_wdMs);
    GetPrivateProfileStringW(L"ui_layer", L"target", L"auto", st, 16, g_ini);
    g_uiTarget = _wcsicmp(st, L"swapchain") == 0 ? 1 : _wcsicmp(st, L"game") == 0 ? 2 : 0;
    g_log("ui-layer: draw target %s", g_uiTarget == 1 ? "swapchain (forced, the old way)"
                                      : g_uiTarget == 2 ? "game's back buffer (forced)"
                                                        : "auto (the game's own back buffer when the swapchain hands out a different one)");
    g_ltOn = GetPrivateProfileIntW(L"ui_layer", L"lock_free", 1, g_ini) != 0;
    g_ltWaitMs = GetPrivateProfileIntW(L"ui_layer", L"lock_free_wait_ms", 4, g_ini);
    if (g_ltWaitMs > 50) g_ltWaitMs = 50;
    g_log("ui-layer: lock-free %s", g_ltOn ? "on (in first person the render thread neither creates nor releases D3D objects - a helper thread does)"
                                          : "off (old way: a render target view created / released every frame)");
    g_ltGuardOn = GetPrivateProfileIntW(L"ui_layer", L"lock_guard", 1, g_ini) != 0;
    g_ltGuardSpinUs = GetPrivateProfileIntW(L"ui_layer", L"lock_guard_spin_us", 1000, g_ini);
    if (g_ltGuardSpinUs > 5000) g_ltGuardSpinUs = 5000;
    g_log("ui-layer: lock guard %s (tries for up to %u us)", g_ltGuardOn ? "on (our drawing before Present only runs when the D3D11 device lock is free - never waits for it)" : "off",
          g_ltGuardSpinUs);
    g_autoSafe = GetPrivateProfileIntW(L"ui_layer", L"auto_safe", 1, g_ini) != 0;
    wchar_t ms[8] = L"";
    GetPrivateProfileStringW(L"ui_layer", L"mode", L"", ms, 8, g_ini);
    char why[240];
    if (g_autoSafe && !ms[0] && g_uiLayerMode == 1 && SafeMarkerMid(why, sizeof why)) {
        g_uiLayerMode = 0;
        g_log("ui-layer: safe mode: mid-frame gun drawing stays off on this PC - it froze here before (%s); the gun is drawn at Present with the "
              "interface kept on top. [ui_layer] mode=1 forces it back, auto_safe=0 ignores the marker", why);
    }
}
void UiLayerOnPresent(ID3D11DeviceContext* ctx, ID3D11Texture2D* bb, UINT bbW, UINT bbH) {
    UiLayerReadIni();
    g_layerCtx = ctx;
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
            g_log("ui-layer: 5s gun drawn under the UI %u frames (%u after the composite step, %u before the frame view's UI, the rest after the back-buffer copy), "
                  "at Present (fallback) %u frames; skipped: other thread %u, no state %u, anti-aliasing off at the composite step %u, render target not the back buffer %u / %u",
                  g_uls.mid, g_uls.midComp, g_uls.midView, g_uls.fallback, g_uls.skipThread, g_uls.skipState, g_uls.skipAa, g_uls.skipRt, g_uls.skipViewRt);
        g_uls.t = now, g_uls.mid = g_uls.fallback = g_uls.skipThread = g_uls.skipState = 0;
        g_uls.midComp = g_uls.skipAa = g_uls.skipRt = g_uls.midView = g_uls.skipViewRt = 0;
    }
}
int UiLayerPresentPart() {
    int part = g_midDone ? kPartCross : kPartAll;
    if (g_midDone && !g_uiLoggedAfterMid) {
        g_uiLoggedAfterMid = true;
        g_log("ui-layer: first Present after a mid-frame gun draw reached (%.1f ms later, state %s)", (Now() - g_uiMidT) * 1000.0,
              g_uiStateSwap ? "swap" : "manual");
    }
    if (!g_midDone && g_uiLayerMode == 1 && (g_bbObj || g_compObj || g_viewN) && g_fp) {
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
struct FullState {
    static constexpr UINT kCb = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, kSrv = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,
                          kSamp = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, kVb = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, kUav = D3D11_1_UAV_SLOT_COUNT,
                          kRt = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, kVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE,
                          kSo = D3D11_SO_BUFFER_SLOT_COUNT, kInst = 256, kDefNum = 4096;
    enum { VS, HS, DS, GS, PS, CS, kStages };
    struct Stage {
        IUnknown* shader;
        ID3D11ClassInstance* inst[kInst];
        UINT nInst;
        ID3D11Buffer* cb[kCb];
        UINT cbFirst[kCb], cbNum[kCb];
        ID3D11ShaderResourceView* srv[kSrv];
        ID3D11SamplerState* samp[kSamp];
    } st[kStages];
    ID3D11InputLayout* layout;
    ID3D11Buffer *vb[kVb], *ib;
    UINT vbStride[kVb], vbOffset[kVb], ibOffset;
    DXGI_FORMAT ibFormat;
    D3D11_PRIMITIVE_TOPOLOGY topo;
    ID3D11RasterizerState* rs;
    UINT nVp, nSc;
    D3D11_VIEWPORT vp[kVp];
    D3D11_RECT sc[kVp];
    ID3D11RenderTargetView* rtv[kRt];
    ID3D11DepthStencilView* dsv;
    ID3D11UnorderedAccessView *omUav[kUav], *csUav[kUav];
    UINT nUav;
    ID3D11BlendState* bs;
    FLOAT blendFactor[4];
    UINT sampleMask;
    ID3D11DepthStencilState* dss;
    UINT stencilRef;
    ID3D11Buffer* so[kSo];
    ID3D11Predicate* pred;
    BOOL predValue;
    bool ranges;
#define TZ_FS_SAVE(I,P,T) \
    { \
        Stage& s = st[I]; \
        T* sh = nullptr; \
        s.nInst = kInst; \
        c->P##GetShader(&sh, s.inst, &s.nInst); \
        s.shader = sh; \
        if (c1) \
            c1->P##GetConstantBuffers1(0, kCb, s.cb, s.cbFirst, s.cbNum); \
        else \
            c->P##GetConstantBuffers(0, kCb, s.cb); \
        c->P##GetShaderResources(0, kSrv, s.srv); \
        c->P##GetSamplers(0, kSamp, s.samp); \
    }
#define TZ_FS_RESTORE(I,P,T) \
    { \
        Stage& s = st[I]; \
        c->P##SetShader(static_cast<T*>(s.shader), s.nInst ? s.inst : nullptr, s.nInst); \
        if (c1 && ranges) \
            c1->P##SetConstantBuffers1(0, kCb, s.cb, s.cbFirst, s.cbNum); \
        else \
            c->P##SetConstantBuffers(0, kCb, s.cb); \
        c->P##SetShaderResources(0, kSrv, s.srv); \
        c->P##SetSamplers(0, kSamp, s.samp); \
    }
    void Save(ID3D11DeviceContext* c, ID3D11DeviceContext1* c1) {
        ID3D11Device* d = nullptr;
        c->GetDevice(&d);
        nUav = d && d->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1 ? kUav : D3D11_PS_CS_UAV_REGISTER_COUNT;
        if (d) d->Release();
        for (Stage& s : st)
            for (UINT k = 0; k < kCb; ++k) s.cbFirst[k] = 0, s.cbNum[k] = kDefNum;
        TZ_FS_SAVE(VS, VS, ID3D11VertexShader)
        TZ_FS_SAVE(HS, HS, ID3D11HullShader)
        TZ_FS_SAVE(DS, DS, ID3D11DomainShader)
        TZ_FS_SAVE(GS, GS, ID3D11GeometryShader)
        TZ_FS_SAVE(PS, PS, ID3D11PixelShader)
        TZ_FS_SAVE(CS, CS, ID3D11ComputeShader)
        ranges = false;
        for (const Stage& s : st)
            for (UINT k = 0; k < kCb; ++k) ranges |= s.cb[k] && (s.cbFirst[k] != 0 || s.cbNum[k] != kDefNum);
        c->IAGetInputLayout(&layout);
        c->IAGetVertexBuffers(0, kVb, vb, vbStride, vbOffset);
        c->IAGetIndexBuffer(&ib, &ibFormat, &ibOffset);
        c->IAGetPrimitiveTopology(&topo);
        c->RSGetState(&rs);
        nVp = nSc = kVp;
        c->RSGetViewports(&nVp, vp);
        c->RSGetScissorRects(&nSc, sc);
        c->OMGetRenderTargetsAndUnorderedAccessViews(kRt, rtv, &dsv, 0, nUav, omUav);
        c->OMGetBlendState(&bs, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&dss, &stencilRef);
        c->CSGetUnorderedAccessViews(0, nUav, csUav);
        c->SOGetTargets(kSo, so);
        c->GetPredication(&pred, &predValue);
    }
    void Restore(ID3D11DeviceContext* c, ID3D11DeviceContext1* c1) {
        UINT keep[kUav], soKeep[kSo];
        for (UINT& k : keep) k = UINT(-1);
        for (UINT& k : soKeep) k = UINT(-1);
        TZ_FS_RESTORE(VS, VS, ID3D11VertexShader)
        TZ_FS_RESTORE(HS, HS, ID3D11HullShader)
        TZ_FS_RESTORE(DS, DS, ID3D11DomainShader)
        TZ_FS_RESTORE(GS, GS, ID3D11GeometryShader)
        TZ_FS_RESTORE(PS, PS, ID3D11PixelShader)
        TZ_FS_RESTORE(CS, CS, ID3D11ComputeShader)
        c->IASetInputLayout(layout);
        c->IASetVertexBuffers(0, kVb, vb, vbStride, vbOffset);
        c->IASetIndexBuffer(ib, ibFormat, ibOffset);
        c->IASetPrimitiveTopology(topo);
        c->RSSetState(rs);
        c->RSSetViewports(nVp, vp);
        c->RSSetScissorRects(nSc, sc);
        UINT nr = 0, u0 = nUav, u1 = 0;
        for (UINT k = 0; k < kRt; ++k)
            if (rtv[k]) nr = k + 1;
        for (UINT k = 0; k < nUav; ++k)
            if (omUav[k]) u0 = k < u0 ? k : u0, u1 = k + 1;
        if (u1 > u0 && u0 >= nr)
            c->OMSetRenderTargetsAndUnorderedAccessViews(nr, rtv, dsv, u0, u1 - u0, omUav + u0, keep);
        else
            c->OMSetRenderTargets(kRt, rtv, dsv);
        c->OMSetBlendState(bs, blendFactor, sampleMask);
        c->OMSetDepthStencilState(dss, stencilRef);
        c->CSSetUnorderedAccessViews(0, nUav, csUav, keep);
        c->SOSetTargets(kSo, so, soKeep);
        c->SetPredication(pred, predValue);
        Release();
    }
#undef TZ_FS_SAVE
#undef TZ_FS_RESTORE
    void Release() {
        auto rel = [](IUnknown* u) {
            if (u) u->Release();
        };
        for (Stage& s : st) {
            rel(s.shader);
            for (UINT k = 0; k < s.nInst; ++k) rel(s.inst[k]);
            for (ID3D11Buffer* b : s.cb) rel(b);
            for (ID3D11ShaderResourceView* v : s.srv) rel(v);
            for (ID3D11SamplerState* p : s.samp) rel(p);
        }
        rel(layout);
        for (ID3D11Buffer* b : vb) rel(b);
        rel(ib), rel(rs), rel(dsv), rel(bs), rel(dss), rel(pred);
        for (ID3D11RenderTargetView* v : rtv) rel(v);
        for (UINT k = 0; k < nUav; ++k) rel(omUav[k]), rel(csUav[k]);
        for (ID3D11Buffer* b : so) rel(b);
    }
};
void UiLayerGunDraw() {
    g_probeMute = true;
    if (g_uiLayerGunFn)
        g_uiLayerGunFn(g_lastSc);
    else
        DrawLayer(g_lastSc, kPartGun);
    g_probeMute = false;
}
bool g_useGameBb = false;
uintptr_t g_pickGameTex = 1;
IDXGISwapChain* g_pickSc = nullptr;
bool g_pickLoggedFp = false;
int g_pickLogs = 0;
uintptr_t GameBbTexAddr() {
    if (!g_bbRouteOk) return 0;
    uintptr_t r = Ptr(g_base + g_ra.globalRenderer), rvt = r ? Ptr(r) : 0;
    uintptr_t t = rvt && Ptr(rvt + g_ra.vtGetBbSlot) == g_base + g_ra.fnGetBb ? Ptr(r + g_ra.oBackBufferObj) : 0;
    uintptr_t tex = t ? Ptr(t + kBbTexOff) : 0;
    return tex && InModule(Ptr(tex)) ? tex : 0;
}
ID3D11Texture2D* GameBbTex() {
    uintptr_t a = GameBbTexAddr();
    ID3D11Texture2D* t = nullptr;
    if (a && FAILED(reinterpret_cast<IUnknown*>(a)->QueryInterface(IID_ID3D11Texture2D, reinterpret_cast<void**>(&t)))) t = nullptr;
    return t;
}
bool LayerDevice(IDXGISwapChain* sc, ID3D11Device** dev) {
    *dev = nullptr;
    if (g_useGameBb) {
        if (ID3D11Texture2D* t = GameBbTex()) {
            t->GetDevice(dev);
            t->Release();
            if (*dev) return true;
        }
    }
    return SUCCEEDED(sc->GetDevice(IID_ID3D11Device, reinterpret_cast<void**>(dev))) && *dev;
}
bool LayerBackBuffer(IDXGISwapChain* sc, ID3D11Texture2D** bb) {
    *bb = nullptr;
    if (g_useGameBb && (*bb = GameBbTex())) return true;
    return SUCCEEDED(sc->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(bb))) && *bb;
}
void PickDescribeDevice(ID3D11Device* d, char* out, size_t n) {
    if (!d) {
        snprintf(out, n, "none");
        return;
    }
    char vt[96], cvt[96] = "?", name[128] = "?";
    unsigned long luid = 0;
    ProbeFmtAddr(reinterpret_cast<void*>(Ptr(reinterpret_cast<uintptr_t>(d))), vt, sizeof vt);
    ID3D11DeviceContext* c = nullptr;
    d->GetImmediateContext(&c);
    if (c) {
        ProbeFmtAddr(reinterpret_cast<void*>(Ptr(reinterpret_cast<uintptr_t>(c))), cvt, sizeof cvt);
        c->Release();
    }
    IDXGIDevice* xd = nullptr;
    if (SUCCEEDED(d->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&xd))) && xd) {
        IDXGIAdapter* ad = nullptr;
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(xd->GetAdapter(&ad)) && ad && SUCCEEDED(ad->GetDesc(&desc))) {
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name, nullptr, nullptr);
            luid = desc.AdapterLuid.LowPart;
        }
        if (ad) ad->Release();
        xd->Release();
    }
    snprintf(out, n, "%p (vtable %s, context vtable %s, adapter '%s' luid %08lx, flags 0x%x)", (void*)d, vt, cvt, name, luid, d->GetCreationFlags());
}
void PickDescribeTex(ID3D11Texture2D* t, char* out, size_t n) {
    if (!t) {
        snprintf(out, n, "none");
        return;
    }
    D3D11_TEXTURE2D_DESC d;
    t->GetDesc(&d);
    snprintf(out, n, "%p %ux%u fmt %u samples %u bind 0x%x misc 0x%x", (void*)t, d.Width, d.Height, d.Format, d.SampleDesc.Count, d.BindFlags,
             d.MiscFlags);
}
void UiLayerPickTarget(IDXGISwapChain* sc) {
    UiLayerReadIni();
    if (LayerNoCreateHere()) return;
    const bool fp = g_fp || g_testLayer;
    if (!fp) g_pickLoggedFp = false;
    const uintptr_t ga = g_uiTarget == 1 ? 0 : GameBbTexAddr();
    const bool enter = fp && !g_pickLoggedFp;
    if (ga == g_pickGameTex && sc == g_pickSc && !enter) return;
    g_pickGameTex = ga, g_pickSc = sc;
    if (fp) g_pickLoggedFp = true;
    ID3D11Device *sd = nullptr, *gd = nullptr;
    ID3D11Texture2D *sb = nullptr, *gt = nullptr;
    if (FAILED(sc->GetDevice(IID_ID3D11Device, reinterpret_cast<void**>(&sd)))) sd = nullptr;
    if (FAILED(sc->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&sb)))) sb = nullptr;
    if (ga && FAILED(reinterpret_cast<IUnknown*>(ga)->QueryInterface(IID_ID3D11Texture2D, reinterpret_cast<void**>(&gt)))) gt = nullptr;
    if (gt) gt->GetDevice(&gd);
    bool usable = false;
    if (gt && gd) {
        D3D11_TEXTURE2D_DESC d;
        gt->GetDesc(&d);
        bool hw = false;
        usable = d.SampleDesc.Count == 1 && (d.BindFlags & D3D11_BIND_RENDER_TARGET) && RtvFormat(d.Format, &hw) != DXGI_FORMAT_UNKNOWN;
    }
    const bool differs = gt && (gt != sb || gd != sd);
    const bool use = usable && (differs || g_uiTarget == 2);
    const char* why = g_uiTarget == 1 ? "the swapchain's ([ui_layer] target=swapchain)"
                      : use ? (differs ? "the game's own back buffer - the swapchain hands out a different device / texture (a layer wraps it); "
                                               "drawing there would not reach the screen"
                                             : "the game's own back buffer ([ui_layer] target=game; same as the swapchain's)")
                      : !gt ? "the swapchain's (the game's back buffer is not readable)"
                      : !usable ? "the swapchain's (the game's back buffer cannot take our render target view)"
                                  : "the swapchain's (same texture and device as the game's)";
    g_useGameBb = use;
    if (g_pickLogs < 16) {
        ++g_pickLogs;
        char vt[96], pr[96], sdd[400], sbd[160], gdd[400], gtd[160];
        ProbeFmtAddr(reinterpret_cast<void*>(g_scVtOrig), vt, sizeof vt);
        ProbeFmtAddr(reinterpret_cast<void*>(g_origPresent), pr, sizeof pr);
        PickDescribeDevice(sd, sdd, sizeof sdd), PickDescribeTex(sb, sbd, sizeof sbd);
        PickDescribeDevice(gd, gdd, sizeof gdd), PickDescribeTex(gt, gtd, sizeof gtd);
        g_log("ui-layer: draw target: %s%s | swapchain %p (vtable %s, Present %s): device %s, buffer %s | game's back buffer (renderer +0x%llx, +0x%llx): %s, "
              "device %s",
              why, fp ? " [first person]" : "", (void*)sc, vt, pr, sdd, sbd, static_cast<unsigned long long>(g_ra.oBackBufferObj),
              static_cast<unsigned long long>(kBbTexOff), gtd, gdd);
    }
    if (sd) sd->Release();
    if (sb) sb->Release();
    if (gd) gd->Release();
    if (gt) gt->Release();
}
struct LayerTarget {
    IDXGISwapChain* sc = nullptr;
    ID3D11Texture2D* gameObj = nullptr;
    IDXGISwapChain* scKey = nullptr;
    uintptr_t gameTex = 0;
    int target = 0;
    LONG gen = 0;
    bool useGame = false;
    bool cancel = false;
    bool singleThreaded = false;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Texture2D* bb = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11Texture2D* hudTex = nullptr;
    CRITICAL_SECTION* devCs = nullptr;
    D3D11_TEXTURE2D_DESC td = {};
    char fail[200] = "";
    double ms = 0;
    bool Ok() const { return !fail[0] && dev && ctx && bb && rtv; }
    void Release() {
        IUnknown* all[] = {hudTex, rtv, bb, ctx, dev, gameObj, sc};
        for (IUnknown* u : all)
            if (u) u->Release();
        hudTex = nullptr, rtv = nullptr, bb = nullptr, ctx = nullptr, dev = nullptr, gameObj = nullptr, sc = nullptr;
    }
};
struct LayerUse {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Texture2D* bb = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    CRITICAL_SECTION* cs = nullptr;
    D3D11_TEXTURE2D_DESC td = {};
    char fail[200] = "";
    void Release() {
        IUnknown* all[] = {rtv, bb, ctx, dev};
        for (IUnknown* u : all)
            if (u) u->Release();
        rtv = nullptr, bb = nullptr, ctx = nullptr, dev = nullptr;
    }
    ~LayerUse() { Release(); }
};
constexpr int kLtFreeMax = 64;
SRWLOCK g_ltLock = SRWLOCK_INIT;
HANDLE g_ltWake = nullptr, g_ltDone = nullptr, g_ltThread = nullptr;
LayerTarget* g_ltCur = nullptr;
LayerTarget* g_ltJob = nullptr;
LayerTarget* g_ltWorking = nullptr;
LayerTarget* g_ltMade = nullptr;
LayerTarget* g_ltFree[kLtFreeMax];
int g_ltFreeN = 0;
volatile LONG g_ltGen = 0;
volatile LONG g_ltDropping = 0;
double g_ltJobT0 = 0;
volatile bool g_ltToldStuck = false;
int g_ltStuckLogs = 0, g_ltReadyLogs = 0, g_ltDropLogs = 0;
UINT g_ltSkipFrames = 0;
double g_ltWaitedMs = 0;
bool g_ltFpSeen = false;
volatile LONG g_rtPhase = 0;
bool LayerOnRenderThread() { return g_presentTid && GetCurrentThreadId() == g_presentTid; }
bool LayerNoCreateHere() { return g_ltOn && (g_fp || g_testLayer) && LayerOnRenderThread(); }
CRITICAL_SECTION* LayerFindDeviceLock(ID3D11Device* dev) {
    static ID3D11Device* lastDev = nullptr;
    static CRITICAL_SECTION* lastCs = nullptr;
    if (dev == lastDev) return lastCs;
    lastDev = dev, lastCs = nullptr;
    ID3D11DeviceContext* c = nullptr;
    dev->GetImmediateContext(&c);
    ID3D11Multithread* mt = nullptr;
    if (c) c->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&mt));
    if (!mt) {
        if (c) c->Release();
        g_log("ui-layer: lock guard: no ID3D11Multithread on this device - guard off (lock-free creation still on)");
        return nullptr;
    }
    auto readable = [](uintptr_t p, size_t cap) -> size_t {
        MEMORY_BASIC_INFORMATION mbi;
        if (p < 0x10000 || (p & 7) || !VirtualQuery(reinterpret_cast<LPCVOID>(p), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READWRITE)))
            return 0;
        const size_t left = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize - p;
        return left < cap ? left : cap;
    };
    constexpr int kMaxBlocks = 512;
    uintptr_t blk[kMaxBlocks];
    size_t len[kMaxBlocks];
    int nb = 0;
    const uintptr_t roots[2] = {reinterpret_cast<uintptr_t>(dev), reinterpret_cast<uintptr_t>(c)};
    for (uintptr_t r : roots) {
        const size_t l = readable(r, 0x8000);
        if (!l || nb >= kMaxBlocks) continue;
        blk[nb] = r, len[nb++] = l;
        for (size_t off = 0; off + 8 <= l && off < 0x2000 && nb < kMaxBlocks; off += 8) {
            uintptr_t q = 0;
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(r + off), &q, 8, &got) || got != 8) continue;
            const size_t l2 = readable(q, 0x400);
            if (l2) blk[nb] = q, len[nb++] = l2;
        }
    }
    const DWORD me = GetCurrentThreadId();
    uintptr_t cand[8];
    int nc = 0;
    mt->Enter();
    for (int b = 0; b < nb; ++b)
        for (size_t off = 0; off + sizeof(CRITICAL_SECTION) <= len[b]; off += 8) {
            CRITICAL_SECTION cs;
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(blk[b] + off), &cs, sizeof cs, &got) || got != sizeof cs) continue;
            if (reinterpret_cast<uintptr_t>(cs.OwningThread) != me || cs.RecursionCount < 1 || cs.RecursionCount > 16) continue;
            bool dup = false;
            for (int k = 0; k < nc; ++k) dup |= cand[k] == blk[b] + off;
            if (!dup && nc < 8) cand[nc++] = blk[b] + off;
        }
    mt->Leave();
    int nok = 0;
    uintptr_t ok = 0;
    for (int k = 0; k < nc; ++k) {
        CRITICAL_SECTION cs;
        SIZE_T got = 0;
        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(cand[k]), &cs, sizeof cs, &got) && got == sizeof cs &&
            reinterpret_cast<uintptr_t>(cs.OwningThread) != me)
            ++nok, ok = cand[k];
    }
    mt->Release();
    c->Release();
    if (nok == 1) lastCs = reinterpret_cast<CRITICAL_SECTION*>(ok);
    g_log("ui-layer: lock guard: device lock %s (device %p; %d candidate(s) while held, %d released after Leave; %d blocks scanned)",
          nok == 1 ? "found" : "not identified - guard off for this device", (void*)dev, nc, nok, nb);
    return lastCs;
}
void LayerMake(LayerTarget* t) {
    ID3D11Device *sd = nullptr, *gd = nullptr;
    ID3D11Texture2D* sb = nullptr;
    IDXGISwapChain* chain = t->sc;
    if (FAILED(chain->GetDevice(IID_ID3D11Device, reinterpret_cast<void**>(&sd)))) sd = nullptr;
    if (FAILED(chain->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&sb)))) sb = nullptr;
    ID3D11Texture2D* gt = t->gameObj;
    if (gt) gt->GetDevice(&gd);
    bool usable = false;
    if (gt && gd) {
        D3D11_TEXTURE2D_DESC d;
        gt->GetDesc(&d);
        bool hw = false;
        usable = d.SampleDesc.Count == 1 && (d.BindFlags & D3D11_BIND_RENDER_TARGET) && RtvFormat(d.Format, &hw) != DXGI_FORMAT_UNKNOWN;
    }
    const bool differs = gt && (gt != sb || gd != sd);
    const bool use = t->target != 1 && usable && (differs || t->target == 2);
    const char* why = t->target == 1 ? "the swapchain's ([ui_layer] target=swapchain)"
                      : use ? (differs ? "the game's own back buffer - the swapchain hands out a different device / texture (a layer wraps it); "
                                               "drawing there would not reach the screen"
                                             : "the game's own back buffer ([ui_layer] target=game; same as the swapchain's)")
                      : !gt ? "the swapchain's (the game's back buffer is not readable)"
                      : !usable ? "the swapchain's (the game's back buffer cannot take our render target view)"
                                  : "the swapchain's (same texture and device as the game's)";
    ID3D11Device* dev = use ? gd : sd;
    ID3D11Texture2D* tex = use ? gt : sb;
    t->useGame = use;
    if (!dev || !tex) {
        snprintf(t->fail, sizeof t->fail, "the swapchain hands out no %s", !dev ? "D3D11 device" : "back buffer");
    } else if (dev->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) {
        t->singleThreaded = true;
        snprintf(t->fail, sizeof t->fail, "the device is single-threaded (creation flags 0x%x)", dev->GetCreationFlags());
    } else {
        tex->GetDesc(&t->td);
        bool hw = false;
        const DXGI_FORMAT rf = RtvFormat(t->td.Format, &hw);
        if (rf == DXGI_FORMAT_UNKNOWN || t->td.SampleDesc.Count != 1) {
            snprintf(t->fail, sizeof t->fail, "back buffer format %d samples %u not supported", t->td.Format, t->td.SampleDesc.Count);
        } else {
            D3D11_RENDER_TARGET_VIEW_DESC rd = {};
            rd.Format = rf;
            rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            HRESULT hr = dev->CreateRenderTargetView(tex, &rd, &t->rtv);
            if (FAILED(hr) || !t->rtv) {
                t->rtv = nullptr;
                snprintf(t->fail, sizeof t->fail, "CreateRenderTargetView on the back buffer failed (hr 0x%08lx)", hr);
            } else {
                dev->GetImmediateContext(&t->ctx);
                t->dev = dev, dev->AddRef();
                t->bb = tex, tex->AddRef();
                t->devCs = LayerFindDeviceLock(dev);
                D3D11_TEXTURE2D_DESC d = {};
                d.Width = t->td.Width, d.Height = t->td.Height, d.MipLevels = d.ArraySize = 1, d.Format = t->td.Format;
                d.SampleDesc.Count = 1, d.Usage = D3D11_USAGE_DEFAULT;
                if (FAILED(dev->CreateTexture2D(&d, nullptr, &t->hudTex))) t->hudTex = nullptr;
            }
        }
    }
    static uintptr_t lastTex = 1;
    static UINT lastW = 0, lastH = 0, lastF = 0;
    static bool lastUse = false;
    static char lastFail[200] = "\x01";
    static int logs = 0;
    const uintptr_t texAddr = reinterpret_cast<uintptr_t>(tex);
    const bool changed = texAddr != lastTex || t->td.Width != lastW || t->td.Height != lastH || static_cast<UINT>(t->td.Format) != lastF ||
                         use != lastUse || strcmp(t->fail, lastFail) != 0;
    if (changed && logs < 64) {
        ++logs;
        lastTex = texAddr, lastW = t->td.Width, lastH = t->td.Height, lastF = t->td.Format, lastUse = use;
        memcpy(lastFail, t->fail, sizeof lastFail);
        char vt[96], pr[96], sdd[400], sbd[160], gdd[400], gtd[160];
        ProbeFmtAddr(reinterpret_cast<void*>(g_scVtOrig), vt, sizeof vt);
        ProbeFmtAddr(reinterpret_cast<void*>(g_origPresent), pr, sizeof pr);
        PickDescribeDevice(sd, sdd, sizeof sdd), PickDescribeTex(sb, sbd, sizeof sbd);
        PickDescribeDevice(gd, gdd, sizeof gdd), PickDescribeTex(gt, gtd, sizeof gtd);
        g_log("ui-layer: draw target: %s [first person] | swapchain %p (vtable %s, Present %s): device %s, buffer %s | game's back buffer (renderer +0x%llx, "
              "+0x%llx): %s, device %s | made off the render thread%s%s",
              why, (void*)t->sc, vt, pr, sdd, sbd, static_cast<unsigned long long>(g_ra.oBackBufferObj), static_cast<unsigned long long>(kBbTexOff), gtd,
              gdd, t->fail[0] ? " - FAILED: " : "", t->fail);
    }
    if (sd) sd->Release();
    if (sb) sb->Release();
    if (gd) gd->Release();
    if (t->gameObj) t->gameObj->Release(), t->gameObj = nullptr;
    if (t->sc) t->sc->Release(), t->sc = nullptr;
}
DWORD WINAPI LayerMakerProc(LPVOID) {
    for (;;) {
        WaitForSingleObject(g_ltWake, 500);
        LayerTarget* drop[kLtFreeMax];
        int nd = 0;
        AcquireSRWLockExclusive(&g_ltLock);
        LayerTarget* job = g_ltJob;
        g_ltJob = nullptr, g_ltWorking = job;
        for (int i = 0; i < g_ltFreeN; ++i) drop[nd++] = g_ltFree[i];
        g_ltFreeN = 0;
        InterlockedExchange(&g_ltDropping, nd);
        ReleaseSRWLockExclusive(&g_ltLock);
        for (int i = 0; i < nd; ++i) {
            drop[i]->Release();
            delete drop[i];
        }
        InterlockedExchange(&g_ltDropping, 0);
        if (!job) continue;
        const double t0 = Now();
        LayerMake(job);
        job->ms = (Now() - t0) * 1000.0;
        const double waited = Now() - g_ltJobT0;
        AcquireSRWLockExclusive(&g_ltLock);
        const bool keep = !job->cancel && job->gen == g_ltGen;
        LayerTarget* old = nullptr;
        if (keep) old = g_ltMade, g_ltMade = job, g_ltWorking = nullptr;
        ReleaseSRWLockExclusive(&g_ltLock);
        if (!keep) {
            job->Release();
            delete job;
            AcquireSRWLockExclusive(&g_ltLock);
            g_ltWorking = nullptr;
            ReleaseSRWLockExclusive(&g_ltLock);
        }
        if (old) {
            old->Release();
            delete old;
        }
        if (g_ltToldStuck) {
            g_ltToldStuck = false;
            g_log("ui-layer: lock-free: the view maker got through after %.1f s (the D3D11 device lock was held elsewhere)", waited);
        }
        SetEvent(g_ltDone);
    }
}
bool LayerStart() {
    if (g_ltThread) return true;
    g_ltWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_ltDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g_ltWake && g_ltDone) g_ltThread = CreateThread(nullptr, 64 * 1024, LayerMakerProc, nullptr, 0, nullptr);
    if (!g_ltThread) {
        g_ltOn = false;
        g_log("ui-layer: lock-free: could not start the view maker thread - back to the old way");
        return false;
    }
    g_log("ui-layer: lock-free: view maker thread started");
    return true;
}
void LayerFreeLocked(LayerTarget* t) {
    if (!t) return;
    if (g_ltFreeN < kLtFreeMax) {
        g_ltFree[g_ltFreeN++] = t;
        return;
    }
    t->Release();
    delete t;
}
int LayerTargetFor(IDXGISwapChain* sc, bool mayWait, LayerUse* u) {
    if (!LayerStart()) return 2;
    ID3D11Texture2D* gt = g_uiTarget == 1 ? nullptr : GameBbTex();
    const uintptr_t ga = reinterpret_cast<uintptr_t>(gt);
    int r = 0;
    bool adopted = false, posted = false, dropped = false, postedNow = false;
    double jobAge = 0;
    for (int pass = 0; pass < 2; ++pass) {
        AcquireSRWLockExclusive(&g_ltLock);
        auto match = [&](const LayerTarget* t) { return t && t->scKey == sc && t->gameTex == ga && t->target == g_uiTarget && t->gen == g_ltGen; };
        if (g_ltMade) {
            if (match(g_ltMade)) {
                LayerFreeLocked(g_ltCur);
                g_ltCur = g_ltMade, g_ltMade = nullptr, adopted = true;
            } else {
                LayerFreeLocked(g_ltMade), g_ltMade = nullptr, dropped = true;
            }
        }
        if (g_ltCur && !match(g_ltCur)) LayerFreeLocked(g_ltCur), g_ltCur = nullptr, dropped = true;
        if (g_ltCur) {
            LayerTarget* t = g_ltCur;
            if (t->singleThreaded) {
                r = 2;
            } else if (!t->Ok()) {
                memcpy(u->fail, t->fail, sizeof u->fail);
                r = -1;
            } else {
                u->dev = t->dev, u->ctx = t->ctx, u->bb = t->bb, u->rtv = t->rtv, u->td = t->td, u->cs = t->devCs;
                u->dev->AddRef(), u->ctx->AddRef(), u->bb->AddRef(), u->rtv->AddRef();
                g_useGameBb = t->useGame;
                r = 1;
            }
        } else if (!g_ltJob && !g_ltWorking) {
            LayerTarget* j = new LayerTarget;
            j->sc = j->scKey = sc;
            sc->AddRef();
            j->gameObj = gt, gt = nullptr;
            j->gameTex = ga, j->target = g_uiTarget, j->gen = g_ltGen;
            g_ltJob = j;
            g_ltJobT0 = Now();
            ResetEvent(g_ltDone);
            posted = postedNow = true;
        } else {
            jobAge = Now() - g_ltJobT0;
        }
        ReleaseSRWLockExclusive(&g_ltLock);
        if (posted || dropped) SetEvent(g_ltWake);
        posted = dropped = false;
        if (r || !mayWait || !postedNow || pass) break;
        const double tw = Now();
        const bool done = WaitForSingleObject(g_ltDone, g_ltWaitMs) == WAIT_OBJECT_0;
        g_ltWaitedMs += (Now() - tw) * 1000.0;
        if (!done) break;
    }
    if (gt) gt->Release();
    if (r == 2) {
        g_ltOn = false;
        g_log("ui-layer: lock-free: the game's device is single-threaded - views created on the render thread again (the old way)");
        return 2;
    }
    if (adopted) {
        static UINT lastW = 0, lastH = 0, lastF = 0;
        static bool lastGame = false;
        AcquireSRWLockShared(&g_ltLock);
        const double ms = g_ltCur ? g_ltCur->ms : 0;
        const bool game = g_ltCur && g_ltCur->useGame;
        const D3D11_TEXTURE2D_DESC td = g_ltCur ? g_ltCur->td : D3D11_TEXTURE2D_DESC{};
        ReleaseSRWLockShared(&g_ltLock);
        const bool changed = td.Width != lastW || td.Height != lastH || static_cast<UINT>(td.Format) != lastF || game != lastGame;
        if (g_ltReadyLogs < 200 && (g_ltReadyLogs < 16 || changed || g_ltSkipFrames || ms > 5.0)) {
            ++g_ltReadyLogs;
            g_log("ui-layer: lock-free: back-buffer view ready (%s %ux%u fmt %u; made off the render thread in %.2f ms; first person waited %u frame(s), "
                  "%.1f ms)",
                  game ? "the game's own back buffer" : "the swapchain's buffer", td.Width, td.Height, static_cast<UINT>(td.Format), ms, g_ltSkipFrames,
                  g_ltWaitedMs);
        }
        lastW = td.Width, lastH = td.Height, lastF = td.Format, lastGame = game;
    }
    if (r == 1) g_ltFpSeen = true;
    if (r == 0) {
        ++g_ltSkipFrames;
        if (jobAge > 1.0 && !g_ltToldStuck && g_ltStuckLogs < 5) {
            g_ltToldStuck = true;
            ++g_ltStuckLogs;
            g_log("ui-layer: lock-free: the view maker has waited %.1f s - the D3D11 device lock is held elsewhere; first person draws nothing "
                  "meanwhile, the game keeps running", jobAge);
        }
    }
    return r;
}
void LayerTargetDrop() {
    g_ltSkipFrames = 0, g_ltWaitedMs = 0;
    const bool seen = g_ltFpSeen;
    g_ltFpSeen = false;
    if (!g_ltCur && !g_ltMade && !g_ltJob) return;
    AcquireSRWLockExclusive(&g_ltLock);
    LayerFreeLocked(g_ltCur), g_ltCur = nullptr;
    LayerFreeLocked(g_ltMade), g_ltMade = nullptr;
    LayerFreeLocked(g_ltJob), g_ltJob = nullptr;
    ReleaseSRWLockExclusive(&g_ltLock);
    if (g_ltWake) SetEvent(g_ltWake);
    if (seen && g_ltDropLogs < 8) {
        ++g_ltDropLogs;
        g_log("ui-layer: lock-free: first person off - our back-buffer view handed back to the view maker for release");
    }
}
ID3D11Texture2D* LayerHudCopy(const D3D11_TEXTURE2D_DESC& td) {
    if (!LayerNoCreateHere()) return nullptr;
    ID3D11Texture2D* t = nullptr;
    AcquireSRWLockShared(&g_ltLock);
    if (g_ltCur && g_ltCur->hudTex && g_ltCur->td.Width == td.Width && g_ltCur->td.Height == td.Height && g_ltCur->td.Format == td.Format)
        t = g_ltCur->hudTex, t->AddRef();
    ReleaseSRWLockShared(&g_ltLock);
    return t;
}
bool LayerTryLock(CRITICAL_SECTION* cs) {
    if (TryEnterCriticalSection(cs)) return true;
    const double until = Now() + g_ltGuardSpinUs * 1e-6;
    while (Now() < until) {
        YieldProcessor();
        if (TryEnterCriticalSection(cs)) return true;
    }
    return false;
}
struct LayerGuard {
    CRITICAL_SECTION* cs;
    bool held;
    explicit LayerGuard(CRITICAL_SECTION* c) : cs(g_ltGuardOn ? c : nullptr), held(!cs || LayerTryLock(cs)) {}
    ~LayerGuard() {
        if (cs && held) LeaveCriticalSection(cs);
    }
    bool Ok() const { return held; }
};
CRITICAL_SECTION* LayerCurrentLock() {
    if (!LayerNoCreateHere()) return nullptr;
    AcquireSRWLockShared(&g_ltLock);
    CRITICAL_SECTION* cs = g_ltCur ? g_ltCur->devCs : nullptr;
    ReleaseSRWLockShared(&g_ltLock);
    return cs;
}
UINT g_ltBusySkips = 0, g_ltBusyMid = 0;
double g_ltBusyT = 0;
int g_ltBusyLogs = 0;
void LayerGuardSkip(bool mid) {
    ++(mid ? g_ltBusyMid : g_ltBusySkips);
    const double now = Now();
    if (now - g_ltBusyT < 5.0) return;
    if (g_ltBusyLogs < 20)
        ++g_ltBusyLogs, g_log("ui-layer: lock guard: the D3D11 device lock was held elsewhere - our drawing skipped %u time(s) at Present, %u mid-frame "
                              "(last 5 s; the game never waited on us)",
                              g_ltBusySkips, g_ltBusyMid);
    g_ltBusyT = now, g_ltBusySkips = g_ltBusyMid = 0;
}
void LayerFlushForResize(const char* what, UINT w, UINT h) {
    if (!g_ltThread) return;
    LayerTarget* take[kLtFreeMax + 4];
    int n = 0;
    AcquireSRWLockExclusive(&g_ltLock);
    InterlockedIncrement(&g_ltGen);
    if (g_ltJob) take[n++] = g_ltJob, g_ltJob = nullptr;
    if (g_ltWorking) g_ltWorking->cancel = true;
    if (g_ltMade) take[n++] = g_ltMade, g_ltMade = nullptr;
    if (g_ltCur) take[n++] = g_ltCur, g_ltCur = nullptr;
    for (int i = 0; i < g_ltFreeN; ++i) take[n++] = g_ltFree[i];
    g_ltFreeN = 0;
    ReleaseSRWLockExclusive(&g_ltLock);
    const double t0 = Now();
    for (;;) {
        AcquireSRWLockShared(&g_ltLock);
        const bool busy = g_ltWorking != nullptr;
        ReleaseSRWLockShared(&g_ltLock);
        if ((!busy && !g_ltDropping) || Now() - t0 > 1.0) break;
        Sleep(1);
    }
    for (int i = 0; i < n; ++i) {
        take[i]->Release();
        delete take[i];
    }
    static int logs = 0;
    if (logs < 32) {
        ++logs;
        if (n)
            g_log("ui-layer: %s %ux%u (thread %lu) - our back-buffer view released first (%d held, waited %.1f ms for the view maker)", what, w, h,
                  GetCurrentThreadId(), n, (Now() - t0) * 1000.0);
        else
            g_log("ui-layer: %s %ux%u (thread %lu) - nothing of ours held, forwarded as is", what, w, h, GetCurrentThreadId());
    }
}
HRESULT STDMETHODCALLTYPE HkResizeBuffers(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl) {
    LayerFlushForResize("ResizeBuffers", w, h);
    return g_origResize(sc, n, w, h, f, fl);
}
HRESULT STDMETHODCALLTYPE HkResizeBuffers1(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT fl, const UINT* mask, IUnknown* const* q) {
    LayerFlushForResize("ResizeBuffers1", w, h);
    return g_origResize1(sc, n, w, h, f, fl, mask, q);
}
constexpr int kMidGen = 1;
wchar_t g_safePath[MAX_PATH] = L"";
const wchar_t* SafePath() {
    if (!g_safePath[0]) {
        wchar_t p[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"APPDATA", p, MAX_PATH);
        if (n && n < MAX_PATH - 80) swprintf(g_safePath, MAX_PATH, L"%ls\\The Creative Assembly\\Warhammer3\\logs\\tenzin_fps_safe_mode.txt", p);
    }
    return g_safePath;
}
bool SafeMarkerMid(char* why, size_t n) {
    why[0] = 0;
    const wchar_t* p = SafePath();
    if (!p[0]) return false;
    FILE* f = _wfopen(p, L"rb");
    if (!f) return false;
    char buf[1024];
    size_t got = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[got] = 0;
    int gen = -1;
    const char* g = strstr(buf, "gen=");
    if (g) gen = atoi(g + 4);
    if (!strstr(buf, "mid_frame_off=1") || gen != kMidGen) return false;
    const char* w = strstr(buf, "when=");
    const char* where = strstr(buf, "where=");
    char a[40] = "?", b[160] = "?";
    if (w) sscanf(w + 5, "%39[^\r\n]", a);
    if (where) sscanf(where + 6, "%159[^\r\n]", b);
    snprintf(why, n, "%s, %s", a, b);
    return true;
}
void SafeMarkerWrite(const char* where) {
    g_uiLayerMode = 0;
    const wchar_t* p = SafePath();
    bool ok = false;
    if (p[0]) {
        if (FILE* f = _wfopen(p, L"wb")) {
            SYSTEMTIME t;
            GetLocalTime(&t);
            fprintf(f, "mid_frame_off=1\r\ngen=%d\r\nwhen=%04u-%02u-%02u %02u:%02u:%02u\r\nwhere=%s\r\n", kMidGen, t.wYear, t.wMonth, t.wDay, t.wHour,
                    t.wMinute, t.wSecond, where);
            ok = fclose(f) == 0;
        }
    }
    g_log("watchdog: safe mode: mid-frame gun drawing turned off on this PC%s%ls%s - from now on the gun is drawn at Present with the interface "
          "kept on top; [ui_layer] mode=1 forces it back",
          ok ? " (marker " : " (marker not written", ok ? p : L"", ")");
}
FullState g_presentFs;
void (*g_uiPresentDrawFn)(IDXGISwapChain*, int) = nullptr;
bool g_uiPresentLoggedFp = false;
int g_uiPresentLogs = 0;
void FullStateDescribe(const FullState& fs, ID3D11Resource* bb, char* out, size_t n) {
    UINT nRt = 0, omU = 0, csU = 0, so = 0, srv = 0;
    bool firstIsBb = false;
    for (UINT k = 0; k < FullState::kRt; ++k)
        if (fs.rtv[k]) ++nRt;
    if (fs.rtv[0] && bb) {
        ID3D11Resource* r = nullptr;
        fs.rtv[0]->GetResource(&r);
        firstIsBb = r == bb;
        if (r) r->Release();
    }
    for (UINT k = 0; k < fs.nUav; ++k) omU += fs.omUav[k] != nullptr, csU += fs.csUav[k] != nullptr;
    for (ID3D11Buffer* b : fs.so) so += b != nullptr;
    char sh[40] = "";
    const char* names[FullState::kStages] = {"VS ", "HS ", "DS ", "GS ", "PS ", "CS "};
    for (int i = 0; i < FullState::kStages; ++i) {
        for (ID3D11ShaderResourceView* v : fs.st[i].srv) srv += v != nullptr;
        if (fs.st[i].shader) strcat(sh, names[i]);
    }
    D3D11_RASTERIZER_DESC rd = {};
    if (fs.rs) fs.rs->GetDesc(&rd);
    snprintf(out, n,
             "predicate %p (value %d), stream-out targets %u, output-merger UAVs %u, compute UAVs %u, %u render targets (first %s the back buffer) + depth %s, "
             "shaders bound: %s, rasterizer %s (scissor %d, cull %d, fill %d, depth clip %d), %u viewports, %u scissor rects, %u shader resources, "
             "constant-buffer ranges %s",
             (void*)fs.pred, (int)fs.predValue, so, omU, csU, nRt, firstIsBb ? "is" : "is not", fs.dsv ? "yes" : "no", sh[0] ? sh : "none ",
             fs.rs ? "set" : "default", (int)rd.ScissorEnable, (int)rd.CullMode, (int)rd.FillMode, (int)rd.DepthClipEnable, fs.nVp, fs.nSc, srv,
             fs.ranges ? "in use" : "default");
}
void UiLayerPresentDraw(IDXGISwapChain* sc, int part) {
    UiLayerReadIni();
    const bool on = g_fp || g_testLayer;
    if (!on) g_uiPresentLoggedFp = false;
    ID3D11DeviceContext* c = nullptr;
    LayerUse lu;
    const int lr = on && !g_failed && LayerNoCreateHere() ? LayerTargetFor(sc, true, &lu) : 2;
    LayerGuard guard(lr == 1 ? lu.cs : nullptr);
    if (!guard.Ok()) {
        LayerGuardSkip(false);
        return;
    }
    if (on && !g_uiPresentPartial && !g_failed) {
        if (lr == 1) {
            c = lu.ctx, c->AddRef();
        } else if (lr == 2) {
            ID3D11Device* dev = nullptr;
            if (LayerDevice(sc, &dev)) {
                dev->GetImmediateContext(&c);
                dev->Release();
            }
        }
    }
    if (!c) {
        if (g_uiPresentDrawFn)
            g_uiPresentDrawFn(sc, part);
        else
            DrawLayer(sc, part);
        lu.Release();
        return;
    }
    ID3D11DeviceContext1* c1 = nullptr;
    if (FAILED(c->QueryInterface(IID_ID3D11DeviceContext1, reinterpret_cast<void**>(&c1)))) c1 = nullptr;
    g_presentFs.Save(c, c1);
    if (!g_uiPresentLoggedFp && g_uiPresentLogs < 12) {
        g_uiPresentLoggedFp = true;
        ++g_uiPresentLogs;
        ID3D11Texture2D* bb = nullptr;
        if (lr == 1)
            bb = lu.bb, bb->AddRef();
        else
            LayerBackBuffer(sc, &bb);
        char what[900];
        FullStateDescribe(g_presentFs, bb, what, sizeof what);
        if (bb) bb->Release();
        g_log("ui-layer: at Present (part %d) the game left: %s - drawing from a cleared state, then put back", part, what);
    }
    c->ClearState();
    if (g_uiPresentDrawFn)
        g_uiPresentDrawFn(sc, part);
    else
        DrawLayer(sc, part);
    c->ClearState();
    g_presentFs.Restore(c, c1);
    if (c1) c1->Release();
    c->Release();
    lu.Release();
}
volatile LONG g_wdBeat = 0;
DWORD g_wdRenderTid = 0, g_wdMainTid = 0;
HANDLE g_wdThread = nullptr;
int g_wdReports = 0;
constexpr size_t kWdStackBytes = 64 * 1024;
constexpr int kWdMaxReports = 3, kWdMaxFrames = 40;
unsigned char g_wdStack[kWdStackBytes];
struct WdShot {
    bool ok;
    DWORD64 rip, rsp;
    size_t got;
    DWORD64 reg[15];
};
bool WdCapture(DWORD tid, WdShot* s) {
    s->ok = false, s->got = 0, s->rip = s->rsp = 0;
    if (!tid || tid == GetCurrentThreadId()) return false;
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!h) return false;
    if (SuspendThread(h) == static_cast<DWORD>(-1)) {
        CloseHandle(h);
        return false;
    }
    CONTEXT ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (GetThreadContext(h, &ctx)) {
        s->ok = true, s->rip = ctx.Rip, s->rsp = ctx.Rsp;
        const DWORD64 regs[15] = {ctx.Rax, ctx.Rcx, ctx.Rdx, ctx.Rbx, ctx.Rbp, ctx.Rsi, ctx.Rdi, ctx.R8, ctx.R9, ctx.R10, ctx.R11, ctx.R12, ctx.R13, ctx.R14, ctx.R15};
        memcpy(s->reg, regs, sizeof regs);
        size_t want = kWdStackBytes;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<LPCVOID>(ctx.Rsp), &mbi, sizeof mbi)) {
            const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (end > ctx.Rsp && end - ctx.Rsp < want) want = end - ctx.Rsp;
        }
        SIZE_T got = 0;
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(ctx.Rsp), g_wdStack, want, &got);
        s->got = got;
    }
    ResumeThread(h);
    CloseHandle(h);
    return s->ok;
}
bool WdIsReturnAddr(uintptr_t a) {
    if (a < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(a - 1), &mbi, sizeof mbi)) return false;
    if (mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE || !(mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
        (mbi.Protect & PAGE_GUARD))
        return false;
    const uintptr_t lo = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    auto at = [&](uintptr_t k) -> int { return a - k >= lo ? *reinterpret_cast<const unsigned char*>(a - k) : -1; };
    auto callModrm = [](int m) { return m >= 0 && (m & 0x38) == 0x10; };
    return at(5) == 0xE8 || (at(6) == 0xFF && callModrm(at(5))) || (at(3) == 0xFF && callModrm(at(2))) ||
           (at(2) == 0xFF && at(1) >= 0xD0 && at(1) <= 0xD7) || (at(7) == 0xFF && callModrm(at(6))) || (at(4) == 0xFF && callModrm(at(3)));
}
void WdFindLock(const WdShot& s, DWORD tid);
void WdReport(const char* label, DWORD tid, bool findLock = false) {
    WdShot s;
    if (!WdCapture(tid, &s)) {
        g_log("watchdog: %s thread %lu - could not capture it", label, tid);
        return;
    }
    char rip[96], frames[4000] = "";
    ProbeFmtAddr(reinterpret_cast<void*>(s.rip), rip, sizeof rip);
    size_t used = 0;
    int n = 0;
    uintptr_t last = 0;
    for (size_t off = 0; off + 8 <= s.got && n < kWdMaxFrames; off += 8) {
        uintptr_t v;
        memcpy(&v, g_wdStack + off, 8);
        if (v == last || !WdIsReturnAddr(v)) continue;
        last = v;
        char one[96];
        ProbeFmtAddr(reinterpret_cast<void*>(v), one, sizeof one);
        int w = snprintf(frames + used, sizeof frames - used, "%s%s", n ? " < " : "", one);
        if (w < 0 || used + w >= sizeof frames) break;
        used += w;
        ++n;
    }
    g_log("watchdog: %s thread %lu at %s (rsp %p, %zu stack bytes read); return addresses on the stack: %s", label, tid, rip,
          reinterpret_cast<void*>(s.rsp), s.got, n ? frames : "none found");
    if (findLock) WdFindLock(s, tid);
}
void WdFindLock(const WdShot& s, DWORD tid) {
    uintptr_t cand[15 + 512];
    int nc = 0;
    for (DWORD64 r : s.reg) cand[nc++] = static_cast<uintptr_t>(r);
    for (size_t off = 0; off + 8 <= s.got && off < 4096; off += 8) memcpy(&cand[nc++], g_wdStack + off, 8);
    struct Hit {
        uintptr_t cs;
        DWORD owner;
        LONG rec, lock;
    } hit[4];
    int nh = 0;
    for (int i = 0; i < nc && nh < 4; ++i) {
        const uintptr_t v = cand[i];
        if (v < 0x10000 || (v & 7)) continue;
        bool dup = false;
        for (int k = 0; k < nh; ++k) dup |= hit[k].cs == v;
        if (dup) continue;
        CRITICAL_SECTION cs;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(v), &cs, sizeof cs, &got) || got != sizeof cs) continue;
        const uintptr_t ow = reinterpret_cast<uintptr_t>(cs.OwningThread);
        if (!ow || ow > 0xffffffffu || ow == tid || cs.RecursionCount < 1 || cs.RecursionCount > 256 || (cs.LockCount & 1)) continue;
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(ow));
        const bool ours = h && GetProcessIdOfThread(h) == GetCurrentProcessId();
        if (h) CloseHandle(h);
        if (!ours) continue;
        hit[nh++] = {v, static_cast<DWORD>(ow), cs.RecursionCount, cs.LockCount};
    }
    if (!nh) {
        g_log("watchdog: lock: no critical section with an owner found around thread %lu's wait", tid);
        return;
    }
    for (int k = 0; k < nh; ++k)
        g_log("watchdog: lock: thread %lu is near critical section %p owned by thread %lu (recursion %ld, lock count 0x%lx)%s", tid, (void*)hit[k].cs,
              hit[k].owner, hit[k].rec, static_cast<unsigned long>(hit[k].lock),
              hit[k].owner == g_wdMainTid ? " = the main thread" : hit[k].owner == GetCurrentThreadId() ? " = the watchdog" : "");
    int told = 0;
    for (int k = 0; k < nh && told < 2; ++k) {
        bool seen = hit[k].owner == g_wdMainTid || hit[k].owner == GetCurrentThreadId();
        for (int j = 0; j < k; ++j) seen |= hit[j].owner == hit[k].owner;
        if (seen) continue;
        ++told;
        WdReport("lock owner", hit[k].owner);
    }
}
void WdThreads() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    struct One {
        DWORD tid;
        DWORD64 rip;
    } th[128];
    int n = 0, total = 0;
    THREADENTRY32 te;
    te.dwSize = sizeof te;
    const DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        ++total;
        if (n >= 128) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
        if (!h) continue;
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL;
        if (SuspendThread(h) != static_cast<DWORD>(-1)) {
            if (GetThreadContext(h, &c)) th[n++] = {te.th32ThreadID, c.Rip};
            ResumeThread(h);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    char out[6000];
    size_t used = 0;
    for (int i = 0; i < n; ++i) {
        char a[96];
        ProbeFmtAddr(reinterpret_cast<void*>(th[i].rip), a, sizeof a);
        int w = snprintf(out + used, sizeof out - used, "%s%lu %s", i ? " | " : "", th[i].tid, a);
        if (w < 0 || used + w >= sizeof out) break;
        used += w;
    }
    g_log("watchdog: threads (%d of %d): %s", n, total, n ? out : "none captured");
}
void WdStep(double now) {
    static LONG lastBeat = -1;
    static double lastChange = 0;
    static bool armed = true;
    const LONG b = g_wdBeat;
    if (!(g_fp || g_testLayer) || b != lastBeat) {
        if (b != lastBeat && !armed) g_log("watchdog: Present is running again after %.1f s", now - lastChange);
        lastBeat = b, lastChange = now, armed = true;
        return;
    }
    if (!armed || g_wdReports >= kWdMaxReports || now - lastChange < g_wdMs / 1000.0) return;
    armed = false;
    ++g_wdReports;
    g_log("watchdog: no Present for %.1f s in first person (report %d of %d) - render thread %lu, main thread %lu", now - lastChange, g_wdReports,
          kWdMaxReports, g_wdRenderTid, g_wdMainTid);
    const LONG ph = g_rtPhase;
    const bool mid = g_midDone;
    const char* where = ph == 1 ? "inside our mid-frame gun drawing"
                        : ph == 2 ? "inside our gun drawing at Present"
                        : ph == 3 ? "inside the real Present (our drawing for this frame was done)"
                        : mid ? "in the game's own code, after our mid-frame gun drawing this frame"
                                  : "in the game's own code (none of our drawing in progress)";
    g_log("watchdog: where: the render thread is %s (phase %ld, mid-frame drawing %s, lock-free %s)", where, ph, g_uiLayerMode == 1 ? "on" : "off",
          g_ltOn ? "on" : "off");
    if (g_autoSafe && g_uiLayerMode == 1 && (ph == 1 || (ph == 0 && mid))) SafeMarkerWrite(where);
    WdReport("render", g_wdRenderTid, true);
    if (g_wdMainTid && g_wdMainTid != g_wdRenderTid) WdReport("main", g_wdMainTid);
    WdThreads();
}
DWORD WINAPI WdThreadProc(LPVOID) {
    for (;;) {
        Sleep(200);
        WdStep(Now());
    }
}
void WdPresentBeat(IDXGISwapChain* sc) {
    InterlockedIncrement(&g_wdBeat);
    if (g_wdThread) return;
    UiLayerReadIni();
    if (!g_wdOn) return;
    g_wdRenderTid = GetCurrentThreadId();
    DXGI_SWAP_CHAIN_DESC d;
    if (SUCCEEDED(sc->GetDesc(&d)) && d.OutputWindow) g_wdMainTid = GetWindowThreadProcessId(d.OutputWindow, nullptr);
    g_wdThread = CreateThread(nullptr, 64 * 1024, WdThreadProc, nullptr, 0, nullptr);
    g_log("watchdog: %s (Present gap > %u ms in first person -> stacks logged; render thread %lu, main thread %lu)",
          g_wdThread ? "on" : "could not start its thread", g_wdMs, g_wdRenderTid, g_wdMainTid);
    if (!g_wdThread) g_wdOn = false;
}
bool UiLayerMidDraw(ID3D11DeviceContext* c) {
    if (GetCurrentThreadId() != g_presentTid) {
        ++g_uls.skipThread;
        if (!g_uls.loggedThread) {
            g_uls.loggedThread = true;
            g_log("ui-layer: mid-frame hook on thread %lu, Present runs on %lu; not drawing there", GetCurrentThreadId(), g_presentTid);
        }
        return false;
    }
    LayerGuard guard(LayerCurrentLock());
    if (!guard.Ok()) {
        LayerGuardSkip(true);
        return false;
    }
    InterlockedExchange(&g_rtPhase, 1);
    if (g_uiStateSwap) {
        if (!UiLayerStateReady(c)) {
            ++g_uls.skipState;
            if (!g_uls.loggedState) {
                g_uls.loggedState = true;
                g_log("ui-layer: no ID3D11DeviceContext1 state object; gun stays at Present");
            }
            InterlockedExchange(&g_rtPhase, 0);
            return false;
        }
        ID3DDeviceContextState* prev = nullptr;
        g_ctx1->SwapDeviceContextState(g_ourState, &prev);
        UiLayerGunDraw();
        g_ctx1->SwapDeviceContextState(prev, nullptr);
        if (prev) prev->Release();
    } else {
        static FullState fs;
        ID3D11DeviceContext1* c1 = nullptr;
        if (FAILED(c->QueryInterface(IID_ID3D11DeviceContext1, reinterpret_cast<void**>(&c1)))) c1 = nullptr;
        fs.Save(c, c1);
        c->ClearState();
        UiLayerGunDraw();
        c->ClearState();
        fs.Restore(c, c1);
        if (!g_uiLoggedManual) {
            g_uiLoggedManual = true;
            g_log("ui-layer: mid-frame gun drawn with a manual full pipeline save / restore (%u UAV slots, constant-buffer ranges %s) - no SwapDeviceContextState",
                  fs.nUav, !c1 ? "not readable (no ID3D11DeviceContext1)" : fs.ranges ? "in use, restored with *ConstantBuffers1" : "default");
        }
        if (c1) c1->Release();
    }
    InterlockedExchange(&g_rtPhase, 0);
    g_uiMidT = Now();
    g_midDone = true;
    ++g_uls.mid;
    return true;
}
void UiLayerAfterCopy(ID3D11DeviceContext* c, ID3D11Resource* dst, void* ret) {
    if (g_probeMute || g_uiLayerMode != 1 || g_midDone || !g_lastSc || !g_probeBb) return;
    if (reinterpret_cast<uintptr_t>(dst) != g_probeBb || !InExe(ret)) return;
    if (g_failed || !(g_fp || g_testLayer)) return;
    if (!UiLayerMidDraw(c)) return;
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
bool CompRouteVerify(uintptr_t e) {
    if (!g_ra.vtComposite || !g_ra.fnCompositeExec || !g_ra.globalGfx || !g_ra.insFrameGraphLoad || !g_ra.insPassListLea || !g_ra.insPassListRead)
        return false;
    static const unsigned char kHead[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24,
                                          0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x05};
    static const unsigned char kRead[] = {0x8B, 0x4E, 0x04, 0x48, 0x8B, 0x5E, 0x08};
    static const unsigned char kAl1[] = {0xB0, 0x01, 0xC3};
    unsigned char aa[] = {0x48, 0x8B, 0xF1, 0x8B, 0x90, 0, 0, 0, 0, 0xFF, 0xCA, 0x83, 0xFA, 0x02, 0x0F, 0x87};
    unsigned char fg[] = {0x48, 0x8B, 0x8B, 0, 0, 0, 0, 0xE8};
    unsigned char pl[] = {0x4C, 0x8D, 0x8F, 0, 0, 0, 0};
    uint32_t off = static_cast<uint32_t>(g_ra.oAaSetting);
    memcpy(aa + 5, &off, 4);
    off = static_cast<uint32_t>(g_ra.oFrameGraph);
    memcpy(fg + 3, &off, 4);
    off = static_cast<uint32_t>(g_ra.oPassList);
    memcpy(pl + 3, &off, 4);
    const uintptr_t f = e + g_ra.fnCompositeExec, slot10 = Ptr(e + g_ra.vtComposite + 0x10);
    int32_t disp = 0;
    return Ptr(e + g_ra.vtComposite + kCompExecSlot) == f && BytesMatch(f, kHead, sizeof kHead) && Rd(f + sizeof kHead, disp) &&
           f + sizeof kHead + 4 + static_cast<intptr_t>(disp) == e + g_ra.globalGfx && BytesMatch(f + sizeof kHead + 4, aa, sizeof aa) &&
           slot10 && BytesMatch(slot10, kAl1, sizeof kAl1) && BytesMatch(e + g_ra.insFrameGraphLoad, fg, sizeof fg) &&
           BytesMatch(e + g_ra.insPassListLea, pl, sizeof pl) && BytesMatch(e + g_ra.insPassListRead, kRead, sizeof kRead);
}
void UiLayerAfterComposite(void* ret) {
    if (g_probeMute || g_uiLayerMode != 1 || g_midDone || !g_lastSc || !g_probeBb || !g_layerCtx) return;
    if (!InExe(ret) || g_failed || !(g_fp || g_testLayer)) return;
    uintptr_t gfx = Ptr(g_base + g_ra.globalGfx);
    int aa = 0;
    if (!gfx || !Rd(gfx + g_ra.oAaSetting, aa)) return;
    if (aa < 1 || aa > 3) {
        ++g_uls.skipAa;
        if (!g_uls.loggedAa) {
            g_uls.loggedAa = true;
            g_log("ui-layer: anti-aliasing setting %d - the game's composite step does nothing this frame; the frame view draws the scene (its UI hook draws the gun)", aa);
        }
        return;
    }
    ID3D11RenderTargetView* rtv = nullptr;
    g_layerCtx->OMGetRenderTargets(1, &rtv, nullptr);
    ID3D11Resource* res = nullptr;
    if (rtv) rtv->GetResource(&res);
    const bool onBb = res && reinterpret_cast<uintptr_t>(res) == g_probeBb;
    if (!onBb && !g_uls.loggedRt) {
        UINT w = 0, h = 0, fmt = 0;
        ProbeTexInfo(res, &w, &h, &fmt);
        g_uls.loggedRt = true;
        g_log("ui-layer: after the composite step the bound render target is %p (%ux%u fmt %u), not the back buffer %p; gun stays at Present",
              (void*)res, w, h, fmt, (void*)g_probeBb);
    }
    if (res) res->Release();
    if (rtv) rtv->Release();
    if (!onBb) {
        ++g_uls.skipRt;
        return;
    }
    if (!UiLayerMidDraw(g_layerCtx)) return;
    ++g_uls.midComp;
    if (!g_uls.loggedComp) {
        char who[96];
        ProbeFmtAddr(ret, who, sizeof who);
        g_uls.loggedComp = true;
        g_log("ui-layer: gun drawn right after the game's composite step drew the scene into the back buffer (no upscaler copy; caller %s, "
              "anti-aliasing %d); UI now draws on top", who, aa);
    }
}
__attribute__((noinline)) uintptr_t HkCompExec(void* self, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
    uintptr_t r = g_compExecOrig(self, a2, a3, a4);
    UiLayerAfterComposite(__builtin_return_address(0));
    return r;
}
void CompUnhook(const char* why, bool write = true) {
    if (!g_compObj) return;
    if (write && Ptr(g_compObj) == reinterpret_cast<uintptr_t>(g_compVtCopy)) {
        uintptr_t orig = g_base + g_ra.vtComposite;
        SIZE_T w = 0;
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(g_compObj), &orig, sizeof orig, &w);
        g_log("ui-layer: composite step %p vtable restored (%s)", (void*)g_compObj, why);
    } else {
        g_log("ui-layer: composite step %p released without writing it (%s)", (void*)g_compObj, why);
    }
    g_compObj = 0;
}
void UiLayerCompStep() {
    UiLayerReadIni();
    bool want = g_uiLayerMode == 1 && g_compRouteOk && (g_fp || g_testLayer) && !g_failed;
    if (!want) {
        CompUnhook(!(g_fp || g_testLayer) ? "first person off" : "composite route off");
        return;
    }
    uintptr_t gfx = Ptr(g_base + g_ra.globalGfx), fg = gfx ? Ptr(gfx + g_ra.oFrameGraph) : 0, arr = 0;
    uint32_t n = 0;
    if (!fg || !Rd(fg + g_ra.oPassList + 4, n) || !n || n > 1024 || !(arr = Ptr(fg + g_ra.oPassList + 8))) {
        if (!g_compLoggedList) {
            g_compLoggedList = true;
            g_log("ui-layer: render pass list not found (graphics %p frame graph %p count %u array %p); without an upscaler the gun stays at Present this frame",
                  (void*)gfx, (void*)fg, n, (void*)arr);
        }
        return;
    }
    const uintptr_t vtGame = g_base + g_ra.vtComposite;
    uintptr_t ours = reinterpret_cast<uintptr_t>(g_compVtCopy), obj = 0;
    int found = 0, at = -1;
    for (uint32_t i = 0; i < n; ++i) {
        uintptr_t p = Ptr(arr + 8 * i), v = p ? Ptr(p) : 0;
        if (v && (v == vtGame || (ours && v == ours))) {
            if (!obj) obj = p, at = static_cast<int>(i);
            ++found;
        }
    }
    if (!obj) {
        if (!g_compLoggedMissing) {
            g_compLoggedMissing = true;
            g_log("ui-layer: composite step (vtable e+%llx) not in the %u-entry render pass list; without an upscaler the gun stays at Present",
                  static_cast<unsigned long long>(g_ra.vtComposite), n);
        }
        CompUnhook("composite step gone from the pass list", false);
        return;
    }
    if (obj == g_compObj && Ptr(obj) == ours) return;
    if (g_compObj && g_compObj != obj) CompUnhook("composite step object changed");
    if (Ptr(obj) != ours) {
        if (!g_compVtCopy) {
            constexpr int kMax = 64;
            int k = 0;
            for (; k < kMax; ++k) {
                uintptr_t v = 0;
                if (!Rd(vtGame + 8 * k, v) || !InExe(reinterpret_cast<void*>(v))) break;
            }
            if (k <= static_cast<int>(kCompExecSlot / 8)) return;
            auto* copy = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (k + 2) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!copy) return;
            copy[0] = Ptr(vtGame - 16);
            copy[1] = Ptr(vtGame - 8);
            for (int i = 0; i < k; ++i) copy[2 + i] = Ptr(vtGame + 8 * i);
            g_compExecOrig = reinterpret_cast<FnCompExec>(copy[2 + kCompExecSlot / 8]);
            copy[2 + kCompExecSlot / 8] = reinterpret_cast<uintptr_t>(&HkCompExec);
            DWORD old;
            VirtualProtect(copy, (k + 2) * sizeof(uintptr_t), PAGE_READONLY, &old);
            g_compVtCopy = copy + 2, ours = reinterpret_cast<uintptr_t>(g_compVtCopy);
            g_log("ui-layer: composite step vtable e+%llx copied (%d slots), slot +0x%llx -> our hook", static_cast<unsigned long long>(g_ra.vtComposite), k,
                  static_cast<unsigned long long>(kCompExecSlot));
        }
        SIZE_T w = 0;
        if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(obj), &ours, sizeof ours, &w) || w != sizeof ours) {
            g_log("ui-layer: vptr write failed on composite step %p", (void*)obj);
            return;
        }
    }
    g_compObj = obj;
    g_log("ui-layer: hooked the game's composite step %p (entry %d of %u, %d with that vtable); D3D context untouched", (void*)obj, at, n, found);
}
bool ViewRouteVerify(uintptr_t e) {
    if (!g_ra.vtView || !g_ra.vtViewSub || !g_ra.fnViewDraw || !g_ra.fnViewUi || !g_ra.insViewTailUi || !g_ra.insHelperCallDraw) return false;
    static const unsigned char kTail[] = {0x48, 0x8B, 0x06, 0x49, 0x8B, 0xD6, 0x48, 0x8B, 0xCE, 0x48, 0x8B, 0x5C, 0x24, 0x68, 0x48, 0x8B, 0x6C, 0x24, 0x70, 0x48,
                                          0x83, 0xC4, 0x30, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5C, 0x5F, 0x5E, 0x48, 0xFF, 0xA0, 0x90, 0x00, 0x00, 0x00, 0xCC};
    static const unsigned char kUiHead[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x99, 0xE0, 0x00,
                                            0x00, 0x00, 0x48, 0x8B, 0xF2};
    static const unsigned char kHelper[] = {0x48, 0x8B, 0x0B, 0x48, 0x8B, 0x53, 0x08, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xF8, 0x00, 0x00, 0x00};
    static const unsigned char kThunk[] = {0x48, 0x83, 0xE9, 0x18, 0xE9};
    const uintptr_t thunk = Ptr(e + g_ra.vtViewSub);
    return Ptr(e + g_ra.vtView + 0xf8) == e + g_ra.fnViewDraw && Ptr(e + g_ra.vtView + kViewUiSlot) == e + g_ra.fnViewUi &&
           BytesMatch(e + g_ra.insViewTailUi, kTail, sizeof kTail) && g_ra.insViewTailUi > g_ra.fnViewDraw &&
           g_ra.insViewTailUi < g_ra.fnViewDraw + 0x400 && BytesMatch(e + g_ra.fnViewUi, kUiHead, sizeof kUiHead) &&
           BytesMatch(e + g_ra.insHelperCallDraw, kHelper, sizeof kHelper) && thunk && BytesMatch(thunk, kThunk, sizeof kThunk);
}
void UiLayerBeforeViewUi(void* ret) {
    if (g_probeMute || g_uiLayerMode != 1 || g_midDone || !g_lastSc || !g_probeBb || !g_layerCtx) return;
    if (!InExe(ret) || g_failed || !(g_fp || g_testLayer)) return;
    ID3D11RenderTargetView* rtv = nullptr;
    g_layerCtx->OMGetRenderTargets(1, &rtv, nullptr);
    ID3D11Resource* res = nullptr;
    if (rtv) rtv->GetResource(&res);
    const bool onBb = res && reinterpret_cast<uintptr_t>(res) == g_probeBb;
    if (!onBb && !g_uls.loggedViewRt) {
        UINT w = 0, h = 0, fmt = 0;
        ProbeTexInfo(res, &w, &h, &fmt);
        g_uls.loggedViewRt = true;
        g_log("ui-layer: when the frame view starts its UI the bound render target is %p (%ux%u fmt %u), not the back buffer %p; gun stays at Present",
              (void*)res, w, h, fmt, (void*)g_probeBb);
    }
    if (res) res->Release();
    if (rtv) rtv->Release();
    if (!onBb) {
        ++g_uls.skipViewRt;
        return;
    }
    if (!UiLayerMidDraw(g_layerCtx)) return;
    ++g_uls.midView;
    if (!g_uls.loggedView) {
        char who[96];
        ProbeFmtAddr(ret, who, sizeof who);
        int aa = -1;
        uintptr_t gfx = Ptr(g_base + g_ra.globalGfx);
        if (gfx) Rd(gfx + g_ra.oAaSetting, aa);
        g_uls.loggedView = true;
        g_log("ui-layer: gun drawn right before the game's frame view draws the UI (the view drew the scene itself; caller %s, anti-aliasing %d); UI now draws on top",
              who, aa);
    }
}
__attribute__((noinline)) uintptr_t HkViewUi(void* self, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
    UiLayerBeforeViewUi(__builtin_return_address(0));
    return g_viewUiOrig(self, a2, a3, a4);
}
void ViewUnhookAt(int i, const char* why, bool write) {
    uintptr_t o = g_viewObjs[i];
    if (write && Ptr(o) == reinterpret_cast<uintptr_t>(g_viewVtCopy)) {
        uintptr_t orig = g_base + g_ra.vtView;
        SIZE_T w = 0;
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(o), &orig, sizeof orig, &w);
        g_log("ui-layer: frame view %p vtable restored (%s)", (void*)o, why);
    } else {
        g_log("ui-layer: frame view %p released without writing it (%s)", (void*)o, why);
    }
    g_viewObjs[i] = g_viewObjs[--g_viewN];
    g_viewObjs[g_viewN] = 0;
}
void UiLayerViewStep() {
    UiLayerReadIni();
    bool want = g_uiLayerMode == 1 && g_viewRouteOk && (g_fp || g_testLayer) && !g_failed;
    if (!want) {
        while (g_viewN) ViewUnhookAt(g_viewN - 1, !(g_fp || g_testLayer) ? "first person off" : "frame view route off", true);
        return;
    }
    uintptr_t gfx = Ptr(g_base + g_ra.globalGfx), fg = gfx ? Ptr(gfx + g_ra.oFrameGraph) : 0, arr = 0;
    uint32_t n = 0;
    if (!fg || !Rd(fg + g_ra.oPassList + 4, n) || !n || n > 1024 || !(arr = Ptr(fg + g_ra.oPassList + 8))) return;
    const uintptr_t vtSub = g_base + g_ra.vtViewSub, vtGame = g_base + g_ra.vtView;
    uintptr_t ours = reinterpret_cast<uintptr_t>(g_viewVtCopy), found[kMaxViews] = {};
    int nf = 0;
    for (uint32_t i = 0; i < n && nf < kMaxViews; ++i) {
        uintptr_t p = Ptr(arr + 8 * i);
        if (!p || Ptr(p) != vtSub) continue;
        uintptr_t v = p - 0x18, vt = Ptr(v);
        if (vt == vtGame || (ours && vt == ours)) found[nf++] = v;
    }
    for (int i = g_viewN - 1; i >= 0; --i) {
        bool still = false;
        for (int k = 0; k < nf; ++k) still |= found[k] == g_viewObjs[i];
        if (!still) ViewUnhookAt(i, "frame view gone from the pass list", false);
    }
    if (!nf) {
        if (!g_viewLoggedMissing) {
            g_viewLoggedMissing = true;
            g_log("ui-layer: frame view (sub-object vtable e+%llx) not in the %u-entry render pass list; with anti-aliasing off the gun stays at Present",
                  static_cast<unsigned long long>(g_ra.vtViewSub), n);
        }
        return;
    }
    for (int k = 0; k < nf; ++k) {
        uintptr_t v = found[k];
        if (Ptr(v) == ours && ours) {
            bool listed = false;
            for (int i = 0; i < g_viewN; ++i) listed |= g_viewObjs[i] == v;
            if (!listed && g_viewN < kMaxViews) g_viewObjs[g_viewN++] = v;
            continue;
        }
        if (!g_viewVtCopy) {
            constexpr int kMax = 64;
            int c = 0;
            for (; c < kMax; ++c) {
                uintptr_t x = 0;
                if (!Rd(vtGame + 8 * c, x) || !InExe(reinterpret_cast<void*>(x))) break;
            }
            if (c <= static_cast<int>(kViewUiSlot / 8)) return;
            auto* copy = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (c + 2) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!copy) return;
            copy[0] = Ptr(vtGame - 16);
            copy[1] = Ptr(vtGame - 8);
            for (int i = 0; i < c; ++i) copy[2 + i] = Ptr(vtGame + 8 * i);
            g_viewUiOrig = reinterpret_cast<FnViewUi>(copy[2 + kViewUiSlot / 8]);
            copy[2 + kViewUiSlot / 8] = reinterpret_cast<uintptr_t>(&HkViewUi);
            DWORD old;
            VirtualProtect(copy, (c + 2) * sizeof(uintptr_t), PAGE_READONLY, &old);
            g_viewVtCopy = copy + 2, ours = reinterpret_cast<uintptr_t>(g_viewVtCopy);
            g_log("ui-layer: frame view vtable e+%llx copied (%d slots), slot +0x%llx -> our hook", static_cast<unsigned long long>(g_ra.vtView), c,
                  static_cast<unsigned long long>(kViewUiSlot));
        }
        SIZE_T w = 0;
        if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(v), &ours, sizeof ours, &w) || w != sizeof ours) {
            g_log("ui-layer: vptr write failed on frame view %p", (void*)v);
            continue;
        }
        if (g_viewN < kMaxViews) g_viewObjs[g_viewN++] = v;
        g_log("ui-layer: hooked the game's frame view %p (%d with that vtable in the %u-entry pass list); D3D context untouched", (void*)v, nf, n);
    }
}
