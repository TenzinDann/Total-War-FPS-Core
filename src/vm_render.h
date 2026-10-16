#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
namespace vm {
struct Settings {
    float fovDeg = 55.0f;
    float offset[3] = {0.13f, -0.15f, 0.32f};
    float rotDeg[3] = {0.0f, 0.0f, 0.0f};
    float tiltDeg[3] = {0.0f, 0.0f, 0.0f};
    float scale = 1.0f;
    bool mirrorX = false;
    float lightDir[3] = {-0.35f, 0.75f, -0.55f};
    float lightColor[3] = {1.0f, 0.95f, 0.88f};
    float lightIntensity = 2.6f;
    float ambSky[3] = {0.42f, 0.47f, 0.55f};
    float ambGround[3] = {0.20f, 0.18f, 0.16f};
    float ambIntensity = 1.0f;
    float exposure = 1.0f;
    float sceneLight = 0.7f;
    float swayScale = 1.0f;
    float bobScale = 1.0f;
    int muzzleBone = -2;
    float muzzleLocal[3] = {0.0f, 0.0f, 0.0f};
    float muzzleAxis[3] = {1.0f, 0.0f, 0.0f};
    float muzzleUp[3] = {0.0f, 0.0f, 1.0f};
    wchar_t fireSound[512] = L"";
    float fireVolume = 0.7f;
    wchar_t fireSoundAds[512] = L"";
    wchar_t fireMech[512] = L"";
    wchar_t fireMechAds[512] = L"";
    float fireMechVolume = 0.7f;
    int fireMaxVoices = 24;
    wchar_t fireLfe[512] = L"";
    float fireLfeVolume = 0.7f;
    wchar_t fireAtmo[512] = L"";
    float fireAtmoVolume = 0.7f;
    int fireAtmoMaxVoices = 4;
    wchar_t fireTail[512] = L"";
    float fireTailVolume = 0.7f;
    float fireTailGap = 0.15f;
    float fireRpm = 0.f;
    int magSize = 0;
    float reloadRefill = 0.f;
    float reloadEmptyRefill = 0.f;
    wchar_t weaponId[64] = L"";
    bool weaponDefault = false;
    char projectile[64] = "";
    wchar_t animSounds[128] = L"";
    float animSoundVolume = 0.7f;
    wchar_t stepFoot[512] = L"";
    wchar_t stepClothRun[512] = L"";
    wchar_t stepClothSprint[512] = L"";
    wchar_t stepRattle[512] = L"";
    float stepFootVolume = 0.3f, stepClothVolume = 1.f, stepRattleVolume = 0.2f;
    float stepSprintGain = 1.3f;
    float stepAdsGain = 0.6f;
    float sprintSteps[16] = {};
    int sprintStepN = 0;
    float tacSprintSteps[16] = {};
    int tacSprintStepN = 0;
    wchar_t flashFx[128] = L"";
    float flashScale = 1.0f;
    float flashIntensity = 1.0f;
    float flashLight = 1.0f;
    bool flashSmoke = true;
    bool flashFlame = true;
    bool flashGas = false;
    wchar_t flashGasFx[128] = L"";
    float flashGasScale = 1.0f;
    float flashGasAlpha = 1.0f;
};
struct WorldCam {
    float eye[3], right[3], up[3], fwd[3];
    float fovY;
};
struct MuzzleFrame {
    float pos[3];
    float fwd[3], left[3], up[3];
    float unit;
    float xs, ys;
    float zn, zf;
};
struct Look {
    float time = 0;
    float swayX = 0, swayY = 0;
    float bobPhase = 0;
    float bobAmount = 0;
    float kick = 0;
    int clip = -1;
    float clipTime = 0;
    int clipPrev = -1;
    float clipTimePrev = 0;
    float blend = 1;
    float flashLight = 0;
};
bool LoadSettings(const wchar_t* ini, Settings& s);
struct CrosshairSettings {
    bool enabled = true;
    float length = 7.0f;
    float gap = 4.0f;
    float thickness = 2.0f;
    float outline = 1.0f;
    bool dot = false;
    float color[4] = {0.0f, 1.0f, 0.0f, 0.9f};
};
bool LoadCrosshair(const wchar_t* ini, CrosshairSettings& s);
class MuzzleFlash {
public:
    bool Load(ID3D11Device* dev, const wchar_t* fxDir, const wchar_t* gasDir, char* err, size_t errLen);
    void Trigger(float ads = 0.f);
    void AddTracer(const float* from, const float* to);
    void BeginFrame(double now, const Settings& s);
    float LightLevel(double now) const;
    bool Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* depth, UINT w, UINT h,
              bool rtvEncodesSrgb, double now, const MuzzleFrame* f, const WorldCam* cam, const Settings& s);
    void Release();
    bool Loaded() const { return tex_[0] != nullptr; }
    bool GasLoaded() const { return tex_[5] && tex_[6]; }
    int LiveTracers() const { return nTr_; }
    int Live() const { return n_; }
    void Seed(unsigned seed) { rng_ = seed ? seed : 1u; }
    struct Sheet {
        int frames[4] = {};
        float rect[4][16][4] = {};
    };
    struct P {
        unsigned char tex, locked, placed, seq;
        float delay, life;
        float p[3], v[3], g[3];
        float drag;
        float r0, rs0, rs1, rt0, rt1;
        float rot, rotSpeed;
        bool flip;
        float col[3], alpha;
        float fadeIn, fadeOut;
        float colorFadeAt;
        float bright;
        bool fitLife;
        float fadeSize0, fadeSize1;
        int frame0;
        float fps;
        bool align;
        float aspect, minFore, anchor;
    };
private:
    bool InitPipeline(ID3D11Device* dev);
    float Rand(float a, float b);
    void Spawn(const Settings& s);
    P* Add();
    void SpawnFlame(const Settings& s);
    void SpawnGas(const Settings& s);
    ID3D11Device* dev_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* vb_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
    ID3D11BlendState* bs_ = nullptr;
    ID3D11RasterizerState* rs_ = nullptr;
    ID3D11DepthStencilState* dss_ = nullptr;
    ID3D11DepthStencilState* dssDepth_ = nullptr;
    ID3D11SamplerState* samp_ = nullptr;
    ID3D11ShaderResourceView* tex_[7] = {};
    Sheet sheet_[7];
    struct Tracer {
        float a[3], b[3];
        double t0;
        bool pending;
        float col[3], alpha, lenSec;
    };
    static constexpr int kMaxTracers = 24;
    Tracer trs_[kMaxTracers];
    int nTr_ = 0;
    static constexpr int kMax = 96;
    P parts_[kMax];
    int n_ = 0;
    bool pending_ = false;
    float ads_ = 0.f;
    double tSpawn_ = -1.0, tSim_ = -1.0;
    unsigned rng_ = 0x9e3779b9u;
};
class Crosshair {
public:
    bool Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, UINT w, UINT h, bool rtvEncodesSrgb,
              const CrosshairSettings& s);
    void Release();
