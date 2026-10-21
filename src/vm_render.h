#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
namespace vm {
constexpr int kMeleeSfx = 17;
extern const wchar_t* const kMeleeSfxNames[kMeleeSfx];
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
    float bobRate = 1.0f;
    int muzzleBone = -2;
    float muzzleLocal[3] = {0.0f, 0.0f, 0.0f};
    float muzzleAxis[3] = {1.0f, 0.0f, 0.0f};
    float muzzleUp[3] = {0.0f, 0.0f, 1.0f};
    int adsSightBone = -1;
    float adsSightPoint[3] = {0.0f, 0.0f, 0.0f};
    float adsSightFront[3] = {0.0f, 0.0f, 0.0f};
    bool handIk = true;
    float ikGripRi[3] = {0.0f, 0.0f, 0.0f};
    float ikGripLe[3] = {0.0f, 0.0f, 0.0f};
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
    wchar_t fireLastSound[512] = L"";
    float fireLastVolume = -1.f;
    wchar_t fireCasing[512] = L"";
    wchar_t fireCasingAds[512] = L"";
    float fireCasingVolume = 0.08f;
    wchar_t shellLandSound[512] = L"";
    float shellLandVolume = 0.04f;
    int shellLandMaxVoices = 4;
    float shellLandPerSec = 6.f;
    float shellGround = 1.75f;
    bool fireSettle = false;
    float fireRpm = 0.f;
    int magSize = 0;
    bool magDisposable = false;
    int ammoTotal = 0;
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
    float stepTacSprintGain = -1.f;
    float stepAdsGain = 0.6f;
    float sprintSteps[16] = {};
    int sprintStepN = 0;
    float tacSprintSteps[16] = {};
    int tacSprintStepN = 0;
    float sprintStepTime = 0.f, tacSprintStepTime = 0.f;
    int fireMode = 0;
    float spinUpTime = 0.5f;
    float spinKeep = 0.8f;
    wchar_t spinUpSound[512] = L"", spinLoopSound[512] = L"", spinDownSound[512] = L"";
    float spinVolume = 0.5f;
    float spinBarrels = 6.0f;
    float spinDownTime = 1.6f;
    int spinBone = -2;
    float adsTime = 0.f, adsTimeSprint = 0.f, adsTimeTac = 0.f;
    float adsCurveTime[4] = {0.f, 0.f, 0.f, 0.f};
    float adsFovAlt = 0.f, adsVmFovAlt = 0.f;
    bool adsFovScaling = true;
    float hipSpreadMin = 0.f, hipSpreadMax = 1.5f;
    bool crossHip = true, crossAds = false;
    float crossScale = 1.f;
    wchar_t flashFx[128] = L"";
    float flashScale = 1.0f;
    float flashIntensity = 1.0f;
    float flashLight = 1.0f;
    bool flashSmoke = true;
    bool flashFlame = true;
    bool hitFx = true;
    float hitFxScale = 1.0f;
    float hitFxAlpha = 0.85f;
    float hitFxColor[3] = {0.42f, 0.03f, 0.02f};
    bool flashGas = false;
    wchar_t flashGasFx[128] = L"";
    float flashGasScale = 1.0f;
    float flashGasAlpha = 1.0f;
    float flashGasDistort = 1.0f;
    float flashGasBarrelSmoke = 1.0f;
    float flashGasBarrelSmokeLife = 1.0f;
    float flashGasBarrelSmokeFade = 0.9f;
    float flashGasLinger = 1.0f;
    bool shellEject = true;
    float shellScale = 1.0f;
    float shellSpeed = 1.0f;
    float shellLife = 1.0f;
    float shellSmoke = 1.0f;
    float shellSparks = 1.0f;
    int shellFollow = 1;
    int shellCaliber = 556;
    float shellOut[2] = {3.0f, 4.0f};
    float shellFwd[2] = {0.4f, 1.2f};
    float shellUp[2] = {0.5f, 1.4f};
    float shellSpin[2] = {1500.f, 3000.f};
    float shellLifeRange[2] = {1.2f, 1.8f};
    float shellGravity = 1.0f;
    bool shellLink = false;
    float linkOut[2] = {0.2f, 0.5f};
    float linkFwd[2] = {-0.1f, 0.1f};
    float linkUp[2] = {0.8f, 1.4f};
    float linkLifeRange[2] = {0.9f, 1.1f};
    float linkGravity = 1.4f;
    int flashGasStyle = 1;
    float flashGasSidePuff = 1.0f;
    float flashGasAds = 1.0f;
    bool shieldAllow = false;
    float shieldPos[3] = {-0.34f, -0.34f, 0.43f};
    float shieldRot[3] = {30.f, -4.f, -8.f};
    float shieldAdsPos[3] = {-0.26f, -0.20f, 0.42f};
    float shieldAdsRot[3] = {12.f, 4.f, 0.f};
    float shieldStowPos[3] = {-0.30f, -0.66f, 0.28f};
    float shieldStowRot[3] = {35.f, 45.f, -20.f};
    float shieldSize[2] = {0.42f, 0.58f};
    float shieldGrip[3] = {0.06f, -0.04f, -0.075f};
    float shieldHandQuat[4] = {0.f, 0.f, 0.f, 0.f}, shieldAdsHandQuat[4] = {0.f, 0.f, 0.f, 0.f};
    float shieldElbow[3] = {0.f, 0.f, 0.f}, shieldAdsElbow[3] = {0.f, 0.f, 0.f}, shieldAdsGrip[3] = {0.f, 0.f, 0.f};
    static constexpr int kShieldFingers = 19;
    float shieldFingerPose[kShieldFingers * 4] = {}, shieldAdsFingerPose[kShieldFingers * 4] = {};
    int shieldFingerN = 0, shieldAdsFingerN = 0;
    float shieldWristRot[3] = {0.f, 0.f, 0.f};
    float shieldColor[3] = {0.30f, 0.33f, 0.29f};
    float shieldGunShift[3] = {0.03f, -0.015f, 0.f};
    float shieldNotch[3] = {0.f, 0.f, 0.f};
    float shieldRestPoint[3] = {0.f, 0.f, 0.f};
    int shieldRestBone = -1;
    float shieldRestPoint2[3] = {0.f, 0.f, 0.f};
    int shieldRestBone2 = -2;
    float shieldRestDrop2 = 0.f;
    float shieldRestSide = 0.f;
    float shieldRest = 1.f;
    float shieldRaiseTime = 0.30f;
    float shieldLowerTime = 0.25f;
    float shieldHitShake = 3.f;
    float shieldHitShakeTime = 0.12f;
    float shieldHitCam = 0.4f;
    float shieldMoveScale = 0.8f;
    float shieldAdsZoom = 0.5f;
    float shieldAdsTime = 1.5f;
    float shieldKick[3] = {1.35f, 1.5f, 0.8f};
    float shieldBlockHip = 0.5f;
    float shieldBlockAds = 1.0f;
    wchar_t shieldModel[520] = L"";
    float shieldModelMetal = 1.f;
    float meleeStrikeLead = 0.25f;
    wchar_t meleeSfx[kMeleeSfx][512] = {};
    float meleeSfxVolume[kMeleeSfx] = {-1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f,
                                       -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f};
    float meleeSfxPitch[kMeleeSfx][2] = {};
    float meleeSfxMaster = 1.f;
    bool meleeSfxSwingCut = false;
    float meleeSfxSwingPre = -1.f;
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
    bool brass;
    float bpos[3], bout[3], bfwd[3], bup[3];
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
    float kickPos[3] = {};
    float kickRot[3] = {};
    float kickPivot[3] = {};
    float adsAlign = 0;
    float fovDeg = 0;
    int spinBone = -1;
    float spinAngle = 0;
    float shieldArmW = 0;
    float shieldPose[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float shieldGrip[3] = {};
    float shieldWristRot[3] = {};
    float shieldHandQuat[4] = {};
    float shieldElbow[3] = {};
    float shieldFinger[19][4] = {};
    int shieldFingerOn = 0;
    float shieldHoldW = 0;
    float gunShift[3] = {};
};
bool LoadSettings(const wchar_t* ini, Settings& s);
struct CrosshairSettings {
    bool enabled = true;
    float length = 7.0f;
    float gap = 4.0f;
    float thickness = 2.0f;
    float outline = 1.0f;
    bool dot = false;
    float dotSize = 0.0f;
    float dotOutline = 0.0f;
    float spreadShot = 0.0f;
    float spreadMove = 0.0f;
    float spreadMax = 16.0f;
    float spreadRecover = 6.0f;
    float outlineAlpha = -1.0f;
    float spreadHold = 0.0f;
    float fadeStart = 0.0f;
    float fadeEnd = 0.0f;
    float color[4] = {0.0f, 1.0f, 0.0f, 0.9f};
};
bool LoadCrosshair(const wchar_t* ini, CrosshairSettings& s);
class MuzzleFlash {
public:
    bool Load(ID3D11Device* dev, const wchar_t* fxDir, const wchar_t* gasDir, char* err, size_t errLen);
    void Trigger(float ads = 0.f);
    void AddTracer(const float* from, const float* to);
    void AddImpact(const float* pos, const float* dir, bool kill);
    void BeginFrame(double now, const Settings& s);
    float LightLevel(double now) const;
    bool Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* depth, UINT w, UINT h,
              bool rtvEncodesSrgb, double now, const MuzzleFrame* f, const WorldCam* cam, const Settings& s);
    void SetScene(ID3D11ShaderResourceView* srv, bool gamma) { scene_ = srv; sceneGamma_ = gamma; }
    bool HasDistortion() const;
    void Release();
    bool Loaded() const { return tex_[0] != nullptr; }
    bool GasLoaded() const { return tex_[5] && tex_[6]; }
    int LiveTracers() const { return nTr_; }
    int LiveImpacts() const { return nImp_; }
    int Live() const { return n_; }
    void Dump(double now) const;
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
        float ease;
        float alphaPow;
        unsigned char axis;
        float distNoise, distPush;
        float lift;
        unsigned char nA, nS;
        float aK[4][2], sK[4][2];
        float dragRate;
        unsigned char frame;
    };
