#include "vm_render.h"
#include <d3dcompiler.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <cstring>
#include <cwchar>
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
bool LoadDds(ID3D11Device* dev, const wchar_t* path, bool srgb, ID3D11ShaderResourceView** srvOut, char* err,
             size_t errLen) {
    unsigned char* data = nullptr;
    size_t len = 0;
    if (!ReadFileAll(path, &data, &len)) {
        SetErr(err, errLen, "texture missing: %ls", path);
        return false;
    }
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
};
cbuffer CBones : register(b1) { float4 uBones[768]; };
cbuffer CMat : register(b2) {
    float4 uMat;
    float4 uMat2;
};
Texture2D tAlbedo : register(t0);
Texture2D tNormal : register(t1);
Texture2D tMaterial : register(t2);
Texture2D tScene : register(t3);
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
float4 PS(PSIn i, bool front : SV_IsFrontFace) : SV_Target {
    float4 alb = tAlbedo.Sample(sLinear, i.uv);
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
    float3 c = (direct + ambient) * uParams.x;
    if (uParams.y > 0.5) c = SrgbEncode(c);
    return float4(c, 1);
}
)HLSL";
typedef HRESULT(WINAPI* FnD3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                     UINT, UINT, ID3DBlob**, ID3DBlob**);
FnD3DCompile GetCompiler() {
    HMODULE m = GetModuleHandleW(L"d3dcompiler_47.dll");
    if (!m) m = LoadLibraryW(L"d3dcompiler_47.dll");
    return m ? reinterpret_cast<FnD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
}
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
    ID3D11ShaderResourceView* psSrv[4] = {};
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
        c->PSGetShaderResources(0, 4, psSrv);
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
        c->PSSetShaderResources(0, 4, psSrv);
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
};
static_assert(sizeof(CFrame) == 256, "cframe");
float IniFloat(const wchar_t* ini, const wchar_t* key, float def) {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"viewmodel", key, L"", buf, 64, ini);
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    return (end != buf && std::isfinite(v)) ? static_cast<float>(v) : def;
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
    float mb = IniFloat(ini, L"muzzle_bone", static_cast<float>(s.muzzleBone));
    s.muzzleBone = (mb >= -2 && mb < static_cast<float>(kMaxBones)) ? static_cast<int>(mb) : -2;
    IniVec3(ini, L"muzzle_local", s.muzzleLocal);
    GetPrivateProfileStringW(L"viewmodel", L"fire_sound", s.fireSound, s.fireSound, 512, ini);
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
    s.fireTailGap = IniFloat(ini, L"fire_tail_gap", s.fireTailGap);
    s.fireRpm = IniFloat(ini, L"fire_rpm", s.fireRpm);
    s.magSize = static_cast<int>(IniFloat(ini, L"mag_size", static_cast<float>(s.magSize)));
    s.reloadRefill = IniFloat(ini, L"reload_refill", s.reloadRefill);
    s.reloadEmptyRefill = IniFloat(ini, L"reload_empty_refill", s.reloadEmptyRefill);
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
    s.flashScale = IniFloat(ini, L"flash_scale", s.flashScale);
    s.flashIntensity = IniFloat(ini, L"flash_intensity", s.flashIntensity);
    s.flashLight = IniFloat(ini, L"flash_light", s.flashLight);
    s.flashSmoke = IniFloat(ini, L"flash_smoke", s.flashSmoke ? 1.f : 0.f) != 0.f;
    s.flashFlame = IniFloat(ini, L"flash_flame", s.flashFlame ? 1.f : 0.f) != 0.f;
    s.flashGas = IniFloat(ini, L"flash_gas", s.flashGas ? 1.f : 0.f) != 0.f;
    GetPrivateProfileStringW(L"viewmodel", L"flash_gas_fx", s.flashGasFx, s.flashGasFx, 128, ini);
    s.flashGasScale = IniFloat(ini, L"flash_gas_scale", s.flashGasScale);
    s.flashGasAlpha = IniFloat(ini, L"flash_gas_alpha", s.flashGasAlpha);
    if (!(s.fireVolume >= 0.f && s.fireVolume <= 2.f)) s.fireVolume = 0.7f;
    if (!(s.flashScale > 0.01f && s.flashScale <= 20.f)) s.flashScale = 1.f;
    if (!(s.flashIntensity >= 0.f && s.flashIntensity <= 10.f)) s.flashIntensity = 1.f;
    if (!(s.flashLight >= 0.f && s.flashLight <= 20.f)) s.flashLight = 1.f;
    if (!(s.fovDeg > 10 && s.fovDeg < 150)) s.fovDeg = 55;
    if (!(s.scale > 0.001f && s.scale < 1000)) s.scale = 1;
    return true;
}
bool Renderer::LoadHvm(const wchar_t* path, char* err, size_t errLen) {
    unsigned char* data = nullptr;
    size_t len = 0;
    if (!ReadFileAll(path, &data, &len)) {
        SetErr(err, errLen, "model file missing: %ls", path);
        return false;
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
            h.boneCount > kMaxBones || h.textureCount > 32 || h.clipCount > 512) {
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
        bool texOk = true;
        for (uint32_t t = 0; t < h.textureCount; ++t) {
            HvmTexture tx;
            memcpy(&tx, data + off, sizeof tx);
            off += sizeof tx;
            tx.file[75] = 0;
            wchar_t rel[128];
            MultiByteToWideChar(CP_UTF8, 0, tx.file, -1, rel, 128);
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
        }
        cameraBone_ = (h.cameraBone >= 0 && h.cameraBone < static_cast<int>(h.boneCount)) ? h.cameraBone : -1;
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
    HRESULT hr = compile(kShader, sizeof kShader - 1, "hero_fps_vm", nullptr, nullptr, "VS", "vs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &msg);
    if (FAILED(hr)) {
        SetErr(err, errLen, "VS compile 0x%08lx: %s", hr, msg ? static_cast<const char*>(msg->GetBufferPointer()) : "");
        SafeRelease(msg);
        return false;
    }
    SafeRelease(msg);
    hr = compile(kShader, sizeof kShader - 1, "hero_fps_vm", nullptr, nullptr, "PS", "ps_5_0",
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
    bd.ByteWidth = 32;
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
bool Renderer::EnsureDepth(UINT w, UINT h) {
    if (dsv_ && w == depthW_ && h == depthH_) return true;
    SafeRelease(dsv_);
    SafeRelease(depthTex_);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &depthTex_)) ||
        FAILED(dev_->CreateDepthStencilView(depthTex_, nullptr, &dsv_))) {
        SafeRelease(dsv_);
        SafeRelease(depthTex_);
        return false;
    }
    depthW_ = w;
    depthH_ = h;
    return true;
}
bool Renderer::EnsureScene(UINT w, UINT h, DXGI_FORMAT fmt) {
    if (sceneBroken_) return false;
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
void Renderer::EvalBones(const Look& look, float* palette, int muzzleBone, const float* muzzleLocal,
                         float* muzzleModel, const float* axisLocal, const float* upLocal, float* axisModel, float* upModel) {
    static Aff world[kMaxBones];
    bool fade = look.clipPrev >= 0 && look.blend < 0.999f;
    float wNew = look.blend < 0 ? 0.f : (look.blend > 1 ? 1.f : look.blend);
    for (unsigned b = 0; b < boneCount_; ++b) {
        float trs[10];
        SampleClip(look.clip, look.clipTime, b, trs);
        if (fade) {
            float old[10];
            SampleClip(look.clipPrev, look.clipTimePrev, b, old);
            float dot = old[3] * trs[3] + old[4] * trs[4] + old[5] * trs[5] + old[6] * trs[6];
            float sgn = dot < 0 ? -1.f : 1.f;
            for (int k = 0; k < 3; ++k) trs[k] = old[k] + (trs[k] - old[k]) * wNew;
            for (int k = 3; k < 7; ++k) trs[k] = old[k] + (trs[k] * sgn - old[k]) * wNew;
            for (int k = 7; k < 10; ++k) trs[k] = old[k] + (trs[k] - old[k]) * wNew;
        }
        Aff local = AffFromTrs(trs);
        world[b] = bones_[b].parent >= 0 ? AffMul(world[bones_[b].parent], local) : local;
    }
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
    if (!ready_ || !ctx || !rtv || !w || !h) return false;
    if (!EnsureDepth(w, h)) return false;
    static float palette[kMaxBones * 12 + 12];
    float muzzleModel[3] = {s.muzzleLocal[0], s.muzzleLocal[1], s.muzzleLocal[2]};
    float axisModel[3] = {s.muzzleAxis[0], s.muzzleAxis[1], s.muzzleAxis[2]};
    float upModel[3] = {s.muzzleUp[0], s.muzzleUp[1], s.muzzleUp[2]};
    EvalBones(look, palette, s.muzzleBone, s.muzzleLocal, muzzleModel, s.muzzleAxis, s.muzzleUp, axisModel, upModel);
    float bobX = std::sin(look.bobPhase) * 0.008f * look.bobAmount * s.bobScale;
    float bobY = -std::fabs(std::cos(look.bobPhase)) * 0.010f * look.bobAmount * s.bobScale;
    float breathe = std::sin(look.time * 1.7f) * 0.0018f;
    Aff sway = AffMul(AffRotY(look.swayX * s.swayScale), AffRotX(look.swayY * s.swayScale));
    Aff place = AffTrans(s.offset[0] + bobX, s.offset[1] + bobY + breathe, s.offset[2] - 0.05f * look.kick);
    Aff rot = AffMul(AffRotY(s.rotDeg[0] * kDeg), AffMul(AffRotX(s.rotDeg[1] * kDeg), AffRotZ(s.rotDeg[2] * kDeg)));
    sway = AffMul(sway, AffRotX(-5.0f * kDeg * look.kick));
    Aff scl = AffScale(s.mirrorX ? -s.scale : s.scale, s.scale, s.scale);
    Aff tilt = AffMul(AffRotY(s.tiltDeg[0] * kDeg), AffMul(AffRotX(s.tiltDeg[1] * kDeg), AffRotZ(s.tiltDeg[2] * kDeg)));
    Aff model = AffMul(sway, AffMul(place, AffMul(tilt, AffMul(rot, scl))));
    if (cameraBone_ >= 0) {
        Aff cam;
        memcpy(cam.m, palette + kMaxBones * 12, sizeof cam.m);
        model = AffMul(model, AffInverse(cam));
    }
    float det = AffDet(model);
    CFrame cf = {};
    float aspect = static_cast<float>(w) / static_cast<float>(h);
    float ys = 1.0f / std::tan(s.fovDeg * kDeg * 0.5f), xs = ys / aspect;
    const float zn = 0.01f, zf = 50.0f;
    float P[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), -zn * zf / (zf - zn), 0, 0, 1, 0};
    memcpy(cf.proj, P, sizeof P);
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
    StateBackup bk;
    bk.Save(ctx);
    bool haveScene = false;
    if (sceneTex && s.sceneLight > 0.001f) {
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
            ctx->CopySubresourceRegion(sceneTex_, 0, 0, 0, 0, sceneTex, 0, nullptr);
            ctx->GenerateMips(sceneSrv_);
            haveScene = true;
            cf.scene[0] = s.sceneLight;
            cf.scene[1] = static_cast<float>(sceneMips_ >= 2 ? sceneMips_ - 2 : 0);
            cf.scene[2] = (f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                           f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                              ? 0.f
                              : 1.f;
            cf.scene[3] = 1.f;
        }
    }
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
    for (unsigned i = 0; i < subCount_; ++i) {
        const Submesh& sm = subs_[i];
        bool hasDef = (sm.flags >> 17) & 1;
        float mat[8] = {static_cast<float>(sm.normalMode), sm.tex[1] >= 0 ? 1.f : 0.f, sm.tex[2] >= 0 ? 1.f : 0.f,
                        static_cast<float>(sm.materialMode),
                        hasDef ? (sm.flags & 0xff) / 255.f : 0.55f, hasDef ? ((sm.flags >> 8) & 0xff) / 255.f : 0.f,
                        ((sm.flags >> 18) & 1) ? 1.f : 0.f, ((sm.flags >> 16) & 1) ? 1.f : 0.f};
        if (SUCCEEDED(ctx->Map(cbMat_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mp))) {
            memcpy(mp.pData, mat, sizeof mat);
            ctx->Unmap(cbMat_, 0);
        }
        ID3D11ShaderResourceView* srv[3];
        for (int k = 0; k < 3; ++k) srv[k] = sm.tex[k] >= 0 ? srv_[sm.tex[k]] : white_;
        ctx->PSSetShaderResources(0, 3, srv);
        ctx->DrawIndexed(sm.indexCount, sm.firstIndex, 0);
    }
    bk.Restore(ctx);
    return true;
}
void Renderer::Release() {
    ready_ = false;
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
    SafeRelease(dsv_);
    SafeRelease(depthTex_);
    SafeRelease(white_);
    SafeRelease(sceneSrv_);
    SafeRelease(sceneTex_);
    sceneW_ = sceneH_ = sceneMips_ = 0;
    sceneFmt_ = DXGI_FORMAT_UNKNOWN;
    sceneBroken_ = false;
    for (auto*& s : srv_) SafeRelease(s);
    texCount_ = 0;
    delete[] subs_;
    subs_ = nullptr;
    subCount_ = 0;
    delete[] bones_;
    bones_ = nullptr;
    boneCount_ = 0;
    delete[] clips_;
    clips_ = nullptr;
    clipCount_ = 0;
    delete[] clipData_;
    clipData_ = nullptr;
    cameraBone_ = -1;
    muzzleValid_ = false;
    muzzleFrameValid_ = false;
    indexCount_ = 0;
    depthW_ = depthH_ = 0;
    SafeRelease(dev_);
}
namespace {
const char kCrossShader[] = R"HLSL(struct VI { float2 p : POSITION; float4 c : COLOR; };
struct VO { float4 p : SV_Position; float4 c : COLOR; };
VO VS(VI i) { VO o; o.p = float4(i.p, 0, 1); o.c = i.c; return o; }
float4 PS(VO i) : SV_Target { return i.c; }
)HLSL";
struct CrossVertex {
    float x, y;
    float r, g, b, a;
};
float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float IniCross(const wchar_t* ini, const wchar_t* key, float def) {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"crosshair", key, L"", buf, 64, ini);
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    return (end != buf && std::isfinite(v)) ? static_cast<float>(v) : def;
}
}
bool LoadCrosshair(const wchar_t* ini, CrosshairSettings& s) {
    s.enabled = IniCross(ini, L"enabled", s.enabled ? 1.f : 0.f) != 0.f;
    s.length = IniCross(ini, L"length", s.length);
    s.gap = IniCross(ini, L"gap", s.gap);
    s.thickness = IniCross(ini, L"thickness", s.thickness);
    s.outline = IniCross(ini, L"outline", s.outline);
    s.dot = IniCross(ini, L"dot", s.dot ? 1.f : 0.f) != 0.f;
    wchar_t buf[128];
    GetPrivateProfileStringW(L"crosshair", L"color", L"", buf, 128, ini);
    if (buf[0]) {
        float v[4] = {s.color[0] * 255.f, s.color[1] * 255.f, s.color[2] * 255.f, s.color[3] * 255.f};
        wchar_t* p = buf;
        for (int k = 0; k < 4 && *p; ++k) {
            wchar_t* end = nullptr;
            double d = wcstod(p, &end);
            if (end == p) break;
            v[k] = static_cast<float>(d);
            p = end;
            while (*p == L',' || *p == L' ') ++p;
        }
        for (int k = 0; k < 4; ++k) s.color[k] = std::fmin(std::fmax(v[k] / 255.f, 0.f), 1.f);
    }
    if (!(s.length >= 0.f && s.length <= 200.f)) s.length = 7.f;
    if (!(s.gap >= 0.f && s.gap <= 200.f)) s.gap = 4.f;
    if (!(s.thickness > 0.f && s.thickness <= 50.f)) s.thickness = 2.f;
    if (!(s.outline >= 0.f && s.outline <= 20.f)) s.outline = 1.f;
    return true;
}
namespace {
const char kFlashShader[] = R"(cbuffer C : register(b0) { float4 opt; };
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
)";
struct FlashVertex {
    float x, y, u, v;
    float r, g, b, a;
    float additive, bright, depth;
};
struct FlashCb {
    float opt[4];
};
constexpr int kFlashTexVent = 0, kFlashTexFire = 1, kFlashTexGlow = 2, kFlashTexSmoke = 3, kFlashTexSpark = 4;
constexpr int kFlashTexGasJet = 5, kFlashTexGasPuff = 6, kFlashTexCount = 7;
const wchar_t* const kFlashTexNames[kFlashTexCount] = {L"ref_flash", L"fire_particle_4", L"particle_flare_004",
                                                       L"ref_mist", L"spark", L"ref_gas_jet", L"ref_gas_puff"};
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
inline float Remap(float x, float i0, float i1, float o0, float o1) {
    float t = i1 != i0 ? Clamp01((x - i0) / (i1 - i0)) : 0.f;
    return o0 + (o1 - o0) * t;
}
}
bool MuzzleFlash::InitPipeline(ID3D11Device* dev) {
    FnD3DCompile compile = GetCompiler();
    if (!compile) return false;
    ID3DBlob *vsb = nullptr, *psb = nullptr, *msg = nullptr;
    bool ok = SUCCEEDED(compile(kFlashShader, sizeof kFlashShader - 1, "hero_fps_flash", nullptr, nullptr, "VS", "vs_5_0",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(compile(kFlashShader, sizeof kFlashShader - 1, "hero_fps_flash", nullptr, nullptr, "PS", "ps_5_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) &&
         SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_));
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
    bd.ByteWidth = sizeof(FlashVertex) * 6 * (kMax + kMaxTracers);
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
bool MuzzleFlash::Load(ID3D11Device* dev, const wchar_t* fxDir, const wchar_t* gasDir, char* err, size_t errLen) {
    Release();
    if (!dev || !fxDir || !fxDir[0]) return false;
    dev_ = dev;
    if (!InitPipeline(dev)) {
        snprintf(err, errLen, "flash pipeline init failed");
        Release();
        return false;
    }
    for (int i = 0; i < 5; ++i) {
        wchar_t path[MAX_PATH * 2];
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls.dds", fxDir, kFlashTexNames[i]);
        char e[256] = {};
        tex_[i] = LoadDdsBc(dev, path, e, sizeof e);
        if (!tex_[i] && i != kFlashTexSmoke && i != kFlashTexSpark) {
            snprintf(err, errLen, "%s", e);
            Release();
            return false;
        }
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls.sheet", fxDir, kFlashTexNames[i]);
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
}
void MuzzleFlash::SpawnGas(const Settings& s) {
    if (!GasLoaded()) return;
    constexpr float kGasSize = 2.2f, kGasAlpha = 1.3f;
    const float k = kGasSize * (s.flashGasScale > 0.f ? s.flashGasScale : 1.f), ka = kGasAlpha * s.flashGasAlpha;
    auto fit = [&](P* p) {
        for (int c = 0; c < 3; ++c) p->p[c] *= k, p->v[c] *= k, p->g[c] *= k;
        p->r0 *= k;
        p->alpha *= ka;
    };
    auto gray = [&](P* p, float a, float b) { p->col[0] = p->col[1] = p->col[2] = Rand(a, b); };
    const float ad = ads_;
    for (int i = 0; i < 2; ++i) {
        P* p = Add();
        if (!p) break;
        p->tex = kFlashTexGasJet;
        p->align = true;
        p->aspect = 2.f, p->minFore = Lerp(0.6f, 0.15f, ad), p->anchor = 1.f;
        p->p[0] = 0.3f + 0.6f * static_cast<float>(i);
        p->r0 = i ? Rand(1.3f, 1.7f) : Rand(2.f, 2.5f);
        p->rs0 = 0.7f, p->rs1 = 1.5f;
        p->life = i ? Rand(0.07f, 0.09f) : Rand(0.11f, 0.14f);
        p->alpha = (i ? 0.35f : 0.5f) * Lerp(1.f, 0.45f, ad);
        p->r0 *= Lerp(1.f, 0.8f, ad);
        p->fadeIn = 0.008f;
        p->fadeOut = p->life * 0.8f;
        gray(p, 0.82f, 0.92f);
        p->frame0 = static_cast<int>(Rand(0.f, 15.99f));
        p->flip = Rand(0.f, 1.f) < 0.5f;
        fit(p);
    }
    for (int i = 0; i < 2; ++i) {
        P* p = Add();
        if (!p) break;
        p->tex = kFlashTexGasPuff;
        p->locked = 0;
        float a = Rand(0.f, 6.2831853f);
        p->p[0] = i ? Rand(4.f, 6.f) : Rand(1.f, 2.f);
        p->v[0] = i ? Rand(50.f, 70.f) : Rand(25.f, 40.f);
        p->v[1] = 4.f * std::cos(a), p->v[2] = 4.f * std::sin(a);
        p->g[2] = 6.f;
        p->drag = 0.15f;
        p->r0 = i ? Rand(1.4f, 1.8f) : Rand(1.8f, 2.3f);
        p->rs0 = 0.6f, p->rs1 = 2.2f;
        p->life = i ? Rand(0.25f, 0.32f) : Rand(0.35f, 0.45f);
        p->alpha = (i ? 0.22f : 0.32f) * Lerp(1.f, 0.6f, ad);
        p->fadeOut = p->life * 0.7f;
        gray(p, 0.8f, 0.9f);
        p->rot = Rand(0.f, 6.2831853f);
        p->rotSpeed = Rand(-0.6f, 0.6f);
        p->flip = Rand(0.f, 1.f) < 0.5f;
        p->fitLife = true;
        p->fadeSize0 = 0.4f, p->fadeSize1 = 0.7f;
        fit(p);
    }
    if (P* p = Add()) {
        p->tex = kFlashTexGasPuff;
        p->locked = 0;
        p->p[0] = Rand(0.5f, 1.5f), p->p[2] = 0.5f;
        p->v[0] = Rand(6.f, 10.f);
        p->g[2] = 9.f;
        p->drag = 0.1f;
        p->r0 = Rand(1.4f, 1.8f);
        p->rs0 = 0.8f, p->rs1 = 2.2f;
        p->life = Rand(0.9f, 1.1f);
        p->alpha = 0.085f;
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
    if (!pending_) return;
    pending_ = false;
    if (!s.flashFlame && !(s.flashGas && GasLoaded())) {
        n_ = 0;
        tSpawn_ = -1;
        return;
    }
    P keep[kMax];
    int nk = 0;
    for (int i = 0; i < n_; ++i)
        if (!parts_[i].locked && parts_[i].placed) {
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
    static FlashVertex verts[(kMax + kMaxTracers) * 6];
    int nv = 0;
    int trStart = 0, trCount = 0;
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
                return FlashVertex{x + px * off * 2.f / w, y + py * off * 2.f / h, u, v, t.col[0], t.col[1], t.col[2], a, 1.f, 1.f, 0.f};
            };
            FlashVertex q4[4] = {vtx(hx, hy, -wh, 0.f, 0.f), vtx(hx, hy, wh, 1.f, 0.f), vtx(tx, tyy, -wt, 0.f, 1.f),
                                 vtx(tx, tyy, wt, 1.f, 1.f)};
            const int idx[6] = {0, 1, 2, 2, 1, 3};
            for (int k = 0; k < 6; ++k) verts[nv++] = q4[idx[k]];
        }
        nTr_ = keep;
        trCount = nv - trStart;
    }
    int counts[kFlashTexCount] = {}, starts[kFlashTexCount] = {};
    const int order[6] = {kFlashTexSmoke, kFlashTexGasPuff, kFlashTexGasJet, kFlashTexFire, kFlashTexVent, kFlashTexGlow};
    int live = 0;
    if (fp && n_ && tSpawn_ >= 0) {
    const MuzzleFrame& f = *fp;
    float dt = static_cast<float>(now - tSim_);
    if (dt < 0.f || dt > 0.1f) dt = 0.f;
    tSim_ = now;
    float unit = f.unit * (s.flashScale > 0.f ? s.flashScale : 1.f);
    auto toView = [&](const float* l, float* out, bool point) {
        for (int k = 0; k < 3; ++k)
            out[k] = (point ? f.pos[k] : 0.f) + (f.fwd[k] * l[0] + f.left[k] * l[1] + f.up[k] * l[2]) * unit;
    };
    for (int oi = 0; oi < 6; ++oi) {
        int tx = order[oi];
        starts[tx] = nv;
        for (int i = 0; i < n_; ++i) {
            P& p = parts_[i];
            if (p.tex != tx) continue;
            float age = static_cast<float>(now - tSpawn_) - p.delay;
            if (age > p.life) continue;
            ++live;
            if (age < 0.f) continue;
            float pos[3];
            if (p.locked) {
                toView(p.p, pos, true);
            } else {
                if (!p.placed) {
                    float pv[3], vv[3];
                    toView(p.p, pv, true);
                    toView(p.v, vv, false);
                    memcpy(p.p, pv, sizeof pv);
                    memcpy(p.v, vv, sizeof vv);
                    float gz = p.g[2];
                    p.g[0] = 0, p.g[1] = gz * unit, p.g[2] = 0;
                    p.placed = 1;
                } else if (dt > 0.f) {
                    float k = std::pow(1.f - p.drag, dt * 30.f);
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
            if (p.rt1 > p.rt0) rs = Lerp(p.rs0, p.rs1, Clamp01((lf - p.rt0) / (p.rt1 - p.rt0)));
            float rad = p.r0 * rs * unit;
            float a = p.alpha * s.flashIntensity;
            if (p.fadeOut > 0.f) a *= Clamp01((p.life - age) / p.fadeOut);
            if (p.fadeIn > 0.f) a *= Clamp01(age / p.fadeIn);
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
                float e[3] = {pos[0] + f.fwd[0] * seg, pos[1] + f.fwd[1] * seg, std::max(pos[2] + f.fwd[2] * seg, 0.02f)};
                float sx = e[0] * pos[2] / e[2] - pos[0], sy = e[1] * pos[2] / e[2] - pos[1];
                float sl = std::sqrt(sx * sx + sy * sy);
                rot = sl > 1e-6f ? std::atan2(sy, sx) : 1.5707963f;
                hl = rad * (p.aspect > 0.f ? p.aspect : 1.f) * std::max(std::min(sl / seg, 1.f), p.minFore);
                cx += std::cos(rot) * hl * p.anchor;
                cy += std::sin(rot) * hl * p.anchor;
                if (p.flip) std::swap(v0, v1);
            } else if (p.flip) {
                std::swap(u0, u1);
            }
            float cr = std::cos(rot), sr = std::sin(rot);
            const float cs[4][2] = {{-1, 1}, {1, 1}, {-1, -1}, {1, -1}};
            const float tc[4][2] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
            const bool gas = tx == kFlashTexGasJet || tx == kFlashTexGasPuff;
            const bool over = gas || tx == kFlashTexSmoke;
            const float zd = gas && depth && f.zf > f.zn ? f.zf / (f.zf - f.zn) * (1.f - f.zn / pos[2]) : 0.f;
            FlashVertex q4[4];
            for (int k = 0; k < 4; ++k) {
                float dx = cs[k][0] * hl * cr - cs[k][1] * rad * sr, dy = cs[k][0] * hl * sr + cs[k][1] * rad * cr;
                q4[k] = {(cx + dx) * f.xs / pos[2], (cy + dy) * f.ys / pos[2], tc[k][0], tc[k][1], col[0], col[1], col[2],
                         a, over ? 0.f : 1.f, p.bright, zd};
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
    FlashCb cb = {{rtvEncodesSrgb ? 1.f : 0.f, 0, 0, 0}};
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
    if (trCount) {
        ctx->PSSetShaderResources(0, 1, &tex_[kFlashTexSpark]);
        ctx->Draw(static_cast<UINT>(trCount), static_cast<UINT>(trStart));
    }
    for (int oi = 0; oi < 6; ++oi) {
        int tx = order[oi];
        if (!counts[tx] || !tex_[tx]) continue;
        ctx->PSSetShaderResources(0, 1, &tex_[tx]);
        ctx->Draw(static_cast<UINT>(counts[tx]), static_cast<UINT>(starts[tx]));
    }
    bk.Restore(ctx);
    return true;
}
void MuzzleFlash::Release() {
    SafeRelease(vs_);
    SafeRelease(ps_);
    SafeRelease(layout_);
    SafeRelease(vb_);
    SafeRelease(cb_);
    SafeRelease(bs_);
    SafeRelease(rs_);
    SafeRelease(dss_);
    SafeRelease(dssDepth_);
    SafeRelease(samp_);
    for (int i = 0; i < kFlashTexCount; ++i) SafeRelease(tex_[i]);
    n_ = 0;
    nTr_ = 0;
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
    bool ok = SUCCEEDED(compile(kCrossShader, sizeof kCrossShader - 1, "hero_fps_cross", nullptr, nullptr, "VS", "vs_5_0",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(compile(kCrossShader, sizeof kCrossShader - 1, "hero_fps_cross", nullptr, nullptr, "PS", "ps_5_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &msg));
    SafeRelease(msg);
    ok = ok && SUCCEEDED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) &&
         SUCCEEDED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_));
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = ok && SUCCEEDED(dev->CreateInputLayout(il, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_));
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
bool Crosshair::Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, UINT w, UINT h, bool rtvEncodesSrgb,
                     const CrosshairSettings& s) {
    if (!s.enabled || !ctx || !rtv || w < 16 || h < 16 || failed_) return false;
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
    float th = std::fmax(1.f, std::round(s.thickness * k)), len = std::round(s.length * k), gap = std::round(s.gap * k);
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
    if (s.dot) rects[n++] = {x0, y0, x0 + th, y0 + th};
    if (!n) return false;
    float col[4] = {s.color[0], s.color[1], s.color[2], s.color[3]};
    if (rtvEncodesSrgb)
        for (int c = 0; c < 3; ++c) col[c] = SrgbToLinear(col[c]);
    CrossVertex v[60];
    int nv = 0;
    auto quad = [&](const R& q, float e, const float* c4) {
        float l = (q.l - e) / w * 2.f - 1.f, r = (q.r + e) / w * 2.f - 1.f;
        float t = 1.f - (q.t - e) / h * 2.f, b = 1.f - (q.b + e) / h * 2.f;
        const CrossVertex a = {l, t, c4[0], c4[1], c4[2], c4[3]}, bq = {r, t, c4[0], c4[1], c4[2], c4[3]},
                          cq = {l, b, c4[0], c4[1], c4[2], c4[3]}, dq = {r, b, c4[0], c4[1], c4[2], c4[3]};
        v[nv++] = a;
        v[nv++] = bq;
        v[nv++] = cq;
        v[nv++] = cq;
        v[nv++] = bq;
        v[nv++] = dq;
    };
    const float black[4] = {0.f, 0.f, 0.f, s.color[3]};
    if (ol > 0.f)
        for (int i = 0; i < n; ++i) quad(rects[i], ol, black);
    for (int i = 0; i < n; ++i) quad(rects[i], 0.f, col);
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
}