private:
    bool Init(ID3D11Device* dev);
    ID3D11Device* dev_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* vb_ = nullptr;
    ID3D11BlendState* bs_ = nullptr;
    ID3D11RasterizerState* rs_ = nullptr;
    ID3D11DepthStencilState* dss_ = nullptr;
    bool failed_ = false;
};
class Renderer {
public:
    bool Init(ID3D11Device* dev, const wchar_t* hvmPath, char* err, size_t errLen);
    bool Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11Texture2D* sceneTex, UINT w, UINT h,
              bool rtvEncodesSrgb, const Settings& s, const Look& look);
    void Release();
    bool Ready() const { return ready_; }
    ID3D11Device* Device() const { return dev_; }
    unsigned Triangles() const { return indexCount_ / 3; }
    unsigned Bones() const { return boneCount_; }
    unsigned Clips() const { return clipCount_; }
    int FindClip(const char* name) const;
    float ClipDuration(int i) const;
    bool ClipLoops(int i) const;
    bool Muzzle(float* ndcX, float* ndcY, float* aspect) const;
    bool MuzzleView(MuzzleFrame* f) const;
    ID3D11DepthStencilView* Depth(UINT w, UINT h) const { return dsv_ && depthW_ == w && depthH_ == h ? dsv_ : nullptr; }
private:
    struct Submesh {
        uint32_t firstIndex, indexCount;
        int tex[3];
        uint32_t normalMode, materialMode, flags;
    };
    struct Bone {
        int parent;
        float bind[10];
        float invBind[12];
    };
    struct Clip {
        char name[32];
        float fps;
        uint32_t frames, flags;
        uint32_t dataOffset;
    };
    bool LoadHvm(const wchar_t* path, char* err, size_t errLen);
    bool CreatePipeline(char* err, size_t errLen);
    bool EnsureDepth(UINT w, UINT h);
    bool EnsureScene(UINT w, UINT h, DXGI_FORMAT fmt);
    void EvalBones(const Look& look, float* palette , int muzzleBone, const float* muzzleLocal,
                   float* muzzleModel, const float* axisLocal, const float* upLocal, float* axisModel, float* upModel);
    void SampleClip(int clip, float time, unsigned bone, float* trs) const;
    bool ready_ = false;
    ID3D11Device* dev_ = nullptr;
    ID3D11Buffer *vb_ = nullptr, *ib_ = nullptr, *cbFrame_ = nullptr, *cbBones_ = nullptr, *cbMat_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11RasterizerState* rs_ = nullptr;
    ID3D11DepthStencilState* dss_ = nullptr;
    ID3D11BlendState* bs_ = nullptr;
    ID3D11SamplerState* samp_ = nullptr;
    ID3D11Texture2D* depthTex_ = nullptr;
    ID3D11DepthStencilView* dsv_ = nullptr;
    UINT depthW_ = 0, depthH_ = 0;
    ID3D11Texture2D* sceneTex_ = nullptr;
    ID3D11ShaderResourceView* sceneSrv_ = nullptr;
    UINT sceneW_ = 0, sceneH_ = 0, sceneMips_ = 0;
    DXGI_FORMAT sceneFmt_ = DXGI_FORMAT_UNKNOWN;
    bool sceneBroken_ = false;
    ID3D11ShaderResourceView* srv_[32] = {};
    ID3D11ShaderResourceView* white_ = nullptr;
    unsigned texCount_ = 0;
    Submesh* subs_ = nullptr;
    unsigned subCount_ = 0;
    Bone* bones_ = nullptr;
    unsigned boneCount_ = 0;
    Clip* clips_ = nullptr;
    unsigned clipCount_ = 0;
    float* clipData_ = nullptr;
    int cameraBone_ = -1;
    unsigned indexCount_ = 0;
    bool muzzleValid_ = false;
    float muzzleNdc_[2] = {0, 0}, muzzleAspect_ = 1;
    bool muzzleFrameValid_ = false;
    MuzzleFrame muzzleFrame_ = {};
};
}