private:
    bool InitPipeline(ID3D11Device* dev);
    float Rand(float a, float b);
    void Spawn(const Settings& s);
    P* Add();
    void SpawnFlame(const Settings& s);
    void SpawnGas(const Settings& s);
    void SpawnBarrelSmoke(const Settings& s, double now);
    void SpawnShellFx(const Settings& s);
    ID3D11Device* dev_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11PixelShader* psDist_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* vb_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
    ID3D11BlendState* bs_ = nullptr;
    ID3D11RasterizerState* rs_ = nullptr;
    ID3D11DepthStencilState* dss_ = nullptr;
    ID3D11DepthStencilState* dssDepth_ = nullptr;
    ID3D11SamplerState* samp_ = nullptr;
    ID3D11ShaderResourceView* tex_[11] = {};
    Sheet sheet_[11];
    ID3D11ShaderResourceView* scene_ = nullptr;
    bool sceneGamma_ = false;
    int burst_ = 0;
    double tLastShot_ = -1.0;
    struct Tracer {
        float a[3], b[3];
        double t0;
        bool pending;
        float col[3], alpha, lenSec;
    };
    static constexpr int kMaxTracers = 24;
    Tracer trs_[kMaxTracers];
    int nTr_ = 0;
    struct Impact {
        float p[3], d[3];
        double t0;
        bool pending, kill;
        unsigned seed;
    };
    static constexpr int kMaxImpacts = 12;
    static constexpr int kImpactQuads = 10;
    Impact imps_[kMaxImpacts];
    int nImp_ = 0;
    static constexpr int kMax = 160;
    P parts_[kMax];
    int n_ = 0;
    bool pending_ = false;
    float ads_ = 0.f;
    double tSpawn_ = -1.0, tSim_ = -1.0;
    unsigned rng_ = 0x9e3779b9u;
};
class ShellEject {
public:
    bool Load(ID3D11Device* dev, char* err, size_t errLen);
    void Trigger(float ads = 0.f) { if (vs_ && pending_ < 4) ++pending_, ads_ = ads; }
    bool Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* depth, UINT w, UINT h, bool rtvEncodesSrgb,
              double now, const MuzzleFrame* f, const WorldCam* cam, const Settings& s, float flashLight,
              ID3D11ShaderResourceView* scene, bool sceneGamma);
    int Live() const { return n_; }
    bool PopLanding(float* dist, float* age, int* kind) {
        if (!nLand_) return false;
        --nLand_;
        *dist = land_[nLand_][0], *age = land_[nLand_][1], *kind = static_cast<int>(land_[nLand_][2]);
        return true;
    }
    float LastMinViewX() const { return lastMinX_; }
    void Clear() { n_ = 0, pending_ = 0, nLand_ = 0; }
    void Seed(unsigned seed) { rng_ = seed ? seed : 1u; }
    bool Loaded() const { return vs_ != nullptr; }
    void Release();
