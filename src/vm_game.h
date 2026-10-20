#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include "kick.h"
namespace vmgame {
using LogFn = void (*)(const char* fmt, ...);
struct RenderAddrs {
    uintptr_t globalRenderer;
    uintptr_t vtRenderer;
    uintptr_t vtInitSlot;
    uintptr_t fnRendererInit;
    uintptr_t fnPresentHelper;
    uintptr_t presentReturn;
    uintptr_t insGlobalWrite;
    uintptr_t insScCreate;
    uintptr_t oSwapchain;
    const unsigned char* presentHelperBytes;
    size_t presentHelperLen;
    const unsigned char* globalWriteBytes;
    size_t globalWriteLen;
};
bool VerifyStatic(uintptr_t exeBase, LogFn log, const RenderAddrs& addrs);
void Setup(uintptr_t exeBase, const wchar_t* dllDir, LogFn log);
bool Enabled();
bool LayerActive();
UINT TestKey();
UINT ReloadKey();
UINT AnimKey();
UINT ModelKey();
void CycleAnim();
void CycleModel();
void Fire();
double FireInterval();
bool CanFire();
bool Reloading();
bool StartMagReload();
void SelectWeapon(int n);
void CycleWeapon(int dir);
bool Busy();
void SetTrigger(bool held);
int CurrentWeapon();
void SetHudCover(float x0, float y0, float x1, float y1);
void GetKick(kick::Params* out);
const char* WeaponProjectile(int idx);
void ToggleTestLayer();
void RequestReload();
void SetFirstPerson(bool on);
void PublishView(float yaw, float pitch, float speed);
void PublishStance(bool ads, bool sprint, bool tac, int adsKind = 0);
void PublishAdsTime(float normal, float sprint, float tac);
float CurrentAdsTime(int kind);
bool CurveAdsState(float* frac, float* adsFovAlt);
void PublishCamera(const float eye[3], const float right[3], const float up[3], const float fwd[3], float fovY);
void Tracer(const float from[3], const float to[3]);
bool Muzzle(float* ndcX, float* ndcY, float* aspect);
UINT ShieldKey();
UINT ShieldHitKey();
void ToggleShield();
void DebugShieldHit();
void ShieldHit();
float ShieldBlockFraction();
bool ShieldWanted();
void ShieldGame(float* weight, float* moveScale, float* zoomKeep);
float ShieldCamKick();
enum MeleeStrikeKind { kStrikeStab, kStrikeSweep, kStrikeHeavySweep, kStrikeHeavy, kStrikeBash, kStrikeKinds };
bool MeleeActive();
void MeleeInput(bool attack, bool block);
int TakeMeleeStrike();
void MeleeHitFeedback(int hits);
}
