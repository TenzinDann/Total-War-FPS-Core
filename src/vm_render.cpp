#include "vm_render.h"
#include "hvm_slim.h"
#include <d3dcompiler.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <vector>
namespace vm {
namespace {
template <class T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
void SetErr(char* err, size_t n, const char* fmt, ...) {
    if (!err || !n) return;
    va_list a;
    va_start(a, fmt);
    vsnprintf(err, n, fmt, a);
    va_end(a);
}
bool ReadFileAll(const wchar_t* path, unsigned char** out, size_t* outLen) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (256ll << 20)) {
        CloseHandle(h);
        return false;
    }
    size_t n = static_cast<size_t>(sz.QuadPart);
    unsigned char* buf = new unsigned char[n];
    DWORD got = 0;
    size_t done = 0;
    while (done < n) {
        DWORD chunk = static_cast<DWORD>((n - done) > (1u << 30) ? (1u << 30) : (n - done));
        if (!ReadFile(h, buf + done, chunk, &got, nullptr) || got == 0) break;
        done += got;
    }
    CloseHandle(h);
    if (done != n) {
        delete[] buf;
        return false;
    }
    *out = buf;
    *outLen = n;
    return true;
}
struct PreEntry {
    int tag;
    bool hvm;
    wchar_t path[MAX_PATH * 2];
    unsigned char* data;
    size_t len;
    ULONGLONG size;
    FILETIME stamp;
    bool srgb;
    bool bgTried;
    ID3D11ShaderResourceView* srv;
    ID3D11Device* dev;
};
constexpr int kMaxPre = 256;
PreEntry g_pre[kMaxPre];
int g_preN = 0;
SRWLOCK g_preLock = SRWLOCK_INIT;
ID3D11Device* g_preDev = nullptr;
volatile LONG g_preBuilt = 0, g_preTaken = 0;
bool FileStamp(const wchar_t* path, ULONGLONG* size, FILETIME* stamp) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return false;
    *size = (static_cast<ULONGLONG>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
    *stamp = fa.ftLastWriteTime;
    return true;
}
void PutPrefetched(int tag, bool hvm, const wchar_t* path, unsigned char* data, size_t len, ULONGLONG size, FILETIME stamp, bool srgb = false,
                   ID3D11ShaderResourceView* srv = nullptr, ID3D11Device* dev = nullptr, bool bgTried = false) {
    AcquireSRWLockExclusive(&g_preLock);
    if (g_preN < kMaxPre && wcslen(path) < MAX_PATH * 2) {
        PreEntry& e = g_pre[g_preN++];
        e.tag = tag, e.hvm = hvm, e.data = data, e.len = len, e.size = size, e.stamp = stamp;
        e.srgb = srgb, e.srv = srv, e.dev = dev, e.bgTried = bgTried || srv;
        wcscpy(e.path, path);
        data = nullptr;
        srv = nullptr;
    }
    ReleaseSRWLockExclusive(&g_preLock);
    delete[] data;
    if (srv) srv->Release();
}
bool TakePrefetchedSrv(const wchar_t* path, bool srgb, ID3D11Device* dev, ID3D11ShaderResourceView** out) {
    PreEntry got = {};
    bool found = false;
    AcquireSRWLockExclusive(&g_preLock);
    for (int i = 0; i < g_preN; ++i) {
        if (g_pre[i].hvm || !g_pre[i].srv || g_pre[i].srgb != srgb || g_pre[i].dev != dev || _wcsicmp(g_pre[i].path, path)) continue;
        got = g_pre[i];
        g_pre[i] = g_pre[--g_preN];
        found = true;
        break;
    }
    ReleaseSRWLockExclusive(&g_preLock);
    if (!found) return false;
    ULONGLONG size = 0;
    FILETIME stamp = {};
    if (!FileStamp(path, &size, &stamp) || size != got.size || CompareFileTime(&stamp, &got.stamp)) {
        got.srv->Release();
        return false;
    }
    *out = got.srv;
    InterlockedIncrement(&g_preTaken);
    return true;
}
bool TakePrefetched(const wchar_t* path, bool hvm, unsigned char** out, size_t* outLen) {
    PreEntry got = {};
    bool found = false;
    AcquireSRWLockExclusive(&g_preLock);
    for (int i = 0; i < g_preN; ++i) {
        if (g_pre[i].hvm != hvm || !g_pre[i].data || _wcsicmp(g_pre[i].path, path)) continue;
        got = g_pre[i];
        g_pre[i] = g_pre[--g_preN];
        found = true;
        break;
    }
    ReleaseSRWLockExclusive(&g_preLock);
    if (!found) return false;
    ULONGLONG size = 0;
    FILETIME stamp = {};
    if (!FileStamp(path, &size, &stamp) || size != got.size || CompareFileTime(&stamp, &got.stamp)) {
        delete[] got.data;
        return false;
    }
    *out = got.data;
    *outLen = got.len;
    return true;
}
#pragma pack(push, 1)
struct DdsPixelFormat {
    DWORD size, flags, fourCC, rgbBitCount, rMask, gMask, bMask, aMask;
};
struct DdsHeader {
    DWORD size, flags, height, width, pitchOrLinearSize, depth, mipMapCount, reserved1[11];
    DdsPixelFormat pf;
    DWORD caps, caps2, caps3, caps4, reserved2;
};
struct DdsHeader10 {
    DWORD dxgiFormat, resourceDimension, miscFlag, arraySize, miscFlags2;
};
#pragma pack(pop)
constexpr DWORD Four(char a, char b, char c, char d) {
    return DWORD(uint8_t(a)) | (DWORD(uint8_t(b)) << 8) | (DWORD(uint8_t(c)) << 16) | (DWORD(uint8_t(d)) << 24);
}
DXGI_FORMAT ToSrgb(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_BC7_UNORM: return DXGI_FORMAT_BC7_UNORM_SRGB;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    default: return f;
    }
}
UINT BlockBytes(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM: return 8;
    case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16: case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB: return 16;
    default: return 0;
    }
}
bool DdsCreate(ID3D11Device* dev, const unsigned char* data, size_t len, bool srgb, const wchar_t* path, ID3D11ShaderResourceView** srvOut,
               char* err, size_t errLen) {
    bool ok = false;
    do {
        if (len < 4 + sizeof(DdsHeader) || memcmp(data, "DDS ", 4) != 0) {
            SetErr(err, errLen, "not a DDS: %ls", path);
            break;
        }
        DdsHeader h;
        memcpy(&h, data + 4, sizeof h);
        size_t off = 4 + sizeof(DdsHeader);
        DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
        if ((h.pf.flags & 0x4) && h.pf.fourCC == Four('D', 'X', '1', '0')) {
            if (len < off + sizeof(DdsHeader10)) break;
            DdsHeader10 h10;
            memcpy(&h10, data + off, sizeof h10);
            off += sizeof h10;
            fmt = static_cast<DXGI_FORMAT>(h10.dxgiFormat);
            if (h10.arraySize > 1 || h10.resourceDimension != 3) {
                SetErr(err, errLen, "unsupported DDS layout (array/dim): %ls", path);
                break;
            }
        } else if (h.pf.flags & 0x4) {
            switch (h.pf.fourCC) {
            case Four('D', 'X', 'T', '1'): fmt = DXGI_FORMAT_BC1_UNORM; break;
            case Four('D', 'X', 'T', '2'): case Four('D', 'X', 'T', '3'): fmt = DXGI_FORMAT_BC2_UNORM; break;
            case Four('D', 'X', 'T', '4'): case Four('D', 'X', 'T', '5'): fmt = DXGI_FORMAT_BC3_UNORM; break;
            case Four('A', 'T', 'I', '1'): case Four('B', 'C', '4', 'U'): fmt = DXGI_FORMAT_BC4_UNORM; break;
            case Four('A', 'T', 'I', '2'): case Four('B', 'C', '5', 'U'): fmt = DXGI_FORMAT_BC5_UNORM; break;
            default: break;
            }
        } else if ((h.pf.flags & 0x40) && h.pf.rgbBitCount == 32) {
            if (h.pf.rMask == 0xff && h.pf.bMask == 0xff0000) fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
            if (h.pf.rMask == 0xff0000 && h.pf.bMask == 0xff) fmt = DXGI_FORMAT_B8G8R8A8_UNORM;
        }
        if (fmt == DXGI_FORMAT_UNKNOWN) {
            SetErr(err, errLen, "unsupported DDS format (fourCC %08lx): %ls", h.pf.fourCC, path);
            break;
        }
        if (srgb) fmt = ToSrgb(fmt);
        UINT w = h.width, ht = h.height;
        UINT mips = (h.flags & 0x20000) && h.mipMapCount ? h.mipMapCount : 1;
        if (!w || !ht || w > 16384 || ht > 16384 || mips > 16) break;
        UINT bb = BlockBytes(fmt);
        D3D11_SUBRESOURCE_DATA sub[16];
        size_t p = off;
        UINT mw = w, mh = ht;
        bool fits = true;
        for (UINT m = 0; m < mips; ++m) {
            UINT row, rows;
            if (bb) {
                row = ((mw + 3) / 4) * bb;
                rows = (mh + 3) / 4;
            } else {
                row = mw * 4;
                rows = mh;
            }
            size_t bytes = static_cast<size_t>(row) * rows;
            if (p + bytes > len) {
                fits = false;
                break;
            }
            sub[m].pSysMem = data + p;
            sub[m].SysMemPitch = row;
            sub[m].SysMemSlicePitch = static_cast<UINT>(bytes);
            p += bytes;
            mw = mw > 1 ? mw / 2 : 1;
            mh = mh > 1 ? mh / 2 : 1;
        }
        if (!fits) {
            SetErr(err, errLen, "DDS truncated: %ls", path);
            break;
        }
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = ht;
        td.MipLevels = mips;
        td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = dev->CreateTexture2D(&td, sub, &tex);
        if (FAILED(hr)) {
            SetErr(err, errLen, "CreateTexture2D 0x%08lx fmt %d: %ls", hr, fmt, path);
            break;
        }
        hr = dev->CreateShaderResourceView(tex, nullptr, srvOut);
        tex->Release();
        if (FAILED(hr)) {
            SetErr(err, errLen, "CreateShaderResourceView 0x%08lx: %ls", hr, path);
            break;
        }
        ok = true;
    } while (false);
    return ok;
}
bool LoadDds(ID3D11Device* dev, const wchar_t* path, bool srgb, ID3D11ShaderResourceView** srvOut, char* err,
             size_t errLen) {
    if (TakePrefetchedSrv(path, srgb, dev, srvOut)) return true;
    unsigned char* data = nullptr;
    size_t len = 0;
    if (!TakePrefetched(path, false, &data, &len) && !ReadFileAll(path, &data, &len)) {
        SetErr(err, errLen, "texture missing: %ls", path);
        return false;
    }
    bool ok = DdsCreate(dev, data, len, srgb, path, srvOut, err, errLen);
    delete[] data;
    return ok;
}
#pragma pack(push, 1)
struct HvmHeader {
    char magic[4];
    uint32_t version, vertexCount, indexCount, submeshCount, boneCount, clipCount, textureCount;
    int32_t cameraBone;
    uint32_t flags;
    float reserved[6];
};
struct HvmTexture {
    char file[76];
    uint32_t srgb;
};
struct HvmSubmesh {
    uint32_t firstIndex, indexCount;
    int32_t albedo, normal, material;
    uint32_t normalMode, materialMode, flags;
};
struct HvmBone {
    char name[48];
    int32_t parent;
    float bind[10];
    float invBind[12];
};
struct HvmClip {
    char name[32];
    float fps;
    uint32_t frames, flags, reserved;
};
#pragma pack(pop)
static_assert(sizeof(HvmHeader) == 64, "hvm header");
static_assert(sizeof(HvmTexture) == 80, "hvm texture");
static_assert(sizeof(HvmSubmesh) == 32, "hvm submesh");
static_assert(sizeof(HvmBone) == 140, "hvm bone");
static_assert(sizeof(HvmClip) == 48, "hvm clip");
constexpr UINT kVertexStride = 56;
constexpr UINT kMaxBones = 256;
struct Aff {
    float m[12];
};
Aff AffIdentity() {
    Aff a = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    return a;
}
Aff AffMul(const Aff& A, const Aff& B) {
    Aff C;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            C.m[r * 4 + c] = A.m[r * 4 + 0] * B.m[0 * 4 + c] + A.m[r * 4 + 1] * B.m[1 * 4 + c] +
                             A.m[r * 4 + 2] * B.m[2 * 4 + c];
        C.m[r * 4 + 3] = A.m[r * 4 + 0] * B.m[3] + A.m[r * 4 + 1] * B.m[7] + A.m[r * 4 + 2] * B.m[11] + A.m[r * 4 + 3];
    }
    return C;
}
Aff AffInverse(const Aff& A) {
    const float* m = A.m;
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float A0 = e * i - f * h, B0 = -(d * i - f * g), C0 = d * h - e * g;
    float det = a * A0 + b * B0 + c * C0;
    Aff R = AffIdentity();
    if (std::fabs(det) < 1e-20f) return R;
    float inv = 1.0f / det;
    float r[9] = {A0 * inv, -(b * i - c * h) * inv, (b * f - c * e) * inv,
                  B0 * inv, (a * i - c * g) * inv, -(a * f - c * d) * inv,
                  C0 * inv, -(a * h - b * g) * inv, (a * e - b * d) * inv};
    for (int k = 0; k < 3; ++k) {
        R.m[k * 4 + 0] = r[k * 3 + 0];
        R.m[k * 4 + 1] = r[k * 3 + 1];
        R.m[k * 4 + 2] = r[k * 3 + 2];
        R.m[k * 4 + 3] = -(r[k * 3 + 0] * m[3] + r[k * 3 + 1] * m[7] + r[k * 3 + 2] * m[11]);
    }
    return R;
}
Aff AffFromTrs(const float* trs) {
    float x = trs[3], y = trs[4], z = trs[5], w = trs[6];
    float n = std::sqrt(x * x + y * y + z * z + w * w);
    if (n > 1e-8f) {
        x /= n;
        y /= n;
        z /= n;
        w /= n;
    } else {
        x = y = z = 0;
        w = 1;
    }
    float sx = trs[7], sy = trs[8], sz = trs[9];
    Aff A;
    A.m[0] = (1 - 2 * (y * y + z * z)) * sx;
    A.m[1] = (2 * (x * y - w * z)) * sy;
    A.m[2] = (2 * (x * z + w * y)) * sz;
    A.m[3] = trs[0];
    A.m[4] = (2 * (x * y + w * z)) * sx;
    A.m[5] = (1 - 2 * (x * x + z * z)) * sy;
    A.m[6] = (2 * (y * z - w * x)) * sz;
    A.m[7] = trs[1];
    A.m[8] = (2 * (x * z - w * y)) * sx;
    A.m[9] = (2 * (y * z + w * x)) * sy;
    A.m[10] = (1 - 2 * (x * x + y * y)) * sz;
    A.m[11] = trs[2];
    return A;
}
Aff AffRotY(float a) {
    float c = std::cos(a), s = std::sin(a);
    Aff R = {{c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0}};
    return R;
}
Aff AffRotX(float a) {
    float c = std::cos(a), s = std::sin(a);
    Aff R = {{1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0}};
    return R;
}
Aff AffRotZ(float a) {
    float c = std::cos(a), s = std::sin(a);
    Aff R = {{c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0}};
    return R;
}
Aff AffTrans(float x, float y, float z) {
    Aff T = AffIdentity();
    T.m[3] = x;
    T.m[7] = y;
    T.m[11] = z;
    return T;
}
Aff AffScale(float x, float y, float z) {
    Aff S = {{x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0}};
    return S;
}
float AffDet(const Aff& A) {
    const float* m = A.m;
    return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
           m[2] * (m[4] * m[9] - m[5] * m[8]);
}
constexpr float kDeg = 3.14159265358979f / 180.0f;
const char kShader[] = R"HLSL(cbuffer CFrame : register(b0) {
    row_major float4x4 uProj;
    row_major float4x4 uModel;
    float4 uLightDir;
    float4 uLightColor;
    float4 uAmbSky;
    float4 uAmbGround;
    float4 uParams;
    float4 uScene;
    float4 uFlashPos;
    float4 uFlashCol;
    float4 uLens0;
    float4 uLens1;
    float4 uLens2;
};
cbuffer CBones : register(b1) { float4 uBones[768]; };
cbuffer CMat : register(b2) {
    float4 uMat;
    float4 uMat2;
    float4 uMat3;
    float4 uEm0;
    float4 uEm1;
    float4 uEm2;
    float4 uEm3;
    float4 uEmStop;
    float4 uEmC1, uEmC2, uEmC3, uEmC4;
    float4 uIrA, uIrB;
};
Texture2D tAlbedo : register(t0);
Texture2D tNormal : register(t1);
Texture2D tMaterial : register(t2);
Texture2D tScene : register(t3);
Texture2D tEmMask : register(t4);
Texture2D tEmTex : register(t5);
Texture2D tEmDist : register(t6);
SamplerState sLinear : register(s0);
struct VSIn {
    float3 pos : POSITION; float3 nrm : NORMAL; float4 tan : TANGENT; float2 uv : TEXCOORD0;
    uint4 bi : BLENDINDICES; float4 bw : BLENDWEIGHT;
};
struct PSIn {
    float4 pos : SV_Position; float3 vpos : TEXCOORD1; float3 n : TEXCOORD2; float4 t : TEXCOORD3; float2 uv : TEXCOORD0;
};
float3x4 Bone(uint i) { return float3x4(uBones[i * 3 + 0], uBones[i * 3 + 1], uBones[i * 3 + 2]); }
PSIn VS(VSIn i) {
    float3x4 m = Bone(i.bi.x) * i.bw.x + Bone(i.bi.y) * i.bw.y + Bone(i.bi.z) * i.bw.z + Bone(i.bi.w) * i.bw.w;
    float3 p = mul(m, float4(i.pos, 1));
    float3 n = mul((float3x3)m, i.nrm);
    float3 t = mul((float3x3)m, i.tan.xyz);
    float4 v = mul(uModel, float4(p, 1));
    PSIn o;
    o.vpos = v.xyz;
    o.pos = mul(uProj, float4(v.xyz, 1));
    o.n = mul((float3x3)uModel, n);
    o.t = float4(mul((float3x3)uModel, t), i.tan.w);
    o.uv = i.uv;
    return o;
}
float3 SrgbEncode(float3 c) {
    c = saturate(c);
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return float3(c.x <= 0.0031308 ? lo.x : hi.x, c.y <= 0.0031308 ? lo.y : hi.y, c.z <= 0.0031308 ? lo.z : hi.z);
}
float3 SrgbDecode(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return float3(c.x <= 0.04045 ? lo.x : hi.x, c.y <= 0.04045 ? lo.y : hi.y, c.z <= 0.04045 ? lo.z : hi.z);
}
float3 RgbToHsl(float3 c) {
    float mn = min(min(c.r, c.g), c.b), mx = max(max(c.r, c.g), c.b), d = mx - mn, l2 = mx + mn;
    if (d <= 0) return float3(0, 0, l2 * 0.5);
    float s = d / (l2 < 1 ? max(l2, 1e-6) : max(2 - l2, 1e-6));
    float3 dl = ((mx - c) / 6 + d * 0.5) / d;
    float hh = c.r == mx ? dl.b - dl.g : (c.g == mx ? 1.0 / 3 + dl.r - dl.b : 2.0 / 3 + dl.g - dl.r);
    hh = hh < 0 ? hh + 1 : (hh > 1 ? hh - 1 : hh);
    return float3(hh, s, l2 * 0.5);
}
float HueToRgb(float v1, float v2, float vh) {
    vh = vh < 0 ? vh + 1 : (vh > 1 ? vh - 1 : vh);
    if (vh < 1.0 / 6) return v1 + (v2 - v1) * 6 * vh;
    if (vh < 0.5) return v2;
    if (vh < 2.0 / 3) return v1 + (v2 - v1) * (2.0 / 3 - vh) * 6;
    return v1;
}
float3 HslToRgb(float3 hsl) {
    if (hsl.y == 0) return hsl.zzz;
    float v2 = hsl.z < 0.5 ? hsl.z * (1 + hsl.y) : hsl.z + hsl.y - hsl.y * hsl.z, v1 = 2 * hsl.z - v2;
    return float3(HueToRgb(v1, v2, hsl.x + 1.0 / 3), HueToRgb(v1, v2, hsl.x), HueToRgb(v1, v2, hsl.x - 1.0 / 3));
}
float Bar(float2 d, float a, float b, float hw) {
    float ax = abs(d.x);
    return saturate(min(ax - a, b - ax) + 0.5) * saturate(hw - abs(d.y) + 0.5);
}
float Reticle(float2 d, float R) {
    float t = max(0.6, R * 0.003), T = max(1.5, R * 0.016), g = R * 0.035, dot_r = max(1.0, R * 0.01);
    float c = max(max(Bar(d, g, 0.6 * R, t), Bar(d.yx, g, 0.6 * R, t)), max(Bar(d, 0.6 * R, R, T), Bar(d.yx, 0.6 * R, R, T)));
    float2 ad = abs(d);
    [unroll] for (int k = 1; k <= 4; ++k) {
        float p = k * 0.12 * R;
        c = max(c, max(saturate(dot_r - length(ad - float2(p, 0)) + 0.5), saturate(dot_r - length(ad - float2(0, p)) + 0.5)));
    }
    return c;
}
float4 PS(PSIn i, bool front : SV_IsFrontFace) : SV_Target {
    float4 alb = tAlbedo.Sample(sLinear, i.uv);
    if (uMat3.y > 0.5 && alb.a < 0.5) discard;
    float3 N = normalize(i.n);
    float3 V = normalize(-i.vpos);
    if (dot(N, V) < 0) N = -N;
    if (uMat.y > 0.5) {
        float4 nt = tNormal.Sample(sLinear, i.uv);
        float2 xy = (uMat.x > 0.5) ? float2(nt.a, nt.g) : nt.rg;
        xy = xy * 2 - 1;
        if (uMat2.w > 0.5) xy.y = -xy.y;
        float z = sqrt(saturate(1 - dot(xy, xy)));
        float3 T = i.t.xyz - N * dot(N, i.t.xyz);
        float tl = length(T);
        if (tl > 1e-5) {
            T /= tl;
            float3 B = cross(N, T) * (i.t.w * uParams.w >= 0 ? 1 : -1);
            N = normalize(T * xy.x + B * xy.y + N * z);
        }
    }
    float metal = uMat2.y, rough = uMat2.x;
    if (uMat.z > 0.5) {
        float4 m = tMaterial.Sample(sLinear, i.uv);
        if (uMat.w > 1.5) {
            float e = 1 + m.r * 149;
            rough = pow(2.0 / (e + 2.0), 0.25);
            if (uMat2.z > 0.5) metal = m.g;
        } else if (uMat.w > 0.5) { rough = m.r; metal = m.g; } else { metal = m.r; rough = m.g; }
    }
    if (uEm3.z > 0.5) {
        float4 mk = tEmMask.Sample(sLinear, i.uv);
        float3 base = alb.rgb;
        float3 ic = lerp(uIrA.rgb, uIrB.rgb, abs(dot(N, V)));
        float3 cg = pow(abs(ic), 1 / 2.2), bg = pow(abs(base), 1 / 2.2);
        float3 ov = dot(base, float3(0.3, 0.59, 0.11)) > 0.269 ? 1 - 2 * (1 - cg) * (1 - bg) : 2 * cg * bg;
        ov = pow(saturate(ov), 2.2);
        float am = max(uEm3.w, 0);
        float3 r5 = lerp(base, ov, min(am, 1));
        float3 hc = RgbToHsl(cg);
        float3 cb = pow(abs(HslToRgb(float3(hc.xy, RgbToHsl(pow(abs(r5), 1 / 2.2)).z))), 2.2);
        float3 ir = lerp(r5, cb, saturate(min(am, 2) - 1));
        alb.rgb = lerp(lerp(base, ir, mk.b), base, saturate(rough));
    }
    float3 emis = float3(0, 0, 0);
    if (uEm0.x > 0.5) {
        float maskA = tEmMask.Sample(sLinear, i.uv).a;
        float2 dn = tEmDist.Sample(sLinear, i.uv).rb;
        float2 euv = (i.uv + dn * uEm0.w + uEm2.xy) * uEm2.zw;
        float et = tEmTex.Sample(sLinear, euv).r;
        float fr = 1 - saturate(dot(N, V));
        fr *= fr;
        float3 e;
        if (uEm0.x > 1.5) {
            e = uEm1.rgb * ((uEm0.y * et + uEm1.w + uEm0.z * fr) * maskA);
        } else {
            float v = (et + uEm1.w + uEm0.z * fr) * maskA;
            float3 g;
            if (v <= uEmStop.x) g = uEmC1.rgb;
            else if (v <= uEmStop.y) g = lerp(uEmC1.rgb, uEmC2.rgb, (v - uEmStop.x) / (uEmStop.y - uEmStop.x + 0.001));
            else if (v <= uEmStop.z) g = lerp(uEmC2.rgb, uEmC3.rgb, (v - uEmStop.y) / (uEmStop.z - uEmStop.y + 0.001));
            else if (v <= uEmStop.w) g = lerp(uEmC3.rgb, uEmC4.rgb, (v - uEmStop.z) / (uEmStop.w - uEmStop.z + 0.001));
            else g = uEmC4.rgb;
            e = g * uEm0.y * uEm1.rgb;
        }
        float d = max(max(max(e.r, e.g), e.b), 1);
        float el = dot(e, float3(0.2126, 0.7152, 0.0722));
        alb.rgb = 1 - (1 - alb.rgb) * (1 - e / d);
        float ls = saturate(el);
        metal *= 1 - ls;
        rough = lerp(rough, 1, ls);
        emis = alb.rgb * (1 - metal) * (min(d * el * 0.02, 1) * 255 * uEm3.x);
    }
    rough = clamp(rough, 0.04, 1);
    float3 ambSky = uAmbSky.rgb, ambGround = uAmbGround.rgb, lightCol = uLightColor.rgb;
    if (uScene.w > 0.5) {
        float3 sTop = tScene.SampleLevel(sLinear, float2(0.5, 0.25), uScene.y).rgb;
        float3 sBot = tScene.SampleLevel(sLinear, float2(0.5, 0.75), uScene.y).rgb;
        if (uScene.z > 0.5) { sTop = SrgbDecode(saturate(sTop)); sBot = SrgbDecode(saturate(sBot)); }
        ambSky = lerp(ambSky, sTop * 1.3, uScene.x);
        ambGround = lerp(ambGround, sBot * 1.3, uScene.x);
        float3 avg = (sTop + sBot) * 0.5;
        float lum = dot(avg, float3(0.2126, 0.7152, 0.0722));
        float3 tint = lum > 1e-4 ? clamp(avg / lum, 0, 3) : float3(1, 1, 1);
        lightCol *= lerp(float3(1, 1, 1), tint, uScene.x * 0.6) * lerp(1.0, saturate(lum * 4.0), uScene.x * 0.6);
    }
    float3 L = normalize(uLightDir.xyz);
    float3 H = normalize(L + V);
    float NdL = saturate(dot(N, L)), NdV = saturate(dot(N, V)) + 1e-4, NdH = saturate(dot(N, H)), VdH = saturate(dot(V, H));
    float a = rough * rough, a2 = a * a;
    float d = NdH * NdH * (a2 - 1) + 1;
    float D = a2 / (3.14159265 * d * d);
    float k = (rough + 1) * (rough + 1) / 8;
    float G = (NdL / (NdL * (1 - k) + k)) * (NdV / (NdV * (1 - k) + k));
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), alb.rgb, metal);
    float3 F = F0 + (1 - F0) * pow(1 - VdH, 5);
    float3 spec = D * G * F / (4 * NdL * NdV + 1e-4);
    float3 diff = (1 - F) * (1 - metal) * alb.rgb / 3.14159265;
    float3 direct = (diff + spec) * lightCol * NdL;
    if (uFlashCol.w > 0) {
        float3 Lp = uFlashPos.xyz - i.vpos;
        float dist = length(Lp);
        float3 Lf = Lp / max(dist, 1e-4);
        float att = saturate(1 - dist / uFlashPos.w);
        att *= att;
        float3 Hf = normalize(Lf + V);
        float NdLf = saturate(dot(N, Lf)), NdHf = saturate(dot(N, Hf));
        float df = NdHf * NdHf * (a2 - 1) + 1;
        float Ref = a2 / (3.14159265 * df * df);
        float Gf = (NdLf / (NdLf * (1 - k) + k)) * (NdV / (NdV * (1 - k) + k));
        float3 Ff = F0 + (1 - F0) * pow(1 - saturate(dot(V, Hf)), 5);
        float3 specf = Ref * Gf * Ff / (4 * NdLf * NdV + 1e-4);
        float3 difff = (1 - Ff) * (1 - metal) * alb.rgb / 3.14159265;
        direct += (difff + specf) * uFlashCol.rgb * NdLf * att;
    }
    float hemi = N.y * 0.5 + 0.5;
    float3 amb = lerp(ambGround, ambSky, hemi);
    float3 Fa = F0 + (max(float3(1 - rough, 1 - rough, 1 - rough), F0) - F0) * pow(1 - NdV, 5);
    float3 ambient = amb * (alb.rgb * (1 - metal) * (1 - Fa)) + lerp(ambGround, ambSky, saturate(reflect(-V, N).y * 0.5 + 0.5)) * Fa * (1 - rough * 0.7);
    float3 c = (direct + ambient + emis) * uParams.x;
    if (uEm3.y > 0.5 && max(max(emis.r, emis.g), emis.b) > 1e-3) {
        float m = max(max(c.r, c.g), c.b);
        if (m > 0.8) c *= (0.8 + 0.2 * (1 - exp(-(m - 0.8) / 0.2))) / m;
    }
    if (uParams.y > 0.5) c = SrgbEncode(c);
    if (uMat3.x > 0.5 && uLens1.z > 0.001 && uScene.w > 0.5) {
        float2 px = i.pos.xy, ax = uLens0.xy;
        float R = max(uLens0.z, 1.0);
        float3 sc = tScene.SampleLevel(sLinear, (ax + (px - ax) / max(uLens0.w, 1.0)) * uLens2.xy, 0).rgb;
        if (uScene.z > 0.5) sc = SrgbDecode(saturate(sc));
        float ra = length(px - ax) / R, rl = length(px - uLens1.xy) / R;
        sc *= (1 - smoothstep(0.80, 1.02, ra)) * (1 - 0.35 * smoothstep(0.65, 1.0, rl));
        sc = lerp(sc, float3(0.004, 0.004, 0.004), Reticle(px - ax, R) * saturate(uLens1.w));
        if (uParams.y > 0.5) sc = SrgbEncode(sc);
        c = lerp(c, sc, saturate(uLens1.z));
    }
    return float4(c, 1);
}
)HLSL";
typedef HRESULT(WINAPI* FnD3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                     UINT, UINT, ID3DBlob**, ID3DBlob**);
FnD3DCompile RealCompiler() {
    HMODULE m = GetModuleHandleW(L"d3dcompiler_47.dll");
    if (!m) m = LoadLibraryW(L"d3dcompiler_47.dll");
    return m ? reinterpret_cast<FnD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
}
struct ShaderCacheEntry {
    uint64_t hash;
    SIZE_T len;
    char entry[16], target[16];
    UINT f1, f2;
    ID3DBlob* blob;
};
ShaderCacheEntry g_shc[32];
int g_shcN = 0;
SRWLOCK g_shcLock = SRWLOCK_INIT;
uint64_t Fnv64(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const unsigned char*>(p)[i]) * 1099511628211ull;
    return h;
}
HRESULT WINAPI CachedCompile(LPCVOID src, SIZE_T len, LPCSTR name, const D3D_SHADER_MACRO* defs, ID3DInclude* inc, LPCSTR entry,
                             LPCSTR target, UINT f1, UINT f2, ID3DBlob** code, ID3DBlob** msgs) {
    FnD3DCompile real = RealCompiler();
    if (!real) return E_FAIL;
    if (defs || inc || !src || !entry || !target || !code || strlen(entry) >= 16 || strlen(target) >= 16)
        return real(src, len, name, defs, inc, entry, target, f1, f2, code, msgs);
    uint64_t h = Fnv64(src, len);
    AcquireSRWLockExclusive(&g_shcLock);
    for (int i = 0; i < g_shcN; ++i) {
        const ShaderCacheEntry& e = g_shc[i];
        if (e.hash == h && e.len == len && e.f1 == f1 && e.f2 == f2 && !strcmp(e.entry, entry) && !strcmp(e.target, target)) {
            e.blob->AddRef();
            *code = e.blob;
            if (msgs) *msgs = nullptr;
            ReleaseSRWLockExclusive(&g_shcLock);
            return S_OK;
        }
    }
    HRESULT hr = real(src, len, name, defs, inc, entry, target, f1, f2, code, msgs);
    if (SUCCEEDED(hr) && *code && g_shcN < static_cast<int>(sizeof g_shc / sizeof *g_shc)) {
        ShaderCacheEntry& e = g_shc[g_shcN++];
        e.hash = h, e.len = len, e.f1 = f1, e.f2 = f2;
        strcpy(e.entry, entry);
        strcpy(e.target, target);
        e.blob = *code;
        e.blob->AddRef();
    }
    ReleaseSRWLockExclusive(&g_shcLock);
    return hr;
}
FnD3DCompile GetCompiler() { return RealCompiler() ? &CachedCompile : nullptr; }
struct StateBackup {
    UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11RasterizerState* rs = nullptr;
    ID3D11BlendState* bs = nullptr;
    FLOAT blendFactor[4];
    UINT sampleMask = 0;
    UINT stencilRef = 0;
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11ShaderResourceView* psSrv[7] = {};
    ID3D11SamplerState* psSampler = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11GeometryShader* gs = nullptr;
    ID3D11HullShader* hs = nullptr;
    ID3D11DomainShader* ds = nullptr;
    UINT psInstCount = 0, vsInstCount = 0, gsInstCount = 0, hsInstCount = 0, dsInstCount = 0;
    ID3D11ClassInstance *psInst[256], *vsInst[256], *gsInst[256], *hsInst[256], *dsInst[256];
    ID3D11Buffer* vsCb[2] = {};
    ID3D11Buffer* psCb[3] = {};
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer *ib = nullptr, *vb = nullptr;
    UINT ibOffset = 0, vbStride = 0, vbOffset = 0;
    DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;
    ID3D11InputLayout* layout = nullptr;
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    void Save(ID3D11DeviceContext* c) {
        c->RSGetScissorRects(&scissorCount, scissors);
        c->RSGetViewports(&viewportCount, viewports);
        c->RSGetState(&rs);
        c->OMGetBlendState(&bs, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&dss, &stencilRef);
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        c->PSGetShaderResources(0, 7, psSrv);
        c->PSGetSamplers(0, 1, &psSampler);
        psInstCount = vsInstCount = gsInstCount = hsInstCount = dsInstCount = 256;
        c->PSGetShader(&ps, psInst, &psInstCount);
        c->VSGetShader(&vs, vsInst, &vsInstCount);
        c->GSGetShader(&gs, gsInst, &gsInstCount);
        c->HSGetShader(&hs, hsInst, &hsInstCount);
        c->DSGetShader(&ds, dsInst, &dsInstCount);
        c->VSGetConstantBuffers(0, 2, vsCb);
        c->PSGetConstantBuffers(0, 3, psCb);
        c->IAGetPrimitiveTopology(&topo);
        c->IAGetIndexBuffer(&ib, &ibFormat, &ibOffset);
        c->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
        c->IAGetInputLayout(&layout);
    }
    void Restore(ID3D11DeviceContext* c) {
        c->RSSetScissorRects(scissorCount, scissors);
        c->RSSetViewports(viewportCount, viewports);
        c->RSSetState(rs);
        SafeRelease(rs);
        c->OMSetBlendState(bs, blendFactor, sampleMask);
        SafeRelease(bs);
        c->OMSetDepthStencilState(dss, stencilRef);
        SafeRelease(dss);
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
        for (auto*& r : rtv) SafeRelease(r);
        SafeRelease(dsv);
        c->PSSetShaderResources(0, 7, psSrv);
        for (auto*& s : psSrv) SafeRelease(s);
        c->PSSetSamplers(0, 1, &psSampler);
        SafeRelease(psSampler);
        c->PSSetShader(ps, psInst, psInstCount);
        SafeRelease(ps);
        for (UINT k = 0; k < psInstCount; ++k) SafeRelease(psInst[k]);
        c->VSSetShader(vs, vsInst, vsInstCount);
        SafeRelease(vs);
        for (UINT k = 0; k < vsInstCount; ++k) SafeRelease(vsInst[k]);
        c->GSSetShader(gs, gsInst, gsInstCount);
        SafeRelease(gs);
        for (UINT k = 0; k < gsInstCount; ++k) SafeRelease(gsInst[k]);
        c->HSSetShader(hs, hsInst, hsInstCount);
        SafeRelease(hs);
        for (UINT k = 0; k < hsInstCount; ++k) SafeRelease(hsInst[k]);
        c->DSSetShader(ds, dsInst, dsInstCount);
        SafeRelease(ds);
        for (UINT k = 0; k < dsInstCount; ++k) SafeRelease(dsInst[k]);
        c->VSSetConstantBuffers(0, 2, vsCb);
        for (auto*& b : vsCb) SafeRelease(b);
        c->PSSetConstantBuffers(0, 3, psCb);
        for (auto*& b : psCb) SafeRelease(b);
        c->IASetPrimitiveTopology(topo);
        c->IASetIndexBuffer(ib, ibFormat, ibOffset);
        SafeRelease(ib);
        c->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
        SafeRelease(vb);
        c->IASetInputLayout(layout);
        SafeRelease(layout);
    }
};
struct CFrame {
    float proj[16];
    float model[16];
    float lightDir[4];
    float lightColor[4];
    float ambSky[4];
    float ambGround[4];
    float params[4];
    float scene[4];
    float flashPos[4];
    float flashColor[4];
    float lens0[4];
    float lens1[4];
    float lens2[4];
};
static_assert(sizeof(CFrame) == 304, "cframe");
float IniFloat(const wchar_t* ini, const wchar_t* key, float def) {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"viewmodel", key, L"", buf, 64, ini);
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    return (end != buf && std::isfinite(v)) ? static_cast<float>(v) : def;
}
void IniVec2(const wchar_t* ini, const wchar_t* key, float* v) {
    wchar_t buf[128];
    GetPrivateProfileStringW(L"viewmodel", key, L"", buf, 128, ini);
    if (!buf[0]) return;
    float t[2];
    if (swscanf(buf, L"%f , %f", &t[0], &t[1]) == 2 && std::isfinite(t[0]) && std::isfinite(t[1])) v[0] = t[0], v[1] = t[1];
}
int IniFloatList(const wchar_t* ini, const wchar_t* key, float* out, int maxN) {
    static wchar_t buf[8192];
    GetPrivateProfileStringW(L"viewmodel", key, L"\x01", buf, 8192, ini);
    if (buf[0] == 1) return -1;
    int n = 0;
    const wchar_t* p = buf;
    while (*p && n < maxN) {
        wchar_t* e = nullptr;
        float v = static_cast<float>(wcstod(p, &e));
        if (e == p || !std::isfinite(v)) break;
        out[n++] = v;
        p = e;
        while (*p == L' ' || *p == L',' || *p == L'\t') ++p;
    }
    return n;
}
void IniQuat(const wchar_t* ini, const wchar_t* key, float* v) {
    wchar_t buf[160];
    GetPrivateProfileStringW(L"viewmodel", key, L"", buf, 160, ini);
    if (!buf[0]) return;
    float t[4];
    if (swscanf(buf, L"%f , %f , %f , %f", &t[0], &t[1], &t[2], &t[3]) != 4) return;
    float n = 0.f;
    for (float x : t) {
        if (!std::isfinite(x)) return;
        n += x * x;
    }
    n = std::sqrt(n);
    for (int k = 0; k < 4; ++k) v[k] = n > 0.5f ? t[k] / n : 0.f;
}
void IniVec3(const wchar_t* ini, const wchar_t* key, float* v) {
    wchar_t buf[128];
    GetPrivateProfileStringW(L"viewmodel", key, L"", buf, 128, ini);
    if (!buf[0]) return;
    float t[3];
    if (swscanf(buf, L"%f , %f , %f", &t[0], &t[1], &t[2]) == 3 && std::isfinite(t[0]) && std::isfinite(t[1]) &&
        std::isfinite(t[2])) {
        v[0] = t[0];
        v[1] = t[1];
        v[2] = t[2];
    }
}
}
const wchar_t* const kMeleeSfxNames[kMeleeSfx] = {L"swing", L"swing_layer", L"push", L"hit_stab", L"hit_stab_heavy",
                                                  L"hit_slash", L"hit_slash_heavy", L"hit_bash", L"hit_body", L"hit_gore",
                                                  L"hit_tail_stab", L"hit_tail_slash", L"block", L"block_tail",
                                                  L"hit_slash_light", L"hit_l4", L"hit_heavy", L"swing_heavy"};
bool LoadSettings(const wchar_t* ini, Settings& s) {
    if (GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES) return false;
    s.fovDeg = IniFloat(ini, L"fov", s.fovDeg);
    IniVec3(ini, L"offset", s.offset);
    IniVec3(ini, L"rotation", s.rotDeg);
    IniVec3(ini, L"tilt", s.tiltDeg);
    s.scale = IniFloat(ini, L"scale", s.scale);
    s.mirrorX = IniFloat(ini, L"mirror_x", s.mirrorX ? 1.f : 0.f) != 0.f;
    IniVec3(ini, L"light_dir", s.lightDir);
    IniVec3(ini, L"light_colour", s.lightColor);
    s.lightIntensity = IniFloat(ini, L"light_intensity", s.lightIntensity);
    IniVec3(ini, L"ambient_sky", s.ambSky);
    IniVec3(ini, L"ambient_ground", s.ambGround);
    s.ambIntensity = IniFloat(ini, L"ambient_intensity", s.ambIntensity);
    s.exposure = IniFloat(ini, L"exposure", s.exposure);
    s.sceneLight = IniFloat(ini, L"scene_light", s.sceneLight);
    if (s.sceneLight < 0) s.sceneLight = 0;
    if (s.sceneLight > 1) s.sceneLight = 1;
    s.swayScale = IniFloat(ini, L"sway", s.swayScale);
    s.bobScale = IniFloat(ini, L"bob", s.bobScale);
    s.bobRate = IniFloat(ini, L"bob_rate", s.bobRate);
    if (s.bobRate < 0.25f) s.bobRate = 0.25f;
    if (s.bobRate > 3.f) s.bobRate = 3.f;
    float mb = IniFloat(ini, L"muzzle_bone", static_cast<float>(s.muzzleBone));
    s.muzzleBone = (mb >= -2 && mb < static_cast<float>(kMaxBones)) ? static_cast<int>(mb) : -2;
    IniVec3(ini, L"muzzle_local", s.muzzleLocal);
    float sb = IniFloat(ini, L"ads_sight_bone", static_cast<float>(s.adsSightBone));
    s.adsSightBone = (sb >= 0 && sb < static_cast<float>(kMaxBones)) ? static_cast<int>(sb) : -1;
    IniVec3(ini, L"ads_sight_point", s.adsSightPoint);
    IniVec3(ini, L"ads_sight_front", s.adsSightFront);
    s.scopeZoom = IniFloat(ini, L"scope_zoom", s.scopeZoom);
    if (!(s.scopeZoom >= 0.f && s.scopeZoom <= 40.f)) s.scopeZoom = 0.f;
    float scb = IniFloat(ini, L"scope_bone", static_cast<float>(s.scopeBone));
    s.scopeBone = (scb >= 0 && scb < static_cast<float>(kMaxBones)) ? static_cast<int>(scb) : -1;
    s.scopeLensRadius = IniFloat(ini, L"scope_lens_radius", s.scopeLensRadius);
    if (!(s.scopeLensRadius > 0.001f && s.scopeLensRadius < 0.2f)) s.scopeLensRadius = 0.0178f;
    s.scopeReticle = IniFloat(ini, L"scope_reticle", s.scopeReticle);
    {
        wchar_t buf[128];
        GetPrivateProfileStringW(L"viewmodel", L"scope_levels", L"", buf, 128, ini);
        float lv[8] = {};
        int n = 0;
        bool ok = buf[0] != 0;
        for (wchar_t* p = buf; ok && *p;) {
            wchar_t* end = nullptr;
            double x = wcstod(p, &end);
            if (end == p || !std::isfinite(x) || x < 1.0 || x > 40.0 || n >= 8 || (n > 0 && x <= lv[n - 1])) {
                ok = false;
                break;
            }
            lv[n++] = static_cast<float>(x);
            p = end;
            while (*p == L' ' || *p == L',' || *p == L'\t') ++p;
        }
        s.scopeLevelCount = ok ? n : 0;
        for (int k = 0; k < 8; ++k) s.scopeLevels[k] = k < s.scopeLevelCount ? lv[k] : 0.f;
        const float def = IniFloat(ini, L"scope_level_default", 0.f);
        int di = 0;
        for (int k = 1; k < s.scopeLevelCount; ++k)
            if (std::fabs(s.scopeLevels[k] - def) < std::fabs(s.scopeLevels[di] - def)) di = k;
        s.scopeLevelDefault = di;
    }
    s.handIk =IniFloat(ini, L"hand_ik", s.handIk ? 1.f : 0.f) != 0.f;
    IniVec3(ini, L"ik_grip_ri", s.ikGripRi);
    IniVec3(ini, L"ik_grip_le", s.ikGripLe);
    GetPrivateProfileStringW(L"viewmodel", L"fire_sound",s.fireSound, s.fireSound, 512, ini);
    s.fireVolume = IniFloat(ini, L"fire_volume", s.fireVolume);
    GetPrivateProfileStringW(L"viewmodel", L"fire_sound_ads", s.fireSoundAds, s.fireSoundAds, 512, ini);
    GetPrivateProfileStringW(L"viewmodel", L"fire_mech", s.fireMech, s.fireMech, 512, ini);
    GetPrivateProfileStringW(L"viewmodel", L"fire_mech_ads", s.fireMechAds, s.fireMechAds, 512, ini);
    s.fireMechVolume = IniFloat(ini, L"fire_mech_volume", s.fireMechVolume);
    GetPrivateProfileStringW(L"viewmodel", L"fire_lfe", s.fireLfe, s.fireLfe, 512, ini);
    s.fireLfeVolume = IniFloat(ini, L"fire_lfe_volume", s.fireLfeVolume);
    GetPrivateProfileStringW(L"viewmodel", L"fire_atmo", s.fireAtmo, s.fireAtmo, 512, ini);
    s.fireAtmoVolume = IniFloat(ini, L"fire_atmo_volume", s.fireAtmoVolume);
    s.fireAtmoMaxVoices = static_cast<int>(IniFloat(ini, L"fire_atmo_max_voices", static_cast<float>(s.fireAtmoMaxVoices)));
    GetPrivateProfileStringW(L"viewmodel", L"fire_tail", s.fireTail, s.fireTail, 512, ini);
    s.fireTailVolume = IniFloat(ini, L"fire_tail_volume", s.fireTailVolume);
    GetPrivateProfileStringW(L"viewmodel", L"fire_last_sound", s.fireLastSound, s.fireLastSound, 512, ini);
    s.fireLastVolume = IniFloat(ini, L"fire_last_volume", s.fireLastVolume);
    GetPrivateProfileStringW(L"viewmodel", L"fire_casing", s.fireCasing, s.fireCasing, 512, ini);
    GetPrivateProfileStringW(L"viewmodel", L"fire_casing_ads", s.fireCasingAds, s.fireCasingAds, 512, ini);
    s.fireCasingVolume = IniFloat(ini, L"fire_casing_volume", s.fireCasingVolume);
    GetPrivateProfileStringW(L"viewmodel", L"shell_land_sound", s.shellLandSound, s.shellLandSound, 512, ini);
    s.shellLandVolume = IniFloat(ini, L"shell_land_volume", s.shellLandVolume);
    s.shellLandMaxVoices = static_cast<int>(IniFloat(ini, L"shell_land_max_voices", static_cast<float>(s.shellLandMaxVoices)));
    s.shellLandPerSec = IniFloat(ini, L"shell_land_per_sec", s.shellLandPerSec);
    s.shellGround = IniFloat(ini, L"shell_ground", s.shellGround);
    s.fireSettle = IniFloat(ini, L"fire_settle", s.fireSettle ? 1.f : 0.f) != 0.f;
    IniVec2(ini, L"fire_stance_sync", s.fireStanceSync);
    if (!(s.fireLastVolume <= 2.f)) s.fireLastVolume = -1.f;
    if (!(s.fireCasingVolume >= 0.f && s.fireCasingVolume <= 2.f)) s.fireCasingVolume = 0.08f;
    if (!(s.shellLandVolume >= 0.f && s.shellLandVolume <= 2.f)) s.shellLandVolume = 0.04f;
    if (s.shellLandMaxVoices < 1 || s.shellLandMaxVoices > 24) s.shellLandMaxVoices = 4;
    if (!(s.shellLandPerSec >= 0.f && s.shellLandPerSec <= 60.f)) s.shellLandPerSec = 6.f;
    if (!(s.shellGround >= 0.1f && s.shellGround <= 10.f)) s.shellGround = 1.75f;
    s.fireTailGap = IniFloat(ini, L"fire_tail_gap", s.fireTailGap);
    s.fireSync = IniFloat(ini, L"fire_sync", s.fireSync ? 1.f : 0.f) != 0.f;
    s.meleeStrikeLead = IniFloat(ini, L"melee_strike_lead", s.meleeStrikeLead);
    if (!(s.meleeStrikeLead >= 0.f && s.meleeStrikeLead <= 0.6f)) s.meleeStrikeLead = 0.25f;
    {
        wchar_t buf[128];
        GetPrivateProfileStringW(L"viewmodel", L"wh3_shader", L"\x01", buf, 128, ini);
        if (buf[0] != 1) {
            static const struct {
                const wchar_t* name;
                int emissive;
                bool iridescent;
            } kWh3Shaders[] = {{L"rigid_character_emissive", 1, false},
                               {L"rigid_character_prop_emissive", 2, false},
                               {L"rigid_character_iridescent", 0, true},
                               {L"rigid_character_iridescent_emissive", 1, true},
                               {L"rigid_character_iridescent_emissive_alpha", 1, true}};
            s.wh3Emissive = 0;
            s.wh3Iridescent = false;
            for (const auto& k : kWh3Shaders)
                if (!_wcsicmp(buf, k.name)) s.wh3Emissive = k.emissive, s.wh3Iridescent = k.iridescent;
        }
        GetPrivateProfileStringW(L"viewmodel", L"wh3_part", s.wh3Part, s.wh3Part, 256, ini);
        GetPrivateProfileStringW(L"viewmodel", L"wh3_mask", s.wh3Mask, s.wh3Mask, 260, ini);
        GetPrivateProfileStringW(L"viewmodel", L"emissive_texture", s.emissiveTexture, s.emissiveTexture, 260, ini);
        GetPrivateProfileStringW(L"viewmodel", L"emissive_distortion", s.emissiveDistortion, s.emissiveDistortion, 260, ini);
        s.emissiveStrength = IniFloat(ini, L"emissive_strength", s.emissiveStrength);
        IniVec3(ini, L"emissive_tint", s.emissiveTint);
        float t[2];
        GetPrivateProfileStringW(L"viewmodel", L"emissive_speed", L"", buf, 128, ini);
        int n = buf[0] ? swscanf(buf, L"%f , %f", &t[0], &t[1]) : 0;
        if (n == 1) t[1] = t[0];
        if (n >= 1 && std::isfinite(t[0]) && std::isfinite(t[1])) s.emissiveSpeed[0] = t[0], s.emissiveSpeed[1] = t[1];
        IniVec2(ini, L"emissive_direction", s.emissiveDirection);
        s.emissiveDistort = IniFloat(ini, L"emissive_distort_strength", s.emissiveDistort);
        s.emissiveDistort = IniFloat(ini, L"emissive_distortion_scale", s.emissiveDistort);
        IniVec2(ini, L"emissive_tiling", s.emissiveTiling);
        float tt = IniFloat(ini, L"emissive_texture_tiling", -1e30f);
        if (tt > -1e29f) s.emissiveTiling[0] = s.emissiveTiling[1] = tt;
        s.emissiveFresnelStrength = IniFloat(ini, L"emissive_fresnel_strength", s.emissiveFresnelStrength);
        s.emissivePulseSpeed = IniFloat(ini, L"emissive_pulse_speed", s.emissivePulseSpeed);
        s.emissivePulseStrength = IniFloat(ini, L"emissive_pulse_strength", s.emissivePulseStrength);
        for (int k = 0; k < 4; ++k) {
            wchar_t key[64];
            swprintf(key, 64, L"emissive_gradient_stop_%d", k + 1);
            s.emissiveGradientStop[k] = IniFloat(ini, key, s.emissiveGradientStop[k]);
            swprintf(key, 64, L"emissive_gradient_colour_stop_%d", k + 1);
            IniVec3(ini, key, s.emissiveGradientColour[k]);
        }
        s.iridescenceAmount = IniFloat(ini, L"iridescence_amount", s.iridescenceAmount);
        IniVec3(ini, L"iridescence_colour_a", s.iridescenceColourA);
        IniVec3(ini, L"iridescence_colour_b", s.iridescenceColourB);
        s.emissiveScale = IniFloat(ini, L"emissive_scale", s.emissiveScale);
        if (!(s.emissiveScale >= 0.f && s.emissiveScale <= 100.f)) s.emissiveScale = 1.f;
        s.emissiveSoft = IniFloat(ini, L"emissive_soft", s.emissiveSoft ? 1.f : 0.f) != 0.f;
    }
    for (int i = 0; i < kMeleeSfx; ++i) {
        wchar_t key[64];
        swprintf(key, 64, L"melee_sfx_%ls", kMeleeSfxNames[i]);
        GetPrivateProfileStringW(L"viewmodel", key, s.meleeSfx[i], s.meleeSfx[i], 512, ini);
        swprintf(key, 64, L"melee_sfx_%ls_volume", kMeleeSfxNames[i]);
        s.meleeSfxVolume[i] = IniFloat(ini, key, s.meleeSfxVolume[i]);
        swprintf(key, 64, L"melee_sfx_%ls_pitch", kMeleeSfxNames[i]);
        wchar_t pv[64] = L"";
        GetPrivateProfileStringW(L"viewmodel", key, L"", pv, 64, ini);
        float lo = 0.f, hi = 0.f;
        if (pv[0] && swscanf(pv, L"%f , %f", &lo, &hi) == 2 && lo >= -1200.f && hi <= 1200.f && lo <= hi)
            s.meleeSfxPitch[i][0] = lo, s.meleeSfxPitch[i][1] = hi;
    }
    s.meleeSfxMaster = IniFloat(ini, L"melee_sfx_volume", s.meleeSfxMaster);
    s.meleeSfxSwingCut = IniFloat(ini, L"melee_sfx_swing_cut", s.meleeSfxSwingCut ? 1.f : 0.f) != 0.f;
    s.meleeSfxSwingPre = IniFloat(ini, L"melee_sfx_swing_pre", s.meleeSfxSwingPre);
    if (!(s.meleeSfxSwingPre <= 1.f)) s.meleeSfxSwingPre = -1.f;
    s.fireRpm = IniFloat(ini, L"fire_rpm", s.fireRpm);
    s.magSize = static_cast<int>(IniFloat(ini, L"mag_size", static_cast<float>(s.magSize)));
    s.magDisposable = IniFloat(ini, L"mag_disposable", s.magDisposable ? 1.f : 0.f) != 0.f;
    s.ammoTotal = static_cast<int>(IniFloat(ini, L"ammo_total", static_cast<float>(s.ammoTotal)));
    s.ammoUpgrade = static_cast<int>(IniFloat(ini, L"ammo_upgrade", static_cast<float>(s.ammoUpgrade)));
    if (s.ammoUpgrade < 0 || s.ammoUpgrade > 10000) s.ammoUpgrade = 0;
    s.reloadRefill = IniFloat(ini, L"reload_refill", s.reloadRefill);
    s.reloadEmptyRefill = IniFloat(ini, L"reload_empty_refill", s.reloadEmptyRefill);
    GetPrivateProfileStringW(L"viewmodel", L"weapon_id", s.weaponId, s.weaponId, 64, ini);
    GetPrivateProfileStringW(L"viewmodel", L"anim_sounds", s.animSounds, s.animSounds, 128, ini);
    s.animSoundVolume = IniFloat(ini, L"anim_sound_volume", s.animSoundVolume);
    GetPrivateProfileStringW(L"viewmodel", L"step_foot", s.stepFoot, s.stepFoot, 512, ini);
    GetPrivateProfileStringW(L"viewmodel", L"step_cloth_run", s.stepClothRun, s.stepClothRun, 512, ini);
    GetPrivateProfileStringW(L"viewmodel", L"step_cloth_sprint", s.stepClothSprint, s.stepClothSprint, 512, ini);
    GetPrivateProfileStringW(L"viewmodel", L"step_rattle", s.stepRattle, s.stepRattle, 512, ini);
    s.stepFootVolume = IniFloat(ini, L"step_foot_volume", s.stepFootVolume);
    s.stepClothVolume = IniFloat(ini, L"step_cloth_volume", s.stepClothVolume);
    s.stepRattleVolume = IniFloat(ini, L"step_rattle_volume", s.stepRattleVolume);
    s.stepSprintGain = IniFloat(ini, L"step_sprint_gain", s.stepSprintGain);
    s.stepTacSprintGain = IniFloat(ini, L"step_tac_sprint_gain", s.stepTacSprintGain);
    s.stepAdsGain = IniFloat(ini, L"step_ads_gain", s.stepAdsGain);
    auto fracList = [&](const wchar_t* key, float* out, int& n) {
        wchar_t buf[256] = L"";
        GetPrivateProfileStringW(L"viewmodel", key, L"", buf, 256, ini);
        if (!buf[0]) return;
        n = 0;
        for (wchar_t* p = buf; *p && n < 16;) {
            wchar_t* e = nullptr;
            float v = static_cast<float>(wcstod(p, &e));
            if (e == p) break;
            if (v >= 0.f && v < 1.f) out[n++] = v;
            p = e;
            while (*p == L',' || *p == L' ' || *p == L';') ++p;
        }
    };
    fracList(L"sprint_steps", s.sprintSteps, s.sprintStepN);
    s.sprintStepTime = IniFloat(ini, L"sprint_step_time", s.sprintStepTime);
    s.tacSprintStepTime = IniFloat(ini, L"tac_sprint_step_time", s.tacSprintStepTime);
    if (!(s.sprintStepTime >= 0.f && s.sprintStepTime <= 2.f)) s.sprintStepTime = 0.f;
    if (!(s.tacSprintStepTime >= 0.f && s.tacSprintStepTime <= 2.f)) s.tacSprintStepTime = 0.f;
    {
        wchar_t fm[32] = L"";
        GetPrivateProfileStringW(L"viewmodel", L"fire_mode", L"", fm, 32, ini);
        if (fm[0]) s.fireMode = !_wcsicmp(fm, L"spin") ? 1 : 0;
    }
    s.spinUpTime = IniFloat(ini, L"spin_up_time", s.spinUpTime);
    s.spinKeep = IniFloat(ini, L"spin_keep", s.spinKeep);
    GetPrivateProfileStringW(L"viewmodel", L"spin_up_sound", s.spinUpSound, s.spinUpSound, 512, ini);
    GetPrivateProfileStringW(L"viewmodel", L"spin_loop_sound", s.spinLoopSound, s.spinLoopSound, 512, ini);
    GetPrivateProfileStringW(L"viewmodel", L"spin_down_sound", s.spinDownSound, s.spinDownSound, 512, ini);
    s.spinVolume = IniFloat(ini, L"spin_volume", s.spinVolume);
    s.spinLoopStart = IniFloat(ini, L"spin_loop_start", s.spinLoopStart);
    if (!(s.spinLoopStart >= 0.f && s.spinLoopStart <= 30.f)) s.spinLoopStart = 0.f;
    s.spinBarrels = IniFloat(ini, L"spin_barrels", s.spinBarrels);
    if (!(s.spinBarrels >= 1.f && s.spinBarrels <= 24.f)) s.spinBarrels = 6.f;
    s.spinDownTime = IniFloat(ini, L"spin_down_time", s.spinDownTime);
    if (!(s.spinDownTime >= 0.05f && s.spinDownTime <= 10.f)) s.spinDownTime = 1.6f;
    s.adsTime = IniFloat(ini, L"ads_time", s.adsTime);
    s.adsTimeSprint = IniFloat(ini, L"ads_time_sprint", s.adsTimeSprint);
    s.adsTimeTac = IniFloat(ini, L"ads_time_tac", s.adsTimeTac);
    for (float* v : {&s.adsTime, &s.adsTimeSprint, &s.adsTimeTac})
        if (!(*v == 0.f || (*v >= 0.01f && *v <= 2.f))) *v = 0.f;
    {
        wchar_t buf[128];
        GetPrivateProfileStringW(L"viewmodel", L"ads_timing", L"", buf, 128, ini);
        if (!buf[0]) GetPrivateProfileStringW(L"viewmodel", L"ads_curve_time", L"", buf, 128, ini);
        float t[4];
        if (buf[0]) {
            bool ok = swscanf(buf, L"%f , %f , %f , %f", &t[0], &t[1], &t[2], &t[3]) == 4;
            for (float x : t) ok = ok && x >= 10.f && x <= 2000.f;
            for (int k = 0; k < 4; ++k) s.adsCurveTime[k] = ok ? t[k] : 0.f;
        }
    }
    s.adsFovAlt = IniFloat(ini, L"ads_fov", s.adsFovAlt);
    s.adsFovScaling = IniFloat(ini, L"ads_fov_scaling", s.adsFovScaling ? 1.f : 0.f) != 0.f;
    s.hipSpreadMin = IniFloat(ini, L"hip_spread_min", s.hipSpreadMin);
    s.hipSpreadMax = IniFloat(ini, L"hip_spread_max", s.hipSpreadMax);
    if (!(s.hipSpreadMin >= 0.f && s.hipSpreadMin <= 30.f)) s.hipSpreadMin = 0.f;
    if (!(s.hipSpreadMax >= s.hipSpreadMin && s.hipSpreadMax <= 30.f)) s.hipSpreadMax = s.hipSpreadMin;
    s.crossHip = IniFloat(ini, L"crosshair_hip", s.crossHip ? 1.f : 0.f) != 0.f;
    s.crossAds = IniFloat(ini, L"crosshair_ads", s.crossAds ? 1.f : 0.f) != 0.f;
    s.crossScale = IniFloat(ini, L"crosshair_scale", s.crossScale);
    if (!(s.crossScale >= 0.1f && s.crossScale <= 5.f)) s.crossScale = 1.f;
    s.adsVmFovAlt = IniFloat(ini, L"ads_vm_fov", s.adsVmFovAlt);
    for (float* v : {&s.adsFovAlt, &s.adsVmFovAlt})
        if (!(*v == 0.f || (*v >= 5.f && *v <= 150.f))) *v = 0.f;
    float spb = IniFloat(ini, L"spin_bone", static_cast<float>(s.spinBone));
    s.spinBone = (spb >= -2 && spb < static_cast<float>(kMaxBones)) ? static_cast<int>(spb) : -2;
    fracList(L"tac_sprint_steps", s.tacSprintSteps, s.tacSprintStepN);
    s.weaponDefault = IniFloat(ini, L"weapon_default", s.weaponDefault ? 1.f : 0.f) != 0.f;
    {
        wchar_t pw[64] = L"";
        GetPrivateProfileStringW(L"viewmodel", L"projectile", L"", pw, 64, ini);
        if (pw[0] && WideCharToMultiByte(CP_UTF8, 0, pw, -1, s.projectile, sizeof s.projectile, nullptr, nullptr) <= 0) s.projectile[0] = 0;
    }
    s.fireRange = IniFloat(ini, L"fire_range", s.fireRange);
    if (!(s.fireRange == 0.f || (s.fireRange >= 5.f && s.fireRange <= 2000.f))) s.fireRange = 0.f;
    {
        wchar_t pw[64] = L"";
        GetPrivateProfileStringW(L"viewmodel", L"projectile_ads", L"", pw, 64, ini);
        if (pw[0] && WideCharToMultiByte(CP_UTF8, 0, pw, -1, s.projectileAds, sizeof s.projectileAds, nullptr, nullptr) <= 0) s.projectileAds[0] = 0;
    }
    s.fireRangeAds = IniFloat(ini, L"fire_range_ads", s.fireRangeAds);
    if (!(s.fireRangeAds == 0.f || (s.fireRangeAds >= 5.f && s.fireRangeAds <= 2000.f))) s.fireRangeAds = 0.f;
    s.pellets = static_cast<int>(IniFloat(ini, L"pellets", static_cast<float>(s.pellets)) + 0.5f);
    s.pelletsAds = static_cast<int>(IniFloat(ini, L"pellets_ads", static_cast<float>(s.pelletsAds)) + 0.5f);
    if (s.pellets < 1 || s.pellets > 8) s.pellets = 1;
    if (s.pelletsAds < 1 || s.pelletsAds > 8) s.pelletsAds = 1;
    s.pelletFan = IniFloat(ini, L"pellet_fan", s.pelletFan);
    s.pelletFanAds = IniFloat(ini, L"pellet_fan_ads", s.pelletFanAds);
    if (!(s.pelletFan >= 0.f && s.pelletFan <= 60.f)) s.pelletFan = 0.f;
    if (!(s.pelletFanAds >= 0.f && s.pelletFanAds <= 60.f)) s.pelletFanAds = 0.f;
    s.burst = IniFloat(ini, L"burst", static_cast<float>(s.burst)) != 0.f ? 1 : 0;
    s.burstMin = IniFloat(ini, L"burst_min", s.burstMin);
    if (!(s.burstMin >= 0.5f && s.burstMin <= 20.f)) s.burstMin = 2.f;
    s.burstScatterAds = IniFloat(ini, L"burst_scatter_ads", s.burstScatterAds);
    if (!(s.burstScatterAds >= 0.f && s.burstScatterAds <= 10.f)) s.burstScatterAds = 0.f;
    s.burstPierceAds = static_cast<int>(IniFloat(ini, L"burst_pierce_ads", static_cast<float>(s.burstPierceAds)) + 0.5f);
    if (s.burstPierceAds < 0 || s.burstPierceAds > 64) s.burstPierceAds = 0;
    s.burstReachAds = IniFloat(ini, L"burst_reach_ads", s.burstReachAds);
    if (!(s.burstReachAds >= 0.55f && s.burstReachAds <= 5.f)) s.burstReachAds = 0.55f;
    s.burstCover = IniFloat(ini, L"burst_cover", s.burstCover);
    if (!(s.burstCover == 0.f || (s.burstCover >= 0.05f && s.burstCover <= 2.f))) s.burstCover = 0.f;
    s.burstCoverMax = static_cast<int>(IniFloat(ini, L"burst_cover_max", static_cast<float>(s.burstCoverMax)) + 0.5f);
    if (s.burstCoverMax < 1 || s.burstCoverMax > 128) s.burstCoverMax = 64;
    if (!(s.fireLfeVolume >= 0.f && s.fireLfeVolume <= 2.f)) s.fireLfeVolume = 0.7f;
    if (!(s.fireAtmoVolume >= 0.f && s.fireAtmoVolume <= 2.f)) s.fireAtmoVolume = 0.7f;
    if (!(s.fireTailVolume >= 0.f && s.fireTailVolume <= 2.f)) s.fireTailVolume = 0.7f;
    if (s.fireAtmoMaxVoices < 1 || s.fireAtmoMaxVoices > 24) s.fireAtmoMaxVoices = 4;
    if (!(s.fireTailGap >= 0.02f && s.fireTailGap <= 2.f)) s.fireTailGap = 0.15f;
    s.fireMaxVoices = static_cast<int>(IniFloat(ini, L"fire_max_voices", static_cast<float>(s.fireMaxVoices)));
    if (!(s.fireMechVolume >= 0.f && s.fireMechVolume <= 2.f)) s.fireMechVolume = 0.7f;
    if (s.fireMaxVoices < 1 || s.fireMaxVoices > 24) s.fireMaxVoices = 24;
    IniVec3(ini, L"muzzle_axis", s.muzzleAxis);
    IniVec3(ini, L"muzzle_up", s.muzzleUp);
    GetPrivateProfileStringW(L"viewmodel", L"flash_fx", s.flashFx, s.flashFx, 128, ini);
    GetPrivateProfileStringW(L"viewmodel", L"effect_fx", s.effectFx, s.effectFx, 128, ini);
    s.flashScale = IniFloat(ini, L"flash_scale", s.flashScale);
    s.flashIntensity = IniFloat(ini, L"flash_intensity", s.flashIntensity);
    s.flashLight = IniFloat(ini, L"flash_light", s.flashLight);
    s.flashSmoke = IniFloat(ini, L"flash_smoke", s.flashSmoke ? 1.f : 0.f) != 0.f;
    s.flashFlame = IniFloat(ini, L"flash_flame", s.flashFlame ? 1.f : 0.f) != 0.f;
    GetPrivateProfileStringW(L"viewmodel", L"flame_fx", s.flameFx, s.flameFx, 128, ini);
    s.flameAim = IniFloat(ini, L"flame_aim", s.flameAim);
    s.flameGap = IniFloat(ini, L"flame_gap", s.flameGap);
    if (!(s.flameAim >= 2.f && s.flameAim <= 500.f)) s.flameAim = 30.f;
    if (!(s.flameGap >= 0.02f && s.flameGap <= 1.f)) s.flameGap = 0.09f;
    s.flameFan = IniFloat(ini, L"flame_fan", s.flameFan);
    s.flameFanAds = IniFloat(ini, L"flame_fan_ads", s.flameFanAds);
    s.flameAimAds = IniFloat(ini, L"flame_aim_ads", s.flameAimAds);
    if (!(s.flameFan >= 0.f && s.flameFan <= 80.f)) s.flameFan = 0.f;
    if (!(s.flameFanAds >= 0.f && s.flameFanAds <= 80.f)) s.flameFanAds = 0.f;
    if (!(s.flameAimAds == 0.f || (s.flameAimAds >= 2.f && s.flameAimAds <= 500.f))) s.flameAimAds = 0.f;
    for (int li = 0; li < 4; ++li) {
        Settings::FlameLayer& L = s.flame[li];
        wchar_t k[48];
        auto key = [&](const wchar_t* n) {
            swprintf(k, 48, L"flame%d_%ls", li + 1, n);
            return k;
        };
        GetPrivateProfileStringW(L"viewmodel", key(L"tex"), L.tex, L.tex, 64, ini);
        float c2[2] = {static_cast<float>(L.cx), static_cast<float>(L.cy)};
        IniVec2(ini, key(L"cells"), c2);
        L.cx = std::max(1, std::min(32, static_cast<int>(c2[0] + 0.5f)));
        L.cy = std::max(1, std::min(32, static_cast<int>(c2[1] + 0.5f)));
        L.fps = IniFloat(ini, key(L"fps"), L.fps);
        L.randFrame = IniFloat(ini, key(L"rand_frame"), L.randFrame ? 1.f : 0.f) != 0.f;
        L.rate = IniFloat(ini, key(L"rate"), L.rate);
        IniVec2(ini, key(L"speed"), L.speed);
        IniVec2(ini, key(L"life"), L.life);
        IniVec2(ini, key(L"size"), L.size);
        L.sizeEase = IniFloat(ini, key(L"size_ease"), L.sizeEase);
        L.gravity = IniFloat(ini, key(L"gravity"), L.gravity);
        L.drag = IniFloat(ini, key(L"drag"), L.drag);
        L.spread = IniFloat(ini, key(L"spread"), L.spread);
        L.jitter = IniFloat(ini, key(L"jitter"), L.jitter);
        IniVec3(ini, key(L"color"), L.col0);
        memcpy(L.col1, L.col0, sizeof L.col1);
        IniVec3(ini, key(L"color_end"), L.col1);
        L.alpha = IniFloat(ini, key(L"alpha"), L.alpha);
        IniVec2(ini, key(L"fade"), L.fade);
        L.bright = IniFloat(ini, key(L"bright"), L.bright);
        L.stretch = IniFloat(ini, key(L"stretch"), L.stretch);
        L.spin = IniFloat(ini, key(L"spin"), L.spin);
        L.additive = IniFloat(ini, key(L"additive"), L.additive ? 1.f : 0.f) != 0.f;
        if (!(L.rate >= 0.f && L.rate <= 3000.f)) L.rate = 0.f;
        for (int j = 0; j < 2; ++j) {
            if (!(L.life[j] >= 0.02f && L.life[j] <= 20.f)) L.life[j] = 1.f;
            if (!(L.speed[j] >= 0.f && L.speed[j] <= 300.f)) L.speed[j] = 20.f;
            if (!(L.size[j] >= 0.f && L.size[j] <= 50.f)) L.size[j] = 0.5f;
            L.fade[j] = std::min(std::max(L.fade[j], 0.f), 1.f);
        }
        if (!(L.fps >= 0.f && L.fps <= 500.f)) L.fps = 0.f;
        if (!(L.sizeEase >= 0.f && L.sizeEase <= 20.f)) L.sizeEase = 2.f;
        if (!(L.spread >= 0.f && L.spread <= 90.f)) L.spread = 2.f;
        IniVec2(ini, key(L"speed_ads"), L.speedAds);
        IniVec2(ini, key(L"life_ads"), L.lifeAds);
        IniVec2(ini, key(L"size_ads"), L.sizeAds);
        L.rateAds = IniFloat(ini, key(L"rate_ads"), L.rateAds);
        L.spreadAds = IniFloat(ini, key(L"spread_ads"), L.spreadAds);
        L.dragAds = IniFloat(ini, key(L"drag_ads"), L.dragAds);
        IniVec2(ini, key(L"fade_ads"), L.fadeAds);
        L.alphaAds = IniFloat(ini, key(L"alpha_ads"), L.alphaAds);
        L.brightAds = IniFloat(ini, key(L"bright_ads"), L.brightAds);
        L.stretchAds = IniFloat(ini, key(L"stretch_ads"), L.stretchAds);
        if (!(L.stretchAds >= 0.f && L.stretchAds <= 1.f)) L.stretchAds = -1.f;
        for (int j = 0; j < 2; ++j) {
            if (!(L.speedAds[j] >= 0.f && L.speedAds[j] <= 300.f)) L.speedAds[j] = -1.f;
            if (!(L.lifeAds[j] >= 0.02f && L.lifeAds[j] <= 20.f)) L.lifeAds[j] = -1.f;
            if (!(L.sizeAds[j] >= 0.f && L.sizeAds[j] <= 50.f)) L.sizeAds[j] = -1.f;
            if (!(L.fadeAds[j] >= 0.f && L.fadeAds[j] <= 1.f)) L.fadeAds[j] = -1.f;
        }
        if (!(L.rateAds >= 0.f && L.rateAds <= 3000.f)) L.rateAds = -1.f;
        if (!(L.spreadAds >= 0.f && L.spreadAds <= 90.f)) L.spreadAds = -1.f;
        if (!(L.dragAds >= 0.f && L.dragAds <= 50.f)) L.dragAds = -1.f;
        if (!(L.alphaAds >= 0.f && L.alphaAds <= 4.f)) L.alphaAds = -1.f;
        if (!(L.brightAds >= 0.f && L.brightAds <= 20.f)) L.brightAds = -1.f;
    }
    s.hitFx = IniFloat(ini, L"hit_fx", s.hitFx ? 1.f : 0.f) != 0.f;
    s.hitFxScale = IniFloat(ini, L"hit_fx_scale", s.hitFxScale);
    s.hitFxAlpha = IniFloat(ini, L"hit_fx_alpha", s.hitFxAlpha);
    IniVec3(ini, L"hit_fx_color", s.hitFxColor);
    IniVec3(ini, L"tracer_tint", s.tracerTint);
    for (float& c : s.tracerTint)
        if (!(c >= 0.f && c <= 8.f)) c = 1.f;
    s.flashGas =IniFloat(ini, L"flash_gas", s.flashGas ? 1.f : 0.f) != 0.f;
    GetPrivateProfileStringW(L"viewmodel", L"flash_gas_fx", s.flashGasFx, s.flashGasFx, 128, ini);
    s.flashGasScale = IniFloat(ini, L"flash_gas_scale", s.flashGasScale);
    s.flashGasAlpha = IniFloat(ini, L"flash_gas_alpha", s.flashGasAlpha);
    s.flashGasDistort = IniFloat(ini, L"flash_gas_distort", s.flashGasDistort);
    s.flashGasBarrelSmoke = IniFloat(ini, L"flash_gas_barrel_smoke", s.flashGasBarrelSmoke);
    s.flashGasBarrelSmokeLife = IniFloat(ini, L"flash_gas_barrel_smoke_life", s.flashGasBarrelSmokeLife);
    s.flashGasBarrelSmokeFade = IniFloat(ini, L"flash_gas_barrel_smoke_fade", s.flashGasBarrelSmokeFade);
    if (!(s.flashGasBarrelSmokeLife >= 0.2f && s.flashGasBarrelSmokeLife <= 3.f)) s.flashGasBarrelSmokeLife = 1.f;
    if (!(s.flashGasBarrelSmokeFade >= 0.05f && s.flashGasBarrelSmokeFade <= 3.f)) s.flashGasBarrelSmokeFade = 0.9f;
    s.flashGasLinger = IniFloat(ini, L"flash_gas_linger", s.flashGasLinger);
    if (!(s.flashGasLinger >= 0.f && s.flashGasLinger <= 5.f)) s.flashGasLinger = 1.f;
    s.flashGasStyle = static_cast<int>(IniFloat(ini, L"flash_gas_style", static_cast<float>(s.flashGasStyle)));
    if (s.flashGasStyle < 0 || s.flashGasStyle > 1) s.flashGasStyle = 1;
    s.flashGasSidePuff = IniFloat(ini, L"flash_gas_side_puff", s.flashGasSidePuff);
    if (!(s.flashGasSidePuff >= 0.f && s.flashGasSidePuff <= 5.f)) s.flashGasSidePuff = 1.f;
    s.flashGasAds = IniFloat(ini, L"flash_gas_ads", s.flashGasAds);
    if (!(s.flashGasAds >= 0.f && s.flashGasAds <= 1.f)) s.flashGasAds = 1.f;
    s.shellEject = IniFloat(ini, L"shell_eject", s.shellEject ? 1.f : 0.f) != 0.f;
    s.shellEjectAnim = IniFloat(ini, L"shell_eject_anim", s.shellEjectAnim ? 1.f : 0.f) != 0.f;
    s.shellScale = IniFloat(ini, L"shell_scale", s.shellScale);
    s.shellSpeed = IniFloat(ini, L"shell_speed", s.shellSpeed);
    s.shellLife = IniFloat(ini, L"shell_life", s.shellLife);
    s.shellSmoke = IniFloat(ini, L"shell_smoke", s.shellSmoke);
    s.shellSparks = IniFloat(ini, L"shell_sparks", s.shellSparks);
    if (!(s.shellScale >= 0.2f && s.shellScale <= 5.f)) s.shellScale = 1.f;
    if (!(s.shellSpeed >= 0.f && s.shellSpeed <= 5.f)) s.shellSpeed = 1.f;
    if (!(s.shellLife >= 0.1f && s.shellLife <= 5.f)) s.shellLife = 1.f;
    if (!(s.shellSmoke >= 0.f && s.shellSmoke <= 5.f)) s.shellSmoke = 1.f;
    if (!(s.shellSparks >= 0.f && s.shellSparks <= 5.f)) s.shellSparks = 1.f;
    s.shellFollow = static_cast<int>(IniFloat(ini, L"shell_follow", static_cast<float>(s.shellFollow)));
    if (s.shellFollow < 0 || s.shellFollow > 1) s.shellFollow = 1;
    s.shellCaliber = static_cast<int>(IniFloat(ini, L"shell_caliber", static_cast<float>(s.shellCaliber)));
    if (s.shellCaliber != 556 && s.shellCaliber != 762) s.shellCaliber = 556;
    IniVec2(ini, L"shell_out", s.shellOut);
    IniVec2(ini, L"shell_fwd", s.shellFwd);
    IniVec2(ini, L"shell_up", s.shellUp);
    IniVec2(ini, L"shell_spin", s.shellSpin);
    IniVec2(ini, L"shell_lifetime", s.shellLifeRange);
    s.shellGravity = IniFloat(ini, L"shell_gravity", s.shellGravity);
    s.shellLink = IniFloat(ini, L"shell_link", s.shellLink ? 1.f : 0.f) != 0.f;
    IniVec2(ini, L"link_out", s.linkOut);
    IniVec2(ini, L"link_fwd", s.linkFwd);
    IniVec2(ini, L"link_up", s.linkUp);
    IniVec2(ini, L"link_lifetime", s.linkLifeRange);
    s.linkGravity = IniFloat(ini, L"link_gravity", s.linkGravity);
    for (float* r : {s.shellLifeRange, s.linkLifeRange})
        if (!(r[0] > 0.05f && r[1] >= r[0] && r[1] <= 10.f)) r[0] = 1.2f, r[1] = 1.8f;
    if (!(s.shellGravity >= 0.f && s.shellGravity <= 5.f)) s.shellGravity = 1.f;
    if (!(s.linkGravity >= 0.f && s.linkGravity <= 5.f)) s.linkGravity = 1.4f;
    {
        const Settings d;
        s.shieldAllow = IniFloat(ini, L"shield_allow", s.shieldAllow ? 1.f : 0.f) != 0.f;
        IniVec3(ini, L"shield_pos", s.shieldPos);
        IniVec3(ini, L"shield_rot", s.shieldRot);
        IniVec3(ini, L"shield_ads_pos", s.shieldAdsPos);
        IniVec3(ini, L"shield_ads_rot", s.shieldAdsRot);
        IniVec3(ini, L"shield_stow_pos", s.shieldStowPos);
        IniVec3(ini, L"shield_stow_rot", s.shieldStowRot);
        IniVec2(ini, L"shield_size", s.shieldSize);
        IniVec3(ini, L"shield_grip", s.shieldGrip);
        IniVec3(ini, L"shield_wrist_rot", s.shieldWristRot);
        IniQuat(ini, L"shield_hand_quat", s.shieldHandQuat);
        IniQuat(ini, L"shield_ads_hand_quat", s.shieldAdsHandQuat);
        IniVec3(ini, L"shield_elbow", s.shieldElbow);
        IniVec3(ini, L"shield_ads_elbow", s.shieldAdsElbow);
        IniVec3(ini, L"shield_ads_grip", s.shieldAdsGrip);
        {
            int n = IniFloatList(ini, L"shield_finger_pose", s.shieldFingerPose, Settings::kShieldFingers * 4);
            if (n >= 0) s.shieldFingerN = n;
            n = IniFloatList(ini, L"shield_ads_finger_pose", s.shieldAdsFingerPose, Settings::kShieldFingers * 4);
            if (n >= 0) s.shieldAdsFingerN = n;
        }
        IniVec3(ini, L"shield_color", s.shieldColor);
        IniVec3(ini, L"shield_gun_shift", s.shieldGunShift);
        IniVec3(ini, L"shield_kick", s.shieldKick);
        IniVec3(ini, L"shield_notch", s.shieldNotch);
        IniVec3(ini, L"shield_rest_point", s.shieldRestPoint);
        {
            float rb = IniFloat(ini, L"shield_rest_bone", static_cast<float>(s.shieldRestBone));
            s.shieldRestBone = (rb >= -1 && rb < static_cast<float>(kMaxBones)) ? static_cast<int>(rb) : -1;
            IniVec3(ini, L"shield_rest_point2", s.shieldRestPoint2);
            rb = IniFloat(ini, L"shield_rest_bone2", static_cast<float>(s.shieldRestBone2));
            s.shieldRestBone2 = (rb >= -2 && rb < static_cast<float>(kMaxBones)) ? static_cast<int>(rb) : -2;
            s.shieldRestDrop2 = IniFloat(ini, L"shield_rest_drop2", s.shieldRestDrop2);
            if (!(s.shieldRestDrop2 >= 0.f && s.shieldRestDrop2 <= 0.2f)) s.shieldRestDrop2 = 0.f;
            s.shieldRestSide = IniFloat(ini, L"shield_rest_side", s.shieldRestSide);
            if (!(s.shieldRestSide >= -0.2f && s.shieldRestSide <= 0.2f)) s.shieldRestSide = 0.f;
        }
        struct F {
            const wchar_t* key;
            float* v;
            float lo, hi, def;
        } fs[] = {{L"shield_raise_time", &s.shieldRaiseTime, 0.05f, 3.f, d.shieldRaiseTime},
                  {L"shield_lower_time", &s.shieldLowerTime, 0.05f, 3.f, d.shieldLowerTime},
                  {L"shield_hit_shake", &s.shieldHitShake, 0.f, 30.f, d.shieldHitShake},
                  {L"shield_hit_shake_time", &s.shieldHitShakeTime, 0.01f, 2.f, d.shieldHitShakeTime},
                  {L"shield_hit_cam", &s.shieldHitCam, 0.f, 10.f, d.shieldHitCam},
                  {L"shield_move_scale", &s.shieldMoveScale, 0.1f, 1.f, d.shieldMoveScale},
                  {L"shield_ads_zoom", &s.shieldAdsZoom, 0.f, 1.f, d.shieldAdsZoom},
                  {L"shield_ads_time", &s.shieldAdsTime, 0.5f, 4.f, d.shieldAdsTime},
                  {L"shield_model_metal", &s.shieldModelMetal, 0.f, 1.f, d.shieldModelMetal},
                  {L"shield_block_hip", &s.shieldBlockHip, 0.f, 1.f, d.shieldBlockHip},
                  {L"shield_block_ads", &s.shieldBlockAds, 0.f, 1.f, d.shieldBlockAds},
                  {L"shield_rest", &s.shieldRest, 0.f, 1.f, d.shieldRest}};
        GetPrivateProfileStringW(L"viewmodel", L"shield_model", s.shieldModel, s.shieldModel, 520, ini);
        for (F& f : fs) {
            *f.v = IniFloat(ini, f.key, *f.v);
            if (!(*f.v >= f.lo && *f.v <= f.hi)) *f.v = f.def;
        }
        if (!(s.shieldSize[0] >= 0.15f && s.shieldSize[0] <= 1.2f && s.shieldSize[1] >= 0.15f && s.shieldSize[1] <= 1.6f))
            s.shieldSize[0] = d.shieldSize[0], s.shieldSize[1] = d.shieldSize[1];
        for (int k = 0; k < 3; ++k) {
            if (!(s.shieldKick[k] >= 0.1f && s.shieldKick[k] <= 5.f)) s.shieldKick[k] = d.shieldKick[k];
            s.shieldColor[k] = s.shieldColor[k] < 0.f ? 0.f : s.shieldColor[k] > 1.f ? 1.f : s.shieldColor[k];
        }
    }
    if (!(s.fireVolume >= 0.f && s.fireVolume <= 2.f)) s.fireVolume = 0.7f;
    if (!(s.flashScale > 0.01f && s.flashScale <= 20.f)) s.flashScale = 1.f;
    if (!(s.flashIntensity >= 0.f && s.flashIntensity <= 10.f)) s.flashIntensity = 1.f;
    if (!(s.flashLight >= 0.f && s.flashLight <= 20.f)) s.flashLight = 1.f;
    if (!(s.hitFxScale > 0.05f && s.hitFxScale <= 10.f)) s.hitFxScale = 1.f;
    if (!(s.hitFxAlpha >= 0.f && s.hitFxAlpha <= 1.f)) s.hitFxAlpha = 0.85f;
    for (float& c : s.hitFxColor)
        if (!(c >= 0.f && c <= 1.f)) c = 0.f;
    if (!(s.fovDeg > 10 && s.fovDeg < 150)) s.fovDeg = 55;
    if (!(s.scale > 0.001f && s.scale < 1000)) s.scale = 1;
    return true;
}
namespace {
bool ReadHvmV1(const wchar_t* path, unsigned char** out, size_t* outLen, char* err, size_t errLen) {
    unsigned char* data = nullptr;
    size_t len = 0;
    if (!ReadFileAll(path, &data, &len)) {
        SetErr(err, errLen, "model file missing: %ls", path);
        return false;
    }
    if (hvmslim::IsV3(data, len)) {
        char relU8[260];
        bool conv = hvmslim::V3BaseRel(data, len, relU8, sizeof relU8, err, errLen);
        unsigned char* base = nullptr;
        size_t baseLen = 0;
        if (conv) {
            wchar_t basePath[MAX_PATH * 2];
            wcsncpy(basePath, path, MAX_PATH - 1);
            basePath[MAX_PATH - 1] = 0;
            wchar_t* slash = wcsrchr(basePath, L'\\');
            if (!slash) slash = wcsrchr(basePath, L'/');
            if (slash) slash[1] = 0;
            else basePath[0] = 0;
            wchar_t relW[260];
            MultiByteToWideChar(CP_UTF8, 0, relU8, -1, relW, 260);
            wcsncat(basePath, relW, MAX_PATH * 2 - wcslen(basePath) - 1);
            if (!ReadFileAll(basePath, &base, &baseLen)) {
                SetErr(err, errLen, "hvm3: base model missing: %ls", basePath);
                conv = false;
            }
        }
        unsigned char* v2 = nullptr;
        size_t v2Len = 0;
        if (conv) conv = hvmslim::V3ToV2(data, len, base, baseLen, &v2, &v2Len, err, errLen);
        delete[] base;
        delete[] data;
        if (!conv) return false;
        data = v2;
        len = v2Len;
    }
    if (hvmslim::IsV2(data, len)) {
        unsigned char* v1 = nullptr;
        size_t v1Len = 0;
        bool conv = hvmslim::ToV1(data, len, &v1, &v1Len, err, errLen);
        delete[] data;
        if (!conv) return false;
        data = v1;
        len = v1Len;
    }
    *out = data;
    *outLen = len;
    return true;
}
}
#include "arms_override.inc"
bool Renderer::LoadHvm(const wchar_t* path, char* err, size_t errLen) {
    unsigned char* data = nullptr;
    size_t len = 0;
    if (!TakePrefetched(path, true, &data, &len) && !ReadHvmV1(path, &data, &len, err, errLen)) return false;
    {
        char note[300];
        if (ApplyArmsOverride(&data, &len, note, sizeof note) || note[0]) ArmsLogAppend(path, note);
    }
    bool ok = false;
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    do {
        if (len < sizeof(HvmHeader)) break;
        HvmHeader h;
        memcpy(&h, data, sizeof h);
        if (memcmp(h.magic, "HVM1", 4) || h.version != 1) {
            SetErr(err, errLen, "bad model header");
            break;
        }
        if (!h.vertexCount || !h.indexCount || !h.submeshCount || h.submeshCount > 256 || h.boneCount < 1 ||
            h.boneCount > kMaxBones || h.textureCount > kMaxTextures || h.clipCount > 512) {
            SetErr(err, errLen, "model counts out of range (v%u i%u s%u b%u t%u c%u)", h.vertexCount, h.indexCount,
                   h.submeshCount, h.boneCount, h.textureCount, h.clipCount);
            break;
        }
        size_t off = sizeof h;
        size_t need = off + h.textureCount * sizeof(HvmTexture) + h.submeshCount * sizeof(HvmSubmesh) +
                      h.boneCount * sizeof(HvmBone) + static_cast<size_t>(h.vertexCount) * kVertexStride +
                      static_cast<size_t>(h.indexCount) * 4;
        if (need > len) {
            SetErr(err, errLen, "model truncated");
            break;
        }
        wchar_t dir[MAX_PATH];
        wcsncpy(dir, path, MAX_PATH - 1);
        dir[MAX_PATH - 1] = 0;
        wchar_t* slash = wcsrchr(dir, L'\\');
        if (!slash) slash = wcsrchr(dir, L'/');
        if (slash) slash[1] = 0;
        wcsncpy(hvmDir_, dir, MAX_PATH - 1);
        bool texOk = true;
        for (uint32_t t = 0; t < h.textureCount; ++t) {
            HvmTexture tx;
            memcpy(&tx, data + off, sizeof tx);
            off += sizeof tx;
            tx.file[75] = 0;
            wchar_t rel[128];
            MultiByteToWideChar(CP_UTF8, 0, tx.file, -1, rel, 128);
            if (t < kMaxTextures) {
                const wchar_t* b = rel;
                for (const wchar_t* q = rel; *q; ++q)
                    if (*q == L'/' || *q == L'\\') b = q + 1;
                wcsncpy(texName_[t], b, 63);
                texName_[t][63] = 0;
                _wcslwr(texName_[t]);
            }
            wchar_t full[MAX_PATH * 2];
            swprintf(full, MAX_PATH * 2, L"%ls%ls", dir, rel);
            if (!LoadDds(dev_, full, tx.srgb != 0, &srv_[t], err, errLen)) {
                texOk = false;
                break;
            }
        }
        if (!texOk) break;
        texCount_ = h.textureCount;
        subs_ = new Submesh[h.submeshCount];
        subCount_ = h.submeshCount;
        lensSubs_ = 0;
        bool subOk = true;
        for (uint32_t s = 0; s < h.submeshCount; ++s) {
            HvmSubmesh hs;
            memcpy(&hs, data + off, sizeof hs);
            off += sizeof hs;
            if (static_cast<uint64_t>(hs.firstIndex) + hs.indexCount > h.indexCount) subOk = false;
            subs_[s].firstIndex = hs.firstIndex;
            subs_[s].indexCount = hs.indexCount;
            subs_[s].tex[0] = hs.albedo;
            subs_[s].tex[1] = hs.normal;
            subs_[s].tex[2] = hs.material;
            for (int k = 0; k < 3; ++k)
                if (subs_[s].tex[k] >= static_cast<int>(h.textureCount)) subs_[s].tex[k] = -1;
            subs_[s].normalMode = hs.normalMode;
            subs_[s].materialMode = hs.materialMode;
            subs_[s].flags = hs.flags;
            if ((hs.flags >> 19) & 1) ++lensSubs_;
        }
        if (!subOk) {
            SetErr(err, errLen, "submesh index range out of bounds");
            break;
        }
        bones_ = new Bone[h.boneCount];
        boneCount_ = h.boneCount;
        for (uint32_t b = 0; b < h.boneCount; ++b) {
            HvmBone hb;
            memcpy(&hb, data + off, sizeof hb);
            off += sizeof hb;
            bones_[b].parent = (hb.parent >= 0 && hb.parent < static_cast<int>(b)) ? hb.parent : -1;
            memcpy(bones_[b].bind, hb.bind, sizeof hb.bind);
            memcpy(bones_[b].invBind, hb.invBind, sizeof hb.invBind);
            if (!strncmp(hb.name, "j_rotating_barrels", sizeof hb.name)) barrelBone_ = static_cast<int>(b);
            if (!strncmp(hb.name, "tag_brass", sizeof hb.name)) brassBone_ = static_cast<int>(b);
            static const char* const kArm[2][3] = {{"j_shoulder_ri", "j_elbow_ri", "j_wrist_ri"}, {"j_shoulder_le", "j_elbow_le", "j_wrist_le"}};
            for (int sd = 0; sd < 2; ++sd)
                for (int k = 0; k < 3; ++k)
                    if (!strncmp(hb.name, kArm[sd][k], sizeof hb.name)) armBone_[sd][k] = static_cast<int>(b);
            if (!strncmp(hb.name, "j_gun", sizeof hb.name)) gunBone_ = static_cast<int>(b);
            static const char* const kShieldFinger[19] = {"6b0862c7234f1c7d", "c6175b0cc18dbea4", "10884e4f124801a1", "395c71f93fb4247f",
                                                          "j_index_le_1", "j_index_le_2", "j_index_le_3", "725b9714677d354e", "725b9614677d339b",
                                                          "725b9514677d31e8", "j_ring_le_1", "j_ring_le_2", "j_ring_le_3", "j_pinky_le_1",
                                                          "j_pinky_le_2", "j_pinky_le_3", "j_thumb_le_1", "j_thumb_le_2", "j_thumb_le_3"};
            for (int k = 0; k < 19; ++k)
                if (!strncmp(hb.name, kShieldFinger[k], sizeof hb.name)) fingerBone_[k] = static_cast<int>(b);
            if (!strncmp(hb.name, "2598db76b21b58e3", sizeof hb.name)) handTag_[0] = static_cast<int>(b);
            if (!strncmp(hb.name, "25c8bb76b2444645", sizeof hb.name)) handTag_[1] = static_cast<int>(b);
            {
                char nm[sizeof hb.name + 1];
                memcpy(nm, hb.name, sizeof hb.name);
                nm[sizeof hb.name] = 0;
                int num = 0, sd = TwistBoneSide(nm, &num);
                if (sd >= 0 && twistN_[sd] < kMaxTwist) twistBone_[sd][twistN_[sd]] = static_cast<int>(b), twistNum_[sd][twistN_[sd]++] = num;
            }
        }
        cameraBone_ = (h.cameraBone >= 0 && h.cameraBone < static_cast<int>(h.boneCount)) ? h.cameraBone : -1;
        TwistSetup();
        unsigned char* verts = data + off;
        for (uint32_t v = 0; v < h.vertexCount; ++v) {
            unsigned char* bi = verts + static_cast<size_t>(v) * kVertexStride + 48;
            for (int k = 0; k < 4; ++k)
                if (bi[k] >= h.boneCount) {
                    bi[k] = 0;
                    bi[4 + k] = 0;
                }
        }
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = h.vertexCount * kVertexStride;
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd = {verts, 0, 0};
        if (FAILED(dev_->CreateBuffer(&bd, &sd, &vb))) {
            SetErr(err, errLen, "vertex buffer");
            break;
        }
        off += static_cast<size_t>(h.vertexCount) * kVertexStride;
        uint32_t* idx = reinterpret_cast<uint32_t*>(data + off);
        bool idxOk = true;
        for (uint32_t k = 0; k < h.indexCount; ++k)
            if (idx[k] >= h.vertexCount) idxOk = false;
        if (!idxOk) {
            SetErr(err, errLen, "index out of range");
            break;
        }
        bd.ByteWidth = h.indexCount * 4;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        sd.pSysMem = idx;
        if (FAILED(dev_->CreateBuffer(&bd, &sd, &ib))) {
            SetErr(err, errLen, "index buffer");
            break;
        }
        off += static_cast<size_t>(h.indexCount) * 4;
        indexCount_ = h.indexCount;
        if (h.clipCount) {
            size_t totalFloats = 0;
            size_t scan = off;
            bool clipOk = true;
            for (uint32_t c = 0; c < h.clipCount; ++c) {
                if (scan + sizeof(HvmClip) > len) {
                    clipOk = false;
                    break;
                }
                HvmClip hc;
                memcpy(&hc, data + scan, sizeof hc);
                size_t floats = static_cast<size_t>(hc.frames) * h.boneCount * 10;
                if (!hc.frames || hc.frames > 100000 || !(hc.fps > 0) ||
                    scan + sizeof hc + floats * 4 > len) {
                    clipOk = false;
                    break;
                }
                scan += sizeof hc + floats * 4;
                totalFloats += floats;
            }
            if (!clipOk) {
                SetErr(err, errLen, "clip data truncated");
                break;
            }
            clips_ = new Clip[h.clipCount];
            clipData_ = new float[totalFloats];
            clipCount_ = h.clipCount;
            size_t at = 0;
            for (uint32_t c = 0; c < h.clipCount; ++c) {
                HvmClip hc;
                memcpy(&hc, data + off, sizeof hc);
                off += sizeof hc;
                size_t floats = static_cast<size_t>(hc.frames) * h.boneCount * 10;
                memcpy(clipData_ + at, data + off, floats * 4);
                off += floats * 4;
                memcpy(clips_[c].name, hc.name, 32);
                clips_[c].name[31] = 0;
                clips_[c].fps = hc.fps;
                clips_[c].frames = hc.frames;
                clips_[c].flags = hc.flags;
                clips_[c].dataOffset = static_cast<uint32_t>(at);
                at += floats;
            }
        }
        barrelDir_ = -1.f;
        int fl = FindClip("fire_loop");
        if (barrelBone_ >= 0 && fl >= 0) {
            const Clip& c = clips_[fl];
            double sum = 0;
            for (uint32_t f = 0; f + 1 < c.frames; ++f) {
                const float* a = clipData_ + c.dataOffset + (static_cast<size_t>(f) * boneCount_ + barrelBone_) * 10 + 3;
                const float* q = a + boneCount_ * 10;
                float rz = a[3] * q[2] - a[0] * q[1] + a[1] * q[0] - a[2] * q[3];
                float rw = a[3] * q[3] + a[0] * q[0] + a[1] * q[1] + a[2] * q[2];
                if (rw < 0) rz = -rz, rw = -rw;
                double d = 2.0 * std::atan2(rz, rw);
                if (std::fabs(d) < 1.5707963) sum += d;
            }
            if (sum > 0) barrelDir_ = 1.f;
        }
        vb_ = vb;
        ib_ = ib;
        vb = ib = nullptr;
        ok = true;
    } while (false);
    SafeRelease(vb);
    SafeRelease(ib);
    delete[] data;
    return ok;
}
bool Renderer::CreatePipeline(char* err, size_t errLen) {
    FnD3DCompile compile = GetCompiler();
    if (!compile) {
        SetErr(err, errLen, "d3dcompiler_47.dll / D3DCompile unavailable");
        return false;
    }
    ID3DBlob *vsb = nullptr, *psb = nullptr, *msg = nullptr;
    HRESULT hr = compile(kShader, sizeof kShader - 1, "tenzin_fps_vm", nullptr, nullptr, "VS", "vs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &msg);
    if (FAILED(hr)) {
        SetErr(err, errLen, "VS compile 0x%08lx: %s", hr, msg ? static_cast<const char*>(msg->GetBufferPointer()) : "");
        SafeRelease(msg);
        return false;
    }
    SafeRelease(msg);
    hr = compile(kShader, sizeof kShader - 1, "tenzin_fps_vm", nullptr, nullptr, "PS", "ps_5_0",
                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &msg);
    if (FAILED(hr)) {
        SetErr(err, errLen, "PS compile 0x%08lx: %s", hr, msg ? static_cast<const char*>(msg->GetBufferPointer()) : "");
        SafeRelease(msg);
        SafeRelease(vsb);
        return false;
    }
    SafeRelease(msg);
    bool ok = SUCCEEDED(dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) &&
              SUCCEEDED(dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_));
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = ok && SUCCEEDED(dev_->CreateInputLayout(il, 6, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_));
    SafeRelease(vsb);
    SafeRelease(psb);
    if (!ok) {
        SetErr(err, errLen, "shader / input layout creation failed");
        return false;
    }
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = sizeof(CFrame);
    ok = SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &cbFrame_));
    bd.ByteWidth = kMaxBones * 3 * 16;
    ok = ok && SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &cbBones_));
    bd.ByteWidth = 48 + 11 * 16;
    ok = ok && SUCCEEDED(dev_->CreateBuffer(&bd, nullptr, &cbMat_));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(dev_->CreateRasterizerState(&rd, &rs_));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    ok = ok && SUCCEEDED(dev_->CreateDepthStencilState(&dd, &dss_));
    D3D11_BLEND_DESC bld = {};
    bld.RenderTarget[0].BlendEnable = FALSE;
    bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN |
                                                D3D11_COLOR_WRITE_ENABLE_BLUE;
    ok = ok && SUCCEEDED(dev_->CreateBlendState(&bld, &bs_));
    D3D11_SAMPLER_DESC sm = {};
    sm.Filter = D3D11_FILTER_ANISOTROPIC;
    sm.MaxAnisotropy = 8;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(dev_->CreateSamplerState(&sm, &samp_));
    uint32_t white = 0xffffffffu;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 1;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {&white, 4, 4};
    ID3D11Texture2D* wt = nullptr;
    ok = ok && SUCCEEDED(dev_->CreateTexture2D(&td, &sd, &wt)) &&
         SUCCEEDED(dev_->CreateShaderResourceView(wt, nullptr, &white_));
    SafeRelease(wt);
    if (!ok) SetErr(err, errLen, "pipeline state creation failed");
    return ok;
}
bool Renderer::Init(ID3D11Device* dev, const wchar_t* hvmPath, char* err, size_t errLen) {
    Release();
    if (!dev) return false;
    dev_ = dev;
    dev_->AddRef();
    if (!CreatePipeline(err, errLen) || !LoadHvm(hvmPath, err, errLen)) {
        Release();
        return false;
    }
    ready_ = true;
    return true;
}
DrawProf g_drawProf;
namespace {
ScreenBufs g_sharedScreen;
double ProfNow() {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
}
}
void ScreenBufs::Release() {
    SafeRelease(dsv);
    SafeRelease(depthTex);
    depthW = depthH = 0;
    SafeRelease(sceneSrv);
    SafeRelease(sceneTex);
    sceneW = sceneH = sceneMips = 0;
    sceneFmt = DXGI_FORMAT_UNKNOWN;
    SafeRelease(distSrv);
    SafeRelease(distTex);
    distW = distH = 0;
    distFmt = DXGI_FORMAT_UNKNOWN;
    SafeRelease(distDepthSrv);
    SafeRelease(distDepthTex);
    distDepthW = distDepthH = 0;
    dev = nullptr;
}
void ReleaseSharedScreenBuffers() { g_sharedScreen.Release(); }
ScreenBufs& Renderer::Sb() {
    ScreenBufs& b = shareScreen_ ? g_sharedScreen : own_;
    if (b.dev != dev_) {
        b.Release();
        b.dev = dev_;
    }
    return b;
}
const ScreenBufs& Renderer::Sb() const { return shareScreen_ ? g_sharedScreen : own_; }
bool Renderer::EnsureDepth(UINT w, UINT h) {
    ScreenBufs& b = Sb();
    ID3D11DepthStencilView*& dsv_ = b.dsv;
    ID3D11Texture2D*& depthTex_ = b.depthTex;
    UINT &depthW_ = b.depthW, &depthH_ = b.depthH;
    if (dsv_ && w == depthW_ && h == depthH_) return true;
    SafeRelease(dsv_);
    SafeRelease(depthTex_);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    D3D11_DEPTH_STENCIL_VIEW_DESC dvd = {};
    dvd.Format = DXGI_FORMAT_D32_FLOAT;
    dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &depthTex_)) ||
        FAILED(dev_->CreateDepthStencilView(depthTex_, &dvd, &dsv_))) {
        SafeRelease(dsv_);
        SafeRelease(depthTex_);
        return false;
    }
    depthW_ = w;
    depthH_ = h;
    return true;
}
ID3D11ShaderResourceView* Renderer::CopyForDistort(ID3D11DeviceContext* ctx, ID3D11Texture2D* backBuffer, UINT w, UINT h, bool* gamma) {
    if (gamma) *gamma = false;
    if (!ctx || !backBuffer || !dev_ || distBroken_) return nullptr;
    ScreenBufs& b = Sb();
    ID3D11Texture2D *&distTex_ = b.distTex, *&depthTex_ = b.depthTex, *&distDepthTex_ = b.distDepthTex;
    ID3D11ShaderResourceView *&distSrv_ = b.distSrv, *&distDepthSrv_ = b.distDepthSrv;
    UINT &depthW_ = b.depthW, &depthH_ = b.depthH;
    D3D11_TEXTURE2D_DESC sd;
    backBuffer->GetDesc(&sd);
    if (sd.SampleDesc.Count != 1 || sd.Width != w || sd.Height != h) return nullptr;
    DXGI_FORMAT f = sd.Format;
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: f = DXGI_FORMAT_R8G8B8A8_UNORM; break;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: f = DXGI_FORMAT_B8G8R8A8_UNORM; break;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: f = DXGI_FORMAT_R10G10B10A2_UNORM; break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: f = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
    default: break;
    }
    if (!EnsureDistTex(w, h, f)) return nullptr;
    ctx->CopySubresourceRegion(distTex_, 0, 0, 0, 0, backBuffer, 0, nullptr);
    distDepthOk_ = false;
    if (depthTex_ && depthW_ == w && depthH_ == h) {
        EnsureDistDepth(w, h);
        if (distDepthSrv_) {
            ctx->CopyResource(distDepthTex_, depthTex_);
            distDepthOk_ = true;
        }
    }
    if (gamma)
        *gamma = !(f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    return distSrv_;
}
bool Renderer::EnsureDistTex(UINT w, UINT h, DXGI_FORMAT f) {
    if (distBroken_ || !dev_) return false;
    ScreenBufs& b = Sb();
    ID3D11Texture2D*& distTex_ = b.distTex;
    ID3D11ShaderResourceView*& distSrv_ = b.distSrv;
    UINT &distW_ = b.distW, &distH_ = b.distH;
    DXGI_FORMAT& distFmt_ = b.distFmt;
    if (distSrv_ && w == distW_ && h == distH_ && f == distFmt_) return true;
    SafeRelease(distSrv_);
    SafeRelease(distTex_);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = f;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &distTex_)) || FAILED(dev_->CreateShaderResourceView(distTex_, nullptr, &distSrv_))) {
        SafeRelease(distSrv_);
        SafeRelease(distTex_);
        distBroken_ = true;
        return false;
    }
    distW_ = w;
    distH_ = h;
    distFmt_ = f;
    return true;
}
void Renderer::EnsureDistDepth(UINT w, UINT h) {
    ScreenBufs& b = Sb();
    ID3D11Texture2D*& distDepthTex_ = b.distDepthTex;
    ID3D11ShaderResourceView*& distDepthSrv_ = b.distDepthSrv;
    UINT &distDepthW_ = b.distDepthW, &distDepthH_ = b.distDepthH;
    if (distDepthSrv_ && distDepthW_ == w && distDepthH_ == h) return;
    SafeRelease(distDepthSrv_);
    SafeRelease(distDepthTex_);
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = w;
    dd.Height = h;
    dd.MipLevels = dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_R32_TYPELESS;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = DXGI_FORMAT_R32_FLOAT;
    vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    vd.Texture2D.MipLevels = 1;
    if (FAILED(dev_->CreateTexture2D(&dd, nullptr, &distDepthTex_)) || FAILED(dev_->CreateShaderResourceView(distDepthTex_, &vd, &distDepthSrv_))) {
        SafeRelease(distDepthSrv_);
        SafeRelease(distDepthTex_);
    }
    distDepthW_ = w;
    distDepthH_ = h;
}
bool Renderer::PrewarmScreen(const D3D11_TEXTURE2D_DESC& bb) {
    if (!ready_ || !dev_ || bb.SampleDesc.Count != 1 || bb.Width < 16 || bb.Height < 16) return false;
    const UINT w = bb.Width, h = bb.Height;
    DXGI_FORMAT f = bb.Format;
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: f = DXGI_FORMAT_R8G8B8A8_UNORM; break;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: f = DXGI_FORMAT_B8G8R8A8_UNORM; break;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: f = DXGI_FORMAT_R10G10B10A2_UNORM; break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: f = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
    default: break;
    }
    const bool depth = EnsureDepth(w, h);
    if (!sceneBroken_) EnsureScene(w, h, f);
    if (EnsureDistTex(w, h, f) && depth) EnsureDistDepth(w, h);
    return depth;
}
bool Renderer::EnsureScene(UINT w, UINT h, DXGI_FORMAT fmt) {
    if (sceneBroken_) return false;
    ScreenBufs& b = Sb();
    ID3D11Texture2D*& sceneTex_ = b.sceneTex;
    ID3D11ShaderResourceView*& sceneSrv_ = b.sceneSrv;
    UINT &sceneW_ = b.sceneW, &sceneH_ = b.sceneH, &sceneMips_ = b.sceneMips;
    DXGI_FORMAT& sceneFmt_ = b.sceneFmt;
    if (sceneSrv_ && w == sceneW_ && h == sceneH_ && fmt == sceneFmt_) return true;
    SafeRelease(sceneSrv_);
    SafeRelease(sceneTex_);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 0;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &sceneTex_)) ||
        FAILED(dev_->CreateShaderResourceView(sceneTex_, nullptr, &sceneSrv_))) {
        SafeRelease(sceneSrv_);
        SafeRelease(sceneTex_);
        sceneBroken_ = true;
        return false;
    }
    sceneTex_->GetDesc(&td);
    sceneMips_ = td.MipLevels;
    sceneW_ = w;
    sceneH_ = h;
    sceneFmt_ = fmt;
    return true;
}
int Renderer::FindClip(const char* name) const {
    for (unsigned i = 0; i < clipCount_; ++i)
        if (!strcmp(clips_[i].name, name)) return static_cast<int>(i);
    return -1;
}
float Renderer::ClipDuration(int i) const {
    if (i < 0 || i >= static_cast<int>(clipCount_)) return 0;
    return static_cast<float>(clips_[i].frames) / clips_[i].fps;
}
bool Renderer::ClipLoops(int i) const { return i >= 0 && i < static_cast<int>(clipCount_) && (clips_[i].flags & 1); }
bool Renderer::Muzzle(float* ndcX, float* ndcY, float* aspect) const {
    if (!muzzleValid_) return false;
    *ndcX = muzzleNdc_[0];
    *ndcY = muzzleNdc_[1];
    *aspect = muzzleAspect_;
    return true;
}
bool Renderer::MuzzleView(MuzzleFrame* f) const {
    if (!muzzleFrameValid_) return false;
    *f = muzzleFrame_;
    return true;
}
void Renderer::SampleClip(int ci, float time, unsigned b, float* trs) const {
    if (ci < 0 || ci >= static_cast<int>(clipCount_)) {
        memcpy(trs, bones_[b].bind, sizeof(float) * 10);
        return;
    }
    const Clip* clip = &clips_[ci];
    float f = time * clip->fps;
    if (f < 0) f = 0;
    unsigned f0, f1;
    if (clip->flags & 1) {
        f = std::fmod(f, static_cast<float>(clip->frames));
        f0 = static_cast<unsigned>(f);
        f1 = (f0 + 1) % clip->frames;
    } else {
        float last = static_cast<float>(clip->frames - 1);
        if (f > last) f = last;
        f0 = static_cast<unsigned>(f);
        f1 = f0 + 1 < clip->frames ? f0 + 1 : f0;
    }
    float t = f - static_cast<float>(f0);
    const float* a = clipData_ + clip->dataOffset + (static_cast<size_t>(f0) * boneCount_ + b) * 10;
    const float* c = clipData_ + clip->dataOffset + (static_cast<size_t>(f1) * boneCount_ + b) * 10;
    float dot = a[3] * c[3] + a[4] * c[4] + a[5] * c[5] + a[6] * c[6];
    float sgn = dot < 0 ? -1.f : 1.f;
    for (int k = 0; k < 3; ++k) trs[k] = a[k] + (c[k] - a[k]) * t;
    for (int k = 3; k < 7; ++k) trs[k] = a[k] + (c[k] * sgn - a[k]) * t;
    for (int k = 7; k < 10; ++k) trs[k] = a[k] + (c[k] - a[k]) * t;
}
bool Renderer::BoneWorldAt(int clip, float time, int bone, float* m34) const {
    if (bone < 0 || bone >= static_cast<int>(boneCount_)) return false;
    int chain[kMaxBones];
    int n = 0;
    for (int b = bone; b >= 0 && n < static_cast<int>(kMaxBones); b = bones_[b].parent) chain[n++] = b;
    Aff w = AffIdentity();
    for (int i = n - 1; i >= 0; --i) {
        float trs[10];
        SampleClip(clip, time, static_cast<unsigned>(chain[i]), trs);
        w = AffMul(w, AffFromTrs(trs));
    }
    memcpy(m34, w.m, sizeof w.m);
    return true;
}
namespace {
Aff g_world[kMaxBones], g_local[kMaxBones];
float g_lastTrs[kMaxBones][10], g_snapTrs[kMaxBones][10];
unsigned g_lastCount = 0;
signed char g_fadeSign[kMaxBones];
float g_lastNew[kMaxBones][4], g_lastOld[kMaxBones][4];
int g_fadeClip = -99, g_fadePrev = -99;
float g_fadeBlend = 2.f;
void KeepHemisphere(float* q, float* last) {
    float d = q[0] * last[0] + q[1] * last[1] + q[2] * last[2] + q[3] * last[3];
    if (d < 0)
        for (int k = 0; k < 4; ++k) q[k] = -q[k];
    memcpy(last, q, sizeof(float) * 4);
}
void Pos(const Aff& a, float* p) { p[0] = a.m[3], p[1] = a.m[7], p[2] = a.m[11]; }
float Len3(const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
Aff AffAxisAngle(const float* n, float ang) {
    float c = std::cos(ang), s = std::sin(ang), t = 1.f - c, x = n[0], y = n[1], z = n[2];
    Aff r = {{t * x * x + c, t * x * y - s * z, t * x * z + s * y, 0, t * x * y + s * z, t * y * y + c, t * y * z - s * x, 0,
              t * x * z - s * y, t * y * z + s * x, t * z * z + c, 0}};
    return r;
}
Aff AffAbout(const float* p, const Aff& r) { return AffMul(AffTrans(p[0], p[1], p[2]), AffMul(r, AffTrans(-p[0], -p[1], -p[2]))); }
void RotToQuat(const Aff& a, float* q) {
    float m[9];
    for (int c = 0; c < 3; ++c) {
        float n = std::sqrt(a.m[c] * a.m[c] + a.m[4 + c] * a.m[4 + c] + a.m[8 + c] * a.m[8 + c]);
        for (int r = 0; r < 3; ++r) m[r * 3 + c] = n > 1e-8f ? a.m[r * 4 + c] / n : (r == c ? 1.f : 0.f);
    }
    float t = m[0] + m[4] + m[8];
    if (t > 0.f) {
        float s = std::sqrt(t + 1.f) * 2.f;
        q[0] = (m[7] - m[5]) / s, q[1] = (m[2] - m[6]) / s, q[2] = (m[3] - m[1]) / s, q[3] = 0.25f * s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = std::sqrt(1.f + m[0] - m[4] - m[8]) * 2.f;
        q[0] = 0.25f * s, q[1] = (m[1] + m[3]) / s, q[2] = (m[2] + m[6]) / s, q[3] = (m[7] - m[5]) / s;
    } else if (m[4] > m[8]) {
        float s = std::sqrt(1.f + m[4] - m[0] - m[8]) * 2.f;
        q[0] = (m[1] + m[3]) / s, q[1] = 0.25f * s, q[2] = (m[5] + m[7]) / s, q[3] = (m[2] - m[6]) / s;
    } else {
        float s = std::sqrt(1.f + m[8] - m[0] - m[4]) * 2.f;
        q[0] = (m[2] + m[6]) / s, q[1] = (m[5] + m[7]) / s, q[2] = 0.25f * s, q[3] = (m[3] - m[1]) / s;
    }
}
void QuatToRot(const float* q, Aff& a) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    a.m[0] = 1 - 2 * (y * y + z * z), a.m[1] = 2 * (x * y - z * w), a.m[2] = 2 * (x * z + y * w);
    a.m[4] = 2 * (x * y + z * w), a.m[5] = 1 - 2 * (x * x + z * z), a.m[6] = 2 * (y * z - x * w);
    a.m[8] = 2 * (x * z - y * w), a.m[9] = 2 * (y * z + x * w), a.m[10] = 1 - 2 * (x * x + y * y);
}
void QuatNlerp(const float* a, const float* b, float t, float* o) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], s = d < 0.f ? -1.f : 1.f, n = 0.f;
    for (int k = 0; k < 4; ++k) o[k] = a[k] + (b[k] * s - a[k]) * t, n += o[k] * o[k];
    n = std::sqrt(n);
    for (int k = 0; k < 4; ++k) o[k] = n > 1e-8f ? o[k] / n : (k == 3 ? 1.f : 0.f);
}
Aff AffBlend(const Aff& A, const Aff& B, float t) {
    float qa[4], qb[4], q[4];
    RotToQuat(A, qa), RotToQuat(B, qb), QuatNlerp(qa, qb, t, q);
    Aff o = AffIdentity();
    QuatToRot(q, o);
    for (int r = 0; r < 3; ++r) o.m[r * 4 + 3] = A.m[r * 4 + 3] + (B.m[r * 4 + 3] - A.m[r * 4 + 3]) * t;
    return o;
}
Aff RotScaled(const Aff& a, float w) {
    float cosA = (a.m[0] + a.m[5] + a.m[10] - 1.f) * 0.5f;
    cosA = cosA > 1.f ? 1.f : (cosA < -1.f ? -1.f : cosA);
    float ax[3] = {a.m[9] - a.m[6], a.m[2] - a.m[8], a.m[4] - a.m[1]}, n = Len3(ax), ang = std::acos(cosA);
    if (n < 1e-8f || ang < 1e-7f) return AffIdentity();
    for (float& v : ax) v /= n;
    return AffAxisAngle(ax, ang * w);
}
void TwoBoneIk(Aff& S, Aff& E, Aff& W, const float* T) {
    float s[3], e[3], w[3];
    Pos(S, s), Pos(E, e), Pos(W, w);
    float se[3] = {e[0] - s[0], e[1] - s[1], e[2] - s[2]}, ew[3] = {w[0] - e[0], w[1] - e[1], w[2] - e[2]};
    float st[3] = {T[0] - s[0], T[1] - s[1], T[2] - s[2]};
    float a = Len3(se), b = Len3(ew), d = Len3(st);
    if (a < 1e-5f || b < 1e-5f || d < 1e-5f) return;
    float lo = std::fabs(a - b) + 1e-4f, hi = a + b - 1e-4f;
    d = d < lo ? lo : (d > hi ? hi : d);
    float u[3] = {-se[0] / a, -se[1] / a, -se[2] / a}, v[3] = {ew[0] / b, ew[1] / b, ew[2] / b};
    float cu = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
    float cw = (a * a + b * b - d * d) / (2.f * a * b);
    float cur = std::acos(cu > 1.f ? 1.f : (cu < -1.f ? -1.f : cu)), want = std::acos(cw > 1.f ? 1.f : (cw < -1.f ? -1.f : cw));
    float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]}, nl = Len3(n);
    if (nl < 1e-6f) n[0] = E.m[0], n[1] = E.m[4], n[2] = E.m[8], nl = Len3(n);
    if (nl > 1e-6f && std::fabs(want - cur) > 1e-6f) {
        for (float& q : n) q /= nl;
        Aff r1 = AffAbout(e, AffAxisAngle(n, want - cur));
        E = AffMul(r1, E);
        W = AffMul(r1, W);
    }
    Pos(W, w);
    float f[3] = {w[0] - s[0], w[1] - s[1], w[2] - s[2]}, fl = Len3(f), tl = Len3(st);
    if (fl < 1e-6f || tl < 1e-6f) return;
    for (int k = 0; k < 3; ++k) f[k] /= fl, st[k] /= tl;
    float ax[3] = {f[1] * st[2] - f[2] * st[1], f[2] * st[0] - f[0] * st[2], f[0] * st[1] - f[1] * st[0]}, al = Len3(ax);
    float ang = std::atan2(al, f[0] * st[0] + f[1] * st[1] + f[2] * st[2]);
    if (al < 1e-8f || ang < 1e-7f) return;
    for (float& q : ax) q /= al;
    Aff r2 = AffAbout(s, AffAxisAngle(ax, ang));
    S = AffMul(r2, S);
    E = AffMul(r2, E);
    W = AffMul(r2, W);
}
}
void Renderer::EvalBones(const Look& look) {
    Aff* world = g_world;
    bool snap = look.clipPrev <= -2 && g_lastCount == boneCount_;
    if (look.clipPrev == -3 && snap) memcpy(g_snapTrs, g_lastTrs, sizeof(float) * 10 * boneCount_);
    bool fade = (look.clipPrev >= 0 || snap) && look.blend < 0.999f;
    float wNew = look.blend < 0 ? 0.f : (look.blend > 1 ? 1.f : look.blend);
    const bool newFade = fade && (look.clipPrev == -3 || look.clip != g_fadeClip || (look.clipPrev >= 0 && look.clipPrev != g_fadePrev) ||
                                  look.blend < g_fadeBlend - 1e-4f || g_lastCount != boneCount_);
    g_fadeClip = fade ? look.clip : -99;
    g_fadePrev = fade ? look.clipPrev : -99;
    g_fadeBlend = fade ? look.blend : 2.f;
    for (unsigned b = 0; b < boneCount_; ++b) {
        float trs[10];
        SampleClip(look.clip, look.clipTime, b, trs);
        if (fade) {
            float old[10];
            if (snap)
                memcpy(old, g_snapTrs[b], sizeof old);
            else
                SampleClip(look.clipPrev, look.clipTimePrev, b, old);
            if (!newFade) {
                KeepHemisphere(trs + 3, g_lastNew[b]);
                if (!snap) KeepHemisphere(old + 3, g_lastOld[b]);
            } else {
                memcpy(g_lastNew[b], trs + 3, sizeof(float) * 4);
                memcpy(g_lastOld[b], old + 3, sizeof(float) * 4);
            }
            float dot = old[3] * trs[3] + old[4] * trs[4] + old[5] * trs[5] + old[6] * trs[6];
            if (newFade) g_fadeSign[b] = dot < 0 ? -1 : 1;
            float sgn = g_fadeSign[b];
            for (int k = 0; k < 3; ++k) trs[k] = old[k] + (trs[k] - old[k]) * wNew;
            for (int k = 3; k < 7; ++k) trs[k] = old[k] + (trs[k] * sgn - old[k]) * wNew;
            for (int k = 7; k < 10; ++k) trs[k] = old[k] + (trs[k] - old[k]) * wNew;
        }
        if (static_cast<int>(b) == look.spinBone) {
            const float* q = bones_[b].bind + 3;
            float hz = std::sin(look.spinAngle * 0.5f), hw = std::cos(look.spinAngle * 0.5f);
            trs[3] = q[0] * hw + q[1] * hz;
            trs[4] = q[1] * hw - q[0] * hz;
            trs[5] = q[3] * hz + q[2] * hw;
            trs[6] = q[3] * hw - q[2] * hz;
        }
        memcpy(g_lastTrs[b], trs, sizeof trs);
        Aff local = AffFromTrs(trs);
        g_local[b] = local;
        world[b] = bones_[b].parent >= 0 ? AffMul(world[bones_[b].parent], local) : local;
    }
    g_lastCount = boneCount_;
}
bool DebugBoneWorld(unsigned b, float m12[12]) {
    if (b >= g_lastCount) return false;
    memcpy(m12, g_world[b].m, sizeof(float) * 12);
    return true;
}
namespace {
struct AnimStateCopy {
    Aff world[kMaxBones], local[kMaxBones];
    float lastTrs[kMaxBones][10], snapTrs[kMaxBones][10];
    unsigned lastCount;
    signed char fadeSign[kMaxBones];
    float lastNew[kMaxBones][4], lastOld[kMaxBones][4];
    int fadeClip, fadePrev;
    float fadeBlend;
} g_animSaved;
}
void AnimStatePush() {
    AnimStateCopy& c = g_animSaved;
    memcpy(c.world, g_world, sizeof g_world), memcpy(c.local, g_local, sizeof g_local);
    memcpy(c.lastTrs, g_lastTrs, sizeof g_lastTrs), memcpy(c.snapTrs, g_snapTrs, sizeof g_snapTrs);
    memcpy(c.fadeSign, g_fadeSign, sizeof g_fadeSign), memcpy(c.lastNew, g_lastNew, sizeof g_lastNew), memcpy(c.lastOld, g_lastOld, sizeof g_lastOld);
    c.lastCount = g_lastCount, c.fadeClip = g_fadeClip, c.fadePrev = g_fadePrev, c.fadeBlend = g_fadeBlend;
}
void AnimStatePop() {
    const AnimStateCopy& c = g_animSaved;
    memcpy(g_world, c.world, sizeof g_world), memcpy(g_local, c.local, sizeof g_local);
    memcpy(g_lastTrs, c.lastTrs, sizeof g_lastTrs), memcpy(g_snapTrs, c.snapTrs, sizeof g_snapTrs);
    memcpy(g_fadeSign, c.fadeSign, sizeof g_fadeSign), memcpy(g_lastNew, c.lastNew, sizeof g_lastNew), memcpy(g_lastOld, c.lastOld, sizeof g_lastOld);
    g_lastCount = c.lastCount, g_fadeClip = c.fadeClip, g_fadePrev = c.fadePrev, g_fadeBlend = c.fadeBlend;
}
void Renderer::ArmIk(const Settings& s, const float* k12) {
    Aff K;
    memcpy(K.m, k12, sizeof K.m);
    Aff* W = g_world;
    Aff gunK = AffMul(K, W[gunBone_]);
    int start = gunBone_;
    for (int side = 0; side < 2; ++side) {
        int sh = armBone_[side][0], el = armBone_[side][1], wr = armBone_[side][2], tag = handTag_[side];
        if (sh < 0 || el < 0 || wr < 0) continue;
        float pw[3], pt[3];
        Pos(W[wr], pw);
        float hold = 1.f;
        if (tag >= 0) {
            Pos(W[tag], pt);
            float dv[3] = {pw[0] - pt[0], pw[1] - pt[1], pw[2] - pt[2]};
            float x = (0.030f - Len3(dv)) / 0.025f;
            x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
            hold = x * x * (3.f - 2.f * x);
        }
        float follow = side == 0 ? 1.f : hold;
        const float* grip = side == 0 ? s.ikGripRi : s.ikGripLe;
        Aff wk = AffMul(K, W[wr]);
        float target[3];
        for (int r = 0; r < 3; ++r) {
            float off = gunK.m[r * 4] * grip[0] + gunK.m[r * 4 + 1] * grip[1] + gunK.m[r * 4 + 2] * grip[2];
            target[r] = pw[r] + follow * (wk.m[r * 4 + 3] - pw[r]) + hold * off;
        }
        Aff rotT = AffMul(RotScaled(K, follow), W[wr]);
        Aff S = W[sh], E = W[el], Wr = W[wr];
        TwoBoneIk(S, E, Wr, target);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) Wr.m[r * 4 + c] = rotT.m[r * 4 + c];
        float pr[3];
        Pos(Wr, pr);
        float res[3] = {pr[0] - target[0], pr[1] - target[1], pr[2] - target[2]};
        float mv[3] = {target[0] - pw[0], target[1] - pw[1], target[2] - pw[2]};
        ikResidual_[side] = Len3(res) * 1000.f;
        ikShift_[side] = Len3(mv) * 1000.f;
        int ps = bones_[sh].parent;
        g_local[sh] = ps >= 0 ? AffMul(AffInverse(W[ps]), S) : S;
        g_local[el] = AffMul(AffInverse(S), E);
        g_local[wr] = AffMul(AffInverse(E), Wr);
        if (side == 0 && bones_[gunBone_].parent == wr) g_local[gunBone_] = AffMul(AffInverse(Wr), gunK);
        if (sh < start) start = sh;
    }
    if (bones_[gunBone_].parent != armBone_[0][2]) {
        int pg = bones_[gunBone_].parent;
        g_local[gunBone_] = pg >= 0 ? AffMul(AffInverse(W[pg]), gunK) : gunK;
    }
    for (unsigned b = static_cast<unsigned>(start); b < boneCount_; ++b)
        W[b] = bones_[b].parent >= 0 ? AffMul(W[bones_[b].parent], g_local[b]) : g_local[b];
}
void Renderer::ShieldIk(const Look& look, const float* nInv12) {
    int sh = armBone_[1][0], el = armBone_[1][1], wr = armBone_[1][2];
    if (sh < 0 || el < 0 || wr < 0) return;
    Aff Ni, P;
    memcpy(Ni.m, nInv12, sizeof Ni.m);
    memcpy(P.m, look.shieldPose, sizeof P.m);
    Aff Pm = AffMul(Ni, P);
    const float* g = look.shieldGrip;
    const float* hq = look.shieldHandQuat;
    bool refHand = hq[0] != 0.f || hq[1] != 0.f || hq[2] != 0.f || hq[3] != 0.f;
    Aff* W = g_world;
    float a = look.shieldArmW < 0.f ? 0.f : (look.shieldArmW > 1.f ? 1.f : look.shieldArmW);
    float eloc[3] = {0.f, 0.f, 0.f};
    shieldForearmErr_ = -1.f;
    shieldArmShift_ = -1.f;
    if (refHand) {
        float s0[3], e0[3], w0[3];
        Pos(W[sh], s0), Pos(W[el], e0), Pos(W[wr], w0);
        float d1[3] = {e0[0] - s0[0], e0[1] - s0[1], e0[2] - s0[2]}, d2[3] = {w0[0] - e0[0], w0[1] - e0[1], w0[2] - e0[2]};
        float lu = Len3(d1), lf = Len3(d2);
        const float* ev = look.shieldElbow;
        float dl[3] = {ev[0] - g[0], ev[1] - g[1], ev[2] - g[2]}, dll = Len3(dl);
        for (int k = 0; k < 3; ++k) eloc[k] = dll > 1e-4f ? g[k] + dl[k] * lf / dll : ev[k];
        float em[3], se[3];
        for (int r = 0; r < 3; ++r) em[r] = Pm.m[r * 4] * eloc[0] + Pm.m[r * 4 + 1] * eloc[1] + Pm.m[r * 4 + 2] * eloc[2] + Pm.m[r * 4 + 3];
        for (int k = 0; k < 3; ++k) se[k] = em[k] - s0[k];
        float d = Len3(se);
        if (d > 1e-4f && lu > 1e-4f) {
            float k = lu / d - 1.f;
            float hw = look.shieldHoldW < 0.f ? 0.f : (look.shieldHoldW > 1.f ? 1.f : look.shieldHoldW);
            for (int r = 0; r < 3; ++r) Pm.m[r * 4 + 3] += se[r] * k * a * hw;
            shieldArmShift_ = d * std::fabs(k) * 1000.f;
        }
    }
    if (shieldRestK_ > 0.f) {
        Aff Nv = AffInverse(Ni);
        float nm[3], nv[3], tg[3], dv[3], dm[3];
        for (int r = 0; r < 3; ++r) nm[r] = Pm.m[r * 4] * shieldNotch_[0] + Pm.m[r * 4 + 1] * shieldNotch_[1] + Pm.m[r * 4 + 2] * shieldNotch_[2] + Pm.m[r * 4 + 3];
        for (int r = 0; r < 3; ++r) nv[r] = Nv.m[r * 4] * nm[0] + Nv.m[r * 4 + 1] * nm[1] + Nv.m[r * 4 + 2] * nm[2] + Nv.m[r * 4 + 3];
        float dz = shieldRestB_[2] - shieldRestA_[2], t = std::fabs(dz) > 1e-5f ? (nv[2] - shieldRestA_[2]) / dz : 0.f;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        for (int r = 0; r < 3; ++r) tg[r] = shieldRestA_[r] + (shieldRestB_[r] - shieldRestA_[r]) * t;
        if (shieldRestT_ >= 0.f) shieldRestT_ = t;
        dv[0] = tg[0] - nv[0], dv[1] = tg[1] - nv[1], dv[2] = 0.f;
        for (int r = 0; r < 3; ++r) dm[r] = Ni.m[r * 4] * dv[0] + Ni.m[r * 4 + 1] * dv[1] + Ni.m[r * 4 + 2] * dv[2];
        for (int r = 0; r < 3; ++r) Pm.m[r * 4 + 3] += dm[r] * shieldRestK_, shieldRestView_[r] = tg[r];
    }
    float gm[3], pw[3], target[3];
    for (int r = 0; r < 3; ++r) gm[r] = Pm.m[r * 4] * g[0] + Pm.m[r * 4 + 1] * g[1] + Pm.m[r * 4 + 2] * g[2] + Pm.m[r * 4 + 3];
    Pos(W[wr], pw);
    for (int r = 0; r < 3; ++r) target[r] = pw[r] + (gm[r] - pw[r]) * a;
    Aff S = W[sh], E = W[el], Wr = W[wr];
    {
        Aff Nv = AffInverse(Ni);
        float sp[3];
        Pos(S, sp);
        for (int r = 0; r < 3; ++r) {
            shieldGripView_[r] = Nv.m[r * 4] * target[0] + Nv.m[r * 4 + 1] * target[1] + Nv.m[r * 4 + 2] * target[2] + Nv.m[r * 4 + 3];
            leftShoulderView_[r] = Nv.m[r * 4] * sp[0] + Nv.m[r * 4 + 1] * sp[1] + Nv.m[r * 4 + 2] * sp[2] + Nv.m[r * 4 + 3];
        }
    }
    TwoBoneIk(S, E, Wr, target);
    if (refHand) {
        float pm[3], s[3], e[3], wv[3];
        for (int r = 0; r < 3; ++r) pm[r] = Pm.m[r * 4] * eloc[0] + Pm.m[r * 4 + 1] * eloc[1] + Pm.m[r * 4 + 2] * eloc[2] + Pm.m[r * 4 + 3];
        Pos(S, s), Pos(E, e), Pos(Wr, wv);
        float u[3] = {wv[0] - s[0], wv[1] - s[1], wv[2] - s[2]}, ul = Len3(u);
        if (ul > 1e-5f) {
            for (float& c : u) c /= ul;
            float de[3] = {e[0] - s[0], e[1] - s[1], e[2] - s[2]}, dp[3] = {pm[0] - s[0], pm[1] - s[1], pm[2] - s[2]};
            float ke = de[0] * u[0] + de[1] * u[1] + de[2] * u[2], kp = dp[0] * u[0] + dp[1] * u[1] + dp[2] * u[2];
            for (int k = 0; k < 3; ++k) de[k] -= u[k] * ke, dp[k] -= u[k] * kp;
            float ax[3] = {de[1] * dp[2] - de[2] * dp[1], de[2] * dp[0] - de[0] * dp[2], de[0] * dp[1] - de[1] * dp[0]}, al = Len3(ax);
            float ang = std::atan2(al, de[0] * dp[0] + de[1] * dp[1] + de[2] * dp[2]);
            if (al > 1e-9f && Len3(de) > 1e-5f && Len3(dp) > 1e-5f) {
                for (float& c : ax) c /= al;
                Aff r = AffAbout(s, AffAxisAngle(ax, ang * a));
                S = AffMul(r, S), E = AffMul(r, E), Wr = AffMul(r, Wr);
            }
        }
        Aff Rt = Pm, Rh = AffIdentity();
        QuatToRot(hq, Rh);
        Rt = AffMul(Rt, Rh);
        float qc[4], qt[4], q[4], p[3];
        RotToQuat(Wr, qc), RotToQuat(Rt, qt), QuatNlerp(qc, qt, a, q);
        Pos(Wr, p);
        QuatToRot(q, Wr);
        Wr.m[3] = p[0], Wr.m[7] = p[1], Wr.m[11] = p[2];
        Pos(E, e);
        float f1[3] = {p[0] - e[0], p[1] - e[1], p[2] - e[2]}, f2[3] = {p[0] - pm[0], p[1] - pm[1], p[2] - pm[2]};
        float l1 = Len3(f1), l2 = Len3(f2);
        if (l1 > 1e-5f && l2 > 1e-5f) {
            float c = (f1[0] * f2[0] + f1[1] * f2[1] + f1[2] * f2[2]) / (l1 * l2);
            shieldForearmErr_ = std::acos(c > 1.f ? 1.f : (c < -1.f ? -1.f : c)) / kDeg;
        }
    }
    const float* wrd = look.shieldWristRot;
    if (!refHand && (wrd[0] != 0.f || wrd[1] != 0.f || wrd[2] != 0.f)) {
        float p[3];
        Pos(Wr, p);
        Aff rl = AffMul(AffRotX(wrd[0] * kDeg * a), AffMul(AffRotY(wrd[1] * kDeg * a), AffRotZ(wrd[2] * kDeg * a)));
        Wr = AffMul(Wr, rl);
        Wr.m[3] = p[0], Wr.m[7] = p[1], Wr.m[11] = p[2];
    }
    if (look.shieldFingerOn) {
        for (int k = 0; k < 19; ++k) {
            int b = fingerBone_[k];
            if (b <= wr || b >= static_cast<int>(boneCount_)) continue;
            float qa[4], q[4], t[3] = {g_local[b].m[3], g_local[b].m[7], g_local[b].m[11]};
            RotToQuat(g_local[b], qa);
            QuatNlerp(qa, look.shieldFinger[k], a, q);
            QuatToRot(q, g_local[b]);
            g_local[b].m[3] = t[0], g_local[b].m[7] = t[1], g_local[b].m[11] = t[2];
        }
    }
    float pr[3];
    Pos(Wr, pr);
    float res[3] = {pr[0] - target[0], pr[1] - target[1], pr[2] - target[2]};
    shieldIkResidual_ = Len3(res) * 1000.f;
    int ps = bones_[sh].parent;
    g_local[sh] = ps >= 0 ? AffMul(AffInverse(W[ps]), S) : S;
    g_local[el] = AffMul(AffInverse(S), E);
    g_local[wr] = AffMul(AffInverse(E), Wr);
    for (unsigned b = static_cast<unsigned>(sh); b < boneCount_; ++b)
        W[b] = bones_[b].parent >= 0 ? AffMul(W[bones_[b].parent], g_local[b]) : g_local[b];
}
void Renderer::TwistSetup() {
    for (int sd = 0; sd < 2; ++sd) {
        const int el = armBone_[sd][1], wr = armBone_[sd][2];
        bool ok = twistN_[sd] > 0 && el >= 0 && wr >= 0 && bones_[wr].parent == el;
        int top = 0;
        for (int i = 0; ok && i < twistN_[sd]; ++i) {
            ok = bones_[twistBone_[sd][i]].parent == el;
            if (twistNum_[sd][i] > top) top = twistNum_[sd][i];
        }
        const float* t = ok ? bones_[wr].bind : nullptr;
        const float al = t ? std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]) : 0.f;
        const float ql = t ? std::sqrt(t[3] * t[3] + t[4] * t[4] + t[5] * t[5] + t[6] * t[6]) : 0.f;
        if (!ok || top <= 0 || al < 1e-6f || ql < 1e-6f) {
            twistN_[sd] = 0;
            continue;
        }
        for (int k = 0; k < 3; ++k) twistAxis_[sd][k] = t[k] / al;
        for (int k = 0; k < 4; ++k) twistBindQ_[sd][k] = t[3 + k] / ql;
        for (int i = 0; i < twistN_[sd]; ++i) twistFrac_[sd][i] = static_cast<float>(twistNum_[sd][i]) / static_cast<float>(top);
        twistHave_[sd] = false;
    }
}
void Renderer::TwistBones() {
    constexpr float kTwoPi = 6.28318531f, kReliableDeg = 100.f;
    for (int sd = 0; sd < 2; ++sd) {
        if (!twistN_[sd]) continue;
        const int el = armBone_[sd][1], wr = armBone_[sd][2];
        const Aff L = AffMul(AffInverse(g_world[el]), g_world[wr]);
        float qc[4];
        RotToQuat(L, qc);
        const float* qb = twistBindQ_[sd];
        const float bx = -qb[0], by = -qb[1], bz = -qb[2], bw = qb[3];
        const float dx = qc[3] * bx + qc[0] * bw + qc[1] * bz - qc[2] * by;
        const float dy = qc[3] * by - qc[0] * bz + qc[1] * bw + qc[2] * bx;
        const float dz = qc[3] * bz + qc[0] * by - qc[1] * bx + qc[2] * bw;
        const float dw = qc[3] * bw - qc[0] * bx - qc[1] * by - qc[2] * bz;
        const float* a = twistAxis_[sd];
        const float pr = dx * a[0] + dy * a[1] + dz * a[2];
        float phi = std::remainder(2.f * std::atan2(pr, dw), kTwoPi);
        float sw = std::fabs(dw * std::cos(phi * 0.5f) + pr * std::sin(phi * 0.5f));
        const float swingDeg = 2.f * std::acos(sw > 1.f ? 1.f : sw) * 57.2957795f;
        if (swingDeg < kReliableDeg) {
            if (twistHave_[sd]) phi += kTwoPi * std::round((twistPrev_[sd] - phi) / kTwoPi);
            twistPrev_[sd] = phi;
            twistHave_[sd] = true;
        } else {
            phi = twistHave_[sd] ? twistPrev_[sd] : 0.f;
        }
        for (int i = 0; i < twistN_[sd]; ++i) {
            const int b = twistBone_[sd][i];
            g_local[b] = AffAxisAngle(a, phi * twistFrac_[sd][i]);
            g_world[b] = AffMul(g_world[el], g_local[b]);
        }
    }
}
void Renderer::FinishBones(float* palette, int muzzleBone, const float* muzzleLocal, float* muzzleModel, const float* axisLocal,
                           const float* upLocal, float* axisModel, float* upModel) {
    TwistBones();
    const Aff* world = g_world;
    for (unsigned b = 0; b < boneCount_; ++b) {
        Aff inv;
        memcpy(inv.m, bones_[b].invBind, sizeof inv.m);
        Aff sk = AffMul(world[b], inv);
        memcpy(palette + b * 12, sk.m, sizeof sk.m);
    }
    if (cameraBone_ >= 0) memcpy(palette + kMaxBones * 12, world[cameraBone_].m, sizeof(float) * 12);
    if (muzzleBone >= 0 && muzzleBone < static_cast<int>(boneCount_)) {
        const float* m = world[muzzleBone].m;
        for (int r = 0; r < 3; ++r) {
            muzzleModel[r] = m[r * 4 + 0] * muzzleLocal[0] + m[r * 4 + 1] * muzzleLocal[1] +
                             m[r * 4 + 2] * muzzleLocal[2] + m[r * 4 + 3];
            axisModel[r] = m[r * 4 + 0] * axisLocal[0] + m[r * 4 + 1] * axisLocal[1] + m[r * 4 + 2] * axisLocal[2];
            upModel[r] = m[r * 4 + 0] * upLocal[0] + m[r * 4 + 1] * upLocal[1] + m[r * 4 + 2] * upLocal[2];
        }
    }
}
bool Renderer::Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11Texture2D* sceneTex, UINT w, UINT h,
                    bool rtvEncodesSrgb, const Settings& s, const Look& look) {
    sceneFresh_ = false;
    shieldViewValid_ = false;
    const double tProf0 = ProfNow();
    g_drawProf = DrawProf();
    if (!ready_ || !ctx || !rtv || !w || !h) return false;
    if (!EnsureDepth(w, h)) return false;
    g_drawProf.depth = (ProfNow() - tProf0) * 1000.0;
    static float palette[kMaxBones * 12 + 12];
    float muzzleModel[3] = {s.muzzleLocal[0], s.muzzleLocal[1], s.muzzleLocal[2]};
    float axisModel[3] = {s.muzzleAxis[0], s.muzzleAxis[1], s.muzzleAxis[2]};
    float upModel[3] = {s.muzzleUp[0], s.muzzleUp[1], s.muzzleUp[2]};
    EvalBones(look);
    float bobX = std::sin(look.bobPhase) * 0.008f * look.bobAmount * s.bobScale;
    float bobY = -std::fabs(std::cos(look.bobPhase)) * 0.010f * look.bobAmount * s.bobScale;
    float breathe = std::sin(look.time * 1.7f) * 0.0018f;
    Aff sway = AffMul(AffRotY(look.swayX * s.swayScale), AffRotX(look.swayY * s.swayScale));
    float aw = look.adsAlign < 0 ? 0.f : (look.adsAlign > 1 ? 1.f : look.adsAlign);
    sway = AffMul(sway, AffMul(AffRotY(bobX * aw / 0.45f), AffRotX(-(bobY + breathe) * aw / 0.45f)));
    Aff place = AffTrans(s.offset[0] + bobX * (1.f - aw), s.offset[1] + (bobY + breathe) * (1.f - aw), s.offset[2] - 0.05f * look.kick);
    Aff rot = AffMul(AffRotY(s.rotDeg[0] * kDeg), AffMul(AffRotX(s.rotDeg[1] * kDeg), AffRotZ(s.rotDeg[2] * kDeg)));
    sway = AffMul(sway, AffRotX(-5.0f * kDeg * look.kick));
    Aff scl = AffScale(s.mirrorX ? -s.scale : s.scale, s.scale, s.scale);
    Aff tilt = AffMul(AffRotY(s.tiltDeg[0] * kDeg), AffMul(AffRotX(s.tiltDeg[1] * kDeg), AffRotZ(s.tiltDeg[2] * kDeg)));
    Aff kick = AffIdentity();
    if (look.kickPos[0] || look.kickPos[1] || look.kickPos[2] || look.kickRot[0] || look.kickRot[1] || look.kickRot[2]) {
        float pv[3] = {s.offset[0] + look.kickPivot[0], s.offset[1] + look.kickPivot[1], s.offset[2] + look.kickPivot[2]};
        Aff r = AffMul(AffRotY(look.kickRot[1]), AffMul(AffRotX(-look.kickRot[0]), AffRotZ(look.kickRot[2])));
        kick = AffMul(AffTrans(pv[0] + look.kickPos[0], pv[1] + look.kickPos[1], pv[2] + look.kickPos[2]),
                      AffMul(r, AffTrans(-pv[0], -pv[1], -pv[2])));
    }
    Aff align = AffIdentity();
    if (aw > 0.f && s.adsSightBone >= 0 && s.adsSightBone < static_cast<int>(boneCount_)) {
        int ci = FindClip("ads_idle");
        Aff sight, camA = AffIdentity();
        if (ci >= 0 && BoneWorldAt(ci, 0.f, s.adsSightBone, sight.m) && (cameraBone_ < 0 || BoneWorldAt(ci, 0.f, cameraBone_, camA.m))) {
            Aff placeStatic = AffTrans(s.offset[0], s.offset[1], s.offset[2]);
            Aff V = AffMul(placeStatic, AffMul(tilt, AffMul(rot, AffMul(scl, AffMul(AffInverse(camA), sight)))));
            const float* sp = s.adsSightPoint;
            float p[3];
            for (int r = 0; r < 3; ++r) p[r] = V.m[r * 4] * sp[0] + V.m[r * 4 + 1] * sp[1] + V.m[r * 4 + 2] * sp[2] + V.m[r * 4 + 3];
            float R[9];
            for (int c = 0; c < 3; ++c) {
                float n = std::sqrt(V.m[c] * V.m[c] + V.m[4 + c] * V.m[4 + c] + V.m[8 + c] * V.m[8 + c]);
                for (int r = 0; r < 3; ++r) R[r * 3 + c] = n > 1e-8f ? V.m[r * 4 + c] / n : (r == c ? 1.f : 0.f);
            }
            float det = R[0] * (R[4] * R[8] - R[5] * R[7]) - R[1] * (R[3] * R[8] - R[5] * R[6]) + R[2] * (R[3] * R[7] - R[4] * R[6]);
            const float* sf = s.adsSightFront;
            float fl[3] = {sf[0] - sp[0], sf[1] - sp[1], sf[2] - sp[2]};
            if (det > 0.5f && fl[0] * fl[0] + fl[1] * fl[1] + fl[2] * fl[2] > 1e-6f) {
                float z[3], y[3] = {R[1], R[4], R[7]};
                for (int r = 0; r < 3; ++r) z[r] = V.m[r * 4] * sf[0] + V.m[r * 4 + 1] * sf[1] + V.m[r * 4 + 2] * sf[2] + V.m[r * 4 + 3] - p[r];
                float zn = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
                for (float& q : z) q /= zn;
                float yz = y[0] * z[0] + y[1] * z[1] + y[2] * z[2];
                for (int r = 0; r < 3; ++r) y[r] -= yz * z[r];
                float yn = std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
                if (yn > 1e-6f) {
                    for (float& q : y) q /= yn;
                    float x[3] = {y[1] * z[2] - y[2] * z[1], y[2] * z[0] - y[0] * z[2], y[0] * z[1] - y[1] * z[0]};
                    for (int r = 0; r < 3; ++r) R[r * 3] = x[r], R[r * 3 + 1] = y[r], R[r * 3 + 2] = z[r];
                }
            }
            if (det > 0.5f) {
                float F[9] = {R[0], R[3], R[6], R[1], R[4], R[7], R[2], R[5], R[8]};
                float cosA = (F[0] + F[4] + F[8] - 1.f) * 0.5f;
                cosA = cosA > 1.f ? 1.f : (cosA < -1.f ? -1.f : cosA);
                float ang = std::acos(cosA), ax[3] = {F[7] - F[5], F[2] - F[6], F[3] - F[1]};
                float an = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
                Aff rw = AffIdentity();
                if (an > 1e-8f && ang > 1e-6f) {
                    for (float& v : ax) v /= an;
                    float a = ang * aw, c = std::cos(a), sn = std::sin(a), t = 1.f - c;
                    float x = ax[0], y = ax[1], z = ax[2];
                    Aff r = {{t * x * x + c, t * x * y - sn * z, t * x * z + sn * y, 0, t * x * y + sn * z, t * y * y + c, t * y * z - sn * x, 0,
                              t * x * z - sn * y, t * y * z + sn * x, t * z * z + c, 0}};
                    rw = r;
                }
                align = AffMul(AffTrans(p[0] * (1.f - aw), p[1] * (1.f - aw), p[2]), AffMul(rw, AffTrans(-p[0], -p[1], -p[2])));
            }
        }
    }
    Aff N = AffMul(align, AffMul(place, AffMul(tilt, AffMul(rot, scl))));
    if (cameraBone_ >= 0) N = AffMul(N, AffInverse(g_world[cameraBone_]));
    bool grip = s.ikGripRi[0] || s.ikGripRi[1] || s.ikGripRi[2] || s.ikGripLe[0] || s.ikGripLe[1] || s.ikGripLe[2];
    bool kicked = kick.m[3] || kick.m[7] || kick.m[11] || kick.m[1] || kick.m[2] || kick.m[6];
    ikResidual_[0] = ikResidual_[1] = -1.f;
    ikShift_[0] = ikShift_[1] = 0.f;
    bool shifted = look.gunShift[0] != 0.f || look.gunShift[1] != 0.f || look.gunShift[2] != 0.f;
    bool shieldArm = look.shieldArmW > 1e-4f;
    shieldIkResidual_ = -1.f;
    Look lk = look;
    shieldRestK_ = 0.f;
    shieldRestGap_ = -1.f;
    const int restBone = s.shieldRestBone >= 0 && s.shieldRestBone < static_cast<int>(boneCount_) ? s.shieldRestBone : gunBone_;
    if (shieldArm && restBone >= 0 && s.shieldRest > 0.f && ArmRig() && !s.mirrorX &&
        (s.shieldNotch[0] != 0.f || s.shieldNotch[1] != 0.f || s.shieldNotch[2] != 0.f) &&
        (s.shieldRestPoint[0] != 0.f || s.shieldRestPoint[1] != 0.f || s.shieldRestPoint[2] != 0.f)) {
        float hw = look.shieldHoldW < 0.f ? 0.f : (look.shieldHoldW > 1.f ? 1.f : look.shieldHoldW);
        float ka = aw / 0.35f;
        ka = ka >= 1.f ? 1.f : ka * ka * (3.f - 2.f * ka);
        float k = ka * hw * s.shieldRest;
        if (k > 1e-4f) {
            const float nl[3] = {s.shieldNotch[0] + s.shieldRestSide, s.shieldNotch[1], s.shieldNotch[2]};
            const float* P = look.shieldPose;
            float nv[3], a[3];
            for (int r = 0; r < 3; ++r) nv[r] = P[r * 4] * nl[0] + P[r * 4 + 1] * nl[1] + P[r * 4 + 2] * nl[2] + P[r * 4 + 3], shieldNotch_[r] = nl[r];
            auto viewPt = [&](int bone, const float* p, float* out) {
                Aff gv = AffMul(N, g_world[bone]);
                for (int r = 0; r < 3; ++r) out[r] = gv.m[r * 4] * p[0] + gv.m[r * 4 + 1] * p[1] + gv.m[r * 4 + 2] * p[2] + gv.m[r * 4 + 3] + look.gunShift[r];
            };
            viewPt(restBone, s.shieldRestPoint, a);
            const int bone2 = s.shieldRestBone2 == -1 ? gunBone_ : s.shieldRestBone2;
            shieldRestT_ = -1.f;
            for (int r = 0; r < 3; ++r) shieldRestA_[r] = shieldRestB_[r] = a[r];
            if (bone2 >= 0 && bone2 < static_cast<int>(boneCount_)) {
                float b[3];
                viewPt(bone2, s.shieldRestPoint2, b);
                b[1] -= s.shieldRestDrop2;
                for (int r = 0; r < 3; ++r) shieldRestB_[r] = b[r];
                float dz = b[2] - a[2], t = std::fabs(dz) > 1e-5f ? (nv[2] - a[2]) / dz : 0.f;
                t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
                for (int r = 0; r < 3; ++r) a[r] += (b[r] - a[r]) * t;
                shieldRestT_ = t;
            }
            for (int r = 0; r < 3; ++r) {
                shieldRestView_[r] = a[r];
                lk.shieldPose[r * 4 + 3] += (a[r] - nv[r]) * k;
            }
            shieldRestK_ = k;
        }
    }
    if (ArmRig() && !s.mirrorX && ((s.handIk && (kicked || grip)) || shifted || shieldArm)) {
        Aff kAll = shifted ? AffMul(AffTrans(look.gunShift[0], look.gunShift[1], look.gunShift[2]), kick) : kick;
        Aff nInv = AffInverse(N);
        Aff kModel = AffMul(nInv, AffMul(kAll, N));
        ArmIk(s, kModel.m);
        kick = AffIdentity();
        if (shieldArm) ShieldIk(lk, nInv.m);
    }
    Aff model = AffMul(sway, AffMul(kick, N));
    {
        Aff P;
        memcpy(P.m, lk.shieldPose, sizeof P.m);
        Aff bobT = AffTrans(bobX * (1.f - aw), (bobY + breathe) * (1.f - aw), 0.f);
        Aff sv = AffMul(sway, AffMul(bobT, P));
        const float* hq = look.shieldHandQuat;
        float ha = look.shieldArmW < 0.f ? 0.f : (look.shieldArmW > 1.f ? 1.f : look.shieldArmW);
        float reach = shieldIkResidual_ >= 0.f ? 1.f - shieldIkResidual_ / 40.f : 0.f;
        ha *= reach < 0.f ? 0.f : (reach > 1.f ? 1.f : reach);
        ha *= look.shieldHoldW < 0.f ? 0.f : (look.shieldHoldW > 1.f ? 1.f : look.shieldHoldW);
        if ((hq[0] != 0.f || hq[1] != 0.f || hq[2] != 0.f || hq[3] != 0.f) && ha > 0.f && shieldIkResidual_ >= 0.f && armBone_[1][2] >= 0) {
            Aff ws = AffIdentity();
            QuatToRot(hq, ws);
            ws.m[3] = look.shieldGrip[0], ws.m[7] = look.shieldGrip[1], ws.m[11] = look.shieldGrip[2];
            Aff hand = AffMul(model, g_world[armBone_[1][2]]);
            sv = AffBlend(sv, AffMul(hand, AffInverse(ws)), ha);
        }
        memcpy(shieldView_, sv.m, sizeof shieldView_);
        shieldViewValid_ = true;
        if (shieldRestK_ > 0.f) {
            float a[3], b[3], n[3], ab[3], an[3];
            const float* nl = shieldNotch_;
            for (int r = 0; r < 3; ++r) {
                a[r] = sway.m[r * 4] * shieldRestA_[0] + sway.m[r * 4 + 1] * shieldRestA_[1] + sway.m[r * 4 + 2] * shieldRestA_[2] + sway.m[r * 4 + 3];
                b[r] = sway.m[r * 4] * shieldRestB_[0] + sway.m[r * 4 + 1] * shieldRestB_[1] + sway.m[r * 4 + 2] * shieldRestB_[2] + sway.m[r * 4 + 3];
                n[r] = sv.m[r * 4] * nl[0] + sv.m[r * 4 + 1] * nl[1] + sv.m[r * 4 + 2] * nl[2] + sv.m[r * 4 + 3];
                ab[r] = b[r] - a[r], an[r] = n[r] - a[r];
            }
            float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
            float t = l2 > 1e-10f ? (an[0] * ab[0] + an[1] * ab[1] + an[2] * ab[2]) / l2 : 0.f;
            t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
            float d2 = 0.f;
            for (int r = 0; r < 3; ++r) d2 += (an[r] - ab[r] * t) * (an[r] - ab[r] * t);
            shieldRestGap_ = std::sqrt(d2) * 1000.f;
        }
        if (armBone_[1][2] >= 0) {
            Aff wv = AffMul(model, g_world[armBone_[1][2]]);
            Pos(wv, leftWristView_);
        }
    }
    FinishBones(palette, s.muzzleBone, s.muzzleLocal, muzzleModel, s.muzzleAxis, s.muzzleUp, axisModel, upModel);
    if (poseDumpPath_[0]) {
        FILE* f = _wfopen(poseDumpPath_, L"wb");
        if (f) {
            unsigned n = boneCount_;
            fwrite("HPD1", 1, 4, f);
            fwrite(&n, 4, 1, f);
            fwrite(model.m, sizeof(float), 12, f);
            fwrite(shieldView_, sizeof(float), 12, f);
            fwrite(palette, sizeof(float), n * 12, f);
            fclose(f);
        }
        poseDumpPath_[0] = 0;
    }
    float det = AffDet(model);
    CFrame cf = {};
    float aspect = static_cast<float>(w) / static_cast<float>(h);
    const float vmFov = (look.fovDeg > 1.f && look.fovDeg < 170.f) ? look.fovDeg : s.fovDeg;
    float ys = 1.0f / std::tan(vmFov * kDeg * 0.5f), xs = ys / aspect;
    const float zn = 0.01f, zf = 50.0f;
    float P[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), -zn * zf / (zf - zn), 0, 0, 1, 0};
    memcpy(cf.proj, P, sizeof P);
    projXs_ = xs, projYs_ = ys;
    muzzleValid_ = false;
    muzzleFrameValid_ = false;
    if (s.muzzleBone >= -1 && (s.muzzleBone < 0 || s.muzzleBone < static_cast<int>(boneCount_))) {
        float v[3];
        for (int r = 0; r < 3; ++r)
            v[r] = model.m[r * 4 + 0] * muzzleModel[0] + model.m[r * 4 + 1] * muzzleModel[1] +
                   model.m[r * 4 + 2] * muzzleModel[2] + model.m[r * 4 + 3];
        if (v[2] > 0.02f && std::isfinite(v[0]) && std::isfinite(v[1])) {
            muzzleNdc_[0] = v[0] * xs / v[2];
            muzzleNdc_[1] = v[1] * ys / v[2];
            muzzleAspect_ = aspect;
            muzzleValid_ = std::fabs(muzzleNdc_[0]) < 4.f && std::fabs(muzzleNdc_[1]) < 4.f;
        }
        float af[3], au[3];
        for (int r = 0; r < 3; ++r) {
            af[r] = model.m[r * 4 + 0] * axisModel[0] + model.m[r * 4 + 1] * axisModel[1] + model.m[r * 4 + 2] * axisModel[2];
            au[r] = model.m[r * 4 + 0] * upModel[0] + model.m[r * 4 + 1] * upModel[1] + model.m[r * 4 + 2] * upModel[2];
        }
        float cfgLen = std::sqrt(s.muzzleAxis[0] * s.muzzleAxis[0] + s.muzzleAxis[1] * s.muzzleAxis[1] + s.muzzleAxis[2] * s.muzzleAxis[2]);
        float fl = std::sqrt(af[0] * af[0] + af[1] * af[1] + af[2] * af[2]);
        muzzleFrameValid_ = muzzleValid_ && fl > 1e-6f && cfgLen > 1e-6f;
        if (muzzleFrameValid_) {
            MuzzleFrame& mf = muzzleFrame_;
            memcpy(mf.pos, v, sizeof v);
            for (int r = 0; r < 3; ++r) mf.fwd[r] = af[r] / fl;
            float d = au[0] * mf.fwd[0] + au[1] * mf.fwd[1] + au[2] * mf.fwd[2];
            for (int r = 0; r < 3; ++r) au[r] -= mf.fwd[r] * d;
            float ul = std::sqrt(au[0] * au[0] + au[1] * au[1] + au[2] * au[2]);
            if (ul < 1e-6f) au[0] = 0, au[1] = 1, au[2] = 0, ul = 1;
            for (int r = 0; r < 3; ++r) mf.up[r] = au[r] / ul;
            mf.left[0] = mf.up[1] * mf.fwd[2] - mf.up[2] * mf.fwd[1];
            mf.left[1] = mf.up[2] * mf.fwd[0] - mf.up[0] * mf.fwd[2];
            mf.left[2] = mf.up[0] * mf.fwd[1] - mf.up[1] * mf.fwd[0];
            mf.unit = 0.0254f * fl / cfgLen;
            mf.zn = zn, mf.zf = zf;
            mf.xs = xs;
            mf.ys = ys;
            mf.brass = false;
            if (brassBone_ >= 0 && brassBone_ < static_cast<int>(boneCount_) && !s.mirrorX) {
                Aff bv = AffMul(model, g_world[brassBone_]);
                float o[3], u[3], fw[3];
                for (int r = 0; r < 3; ++r) mf.bpos[r] = bv.m[r * 4 + 3], o[r] = bv.m[r * 4 + 2], u[r] = bv.m[r * 4 + 1], fw[r] = -bv.m[r * 4];
                float ol = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
                float du = (u[0] * o[0] + u[1] * o[1] + u[2] * o[2]) / (ol > 1e-8f ? ol * ol : 1.f);
                for (int r = 0; r < 3; ++r) u[r] -= o[r] * du;
                float ul2 = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
                if (ol > 1e-6f && ul2 > 1e-6f && std::isfinite(mf.bpos[0] + mf.bpos[1] + mf.bpos[2])) {
                    for (int r = 0; r < 3; ++r) mf.bout[r] = o[r] / ol, mf.bup[r] = u[r] / ul2;
                    float c[3] = {mf.bup[1] * mf.bout[2] - mf.bup[2] * mf.bout[1], mf.bup[2] * mf.bout[0] - mf.bup[0] * mf.bout[2],
                                  mf.bup[0] * mf.bout[1] - mf.bup[1] * mf.bout[0]};
                    float sg = c[0] * fw[0] + c[1] * fw[1] + c[2] * fw[2] >= 0.f ? 1.f : -1.f;
                    for (int r = 0; r < 3; ++r) mf.bfwd[r] = c[r] * sg;
                    mf.brass = true;
                }
            }
        }
    }
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) cf.model[r * 4 + c] = model.m[r * 4 + c];
    cf.model[12] = cf.model[13] = cf.model[14] = 0;
    cf.model[15] = 1;
    float ln = std::sqrt(s.lightDir[0] * s.lightDir[0] + s.lightDir[1] * s.lightDir[1] + s.lightDir[2] * s.lightDir[2]);
    if (ln < 1e-6f) ln = 1;
    for (int k = 0; k < 3; ++k) {
        cf.lightDir[k] = s.lightDir[k] / ln;
        cf.lightColor[k] = s.lightColor[k] * s.lightIntensity;
        cf.ambSky[k] = s.ambSky[k] * s.ambIntensity;
        cf.ambGround[k] = s.ambGround[k] * s.ambIntensity;
    }
    cf.params[0] = s.exposure;
    cf.params[1] = rtvEncodesSrgb ? 0.f : 1.f;
    cf.params[2] = look.time;
    cf.params[3] = det >= 0 ? 1.f : -1.f;
    if (look.flashLight > 0.f && s.flashLight > 0.f && muzzleFrameValid_) {
        const MuzzleFrame& mf = muzzleFrame_;
        for (int k = 0; k < 3; ++k) cf.flashPos[k] = mf.pos[k] + mf.fwd[k] * 4.f * mf.unit;
        cf.flashPos[3] = 80.f * mf.unit * (0.35f + 0.65f * look.flashLight);
        const float c[3] = {1.f, 192.f / 255.f, 64.f / 255.f};
        for (int k = 0; k < 3; ++k) cf.flashColor[k] = c[k] * 2.5f * s.flashLight * look.flashLight;
        cf.flashColor[3] = 1.f;
    }
    bool lensOn = false;
    const int lensBone = s.scopeBone >= 0 ? s.scopeBone : s.adsSightBone;
    if (s.scopeZoom > 1.f && aw > 0.f && lensSubs_ && lensBone >= 0 && lensBone < static_cast<int>(boneCount_)) {
        Aff lv = AffMul(model, g_world[lensBone]);
        float p[3] = {lv.m[3], lv.m[7], lv.m[11]}, d[3] = {lv.m[2], lv.m[6], lv.m[10]};
        float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dl > 1e-6f && d[2] > 0.05f * dl && p[2] > zn) {
            float hw = 0.5f * static_cast<float>(w), hh = 0.5f * static_cast<float>(h);
            cf.lens0[0] = hw + hw * xs * d[0] / d[2];
            cf.lens0[1] = hh - hh * ys * d[1] / d[2];
            cf.lens0[2] = hh * ys * s.scopeLensRadius * dl / p[2];
            cf.lens0[3] = s.scopeZoom;
            cf.lens1[0] = hw + hw * xs * p[0] / p[2];
            cf.lens1[1] = hh - hh * ys * p[1] / p[2];
            cf.lens1[2] = aw;
            cf.lens1[3] = s.scopeReticle;
            cf.lens2[0] = 1.f / static_cast<float>(w);
            cf.lens2[1] = 1.f / static_cast<float>(h);
            lensOn = true;
        }
    }
    StateBackup bk;
    bk.Save(ctx);
    bool haveScene = false;
    const double tProfScene = ProfNow();
    if (sceneTex && (s.sceneLight > 0.001f || lensOn)) {
        D3D11_TEXTURE2D_DESC sd;
        sceneTex->GetDesc(&sd);
        DXGI_FORMAT f = sd.Format;
        switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: f = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: f = DXGI_FORMAT_B8G8R8A8_UNORM; break;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: f = DXGI_FORMAT_R10G10B10A2_UNORM; break;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: f = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        default: break;
        }
        if (sd.SampleDesc.Count == 1 && sd.Width == w && sd.Height == h && EnsureScene(w, h, f)) {
            const ScreenBufs& b = Sb();
            ctx->CopySubresourceRegion(b.sceneTex, 0, 0, 0, 0, sceneTex, 0, nullptr);
            ctx->GenerateMips(b.sceneSrv);
            haveScene = true;
            cf.scene[0] = s.sceneLight;
            cf.scene[1] = static_cast<float>(b.sceneMips >= 2 ? b.sceneMips - 2 : 0);
            cf.scene[2] = (f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                           f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                              ? 0.f
                              : 1.f;
            cf.scene[3] = 1.f;
        }
    }
    sceneFresh_ = haveScene;
    sceneGammaEnc_ = haveScene && cf.scene[2] > 0.5f;
    g_drawProf.scene = (ProfNow() - tProfScene) * 1000.0;
    ID3D11DepthStencilView* dsv_ = Sb().dsv;
    ID3D11ShaderResourceView* sceneSrv_ = Sb().sceneSrv;
    D3D11_MAPPED_SUBRESOURCE mp;
    if (SUCCEEDED(ctx->Map(cbFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) {
        memcpy(mp.pData, &cf, sizeof cf);
        ctx->Unmap(cbFrame_, 0);
    }
    if (SUCCEEDED(ctx->Map(cbBones_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) {
        memcpy(mp.pData, palette, boneCount_ * 12 * sizeof(float));
        ctx->Unmap(cbBones_, 0);
    }
    ctx->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->OMSetRenderTargets(1, &rtv, dsv_);
    D3D11_VIEWPORT vp = {0, 0, static_cast<float>(w), static_cast<float>(h), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs_);
    const float bf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(bs_, bf, 0xffffffffu);
    ctx->OMSetDepthStencilState(dss_, 0);
    ctx->IASetInputLayout(layout_);
    UINT stride = kVertexStride, voff = 0;
    ctx->IASetVertexBuffers(0, 1, &vb_, &stride, &voff);
    ctx->IASetIndexBuffer(ib_, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_, nullptr, 0);
    ctx->PSSetShader(ps_, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* vcb[2] = {cbFrame_, cbBones_};
    ctx->VSSetConstantBuffers(0, 2, vcb);
    ID3D11Buffer* pcb[3] = {cbFrame_, nullptr, cbMat_};
    ctx->PSSetConstantBuffers(0, 3, pcb);
    ctx->PSSetSamplers(0, 1, &samp_);
    ID3D11ShaderResourceView* sceneView = haveScene ? sceneSrv_ : white_;
    ctx->PSSetShaderResources(3, 1, &sceneView);
    const double tProfWh3 = ProfNow();
    SetupWh3Material(s);
    g_drawProf.wh3 = (ProfNow() - tProfWh3) * 1000.0;
    float em[44] = {};
    if (wh3On_) {
        if (s.wh3Emissive) {
            double t = look.time;
            bool prop = s.wh3Emissive == 2;
            for (int k = 0; k < 2; ++k) {
                double v = prop ? 0.05 * s.emissiveSpeed[k] : static_cast<double>(s.emissiveSpeed[0]) * s.emissiveDirection[k];
                double per = std::fabs(s.emissiveTiling[k]) > 1e-4 ? 1.0 / std::fabs(s.emissiveTiling[k]) : 1.0;
                em[8 + k] = static_cast<float>(std::fmod(t * v, per));
                em[10 + k] = s.emissiveTiling[k];
            }
            double ph = prop ? s.emissivePulseSpeed * (t + 0.5) : s.emissivePulseSpeed * t;
            em[0] = static_cast<float>(s.wh3Emissive);
            em[1] = s.emissiveStrength;
            em[2] = s.emissiveFresnelStrength;
            em[3] = s.emissiveDistort;
            em[4] = s.emissiveTint[0], em[5] = s.emissiveTint[1], em[6] = s.emissiveTint[2];
            em[7] = static_cast<float>(0.1 * s.emissivePulseStrength * (std::sin(std::fmod(ph, 6.283185307179586)) + 0.99));
            for (int k = 0; k < 4; ++k) {
                em[16 + k] = s.emissiveGradientStop[k];
                for (int j = 0; j < 3; ++j) em[20 + k * 4 + j] = s.emissiveGradientColour[k][j];
            }
        }
        em[12] = s.emissiveScale;
        em[13] = s.emissiveSoft ? 1.f : 0.f;
        em[14] = s.wh3Iridescent ? 1.f : 0.f;
        em[15] = s.iridescenceAmount;
        for (int j = 0; j < 3; ++j) em[36 + j] = s.iridescenceColourA[j], em[40 + j] = s.iridescenceColourB[j];
        ID3D11ShaderResourceView* t3[3] = {wh3Srv_[0], wh3Srv_[1] ? wh3Srv_[1] : white_, wh3Srv_[2] ? wh3Srv_[2] : white_};
        ctx->PSSetShaderResources(4, 3, t3);
    }
    for (unsigned i = 0; i < subCount_; ++i) {
        const Submesh& sm = subs_[i];
        bool hasDef = (sm.flags >> 17) & 1;
        float mat[12] = {static_cast<float>(sm.normalMode), sm.tex[1] >= 0 ? 1.f : 0.f, sm.tex[2] >= 0 ? 1.f : 0.f,
                         static_cast<float>(sm.materialMode),
                         hasDef ? (sm.flags & 0xff) / 255.f : 0.55f, hasDef ? ((sm.flags >> 8) & 0xff) / 255.f : 0.f,
                         ((sm.flags >> 18) & 1) ? 1.f : 0.f, ((sm.flags >> 16) & 1) ? 1.f : 0.f,
                         ((sm.flags >> 19) & 1) ? 1.f : 0.f, ((sm.flags >> 20) & 1) ? 1.f : 0.f, 0.f, 0.f};
        if (SUCCEEDED(ctx->Map(cbMat_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) {
            memcpy(mp.pData, mat, sizeof mat);
            if (wh3On_ && sm.wh3) memcpy(static_cast<char*>(mp.pData) + sizeof mat, em, sizeof em);
            else memset(static_cast<char*>(mp.pData) + sizeof mat, 0, sizeof em);
            ctx->Unmap(cbMat_, 0);
        }
        ID3D11ShaderResourceView* srv[3];
        for (int k = 0; k < 3; ++k) srv[k] = sm.tex[k] >= 0 ? srv_[sm.tex[k]] : white_;
        ctx->PSSetShaderResources(0, 3, srv);
        ctx->DrawIndexed(sm.indexCount, sm.firstIndex, 0);
    }
    bk.Restore(ctx);
    g_drawProf.total = (ProfNow() - tProf0) * 1000.0;
    return true;
}
void Renderer::SetupWh3Material(const Settings& s) {
    bool any = s.wh3Emissive || s.wh3Iridescent;
    uint64_t key = any ? Fnv64(s.wh3Part, sizeof s.wh3Part) ^ Fnv64(s.wh3Mask, sizeof s.wh3Mask) * 3 ^
                             Fnv64(s.emissiveTexture, sizeof s.emissiveTexture) * 5 ^ Fnv64(s.emissiveDistortion, sizeof s.emissiveDistortion) * 7 ^
                             (static_cast<uint64_t>(s.wh3Emissive) << 1 | (s.wh3Iridescent ? 1u : 0u))
                       : 0;
    if (key == wh3Key_) return;
    wh3Key_ = key;
    wh3On_ = false;
    for (auto*& v : wh3Srv_) SafeRelease(v);
    for (unsigned i = 0; i < subCount_; ++i) subs_[i].wh3 = false;
    if (!any || !dev_) return;
    wchar_t parts[256];
    wcsncpy(parts, s.wh3Part, 255);
    parts[255] = 0;
    _wcslwr(parts);
    unsigned hit = 0;
    for (unsigned i = 0; i < subCount_; ++i) {
        int a = subs_[i].tex[0];
        if (a < 0 || a >= static_cast<int>(kMaxTextures) || !texName_[a][0]) continue;
        size_t n = wcslen(texName_[a]);
        for (const wchar_t* p = parts; *p;) {
            const wchar_t* e = wcschr(p, L';');
            size_t len = e ? static_cast<size_t>(e - p) : wcslen(p);
            if (len == n && !wcsncmp(p, texName_[a], n)) {
                subs_[i].wh3 = true;
                ++hit;
                break;
            }
            if (!e) break;
            p = e + 1;
        }
    }
    if (!hit) return;
    const wchar_t* rel[3] = {s.wh3Mask, s.emissiveTexture, s.emissiveDistortion};
    for (int k = 0; k < (s.wh3Emissive ? 3 : 1); ++k) {
        wchar_t full[MAX_PATH * 2];
        swprintf(full, MAX_PATH * 2, L"%ls%ls", hvmDir_, rel[k]);
        char err[256];
        if (!rel[k][0] || !LoadDds(dev_, full, false, &wh3Srv_[k], err, sizeof err)) {
            for (auto*& v : wh3Srv_) SafeRelease(v);
            return;
        }
    }
    wh3On_ = true;
}
void Renderer::Release() {
    ready_ = false;
    for (auto*& v : wh3Srv_) SafeRelease(v);
    wh3Key_ = 0;
    wh3On_ = false;
    SafeRelease(vb_);
    SafeRelease(ib_);
    SafeRelease(cbFrame_);
    SafeRelease(cbBones_);
    SafeRelease(cbMat_);
    SafeRelease(vs_);
    SafeRelease(ps_);
    SafeRelease(layout_);
    SafeRelease(rs_);
    SafeRelease(dss_);
    SafeRelease(bs_);
    SafeRelease(samp_);
    own_.Release();
    SafeRelease(white_);
    sceneBroken_ = false;
    distBroken_ = false;
    distDepthOk_ = false;
    for (auto*& s : srv_) SafeRelease(s);
    texCount_ = 0;
    delete[] subs_;
    subs_ = nullptr;
    subCount_ = 0;
    lensSubs_ = 0;
    delete[] bones_;
    bones_ = nullptr;
    boneCount_ = 0;
    delete[] clips_;
    clips_ = nullptr;
    clipCount_ = 0;
    delete[] clipData_;
    clipData_ = nullptr;
    cameraBone_ = -1;
    barrelBone_ = -1;
    brassBone_ = -1;
    barrelDir_ = -1.f;
    for (int sd = 0; sd < 2; ++sd) {
        armBone_[sd][0] = armBone_[sd][1] = armBone_[sd][2] = -1;
        handTag_[sd] = -1;
        ikResidual_[sd] = 0.f;
        twistN_[sd] = 0;
        twistHave_[sd] = false;
    }
    gunBone_ = -1;
    muzzleValid_ = false;
    muzzleFrameValid_ = false;
    indexCount_ = 0;
    SafeRelease(dev_);
}
namespace {
const char kCrossShader[] = R"HLSL(struct VI { float2 p : POSITION; float4 c : COLOR; float3 m : TEXCOORD0; };
struct VO { float4 p : SV_Position; float4 c : COLOR; float3 m : TEXCOORD0; };
VO VS(VI i) { VO o; o.p = float4(i.p, 0, 1); o.c = i.c; o.m = i.m; return o; }
float4 PS(VO i) : SV_Target {
    float4 c = i.c;
    if (i.m.z > 0) c.a *= saturate(i.m.z - length(i.m.xy) + 0.5);
    return c;
}
)HLSL";
struct CrossVertex {
    float x, y;
    float r, g, b, a;
    float mx, my, mr;
};
float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float IniCross(const wchar_t* ini, const wchar_t* sec, const wchar_t* key, float def) {
    wchar_t buf[64];
    GetPrivateProfileStringW(sec, key, L"", buf, 64, ini);
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    return (end != buf && std::isfinite(v)) ? static_cast<float>(v) : def;
}
void IniCrossColor(const wchar_t* ini, const wchar_t* sec, const wchar_t* key, float* c) {
    wchar_t buf[128];
    GetPrivateProfileStringW(sec, key, L"", buf, 128, ini);
    if (!buf[0]) return;
    float v[4] = {c[0] * 255.f, c[1] * 255.f, c[2] * 255.f, c[3] * 255.f};
    wchar_t* p = buf;
    for (int k = 0; k < 4 && *p; ++k) {
        wchar_t* end = nullptr;
        double d = wcstod(p, &end);
        if (end == p) break;
        v[k] = static_cast<float>(d);
        p = end;
        while (*p == L',' || *p == L' ') ++p;
    }
    for (int k = 0; k < 4; ++k) c[k] = std::fmin(std::fmax(v[k] / 255.f, 0.f), 1.f);
}
void ReadCrossSection(const wchar_t* ini, const wchar_t* sec, CrosshairSettings& s) {
    s.enabled = IniCross(ini, sec, L"enabled", s.enabled ? 1.f : 0.f) != 0.f;
    s.length = IniCross(ini, sec, L"length", s.length);
    s.gap = IniCross(ini, sec, L"gap", s.gap);
    s.thickness = IniCross(ini, sec, L"thickness", s.thickness);
    s.outline = IniCross(ini, sec, L"outline", s.outline);
    s.dot = IniCross(ini, sec, L"dot", s.dot ? 1.f : 0.f) != 0.f;
    s.dotSize = IniCross(ini, sec, L"dot_size", s.dotSize);
    s.dotOutline = IniCross(ini, sec, L"dot_outline", s.dotOutline);
    s.spreadShot = IniCross(ini, sec, L"spread_shot", s.spreadShot);
    s.spreadMove = IniCross(ini, sec, L"spread_move", s.spreadMove);
    s.spreadMax = IniCross(ini, sec, L"spread_max", s.spreadMax);
    s.spreadRecover = IniCross(ini, sec, L"spread_recover", s.spreadRecover);
    s.spreadHold = IniCross(ini, sec, L"spread_hold", s.spreadHold);
    s.fadeStart = IniCross(ini, sec, L"fade_start", s.fadeStart);
    s.fadeEnd = IniCross(ini, sec, L"fade_end", s.fadeEnd);
    float oa = IniCross(ini, sec, L"outline_alpha", s.outlineAlpha < 0.f ? -1.f : s.outlineAlpha * 255.f);
    s.outlineAlpha = oa < 0.f ? -1.f : std::fmin(oa / 255.f, 1.f);
    IniCrossColor(ini, sec, L"color", s.color);
}
}
bool LoadCrosshair(const wchar_t* ini, CrosshairSettings& s) {
    ReadCrossSection(ini, L"crosshair", s);
    wchar_t style[64], sec[96];
    GetPrivateProfileStringW(L"crosshair", L"style", L"", style, 64, ini);
    if (style[0]) {
        swprintf(sec, 96, L"crosshair_%ls", style);
        ReadCrossSection(ini, sec, s);
    }
    if (!(s.length >= 0.f && s.length <= 200.f)) s.length = 7.f;
    if (!(s.gap >= 0.f && s.gap <= 200.f)) s.gap = 4.f;
    if (!(s.thickness > 0.f && s.thickness <= 50.f)) s.thickness = 2.f;
    if (!(s.outline >= 0.f && s.outline <= 20.f)) s.outline = 1.f;
    if (!(s.dotSize >= 0.f && s.dotSize <= 50.f)) s.dotSize = 0.f;
    if (!(s.dotOutline >= 0.f && s.dotOutline <= 20.f)) s.dotOutline = 0.f;
    if (!(s.spreadShot >= 0.f && s.spreadShot <= 50.f)) s.spreadShot = 0.f;
    if (!(s.spreadMove >= 0.f && s.spreadMove <= 100.f)) s.spreadMove = 0.f;
    if (!(s.spreadMax >= 0.f && s.spreadMax <= 200.f)) s.spreadMax = 16.f;
    if (!(s.spreadRecover >= 0.5f && s.spreadRecover <= 60.f)) s.spreadRecover = 6.f;
    if (!(s.spreadHold >= 0.f && s.spreadHold <= 2.f)) s.spreadHold = 0.f;
    if (!(s.fadeStart >= 0.f && s.fadeStart <= 200.f)) s.fadeStart = 0.f;
    if (!(s.fadeEnd >= 0.f && s.fadeEnd <= 200.f)) s.fadeEnd = 0.f;
    return true;
}
namespace {
const char kFlashShader[] = R"(cbuffer C : register(b0) { float4 opt; float4 opt2; };
Texture2D T : register(t0);
SamplerState S : register(s0);
struct V { float2 p : POSITION; float2 uv : TEXCOORD0; float4 c : COLOR0; float3 m : TEXCOORD1; };
struct P { float4 p : SV_POSITION; float2 uv : TEXCOORD0; float4 c : COLOR0; float3 m : TEXCOORD1; };
P VS(V v) { P o; o.p = float4(v.p, v.m.z, 1); o.uv = v.uv; o.c = v.c; o.m = v.m; return o; }
float4 PS(P i) : SV_TARGET {
    float4 t = T.Sample(S, i.uv);
    float a = t.a * i.c.a;
    float3 c = t.rgb * i.c.rgb * i.m.y;
    if (opt.x > 0.5) c = pow(max(c, 0), 2.2);
    return i.m.x > 0.5 ? float4(c * a, 0) : float4(c * a, a);
}
Texture2D Sc : register(t1);
Texture2D<float> Dp : register(t2);
float4 PSD(P i) : SV_TARGET {
    float2 q = i.uv * 2 - 1;
    float r = length(q);
    float edge = saturate((1 - r) * 2.5);
    float4 t = T.Sample(S, i.uv);
    float2 n = (i.m.y > 0.5 ? float2(t.r, T.Sample(S, i.uv.yx * 0.83 + 0.09).r) : t.rg) * 2 - 1;
    float front = exp(-pow((r - 0.62) / 0.2, 2));
    float2 d = r > 1e-3 ? q / r : float2(0, 0);
    float2 off = (n * t.a * i.c.r - d * front * i.c.g) * i.c.b * edge * i.c.a;
    off.x *= opt.z / opt.w;
    float2 uv0 = i.p.xy * opt.zw, uv = uv0 + off;
    if (opt2.x > 0.5) {
        float4 g = Dp.Gather(S, uv);
        if (min(min(g.x, g.y), min(g.z, g.w)) < i.p.z) uv = uv0;
    }
    float3 c = Sc.SampleLevel(S, uv, 0).rgb;
    if (opt.y > 0.5) c = pow(max(c, 0), 2.2);
    float a = saturate(edge * 4);
    return float4(c * a, a);
}
)";
struct FlashVertex {
    float x, y, u, v;
    float r, g, b, a;
    float additive, bright, depth;
};
struct FlashCb {
    float opt[4];
    float opt2[4];
};
constexpr int kFlashTexVent = 0, kFlashTexFire = 1, kFlashTexGlow = 2, kFlashTexSmoke = 3, kFlashTexSpark = 4;
constexpr int kFlashTexGasJet = 5, kFlashTexGasPuff = 6;
constexpr int kFlashTexDistRing = 7, kFlashTexDistHaze = 8, kFlashTexGasBall = 9, kFlashTexGasWisp = 10, kFlashTexCount = 11;
const wchar_t* const kFlashTexNames[kFlashTexCount] = {L"flash_vent", L"flash_fire", L"flash_glow", L"hit_mist",
                                                       L"tracer", L"gas_jet", L"gas_puff", L"dist_ring", L"dist_haze",
                                                       L"gas_ball", L"gas_wisp"};
constexpr float kTracerSpeed = 13000.f * 0.0254f, kTracerMaxLen = 500.f * 0.0254f, kTracerHalfWidth = 3.f * 0.0254f;
ID3D11ShaderResourceView* LoadDdsBc(ID3D11Device* dev, const wchar_t* path, char* err, size_t errLen) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(err, errLen, "cannot open %ls", path);
        return nullptr;
    }
    DWORD size = GetFileSize(h, nullptr), got = 0;
    unsigned char* b = (size > 128 && size < (32u << 20)) ? static_cast<unsigned char*>(malloc(size)) : nullptr;
    bool rd = b && ReadFile(h, b, size, &got, nullptr) && got == size;
    CloseHandle(h);
    if (!rd || memcmp(b, "DDS ", 4)) {
        free(b);
        snprintf(err, errLen, "not a DDS: %ls", path);
        return nullptr;
    }
    uint32_t hh = 0, ww = 0, mips = 0;
    memcpy(&hh, b + 12, 4);
    memcpy(&ww, b + 16, 4);
    memcpy(&mips, b + 28, 4);
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    unsigned block = 0;
    if (!memcmp(b + 84, "DXT1", 4)) fmt = DXGI_FORMAT_BC1_UNORM, block = 8;
    if (!memcmp(b + 84, "DXT5", 4)) fmt = DXGI_FORMAT_BC3_UNORM, block = 16;
    if (!block || !ww || !hh || ww > 4096 || hh > 4096) {
        free(b);
        snprintf(err, errLen, "unsupported DDS (need DXT1/DXT5): %ls", path);
        return nullptr;
    }
    if (mips < 1) mips = 1;
    if (mips > 13) mips = 13;
    D3D11_SUBRESOURCE_DATA sd[13] = {};
    size_t off = 128;
    UINT n = 0;
    for (UINT m = 0; m < mips; ++m) {
        UINT mw = ww >> m ? ww >> m : 1, mh = hh >> m ? hh >> m : 1;
        UINT pitch = ((mw + 3) / 4) * block, rows = (mh + 3) / 4;
        if (off + static_cast<size_t>(pitch) * rows > size) break;
        sd[m].pSysMem = b + off;
        sd[m].SysMemPitch = pitch;
        off += static_cast<size_t>(pitch) * rows;
        ++n;
    }
    if (!n) {
        free(b);
        snprintf(err, errLen, "DDS truncated: %ls", path);
        return nullptr;
    }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = ww;
    td.Height = hh;
    td.MipLevels = n;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&td, sd, &tex)) && tex) dev->CreateShaderResourceView(tex, nullptr, &srv);
    SafeRelease(tex);
    free(b);
    if (!srv) snprintf(err, errLen, "texture create failed: %ls", path);
    return srv;
}
void LoadSheet(const wchar_t* path, MuzzleFlash::Sheet& sh) {
    sh = MuzzleFlash::Sheet();
    FILE* f = _wfopen(path, L"r");
    if (f) {
        int q, k;
        float r[4];
        char line[160];
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "%d %d %f %f %f %f", &q, &k, &r[0], &r[1], &r[2], &r[3]) == 6 && q >= 0 && q < 4 && k >= 0 && k < 16) {
                memcpy(sh.rect[q][k], r, sizeof r);
                if (k + 1 > sh.frames[q]) sh.frames[q] = k + 1;
            }
        fclose(f);
    }
    for (int q = 0; q < 4; ++q)
        if (!sh.frames[q]) {
            const float whole[4] = {0, 0, 1, 1};
            memcpy(sh.rect[q][0], whole, sizeof whole);
            sh.frames[q] = 1;
        }
}
constexpr float kLdrOverbright = 2.f, kLdrGlow = 0.4f;
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline float CurveAt(const float (*k)[2], int n, float t) {
    if (t <= k[0][0]) return k[0][1];
    for (int i = 1; i < n; ++i)
        if (t <= k[i][0]) {
            float w = k[i][0] > k[i - 1][0] ? (t - k[i - 1][0]) / (k[i][0] - k[i - 1][0]) : 1.f;
            return Lerp(k[i - 1][1], k[i][1], w);
        }
    return k[n - 1][1];
}
inline float VisibleLife(const MuzzleFlash::P& p) {
    if (!p.nA || p.aK[p.nA - 1][1] > 0.f) return p.life;
    int i = p.nA - 1;
    while (i > 0 && p.aK[i - 1][1] <= 0.f) --i;
    return p.aK[i][0] * p.life;
}
inline float Remap(float x, float i0, float i1, float o0, float o1) {
    float t = i1 != i0 ? Clamp01((x - i0) / (i1 - i0)) : 0.f;
    return o0 + (o1 - o0) * t;
}
}
bool MuzzleFlash::InitPipeline(ID3D11Device* dev) {
    FnD3DCompile compile = GetCompiler();
    if (!compile) return false;
    ID3DBlob *vsb = nullptr, *psb = nullptr, *msg = nullptr;
    bool ok = SUCCEEDED(compile(kFlashShader, sizeof kFlashShader - 1, "tenzin_fps_flash", nullptr, nullptr, "VS", "vs_5_0",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(compile(kFlashShader, sizeof kFlashShader - 1, "tenzin_fps_flash", nullptr, nullptr, "PS", "ps_5_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) &&
         SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_));
    if (ok) {
        ID3DBlob* pdb = nullptr;
        if (SUCCEEDED(compile(kFlashShader, sizeof kFlashShader - 1, "tenzin_fps_flash", nullptr, nullptr, "PSD", "ps_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pdb, &msg)))
            dev->CreatePixelShader(pdb->GetBufferPointer(), pdb->GetBufferSize(), nullptr, &psDist_);
        SafeRelease(msg);
        SafeRelease(pdb);
    }
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = ok && SUCCEEDED(dev->CreateInputLayout(il, 4, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_));
    SafeRelease(vsb);
    SafeRelease(psb);
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(FlashVertex) * 6 * (kMax + kMaxTracers + kMaxImpacts * kImpactQuads + kMaxFlame);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &vb_));
    D3D11_BUFFER_DESC cd = {};
    cd.ByteWidth = sizeof(FlashCb);
    cd.Usage = D3D11_USAGE_DYNAMIC;
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&cd, nullptr, &cb_));
    D3D11_BLEND_DESC bld = {};
    bld.RenderTarget[0].BlendEnable = TRUE;
    bld.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bld.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    ok = ok && SUCCEEDED(dev->CreateBlendState(&bld, &bs_));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(dev->CreateRasterizerState(&rd, &rs_));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    ok = ok && SUCCEEDED(dev->CreateDepthStencilState(&dd, &dss_));
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ok = ok && SUCCEEDED(dev->CreateDepthStencilState(&dd, &dssDepth_));
    D3D11_SAMPLER_DESC sm = {};
    sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(dev->CreateSamplerState(&sm, &samp_));
    return ok;
}
void MuzzleFlash::AddTracer(const float* from, const float* to) {
    if (!tex_[kFlashTexSpark]) return;
    if (nTr_ >= kMaxTracers) {
        memmove(&trs_[0], &trs_[1], sizeof(Tracer) * (kMaxTracers - 1));
        --nTr_;
    }
    Tracer& t = trs_[nTr_++];
    memcpy(t.a, from, sizeof t.a);
    memcpy(t.b, to, sizeof t.b);
    t.t0 = -1.0;
    t.pending = true;
    float k = Rand(0.f, 1.f);
    t.col[0] = Lerp(247.f, 255.f, k) / 255.f, t.col[1] = Lerp(188.f, 245.f, k) / 255.f, t.col[2] = Lerp(94.f, 219.f, k) / 255.f;
    t.alpha = Rand(160.f, 175.f) / 255.f;
    float r = Rand(0.f, 1.f);
    t.lenSec = 0.084f + (0.093f - 0.084f) * r * r;
}
void MuzzleFlash::AddImpact(const float* pos, const float* dir, bool kill) {
    if (nImp_ >= kMaxImpacts) {
        memmove(&imps_[0], &imps_[1], sizeof(Impact) * (kMaxImpacts - 1));
        --nImp_;
    }
    Impact& m = imps_[nImp_++];
    memcpy(m.p, pos, sizeof m.p);
    float l = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    for (int k = 0; k < 3; ++k) m.d[k] = l > 1e-4f ? dir[k] / l : (k == 2 ? 1.f : 0.f);
    m.t0 = -1.0;
    m.pending = true;
    m.kill = kill;
    rng_ = rng_ * 1664525u + 1013904223u;
    m.seed = rng_ ? rng_ : 1u;
}
bool MuzzleFlash::Load(ID3D11Device* dev, const wchar_t* fxDir, const wchar_t* effectDir, const wchar_t* gasDir, char* err, size_t errLen) {
    Release();
    if (!dev) return false;
    dev_ = dev;
    if (!InitPipeline(dev)) {
        snprintf(err, errLen, "flash pipeline init failed");
        Release();
        return false;
    }
    for (int i = 0; i < 5; ++i) {
        const wchar_t* d = i == kFlashTexSmoke || i == kFlashTexSpark ? effectDir : fxDir;
        if (!d || !d[0]) continue;
        wchar_t path[MAX_PATH * 2];
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls.dds", d, kFlashTexNames[i]);
        char e[256] = {};
        tex_[i] = LoadDdsBc(dev, path, e, sizeof e);
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls.sheet", d, kFlashTexNames[i]);
        LoadSheet(path, sheet_[i]);
    }
    for (int i = kFlashTexGasJet; gasDir && gasDir[0] && i < kFlashTexCount; ++i) {
        wchar_t path[MAX_PATH * 2];
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls.dds", gasDir, kFlashTexNames[i]);
        char e[256] = {};
        tex_[i] = LoadDdsBc(dev, path, e, sizeof e);
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls.sheet", gasDir, kFlashTexNames[i]);
        LoadSheet(path, sheet_[i]);
    }
    return true;
}
void MuzzleFlash::LoadFlame(const wchar_t* dir, const Settings& s) {
    for (auto& t : flTex_) SafeRelease(t);
    nFl_ = 0;
    if (!dev_ || !dir || !dir[0]) return;
    for (int li = 0; li < 4; ++li) {
        if (!s.flame[li].tex[0]) continue;
        wchar_t path[MAX_PATH * 2];
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls.dds", dir, s.flame[li].tex);
        char e[256] = {};
        flTex_[li] = LoadDdsBc(dev_, path, e, sizeof e);
    }
}
float MuzzleFlash::Rand(float a, float b) {
    rng_ = rng_ * 1664525u + 1013904223u;
    return a + (b - a) * static_cast<float>((rng_ >> 8) & 0xffffff) / 16777216.f;
}
void MuzzleFlash::Trigger(float ads) {
    if (!Loaded()) return;
    pending_ = true;
    ads_ = Clamp01(ads);
}
MuzzleFlash::P* MuzzleFlash::Add() {
    if (n_ >= kMax) return nullptr;
    P* p = &parts_[n_++];
    memset(p, 0, sizeof *p);
    p->locked = 1;
    p->rs0 = p->rs1 = 1;
    p->rt1 = 1;
    p->colorFadeAt = -1;
    p->bright = 1;
    return p;
}
void MuzzleFlash::Spawn(const Settings& s) {
    n_ = 0;
    if (s.flashFlame) SpawnFlame(s);
    if (s.flashGas) SpawnGas(s);
    if (s.shellEject && !s.shellEjectAnim) SpawnShellFx(s);
}
void MuzzleFlash::SpawnGas(const Settings& s) {
    if (!GasLoaded()) return;
    const float k = s.flashGasScale > 0.f ? s.flashGasScale : 1.f, ka = s.flashGasAlpha, kd = s.flashGasDistort;
    auto fit = [&](P* p) {
        for (int c = 0; c < 3; ++c) p->p[c] *= k, p->v[c] *= k, p->g[c] *= k;
        p->r0 *= k;
        p->alpha *= ka;
    };
    auto gray = [&](P* p, float a, float b) { p->col[0] = p->col[1] = p->col[2] = Rand(a, b); };
    const float ad = ads_ * s.flashGasAds;
    const float hot = Clamp01(static_cast<float>(burst_ - 1) / 10.f);
    if (kd > 0.f && tex_[kFlashTexDistRing]) {
        if (P* p = Add()) {
            p->tex = kFlashTexDistRing;
            p->p[0] = 2.5f;
            p->r0 = Lerp(8.f, 5.f, ad);
            p->rs0 = 0.35f, p->rs1 = 1.f, p->ease = 2.5f;
            p->life = 0.085f;
            p->alpha = 1.f, p->alphaPow = 1.3f;
            p->distNoise = 0.12f * kd * Lerp(1.f, 0.6f, ad);
            p->distPush = 0.3f * kd * Lerp(1.f, 0.6f, ad);
            fit(p);
            p->alpha = 1.f;
        }
    }
    const bool curve = s.flashGasStyle == 1;
    auto keys = [](float (*k)[2], unsigned char& n, std::initializer_list<float> v) {
        n = 0;
        for (auto it = v.begin(); it + 1 < v.end() && n < 4; it += 2, ++n) k[n][0] = it[0], k[n][1] = it[1];
    };
    const int ball = tex_[kFlashTexGasBall] ? kFlashTexGasBall : kFlashTexGasPuff;
    if (P* p = Add()) {
        p->tex = static_cast<unsigned char>(ball);
        p->locked = 0;
        p->p[0] = 1.5f;
        p->v[0] = 30.f;
        p->drag = 0.5f;
        p->r0 = Rand(3.6f, 4.2f) * Lerp(1.f, 0.6f, ad);
        p->rs0 = 0.65f, p->rs1 = 1.f, p->ease = 3.f;
        p->life = Rand(0.11f, 0.14f);
        p->alpha = 0.72f * Lerp(1.f, 0.45f, ad), p->alphaPow = 1.5f;
        gray(p, 0.95f, 1.05f);
        p->rot = Rand(0.f, 6.2831853f);
        p->rotSpeed = Rand(-1.5f, 1.5f);
        p->flip = Rand(0.f, 1.f) < 0.5f;
        if (ball == kFlashTexGasBall) p->frame0 = static_cast<int>(Rand(0.f, 15.99f)), p->fps = 40.f;
        else p->fitLife = true;
        p->fadeSize0 = 0.45f, p->fadeSize1 = 0.8f;
        if (curve) {
            p->v[0] = 45.f;
            p->dragRate = 12.f;
            p->life = Rand(0.5f, 0.6f);
            p->alphaPow = 0.f;
            keys(p->aK, p->nA, {0.f, 1.f, 0.05f, 0.85f, 0.15f, 0.35f, 0.25f, 0.f});
            keys(p->sK, p->nS, {0.f, 0.55f, 0.05f, 0.8f, 0.25f, 1.25f, 1.f, 1.8f});
            if (ball != kFlashTexGasBall) p->fitLife = false, p->frame0 = 4, p->fps = 60.f;
        }
        fit(p);
    }
    const float base = Rand(0.f, 6.2831853f);
    for (int i = 0; i < 5; ++i) {
        P* p = Add();
        if (!p) break;
        p->tex = kFlashTexGasPuff;
        p->locked = 0;
        float a = base + (static_cast<float>(i) + Rand(-0.25f, 0.25f)) * 1.2566371f, sp = Rand(110.f, 150.f);
        p->p[0] = 1.f, p->p[1] = 0.4f * std::cos(a), p->p[2] = 0.4f * std::sin(a);
        p->v[0] = Rand(40.f, 70.f), p->v[1] = sp * std::cos(a), p->v[2] = sp * std::sin(a);
        p->drag = 0.55f;
        p->r0 = Rand(2.f, 2.6f) * Lerp(1.f, 0.7f, ad);
        p->rs0 = 0.5f, p->rs1 = 1.3f, p->ease = 3.f;
        p->life = Rand(0.13f, 0.2f);
        p->alpha = 0.38f * Lerp(1.f, 0.5f, ad), p->alphaPow = 1.4f;
        gray(p, 0.9f, 1.f);
        p->rot = Rand(0.f, 6.2831853f);
        p->rotSpeed = Rand(-1.f, 1.f);
        p->flip = Rand(0.f, 1.f) < 0.5f;
        p->fitLife = true;
        p->frame0 = 4;
        p->fadeSize0 = 0.4f, p->fadeSize1 = 0.7f;
        if (curve) {
            p->dragRate = 14.f;
            p->life = Rand(0.4f, 0.5f);
            p->alphaPow = 0.f;
            keys(p->aK, p->nA, {0.f, 0.9f, 0.05f, 1.f, 0.14f, 0.45f, 0.25f, 0.f});
            keys(p->sK, p->nS, {0.f, 0.45f, 0.06f, 0.8f, 0.25f, 1.35f, 1.f, 1.9f});
            p->fitLife = false, p->fps = 60.f;
        }
        fit(p);
    }
    if (curve && s.flashGasSidePuff > 0.f) {
        const float side = Rand(90.f, 120.f), fwd = Rand(25.f, 40.f), up = Rand(8.f, 15.f), r0 = Rand(3.2f, 4.f);
        const float life = Rand(0.42f, 0.55f), rot = Rand(0.f, 6.2831853f), spin = Rand(-1.2f, 1.2f), c = Rand(0.82f, 0.92f);
        const bool flip = Rand(0.f, 1.f) < 0.5f;
        const int f0 = 4 + static_cast<int>(Rand(0.f, 1.99f));
        for (int i = 0; i < 2; ++i) {
            P* p = Add();
            if (!p) break;
            const float sg = i ? -1.f : 1.f;
            p->tex = kFlashTexGasPuff;
            p->locked = 0;
            p->p[0] = 0.8f, p->p[1] = 0.5f * sg;
            p->v[0] = fwd, p->v[1] = side * sg, p->v[2] = up;
            p->g[2] = 6.f;
            p->dragRate = 10.f;
            p->r0 = r0 * Lerp(1.f, 0.65f, ad);
            p->life = life;
            p->alpha = 0.34f * Lerp(1.f, 0.5f, ad) * s.flashGasSidePuff;
            keys(p->aK, p->nA, {0.f, 0.45f, 0.06f, 1.f, 0.16f, 0.45f, 0.3f, 0.f});
            keys(p->sK, p->nS, {0.f, 0.4f, 0.08f, 0.65f, 0.3f, 1.f, 1.f, 1.3f});
            p->col[0] = p->col[1] = p->col[2] = c;
            p->rot = rot * sg;
            p->rotSpeed = spin * sg;
            p->flip = i ? !flip : flip;
            p->frame0 = f0, p->fps = 50.f;
            p->fadeSize0 = 0.4f, p->fadeSize1 = 0.7f;
            fit(p);
        }
    }
    for (int i = 0; i < 2; ++i) {
        P* p = Add();
        if (!p) break;
        p->tex = kFlashTexGasJet;
        p->align = true, p->axis = static_cast<unsigned char>(1 + i);
        p->aspect = 1.8f, p->minFore = 0.35f, p->anchor = 1.f;
        p->p[0] = 0.6f;
        p->r0 = Rand(1.5f, 1.8f) * Lerp(1.f, 0.7f, ad);
        p->rs0 = 0.55f, p->rs1 = 1.2f, p->ease = 3.f;
        p->life = Rand(0.05f, 0.065f);
        p->alpha = 0.45f * Lerp(1.f, 0.5f, ad), p->alphaPow = 1.2f;
        gray(p, 0.92f, 1.f);
        p->frame0 = static_cast<int>(Rand(0.f, 15.99f));
        p->flip = Rand(0.f, 1.f) < 0.5f;
        fit(p);
    }
    for (int i = 0; i < 2; ++i) {
        P* p = Add();
        if (!p) break;
        p->tex = kFlashTexGasJet;
        p->align = true;
        p->aspect = 1.3f, p->minFore = Lerp(0.6f, 0.15f, ad), p->anchor = 1.f;
        p->p[0] = 0.3f + 0.8f * static_cast<float>(i);
        p->r0 = (i ? Rand(2.6f, 3.f) : Rand(3.8f, 4.4f)) * Lerp(1.f, 0.75f, ad);
        p->rs0 = 0.55f, p->rs1 = 1.2f, p->ease = 3.f;
        p->life = i ? Rand(0.045f, 0.055f) : Rand(0.06f, 0.08f);
        p->alpha = (i ? 0.3f : 0.42f) * Lerp(1.f, 0.45f, ad), p->alphaPow = 1.5f;
        gray(p, 0.92f, 1.f);
        p->frame0 = static_cast<int>(Rand(0.f, 15.99f));
        p->flip = Rand(0.f, 1.f) < 0.5f;
        fit(p);
    }
    if (s.flashGasLinger <= 0.f) {
    } else if (P* p = Add()) {
        p->tex = kFlashTexGasPuff;
        p->locked = 0;
        p->p[0] = Rand(1.1f, 3.3f), p->p[2] = 1.1f;
        p->v[0] = Rand(13.f, 22.f);
        p->g[2] = 20.f;
        p->drag = 0.1f;
        p->r0 = Rand(3.1f, 4.f);
        p->rs0 = 0.8f, p->rs1 = 2.2f;
        p->life = Rand(0.9f, 1.1f);
        p->alpha = 0.075f * Lerp(1.f, 0.7f, ad) * s.flashGasLinger;
        p->fadeIn = 0.1f;
        p->fadeOut = 0.5f;
        gray(p, 0.75f, 0.85f);
        p->rot = Rand(0.f, 6.2831853f);
        p->rotSpeed = Rand(-0.3f, 0.3f);
        p->flip = Rand(0.f, 1.f) < 0.5f;
        p->fitLife = true;
        p->seq = 1;
        p->fadeSize0 = 0.4f, p->fadeSize1 = 0.7f;
        fit(p);
    }
    if (kd > 0.f && tex_[kFlashTexDistHaze]) {
        if (P* p = Add()) {
            p->tex = kFlashTexDistHaze;
            p->locked = 0;
            p->p[0] = 1.5f, p->p[2] = 1.f;
            p->v[0] = 6.f;
            p->g[2] = 25.f;
            p->drag = 0.15f;
            p->r0 = Rand(3.5f, 4.5f);
            p->rs0 = 0.7f, p->rs1 = 1.5f;
            p->life = Rand(0.5f, 0.7f);
            p->alpha = 1.f;
            p->fadeIn = 0.06f;
            p->fadeOut = 0.35f;
            p->distNoise = 0.035f * kd * Lerp(0.4f, 1.f, hot) * Lerp(1.f, 0.6f, ad);
            fit(p);
            p->alpha = 1.f;
        }
    }
}
void MuzzleFlash::SpawnShellFx(const Settings& s) {
    const float ad = ads_;
    auto keys = [](float (*k)[2], unsigned char& n, std::initializer_list<float> v) {
        n = 0;
        for (auto it = v.begin(); it + 1 < v.end() && n < 4; it += 2, ++n) k[n][0] = it[0], k[n][1] = it[1];
    };
    if (s.shellSmoke > 0.f && GasLoaded()) {
        for (int i = 0; i < 2; ++i) {
            P* p = Add();
            if (!p) break;
            p->tex = kFlashTexGasPuff;
            p->locked = 0;
            p->frame = 1;
            p->p[0] = 0.3f + 0.5f * static_cast<float>(i), p->p[2] = 0.15f;
            p->v[0] = Rand(22.f, 34.f) * (i ? 0.65f : 1.f), p->v[1] = Rand(-4.f, 6.f), p->v[2] = Rand(4.f, 10.f);
            p->dragRate = 9.f;
            p->r0 = Rand(0.9f, 1.2f) * (i ? 1.35f : 1.f) * Lerp(1.f, 0.75f, ad);
            p->life = Rand(0.16f, 0.24f);
            p->alpha = (i ? 0.16f : 0.26f) * Lerp(1.f, 0.6f, ad) * s.shellSmoke * s.flashGasAlpha;
            keys(p->aK, p->nA, {0.f, 0.6f, 0.1f, 1.f, 0.5f, 0.25f, 1.f, 0.f});
            keys(p->sK, p->nS, {0.f, 0.4f, 0.15f, 0.75f, 1.f, 1.4f});
            p->col[0] = p->col[1] = p->col[2] = Rand(0.82f, 0.9f);
            p->rot = Rand(0.f, 6.2831853f);
            p->rotSpeed = Rand(-1.5f, 1.5f);
            p->flip = Rand(0.f, 1.f) < 0.5f;
            p->frame0 = 4, p->fps = 60.f;
            p->fadeSize0 = 0.4f, p->fadeSize1 = 0.7f;
        }
    }
    if (s.shellSparks > 0.f && tex_[kFlashTexGlow]) {
        const int n = Rand(0.f, 1.f) < 0.5f ? 2 : 3;
        for (int i = 0; i < n; ++i) {
            P* p = Add();
            if (!p) break;
            p->tex = kFlashTexGlow;
            p->locked = 0;
            p->frame = 1;
            p->p[0] = 0.2f;
            p->v[0] = Rand(20.f, 40.f), p->v[1] = Rand(-10.f, 15.f), p->v[2] = Rand(25.f, 60.f);
            p->g[2] = -250.f;
            p->dragRate = 4.f;
            p->r0 = Rand(0.10f, 0.17f) * Lerp(1.f, 0.7f, ad);
            p->life = Rand(0.035f, 0.06f);
            p->alpha = Rand(0.7f, 0.9f) * Lerp(1.f, 0.5f, ad) * s.shellSparks;
            p->alphaPow = 1.2f;
            p->col[0] = 1.f, p->col[1] = Rand(0.62f, 0.75f), p->col[2] = Rand(0.25f, 0.35f);
            p->bright = kLdrOverbright;
            p->rot = Rand(0.f, 6.2831853f);
        }
    }
}
void MuzzleFlash::SpawnBarrelSmoke(const Settings& s, double now) {
    if (s.flashGasBarrelSmoke <= 0.f || !tex_[kFlashTexGasWisp] || tSpawn_ < 0.0) return;
    const float k = s.flashGasScale > 0.f ? s.flashGasScale : 1.f;
    const float str = Clamp01(static_cast<float>(burst_ - 2) / 10.f) * s.flashGasBarrelSmoke * s.flashGasAlpha * Lerp(1.f, 0.6f, ads_ * s.flashGasAds);
    const float age0 = static_cast<float>(now - tSpawn_);
    for (int i = 0; i < 2; ++i) {
        P* p = Add();
        if (!p) break;
        p->tex = kFlashTexGasWisp;
        p->delay = age0 + 0.15f * static_cast<float>(i);
        p->p[0] = (0.3f + 0.5f * static_cast<float>(i)) * k, p->p[1] = Rand(-0.2f, 0.2f) * k, p->p[2] = 0.2f * k;
        p->r0 = Rand(5.f, 6.f) * k;
        p->aspect = 0.5f, p->lift = 0.9f;
        p->rs0 = 0.8f, p->rs1 = 1.25f;
        p->life = Rand(1.6f, 2.1f) * s.flashGasBarrelSmokeLife;
        p->alpha = (i ? 0.4f : 0.55f) * str;
        p->fadeIn = 0.2f < p->life * 0.3f ? 0.2f : p->life * 0.3f;
        p->fadeOut = s.flashGasBarrelSmokeFade < p->life - p->fadeIn ? s.flashGasBarrelSmokeFade : p->life - p->fadeIn;
        p->col[0] = p->col[1] = p->col[2] = Rand(0.9f, 1.f);
        p->rot = Rand(-0.08f, 0.08f);
        p->flip = Rand(0.f, 1.f) < 0.5f;
        p->fitLife = true;
        p->fadeSize0 = 0.5f, p->fadeSize1 = 0.9f;
    }
}
void MuzzleFlash::SpawnFlame(const Settings& s) {
    auto add = [&]() { return Add(); };
    if (P* p = add()) {
        p->tex = kFlashTexVent;
        float r = Rand(0.f, 0.1f), a = Rand(0.f, 6.2831853f);
        p->p[1] = r * std::cos(a);
        p->p[2] = r * std::sin(a);
        p->rot = Rand(-15.f, 15.f) * kDeg;
        p->life = 0.025f;
        p->r0 = Rand(5.f, 7.f);
        p->alpha = Rand(190.f, 230.f) / 255.f;
        p->col[0] = 1.f, p->col[1] = 178.f / 255.f, p->col[2] = 136.f / 255.f;
        p->fadeOut = 0.025f;
    }
    for (int i = 0; i < 8; ++i) {
        P* p = add();
        if (!p) break;
        p->tex = kFlashTexFire;
        float d = 17.5f * static_cast<float>(i) / 8.f + Rand(1.f, 1.5f);
        p->p[0] = d;
        p->r0 = Remap(d, 0.f, 19.f, 12.f, 4.f);
        p->alpha = Rand(32.f, 36.f) / 255.f * Remap(p->r0, 4.f, 12.f, 0.125f, 1.f);
        float t = Rand(0.f, 1.f);
        p->col[0] = Lerp(126.f, 120.f, t) / 255.f, p->col[1] = Lerp(87.f, 92.f, t) / 255.f, p->col[2] = Lerp(20.f, 75.f, t) / 255.f;
        p->rot = Rand(0.f, 6.2831853f);
        p->flip = Rand(0.f, 1.f) < 0.5f;
        p->life = 0.015f;
        p->fadeOut = Rand(0.0025f, 0.01f);
        p->rs0 = 1.f, p->rs1 = 1.125f, p->rt0 = 0.3f, p->rt1 = 1.f;
        p->bright = kLdrOverbright;
    }
    if (P* p = add()) {
        p->tex = kFlashTexGlow;
        p->r0 = 25.f;
        p->alpha = Rand(160.f, 190.f) / 255.f * kLdrGlow;
        float t = Rand(0.f, 1.f);
        p->col[0] = Lerp(255.f, 130.f, t) / 255.f, p->col[1] = Lerp(188.f, 92.f, t) / 255.f, p->col[2] = Lerp(189.f, 75.f, t) / 255.f;
        p->rot = Rand(0.f, 6.2831853f);
        p->life = 0.015f;
        p->fadeOut = Rand(0.015f, 0.025f);
        p->colorFadeAt = 0.7f;
    }
    for (int i = 0; i < 8; ++i) {
        P* p = add();
        if (!p) break;
        p->tex = kFlashTexFire;
        float ct = static_cast<float>(i) / 1200.f;
        float r = Rand(0.f, 0.5f), a = Rand(0.f, 6.2831853f), e = Rand(-1.f, 1.f);
        p->p[0] = 1.f + r * 4.f * e + Remap(ct, 0.f, 0.0075f, 0.f, 29.f);
        p->p[1] = r * std::cos(a) * std::sqrt(1.f - e * e);
        p->p[2] = r * std::sin(a) * std::sqrt(1.f - e * e);
        p->delay = ct;
        p->r0 = Remap(ct, 0.f, 0.0075f, 8.f, 1.f);
        p->alpha = Rand(100.f, 120.f) / 255.f * Remap(ct, 0.f, 0.01f, 0.5f, 0.f);
        p->col[0] = 254.f / 255.f, p->col[1] = 233.f / 255.f, p->col[2] = 216.f / 255.f;
        p->rot = Rand(0.f, 6.2831853f);
        p->life = Remap(ct, 0.f, 0.0075f, 0.025f, 0.0175f);
        p->bright = kLdrOverbright;
    }
    if (!s.flashSmoke || !tex_[kFlashTexSmoke]) return;
    if (P* p = add()) {
        p->tex = kFlashTexSmoke;
        p->locked = 0;
        float r = Rand(0.f, 3.f), a = Rand(0.f, 6.2831853f), e = Rand(-1.f, 1.f);
        p->p[0] = Rand(1.f, 6.f) + r * e;
        p->p[1] = r * std::cos(a) * std::sqrt(1.f - e * e);
        p->p[2] = Rand(2.f, 4.f) + r * 0.25f * std::sin(a) * std::sqrt(1.f - e * e);
        p->v[0] = Rand(100.f, 120.f);
        p->g[2] = 40.f;
        p->drag = 0.05f;
        p->r0 = Rand(4.f, 7.f);
        p->rs0 = 1.f, p->rs1 = 3.f;
        p->life = 0.2f;
        p->alpha = 60.f / 255.f;
        p->fadeOut = 0.18f;
        float t = Rand(0.f, 1.f);
        p->col[0] = Lerp(169.f, 99.f, t) / 255.f, p->col[1] = Lerp(168.f, 97.f, t) / 255.f, p->col[2] = Lerp(166.f, 96.f, t) / 255.f;
        p->rot = Rand(0.f, 6.2831853f);
        p->flip = Rand(0.f, 1.f) < 0.5f;
        p->seq = 0;
        p->fadeSize0 = 0.4f, p->fadeSize1 = 0.7f;
    }
    if (P* p = add()) {
        p->tex = kFlashTexSmoke;
        p->locked = 0;
        p->p[0] = -20.f, p->p[1] = 2.f, p->p[2] = 2.f;
        p->v[1] = 3.f;
        p->g[2] = -4.f;
        p->drag = 0.05f;
        p->r0 = 5.f;
        p->rs0 = 1.f, p->rs1 = 1.5f;
        p->life = 1.5f;
        p->alpha = 40.f / 255.f;
        p->fadeIn = 0.1f;
        p->fadeOut = 0.8f;
        float t = Rand(0.2f, 1.f);
        p->col[0] = p->col[1] = p->col[2] = Lerp(193.f, 156.f, t) / 255.f;
        p->rot = 3.1415927f;
        p->rotSpeed = 0.1f;
        p->flip = true;
        p->seq = 1;
        p->fitLife = true;
        p->fadeSize0 = 0.4f, p->fadeSize1 = 0.55f;
    }
    if (P* p = add()) {
        p->tex = kFlashTexSmoke;
        p->locked = 0;
        p->p[0] = -25.f;
        p->v[1] = 5.f;
        p->g[2] = -10.f;
        p->drag = 0.05f;
        p->r0 = Rand(10.f, 12.f);
        p->rs0 = 0.5f, p->rs1 = 1.2f, p->rt0 = 0.f, p->rt1 = 2.f;
        p->life = 1.f;
        p->alpha = 20.f / 255.f;
        p->fadeOut = 1.2f;
        float t = Rand(0.2f, 1.f);
        p->col[0] = p->col[1] = p->col[2] = Lerp(221.f, 187.f, t) / 255.f;
        p->rot = Rand(0.f, 6.2831853f);
        p->flip = Rand(0.f, 1.f) < 0.5f;
        p->seq = 2;
        p->fitLife = true;
        p->fadeSize0 = 0.4f, p->fadeSize1 = 0.55f;
    }
}
void MuzzleFlash::BeginFrame(double now, const Settings& s) {
    for (int i = 0; i < nTr_; ++i)
        if (trs_[i].pending) {
            trs_[i].pending = false;
            trs_[i].t0 = now;
        }
    for (int i = 0; i < nImp_; ++i)
        if (imps_[i].pending) {
            imps_[i].pending = false;
            imps_[i].t0 = now;
        }
    if (!pending_) {
        if (burst_ >= 3 && tLastShot_ >= 0.0 && now - tLastShot_ > 0.12 && tSpawn_ >= 0.0) {
            if (s.flashGas && GasLoaded()) SpawnBarrelSmoke(s, now);
            burst_ = 0;
        }
        return;
    }
    pending_ = false;
    burst_ = tLastShot_ >= 0.0 && now - tLastShot_ < 0.3 ? burst_ + 1 : 1;
    tLastShot_ = now;
    if (!s.flashFlame && !(s.flashGas && GasLoaded())) {
        n_ = 0;
        tSpawn_ = -1;
        return;
    }
    P keep[kMax];
    int nk = 0;
    for (int i = 0; i < n_; ++i)
        if (!parts_[i].locked && parts_[i].placed && now - tSpawn_ - parts_[i].delay <= VisibleLife(parts_[i])) {
            keep[nk] = parts_[i];
            keep[nk].delay -= static_cast<float>(now - tSpawn_);
            ++nk;
        }
    Spawn(s);
    for (int i = 0; i < nk && n_ < kMax; ++i) parts_[n_++] = keep[i];
    tSpawn_ = now;
    tSim_ = now;
}
float MuzzleFlash::LightLevel(double now) const {
    if (pending_) return 1.f;
    if (tSpawn_ < 0) return 0.f;
    float age = static_cast<float>(now - tSpawn_);
    return age < 0.f ? 1.f : Clamp01(1.f - age / 0.05f);
}
bool MuzzleFlash::Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* depth, UINT w, UINT h,
                       bool rtvEncodesSrgb, double now, const MuzzleFrame* fp, const WorldCam* cam, const Settings& s) {
    if (!Loaded() || !ctx || !rtv || w < 16 || h < 16) return false;
    static FlashVertex verts[(kMax + kMaxTracers + kMaxImpacts * kImpactQuads + kMaxFlame) * 6];
    int nv = 0;
    int trStart = nv, trCount = 0;
    if (cam && tex_[kFlashTexSpark]) {
        float ty = std::tan(cam->fovY * 0.5f), aspect = static_cast<float>(w) / static_cast<float>(h);
        auto toCam = [&](const float* p, float* o) {
            float d[3] = {p[0] - cam->eye[0], p[1] - cam->eye[1], p[2] - cam->eye[2]};
            o[0] = d[0] * cam->right[0] + d[1] * cam->right[1] + d[2] * cam->right[2];
            o[1] = d[0] * cam->up[0] + d[1] * cam->up[1] + d[2] * cam->up[2];
            o[2] = d[0] * cam->fwd[0] + d[1] * cam->fwd[1] + d[2] * cam->fwd[2];
        };
        int keep = 0;
        for (int i = 0; i < nTr_; ++i) {
            Tracer& t = trs_[i];
            float dir[3] = {t.b[0] - t.a[0], t.b[1] - t.a[1], t.b[2] - t.a[2]};
            float total = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            float life = total / kTracerSpeed;
            float age = t.pending || t.t0 < 0 ? 0.f : static_cast<float>(now - t.t0);
            if (total < 0.01f || age > life) continue;
            trs_[keep++] = t;
            if (t.pending) continue;
            for (float& c : dir) c /= total;
            float lf = age / life;
            float a = t.alpha * s.flashIntensity;
            if (lf < 0.2f) a = 0.f;
            else if (lf < 0.3f) a *= (lf - 0.2f) / 0.1f;
            else if (lf > 0.95f) a *= Clamp01((1.f - lf) / 0.05f);
            if (a <= 0.001f) continue;
            float head = std::min(age * kTracerSpeed, total);
            float len = std::min(kTracerSpeed * t.lenSec, kTracerMaxLen) * Clamp01(age / 0.101f);
            len = std::min(len, head);
            float hw[3], tw[3], hv[3], tv[3];
            for (int k = 0; k < 3; ++k) {
                hw[k] = t.a[k] + dir[k] * head;
                tw[k] = t.a[k] + dir[k] * (head - len);
            }
            toCam(hw, hv);
            toCam(tw, tv);
            const float zn = 0.05f;
            if (hv[2] < zn && tv[2] < zn) continue;
            if (hv[2] < zn || tv[2] < zn) {
                float* in = hv[2] < zn ? hv : tv;
                const float* outp = hv[2] < zn ? tv : hv;
                float k = (zn - in[2]) / (outp[2] - in[2]);
                for (int c = 0; c < 3; ++c) in[c] += (outp[c] - in[c]) * k;
            }
            float hx = hv[0] / (hv[2] * ty * aspect), hy = hv[1] / (hv[2] * ty);
            float tx = tv[0] / (tv[2] * ty * aspect), tyy = tv[1] / (tv[2] * ty);
            float sx = (hx - tx) * w * 0.5f, sy = (hy - tyy) * h * 0.5f;
            float sl = std::sqrt(sx * sx + sy * sy);
            float px = sl > 1e-4f ? -sy / sl : 0.f, py = sl > 1e-4f ? sx / sl : 1.f;
            float wh = kTracerHalfWidth * h * 0.5f / (hv[2] * ty), wt = kTracerHalfWidth * h * 0.5f / (tv[2] * ty);
            wh = std::max(wh, 0.75f), wt = std::max(wt, 0.75f);
            auto vtx = [&](float x, float y, float off, float u, float v) {
                return FlashVertex{x + px * off * 2.f / w, y + py * off * 2.f / h, u, v, t.col[0] * s.tracerTint[0], t.col[1] * s.tracerTint[1], t.col[2] * s.tracerTint[2], a, 1.f, 1.f, 0.f};
            };
            FlashVertex q4[4] = {vtx(hx, hy, -wh, 0.f, 0.f), vtx(hx, hy, wh, 1.f, 0.f), vtx(tx, tyy, -wt, 0.f, 1.f),
                                 vtx(tx, tyy, wt, 1.f, 1.f)};
            const int idx[6] = {0, 1, 2, 2, 1, 3};
            for (int k = 0; k < 6; ++k) verts[nv++] = q4[idx[k]];
        }
        nTr_ = keep;
        trCount = nv - trStart;
    }
    int imStart = nv, imCount = 0;
    if (cam && s.hitFx && tex_[kFlashTexSmoke] && nImp_) {
        float ty = std::tan(cam->fovY * 0.5f), aspect = static_cast<float>(w) / static_cast<float>(h);
        const float px1 = 2.f / static_cast<float>(h);
        const Sheet& sh = sheet_[kFlashTexSmoke];
        auto quad = [&](const float* c, float rad, float minPx, float a, float rot, int fr) {
            float d[3] = {c[0] - cam->eye[0], c[1] - cam->eye[1], c[2] - cam->eye[2]};
            float z = d[0] * cam->fwd[0] + d[1] * cam->fwd[1] + d[2] * cam->fwd[2];
            if (z < 0.2f || a <= 0.002f) return;
            float x = (d[0] * cam->right[0] + d[1] * cam->right[1] + d[2] * cam->right[2]) / (z * ty * aspect);
            float y = (d[0] * cam->up[0] + d[1] * cam->up[1] + d[2] * cam->up[2]) / (z * ty);
            float hy = std::max(rad / (z * ty), minPx * px1 * static_cast<float>(h) / 1080.f), hx = hy / aspect;
            float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
            if (sh.frames[0] > 0) {
                const float* uv = sh.rect[0][fr % sh.frames[0]];
                u0 = uv[0], v0 = uv[1], u1 = uv[2], v1 = uv[3];
            }
            float cr = std::cos(rot), sr = std::sin(rot);
            auto vtx = [&](float cu, float cv, float u, float v) {
                return FlashVertex{x + (cu * cr - cv * sr) * hx, y + (cu * sr + cv * cr) * hy, u, v, s.hitFxColor[0], s.hitFxColor[1],
                                   s.hitFxColor[2], a, 0.f, 1.f, 0.9999f};
            };
            FlashVertex q4[4] = {vtx(-1.f, 1.f, u0, v0), vtx(1.f, 1.f, u1, v0), vtx(-1.f, -1.f, u0, v1), vtx(1.f, -1.f, u1, v1)};
            const int idx[6] = {0, 1, 2, 2, 1, 3};
            for (int k = 0; k < 6; ++k) verts[nv++] = q4[idx[k]];
        };
        int keep = 0;
        for (int i = 0; i < nImp_; ++i) {
            Impact m = imps_[i];
            const float life = m.kill ? 0.55f : 0.4f, sc = s.hitFxScale * (m.kill ? 1.3f : 1.f);
            float age = m.pending || m.t0 < 0 ? 0.f : static_cast<float>(now - m.t0);
            if (age > life) continue;
            imps_[keep++] = m;
            float t = age / life, a0 = s.hitFxAlpha;
            unsigned r = m.seed;
            auto rnd = [&r](float lo, float hi) {
                r = r * 1664525u + 1013904223u;
                return lo + (hi - lo) * static_cast<float>((r >> 8) & 0xffff) / 65535.f;
            };
            float sd[3] = {m.d[2], 0.f, -m.d[0]}, sl = std::sqrt(sd[0] * sd[0] + sd[2] * sd[2]);
            if (sl < 1e-3f) sd[0] = 1.f, sd[2] = 0.f, sl = 1.f;
            sd[0] /= sl, sd[2] /= sl;
            float ease = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
            float c0[3] = {m.p[0] - m.d[0] * 0.2f, m.p[1] - m.d[1] * 0.2f, m.p[2] - m.d[2] * 0.2f};
            float am = a0 * (t < 0.15f ? 1.f : std::pow(1.f - (t - 0.15f) / 0.85f, 1.5f));
            quad(c0, (0.1f + 0.35f * ease) * sc, 7.f * (m.kill ? 1.3f : 1.f), am, rnd(0.f, 6.283f), static_cast<int>(rnd(0.f, 64.f)));
            for (int k = 0; k < 2; ++k) {
                float lat = rnd(-0.15f, 0.15f), fw = 0.15f + rnd(0.6f, 1.0f) * age;
                float c[3] = {m.p[0] + m.d[0] * fw + sd[0] * lat, m.p[1] + m.d[1] * fw + rnd(0.f, 0.1f), m.p[2] + m.d[2] * fw + sd[2] * lat};
                quad(c, (0.08f + 0.22f * ease) * sc, 4.f, a0 * 0.7f * (1.f - t) * (1.f - t), rnd(0.f, 6.283f), static_cast<int>(rnd(0.f, 64.f)));
            }
            for (int k = 0; k < kImpactQuads - 3; ++k) {
                float sp = rnd(1.5f, 4.f), lat = rnd(-1.6f, 1.6f), up = rnd(0.3f, 2.2f);
                float v[3] = {m.d[0] * sp + sd[0] * lat, m.d[1] * sp + up, m.d[2] * sp + sd[2] * lat};
                float c[3] = {m.p[0] + v[0] * age, m.p[1] + v[1] * age - 4.9f * age * age, m.p[2] + v[2] * age};
                quad(c, rnd(0.03f, 0.06f) * sc, 1.5f, a0 * std::pow(1.f - t, 1.2f), rnd(0.f, 6.283f), static_cast<int>(rnd(0.f, 64.f)));
            }
        }
        nImp_ = keep;
        imCount = nv - imStart;
    }
    int flStart[4] = {}, flCount[4] = {};
    if (cam && s.flameFx[0]) {
        float dtf = tFlSim_ < 0.0 ? 0.f : static_cast<float>(now - tFlSim_);
        if (dtf < 0.f || dtf > 0.1f) dtf = 0.f;
        tFlSim_ = now;
        const float ty = std::tan(cam->fovY * 0.5f), aspect = static_cast<float>(w) / static_cast<float>(h);
        int keep = 0;
        for (int i = 0; i < nFl_; ++i) {
            FlameP q = fl_[i];
            const Settings::FlameLayer& L = s.flame[q.layer];
            q.age += dtf;
            if (q.age >= q.life) continue;
            float k = q.drag > 0.f ? std::exp(-q.drag * dtf) : 1.f;
            for (int c = 0; c < 3; ++c) q.v[c] *= k;
            q.v[1] += L.gravity * dtf;
            for (int c = 0; c < 3; ++c) q.p[c] += q.v[c] * dtf;
            q.rot += q.spin * dtf;
            fl_[keep++] = q;
        }
        nFl_ = keep;
        const bool on = fp && tLastShot_ >= 0.0 && now - tLastShot_ < s.flameGap && dtf > 0.f;
        if (on) {
            const MuzzleFrame& f = *fp;
            const float d = std::max(f.pos[2], 0.05f);
            const float ox = f.pos[0] * f.xs / d * ty * aspect * d, oy = f.pos[1] * f.ys / d * ty * d;
            const float ad = Clamp01(ads_);
            auto mix = [ad](float hip, float foc) { return foc >= 0.f ? Lerp(hip, foc, ad) : hip; };
            const float aimD = s.flameAimAds > 0.f ? Lerp(s.flameAim, s.flameAimAds, ad) : s.flameAim;
            const float fan = Lerp(s.flameFan, s.flameFanAds, ad) * 0.01745329f;
            float start[3], dir[3];
            for (int c = 0; c < 3; ++c) {
                start[c] = cam->eye[c] + cam->right[c] * ox + cam->up[c] * oy + cam->fwd[c] * d;
                dir[c] = cam->eye[c] + cam->fwd[c] * aimD - start[c];
            }
            float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            for (float& c : dir) c /= dl > 1e-6f ? dl : 1.f;
            float a1[3] = {dir[1] * cam->right[2] - dir[2] * cam->right[1], dir[2] * cam->right[0] - dir[0] * cam->right[2],
                           dir[0] * cam->right[1] - dir[1] * cam->right[0]};
            float al = std::sqrt(a1[0] * a1[0] + a1[1] * a1[1] + a1[2] * a1[2]);
            for (float& c : a1) c /= al > 1e-6f ? al : 1.f;
            float a2[3] = {dir[1] * a1[2] - dir[2] * a1[1], dir[2] * a1[0] - dir[0] * a1[2], dir[0] * a1[1] - dir[1] * a1[0]};
            for (int li = 0; li < 4; ++li) {
                const Settings::FlameLayer& L = s.flame[li];
                const float rate = mix(L.rate, L.rateAds);
                if (!flTex_[li] || rate <= 0.f) {
                    flAcc_[li] = 0.f;
                    continue;
                }
                flAcc_[li] += rate * dtf;
                const float spread = mix(L.spread, L.spreadAds) * 0.01745329f;
                while (flAcc_[li] >= 1.f && nFl_ < kMaxFlame) {
                    flAcc_[li] -= 1.f;
                    FlameP& q = fl_[nFl_++];
                    float u = 0.f;
                    if (fan > 0.f) {
                        flPh_[li] += 0.6180339887f;
                        if (flPh_[li] >= 1.f) flPh_[li] -= 1.f;
                        u = flPh_[li] + Rand(-0.04f, 0.04f);
                        u = u < 0.f ? u + 1.f : u >= 1.f ? u - 1.f : u;
                    }
                    const float yw = fan > 0.f ? -fan + 2.f * fan * u : 0.f, cy = std::cos(yw), sy = std::sin(yw);
                    float th = Rand(0.f, 6.2831853f), ph = spread * std::sqrt(Rand(0.f, 1.f));
                    float sp = Rand(mix(L.speed[0], L.speedAds[0]), mix(L.speed[1], L.speedAds[1])), sub = Rand(0.f, dtf);
                    for (int c = 0; c < 3; ++c) {
                        const float bd = dir[c] * cy + a2[c] * sy, b2 = a2[c] * cy - dir[c] * sy;
                        float vd = bd * std::cos(ph) + (a1[c] * std::cos(th) + b2 * std::sin(th)) * std::sin(ph);
                        q.v[c] = vd * sp;
                        q.p[c] = start[c] + Rand(-L.jitter, L.jitter) + q.v[c] * sub;
                    }
                    q.age = sub;
                    q.life = Rand(mix(L.life[0], L.lifeAds[0]), mix(L.life[1], L.lifeAds[1]));
                    q.drag = mix(L.drag, L.dragAds);
                    q.s0 = mix(L.size[0], L.sizeAds[0]);
                    q.s1 = mix(L.size[1], L.sizeAds[1]);
                    q.ad = ad;
                    q.rot = Rand(0.f, 6.2831853f);
                    q.spin = Rand(-L.spin, L.spin);
                    q.layer = static_cast<unsigned char>(li);
                    q.f0 = static_cast<unsigned short>(L.randFrame ? Rand(0.f, static_cast<float>(L.cx * L.cy) - 0.001f) : 0.f);
                }
            }
        } else {
            for (float& a : flAcc_) a = 0.f;
        }
        for (int li = 0; li < 4; ++li) {
            flStart[li] = nv;
            const Settings::FlameLayer& L = s.flame[li];
            if (!flTex_[li]) continue;
            const int nfr = L.cx * L.cy;
            for (int i = 0; i < nFl_; ++i) {
                const FlameP& q = fl_[i];
                if (q.layer != li) continue;
                float dd[3] = {q.p[0] - cam->eye[0], q.p[1] - cam->eye[1], q.p[2] - cam->eye[2]};
                float z = dd[0] * cam->fwd[0] + dd[1] * cam->fwd[1] + dd[2] * cam->fwd[2];
                if (z < 0.15f) continue;
                float t = Clamp01(q.age / q.life);
                auto mq = [&q](float hip, float foc) { return foc >= 0.f ? Lerp(hip, foc, q.ad) : hip; };
                const float fa = mq(L.fade[0], L.fadeAds[0]), fb = mq(L.fade[1], L.fadeAds[1]), bright = mq(L.bright, L.brightAds);
                float a = mq(L.alpha, L.alphaAds) * s.flashIntensity;
                if (fa > 0.f && t < fa) a *= t / fa;
                if (t > fb) a *= Clamp01((1.f - t) / std::max(1.f - fb, 1e-3f));
                if (a <= 0.002f) continue;
                float cx = (dd[0] * cam->right[0] + dd[1] * cam->right[1] + dd[2] * cam->right[2]) / (z * ty * aspect);
                float cy = (dd[0] * cam->up[0] + dd[1] * cam->up[1] + dd[2] * cam->up[2]) / (z * ty);
                float ge = L.sizeEase > 0.f ? 1.f - std::pow(1.f - t, L.sizeEase) : t;
                float r = Lerp(q.s0, q.s1, ge);
                float hy = r / (z * ty);
                float ang = q.rot, hl = hy;
                const float stretch = mq(L.stretch, L.stretchAds);
                if (stretch > 0.f) {
                    float e[3] = {dd[0] + q.v[0] * stretch, dd[1] + q.v[1] * stretch, dd[2] + q.v[2] * stretch};
                    float ez = std::max(e[0] * cam->fwd[0] + e[1] * cam->fwd[1] + e[2] * cam->fwd[2], 0.15f);
                    float ex = (e[0] * cam->right[0] + e[1] * cam->right[1] + e[2] * cam->right[2]) / (ez * ty * aspect);
                    float ey = (e[0] * cam->up[0] + e[1] * cam->up[1] + e[2] * cam->up[2]) / (ez * ty);
                    float sx = (ex - cx) * aspect, sy = ey - cy, sl = std::sqrt(sx * sx + sy * sy);
                    if (sl > 1e-5f) ang = std::atan2(sy, sx), hl = hy + sl * 0.5f;
                }
                int fr = L.fps > 0.f ? (q.f0 + static_cast<int>(q.age * L.fps)) % nfr : std::min(nfr - 1, static_cast<int>(t * nfr));
                float u0 = static_cast<float>(fr % L.cx) / L.cx, v0 = static_cast<float>(fr / L.cx) / L.cy;
                float u1 = u0 + 1.f / L.cx, v1 = v0 + 1.f / L.cy;
                float col[3];
                for (int c = 0; c < 3; ++c) col[c] = Lerp(L.col0[c], L.col1[c], t);
                float ca = std::cos(ang), sa = std::sin(ang);
                const float cs[4][2] = {{-1, 1}, {1, 1}, {-1, -1}, {1, -1}};
                const float tc[4][2] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
                FlashVertex q4[4];
                for (int k = 0; k < 4; ++k) {
                    float ix = cs[k][0] * hl * ca - cs[k][1] * hy * sa, iy = cs[k][0] * hl * sa + cs[k][1] * hy * ca;
                    q4[k] = {cx + ix / aspect, cy + iy, tc[k][0], tc[k][1], col[0], col[1], col[2], a, L.additive ? 1.f : 0.f, bright, 0.9999f};
                }
                const int idx[6] = {0, 1, 2, 2, 1, 3};
                for (int k = 0; k < 6; ++k) verts[nv++] = q4[idx[k]];
            }
            flCount[li] = nv - flStart[li];
        }
    }
    int counts[kFlashTexCount] = {}, starts[kFlashTexCount] = {};
    const int order[10] = {kFlashTexDistHaze, kFlashTexDistRing, kFlashTexSmoke, kFlashTexGasWisp, kFlashTexGasPuff,
                           kFlashTexGasBall, kFlashTexGasJet, kFlashTexFire, kFlashTexVent, kFlashTexGlow};
    const bool canDist = scene_ && psDist_;
    int live = 0;
    if (fp && n_ && tSpawn_ >= 0) {
    const MuzzleFrame& f = *fp;
    float dt = static_cast<float>(now - tSim_);
    if (dt < 0.f || dt > 0.1f) dt = 0.f;
    tSim_ = now;
    float unit = f.unit * (s.flashScale > 0.f ? s.flashScale : 1.f);
    auto toView = [&](const float* l, float* out, bool point, int frame) {
        if (frame == 1) {
            for (int k = 0; k < 3; ++k)
                out[k] = (point ? f.bpos[k] : 0.f) + (f.bout[k] * l[0] + f.bfwd[k] * l[1] + f.bup[k] * l[2]) * unit;
            return;
        }
        for (int k = 0; k < 3; ++k)
            out[k] = (point ? f.pos[k] : 0.f) + (f.fwd[k] * l[0] + f.left[k] * l[1] + f.up[k] * l[2]) * unit;
    };
    for (int oi = 0; oi < 10; ++oi) {
        int tx = order[oi];
        starts[tx] = nv;
        for (int i = 0; i < n_; ++i) {
            P& p = parts_[i];
            if (p.tex != tx) continue;
            float age = static_cast<float>(now - tSpawn_) - p.delay;
            if (age > VisibleLife(p)) continue;
            ++live;
            if (age < 0.f) continue;
            const bool dist = tx == kFlashTexDistRing || tx == kFlashTexDistHaze;
            if (dist && !canDist) continue;
            if (p.frame == 1 && !f.brass && (p.locked || !p.placed)) continue;
            float pos[3];
            if (p.locked) {
                toView(p.p, pos, true, p.frame);
            } else {
                if (!p.placed) {
                    float pv[3], vv[3];
                    toView(p.p, pv, true, p.frame);
                    toView(p.v, vv, false, p.frame);
                    memcpy(p.p, pv, sizeof pv);
                    memcpy(p.v, vv, sizeof vv);
                    float gz = p.g[2];
                    p.g[0] = 0, p.g[1] = gz * unit, p.g[2] = 0;
                    p.placed = 1;
                } else if (dt > 0.f) {
                    float k = p.dragRate > 0.f ? std::exp(-p.dragRate * dt) : std::pow(1.f - p.drag, dt * 30.f);
                    for (int c = 0; c < 3; ++c) {
                        p.v[c] = (p.v[c] + p.g[c] * dt) * k;
                        p.p[c] += p.v[c] * dt;
                    }
                }
                memcpy(pos, p.p, sizeof pos);
            }
            if (pos[2] < 0.02f) continue;
            float lf = p.life > 0.f ? Clamp01(age / p.life) : 1.f;
            float rs = p.rs0;
            if (p.rt1 > p.rt0) {
                float u = Clamp01((lf - p.rt0) / (p.rt1 - p.rt0));
                if (p.ease > 0.f) u = 1.f - std::pow(1.f - u, p.ease);
                rs = Lerp(p.rs0, p.rs1, u);
            }
            if (p.nS) rs = CurveAt(p.sK, p.nS, lf);
            float rad = p.r0 * rs * unit;
            float a = p.alpha * s.flashIntensity;
            if (p.fadeOut > 0.f) a *= Clamp01((p.life - age) / p.fadeOut);
            if (p.fadeIn > 0.f) a *= Clamp01(age / p.fadeIn);
            if (p.alphaPow > 0.f) a *= std::pow(1.f - lf, p.alphaPow);
            if (p.nA) a *= CurveAt(p.aK, p.nA, lf);
            float col[3] = {p.col[0], p.col[1], p.col[2]};
            if (p.colorFadeAt >= 0.f && lf > p.colorFadeAt)
                for (float& c : col) c *= 1.f - Clamp01((lf - p.colorFadeAt) / (1.f - p.colorFadeAt));
            if (p.fadeSize1 > p.fadeSize0) {
                float frac = rad * f.ys / pos[2];
                a *= 1.f - Clamp01((frac - p.fadeSize0) / (p.fadeSize1 - p.fadeSize0));
            }
            if (a <= 0.001f) continue;
            const Sheet& sh = sheet_[tx];
            int q = p.seq < 4 ? p.seq : 0, nf = sh.frames[q];
            int fr = p.frame0;
            if (p.fitLife && nf > 1) fr = p.frame0 + static_cast<int>(lf * static_cast<float>(nf - p.frame0));
            else if (p.fps > 0.f && nf > 1) fr = (p.frame0 + static_cast<int>(age * p.fps)) % nf;
            if (fr >= nf) fr = nf - 1;
            if (fr < 0) fr = 0;
            const float* uv = sh.rect[q][fr];
            float u0 = uv[0], u1 = uv[2], v0 = uv[1], v1 = uv[3];
            float rot = p.rot + p.rotSpeed * age, hl = rad;
            float cx = pos[0], cy = pos[1];
            if (p.align) {
                const float seg = 0.05f;
                const float* ax = p.axis == 1 || p.axis == 2 ? f.left : p.axis == 3 ? f.up : f.fwd;
                const float sg = p.axis == 2 ? -seg : seg;
                float e[3] = {pos[0] + ax[0] * sg, pos[1] + ax[1] * sg, std::max(pos[2] + ax[2] * sg, 0.02f)};
                float sx = e[0] * pos[2] / e[2] - pos[0], sy = e[1] * pos[2] / e[2] - pos[1];
                float sl = std::sqrt(sx * sx + sy * sy);
                rot = sl > 1e-6f ? std::atan2(sy, sx) : 1.5707963f;
                hl = rad * (p.aspect > 0.f ? p.aspect : 1.f) * std::max(std::min(sl / seg, 1.f), p.minFore);
                cx += std::cos(rot) * hl * p.anchor;
                cy += std::sin(rot) * hl * p.anchor;
                if (p.flip) std::swap(v0, v1);
            } else {
                if (p.aspect > 0.f) hl = rad * p.aspect;
                cy += rad * p.lift;
                if (p.flip) std::swap(u0, u1);
            }
            float cr = std::cos(rot), sr = std::sin(rot);
            const float cs[4][2] = {{-1, 1}, {1, 1}, {-1, -1}, {1, -1}};
            const float tc[4][2] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
            const bool gas = tx == kFlashTexGasJet || tx == kFlashTexGasPuff || tx == kFlashTexGasBall || tx == kFlashTexGasWisp || dist;
            const bool over = gas || tx == kFlashTexSmoke;
            const float zd = gas && depth && f.zf > f.zn ? f.zf / (f.zf - f.zn) * (1.f - f.zn / pos[2]) : 0.f;
            float bright = p.bright;
            if (dist) {
                col[0] = p.distNoise, col[1] = p.distPush, col[2] = rad * f.ys / pos[2] * 0.5f;
                bright = tx == kFlashTexDistHaze ? 1.f : 0.f;
            }
            FlashVertex q4[4];
            for (int k = 0; k < 4; ++k) {
                float dx = cs[k][0] * hl * cr - cs[k][1] * rad * sr, dy = cs[k][0] * hl * sr + cs[k][1] * rad * cr;
                q4[k] = {(cx + dx) * f.xs / pos[2], (cy + dy) * f.ys / pos[2], tc[k][0], tc[k][1], col[0], col[1], col[2],
                         a, over ? 0.f : 1.f, bright, zd};
            }
            const int idx[6] = {0, 1, 2, 2, 1, 3};
            for (int k = 0; k < 6; ++k) verts[nv++] = q4[idx[k]];
        }
        counts[tx] = nv - starts[tx];
    }
    if (!live) n_ = 0;
    }
    if (!nv) return false;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    memcpy(m.pData, verts, sizeof(FlashVertex) * nv);
    ctx->Unmap(vb_, 0);
    FlashCb cb = {{rtvEncodesSrgb ? 1.f : 0.f, rtvEncodesSrgb && sceneGamma_ ? 1.f : 0.f, 1.f / static_cast<float>(w), 1.f / static_cast<float>(h)},
                  {depth && sceneDepth_ ? 1.f : 0.f, static_cast<float>(w), static_cast<float>(h), 0.f}};
    if (FAILED(ctx->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    memcpy(m.pData, &cb, sizeof cb);
    ctx->Unmap(cb_, 0);
    StateBackup bk;
    bk.Save(ctx);
    D3D11_VIEWPORT vp = {0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs_);
    ctx->OMSetRenderTargets(1, &rtv, depth);
    const float bf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(bs_, bf, 0xffffffff);
    ctx->OMSetDepthStencilState(depth ? dssDepth_ : dss_, 0);
    UINT stride = sizeof(FlashVertex), off = 0;
    ctx->IASetVertexBuffers(0, 1, &vb_, &stride, &off);
    ctx->IASetInputLayout(layout_);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_, nullptr, 0);
    ctx->PSSetShader(ps_, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &cb_);
    ctx->PSSetSamplers(0, 1, &samp_);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    if (canDist && (counts[kFlashTexDistHaze] || counts[kFlashTexDistRing])) {
        ctx->PSSetShader(psDist_, nullptr, 0);
        for (int tx : {kFlashTexDistHaze, kFlashTexDistRing}) {
            if (!counts[tx] || !tex_[tx]) continue;
            ID3D11ShaderResourceView* sv[3] = {tex_[tx], scene_, sceneDepth_};
            ctx->PSSetShaderResources(0, 3, sv);
            ctx->Draw(static_cast<UINT>(counts[tx]), static_cast<UINT>(starts[tx]));
        }
        ID3D11ShaderResourceView* none[3] = {};
        ctx->PSSetShaderResources(0, 3, none);
        ctx->PSSetShader(ps_, nullptr, 0);
    }
    if (trCount) {
        ctx->PSSetShaderResources(0, 1, &tex_[kFlashTexSpark]);
        ctx->Draw(static_cast<UINT>(trCount), static_cast<UINT>(trStart));
    }
    if (imCount) {
        ctx->PSSetShaderResources(0, 1, &tex_[kFlashTexSmoke]);
        ctx->Draw(static_cast<UINT>(imCount), static_cast<UINT>(imStart));
    }
    for (int li = 0; li < 4; ++li) {
        if (!flCount[li] || !flTex_[li]) continue;
        ctx->PSSetShaderResources(0, 1, &flTex_[li]);
        ctx->Draw(static_cast<UINT>(flCount[li]), static_cast<UINT>(flStart[li]));
    }
    for (int oi = 2; oi < 10; ++oi) {
        int tx = order[oi];
        if (!counts[tx] || !tex_[tx]) continue;
        ctx->PSSetShaderResources(0, 1, &tex_[tx]);
        ctx->Draw(static_cast<UINT>(counts[tx]), static_cast<UINT>(starts[tx]));
    }
    bk.Restore(ctx);
    return true;
}
void MuzzleFlash::Dump(double now) const {
    for (int i = 0; i < n_; ++i) {
        const P& p = parts_[i];
        printf("  part %2d tex %2d age %7.3f life %6.3f alpha %.3f locked %d placed %d\n", i, p.tex,
               static_cast<float>(now - tSpawn_) - p.delay, p.life, p.alpha, p.locked, p.placed);
    }
}
bool MuzzleFlash::HasDistortion() const {
    for (int i = 0; i < n_; ++i)
        if (parts_[i].tex == kFlashTexDistRing || parts_[i].tex == kFlashTexDistHaze) return true;
    return false;
}
void MuzzleFlash::Release() {
    SafeRelease(vs_);
    SafeRelease(ps_);
    SafeRelease(psDist_);
    SafeRelease(layout_);
    SafeRelease(vb_);
    SafeRelease(cb_);
    SafeRelease(bs_);
    SafeRelease(rs_);
    SafeRelease(dss_);
    SafeRelease(dssDepth_);
    SafeRelease(samp_);
    for (int i = 0; i < kFlashTexCount; ++i) SafeRelease(tex_[i]);
    for (auto& t : flTex_) SafeRelease(t);
    nFl_ = 0;
    scene_ = nullptr;
    sceneDepth_ = nullptr;
    burst_ = 0;
    tLastShot_ = -1.0;
    n_ = 0;
    nTr_ = 0;
    nImp_ = 0;
    pending_ = false;
    tSpawn_ = tSim_ = -1.0;
    dev_ = nullptr;
}
bool Crosshair::Init(ID3D11Device* dev) {
    Release();
    dev_ = dev;
    FnD3DCompile compile = GetCompiler();
    if (!compile) return false;
    ID3DBlob *vsb = nullptr, *psb = nullptr, *msg = nullptr;
    bool ok = SUCCEEDED(compile(kCrossShader, sizeof kCrossShader - 1, "tenzin_fps_cross", nullptr, nullptr, "VS", "vs_5_0",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(compile(kCrossShader, sizeof kCrossShader - 1, "tenzin_fps_cross", nullptr, nullptr, "PS", "ps_5_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) &&
         SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_));
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = ok && SUCCEEDED(dev->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_));
    SafeRelease(vsb);
    SafeRelease(psb);
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(CrossVertex) * 128;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &vb_));
    D3D11_BLEND_DESC bld = {};
    bld.RenderTarget[0].BlendEnable = TRUE;
    bld.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bld.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    ok = ok && SUCCEEDED(dev->CreateBlendState(&bld, &bs_));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(dev->CreateRasterizerState(&rd, &rs_));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    ok = ok && SUCCEEDED(dev->CreateDepthStencilState(&dd, &dss_));
    if (!ok) Release();
    return ok;
}
void Crosshair::Release() {
    SafeRelease(vs_);
    SafeRelease(ps_);
    SafeRelease(layout_);
    SafeRelease(vb_);
    SafeRelease(bs_);
    SafeRelease(rs_);
    SafeRelease(dss_);
    dev_ = nullptr;
}
bool Crosshair::Prewarm(ID3D11Device* dev) {
    if (failed_ || !dev) return false;
    if (dev == dev_ && vs_) return true;
    if (!Init(dev)) {
        failed_ = true;
        return false;
    }
    return true;
}
bool Crosshair::Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, UINT w, UINT h, bool rtvEncodesSrgb,
                     const CrosshairSettings& s, float spread, float alpha) {
    if (!s.enabled || !ctx || !rtv || w < 16 || h < 16 || failed_ || !(alpha > 0.004f)) return false;
    if (alpha > 1.f) alpha = 1.f;
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return false;
    if (dev != dev_ || !vs_) {
        if (!Init(dev)) {
            failed_ = true;
            dev->Release();
            return false;
        }
    }
    dev->Release();
    float k = static_cast<float>(h) / 1080.f;
    float th = std::fmax(1.f, std::round(s.thickness * k)), len = std::round(s.length * k);
    float gap = std::round((s.gap + (spread > 0.f ? spread : 0.f)) * k);
    float ol = s.outline > 0.f ? std::fmax(1.f, std::round(s.outline * k)) : 0.f;
    float cx = std::floor(w * 0.5f), cy = std::floor(h * 0.5f);
    float x0 = cx - std::floor(th * 0.5f), y0 = cy - std::floor(th * 0.5f);
    struct R {
        float l, t, r, b;
    };
    R rects[5];
    int n = 0;
    if (len > 0.f) {
        rects[n++] = {cx + gap + th - std::floor(th * 0.5f), y0, cx + gap + th - std::floor(th * 0.5f) + len, y0 + th};
        rects[n++] = {x0 - gap - len, y0, x0 - gap, y0 + th};
        rects[n++] = {x0, y0 - gap - len, x0 + th, y0 - gap};
        rects[n++] = {x0, cy + gap + th - std::floor(th * 0.5f), x0 + th, cy + gap + th - std::floor(th * 0.5f) + len};
    }
    const bool roundDot = s.dot && s.dotSize > 0.f;
    if (s.dot && !roundDot) rects[n++] = {x0, y0, x0 + th, y0 + th};
    if (!n && !roundDot) return false;
    float col[4] = {s.color[0], s.color[1], s.color[2], s.color[3] * alpha};
    if (rtvEncodesSrgb)
        for (int c = 0; c < 3; ++c) col[c] = SrgbToLinear(col[c]);
    CrossVertex v[96];
    int nv = 0;
    auto quad = [&](const R& q, float e, const float* c4) {
        float l = (q.l - e) / w * 2.f - 1.f, r = (q.r + e) / w * 2.f - 1.f;
        float t = 1.f - (q.t - e) / h * 2.f, b = 1.f - (q.b + e) / h * 2.f;
        const CrossVertex a = {l, t, c4[0], c4[1], c4[2], c4[3], 0, 0, 0}, bq = {r, t, c4[0], c4[1], c4[2], c4[3], 0, 0, 0},
                          cq = {l, b, c4[0], c4[1], c4[2], c4[3], 0, 0, 0}, dq = {r, b, c4[0], c4[1], c4[2], c4[3], 0, 0, 0};
        v[nv++] = a;
        v[nv++] = bq;
        v[nv++] = cq;
        v[nv++] = cq;
        v[nv++] = bq;
        v[nv++] = dq;
    };
    const float dcx = x0 + th * 0.5f, dcy = y0 + th * 0.5f;
    auto disc = [&](float rad, const float* c4) {
        float e = rad + 1.f;
        float l = (dcx - e) / w * 2.f - 1.f, r = (dcx + e) / w * 2.f - 1.f;
        float t = 1.f - (dcy - e) / h * 2.f, b = 1.f - (dcy + e) / h * 2.f;
        const CrossVertex a = {l, t, c4[0], c4[1], c4[2], c4[3], -e, -e, rad}, bq = {r, t, c4[0], c4[1], c4[2], c4[3], e, -e, rad},
                          cq = {l, b, c4[0], c4[1], c4[2], c4[3], -e, e, rad}, dq = {r, b, c4[0], c4[1], c4[2], c4[3], e, e, rad};
        v[nv++] = a;
        v[nv++] = bq;
        v[nv++] = cq;
        v[nv++] = cq;
        v[nv++] = bq;
        v[nv++] = dq;
    };
    const float dotR = std::fmax(0.5f, s.dotSize * k * 0.5f);
    const float dotOl = s.dotOutline > 0.f ? std::fmax(1.f, std::round(s.dotOutline * k)) : 0.f;
    const float black[4] = {0.f, 0.f, 0.f, (s.outlineAlpha >= 0.f ? s.outlineAlpha : s.color[3]) * alpha};
    if (ol > 0.f)
        for (int i = 0; i < n; ++i) quad(rects[i], ol, black);
    if (roundDot && dotOl > 0.f) disc(dotR + dotOl, black);
    for (int i = 0; i < n; ++i) quad(rects[i], 0.f, col);
    if (roundDot) disc(dotR, col);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(vb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    memcpy(m.pData, v, sizeof(CrossVertex) * nv);
    ctx->Unmap(vb_, 0);
    StateBackup bk;
    bk.Save(ctx);
    D3D11_VIEWPORT vp = {0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs_);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    const float bf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(bs_, bf, 0xffffffff);
    ctx->OMSetDepthStencilState(dss_, 0);
    UINT stride = sizeof(CrossVertex), off = 0;
    ctx->IASetVertexBuffers(0, 1, &vb_, &stride, &off);
    ctx->IASetInputLayout(layout_);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_, nullptr, 0);
    ctx->PSSetShader(ps_, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->Draw(static_cast<UINT>(nv), 0);
    bk.Restore(ctx);
    return true;
}
namespace {
const char kShellShader[] = R"HLSL(cbuffer CShellFrame : register(b0) {
    row_major float4x4 uProj;
    float4 uLightDir;
    float4 uLightColor;
    float4 uAmbSky;
    float4 uAmbGround;
    float4 uParams;
    float4 uScene;
    float4 uFlashPos;
    float4 uFlashCol;
    float4 uBrass;
};
cbuffer CShellObj : register(b1) { float4 uRows[192]; };
Texture2D tScene : register(t3);
SamplerState sLinear : register(s0);
struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float shade : TEXCOORD0; };
struct PSIn { float4 pos : SV_Position; float3 vpos : TEXCOORD1; float3 n : TEXCOORD2; float shade : TEXCOORD0; };
PSIn VS(VSIn i, uint id : SV_InstanceID) {
    float3x4 m = float3x4(uRows[id * 3], uRows[id * 3 + 1], uRows[id * 3 + 2]);
    PSIn o;
    o.vpos = mul(m, float4(i.pos, 1));
    o.pos = mul(uProj, float4(o.vpos, 1));
    o.n = mul((float3x3)m, i.nrm);
    o.shade = i.shade;
    return o;
}
float3 SrgbEncode(float3 c) {
    c = saturate(c);
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return float3(c.x <= 0.0031308 ? lo.x : hi.x, c.y <= 0.0031308 ? lo.y : hi.y, c.z <= 0.0031308 ? lo.z : hi.z);
}
float3 SrgbDecode(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return float3(c.x <= 0.04045 ? lo.x : hi.x, c.y <= 0.04045 ? lo.y : hi.y, c.z <= 0.04045 ? lo.z : hi.z);
}
float4 PS(PSIn i) : SV_Target {
    float3 alb = uBrass.rgb * i.shade;
    float rough = clamp(uBrass.w, 0.04, 1);
    float3 N = normalize(i.n);
    float3 V = normalize(-i.vpos);
    if (dot(N, V) < 0) N = -N;
    float3 ambSky = uAmbSky.rgb, ambGround = uAmbGround.rgb, lightCol = uLightColor.rgb;
    if (uScene.w > 0.5) {
        float3 sTop = tScene.SampleLevel(sLinear, float2(0.5, 0.25), uScene.y).rgb;
        float3 sBot = tScene.SampleLevel(sLinear, float2(0.5, 0.75), uScene.y).rgb;
        if (uScene.z > 0.5) { sTop = SrgbDecode(saturate(sTop)); sBot = SrgbDecode(saturate(sBot)); }
        ambSky = lerp(ambSky, sTop * 1.3, uScene.x);
        ambGround = lerp(ambGround, sBot * 1.3, uScene.x);
        float3 avg = (sTop + sBot) * 0.5;
        float lum = dot(avg, float3(0.2126, 0.7152, 0.0722));
        float3 tint = lum > 1e-4 ? clamp(avg / lum, 0, 3) : float3(1, 1, 1);
        lightCol *= lerp(float3(1, 1, 1), tint, uScene.x * 0.6) * lerp(1.0, saturate(lum * 4.0), uScene.x * 0.6);
    }
    float3 L = normalize(uLightDir.xyz);
    float3 H = normalize(L + V);
    float NdL = saturate(dot(N, L)), NdV = saturate(dot(N, V)) + 1e-4, NdH = saturate(dot(N, H)), VdH = saturate(dot(V, H));
    float a = rough * rough, a2 = a * a;
    float d = NdH * NdH * (a2 - 1) + 1;
    float D = a2 / (3.14159265 * d * d);
    float k = (rough + 1) * (rough + 1) / 8;
    float G = (NdL / (NdL * (1 - k) + k)) * (NdV / (NdV * (1 - k) + k));
    float3 F0 = alb;
    float3 F = F0 + (1 - F0) * pow(1 - VdH, 5);
    float3 direct = D * G * F / (4 * NdL * NdV + 1e-4) * lightCol * NdL;
    if (uFlashCol.w > 0) {
        float3 Lp = uFlashPos.xyz - i.vpos;
        float dist = length(Lp);
        float3 Lf = Lp / max(dist, 1e-4);
        float att = saturate(1 - dist / uFlashPos.w);
        att *= att;
        float3 Hf = normalize(Lf + V);
        float NdLf = saturate(dot(N, Lf)), NdHf = saturate(dot(N, Hf));
        float df = NdHf * NdHf * (a2 - 1) + 1;
        float Ref = a2 / (3.14159265 * df * df);
        float Gf = (NdLf / (NdLf * (1 - k) + k)) * (NdV / (NdV * (1 - k) + k));
        float3 Ff = F0 + (1 - F0) * pow(1 - saturate(dot(V, Hf)), 5);
        direct += Ref * Gf * Ff / (4 * NdLf * NdV + 1e-4) * uFlashCol.rgb * NdLf * att;
    }
    float3 Fa = F0 + (max(float3(1 - rough, 1 - rough, 1 - rough), F0) - F0) * pow(1 - NdV, 5);
    float3 ambient = lerp(ambGround, ambSky, saturate(reflect(-V, N).y * 0.5 + 0.5)) * Fa * (1 - rough * 0.7);
    float3 c = (direct + ambient) * uParams.x;
    if (uParams.y > 0.5) c = SrgbEncode(c);
    return float4(c, 1);
}
)HLSL";
struct ShellCbFrame {
    float proj[16];
    float lightDir[4], lightColor[4], ambSky[4], ambGround[4];
    float params[4], scene[4], flashPos[4], flashCol[4], brass[4];
};
static_assert(sizeof(ShellCbFrame) == 208, "shell cb");
struct ShellVtx {
    float p[3], n[3], shade;
};
constexpr int kShellSeg = 16;
struct ShellProfile {
    float x0, r0, x1, r1, shade;
};
const ShellProfile kProfile556[] = {
    {0.00f, 4.80f, 1.14f, 4.80f, 0.95f},
    {1.14f, 4.80f, 1.30f, 4.20f, 0.85f},
    {1.30f, 4.20f, 1.90f, 4.20f, 0.70f},
    {1.90f, 4.20f, 2.40f, 4.78f, 0.85f},
    {2.40f, 4.78f, 36.50f, 4.50f, 1.00f},
    {36.50f, 4.50f, 39.60f, 3.21f, 1.00f},
    {39.60f, 3.21f, 44.70f, 3.21f, 1.00f},
    {44.70f, 3.21f, 44.70f, 2.86f, 0.90f},
    {44.70f, 2.86f, 40.00f, 2.86f, 0.22f},
};
const ShellProfile kProfile762[] = {
    {0.00f, 5.97f, 1.27f, 5.97f, 0.95f},
    {1.27f, 5.97f, 1.45f, 5.20f, 0.85f},
    {1.45f, 5.20f, 2.10f, 5.20f, 0.70f},
    {2.10f, 5.20f, 2.80f, 5.98f, 0.85f},
    {2.80f, 5.98f, 39.60f, 5.77f, 1.00f},
    {39.60f, 5.77f, 43.60f, 4.39f, 1.00f},
    {43.60f, 4.39f, 51.18f, 4.39f, 1.00f},
    {51.18f, 4.39f, 51.18f, 3.95f, 0.90f},
    {51.18f, 3.95f, 45.00f, 3.95f, 0.22f},
};
constexpr int kMeshVertCap = 1200, kMeshIdxCap = 4800;
struct MeshOut {
    ShellVtx* v;
    uint32_t* ix;
    int nv, ni;
};
void LatheCasing(MeshOut& m, const ShellProfile* prof, int n, float len, float innerX, float innerR) {
    const float xc = len * 0.45f, mm = 0.001f;
    const int v0 = m.nv;
    for (int i = 0; i < n; ++i) {
        const ShellProfile& b = prof[i];
        float dx = b.x1 - b.x0, dr = b.r1 - b.r0, l = std::sqrt(dx * dx + dr * dr);
        float nx = -dr / l, nr = dx / l;
        uint32_t base = static_cast<uint32_t>(m.nv - v0);
        for (int k = 0; k <= kShellSeg; ++k) {
            float a = 6.2831853f * static_cast<float>(k) / kShellSeg, c = std::cos(a), s = std::sin(a);
            m.v[m.nv++] = {{(b.x0 - xc) * mm, b.r0 * c * mm, b.r0 * s * mm}, {nx, nr * c, nr * s}, b.shade};
            m.v[m.nv++] = {{(b.x1 - xc) * mm, b.r1 * c * mm, b.r1 * s * mm}, {nx, nr * c, nr * s}, b.shade};
        }
        for (int k = 0; k < kShellSeg; ++k) {
            uint32_t a0 = base + k * 2, a1 = a0 + 1, b0 = a0 + 2, b1 = a0 + 3;
            m.ix[m.ni++] = a0, m.ix[m.ni++] = b0, m.ix[m.ni++] = a1;
            m.ix[m.ni++] = a1, m.ix[m.ni++] = b0, m.ix[m.ni++] = b1;
        }
    }
    const float disk[2][4] = {{0.f, prof[0].r0, -1.f, 0.9f}, {innerX, innerR, 1.f, 0.12f}};
    for (const auto& d : disk) {
        uint32_t c0 = static_cast<uint32_t>(m.nv - v0);
        m.v[m.nv++] = {{(d[0] - xc) * mm, 0.f, 0.f}, {d[2], 0.f, 0.f}, d[3]};
        for (int k = 0; k <= kShellSeg; ++k) {
            float a = 6.2831853f * static_cast<float>(k) / kShellSeg;
            m.v[m.nv++] = {{(d[0] - xc) * mm, d[1] * std::cos(a) * mm, d[1] * std::sin(a) * mm}, {d[2], 0.f, 0.f}, d[3]};
        }
        for (int k = 0; k < kShellSeg; ++k) m.ix[m.ni++] = c0, m.ix[m.ni++] = c0 + 1 + k, m.ix[m.ni++] = c0 + 2 + k;
    }
}
void BuildLink(MeshOut& m) {
    const float mm = 0.001f, r = 6.2f, pitch = 12.7f, yc = pitch * 0.5f;
    const int seg = 12, v0 = m.nv;
    auto band = [&](float x0, float x1, float cy) {
        uint32_t base = static_cast<uint32_t>(m.nv - v0);
        for (int k = 0; k <= seg; ++k) {
            float a = (120.f + 300.f * static_cast<float>(k) / seg) * kDeg, c = std::cos(a), s = std::sin(a);
            m.v[m.nv++] = {{x0 * mm, (cy + r * c - yc) * mm, r * s * mm}, {0.f, c, s}, 1.f};
            m.v[m.nv++] = {{x1 * mm, (cy + r * c - yc) * mm, r * s * mm}, {0.f, c, s}, 1.f};
        }
        for (int k = 0; k < seg; ++k) {
            uint32_t a0 = base + k * 2, a1 = a0 + 1, b0 = a0 + 2, b1 = a0 + 3;
            m.ix[m.ni++] = a0, m.ix[m.ni++] = b0, m.ix[m.ni++] = a1;
            m.ix[m.ni++] = a1, m.ix[m.ni++] = b0, m.ix[m.ni++] = b1;
        }
    };
    band(-4.5f, 4.5f, 0.f);
    band(-11.f, -6.5f, pitch);
    band(6.5f, 11.f, pitch);
    uint32_t base = static_cast<uint32_t>(m.nv - v0);
    const float pz = -r * mm;
    m.v[m.nv++] = {{-11.f * mm, (0.f - yc) * mm, pz}, {0.f, 0.f, -1.f}, 0.85f};
    m.v[m.nv++] = {{11.f * mm, (0.f - yc) * mm, pz}, {0.f, 0.f, -1.f}, 0.85f};
    m.v[m.nv++] = {{-11.f * mm, (pitch - yc) * mm, pz}, {0.f, 0.f, -1.f}, 0.85f};
    m.v[m.nv++] = {{11.f * mm, (pitch - yc) * mm, pz}, {0.f, 0.f, -1.f}, 0.85f};
    m.ix[m.ni++] = base, m.ix[m.ni++] = base + 2, m.ix[m.ni++] = base + 1;
    m.ix[m.ni++] = base + 1, m.ix[m.ni++] = base + 2, m.ix[m.ni++] = base + 3;
}
void QuatAxis(const float* ax, float ang, float* q) {
    float s = std::sin(ang * 0.5f);
    q[0] = ax[0] * s, q[1] = ax[1] * s, q[2] = ax[2] * s, q[3] = std::cos(ang * 0.5f);
}
void QuatMul(const float* a, const float* b, float* o) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof r);
}
void QuatToMat(const float* q, float* m) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z), m[1] = 2 * (x * y - z * w), m[2] = 2 * (x * z + y * w);
    m[3] = 2 * (x * y + z * w), m[4] = 1 - 2 * (x * x + z * z), m[5] = 2 * (y * z - x * w);
    m[6] = 2 * (x * z - y * w), m[7] = 2 * (y * z + x * w), m[8] = 1 - 2 * (x * x + y * y);
}
void MatToQuat(const float* m, float* q) {
    float tr = m[0] + m[4] + m[8];
    if (tr > 0.f) {
        float s = std::sqrt(tr + 1.f) * 2.f;
        q[3] = 0.25f * s, q[0] = (m[7] - m[5]) / s, q[1] = (m[2] - m[6]) / s, q[2] = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = std::sqrt(1.f + m[0] - m[4] - m[8]) * 2.f;
        q[3] = (m[7] - m[5]) / s, q[0] = 0.25f * s, q[1] = (m[1] + m[3]) / s, q[2] = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        float s = std::sqrt(1.f + m[4] - m[0] - m[8]) * 2.f;
        q[3] = (m[2] - m[6]) / s, q[0] = (m[1] + m[3]) / s, q[1] = 0.25f * s, q[2] = (m[5] + m[7]) / s;
    } else {
        float s = std::sqrt(1.f + m[8] - m[0] - m[4]) * 2.f;
        q[3] = (m[3] - m[1]) / s, q[0] = (m[2] + m[6]) / s, q[1] = (m[5] + m[7]) / s, q[2] = 0.25f * s;
    }
    float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int k = 0; k < 4; ++k) q[k] /= l;
}
void Norm3(float* v) {
    float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-12f)
        for (int k = 0; k < 3; ++k) v[k] /= l;
}
void Cross3(const float* a, const float* b, float* o) {
    float r[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    memcpy(o, r, sizeof r);
}
}
float ShellEject::Rand(float a, float b) {
    rng_ = rng_ * 1664525u + 1013904223u;
    return a + (b - a) * static_cast<float>((rng_ >> 8) & 0xffffff) / 16777216.f;
}
bool ShellEject::Load(ID3D11Device* dev, char* err, size_t errLen) {
    Release();
    auto fail = [&](const char* m) {
        if (err && errLen) snprintf(err, errLen, "shell eject: %s", m);
        Release();
        return false;
    };
    if (!dev) return fail("no device");
    FnD3DCompile compile = GetCompiler();
    if (!compile) return fail("d3dcompiler_47 missing");
    ID3DBlob *vsb = nullptr, *psb = nullptr, *msg = nullptr;
    bool ok = SUCCEEDED(compile(kShellShader, sizeof kShellShader - 1, "tenzin_fps_shell", nullptr, nullptr, "VS", "vs_5_0",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(compile(kShellShader, sizeof kShellShader - 1, "tenzin_fps_shell", nullptr, nullptr, "PS", "ps_5_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) &&
         SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_));
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = ok && SUCCEEDED(dev->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_));
    SafeRelease(vsb);
    SafeRelease(psb);
    if (!ok) return fail("shader / layout");
    static ShellVtx verts[kMeshVertCap];
    static uint32_t idx[kMeshIdxCap];
    MeshOut mo = {verts, idx, 0, 0};
    for (int k = 0; k < 3; ++k) {
        kindIdx_[k] = static_cast<UINT>(mo.nv);
        kindFirst_[k] = static_cast<UINT>(mo.ni);
        if (k == 0) LatheCasing(mo, kProfile556, sizeof kProfile556 / sizeof *kProfile556, 44.7f, 40.0f, 2.86f);
        else if (k == 1) LatheCasing(mo, kProfile762, sizeof kProfile762 / sizeof *kProfile762, 51.18f, 45.0f, 3.95f);
        else BuildLink(mo);
        kindCount_[k] = static_cast<UINT>(mo.ni) - kindFirst_[k];
    }
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = sizeof(ShellVtx) * static_cast<UINT>(mo.nv);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {verts, 0, 0};
    ok = SUCCEEDED(dev->CreateBuffer(&bd, &sd, &vb_));
    bd.ByteWidth = sizeof(uint32_t) * static_cast<UINT>(mo.ni);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = idx;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, &sd, &ib_));
    indexCount_ = static_cast<UINT>(mo.ni);
    D3D11_BUFFER_DESC cd = {};
    cd.Usage = D3D11_USAGE_DYNAMIC;
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cd.ByteWidth = sizeof(ShellCbFrame);
    ok = ok && SUCCEEDED(dev->CreateBuffer(&cd, nullptr, &cbFrame_));
    cd.ByteWidth = sizeof(float) * 4 * 3 * kMax;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&cd, nullptr, &cbObj_));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(dev->CreateRasterizerState(&rd, &rs_));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ok = ok && SUCCEEDED(dev->CreateDepthStencilState(&dd, &dss_));
    dd.DepthEnable = FALSE;
    ok = ok && SUCCEEDED(dev->CreateDepthStencilState(&dd, &dssNoDepth_));
    D3D11_BLEND_DESC bld = {};
    bld.RenderTarget[0].BlendEnable = FALSE;
    bld.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    ok = ok && SUCCEEDED(dev->CreateBlendState(&bld, &bs_));
    D3D11_SAMPLER_DESC sm = {};
    sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(dev->CreateSamplerState(&sm, &samp_));
    if (!ok) return fail("buffers / states");
    n_ = pending_ = 0;
    tSim_ = -1.0;
    camValid_ = false;
    return true;
}
bool ShellEject::Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* depth, UINT w, UINT h,
                      bool rtvEncodesSrgb, double now, const MuzzleFrame* f, const WorldCam* cam, const Settings& s, float flashLight,
                      ID3D11ShaderResourceView* scene, bool sceneGamma) {
    if (!vs_ || !ctx || !rtv || !w || !h) return false;
    float dt = tSim_ >= 0.0 ? static_cast<float>(now - tSim_) : 0.f;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;
    tSim_ = now;
    const bool follow = s.shellFollow != 0;
    if (cam) {
        if (camValid_ && dt > 0.f) {
            float d[3] = {cam->eye[0] - camPrev_[0], cam->eye[1] - camPrev_[1], cam->eye[2] - camPrev_[2]};
            if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 9.f) {
                camVel_[0] = camVel_[1] = camVel_[2] = 0.f;
            } else {
                float k = 1.f - std::exp(-dt / 0.08f);
                for (int c = 0; c < 3; ++c) camVel_[c] += (d[c] / dt - camVel_[c]) * k;
                if (follow)
                    for (int i = 0; i < n_; ++i)
                        if (sh_[i].world)
                            for (int c = 0; c < 3; ++c) sh_[i].p[c] += d[c];
            }
        }
        memcpy(camPrev_, cam->eye, sizeof camPrev_);
        camValid_ = true;
    } else {
        camValid_ = false;
        camVel_[0] = camVel_[1] = camVel_[2] = 0.f;
    }
    auto toWorldV = [&](const float* v, float* o) {
        for (int c = 0; c < 3; ++c) o[c] = cam->right[c] * v[0] + cam->up[c] * v[1] + cam->fwd[c] * v[2];
    };
    const bool canSpawn = f && f->brass && s.shellEject;
    if (!canSpawn) pending_ = 0;
    auto spawnOne = [&](int kind) {
        if (n_ >= kMax) {
            memmove(sh_, sh_ + 1, sizeof(Shell) * (kMax - 1));
            --n_;
        }
        Shell& e = sh_[n_++];
        const float g = f->unit / 0.0254f;
        const float* O = f->bout;
        const float* F = f->bfwd;
        const float* U = f->bup;
        const bool link = kind == 2;
        const float *ro = link ? s.linkOut : s.shellOut, *rf = link ? s.linkFwd : s.shellFwd, *ru = link ? s.linkUp : s.shellUp;
        const float* lr = link ? s.linkLifeRange : s.shellLifeRange;
        float sp = s.shellSpeed * g;
        float vo = Rand(ro[0], ro[1]) * sp, vf = Rand(rf[0], rf[1]) * sp, vu = Rand(ru[0], ru[1]) * sp;
        float p[3], v[3];
        for (int c = 0; c < 3; ++c) {
            p[c] = f->bpos[c] + O[c] * 0.004f * g;
            v[c] = O[c] * vo + F[c] * vf + U[c] * vu;
        }
        float ty = std::tan(Rand(-12.f, 12.f) * kDeg), tp = std::tan(Rand(-8.f, 8.f) * kDeg);
        float X[3], Y[3], Z[3];
        for (int c = 0; c < 3; ++c) X[c] = F[c] + O[c] * ty + U[c] * tp;
        Norm3(X);
        float du = U[0] * X[0] + U[1] * X[1] + U[2] * X[2];
        for (int c = 0; c < 3; ++c) Y[c] = U[c] - X[c] * du;
        Norm3(Y);
        Cross3(X, Y, Z);
        float wv[3];
        if (link) {
            float rl = Rand(0.f, 6.2831853f), cr = std::cos(rl), sr = std::sin(rl);
            for (int c = 0; c < 3; ++c) {
                float y = Y[c], z = Z[c];
                Y[c] = y * cr + z * sr, Z[c] = -y * sr + z * cr;
            }
            float wx = Rand(2000.f, 3200.f) * kDeg, wy = Rand(-2300.f, -500.f) * kDeg, wz = Rand(-1750.f, 1750.f) * kDeg;
            for (int c = 0; c < 3; ++c) wv[c] = X[c] * wx + Y[c] * wy + Z[c] * wz;
        } else {
            float A[3];
            Cross3(F, O, A);
            Norm3(A);
            float spin = Rand(s.shellSpin[0], s.shellSpin[1]) * kDeg, wo = Rand(-90.f, 90.f) * kDeg, wf = Rand(-200.f, 200.f) * kDeg;
            for (int c = 0; c < 3; ++c) wv[c] = A[c] * spin + O[c] * wo + F[c] * wf;
        }
        e.world = cam != nullptr;
        if (e.world) {
            float pw[3], vw[3], m[9], cx[3], cy[3], cz[3];
            toWorldV(p, pw);
            for (int c = 0; c < 3; ++c) pw[c] += cam->eye[c];
            toWorldV(v, vw);
            if (!follow)
                for (int c = 0; c < 3; ++c) vw[c] += camVel_[c];
            toWorldV(X, cx), toWorldV(Y, cy), toWorldV(Z, cz);
            for (int r = 0; r < 3; ++r) m[r * 3] = cx[r], m[r * 3 + 1] = cy[r], m[r * 3 + 2] = cz[r];
            MatToQuat(m, e.q);
            memcpy(e.p, pw, sizeof pw);
            memcpy(e.v, vw, sizeof vw);
            toWorldV(wv, e.w);
        } else {
            float m[9];
            for (int r = 0; r < 3; ++r) m[r * 3] = X[r], m[r * 3 + 1] = Y[r], m[r * 3 + 2] = Z[r];
            MatToQuat(m, e.q);
            memcpy(e.p, p, sizeof p);
            memcpy(e.v, v, sizeof v);
            memcpy(e.w, wv, sizeof wv);
        }
        e.age = 0.f;
        e.life = Rand(lr[0], lr[1]) * s.shellLife;
        e.size = s.shellScale * g;
        e.g = g * (link ? s.linkGravity : s.shellGravity);
        e.kind = static_cast<unsigned char>(kind);
        e.landed = false;
    };
    for (; pending_ > 0; --pending_) {
        spawnOne(s.shellCaliber == 762 ? 1 : 0);
        if (s.shellLink) spawnOne(2);
    }
    int keep = 0;
    for (int i = 0; i < n_; ++i) {
        Shell& e = sh_[i];
        e.age += dt;
        if (e.age >= e.life) continue;
        if (dt > 0.f) {
            float drag = std::exp(-0.25f * dt);
            e.v[1] -= 9.81f * e.g * dt;
            for (int c = 0; c < 3; ++c) e.v[c] *= drag, e.p[c] += e.v[c] * dt;
            if (e.world && cam && !e.landed && e.v[1] < 0.f && e.p[1] < cam->eye[1] - s.shellGround) {
                e.landed = true;
                if (nLand_ < 16) {
                    float d[3] = {e.p[0] - cam->eye[0], e.p[1] - cam->eye[1], e.p[2] - cam->eye[2]};
                    land_[nLand_][0] = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    land_[nLand_][1] = e.age;
                    land_[nLand_][2] = static_cast<float>(e.kind);
                    ++nLand_;
                }
            }
            float wl = std::sqrt(e.w[0] * e.w[0] + e.w[1] * e.w[1] + e.w[2] * e.w[2]);
            if (wl > 1e-6f) {
                float ax[3] = {e.w[0] / wl, e.w[1] / wl, e.w[2] / wl}, dq[4];
                QuatAxis(ax, wl * dt, dq);
                QuatMul(dq, e.q, e.q);
                float l = std::sqrt(e.q[0] * e.q[0] + e.q[1] * e.q[1] + e.q[2] * e.q[2] + e.q[3] * e.q[3]);
                for (float& c : e.q) c /= l;
            }
        }
        sh_[keep++] = e;
    }
    n_ = keep;
    lastMinX_ = 1e9f;
    if (!n_ || !f) return false;
    static float rowsK[3][kMax * 12];
    int ndK[3] = {}, nd = 0;
    for (int i = 0; i < n_; ++i) {
        const Shell& e = sh_[i];
        float lf = e.age / e.life;
        float u = lf < 0.6f ? 0.f : (lf - 0.6f) / 0.4f;
        float sc = e.size * (1.f - u * u * (3.f - 2.f * u));
        if (sc < 1e-4f) continue;
        float m[9], pv[3], mv[9];
        QuatToMat(e.q, m);
        if (e.world) {
            if (!cam) continue;
            float d[3] = {e.p[0] - cam->eye[0], e.p[1] - cam->eye[1], e.p[2] - cam->eye[2]};
            const float* ax[3] = {cam->right, cam->up, cam->fwd};
            for (int r = 0; r < 3; ++r) {
                pv[r] = ax[r][0] * d[0] + ax[r][1] * d[1] + ax[r][2] * d[2];
                for (int c = 0; c < 3; ++c) mv[r * 3 + c] = ax[r][0] * m[c] + ax[r][1] * m[3 + c] + ax[r][2] * m[6 + c];
            }
        } else {
            memcpy(pv, e.p, sizeof pv);
            memcpy(mv, m, sizeof mv);
        }
        if (pv[2] < f->zn + 0.005f || pv[2] > f->zf) continue;
        if (pv[0] < lastMinX_) lastMinX_ = pv[0];
        const int k = e.kind < 3 ? e.kind : 0;
        float* o = rowsK[k] + ndK[k] * 12;
        for (int r = 0; r < 3; ++r)
            o[r * 4] = mv[r * 3] * sc, o[r * 4 + 1] = mv[r * 3 + 1] * sc, o[r * 4 + 2] = mv[r * 3 + 2] * sc, o[r * 4 + 3] = pv[r];
        ++ndK[k], ++nd;
    }
    if (!nd) return false;
    ShellCbFrame cf = {};
    const float zn = f->zn, zf = f->zf;
    const float P[16] = {f->xs, 0, 0, 0, 0, f->ys, 0, 0, 0, 0, zf / (zf - zn), -zn * zf / (zf - zn), 0, 0, 1, 0};
    memcpy(cf.proj, P, sizeof P);
    float ln = std::sqrt(s.lightDir[0] * s.lightDir[0] + s.lightDir[1] * s.lightDir[1] + s.lightDir[2] * s.lightDir[2]);
    if (ln < 1e-6f) ln = 1;
    for (int k = 0; k < 3; ++k) {
        cf.lightDir[k] = s.lightDir[k] / ln;
        cf.lightColor[k] = s.lightColor[k] * s.lightIntensity;
        cf.ambSky[k] = s.ambSky[k] * s.ambIntensity;
        cf.ambGround[k] = s.ambGround[k] * s.ambIntensity;
    }
    cf.params[0] = s.exposure;
    cf.params[1] = rtvEncodesSrgb ? 0.f : 1.f;
    if (scene && s.sceneLight > 0.001f) {
        ID3D11Resource* res = nullptr;
        scene->GetResource(&res);
        ID3D11Texture2D* tex = nullptr;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex) {
            D3D11_TEXTURE2D_DESC td;
            tex->GetDesc(&td);
            cf.scene[0] = s.sceneLight;
            cf.scene[1] = static_cast<float>(td.MipLevels >= 2 ? td.MipLevels - 2 : 0);
            cf.scene[2] = sceneGamma ? 1.f : 0.f;
            cf.scene[3] = 1.f;
        }
        SafeRelease(tex);
        SafeRelease(res);
    }
    if (flashLight > 0.f && s.flashLight > 0.f) {
        for (int k = 0; k < 3; ++k) cf.flashPos[k] = f->pos[k] + f->fwd[k] * 4.f * f->unit;
        cf.flashPos[3] = 80.f * f->unit * (0.35f + 0.65f * flashLight);
        const float c[3] = {1.f, 192.f / 255.f, 64.f / 255.f};
        for (int k = 0; k < 3; ++k) cf.flashCol[k] = c[k] * 2.5f * s.flashLight * flashLight;
        cf.flashCol[3] = 1.f;
    }
    const float mat[3][4] = {{0.95f, 0.70f, 0.32f, 0.30f}, {0.95f, 0.70f, 0.32f, 0.30f}, {0.42f, 0.42f, 0.44f, 0.45f}};
    D3D11_MAPPED_SUBRESOURCE mp;
    StateBackup bk;
    bk.Save(ctx);
    D3D11_VIEWPORT vp = {0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs_);
    ctx->OMSetRenderTargets(1, &rtv, depth);
    const float bf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(bs_, bf, 0xffffffff);
    ctx->OMSetDepthStencilState(depth ? dss_ : dssNoDepth_, 0);
    UINT stride = sizeof(ShellVtx), off = 0;
    ctx->IASetVertexBuffers(0, 1, &vb_, &stride, &off);
    ctx->IASetIndexBuffer(ib_, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetInputLayout(layout_);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_, nullptr, 0);
    ctx->PSSetShader(ps_, nullptr, 0);
    ID3D11Buffer* vcb[2] = {cbFrame_, cbObj_};
    ctx->VSSetConstantBuffers(0, 2, vcb);
    ctx->PSSetConstantBuffers(0, 1, &cbFrame_);
    ID3D11ShaderResourceView* sv = cf.scene[3] > 0.5f ? scene : nullptr;
    ctx->PSSetShaderResources(3, 1, &sv);
    ctx->PSSetSamplers(0, 1, &samp_);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    for (int k = 0; k < 3; ++k) {
        if (!ndK[k] || !kindCount_[k]) continue;
        memcpy(cf.brass, mat[k], sizeof cf.brass);
        if (FAILED(ctx->Map(cbFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) break;
        memcpy(mp.pData, &cf, sizeof cf);
        ctx->Unmap(cbFrame_, 0);
        if (FAILED(ctx->Map(cbObj_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) break;
        memcpy(mp.pData, rowsK[k], sizeof(float) * 12 * ndK[k]);
        ctx->Unmap(cbObj_, 0);
        ctx->DrawIndexedInstanced(kindCount_[k], static_cast<UINT>(ndK[k]), kindFirst_[k], static_cast<INT>(kindIdx_[k]), 0);
    }
    bk.Restore(ctx);
    return true;
}
void ShellEject::Release() {
    SafeRelease(vs_);
    SafeRelease(ps_);
    SafeRelease(layout_);
    SafeRelease(vb_);
    SafeRelease(ib_);
    SafeRelease(cbFrame_);
    SafeRelease(cbObj_);
    SafeRelease(rs_);
    SafeRelease(dss_);
    SafeRelease(dssNoDepth_);
    SafeRelease(bs_);
    SafeRelease(samp_);
    indexCount_ = 0;
    n_ = pending_ = 0;
    nLand_ = 0;
}
namespace {
const char kShieldShader[] = R"HLSL(cbuffer CShieldFrame : register(b0) {
    row_major float4x4 uProj;
    float4 uLightDir;
    float4 uLightColor;
    float4 uAmbSky;
    float4 uAmbGround;
    float4 uParams;
    float4 uScene;
};
cbuffer CShieldObj : register(b1) { float4 uRows[256]; };
Texture2D tBase : register(t0);
Texture2D tNrm : register(t1);
Texture2D tMask : register(t2);
Texture2D tScene : register(t3);
SamplerState sLinear : register(s0);
struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; float3 col : COLOR; float2 mat : TEXCOORD0; float2 uv : TEXCOORD1; float4 tan : TANGENT; };
struct PSIn { float4 pos : SV_Position; float3 vpos : TEXCOORD1; float3 n : TEXCOORD2; float3 col : COLOR; float2 mat : TEXCOORD0;
              float4 fx : TEXCOORD3; float2 uv : TEXCOORD4; float4 t : TEXCOORD5; };
PSIn VS(VSIn i, uint id : SV_InstanceID) {
    float3x4 m = float3x4(uRows[id * 4], uRows[id * 4 + 1], uRows[id * 4 + 2]);
    PSIn o;
    o.vpos = mul(m, float4(i.pos, 1));
    o.pos = mul(uProj, float4(o.vpos, 1));
    o.n = mul((float3x3)m, i.nrm);
    o.col = i.col;
    o.mat = i.mat;
    o.fx = uRows[id * 4 + 3];
    o.uv = i.uv;
    o.t = float4(mul((float3x3)m, i.tan.xyz), i.tan.w);
    return o;
}
float3 SrgbEncode(float3 c) {
    c = saturate(c);
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return float3(c.x <= 0.0031308 ? lo.x : hi.x, c.y <= 0.0031308 ? lo.y : hi.y, c.z <= 0.0031308 ? lo.z : hi.z);
}
float3 SrgbDecode(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return float3(c.x <= 0.04045 ? lo.x : hi.x, c.y <= 0.04045 ? lo.y : hi.y, c.z <= 0.04045 ? lo.z : hi.z);
}
float4 PS(PSIn i) : SV_Target {
    if (i.fx.x > 0) {
        float3 e = i.col * i.fx.x;
        if (uParams.y > 0.5) e = SrgbEncode(e);
        return float4(e, 1);
    }
    float3 alb = i.col * i.fx.y;
    float metal = saturate(i.mat.x), rough = clamp(i.mat.y, 0.04, 1);
    float3 N = normalize(i.n);
    float3 V = normalize(-i.vpos);
    if (dot(N, V) < 0) N = -N;
    if (i.fx.z > 0.5) {
        alb = tBase.Sample(sLinear, i.uv).rgb * i.fx.y;
        float2 xy = tNrm.Sample(sLinear, i.uv).rg * 2 - 1;
        float z = sqrt(saturate(1 - dot(xy, xy)));
        float3 T = i.t.xyz - N * dot(N, i.t.xyz);
        float tl = length(T);
        if (tl > 1e-5) {
            T /= tl;
            float3 B = cross(N, T) * (i.t.w >= 0 ? 1 : -1);
            N = normalize(T * xy.x + B * xy.y + N * z);
        }
        float4 mk = tMask.Sample(sLinear, i.uv);
        metal = saturate(mk.r * i.fx.w);
        rough = clamp(mk.g, 0.04, 1);
    }
    float3 ambSky = uAmbSky.rgb, ambGround = uAmbGround.rgb, lightCol = uLightColor.rgb;
    if (uScene.w > 0.5) {
        float3 sTop = tScene.SampleLevel(sLinear, float2(0.5, 0.25), uScene.y).rgb;
        float3 sBot = tScene.SampleLevel(sLinear, float2(0.5, 0.75), uScene.y).rgb;
        if (uScene.z > 0.5) { sTop = SrgbDecode(saturate(sTop)); sBot = SrgbDecode(saturate(sBot)); }
        ambSky = lerp(ambSky, sTop * 1.3, uScene.x);
        ambGround = lerp(ambGround, sBot * 1.3, uScene.x);
        float3 avg = (sTop + sBot) * 0.5;
        float lum = dot(avg, float3(0.2126, 0.7152, 0.0722));
        float3 tint = lum > 1e-4 ? clamp(avg / lum, 0, 3) : float3(1, 1, 1);
        lightCol *= lerp(float3(1, 1, 1), tint, uScene.x * 0.6) * lerp(1.0, saturate(lum * 4.0), uScene.x * 0.6);
    }
    float3 L = normalize(uLightDir.xyz);
    float3 H = normalize(L + V);
    float NdL = saturate(dot(N, L)), NdV = saturate(dot(N, V)) + 1e-4, NdH = saturate(dot(N, H)), VdH = saturate(dot(V, H));
    float a = rough * rough, a2 = a * a;
    float d = NdH * NdH * (a2 - 1) + 1;
    float D = a2 / (3.14159265 * d * d);
    float k = (rough + 1) * (rough + 1) / 8;
    float G = (NdL / (NdL * (1 - k) + k)) * (NdV / (NdV * (1 - k) + k));
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), alb, metal);
    float3 F = F0 + (1 - F0) * pow(1 - VdH, 5);
    float3 spec = D * G * F / (4 * NdL * NdV + 1e-4);
    float3 diff = (1 - F) * (1 - metal) * alb / 3.14159265;
    float3 direct = (diff + spec) * lightCol * NdL;
    float hemi = N.y * 0.5 + 0.5;
    float3 amb = lerp(ambGround, ambSky, hemi);
    float3 Fa = F0 + (max(float3(1 - rough, 1 - rough, 1 - rough), F0) - F0) * pow(1 - NdV, 5);
    float3 ambient = amb * (alb * (1 - metal) * (1 - Fa)) + lerp(ambGround, ambSky, saturate(reflect(-V, N).y * 0.5 + 0.5)) * Fa * (1 - rough * 0.7);
    float3 c = (direct + ambient) * uParams.x;
    if (uParams.y > 0.5) c = SrgbEncode(c);
    return float4(c, 1);
}
)HLSL";
struct ShieldCbFrame {
    float proj[16];
    float lightDir[4], lightColor[4], ambSky[4], ambGround[4], params[4], scene[4];
};
static_assert(sizeof(ShieldCbFrame) == 160, "shield cb");
struct ShieldVtx {
    float p[3], n[3], c[3], m[2];
    float uv[2], t[4];
};
static_assert(sizeof(ShieldVtx) == 68, "shield vtx");
constexpr int kShVertCap = 16000, kShIdxCap = 80000;
struct SMesh {
    ShieldVtx* v;
    uint32_t* ix;
    int nv, ni, base;
    bool over;
};
uint32_t SAdd(SMesh& m, const float* p, const float* n, const float* c, float metal, float rough) {
    if (m.nv >= kShVertCap) {
        m.over = true;
        return 0;
    }
    ShieldVtx& v = m.v[m.nv];
    float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (int k = 0; k < 3; ++k) v.p[k] = p[k], v.n[k] = nl > 1e-9f ? n[k] / nl : (k == 2 ? 1.f : 0.f), v.c[k] = c[k];
    v.m[0] = metal, v.m[1] = rough;
    v.uv[0] = v.uv[1] = 0.f;
    v.t[0] = v.t[1] = v.t[2] = v.t[3] = 0.f;
    return static_cast<uint32_t>(m.nv++ - m.base);
}
void STri(SMesh& m, uint32_t a, uint32_t b, uint32_t c) {
    if (m.ni + 3 > kShIdxCap) {
        m.over = true;
        return;
    }
    m.ix[m.ni++] = a, m.ix[m.ni++] = b, m.ix[m.ni++] = c;
}
void SQuad(SMesh& m, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    STri(m, a, b, c);
    STri(m, a, c, d);
}
void SBox(SMesh& m, const float* lo, const float* hi, const float* c, float metal, float rough) {
    for (int ax = 0; ax < 3; ++ax)
        for (int sd = 0; sd < 2; ++sd) {
            int u = (ax + 1) % 3, w = (ax + 2) % 3;
            float n[3] = {0, 0, 0};
            n[ax] = sd ? 1.f : -1.f;
            uint32_t id[4];
            for (int k = 0; k < 4; ++k) {
                float p[3];
                p[ax] = sd ? hi[ax] : lo[ax];
                p[u] = (k == 1 || k == 2) ? hi[u] : lo[u];
                p[w] = (k >= 2) ? hi[w] : lo[w];
                id[k] = SAdd(m, p, n, c, metal, rough);
            }
            SQuad(m, id[0], id[1], id[2], id[3]);
        }
}
unsigned ShRand(unsigned& r) {
    r = r * 1664525u + 1013904223u;
    return r >> 8;
}
float ShRandF(unsigned& r, float a, float b) { return a + (b - a) * static_cast<float>(ShRand(r) & 0xffffff) / 16777216.f; }
void LinColor(const float* srgb, float* lin) {
    for (int k = 0; k < 3; ++k) lin[k] = SrgbToLinear(srgb[k]);
}
struct ShieldGeo {
    float W, H, T = 0.022f, R = 0.85f, rc;
    float Curve(float x) const {
        float r2 = R * R - x * x;
        return -(R - std::sqrt(r2 > 0.f ? r2 : 0.f));
    }
    float BackZ(float x, float y) const {
        (void)y;
        return Curve(x) - T;
    }
    float FrontZ(float x, float y) const {
        (void)y;
        return Curve(x);
    }
    void BackNormal(float x, float y, float* n) const {
        const float e = 0.002f;
        n[0] = (BackZ(x + e, y) - BackZ(x - e, y)) / (2.f * e);
        n[1] = (BackZ(x, y + e) - BackZ(x, y - e)) / (2.f * e);
        n[2] = -1.f;
    }
    void FrontNormal(float x, float y, float* n) const {
        const float e = 0.002f;
        n[0] = -(FrontZ(x + e, y) - FrontZ(x - e, y)) / (2.f * e);
        n[1] = -(FrontZ(x, y + e) - FrontZ(x, y - e)) / (2.f * e);
        n[2] = 1.f;
    }
    void Outline(float t, float* p, float* n) const {
        float hw = W * 0.5f - rc, hh = H * 0.5f - rc, arc = 0.5f * 3.14159265f * rc;
        float seg[8] = {2.f * hw, arc, 2.f * hh, arc, 2.f * hw, arc, 2.f * hh, arc}, per = 0.f;
        for (float sgl : seg) per += sgl;
        float d = (t - std::floor(t)) * per;
        int i = 0;
        while (i < 7 && d > seg[i]) d -= seg[i++];
        float f = seg[i] > 1e-9f ? d / seg[i] : 0.f;
        const float cx[4] = {hw, hw, -hw, -hw}, cy[4] = {-hh, hh, hh, -hh};
        if (i % 2 == 0) {
            int e = i / 2;
            if (e == 0) p[0] = -hw + 2.f * hw * f, p[1] = -H * 0.5f, n[0] = 0, n[1] = -1;
            if (e == 1) p[0] = W * 0.5f, p[1] = -hh + 2.f * hh * f, n[0] = 1, n[1] = 0;
            if (e == 2) p[0] = hw - 2.f * hw * f, p[1] = H * 0.5f, n[0] = 0, n[1] = 1;
            if (e == 3) p[0] = -W * 0.5f, p[1] = hh - 2.f * hh * f, n[0] = -1, n[1] = 0;
        } else {
            int c = i / 2;
            float a = (-90.f + 90.f * c + 90.f * f) * kDeg;
            n[0] = std::cos(a), n[1] = std::sin(a);
            p[0] = cx[c] + rc * n[0], p[1] = cy[c] + rc * n[1];
        }
    }
};
void BuildShieldBoard(SMesh& m, const ShieldGeo& g, const float* paintLin, const float* grip) {
    const int M = 64;
    const float rings[] = {0.f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.68f, 0.76f, 0.785f, 0.80f, 0.86f, 0.92f, 0.935f, 0.95f, 0.975f, 1.f};
    const int NR = sizeof rings / sizeof *rings;
    const float pad[3] = {0.075f, 0.080f, 0.074f}, rubber[3] = {0.022f, 0.022f, 0.022f};
    float frontPaint[3], ol[M][2], on[M][2];
    for (int k = 0; k < 3; ++k) frontPaint[k] = paintLin[k] * 0.8f;
    for (int j = 0; j < M; ++j) g.Outline(static_cast<float>(j) / M, ol[j], on[j]);
    const float wy = g.H * 0.27f, wx = g.W * 0.21f, wh = g.H * 0.055f, fr = 0.013f;
    for (int face = 0; face < 2; ++face) {
        uint32_t idx[NR][M];
        for (int r = 0; r < NR; ++r) {
            float s = rings[r];
            for (int j = 0; j < M; ++j) {
                float x = ol[j][0] * s, y = ol[j][1] * s, p[3] = {x, y, 0.f}, n[3], c[3];
                float metal = 0.f, rough = 0.9f;
                if (face == 0) {
                    p[2] = g.BackZ(x, y) - (s >= 0.935f ? 0.004f : 0.f);
                    g.BackNormal(x, y, n);
                    if (s < 0.79f) {
                        float k = 0.78f + 0.32f * (1.f - s * s);
                        for (int q = 0; q < 3; ++q) c[q] = pad[q] * k;
                        metal = 0.f, rough = 0.95f;
                    } else if (s < 0.93f) {
                        memcpy(c, paintLin, sizeof c), metal = 0.45f, rough = 0.45f;
                    } else {
                        memcpy(c, rubber, sizeof c), metal = 0.f, rough = 0.8f;
                    }
                } else {
                    p[2] = g.FrontZ(x, y);
                    g.FrontNormal(x, y, n);
                    memcpy(c, s < 0.95f ? frontPaint : rubber, sizeof c), metal = s < 0.95f ? 0.35f : 0.f, rough = s < 0.95f ? 0.5f : 0.8f;
                }
                idx[r][j] = SAdd(m, p, n, c, metal, rough);
            }
        }
        for (int r = 0; r + 1 < NR; ++r)
            for (int j = 0; j < M; ++j) SQuad(m, idx[r][j], idx[r][(j + 1) % M], idx[r + 1][(j + 1) % M], idx[r + 1][j]);
    }
    {
        uint32_t a[M], b[M];
        for (int j = 0; j < M; ++j) {
            float x = ol[j][0], y = ol[j][1], n[3] = {on[j][0], on[j][1], 0.f};
            float pb[3] = {x, y, g.BackZ(x, y) - 0.004f}, pf[3] = {x, y, g.FrontZ(x, y)};
            a[j] = SAdd(m, pb, n, rubber, 0.f, 0.8f);
            b[j] = SAdd(m, pf, n, rubber, 0.f, 0.8f);
        }
        for (int j = 0; j < M; ++j) SQuad(m, a[j], a[(j + 1) % M], b[(j + 1) % M], b[j]);
    }
    {
        const int GX = 12, GY = 3;
        const float glass[3] = {0.012f, 0.016f, 0.020f};
        uint32_t gi[GY + 1][GX + 1];
        for (int iy = 0; iy <= GY; ++iy)
            for (int ix = 0; ix <= GX; ++ix) {
                float x = -wx + 2.f * wx * ix / GX, y = wy - wh + 2.f * wh * iy / GY, n[3];
                float p[3] = {x, y, g.BackZ(x, y) - 0.0015f};
                g.BackNormal(x, y, n);
                gi[iy][ix] = SAdd(m, p, n, glass, 0.f, 0.06f);
            }
        for (int iy = 0; iy < GY; ++iy)
            for (int ix = 0; ix < GX; ++ix) SQuad(m, gi[iy][ix], gi[iy][ix + 1], gi[iy + 1][ix + 1], gi[iy + 1][ix]);
        auto band = [&](float x0, float y0, float x1, float y1, int nseg) {
            uint32_t pa = 0, pb = 0;
            bool horiz = std::fabs(y1 - y0) < std::fabs(x1 - x0) * 0.5f || nseg > 1;
            (void)horiz;
            for (int i = 0; i <= nseg; ++i) {
                float t = static_cast<float>(i) / nseg, x = x0 + (x1 - x0) * t, n[3];
                float pA[3] = {x, y0, 0.f}, pB[3] = {x, y1, 0.f};
                pA[2] = g.BackZ(pA[0], pA[1]) - 0.005f, pB[2] = g.BackZ(pB[0], pB[1]) - 0.005f;
                g.BackNormal(x, (y0 + y1) * 0.5f, n);
                uint32_t ia = SAdd(m, pA, n, rubber, 0.f, 0.75f), ib = SAdd(m, pB, n, rubber, 0.f, 0.75f);
                if (i) SQuad(m, pa, ia, ib, pb);
                pa = ia, pb = ib;
            }
        };
        band(-wx - fr, wy + wh, wx + fr, wy + wh + fr, 12);
        band(-wx - fr, wy - wh - fr, wx + fr, wy - wh, 12);
        band(-wx - fr, wy - wh, -wx, wy + wh, 1);
        band(wx, wy - wh, wx + fr, wy + wh, 1);
    }
    {
        const float strap[3] = {0.030f, 0.032f, 0.030f};
        float y0 = grip[1] - 0.095f, y1 = y0 + 0.036f, x0 = grip[0] - 0.16f, x1 = grip[0] + 0.02f;
        uint32_t pa = 0, pb = 0;
        for (int i = 0; i <= 8; ++i) {
            float x = x0 + (x1 - x0) * i / 8.f, n[3];
            g.BackNormal(x, (y0 + y1) * 0.5f, n);
            float a[3] = {x, y0, g.BackZ(x, y0) - 0.007f}, b[3] = {x, y1, g.BackZ(x, y1) - 0.007f};
            uint32_t ia = SAdd(m, a, n, strap, 0.f, 0.85f), ib = SAdd(m, b, n, strap, 0.f, 0.85f);
            if (i) SQuad(m, pa, ia, ib, pb);
            pa = ia, pb = ib;
        }
    }
    {
        const float poly[3] = {0.035f, 0.035f, 0.036f};
        float gx = grip[0] + 0.03f, gy = grip[1] + 0.005f, zb = g.BackZ(gx, gy);
        float lo[3] = {gx - 0.0125f, gy - 0.065f, zb - 0.051f}, hi[3] = {gx + 0.0125f, gy + 0.065f, zb - 0.029f};
        SBox(m, lo, hi, poly, 0.f, 0.6f);
        for (int k = -1; k <= 1; k += 2) {
            float y = gy + 0.055f * k, z0 = g.BackZ(gx, y);
            float l2[3] = {gx - 0.009f, y - 0.009f, z0 - 0.030f}, h2[3] = {gx + 0.009f, y + 0.009f, z0 + 0.001f};
            SBox(m, l2, h2, poly, 0.f, 0.6f);
        }
    }
}
}
float Shield::Rand(float a, float b) { return ShRandF(rng_, a, b); }
bool Shield::Load(ID3D11Device* dev, const Settings& s, char* err, size_t errLen) {
    Release();
    auto fail = [&](const char* msg) {
        if (err && errLen) snprintf(err, errLen, "shield: %s", msg);
        Release();
        return false;
    };
    if (!dev) return fail("no device");
    FnD3DCompile compile = GetCompiler();
    if (!compile) return fail("d3dcompiler_47 missing");
    ID3DBlob *vsb = nullptr, *psb = nullptr, *msg = nullptr;
    bool ok = SUCCEEDED(compile(kShieldShader, sizeof kShieldShader - 1, "tenzin_fps_shield", nullptr, nullptr, "VS", "vs_5_0",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(compile(kShieldShader, sizeof kShieldShader - 1, "tenzin_fps_shield", nullptr, nullptr, "PS", "ps_5_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) &&
         SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_));
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = ok && SUCCEEDED(dev->CreateInputLayout(il, 6, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_));
    SafeRelease(vsb);
    SafeRelease(psb);
    if (!ok) return fail("shader / layout");
    static ShieldVtx verts[kShVertCap];
    static uint32_t idx[kShIdxCap];
    SMesh m = {verts, idx, 0, 0, 0, false};
    float paint[3];
    LinColor(s.shieldColor, paint);
    if (err && errLen) err[0] = 0;
    auto loadModel = [&](const wchar_t* path) -> bool {
        unsigned char* data = nullptr;
        size_t len = 0;
        auto bad = [&](const char* why) {
            if (err && errLen) snprintf(err, errLen, "shield model %ls: %s (using the built-in shield)", path, why);
            delete[] data;
            for (int t = 0; t < kMaxTex; ++t) SafeRelease(srv_[t]);
            nSub_ = nTex_ = nOutline_ = modelTris_ = 0;
            m.nv = m.ni = 0;
            return false;
        };
        if (!ReadFileAll(path, &data, &len)) return bad("file missing");
        uint32_t h[7];
        if (len < sizeof h) return bad("truncated header");
        memcpy(h, data, sizeof h);
        if (memcmp(data, "HSM1", 4) || h[1] != 1) return bad("not an HSM1 v1 file");
        const uint32_t nv = h[2], ni = h[3], nsub = h[4], nol = h[5], ntex = h[6];
        if (!nv || !ni || ni % 3 || !nsub || nsub > kMaxSub || nol > kMaxOutline || !ntex || ntex > kMaxTex || nv + 64 > kShVertCap ||
            ni + 256 > kShIdxCap)
            return bad("counts out of range");
        size_t p = sizeof h;
        wchar_t dir[520];
        wcsncpy(dir, path, 519);
        dir[519] = 0;
        if (wchar_t* sl = wcsrchr(dir, L'\\')) *sl = 0;
        for (uint32_t t = 0; t < ntex; ++t) {
            if (p + 2 > len) return bad("truncated texture table");
            uint16_t n = static_cast<uint16_t>(data[p] | (data[p + 1] << 8));
            p += 2;
            if (!n || n > 400 || p + n + 1 > len) return bad("bad texture name");
            wchar_t rel[512], full[1040];
            int wn = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data + p), n, rel, 511);
            rel[wn > 0 ? wn : 0] = 0;
            for (wchar_t* c = rel; *c; ++c)
                if (*c == L'/') *c = L'\\';
            bool srgb = data[p + n] != 0;
            p += n + 1;
            swprintf(full, 1040, L"%ls\\%ls", dir, rel);
            char terr[512] = {};
            if (!LoadDds(dev, full, srgb, &srv_[t], terr, sizeof terr)) return bad(terr);
            nTex_ = static_cast<int>(t) + 1;
        }
        if (p + nsub * 20 + static_cast<size_t>(nv) * 48 + static_cast<size_t>(ni) * 4 + nol * 12 > len) return bad("truncated body");
        for (uint32_t k = 0; k < nsub; ++k) {
            int32_t q[5];
            memcpy(q, data + p, sizeof q);
            p += sizeof q;
            if (q[0] < 0 || q[1] <= 0 || static_cast<uint32_t>(q[0] + q[1]) > ni) return bad("bad submesh range");
            for (int t = 0; t < 3; ++t)
                if (q[2 + t] < 0 || q[2 + t] >= static_cast<int32_t>(ntex)) return bad("bad submesh texture");
            sub_[k] = {static_cast<UINT>(q[0]), static_cast<UINT>(q[1]), {q[2], q[3], q[4]}};
        }
        const float* f = reinterpret_cast<const float*>(data + p);
        for (uint32_t i = 0; i < nv; ++i, f += 12) {
            ShieldVtx& v = m.v[m.nv++];
            for (int k = 0; k < 3; ++k) v.p[k] = f[k], v.n[k] = f[3 + k], v.c[k] = 1.f;
            v.m[0] = 0.f, v.m[1] = 0.5f;
            for (int k = 0; k < 4; ++k) v.t[k] = f[6 + k];
            v.uv[0] = f[10], v.uv[1] = f[11];
        }
        p += static_cast<size_t>(nv) * 48;
        for (uint32_t i = 0; i < ni; ++i) {
            uint32_t x;
            memcpy(&x, data + p + i * 4, 4);
            if (x >= nv) return bad("index out of range");
            m.ix[m.ni++] = x;
        }
        p += static_cast<size_t>(ni) * 4;
        memcpy(outline_, data + p, nol * 12);
        nOutline_ = static_cast<int>(nol);
        nSub_ = static_cast<int>(nsub);
        modelTris_ = static_cast<int>(ni / 3);
        delete[] data;
        return true;
    };
    for (int k = 0; k < kKinds; ++k) {
        m.base = m.nv;
        kindBase_[k] = static_cast<UINT>(m.nv);
        kindFirst_[k] = static_cast<UINT>(m.ni);
        if (k == 0) {
            if (!s.shieldModel[0] || !loadModel(s.shieldModel)) {
                ShieldGeo g;
                g.W = s.shieldSize[0], g.H = s.shieldSize[1];
                g.rc = (g.W < g.H ? g.W : g.H) * 0.16f;
                BuildShieldBoard(m, g, paint, s.shieldGrip);
            }
        } else {
            const float c[3] = {1.f, 0.42f, 0.10f}, lo[3] = {-0.017f, -0.0008f, -0.0008f}, hi[3] = {0.017f, 0.0008f, 0.0008f};
            SBox(m, lo, hi, c, 0.f, 1.f);
        }
        kindCount_[k] = static_cast<UINT>(m.ni) - kindFirst_[k];
    }
    if (m.over) return fail("mesh capacity");
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = sizeof(ShieldVtx) * static_cast<UINT>(m.nv);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {verts, 0, 0};
    ok = SUCCEEDED(dev->CreateBuffer(&bd, &sd, &vb_));
    bd.ByteWidth = sizeof(uint32_t) * static_cast<UINT>(m.ni);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = idx;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, &sd, &ib_));
    D3D11_BUFFER_DESC cd = {};
    cd.Usage = D3D11_USAGE_DYNAMIC;
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cd.ByteWidth = sizeof(ShieldCbFrame);
    ok = ok && SUCCEEDED(dev->CreateBuffer(&cd, nullptr, &cbFrame_));
    cd.ByteWidth = sizeof(float) * 4 * 256;
    ok = ok && SUCCEEDED(dev->CreateBuffer(&cd, nullptr, &cbObj_));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(dev->CreateRasterizerState(&rd, &rs_));
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ok = ok && SUCCEEDED(dev->CreateDepthStencilState(&dd, &dss_));
    dd.DepthEnable = FALSE;
    ok = ok && SUCCEEDED(dev->CreateDepthStencilState(&dd, &dssNoDepth_));
    D3D11_BLEND_DESC bld = {};
    bld.RenderTarget[0].BlendEnable = FALSE;
    bld.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    ok = ok && SUCCEEDED(dev->CreateBlendState(&bld, &bs_));
    D3D11_SAMPLER_DESC sm = {};
    sm.Filter = D3D11_FILTER_ANISOTROPIC;
    sm.MaxAnisotropy = 8;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(dev->CreateSamplerState(&sm, &samp_));
    if (!ok) return fail("buffers / states");
    builtSize_[0] = s.shieldSize[0], builtSize_[1] = s.shieldSize[1];
    for (int k = 0; k < 3; ++k) builtGrip_[k] = s.shieldGrip[k], builtColor_[k] = s.shieldColor[k];
    wcsncpy(builtModel_, s.shieldModel, 519);
    builtModel_[519] = 0;
    n_ = hitPending_ = 0;
    tSim_ = -1.0;
    return true;
}
bool Shield::Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* depth, UINT w, UINT h, bool rtvEncodesSrgb,
                  double now, int look, const float* m34, float xs, float ys, const WorldCam* cam, const Settings& s,
                  ID3D11ShaderResourceView* scene, bool sceneGamma) {
    if (!vs_ || !ctx || !rtv || !w || !h || !(xs > 0.f) || !(ys > 0.f)) return false;
    float dt = tSim_ >= 0.0 ? static_cast<float>(now - tSim_) : 0.f;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;
    tSim_ = now;
    float R[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, O[3] = {0, 0, 0};
    if (m34) {
        for (int c = 0; c < 3; ++c) {
            float l = std::sqrt(m34[c] * m34[c] + m34[4 + c] * m34[4 + c] + m34[8 + c] * m34[8 + c]);
            for (int r = 0; r < 3; ++r) R[r * 3 + c] = l > 1e-8f ? m34[r * 4 + c] / l : (r == c ? 1.f : 0.f);
        }
        O[0] = m34[3], O[1] = m34[7], O[2] = m34[11];
    }
    auto loc = [&](const float* l, float* v) {
        for (int r = 0; r < 3; ++r) v[r] = R[r * 3] * l[0] + R[r * 3 + 1] * l[1] + R[r * 3 + 2] * l[2] + O[r];
    };
    auto locDir = [&](const float* l, float* v) {
        for (int r = 0; r < 3; ++r) v[r] = R[r * 3] * l[0] + R[r * 3 + 1] * l[1] + R[r * 3 + 2] * l[2];
    };
    auto toWorldV = [&](const float* v, float* o) {
        for (int c = 0; c < 3; ++c) o[c] = cam->right[c] * v[0] + cam->up[c] * v[1] + cam->fwd[c] * v[2];
    };
    auto spawn = [&](int kind, const float* pv, const float* vv, const float* rot9, const float* wv, float life, float grav) {
        if (n_ >= kMax) {
            memmove(pc_, pc_ + 1, sizeof(Piece) * (kMax - 1));
            --n_;
        }
        Piece& e = pc_[n_++];
        e.kind = static_cast<unsigned char>(kind);
        e.age = 0.f, e.life = life, e.g = grav;
        e.world = cam != nullptr;
        if (e.world) {
            float pw[3], m[9], cx[3], cy[3], cz[3], col[3][3];
            toWorldV(pv, pw);
            for (int c = 0; c < 3; ++c) pw[c] += cam->eye[c];
            toWorldV(vv, e.v);
            toWorldV(wv, e.w);
            for (int c = 0; c < 3; ++c) col[0][c] = rot9[c * 3], col[1][c] = rot9[c * 3 + 1], col[2][c] = rot9[c * 3 + 2];
            toWorldV(col[0], cx), toWorldV(col[1], cy), toWorldV(col[2], cz);
            for (int r = 0; r < 3; ++r) m[r * 3] = cx[r], m[r * 3 + 1] = cy[r], m[r * 3 + 2] = cz[r];
            MatToQuat(m, e.q);
            memcpy(e.p, pw, sizeof pw);
        } else {
            MatToQuat(rot9, e.q);
            memcpy(e.p, pv, sizeof e.p);
            memcpy(e.v, vv, sizeof e.v);
            memcpy(e.w, wv, sizeof e.w);
        }
    };
    const float W = s.shieldSize[0], H = s.shieldSize[1];
    float olc[2] = {0.f, 0.f};
    for (int k = 0; k < nOutline_; ++k) olc[0] += outline_[k][0] / nOutline_, olc[1] += outline_[k][1] / nOutline_;
    auto sparks = [&](int count, float speed) {
        ShieldGeo g;
        g.W = W, g.H = H, g.rc = (W < H ? W : H) * 0.16f;
        for (int i = 0; i < count; ++i) {
            float p2[2], n2[2], fz;
            if (HasModel() && nOutline_ >= 3) {
                float u = Rand(0.f, static_cast<float>(nOutline_));
                int a = static_cast<int>(u) % nOutline_, b = (a + 1) % nOutline_;
                float f = u - std::floor(u);
                for (int c = 0; c < 2; ++c) p2[c] = outline_[a][c] + (outline_[b][c] - outline_[a][c]) * f;
                fz = outline_[a][2] + (outline_[b][2] - outline_[a][2]) * f;
                n2[0] = p2[0] - olc[0], n2[1] = p2[1] - olc[1];
                float l = std::sqrt(n2[0] * n2[0] + n2[1] * n2[1]);
                n2[0] = l > 1e-6f ? n2[0] / l : 0.f, n2[1] = l > 1e-6f ? n2[1] / l : 1.f;
                p2[0] -= n2[0] * 0.01f, p2[1] -= n2[1] * 0.01f;
            } else {
                g.Outline(Rand(0.f, 1.f), p2, n2);
                fz = g.FrontZ(p2[0], p2[1]);
                p2[0] *= 0.97f, p2[1] *= 0.97f;
            }
            float lp[3] = {p2[0], p2[1], fz + 0.004f}, pv[3];
            float ld[3] = {n2[0] * Rand(0.6f, 1.f) + Rand(-0.3f, 0.3f), n2[1] * Rand(0.6f, 1.f) + Rand(-0.2f, 0.5f), Rand(0.3f, 1.2f)}, vv[3];
            loc(lp, pv);
            locDir(ld, vv);
            float sp = Rand(0.5f, 1.f) * speed;
            for (float& c : vv) c *= sp;
            const float I9[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, z3[3] = {0, 0, 0};
            spawn(1, pv, vv, I9, z3, Rand(0.12f, 0.30f), 0.5f);
        }
    };
    if (hitPending_ > 0 && m34) sparks(7 * hitPending_, 3.5f);
    hitPending_ = 0;
    int keep = 0;
    for (int i = 0; i < n_; ++i) {
        Piece& e = pc_[i];
        e.age += dt;
        if (e.age >= e.life) continue;
        if (dt > 0.f) {
            float drag = std::exp(-1.5f * dt);
            e.v[1] -= 9.81f * e.g * dt;
            for (int c = 0; c < 3; ++c) e.v[c] *= drag, e.p[c] += e.v[c] * dt;
            float wl = std::sqrt(e.w[0] * e.w[0] + e.w[1] * e.w[1] + e.w[2] * e.w[2]);
            if (wl > 1e-6f) {
                float ax[3] = {e.w[0] / wl, e.w[1] / wl, e.w[2] / wl}, dq[4];
                QuatAxis(ax, wl * dt, dq);
                QuatMul(dq, e.q, e.q);
                float l = std::sqrt(e.q[0] * e.q[0] + e.q[1] * e.q[1] + e.q[2] * e.q[2] + e.q[3] * e.q[3]);
                for (float& c : e.q) c /= l;
            }
        }
        pc_[keep++] = e;
    }
    n_ = keep;
    static float rows[kKinds][kMax * 16];
    int nk[kKinds] = {};
    if (look == 0 && m34) {
        float* o = rows[0];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) o[r * 4 + c] = m34[r * 4 + c];
        o[12] = 0.f, o[13] = 1.f, o[14] = HasModel() ? 1.f : 0.f, o[15] = s.shieldModelMetal;
        nk[0] = 1;
    }
    const float zn = 0.01f, zf = 50.f;
    for (int i = 0; i < n_; ++i) {
        const Piece& e = pc_[i];
        const int k = 1;
        if (nk[k] >= kMax) continue;
        float m[9], pv[3], mv[9], vview[3];
        QuatToMat(e.q, m);
        if (e.world) {
            if (!cam) continue;
            float d[3] = {e.p[0] - cam->eye[0], e.p[1] - cam->eye[1], e.p[2] - cam->eye[2]};
            const float* ax[3] = {cam->right, cam->up, cam->fwd};
            for (int r = 0; r < 3; ++r) {
                pv[r] = ax[r][0] * d[0] + ax[r][1] * d[1] + ax[r][2] * d[2];
                vview[r] = ax[r][0] * e.v[0] + ax[r][1] * e.v[1] + ax[r][2] * e.v[2];
                for (int c = 0; c < 3; ++c) mv[r * 3 + c] = ax[r][0] * m[c] + ax[r][1] * m[3 + c] + ax[r][2] * m[6 + c];
            }
        } else {
            memcpy(pv, e.p, sizeof pv), memcpy(mv, m, sizeof mv), memcpy(vview, e.v, sizeof vview);
        }
        if (pv[2] < zn + 0.005f || pv[2] > zf) continue;
        float lf = e.age / e.life, sc = 1.f, emis = 0.f;
        {
            float x[3] = {vview[0], vview[1], vview[2]}, up[3] = {0.f, 1.f, 0.f}, y[3], z[3];
            float sp = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
            if (sp < 1e-4f) x[0] = 1.f, x[1] = x[2] = 0.f;
            Norm3(x);
            if (std::fabs(x[1]) > 0.95f) up[0] = 1.f, up[1] = 0.f;
            Cross3(up, x, z);
            Norm3(z);
            Cross3(x, z, y);
            float len = 0.6f + 0.25f * sp;
            for (int r = 0; r < 3; ++r) mv[r * 3] = x[r] * len, mv[r * 3 + 1] = y[r], mv[r * 3 + 2] = z[r];
            emis = 2.4f * (1.f - lf * lf);
        }
        float* o = rows[k] + nk[k] * 16;
        for (int r = 0; r < 3; ++r)
            o[r * 4] = mv[r * 3] * sc, o[r * 4 + 1] = mv[r * 3 + 1] * sc, o[r * 4 + 2] = mv[r * 3 + 2] * sc, o[r * 4 + 3] = pv[r];
        o[12] = emis, o[13] = 1.f, o[14] = o[15] = 0.f;
        ++nk[k];
    }
    int total = 0;
    for (int k = 0; k < kKinds; ++k) total += nk[k];
    if (!total) return false;
    ShieldCbFrame cf = {};
    const float P[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), -zn * zf / (zf - zn), 0, 0, 1, 0};
    memcpy(cf.proj, P, sizeof P);
    float ln = std::sqrt(s.lightDir[0] * s.lightDir[0] + s.lightDir[1] * s.lightDir[1] + s.lightDir[2] * s.lightDir[2]);
    if (ln < 1e-6f) ln = 1;
    for (int k = 0; k < 3; ++k) {
        cf.lightDir[k] = s.lightDir[k] / ln;
        cf.lightColor[k] = s.lightColor[k] * s.lightIntensity;
        cf.ambSky[k] = s.ambSky[k] * s.ambIntensity;
        cf.ambGround[k] = s.ambGround[k] * s.ambIntensity;
    }
    cf.params[0] = s.exposure;
    cf.params[1] = rtvEncodesSrgb ? 0.f : 1.f;
    if (scene && s.sceneLight > 0.001f) {
        ID3D11Resource* res = nullptr;
        scene->GetResource(&res);
        ID3D11Texture2D* tex = nullptr;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex) {
            D3D11_TEXTURE2D_DESC td;
            tex->GetDesc(&td);
            cf.scene[0] = s.sceneLight;
            cf.scene[1] = static_cast<float>(td.MipLevels >= 2 ? td.MipLevels - 2 : 0);
            cf.scene[2] = sceneGamma ? 1.f : 0.f;
            cf.scene[3] = 1.f;
        }
        SafeRelease(tex);
        SafeRelease(res);
    }
    D3D11_MAPPED_SUBRESOURCE mp;
    StateBackup bk;
    bk.Save(ctx);
    D3D11_VIEWPORT vp = {0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs_);
    ctx->OMSetRenderTargets(1, &rtv, depth);
    const float bf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(bs_, bf, 0xffffffff);
    ctx->OMSetDepthStencilState(depth ? dss_ : dssNoDepth_, 0);
    UINT stride = sizeof(ShieldVtx), off = 0;
    ctx->IASetVertexBuffers(0, 1, &vb_, &stride, &off);
    ctx->IASetIndexBuffer(ib_, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetInputLayout(layout_);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_, nullptr, 0);
    ctx->PSSetShader(ps_, nullptr, 0);
    ID3D11Buffer* vcb[2] = {cbFrame_, cbObj_};
    ctx->VSSetConstantBuffers(0, 2, vcb);
    ctx->PSSetConstantBuffers(0, 1, &cbFrame_);
    ID3D11ShaderResourceView* sv = cf.scene[3] > 0.5f ? scene : nullptr;
    ctx->PSSetShaderResources(3, 1, &sv);
    ctx->PSSetSamplers(0, 1, &samp_);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    if (SUCCEEDED(ctx->Map(cbFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) {
        memcpy(mp.pData, &cf, sizeof cf);
        ctx->Unmap(cbFrame_, 0);
    }
    for (int k = 0; k < kKinds; ++k) {
        if (!nk[k] || !kindCount_[k]) continue;
        if (FAILED(ctx->Map(cbObj_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) break;
        memcpy(mp.pData, rows[k], sizeof(float) * 16 * nk[k]);
        ctx->Unmap(cbObj_, 0);
        if (k == 0 && HasModel()) {
            for (int q = 0; q < nSub_; ++q) {
                ID3D11ShaderResourceView* t3[3] = {srv_[sub_[q].tex[0]], srv_[sub_[q].tex[1]], srv_[sub_[q].tex[2]]};
                ctx->PSSetShaderResources(0, 3, t3);
                ctx->DrawIndexedInstanced(sub_[q].count, static_cast<UINT>(nk[k]), kindFirst_[k] + sub_[q].first, static_cast<INT>(kindBase_[k]), 0);
            }
            continue;
        }
        ctx->DrawIndexedInstanced(kindCount_[k], static_cast<UINT>(nk[k]), kindFirst_[k], static_cast<INT>(kindBase_[k]), 0);
    }
    bk.Restore(ctx);
    return true;
}
void Shield::Release() {
    SafeRelease(vs_);
    SafeRelease(ps_);
    SafeRelease(layout_);
    SafeRelease(vb_);
    SafeRelease(ib_);
    SafeRelease(cbFrame_);
    SafeRelease(cbObj_);
    SafeRelease(rs_);
    SafeRelease(dss_);
    SafeRelease(dssNoDepth_);
    SafeRelease(bs_);
    SafeRelease(samp_);
    for (ID3D11ShaderResourceView*& t : srv_) SafeRelease(t);
    nSub_ = nTex_ = nOutline_ = modelTris_ = 0;
    builtModel_[0] = 0;
    n_ = hitPending_ = 0;
    for (UINT& c : kindCount_) c = 0;
}
namespace prefetch {
ID3D11Device* DeviceRef() {
    AcquireSRWLockShared(&g_preLock);
    ID3D11Device* d = g_preDev;
    if (d) d->AddRef();
    ReleaseSRWLockShared(&g_preLock);
    return d;
}
void PutTexture(int tag, const wchar_t* path, unsigned char* data, size_t len, ULONGLONG size, FILETIME stamp, bool srgb) {
    ID3D11Device* d = DeviceRef();
    ID3D11ShaderResourceView* srv = nullptr;
    char err[8];
    if (d && DdsCreate(d, data, len, srgb, path, &srv, err, sizeof err)) {
        delete[] data;
        InterlockedIncrement(&g_preBuilt);
        PutPrefetched(tag, false, path, nullptr, 0, size, stamp, srgb, srv, d, true);
    } else {
        PutPrefetched(tag, false, path, data, len, size, stamp, srgb, nullptr, nullptr, d != nullptr);
    }
    if (d) d->Release();
}
bool SetDevice(ID3D11Device* dev) {
    const bool mt = dev && !(dev->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED);
    ID3D11ShaderResourceView* old[kMaxPre];
    int n = 0;
    AcquireSRWLockExclusive(&g_preLock);
    ID3D11Device* prev = g_preDev;
    g_preDev = mt ? dev : nullptr;
    if (g_preDev) g_preDev->AddRef();
    for (int i = 0; i < g_preN;) {
        if (g_pre[i].srv && g_pre[i].dev != g_preDev) {
            old[n++] = g_pre[i].srv;
            g_pre[i] = g_pre[--g_preN];
        } else {
            ++i;
        }
    }
    ReleaseSRWLockExclusive(&g_preLock);
    for (int k = 0; k < n; ++k) old[k]->Release();
    if (prev) prev->Release();
    return mt;
}
int Upgrade(int tag) {
    ID3D11Device* d = DeviceRef();
    if (!d) return 0;
    int n = 0;
    for (;;) {
        PreEntry e = {};
        bool got = false;
        AcquireSRWLockExclusive(&g_preLock);
        for (int i = 0; i < g_preN; ++i) {
            if (g_pre[i].tag != tag || g_pre[i].hvm || !g_pre[i].data || g_pre[i].bgTried) continue;
            e = g_pre[i];
            g_pre[i] = g_pre[--g_preN];
            got = true;
            break;
        }
        ReleaseSRWLockExclusive(&g_preLock);
        if (!got) break;
        ID3D11ShaderResourceView* srv = nullptr;
        char err[8];
        if (DdsCreate(d, e.data, e.len, e.srgb, e.path, &srv, err, sizeof err)) {
            delete[] e.data;
            InterlockedIncrement(&g_preBuilt);
            PutPrefetched(e.tag, false, e.path, nullptr, 0, e.size, e.stamp, e.srgb, srv, d, true);
            ++n;
        } else {
            PutPrefetched(e.tag, false, e.path, e.data, e.len, e.size, e.stamp, e.srgb, nullptr, nullptr, true);
        }
    }
    d->Release();
    return n;
}
bool ShieldModel(int tag, const wchar_t* hsmPath, int* files, size_t* bytes) {
    *files = 0;
    *bytes = 0;
    unsigned char* data = nullptr;
    size_t len = 0;
    if (!ReadFileAll(hsmPath, &data, &len)) return false;
    uint32_t h[7];
    bool ok = len >= sizeof h && !memcmp(data, "HSM1", 4);
    if (ok) memcpy(h, data, sizeof h);
    ok = ok && h[1] == 1 && h[6] >= 1 && h[6] <= 12;
    wchar_t dir[520];
    wcsncpy(dir, hsmPath, 519);
    dir[519] = 0;
    if (wchar_t* sl = wcsrchr(dir, L'\\')) *sl = 0;
    size_t p = sizeof h;
    for (uint32_t t = 0; ok && t < h[6]; ++t) {
        if (p + 2 > len) break;
        uint16_t n = static_cast<uint16_t>(data[p] | (data[p + 1] << 8));
        p += 2;
        if (!n || n > 400 || p + n + 1 > len) break;
        wchar_t rel[512], full[1040];
        int wn = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data + p), n, rel, 511);
        rel[wn > 0 ? wn : 0] = 0;
        for (wchar_t* c = rel; *c; ++c)
            if (*c == L'/') *c = L'\\';
        bool srgb = data[p + n] != 0;
        p += n + 1;
        swprintf(full, 1040, L"%ls\\%ls", dir, rel);
        ULONGLONG size = 0;
        FILETIME stamp = {};
        unsigned char* d = nullptr;
        size_t l = 0;
        if (!FileStamp(full, &size, &stamp) || !ReadFileAll(full, &d, &l)) continue;
        ++*files;
        *bytes += l;
        PutTexture(tag, full, d, l, size, stamp, srgb);
    }
    delete[] data;
    return ok;
}
void Counters(long* built, long* taken) {
    *built = g_preBuilt;
    *taken = g_preTaken;
}
bool Model(int tag, const wchar_t* hvmPath, int* files, size_t* bytes, char* err, size_t errLen) {
    *files = 0;
    *bytes = 0;
    ULONGLONG size = 0;
    FILETIME stamp = {};
    if (!FileStamp(hvmPath, &size, &stamp)) {
        SetErr(err, errLen, "model file missing: %ls", hvmPath);
        return false;
    }
    unsigned char* data = nullptr;
    size_t len = 0;
    if (!ReadHvmV1(hvmPath, &data, &len, err, errLen)) return false;
    HvmHeader h = {};
    constexpr uint32_t kTexCap = 64;
    HvmTexture tex[kTexCap];
    uint32_t texN = 0;
    if (len >= sizeof h) {
        memcpy(&h, data, sizeof h);
        if (!memcmp(h.magic, "HVM1", 4) && h.version == 1 && h.textureCount <= kTexCap &&
            sizeof h + h.textureCount * sizeof(HvmTexture) <= len)
            texN = h.textureCount;
    }
    memcpy(tex, data + sizeof h, texN * sizeof(HvmTexture));
    *files = 1;
    *bytes = len;
    PutPrefetched(tag, true, hvmPath, data, len, size, stamp);
    wchar_t dir[MAX_PATH];
    wcsncpy(dir, hvmPath, MAX_PATH - 1);
    dir[MAX_PATH - 1] = 0;
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) slash = wcsrchr(dir, L'/');
    if (slash) slash[1] = 0;
    for (uint32_t t = 0; t < texN; ++t) {
        tex[t].file[75] = 0;
        wchar_t rel[128];
        MultiByteToWideChar(CP_UTF8, 0, tex[t].file, -1, rel, 128);
        wchar_t full[MAX_PATH * 2];
        swprintf(full, MAX_PATH * 2, L"%ls%ls", dir, rel);
        unsigned char* d = nullptr;
        size_t l = 0;
        if (!FileStamp(full, &size, &stamp) || !ReadFileAll(full, &d, &l)) continue;
        ++*files;
        *bytes += l;
        PutTexture(tag, full, d, l, size, stamp, tex[t].srgb != 0);
    }
    return true;
}
void Drop(int tag) {
    unsigned char* del[kMaxPre];
    ID3D11ShaderResourceView* srv[kMaxPre];
    int n = 0;
    AcquireSRWLockExclusive(&g_preLock);
    for (int i = 0; i < g_preN;) {
        if (g_pre[i].tag != tag) {
            ++i;
            continue;
        }
        del[n] = g_pre[i].data;
        srv[n++] = g_pre[i].srv;
        g_pre[i] = g_pre[--g_preN];
    }
    ReleaseSRWLockExclusive(&g_preLock);
    for (int k = 0; k < n; ++k) {
        delete[] del[k];
        if (srv[k]) srv[k]->Release();
    }
}
int WarmShaders() {
    FnD3DCompile compile = GetCompiler();
    if (!compile) return 0;
    struct One {
        const char* src;
        size_t len;
        const char *name, *entry, *target;
    } const list[] = {{kShader, sizeof kShader - 1, "tenzin_fps_vm", "VS", "vs_5_0"},
                      {kShader, sizeof kShader - 1, "tenzin_fps_vm", "PS", "ps_5_0"},
                      {kFlashShader, sizeof kFlashShader - 1, "tenzin_fps_flash", "VS", "vs_5_0"},
                      {kFlashShader, sizeof kFlashShader - 1, "tenzin_fps_flash", "PS", "ps_5_0"},
                      {kFlashShader, sizeof kFlashShader - 1, "tenzin_fps_flash", "PSD", "ps_5_0"},
                      {kShellShader, sizeof kShellShader - 1, "tenzin_fps_shell", "VS", "vs_5_0"},
                      {kShellShader, sizeof kShellShader - 1, "tenzin_fps_shell", "PS", "ps_5_0"},
                      {kCrossShader, sizeof kCrossShader - 1, "tenzin_fps_cross", "VS", "vs_5_0"},
                      {kCrossShader, sizeof kCrossShader - 1, "tenzin_fps_cross", "PS", "ps_5_0"},
                      {kShieldShader, sizeof kShieldShader - 1, "tenzin_fps_shield", "VS", "vs_5_0"},
                      {kShieldShader, sizeof kShieldShader - 1, "tenzin_fps_shield", "PS", "ps_5_0"}};
    int ok = 0;
    for (const One& o : list) {
        ID3DBlob *b = nullptr, *m = nullptr;
        if (SUCCEEDED(compile(o.src, o.len, o.name, nullptr, nullptr, o.entry, o.target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &m))) ++ok;
        SafeRelease(b);
        SafeRelease(m);
    }
    return ok;
}
}
}
#include "cast_marker_draw.inc"