private:
    float Rand(float a, float b);
    struct Shell {
        float p[3], v[3];
        float q[4];
        float w[3];
        float age, life, size;
        float g;
        unsigned char kind;
        bool world;
        bool landed;
    };
    static constexpr int kMax = 64;
    Shell sh_[kMax];
    int n_ = 0, pending_ = 0;
    float ads_ = 0.f;
    double tSim_ = -1.0;
    bool camValid_ = false;
    float camPrev_[3] = {}, camVel_[3] = {};
    float lastMinX_ = 1e9f;
    float land_[16][3] = {};
    int nLand_ = 0;
    unsigned rng_ = 0x2545f491u;
    UINT indexCount_ = 0;
    UINT kindIdx_[3] = {}, kindFirst_[3] = {}, kindCount_[3] = {};
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer *vb_ = nullptr, *ib_ = nullptr, *cbFrame_ = nullptr, *cbObj_ = nullptr;
    ID3D11RasterizerState* rs_ = nullptr;
    ID3D11DepthStencilState *dss_ = nullptr, *dssNoDepth_ = nullptr;
    ID3D11BlendState* bs_ = nullptr;
    ID3D11SamplerState* samp_ = nullptr;
};
class Shield {
public:
    bool Load(ID3D11Device* dev, const Settings& s, char* err, size_t errLen);
    bool Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* depth, UINT w, UINT h, bool rtvEncodesSrgb,
              double now, int look, const float* m34, float xs, float ys, const WorldCam* cam, const Settings& s,
              ID3D11ShaderResourceView* scene, bool sceneGamma);
    void Hit() { if (hitPending_ < 8) ++hitPending_; }
    int LiveSparks() const { return n_; }
    void Clear() { n_ = 0, hitPending_ = 0; }
    bool Loaded() const { return vs_ != nullptr; }
    bool BuiltFor(const Settings& s) const {
        return vs_ && builtSize_[0] == s.shieldSize[0] && builtSize_[1] == s.shieldSize[1] && builtGrip_[0] == s.shieldGrip[0] &&
               builtGrip_[1] == s.shieldGrip[1] && builtGrip_[2] == s.shieldGrip[2] && builtColor_[0] == s.shieldColor[0] &&
               builtColor_[1] == s.shieldColor[1] && builtColor_[2] == s.shieldColor[2] && !wcscmp(builtModel_, s.shieldModel);
    }
    bool HasModel() const { return nSub_ > 0; }
    int ModelTriangles() const { return modelTris_; }
    void Seed(unsigned seed) { rng_ = seed ? seed : 1u; }
    void Release();
private:
    float Rand(float a, float b);
    struct Piece {
        float p[3], v[3], q[4], w[3];
        float age, life, g;
        unsigned char kind;
        bool world;
    };
    static constexpr int kMax = 64;
    Piece pc_[kMax];
    int n_ = 0, hitPending_ = 0;
    double tSim_ = -1.0;
    unsigned rng_ = 0x51ed27u;
    float builtSize_[2] = {0.f, 0.f}, builtGrip_[3] = {}, builtColor_[3] = {};
    wchar_t builtModel_[520] = L"";
    static constexpr int kKinds = 2;
    UINT kindBase_[kKinds] = {}, kindFirst_[kKinds] = {}, kindCount_[kKinds] = {};
    struct Sub {
        UINT first, count;
        int tex[3];
    };
    static constexpr int kMaxSub = 8, kMaxTex = 12, kMaxOutline = 64;
    Sub sub_[kMaxSub] = {};
    int nSub_ = 0, nTex_ = 0, nOutline_ = 0, modelTris_ = 0;
    ID3D11ShaderResourceView* srv_[kMaxTex] = {};
    float outline_[kMaxOutline][3] = {};
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer *vb_ = nullptr, *ib_ = nullptr, *cbFrame_ = nullptr, *cbObj_ = nullptr;
    ID3D11RasterizerState* rs_ = nullptr;
    ID3D11DepthStencilState *dss_ = nullptr, *dssNoDepth_ = nullptr;
    ID3D11BlendState* bs_ = nullptr;
    ID3D11SamplerState* samp_ = nullptr;
};
class Crosshair {
public:
    bool Draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, UINT w, UINT h, bool rtvEncodesSrgb,
              const CrosshairSettings& s, float spread = 0.f, float alpha = 1.f);
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
    int BarrelBone() const { return barrelBone_; }
    float BarrelDir() const { return barrelDir_; }
    bool ArmRig() const {
        return gunBone_ >= 0 && armBone_[0][0] >= 0 && armBone_[0][1] >= 0 && armBone_[0][2] >= 0 && armBone_[1][0] >= 0 &&
               armBone_[1][1] >= 0 && armBone_[1][2] >= 0;
    }
    float IkResidualMm(int side) const { return side == 0 || side == 1 ? ikResidual_[side] : -1.f; }
    float IkShiftMm(int side) const { return side == 0 || side == 1 ? ikShift_[side] : 0.f; }
    bool ShieldView(float* m34) const {
        if (!shieldViewValid_) return false;
        for (int i = 0; i < 12; ++i) m34[i] = shieldView_[i];
        return true;
    }
    float ShieldIkResidualMm() const { return shieldIkResidual_; }
    float ShieldForearmErrDeg() const { return shieldForearmErr_; }
    float ShieldArmShiftMm() const { return shieldArmShift_; }
    float ShieldRestWeight() const { return shieldRestK_; }
    float ShieldRestGapMm() const { return shieldRestGap_; }
    float ShieldRestT() const { return shieldRestT_; }
    void ShieldRestTarget(float* p) const { p[0] = shieldRestView_[0], p[1] = shieldRestView_[1], p[2] = shieldRestView_[2]; }
    void ShieldGripTarget(float* p) const { p[0] = shieldGripView_[0], p[1] = shieldGripView_[1], p[2] = shieldGripView_[2]; }
    void LeftShoulderView(float* p) const { p[0] = leftShoulderView_[0], p[1] = leftShoulderView_[1], p[2] = leftShoulderView_[2]; }
    void RequestPoseDump(const wchar_t* path) { wcsncpy(poseDumpPath_, path, 519), poseDumpPath_[519] = 0; }
    bool Proj(float* xs, float* ys) const {
        if (!(projXs_ > 0.f)) return false;
        *xs = projXs_, *ys = projYs_;
        return true;
    }
    bool LeftWristView(float* p) const {
        if (!shieldViewValid_) return false;
        for (int i = 0; i < 3; ++i) p[i] = leftWristView_[i];
        return true;
    }
    unsigned Clips() const { return clipCount_; }
    int FindClip(const char* name) const;
    float ClipDuration(int i) const;
    bool ClipLoops(int i) const;
    bool Muzzle(float* ndcX, float* ndcY, float* aspect) const;
    bool MuzzleView(MuzzleFrame* f) const;
    ID3D11DepthStencilView* Depth(UINT w, UINT h) const { return dsv_ && depthW_ == w && depthH_ == h ? dsv_ : nullptr; }
    ID3D11ShaderResourceView* SceneSrv() const { return sceneFresh_ ? sceneSrv_ : nullptr; }
    bool SceneGamma() const { return sceneGammaEnc_; }
    ID3D11ShaderResourceView* CopyForDistort(ID3D11DeviceContext* ctx, ID3D11Texture2D* backBuffer, UINT w, UINT h, bool* gamma);
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
    void EvalBones(const Look& look);
    void ArmIk(const Settings& s, const float* k12);
    void ShieldIk(const Look& look, const float* nInv12);
    void FinishBones(float* palette , int muzzleBone, const float* muzzleLocal, float* muzzleModel,
                     const float* axisLocal, const float* upLocal, float* axisModel, float* upModel);
    void SampleClip(int clip, float time, unsigned bone, float* trs) const;
    bool BoneWorldAt(int clip, float time, int bone, float* m34) const;
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
    bool sceneFresh_ = false, sceneGammaEnc_ = false;
    ID3D11Texture2D* distTex_ = nullptr;
    ID3D11ShaderResourceView* distSrv_ = nullptr;
    UINT distW_ = 0, distH_ = 0;
    DXGI_FORMAT distFmt_ = DXGI_FORMAT_UNKNOWN;
    bool distBroken_ = false;
    static constexpr unsigned kMaxTextures = 64;
    ID3D11ShaderResourceView* srv_[kMaxTextures] = {};
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
    int barrelBone_ = -1;
    int brassBone_ = -1;
    float barrelDir_ = -1.f;
    int armBone_[2][3] = {{-1, -1, -1}, {-1, -1, -1}};
    int gunBone_ = -1, handTag_[2] = {-1, -1};
    int fingerBone_[19] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    float ikResidual_[2] = {-1.f, -1.f}, ikShift_[2] = {0.f, 0.f};
    bool shieldViewValid_ = false;
    float shieldView_[12] = {}, leftWristView_[3] = {}, shieldIkResidual_ = -1.f, shieldForearmErr_ = -1.f, shieldArmShift_ = -1.f;
    float shieldRestK_ = 0.f, shieldRestView_[3] = {}, shieldNotch_[3] = {}, shieldRestGap_ = -1.f, shieldRestT_ = -1.f;
    float shieldGripView_[3] = {}, leftShoulderView_[3] = {};
    float shieldRestA_[3] = {}, shieldRestB_[3] = {};
    wchar_t poseDumpPath_[520] = L"";
    float projXs_ = 0.f, projYs_ = 0.f;
    unsigned indexCount_ = 0;
    bool muzzleValid_ = false;
    float muzzleNdc_[2] = {0, 0}, muzzleAspect_ = 1;
    bool muzzleFrameValid_ = false;
    MuzzleFrame muzzleFrame_ = {};
};
namespace prefetch {
bool Model(int tag, const wchar_t* hvmPath, int* files, size_t* bytes, char* err, size_t errLen);
void Drop(int tag);
int WarmShaders();
}
}
