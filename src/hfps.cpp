#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "shield_hit.h"
#include "vm_game.h"
#include "cast_marker.h"
#include "game_volume.h"
#include "vm_audio.h"
#ifdef HFPS_PACK_ASSETS
#include "pack_extract.h"
#endif
extern "C" {
void* p_GetFileVersionInfoA;
void* p_GetFileVersionInfoByHandle;
void* p_GetFileVersionInfoExA;
void* p_GetFileVersionInfoExW;
void* p_GetFileVersionInfoSizeA;
void* p_GetFileVersionInfoSizeExA;
void* p_GetFileVersionInfoSizeExW;
void* p_GetFileVersionInfoSizeW;
void* p_GetFileVersionInfoW;
void* p_VerFindFileA;
void* p_VerFindFileW;
void* p_VerInstallFileA;
void* p_VerInstallFileW;
void* p_VerLanguageNameA;
void* p_VerLanguageNameW;
void* p_VerQueryValueA;
void* p_VerQueryValueW;
void* g_orig_cam_update;
void hfps_cam_update_thunk();
void hfps_after_cam_update(uintptr_t mgr);
void* g_orig_sim_tick;
uintptr_t g_sim_tick_ret;
void hfps_sim_tick_thunk();
void hfps_sim_tick(uintptr_t self, uintptr_t packet, uintptr_t battle);
void* g_orig_post_step;
uintptr_t g_post_step_ret;
void hfps_post_step_thunk();
void hfps_post_step_begin(uintptr_t self, uintptr_t world);
void* g_orig_hero_1c8;
uintptr_t g_hero_1c8_ret1, g_hero_1c8_ret2;
void hfps_hero_1c8_thunk();
void hfps_hero_melee(uintptr_t self, uintptr_t pend);
}
namespace {
struct CodePrint {
    uintptr_t rva, len;
    unsigned long long fnv;
};
struct PrintSet {
    const CodePrint* p;
    size_t n;
};
template <size_t N>
constexpr PrintSet Prints(const CodePrint (&a)[N]) {
    return {a, N};
}
const CodePrint kNetPrints904[] = {
    {0x2f8fbd8, 0x2bc, 0xdb3588364c85fa39ull},
    {0x2f795c0, 0x13f, 0x3bf77016869b3696ull},
    {0x310d064, 0x71d, 0xe696c21e9c1292a5ull},
    {0x3110ff0, 0x46, 0x3860f3f85297a94dull},
    {0x3132178, 0x4e, 0x9306c6b89a050292ull}};
const CodePrint kArrowPrints904[] = {
    {0x2fd07d0, 0x20e, 0x6ec8c833b7e346d3ull},
    {0x2fd09e0, 0x3d62, 0x54ddff0da7591a60ull},
    {0x2fa3f64, 0xdee, 0x10284c53e5b108d0ull},
    {0x2fa3d54, 0x20e, 0xcdc36317d158b862ull},
    {0x2f98cc2, 0x34, 0x5f1881ec384120feull}};
constexpr uintptr_t oVisRootArrowMgr = 0x1f0, kVtArrowMgr904 = 0x3ad0490, kFnArrowUpdate904 = 0x2fea7d8, kFnArrowDraw904 = 0x2fd07d0,
                    kVtArrowVisual2_904 = 0x3ad04b8, oArrowMgrCount = 0x5c, oArrowMgrList = 0x60, oArrowVt2 = 0x10, oArrowSoldierVis = 0xe0,
                    oArrowBone = 0x144, oArrowFlags = 0x151;
const CodePrint kCamPrints904[] = {{0x2f1dc80, 0x40, 0x5b87161bbe161bceull},
                                   {0x181c8ac, 0x3b, 0xf51ecd15ecf21432ull},
                                   {0x181c91c, 0x3b, 0x4089e72f9a4161e2ull},
                                   {0x181c990, 0x71, 0x8a49f5d5f33a03a6ull},
                                   {0x8d65c0, 0x6, 0x6af6c27548783d34ull},
                                   {0x8d6560, 0x6, 0x6b2ca27548a59644ull},
                                   {0x8d6530, 0x6, 0x6b3aba7548b1fc68ull}};
const CodePrint kFirePrints904[] = {{0x2e9b9e4, 0x6e, 0x56cf095dd3517c1bull},
                                    {0x2eecf1c, 0xcb, 0x6d8db3c33ca24565ull},
                                    {0x2f1aeaa, 0x20, 0x7b6528ba6af5ed25ull}};
const CodePrint kMovePrints904[] = {{0x2ed8a10, 0x16c, 0x7d6baa5fab37caa4ull},
                                   {0x2ee7a5c, 0x93, 0x6c4375a2273c602aull},
                                   {0x2edcfac, 0xd5, 0x80ebc11e086bbf53ull},
                                   {0x2ea9a94, 0x7f, 0x74bbb1c7bdaedd6full}};
const CodePrint kCmdPrints904[] = {{0x1bb1924, 0x59, 0x7710609ca2576686ull},
                                  {0x1bca278, 0xd6, 0x4f0781454a0ac847ull},
                                  {0x1bcd5e4, 0x56, 0xe834d2937f8440fcull},
                                  {0x668ad4, 0x8b, 0x6767db014f05baaaull},
                                  {0x2eafa2c, 0x83, 0x3107b7a6b4073cc4ull},
                                  {0x310cfac, 0xb6, 0xc28718f91f83b034ull}};
const CodePrint kOvrPrints904[] = {{0x2ecfb0c, 0xc7, 0x676e2663dc2d068dull},
                                  {0x2f6496c, 0x63, 0xb58bc000221fc6a1ull}};
const CodePrint kHaltPrints904[] = {{0x2ecf98c, 0x59, 0xb4942d93f7bdd5a6ull}};
const CodePrint kTpPrints904[] = {{0x2ecfbd4, 0xb3, 0x1987684e822436bdull},
                                 {0x2f64f54, 0x5a, 0xe98dc842affae2a9ull},
                                 {0x2f78b30, 0x167, 0xf7ff13396414fd5bull},
                                 {0x8f5a70, 0x8, 0x39ed053363d24f18ull}};
const CodePrint kGhostPrints904[] = {{0x2e9e1d0, 0x2a, 0x9a3118e3ad43cf9dull},
                                     {0x2df5e84, 0x6e, 0xaed63465efc6ed6cull},
                                     {0x2eef630, 0xde, 0xa7c5c19182e3be3full},
                                     {0x2ed679c, 0x4a, 0x75204a686fbb6fb9ull},
                                     {0x2f0fdf0, 0x37, 0x15207b5a295a72fbull}};
const CodePrint kRevealPrints904[] = {
    {0x2e9e178, 0x2a, 0x13ecfaf02c8144b7ull},
    {0x2e9d5e4, 0x6e, 0x3806c0c92e018d59ull},
    {0x2eef54c, 0x68, 0x4f74d03fa523bb47ull},
    {0x2ed65e8, 0x97, 0x921c3388e6112858ull},
    {0x2ef0660, 0x118, 0xc1878d4db3962358ull},
    {0x2f0f0b8, 0x6b, 0xb62bedbb98182291ull},
    {0x2ebe1c8, 0x53, 0xa099cdf5f0f0e2bcull},
    {0x2ec93d0, 0x47, 0x14963bd760dd339aull},
    {0x2ef0778, 0x9c, 0x9ebbd6ecde0e562aull},
    {0x2ec1620, 0x61, 0xf014451f69278bddull},
    {0x2ec9418, 0x44, 0x9bb70d30ef8d7834ull},
    {0x2ef0814, 0x96, 0xca19c7d22e7b75f9ull},
    {0x2f1c22c, 0x56, 0x8eec25387fe73828ull},
    {0x2ec9528, 0x47, 0x14baa207e1738e5aull},
    {0x2ef42e0, 0x66, 0x82e6bda8c15c8d73ull}};
const CodePrint kMeleePrints904[] = {
    {0x2e9d884, 0x6e, 0xb63e0c8b829b0d0dull},
    {0x2eef710, 0x6c, 0xc1780c5a13d128ebull},
    {0x2ed6a64, 0xdd, 0x63a3d8a07e1e48f7ull},
    {0x2f11074, 0xd8, 0xd1ddb24a3eb5e1d2ull},
    {0x306e89c, 0x104, 0x4cdfdd85fa044ffaull},
    {0x301d3b4, 0x45, 0x57523644a9437e11ull},
    {0x146ff40, 0x50, 0x71341f795387a8ffull},
    {0x163d90, 0x6c2, 0x053387320a55e20dull}};
const CodePrint kUiPrints904[] = {
    {0x22cb2a8, 0x82, 0xa6ed6737bf2e4e83ull},
    {0x1bdfb70, 0x12e, 0xd1548a1dc462c4b2ull},
    {0x1bddb90, 0x132, 0x5d683031b86fce92ull},
    {0x22ad3dc, 0x3a, 0xde727370bf7134e4ull},
    {0x2299db0, 0x39, 0xf29e9d11d31b4e86ull},
    {0x22a7dec, 0x173, 0x832ff62389adf7faull},
    {0x22d4af8, 0x7f, 0x8ca58464071e59d7ull},
    {0x22cb32c, 0x3a, 0x8d0e6cf551903783ull},
    {0x1c38035, 0x58, 0x5c82c655cb828a0eull},
    {0x1c24144, 0x166, 0xa73b9700c872ececull},
    {0x1424718, 0x2e, 0x02081a65e0726111ull},
    {0x1442bc0, 0x41, 0x40b5e72861e3473bull},
    {0x17b5130, 0x2d, 0xdf19a84528fb6a62ull},
    {0x6653e8, 0x22, 0x84cc2258d57cb844ull},
    {0x1299686, 0x1c, 0xd25642c88d35193aull},
    {0x1c13d80, 0x6b, 0x0d48789a2df9f7ccull},
    {0x2ffc630, 0x65, 0x6144223e17747cdeull},
    {0x2ffc4b0, 0x9e, 0x508d76fb65cd23f2ull},
    {0x1c05dec, 0x1dc, 0xee4400770fc1b978ull},
    {0x1c27fac, 0x27b, 0x6e682eb009f5c1c0ull},
    {0x22c8a44, 0x90, 0x77ec91d23d95fe48ull},
    {0x22c8b00, 0x7f, 0x07eeca7c85c04c90ull},
    {0x22c8ad4, 0x29, 0xaade64b6ef7da547ull},
    {0x22ba4dc, 0x99, 0x1b34a4da02df276dull}};
const CodePrint kShotPrints904[] = {
    {0x2ecfd54, 0x2cb, 0x71908907c3d35b29ull},
    {0x306afbc, 0x84, 0x61b3d45859cc33c2ull},
    {0x307ad10, 0x6f, 0xbb653ab7126bcb43ull},
    {0x306b1a8, 0x8e4, 0x735cc3a5886923b0ull},
    {0x309b960, 0xe2, 0x2078c949a2740202ull},
    {0x3253204, 0x390, 0x963dcd7a776dc208ull},
    {0x305947c, 0x40, 0x3723d2b3015e36c0ull},
    {0x8d3ca0, 0x3, 0x58389c18233078ffull},
    {0x2f1ad84, 0x172, 0x00629d11bc269ca9ull},
    {0x15186c4, 0x2d, 0x568d96aaf3eb0794ull},
    {0x500c6c, 0xc4, 0x1f11cb2d47ce53e1ull},
    {0x500294, 0x13, 0xa18aa5532e26f9aeull},
    {0x4d7960, 0x39, 0xdf066808b9520cc0ull},
    {0x4d8580, 0x59, 0x78aac2ac213eed1cull}};
const CodePrint kMeleeHitPrints904[] = {
    {0x30a9344, 0xdc, 0x464ba6f56a355778ull},
    {0x30a8f94, 0x3ad, 0xb30056df6fb0a329ull},
    {0x30a8ddc, 0x1b6, 0x3f368ba650abe582ull},
    {0x3250ef0, 0x5d, 0x57ffe749a4a8bd11ull},
    {0x8fc238, 0x63, 0x59342328bccd2923ull},
    {0x3255c68, 0x146, 0x24bb7edc29c9ecccull},
    {0x306dbf0, 0x49, 0x12020d3518b4df41ull},
    {0x3085114, 0x1b4, 0xd8bfa118ace5cd31ull},
    {0x3253d8c, 0x656, 0xdc53d4d03a3943f8ull},
    {0x30676cc, 0x2fd, 0x3f85fad2c739bff5ull},
    {0x307fb18, 0x9a6, 0x11c52f1a3be95d62ull}};
constexpr uintptr_t kFnMeleeHitCalc904 = 0x30a9344, kFnHitInfoCtor904 = 0x3250ef0, kFnHitInfoDtor904 = 0x8fc238,
                    kFnKillResolve904 = 0x3253204, kFnCanBeHit904 = 0x306dbf0, kFnHitEffects904 = 0x3085114;
const CodePrint kNoKnockPrints904[] = {
    {0x325369c, 0x1c8, 0x7c8a1f823c6190dbull},
    {0x3253864, 0x145, 0x5976029983ed5217ull},
    {0x32539ac, 0x2da, 0x26345717ef031c0cull},
    {0x325c70c, 0x62, 0xd2c4dec66c65fbebull}};
const CodePrint kShieldGamePrints904[] = {
    {0x306b1a8, 0x8e4, 0x735cc3a5886923b0ull},
    {0x305947c, 0x57, 0x43af29536f3defa7ull},
    {0x3253d8c, 0x656, 0xdc53d4d03a3943f8ull},
    {0x32654d0, 0x708, 0xe253d33a7cb2f415ull},
    {0x307517c, 0x68, 0xc7cbd99e65c01a48ull},
    {0x306ee40, 0x296, 0xa637304bb565fab0ull},
    {0x3075b20, 0xa8, 0x24bab350ff394161ull},
    {0x3252ed0, 0xb1, 0x63f194fe2dc7f0a0ull},
    {0x310157c, 0x1e, 0x891955780f4d6a5eull},
    {0x8d3ca0, 0x3, 0x58389c18233078ffull}};
constexpr uintptr_t kVtSoldier904 = 0x3912950, kFnProjHit904 = 0x306b1a8, kFnProjCanHit904 = 0x305947c, kFnRet0Of1c8_904 = 0x8d3ca0,
                    kFnHitReact904 = 0x307517c, kFnStagger904 = 0x306ee40, kMeleeRet1_904 = 0x3253e70, kMeleeRet2_904 = 0x326565e;
const CodePrint kArmorPrints904[] = {
    {0x306ee40, 0x296, 0xa637304bb565fab0ull},
    {0x306b1a8, 0x8e4, 0x735cc3a5886923b0ull},
    {0x307517c, 0x68, 0xc7cbd99e65c01a48ull}};
const CodePrint kAbilityPrints904[] = {
    {0x2f008d0, 0x27b, 0x594cd05ca9272710ull},
    {0x2ea94e8, 0xa0, 0x744ebda3b94ebcedull},
    {0x2e9df94, 0x2a, 0xfebdfc7ea7568f1dull},
    {0x1bc2ff0, 0x6e, 0x5d2cb85accdf2e34ull},
    {0x2eed7a8, 0x136, 0x3b941bd6a61e91b8ull},
    {0x1bccb8c, 0x22, 0xb3fc638559b54daeull},
    {0x1535340, 0x2d, 0xe8700aef872daaeaull},
    {0x2ed1478, 0x1b0, 0x5ccc3fdb5f7825f2ull},
    {0x2f35c64, 0x278, 0xcf324f891c88c2a4ull},
    {0x3037d78, 0x50c, 0x21c1d1989d8dc5e2ull},
    {0x2d93918, 0x1ab, 0xe6393eae6bb589caull},
    {0x2d9334c, 0x77, 0x3f34f133ef2c7aa9ull}};
constexpr uintptr_t kFnAbilityTable904 = 0x1535340, kFnAbilityGroundCtor904 = 0x2ea94e8, kFnAbilityIssue904 = 0x1bc2ff0,
                    kVtAbilityCmd904 = 0x39071f8, kFnAbilityCmdWrite904 = 0x2eed7a8;
const CodePrint kCharPrints904[] = {
    {0x1e8392c, 0x25, 0x909bd734bb5f100bull},
    {0x4d3970, 0x2e, 0xb416bbb3274728dcull},
    {0x8e2880, 0x0c, 0xf55c7e50acf69be9ull},
    {0x1e86204, 0x11, 0x8aa88ae5c72a99d6ull}};
constexpr uintptr_t oUnitCharDetails = 0x158, oCharKeyStr = 0x4e8, oUiUnitIsGeneral = 0x6fa;
const CodePrint kFlyPrints904[] = {
    {0x8e2420, 0xc, 0x7a3750e9a30ac0d2ull},
    {0x8e2430, 0xc, 0x62ce00c3f2a8c2afull},
    {0x8e2440, 0xc, 0xc7052de21b161035ull},
    {0x8e2860, 0xc, 0x13d482da9323de80ull},
    {0x2ea9358, 0x38, 0xf662140a1ee931baull},
    {0x1e3f410, 0x6e, 0x231ab3a1d31f4b75ull},
    {0x2ed1160, 0x62, 0x4c0cf35d628b1a09ull},
    {0x2f4ae00, 0xe5, 0x9df247d2bf9b1367ull}};
const CodePrint kRoutPrints904[] = {
    {0x2eeb33c, 0x52, 0xe308885f1334b146ull},
    {0x2eeb570, 0x4b, 0x598a680916059c32ull},
    {0x30348f0, 0x2f, 0x0fe36b149bafd51full},
    {0x303482c, 0x53, 0x1dd38af0a1bce2e1ull}};
constexpr uintptr_t kFnUnitRouting904 = 0x30348f0, oUnitMorale = 0x3b30, oMoraleState = 0x40;
constexpr uintptr_t oUiCanFly = 0x70f, oUiAlwaysFly = 0x710, oUiCanToggleFly = 0x711, oUiIsFlying = 0x712;
constexpr uintptr_t kFnFlyCmdCtor904 = 0x2ea9358, kFnFlyCmdIssue904 = 0x1e3f410;
bool g_flyOk = false;
const CodePrint kWallPrints904[] = {
    {0x2dc67a8, 0x1ab, 0xb8fa363f9006326full},
    {0x30b3cf8, 0xa3, 0x73b7107b30625308ull},
    {0x183327c, 0x3a, 0x0210c7b5522ef8adull},
    {0x2ddb148, 0x91, 0xec20489e15abcf80ull},
    {0x2ed623c, 0x13b, 0x43539aee1c8b6d43ull}};
constexpr uintptr_t kFnNavEntryValid904 = 0x30b3cf8, kFnBlockContains904 = 0x183327c;
constexpr uintptr_t oWorldNavEntries = 0x108, oWorldNavGrid = 0x138, oBlkPoly = 0x10, oBlkPlane = 0x6c;
bool g_wallOk = false;
const CodePrint kBuildPrints904[] = {
    {0x2db3470, 0x1e0, 0x3c8867f0744bc1a9ull},
    {0x30a6f80, 0x529, 0xd8d80fffc173bed8ull},
    {0x2f6eb34, 0xb78, 0xd1396ee29531e84full},
    {0x316d79c, 0x295, 0x1986cd8c729b5bf0ull},
    {0x1bc048c, 0xd9, 0xa04e459ecba56bccull}};
constexpr uintptr_t kFnSegQuery904 = 0x2db3470, oWorldCollide = 0x150;
constexpr uint32_t kBuildMask = 0x4800;
bool g_buildOk = false;
bool g_netOk = false;
const CodePrint kGameVolPrints904[] = {
    {0x5af310, 0x161, 0x0caa95b7b9f64779ull},
    {0x5aecd4, 0x16, 0x3a3f3a78d580c508ull},
    {0x5aecec, 0x1b, 0xa5e7a245781999a6ull},
    {0x1b4836c, 0x16, 0x03a64b3be52a277aull},
    {0x8c8db4, 0x65, 0xdd8e36b575767c89ull}};
constexpr uintptr_t kGlobalAudioMgr904 = 0x3de7f18, kVtAudioMgr904 = 0x34bdd60, kFnAudioSetOn904 = 0x5aecec, kFnAudioSetVol904 = 0x5aecd4,
                    kFnAudioSetScale904 = 0x1b4836c;
bool g_gameVolOk = false;
constexpr uintptr_t oUnitAbilityMgr = 0x3d48, oAbMgrEnd = 0x20, oAbMgrFirst = 0x28, oAbNodeNext = 0x8, oAbNodeDef = 0x70, oAbNodeInst = 0x10;
constexpr uintptr_t oAbDefId = 0x8, oAbDefWindup = 0x30, oAbDefRange = 0x54, oAbRecId = 0x50;
constexpr uintptr_t oAbWindSteps = 0x120, oAbCooldown = 0x134, oAbUses = 0xb4, oAbFlags = 0x170;
const unsigned char kPresentHelper904[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x33,
                                           0xDB, 0x48, 0x8B, 0xF9, 0x38, 0x1D, 0x07, 0x3E, 0xED, 0x03, 0x48,
                                           0x8B, 0x09, 0x0F, 0x85, 0x98, 0x9E, 0xCC, 0x00, 0x4C, 0x8B, 0x89,
                                           0x50, 0x13, 0x00, 0x00, 0x49, 0x8B, 0x01, 0x4C, 0x8B, 0x50, 0x40};
const unsigned char kGlobalWrite904[] = {0x48, 0x89, 0x05, 0xE9, 0xB4, 0x84, 0x03};
struct GameAddrs {
    const char* name;
    DWORD stamp, size;
    uintptr_t globalUi, vtCamMgr, fnCamMgrUpdate, fnCamSetPos, fnCamSetTarget, fnCamSetUp, fnCamSetFov, fnCamSetNear,
        fnCamSetFar;
    uintptr_t globalScript, fnIssueFire, vtFireCmd, fnFireCmdWrite;
    uintptr_t fnSelCtor, fnSelAdd, fnSelCopy, fnSelDtor, fnSizeParam, fnMakeDest, fnMoveCtor, fnIssueMove, fnIssueHalt,
        vtHaltCmd;
    uintptr_t typeOvrMove, typeOvrHalt, typeOvrTeleport, regOvrMove, regOvrHalt, regOvrTeleport, fnOvrMoveFactory,
        fnOvrHaltFactory, fnOvrTeleportFactory, fnCmdRegister, fnCmdDesc, fnCmdBegin, fnCmdEnd, fnBsWrite, fnDestWrite,
        vtOvrTeleport, fnOvrTeleportStart;
    uintptr_t vtUnitEnableCmd, fnUnitEnableWrite, fnScriptIssueEnable;
    uintptr_t vtUnitVisibleCmd, fnUnitVisibleWrite, fnScriptIssueVisible;
    uintptr_t vtStatAttrCmd, fnStatAttrWrite, fnIssueStatAttr;
    uintptr_t fnUicSetVisible, globalUiRoot, fnSetCursorEnabled, globalCursorEnabled, fnSetCursorVisible, globalCursorVisible;
    uintptr_t fnRetZero;
    uintptr_t fnProjTable, fnTableFind, fnTableAt, fnStrCtor, fnStrDtor;
    PrintSet cam, fire, move, cmd, ovr, halt, tp, ghost, reveal, melee, ui, shot;
    vmgame::RenderAddrs render;
};
const GameAddrs kGames[] = {
    {"9.0.4", 0x6acec6f4, 0xe029000,
                0x4480d10, 0x3ab68f0, 0x2f1dc80, 0x181c8ac, 0x181c91c, 0x181c990, 0x8d65c0, 0x8d6560, 0x8d6530,
                0x4480da0, 0x2e9b9e4, 0x3ab9018, 0x2eecf1c,
                      0x2f2efc8, 0x2f37be4, 0x2da2cd8, 0x1bcd8ac, 0x2f53ff4, 0x310cfac, 0x2ea9a94, 0x1cae60c, 0x1bc31b0, 0x39072c8,
                      0x406c798, 0x406cea8, 0x406cd30, 0x4ae020, 0x4adfe0, 0x4ae040, 0x2ecfb0c, 0x2ecf98c, 0x2ecfbd4, 0x668a14,
     0x668ad4, 0x1bca278, 0x1bcd5e4, 0x1bb1924, 0x2eafa2c, 0x390b5e8, 0x2f78b30,
                      0x3905f40, 0x2eef630, 0x2e9e1d0,
                      0x3907288, 0x2eef54c, 0x2e9e178,
                   0x3907220, 0x2eef710, 0x2e9d884,
                0x22cb2a8, 0x4480d48, 0x1424718, 0x3d0e380, 0x6653e8, 0x3d0e378,
                      0x8d3ca0,
                      0x15186c4, 0x500c6c, 0x500294, 0x4d7960, 0x4d8580,
     Prints(kCamPrints904), Prints(kFirePrints904), Prints(kMovePrints904), Prints(kCmdPrints904), Prints(kOvrPrints904),
     Prints(kHaltPrints904), Prints(kTpPrints904), Prints(kGhostPrints904), Prints(kRevealPrints904),
     Prints(kMeleePrints904), Prints(kUiPrints904), Prints(kShotPrints904),
                   {0x3de7a28, 0x33d6a78, 0x120, 0x582ae4, 0x5acedc, 0x5acf21, 0x59c538, 0x59c69b, 0x1350, kPresentHelper904,
                sizeof kPresentHelper904, kGlobalWrite904, sizeof kGlobalWrite904,
                                                      0x208, 0x583710, 0x4128, 0x1988b7c,
                                                                 0x3de7ed0, 0x1d0, 0x650, 0x3a15298, 0x1a362e0, 0x16b488, 0x19a6ba9, 0x1a402ae, 0x19404cc,
                                                                          0x3a0e568, 0x3a0cdb8, 0x1a15804, 0x18c3268, 0x1a159cd, 0x19f4bc4}},
};
const GameAddrs* g_game = nullptr;
uintptr_t kGlobalUi, kVtCamMgr, kFnCamMgrUpdate, kFnCamSetPos, kFnCamSetTarget, kFnCamSetUp, kFnCamSetFov, kFnCamSetNear,
    kFnCamSetFar;
uintptr_t kGlobalScript, kFnIssueCmd, kVtFireCmd, kFnFireCmdWrite;
uintptr_t kFnSelCtor, kFnSelAdd, kFnSelCopy, kFnSelDtor, kFnSizeParam, kFnMakeDest, kFnMoveCtor, kFnIssueMove, kFnIssueHalt,
    kVtHaltCmd;
uintptr_t kTypeOvrMove, kTypeOvrHalt, kTypeOvrTeleport, kRegOvrMove, kRegOvrHalt, kRegOvrTeleport, kFnOvrMoveFactory,
    kFnOvrHaltFactory, kFnOvrTeleportFactory, kFnCmdRegister, kFnCmdDesc, kFnCmdBegin, kFnCmdEnd, kFnBsWrite, kFnDestWrite,
    kVtOvrTeleport, kFnOvrTeleportStart;
uintptr_t kVtUnitEnableCmd, kFnUnitEnableWrite, kFnScriptIssueEnable;
uintptr_t kVtUnitVisibleCmd, kFnUnitVisibleWrite, kFnScriptIssueVisible;
uintptr_t kVtStatAttrCmd, kFnStatAttrWrite, kFnIssueStatAttr;
uintptr_t kFnUicSetVisible, kGlobalUiRoot, kFnSetCursorEnabled, kGlobalCursorEnabled, kFnSetCursorVisible, kGlobalCursorVisible;
uintptr_t kFnRetZero;
uintptr_t kFnProjTable, kFnTableFind, kFnTableAt, kFnStrCtor, kFnStrDtor;
void UseGame(const GameAddrs& a) {
    g_game = &a;
    kGlobalUi = a.globalUi, kVtCamMgr = a.vtCamMgr, kFnCamMgrUpdate = a.fnCamMgrUpdate, kFnCamSetPos = a.fnCamSetPos;
    kFnCamSetTarget = a.fnCamSetTarget, kFnCamSetUp = a.fnCamSetUp, kFnCamSetFov = a.fnCamSetFov;
    kFnCamSetNear = a.fnCamSetNear, kFnCamSetFar = a.fnCamSetFar;
    kGlobalScript = a.globalScript, kFnIssueCmd = a.fnIssueFire, kVtFireCmd = a.vtFireCmd, kFnFireCmdWrite = a.fnFireCmdWrite;
    kFnSelCtor = a.fnSelCtor, kFnSelAdd = a.fnSelAdd, kFnSelCopy = a.fnSelCopy, kFnSelDtor = a.fnSelDtor;
    kFnSizeParam = a.fnSizeParam, kFnMakeDest = a.fnMakeDest, kFnMoveCtor = a.fnMoveCtor, kFnIssueMove = a.fnIssueMove;
    kFnIssueHalt = a.fnIssueHalt, kVtHaltCmd = a.vtHaltCmd;
    kTypeOvrMove = a.typeOvrMove, kTypeOvrHalt = a.typeOvrHalt, kTypeOvrTeleport = a.typeOvrTeleport;
    kRegOvrMove = a.regOvrMove, kRegOvrHalt = a.regOvrHalt, kRegOvrTeleport = a.regOvrTeleport;
    kFnOvrMoveFactory = a.fnOvrMoveFactory, kFnOvrHaltFactory = a.fnOvrHaltFactory;
    kFnOvrTeleportFactory = a.fnOvrTeleportFactory, kFnCmdRegister = a.fnCmdRegister;
    kFnCmdDesc = a.fnCmdDesc, kFnCmdBegin = a.fnCmdBegin, kFnCmdEnd = a.fnCmdEnd, kFnBsWrite = a.fnBsWrite;
    kFnDestWrite = a.fnDestWrite, kVtOvrTeleport = a.vtOvrTeleport, kFnOvrTeleportStart = a.fnOvrTeleportStart;
    kVtUnitEnableCmd = a.vtUnitEnableCmd, kFnUnitEnableWrite = a.fnUnitEnableWrite, kFnScriptIssueEnable = a.fnScriptIssueEnable;
    kVtUnitVisibleCmd = a.vtUnitVisibleCmd, kFnUnitVisibleWrite = a.fnUnitVisibleWrite;
    kFnScriptIssueVisible = a.fnScriptIssueVisible;
    kVtStatAttrCmd = a.vtStatAttrCmd, kFnStatAttrWrite = a.fnStatAttrWrite, kFnIssueStatAttr = a.fnIssueStatAttr;
    kFnUicSetVisible = a.fnUicSetVisible, kGlobalUiRoot = a.globalUiRoot;
    kFnSetCursorEnabled = a.fnSetCursorEnabled, kGlobalCursorEnabled = a.globalCursorEnabled;
    kFnSetCursorVisible = a.fnSetCursorVisible, kGlobalCursorVisible = a.globalCursorVisible;
    kFnRetZero = a.fnRetZero;
    kFnProjTable = a.fnProjTable, kFnTableFind = a.fnTableFind, kFnTableAt = a.fnTableAt;
    kFnStrCtor = a.fnStrCtor, kFnStrDtor = a.fnStrDtor;
}
constexpr uintptr_t oUiBattle = 0x418, oUiSelection = 0x13b8, oSelCount = 0x1bc, oSelArray = 0x1c0,
                    oUiUnitBattleUnit = 0x758;
constexpr uintptr_t oBattleCamMgr = 0x64558, oBattleFpCtrl = 0x647a0;
constexpr uintptr_t oMgrBattle = 0x3e0, oMgrActive = 0xc30, oMgrTransition = 0xc10, oMgrSpecial = 0xc48;
constexpr uintptr_t oFpCtrlCamera = 0x60, oFpCtrlEntity = 0x518;
constexpr uintptr_t oCamPos = 0x8, oCamTarget = 0x14, oCamFov = 0x24, oCamNear = 0x34, oCamFar = 0x38,
                    oCamDone = 0x3f4;
constexpr uintptr_t oUnitEntCount = 0x134, oUnitEntArray = 0x138, oEntPos = 0x88;
constexpr uintptr_t vtSetPos = 0xb8, vtSetTarget = 0xc0, vtSetUp = 0xc8, vtSetFov = 0xd0, vtSetNear = 0x90,
                    vtSetFar = 0x98, vtMgrUpdate = 0x298;
constexpr size_t kVtCopySlots = 0x100;
constexpr float kPi = 3.14159265358979f;
constexpr uintptr_t oEntRecord = 0x2d8, oRecHeight = 0x6c;
float g_leadAdj[4] = {0.f, 0.f, -0.05f, -0.05f};
int LeadBand(float v) { return v < 4.5f ? 0 : v < 8.f ? 1 : v < 11.f ? 2 : 3; }
float LeadAutoStep(float adj, float d, float v, float dt, float tau, float lo, float hi) {
    if (!(v > 0.5f) || !(dt > 0.f) || !std::isfinite(d)) return adj;
    float a = adj - (d / v) * (1.f - std::exp(-dt / (tau > 0.05f ? tau : 0.05f)));
    return a < lo ? lo : a > hi ? hi : a;
}
float EyeFromHeight(float h, float def) {
    if (!(h > 0.3f && h < 30.f)) return def;
    if (h < 1.95f) return std::fmax(0.6f, def * h / 1.95f);
    if (h > 2.5f) return def + (h - 2.5f) * 0.9f;
    return def;
}
constexpr uintptr_t oScriptBattle = 0x468, oBattleCmdQueue = 0x644e8, oBattleNoCmd1 = 0x3b0, oBattleNoCmd2 = 0x3b1;
constexpr uintptr_t oUnitProjectile = 0x3d50;
constexpr uintptr_t oEntId = 0x258;
struct FireCmd {
    uintptr_t vt;
    int32_t projId;
    float f0c;
    float launch[3];
    float pad1c;
    float target[3];
    float pad2c;
    uint64_t tgtRef[2];
    uint8_t tgtIsPos;
    uint8_t pad41[7];
    uintptr_t shooter;
    float f50;
    uint32_t pad54;
};
static_assert(sizeof(FireCmd) == 0x58, "fire cmd size");
static_assert(__builtin_offsetof(FireCmd, target) == 0x20 && __builtin_offsetof(FireCmd, tgtIsPos) == 0x40 &&
                  __builtin_offsetof(FireCmd, shooter) == 0x48,
              "fire cmd layout");
bool g_fireOk = false;
constexpr uintptr_t oBattlePath = 0x440, oPathMgr = 0x160;
constexpr uintptr_t oSelUnitCount = 0x1c;
bool g_moveOk = false;
struct alignas(16) SelBuf {
    unsigned char b[0x80];
};
struct alignas(16) MoveCmdBuf {
    unsigned char b[0xb0];
};
struct alignas(16) HaltCmdBuf {
    unsigned char b[0x90];
};
bool g_tpOk = false;
bool g_ovrOk = false, g_haltOk = false;
constexpr uintptr_t oEntActivity = 0x324;
struct EnableCmd {
    uintptr_t vt;
    uintptr_t unit;
    uint8_t enabled;
    uint8_t pad[15];
};
bool g_ghostOk = false;
struct VisibleCmd {
    uintptr_t vt;
    uintptr_t unit;
    uint8_t visible;
    uint8_t noHidden;
    uint8_t noLeave;
    uint8_t pad[13];
};
constexpr uintptr_t oWorldAlliances = 0x150, oAlliancesCount = 0x134, oAlliancesArray = 0x138;
constexpr uintptr_t oAllianceArmyCount = 0x34, oAllianceArmyArray = 0x38, oArmyUnitCount = 0x164, oArmyUnitArray = 0x168;
constexpr uintptr_t oUnitVisState = 0x3d38;
constexpr int32_t kVisAlways = 1, kVisNormal = 5;
constexpr unsigned kMaxRevealed = 512;
bool g_revealOk = false;
struct StatAttrCmd {
    uintptr_t vt;
    uintptr_t unit;
    int32_t attr;
    uint8_t on;
    uint8_t pad[11];
};
constexpr uintptr_t oUnitAttrScript = 0x36f0;
constexpr int32_t kAttrMeleeDisabled = 0x2a;
bool g_meleeOk = false;
bool g_shotOk = false;
bool g_meleeHitOk = false;
bool g_noKnockOk = false;
bool g_shieldGameOk = false;
bool g_armorOk = false;
bool g_abilityOk = false;
bool g_charOk = false;
bool g_arrowOk = false;
bool g_routOk = false;
constexpr uintptr_t oUiHudRoot = 0x358, oUicChildCount = 0x1cc, oUicChildren = 0x1d0, oUicId = 0x2a0, oUicVisible = 0x353;
constexpr int kMaxHideUi = 12;
constexpr uintptr_t oUicPos = 0x2c0, oUicLayout = 0x2f0, oLayoutW = 0x11c, oLayoutH = 0x120;
bool g_uiOk = false;
struct Config {
    bool enabled = true;
    UINT toggleKey = VK_F6;
    float eyeHeight = 1.75f;
    float eyeForward = 0.12f;
    float nearClip = 0.1f;
    float fovOverride = 0.0f;
    float sensitivity = 0.07f;
    bool invertY = false;
    int logIntervalMs = 1000;
    bool fireEnabled = true;
    float fireInterval = 0.1f;
    float fireRange = 120.0f;
    float fireForward = 0.6f;
    bool fireShooter = true;
    bool unlimitedAmmo = true;
    float bulletSpeed = 2000.f;
    char fireProjectile[64] = "tenzin_fps_rifle_projectile";
    float muzzleDepth = 1.2f;
    float fireClearance = 1.6f;
    float fireLookahead = 0.2f;
    float pinnedClearance = 1.0f;
    float tpShotGuard = 1.1f;
    bool fastFire = true;
    bool earlyFlight = true;
    bool autoLead = true;
    float leadExtra = 0.f;
    float leadScale = 1.f;
    float displayLag = 0.5f;
    bool hitProbe = false;
    bool moveEnabled = true;
    int moveMode = 0;
    bool moveOverride = true;
    float runSpeed = 6.0f;
    float walkSpeed = 2.5f;
    float sprintSpeed = 10.0f;
    float tacStamina = 15.f;
    float tacStaminaRegen = 5.f;
    float tacStaminaDelay = 1.f;
    float tacStaminaMin = 0.25f;
    float tacSprintSpeed = 12.5f;
    float tacSprintToFire = 0.3f;
    float adsMoveScale = 0.6f;
    float sprintToFire = 0.2f;
    float adsTimeSprint = 0.35f, adsTimeTac = 0.55f;
    float adsZoom = 1.25f;
    float adsTime = 0.25f;
    float accelTau = 0.06f;
    float tpInterval = 0.1f;
    float leash = 3.0f;
    float stuckLeash = 0.6f;
    float tpLead = 0.2f;
    bool leadAuto = true, eyeFromHeight = true;
    bool hideSelf = true;
    bool hideArrows = true;
    float leadAutoTau = 1.5f;
    bool wall = true;
    float stepUp = 0.6f, stepDown = 0.8f, gravity = 20.3f, jumpHeight = 0.f, airControl = 0.35f, airTpMax = 1.5f, landDip = 0.12f;
    float dropClearance = 3.5f, dropPushSpeed = 6.f, dropMaxPush = 6.f;
    bool building = true;
    float bldLow = 1.f, bldHigh = 2.05f, bldSkin = 0.4f;
    bool wallUp = true;
    unsigned wallUpKey = 'E';
    float wallUpReach = 3.f, wallUpMin = 3.f, wallUpMaxH = 30.f, wallUpGateR = 8.f, wallUpBase = 3.f;
    bool wallBody = true;
    float wallBodyMargin = 1.f;
    bool wallBodyNet = true;
    bool ghost = false;
    bool revealEnemies = true;
    bool noMelee = true;
    bool noKnock = true;
    bool superArmor = true;
    bool heroesOnly = true;
    bool fly = true, flyTakeoff = true;
    float flyAhead = 60.f, flyInterval = 2.0f, flyTurnDeg = 20.f, flyEyeHeight = 1.2f;
    bool shieldGame = true;
    bool shieldStagger = true;
    float shieldAngle = 180.f;
    bool shieldRefund = true;
    bool hideHud = true;
    bool hideCursor = true;
    bool hidePaths = true;
    bool hideEdgeArrows = true;
    bool hudEnabled = true;
    bool castKeys = true;
    bool castMarker = true;
    float castMarkerPillar = 6.f;
    bool hudCover = true;
    char hideNames[kMaxHideUi][48] = {};
    int hideCount = 0;
    float moveInterval = 0.2f;
    float moveAhead = 8.0f;
    float moveTurnDeg = 25.0f;
    bool predict = true;
    float predictSpeed = 6.0f;
    float predictTau = 0.35f;
    float predictMax = 2.0f;
    bool meleeEngine = true;
    bool meleeAlwaysHit = true;
    float meleeDamage = 1.0f;
    bool meleeReact = true;
    bool meleeEffects = true;
    float meleeFallback = 60.f;
    float meleeSplashRadius = 1.5f;
    float meleeSplashDamage = 0.5f;
    int meleeSplashMax = 6;
    float meleeHeavySplashRadius = 2.5f;
    float meleeHeavySplashDamage = 0.6f;
    int meleeHeavySplashMax = 12;
    float meleeHeavyReach = 1.3f;
} cfg;
wchar_t g_dir[MAX_PATH];
uintptr_t g_base = 0;
bool g_coreOk = false;
enum DllStatus : LONG {
    kStatusStarting = 0, kStatusReady = 1,
    kStatusOldProxy = 2,
    kStatusOtherCopy = 3,
    kStatusAssets = 4,
    kStatusVersion = 5,
    kStatusSignatures = 6,
    kStatusDisabled = 7,
    kStatusNoWindow = 8
};
enum RefuseCode : LONG {
    kRefuseSelect = 1,
    kRefuseNotHero = 2,
    kRefuseNotOwner = 3,
    kRefusePaused = 4,
    kRefuseArtillery = 5,
    kRefuseOther = 6,
    kRefuseRouting = 7
};
volatile LONG g_status = kStatusStarting;
volatile LONG g_refuseSeq = 0, g_refuseCode = 0;
void Refuse(LONG code) {
    InterlockedExchange(&g_refuseCode, code);
    InterlockedIncrement(&g_refuseSeq);
}
CRITICAL_SECTION g_logLock;
FILE* g_log = nullptr;
__attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2))) void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d t%lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
            GetCurrentThreadId());
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}
void OpenLog() {
    InitializeCriticalSection(&g_logLock);
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
    if (n && n < MAX_PATH - 80) {
        wcscat(path, L"\\The Creative Assembly\\Warhammer3\\logs\\tenzin_fps_native.log");
        g_log = _wfopen(path, L"a");
    }
    if (!g_log) {
        wcscpy(path, g_dir);
        wcscat(path, L"\\tenzin_fps_native.log");
        g_log = _wfopen(path, L"a");
    }
}
template <class T>
bool Rd(uintptr_t a, T& out) {
    if (a < 0x10000) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(a), &out, sizeof(T), &got) &&
           got == sizeof(T);
}
uintptr_t Ptr(uintptr_t a) {
    uintptr_t v = 0;
    return Rd(a, v) ? v : 0;
}
template <class T>
bool Wr(uintptr_t a, const T& v) {
    if (a < 0x10000) return false;
    SIZE_T n = 0;
    return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(a), &v, sizeof(T), &n) &&
           n == sizeof(T);
}
uintptr_t g_imageSize = 0;
bool InExe(uintptr_t p) { return p >= g_base && p < g_base + g_imageSize; }
float IniF(const wchar_t* sec, const wchar_t* key, float def, const wchar_t* ini) {
    wchar_t buf[64];
    GetPrivateProfileStringW(sec, key, L"", buf, 64, ini);
    if (!buf[0]) return def;
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    return (end != buf && std::isfinite(v)) ? static_cast<float>(v) : def;
}
void LoadMeleeCats(const wchar_t* ini);
extern float g_meleeTargetRadiusMax;
LONG g_cfgGen = 0;
void LoadConfig() {
    wchar_t ini[MAX_PATH];
    wcscpy(ini, g_dir);
    wcscat(ini, L"\\tenzin_fps.ini");
    InterlockedIncrement(&g_cfgGen);
    cfg.enabled = GetPrivateProfileIntW(L"general", L"enabled", 1, ini) != 0;
    UINT key = GetPrivateProfileIntW(L"general", L"toggle_key", VK_F6, ini);
    cfg.toggleKey = (key > 0 && key < 0xFF) ? key : VK_F6;
    cfg.eyeHeight = IniF(L"camera", L"eye_height", cfg.eyeHeight, ini);
    cfg.eyeForward = IniF(L"camera", L"eye_forward", cfg.eyeForward, ini);
    cfg.nearClip = IniF(L"camera", L"near_clip", cfg.nearClip, ini);
    cfg.fovOverride = IniF(L"camera", L"fov_override", cfg.fovOverride, ini);
    cfg.sensitivity = IniF(L"mouse", L"sensitivity", cfg.sensitivity, ini);
    cfg.invertY = GetPrivateProfileIntW(L"mouse", L"invert_y", 0, ini) != 0;
    cfg.logIntervalMs = GetPrivateProfileIntW(L"debug", L"log_interval_ms", 1000, ini);
    cfg.fireEnabled = GetPrivateProfileIntW(L"fire", L"enabled", 1, ini) != 0;
    cfg.fireInterval = IniF(L"fire", L"interval", cfg.fireInterval, ini);
    cfg.fireRange = IniF(L"fire", L"range", cfg.fireRange, ini);
    cfg.fireForward = IniF(L"fire", L"launch_forward", cfg.fireForward, ini);
    if (cfg.fireInterval < 0.03f || cfg.fireInterval > 5.f) cfg.fireInterval = 0.1f;
    if (cfg.fireRange < 5.f || cfg.fireRange > 2000.f) cfg.fireRange = 120.f;
    if (cfg.fireForward < 0.f || cfg.fireForward > 5.f) cfg.fireForward = 0.6f;
    cfg.fireShooter = GetPrivateProfileIntW(L"fire", L"shooter", 1, ini) != 0;
    cfg.unlimitedAmmo = GetPrivateProfileIntW(L"fire", L"unlimited_ammo", 1, ini) != 0;
    {
        wchar_t w[64] = {};
        GetPrivateProfileStringW(L"fire", L"projectile", L"tenzin_fps_rifle_projectile", w, 64, ini);
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, cfg.fireProjectile, sizeof cfg.fireProjectile, nullptr, nullptr);
        if (n <= 0) cfg.fireProjectile[0] = 0;
    }
    cfg.bulletSpeed = IniF(L"fire", L"bullet_speed", cfg.bulletSpeed, ini);
    if (cfg.bulletSpeed != 0.f && (cfg.bulletSpeed < 50.f || cfg.bulletSpeed > 10000.f)) cfg.bulletSpeed = 2000.f;
    cfg.muzzleDepth = IniF(L"fire", L"muzzle_depth", cfg.muzzleDepth, ini);
    cfg.fireClearance = IniF(L"fire", L"clearance", cfg.fireClearance, ini);
    if (cfg.muzzleDepth < 0.2f || cfg.muzzleDepth > 5.f) cfg.muzzleDepth = 1.2f;
    if (cfg.fireClearance < 0.f || cfg.fireClearance > 5.f) cfg.fireClearance = 1.6f;
    cfg.fireLookahead = IniF(L"fire", L"clearance_lookahead", cfg.fireLookahead, ini);
    if (cfg.fireLookahead < 0.f || cfg.fireLookahead > 1.f) cfg.fireLookahead = 0.2f;
    cfg.moveEnabled = GetPrivateProfileIntW(L"move", L"enabled", 1, ini) != 0;
    {
        wchar_t mode[32];
        GetPrivateProfileStringW(L"move", L"mode", L"", mode, 32, ini);
        if (!_wcsicmp(mode, L"unit"))
            cfg.moveMode = 1;
        else if (!_wcsicmp(mode, L"entity"))
            cfg.moveMode = 2;
        else if (!mode[0] && GetPrivateProfileIntW(L"move", L"override", -1, ini) == 0)
            cfg.moveMode = 1;
        else
            cfg.moveMode = 0;
        cfg.moveOverride = cfg.moveMode == 2;
    }
    cfg.runSpeed = IniF(L"move", L"run_speed", cfg.runSpeed, ini);
    cfg.walkSpeed = IniF(L"move", L"walk_speed", cfg.walkSpeed, ini);
    cfg.sprintSpeed = IniF(L"move", L"sprint_speed", cfg.sprintSpeed, ini);
    cfg.adsMoveScale = IniF(L"move", L"ads_move_scale", cfg.adsMoveScale, ini);
    cfg.sprintToFire = IniF(L"fire", L"sprint_to_fire", cfg.sprintToFire, ini);
    cfg.tacSprintSpeed = IniF(L"move", L"tac_sprint_speed", cfg.tacSprintSpeed, ini);
    cfg.tacSprintToFire = IniF(L"fire", L"tac_sprint_to_fire", cfg.tacSprintToFire, ini);
    cfg.tacStamina = IniF(L"move", L"tac_stamina", cfg.tacStamina, ini);
    cfg.tacStaminaRegen = IniF(L"move", L"tac_stamina_regen", cfg.tacStaminaRegen, ini);
    cfg.tacStaminaDelay = IniF(L"move", L"tac_stamina_delay", cfg.tacStaminaDelay, ini);
    cfg.tacStaminaMin = IniF(L"move", L"tac_stamina_min", cfg.tacStaminaMin, ini);
    if (!(cfg.tacStamina > 0.2f && cfg.tacStamina < 600.f)) cfg.tacStamina = 15.f;
    if (!(cfg.tacStaminaRegen > 0.2f && cfg.tacStaminaRegen < 600.f)) cfg.tacStaminaRegen = 5.f;
    if (!(cfg.tacStaminaDelay >= 0.f && cfg.tacStaminaDelay < 30.f)) cfg.tacStaminaDelay = 1.f;
    if (!(cfg.tacStaminaMin >= 0.f && cfg.tacStaminaMin <= 1.f)) cfg.tacStaminaMin = 0.25f;
    if (cfg.tacSprintSpeed < 0.5f || cfg.tacSprintSpeed > 40.f) cfg.tacSprintSpeed = 12.5f;
    if (cfg.tacSprintToFire < 0.f || cfg.tacSprintToFire > 2.f) cfg.tacSprintToFire = 0.3f;
    cfg.adsZoom = IniF(L"camera", L"ads_zoom", cfg.adsZoom, ini);
    cfg.adsTime = IniF(L"camera", L"ads_time", cfg.adsTime, ini);
    cfg.adsTimeSprint = IniF(L"camera", L"ads_time_sprint", cfg.adsTimeSprint, ini);
    cfg.adsTimeTac = IniF(L"camera", L"ads_time_tac", cfg.adsTimeTac, ini);
    if (cfg.adsTimeSprint < 0.01f || cfg.adsTimeSprint > 2.f) cfg.adsTimeSprint = 0.35f;
    if (cfg.adsTimeTac < 0.01f || cfg.adsTimeTac > 2.f) cfg.adsTimeTac = 0.55f;
    if (cfg.sprintSpeed < 0.5f || cfg.sprintSpeed > 40.f) cfg.sprintSpeed = 10.f;
    if (cfg.adsMoveScale < 0.05f || cfg.adsMoveScale > 1.f) cfg.adsMoveScale = 0.6f;
    if (cfg.sprintToFire < 0.f || cfg.sprintToFire > 2.f) cfg.sprintToFire = 0.2f;
    if (cfg.adsZoom < 1.f || cfg.adsZoom > 8.f) cfg.adsZoom = 1.25f;
    if (cfg.adsTime < 0.01f || cfg.adsTime > 2.f) cfg.adsTime = 0.25f;
    vmgame::PublishAdsTime(cfg.adsTime, cfg.adsTimeSprint, cfg.adsTimeTac);
    cfg.accelTau = IniF(L"move", L"accel", cfg.accelTau, ini);
    cfg.tpInterval = IniF(L"move", L"teleport_interval", cfg.tpInterval, ini);
    cfg.leash = IniF(L"move", L"leash", cfg.leash, ini);
    if (cfg.runSpeed < 0.5f || cfg.runSpeed > 40.f) cfg.runSpeed = 6.f;
    if (cfg.walkSpeed < 0.2f || cfg.walkSpeed > 40.f) cfg.walkSpeed = 2.5f;
    if (cfg.accelTau < 0.f || cfg.accelTau > 1.f) cfg.accelTau = 0.06f;
    if (cfg.tpInterval < 0.03f || cfg.tpInterval > 1.f) cfg.tpInterval = 0.1f;
    if (cfg.leash < 0.5f || cfg.leash > 20.f) cfg.leash = 3.f;
    cfg.ghost = GetPrivateProfileIntW(L"hero", L"ghost", 0, ini) != 0;
    cfg.pinnedClearance = IniF(L"fire", L"pinned_clearance", cfg.pinnedClearance, ini);
    if (cfg.pinnedClearance < 0.5f || cfg.pinnedClearance > 3.f) cfg.pinnedClearance = 1.0f;
    cfg.tpShotGuard = IniF(L"fire", L"teleport_guard", cfg.tpShotGuard, ini);
    cfg.fastFire = GetPrivateProfileIntW(L"fire", L"fast_fire", 1, ini) != 0;
    cfg.earlyFlight = GetPrivateProfileIntW(L"fire", L"early_flight", 1, ini) != 0;
    cfg.autoLead = GetPrivateProfileIntW(L"fire", L"auto_lead", 1, ini) != 0;
    cfg.leadExtra = IniF(L"fire", L"lead_extra", cfg.leadExtra, ini);
    if (!(cfg.leadExtra >= -0.3f && cfg.leadExtra <= 0.5f)) cfg.leadExtra = 0.f;
    cfg.leadScale = IniF(L"fire", L"lead_scale", cfg.leadScale, ini);
    if (!(cfg.leadScale >= 0.f && cfg.leadScale <= 10.f)) cfg.leadScale = 1.f;
    cfg.displayLag = IniF(L"fire", L"display_lag", cfg.displayLag, ini);
    if (!(cfg.displayLag >= 0.f && cfg.displayLag <= 1.0f)) cfg.displayLag = 0.5f;
    cfg.hitProbe = GetPrivateProfileIntW(L"fire", L"hit_probe", 0, ini) != 0;
    if (cfg.tpShotGuard != 0.f && (cfg.tpShotGuard < 0.5f || cfg.tpShotGuard > 3.f)) cfg.tpShotGuard = 1.1f;
    cfg.tpLead = IniF(L"move", L"teleport_lead", cfg.tpLead, ini);
    if (cfg.tpLead < 0.f || cfg.tpLead > 0.6f) cfg.tpLead = 0.2f;
    cfg.leadAuto = GetPrivateProfileIntW(L"move", L"teleport_lead_auto", 1, ini) != 0;
    cfg.leadAutoTau = IniF(L"move", L"teleport_lead_auto_tau", 1.5f, ini);
    if (!(cfg.leadAutoTau >= 0.2f && cfg.leadAutoTau <= 10.f)) cfg.leadAutoTau = 1.5f;
    cfg.eyeFromHeight = GetPrivateProfileIntW(L"camera", L"eye_from_height", 1, ini) != 0;
    cfg.hideSelf = GetPrivateProfileIntW(L"camera", L"hide_self", 1, ini) != 0;
    cfg.hideArrows = GetPrivateProfileIntW(L"camera", L"hide_arrows", 1, ini) != 0;
    Log("ghost: teleport_lead=%.3f teleport_lead_auto=%d tau=%.2f eye_from_height=%d", cfg.tpLead, cfg.leadAuto, cfg.leadAutoTau,
        cfg.eyeFromHeight);
    cfg.wall = GetPrivateProfileIntW(L"move", L"wall", 1, ini) != 0;
    cfg.stepUp = IniF(L"move", L"step_up", 0.6f, ini);
    cfg.stepDown = IniF(L"move", L"step_down", 0.8f, ini);
    cfg.gravity = IniF(L"move", L"gravity", 20.3f, ini);
    cfg.jumpHeight = IniF(L"move", L"jump_height", 0.f, ini);
    cfg.airControl = IniF(L"move", L"air_control", 0.35f, ini);
    cfg.airTpMax = IniF(L"move", L"air_teleport_max", 1.5f, ini);
    cfg.landDip = IniF(L"move", L"land_dip", 0.12f, ini);
    cfg.dropClearance = IniF(L"move", L"drop_clearance", 3.5f, ini);
    cfg.dropPushSpeed = IniF(L"move", L"drop_push_speed", 6.f, ini);
    cfg.dropMaxPush = IniF(L"move", L"drop_max_push", 6.f, ini);
    if (!(cfg.stepUp >= 0.05f && cfg.stepUp <= 3.f)) cfg.stepUp = 0.6f;
    if (!(cfg.stepDown >= 0.05f && cfg.stepDown <= 5.f)) cfg.stepDown = 0.8f;
    if (!(cfg.gravity >= 1.f && cfg.gravity <= 100.f)) cfg.gravity = 20.3f;
    if (!(cfg.jumpHeight >= 0.f && cfg.jumpHeight <= 5.f)) cfg.jumpHeight = 0.f;
    if (!(cfg.airControl >= 0.f && cfg.airControl <= 5.f)) cfg.airControl = 0.35f;
    if (!(cfg.airTpMax >= 0.f && cfg.airTpMax <= 50.f)) cfg.airTpMax = 1.5f;
    if (!(cfg.landDip >= 0.f && cfg.landDip <= 0.5f)) cfg.landDip = 0.12f;
    if (!(cfg.dropClearance >= 0.f && cfg.dropClearance <= 10.f)) cfg.dropClearance = 3.5f;
    if (!(cfg.dropPushSpeed >= 1.f && cfg.dropPushSpeed <= 30.f)) cfg.dropPushSpeed = 6.f;
    if (!(cfg.dropMaxPush >= 0.5f && cfg.dropMaxPush <= 20.f)) cfg.dropMaxPush = 6.f;
    if (cfg.dropMaxPush < cfg.dropClearance) cfg.dropMaxPush = cfg.dropClearance + 1.f;
    cfg.building = GetPrivateProfileIntW(L"move", L"building", 1, ini) != 0;
    cfg.bldLow = IniF(L"move", L"building_low", 1.f, ini);
    cfg.bldHigh = IniF(L"move", L"building_high", 2.05f, ini);
    cfg.bldSkin = IniF(L"move", L"building_skin", 0.4f, ini);
    if (!(cfg.bldLow >= 0.3f && cfg.bldLow <= 3.f)) cfg.bldLow = 1.f;
    if (!(cfg.bldHigh >= cfg.bldLow && cfg.bldHigh <= 6.f)) cfg.bldHigh = 2.05f;
    if (!(cfg.bldSkin >= 0.f && cfg.bldSkin <= 2.f)) cfg.bldSkin = 0.4f;
    Log("wall: building=%d building_low=%.2f building_high=%.2f building_skin=%.2f", cfg.building, cfg.bldLow, cfg.bldHigh, cfg.bldSkin);
    cfg.wallUp = GetPrivateProfileIntW(L"move", L"wall_up", 1, ini) != 0;
    cfg.wallUpKey = GetPrivateProfileIntW(L"move", L"wall_up_key", 'E', ini);
    if (!(cfg.wallUpKey > 0 && cfg.wallUpKey < 0xFF)) cfg.wallUpKey = 'E';
    cfg.wallUpReach = IniF(L"move", L"wall_up_reach", 3.f, ini);
    cfg.wallUpMin = IniF(L"move", L"wall_up_min_height", 3.f, ini);
    cfg.wallUpMaxH = IniF(L"move", L"wall_up_max_height", 30.f, ini);
    if (!(cfg.wallUpReach >= 0.5f && cfg.wallUpReach <= 10.f)) cfg.wallUpReach = 3.f;
    if (!(cfg.wallUpMin >= 1.5f && cfg.wallUpMin <= 20.f)) cfg.wallUpMin = 3.f;
    if (!(cfg.wallUpMaxH > cfg.wallUpMin && cfg.wallUpMaxH <= 80.f)) cfg.wallUpMaxH = 30.f;
    cfg.wallUpGateR = IniF(L"move", L"wall_up_gate_radius", 8.f, ini);
    if (!(cfg.wallUpGateR >= 0.f && cfg.wallUpGateR <= 40.f)) cfg.wallUpGateR = 8.f;
    cfg.wallUpBase = IniF(L"move", L"wall_up_base", 3.f, ini);
    if (!(cfg.wallUpBase >= 0.f && cfg.wallUpBase <= 10.f)) cfg.wallUpBase = 3.f;
    Log("wall-up: enabled=%d key=0x%02X reach=%.2f min_height=%.2f max_height=%.1f gate_radius=%.1f base=%.1f", cfg.wallUp, cfg.wallUpKey, cfg.wallUpReach,
        cfg.wallUpMin, cfg.wallUpMaxH, cfg.wallUpGateR, cfg.wallUpBase);
    cfg.wallBody = GetPrivateProfileIntW(L"move", L"wall_body", 1, ini) != 0;
    cfg.wallBodyMargin = IniF(L"move", L"wall_body_margin", 1.f, ini);
    if (!(cfg.wallBodyMargin >= 0.f && cfg.wallBodyMargin <= 3.f)) cfg.wallBodyMargin = 1.f;
    cfg.wallBodyNet = GetPrivateProfileIntW(L"move", L"wall_body_net", 1, ini) != 0;
    Log("wall: wall_body=%d wall_body_margin=%.2f wall_body_net=%d", cfg.wallBody, cfg.wallBodyMargin, cfg.wallBodyNet);
    Log("wall: drop_clearance=%.2f drop_push_speed=%.1f drop_max_push=%.1f", cfg.dropClearance, cfg.dropPushSpeed, cfg.dropMaxPush);
    Log("wall: wall=%d step_up=%.2f step_down=%.2f gravity=%.1f jump_height=%.2f air_control=%.2f air_teleport_max=%.2f land_dip=%.3f",
        cfg.wall, cfg.stepUp, cfg.stepDown, cfg.gravity, cfg.jumpHeight, cfg.airControl, cfg.airTpMax, cfg.landDip);
    cfg.revealEnemies = GetPrivateProfileIntW(L"hero", L"reveal_enemies", 1, ini) != 0;
    cfg.noMelee = GetPrivateProfileIntW(L"hero", L"no_melee", 1, ini) != 0;
    cfg.noKnock = GetPrivateProfileIntW(L"hero", L"no_knockdown", 1, ini) != 0;
    cfg.superArmor = GetPrivateProfileIntW(L"hero", L"super_armor", 1, ini) != 0;
    cfg.heroesOnly = GetPrivateProfileIntW(L"hero", L"heroes_only", 1, ini) != 0;
    cfg.fly = GetPrivateProfileIntW(L"move", L"fly", 1, ini) != 0;
    cfg.flyTakeoff = GetPrivateProfileIntW(L"move", L"fly_takeoff", 1, ini) != 0;
    cfg.flyAhead = IniF(L"move", L"fly_ahead", cfg.flyAhead, ini);
    cfg.flyInterval = IniF(L"move", L"fly_interval", cfg.flyInterval, ini);
    cfg.flyTurnDeg = IniF(L"move", L"fly_turn_deg", cfg.flyTurnDeg, ini);
    cfg.flyEyeHeight = IniF(L"move", L"fly_eye_height", cfg.flyEyeHeight, ini);
    if (cfg.flyAhead < 5.f || cfg.flyAhead > 200.f) cfg.flyAhead = 60.f;
    if (cfg.flyInterval < 0.1f || cfg.flyInterval > 10.f) cfg.flyInterval = 2.0f;
    if (cfg.flyTurnDeg < 1.f || cfg.flyTurnDeg > 180.f) cfg.flyTurnDeg = 20.f;
    if (cfg.flyEyeHeight < -5.f || cfg.flyEyeHeight > 20.f) cfg.flyEyeHeight = 1.2f;
    cfg.shieldGame = GetPrivateProfileIntW(L"viewmodel", L"shield_block_game", 1, ini) != 0;
    cfg.shieldStagger = GetPrivateProfileIntW(L"viewmodel", L"shield_block_stagger", 1, ini) != 0;
    cfg.shieldAngle = IniF(L"viewmodel", L"shield_block_angle", 180.f, ini);
    if (!(cfg.shieldAngle >= 0.f && cfg.shieldAngle <= 180.f)) cfg.shieldAngle = 180.f;
    cfg.shieldRefund = GetPrivateProfileIntW(L"viewmodel", L"shield_block_refund", 1, ini) != 0;
    cfg.meleeEngine = GetPrivateProfileIntW(L"melee", L"engine", 1, ini) != 0;
    cfg.meleeAlwaysHit = GetPrivateProfileIntW(L"melee", L"always_hit", 1, ini) != 0;
    cfg.meleeDamage = IniF(L"melee", L"damage_scale", cfg.meleeDamage, ini);
    if (!(cfg.meleeDamage >= 0.f && cfg.meleeDamage <= 20.f)) cfg.meleeDamage = 1.f;
    cfg.meleeReact = GetPrivateProfileIntW(L"melee", L"react", 1, ini) != 0;
    cfg.meleeEffects = GetPrivateProfileIntW(L"melee", L"effects", 1, ini) != 0;
    cfg.meleeFallback = IniF(L"melee", L"fallback_damage", cfg.meleeFallback, ini);
    if (!(cfg.meleeFallback >= 0.f && cfg.meleeFallback <= 5000.f)) cfg.meleeFallback = 60.f;
    cfg.meleeSplashRadius = IniF(L"melee", L"splash_radius", cfg.meleeSplashRadius, ini);
    if (!(cfg.meleeSplashRadius >= 0.f && cfg.meleeSplashRadius <= 6.f)) cfg.meleeSplashRadius = 1.5f;
    cfg.meleeSplashDamage = IniF(L"melee", L"splash_damage", cfg.meleeSplashDamage, ini);
    if (!(cfg.meleeSplashDamage >= 0.f && cfg.meleeSplashDamage <= 5.f)) cfg.meleeSplashDamage = 0.5f;
    cfg.meleeSplashMax = GetPrivateProfileIntW(L"melee", L"splash_max", cfg.meleeSplashMax, ini);
    if (cfg.meleeSplashMax < 0 || cfg.meleeSplashMax > 32) cfg.meleeSplashMax = 6;
    cfg.meleeHeavySplashRadius = IniF(L"melee", L"heavy_splash_radius", cfg.meleeHeavySplashRadius, ini);
    if (!(cfg.meleeHeavySplashRadius >= 0.f && cfg.meleeHeavySplashRadius <= 6.f)) cfg.meleeHeavySplashRadius = 2.5f;
    cfg.meleeHeavySplashDamage = IniF(L"melee", L"heavy_splash_damage", cfg.meleeHeavySplashDamage, ini);
    if (!(cfg.meleeHeavySplashDamage >= 0.f && cfg.meleeHeavySplashDamage <= 5.f)) cfg.meleeHeavySplashDamage = 0.6f;
    cfg.meleeHeavySplashMax = GetPrivateProfileIntW(L"melee", L"heavy_splash_max", cfg.meleeHeavySplashMax, ini);
    if (cfg.meleeHeavySplashMax < 0 || cfg.meleeHeavySplashMax > 32) cfg.meleeHeavySplashMax = 12;
    cfg.meleeHeavyReach = IniF(L"melee", L"heavy_reach", cfg.meleeHeavyReach, ini);
    if (!(cfg.meleeHeavyReach >= 0.5f && cfg.meleeHeavyReach <= 3.f)) cfg.meleeHeavyReach = 1.3f;
    LoadMeleeCats(ini);
    g_meleeTargetRadiusMax = IniF(L"melee", L"target_radius_max", 4.f, ini);
    if (!(g_meleeTargetRadiusMax >= 0.f && g_meleeTargetRadiusMax <= 12.f)) g_meleeTargetRadiusMax = 4.f;
    Log("melee: riders are hit through their mounts (crew manning a gun are hit themselves); big targets measured to their edge (radius counted up to "
        "%.1f m, 0 = off)",
        g_meleeTargetRadiusMax);
    cfg.hideHud = GetPrivateProfileIntW(L"ui", L"hide_hud", 1, ini) != 0;
    cfg.hideCursor = GetPrivateProfileIntW(L"ui", L"hide_cursor", 1, ini) != 0;
    cfg.hidePaths = GetPrivateProfileIntW(L"ui", L"hide_paths", 1, ini) != 0;
    cfg.hideEdgeArrows = GetPrivateProfileIntW(L"ui", L"hide_edge_arrows", 1, ini) != 0;
    cfg.hudEnabled = GetPrivateProfileIntW(L"hud", L"enabled", 1, ini) != 0;
    cfg.hudCover = GetPrivateProfileIntW(L"hud", L"cover_viewmodel", 1, ini) != 0;
    cfg.castKeys = GetPrivateProfileIntW(L"hud", L"cast_keys", 1, ini) != 0;
    cfg.castMarker = GetPrivateProfileIntW(L"hud", L"cast_marker", 1, ini) != 0;
    cfg.castMarkerPillar = IniF(L"hud", L"cast_marker_pillar", cfg.castMarkerPillar, ini);
    if (!cfg.hudCover) vmgame::SetHudCover(0, 0, 0, 0);
    {
        wchar_t list[512];
        GetPrivateProfileStringW(L"ui", L"hide", L"info_parent,battle_orders,winds_of_magic", list, 512, ini);
        cfg.hideCount = 0;
        for (wchar_t* tok = list; *tok && cfg.hideCount < kMaxHideUi;) {
            wchar_t* end = wcschr(tok, L',');
            if (end) *end = 0;
            while (*tok == L' ') ++tok;
            size_t n = 0;
            for (; tok[n] && tok[n] != L' ' && n < 47; ++n) cfg.hideNames[cfg.hideCount][n] = static_cast<char>(tok[n] < 128 ? tok[n] : '?');
            cfg.hideNames[cfg.hideCount][n] = 0;
            if (n) ++cfg.hideCount;
            if (!end) break;
            tok = end + 1;
        }
    }
    cfg.stuckLeash = IniF(L"move", L"stuck_leash", cfg.stuckLeash, ini);
    if (cfg.stuckLeash < 0.f || cfg.stuckLeash > 5.f) cfg.stuckLeash = 0.6f;
    cfg.moveInterval = IniF(L"move", L"interval", cfg.moveInterval, ini);
    cfg.moveAhead = IniF(L"move", L"ahead", cfg.moveAhead, ini);
    cfg.moveTurnDeg = IniF(L"move", L"turn_deg", cfg.moveTurnDeg, ini);
    if (cfg.moveInterval < 0.05f || cfg.moveInterval > 2.f) cfg.moveInterval = 0.2f;
    if (cfg.moveAhead < 1.f || cfg.moveAhead > 50.f) cfg.moveAhead = 8.f;
    if (cfg.moveTurnDeg < 1.f || cfg.moveTurnDeg > 180.f) cfg.moveTurnDeg = 25.f;
    cfg.predict = GetPrivateProfileIntW(L"move", L"predict", 1, ini) != 0;
    cfg.predictSpeed = IniF(L"move", L"predict_speed", cfg.predictSpeed, ini);
    cfg.predictTau = IniF(L"move", L"predict_tau", cfg.predictTau, ini);
    cfg.predictMax = IniF(L"move", L"predict_max", cfg.predictMax, ini);
    if (cfg.predictSpeed < 0.5f || cfg.predictSpeed > 40.f) cfg.predictSpeed = 6.f;
    if (cfg.predictTau < 0.05f || cfg.predictTau > 3.f) cfg.predictTau = 0.35f;
    if (cfg.predictMax < 0.f || cfg.predictMax > 6.f) cfg.predictMax = 2.0f;
    if (cfg.eyeHeight < 0.2f || cfg.eyeHeight > 20.f) cfg.eyeHeight = 1.75f;
    if (cfg.nearClip < 0.01f || cfg.nearClip > 5.f) cfg.nearClip = 0.1f;
    if (cfg.sensitivity <= 0.f || cfg.sensitivity > 5.f) cfg.sensitivity = 0.07f;
    if (cfg.logIntervalMs < 200) cfg.logIntervalMs = 200;
    Log("config enabled=%d toggle=0x%02X eye_height=%.2f eye_forward=%.2f near=%.3f fov_override=%.3f sens=%.3f invert_y=%d "
        "fire=%d interval=%.3f range=%.1f launch_forward=%.2f shooter=%d muzzle_depth=%.2f clearance=%.2f lookahead=%.2f "
        "move=%d mode=%s run=%.1f walk=%.1f accel=%.2f tp_interval=%.2f leash=%.1f stuck_leash=%.1f interval=%.2f ahead=%.1f turn=%.0f "
        "predict=%d speed=%.1f tau=%.2f max=%.2f ghost=%d reveal_enemies=%d no_melee=%d tp_lead=%.2f pinned_clearance=%.2f "
        "teleport_guard=%.2f unlimited_ammo=%d bullet_speed=%.0f projectile=%s hide_hud=%d hide_count=%d",
        cfg.enabled, cfg.toggleKey, cfg.eyeHeight, cfg.eyeForward, cfg.nearClip, cfg.fovOverride,
        cfg.sensitivity, cfg.invertY, cfg.fireEnabled, cfg.fireInterval, cfg.fireRange, cfg.fireForward,
        cfg.fireShooter, cfg.muzzleDepth, cfg.fireClearance, cfg.fireLookahead, cfg.moveEnabled,
        cfg.moveMode == 0 ? "teleport" : cfg.moveMode == 1 ? "unit" : "entity", cfg.runSpeed, cfg.walkSpeed,
        cfg.accelTau, cfg.tpInterval, cfg.leash, cfg.stuckLeash, cfg.moveInterval, cfg.moveAhead, cfg.moveTurnDeg, cfg.predict,
        cfg.predictSpeed, cfg.predictTau, cfg.predictMax, cfg.ghost, cfg.revealEnemies, cfg.noMelee, cfg.tpLead,
        cfg.pinnedClearance, cfg.tpShotGuard, cfg.unlimitedAmmo, cfg.bulletSpeed, cfg.fireProjectile[0] ? cfg.fireProjectile : "(hero's own)",
        cfg.hideHud, cfg.hideCount);
}
using FnSetVec = void (*)(uintptr_t self, const float* v);
using FnSetF = void (*)(uintptr_t self, float v);
bool FpCamOk(uintptr_t cam) {
    uintptr_t vt = cam ? Ptr(cam) : 0;
    return vt && InExe(vt) && Ptr(vt + vtSetPos) == g_base + kFnCamSetPos &&
           Ptr(vt + vtSetTarget) == g_base + kFnCamSetTarget && Ptr(vt + vtSetUp) == g_base + kFnCamSetUp &&
           Ptr(vt + vtSetFov) == g_base + kFnCamSetFov && Ptr(vt + vtSetNear) == g_base + kFnCamSetNear &&
           Ptr(vt + vtSetFar) == g_base + kFnCamSetFar;
}
void CamSetVec(uintptr_t cam, uintptr_t slot, const float* v) {
    uintptr_t fn = slot == vtSetPos ? kFnCamSetPos : slot == vtSetTarget ? kFnCamSetTarget : kFnCamSetUp;
    reinterpret_cast<FnSetVec>(g_base + fn)(cam, v);
}
void CamSetF(uintptr_t cam, uintptr_t slot, float v) {
    uintptr_t fn = slot == vtSetFov ? kFnCamSetFov : slot == vtSetNear ? kFnCamSetNear : kFnCamSetFar;
    reinterpret_cast<FnSetF>(g_base + fn)(cam, v);
}
uintptr_t SelectedBattleUnit(int* countOut, uintptr_t* uiUnitOut = nullptr) {
    uintptr_t ui = Ptr(g_base + kGlobalUi);
    uintptr_t sel = ui ? Ptr(ui + oUiSelection) : 0;
    int count = 0;
    if (!sel || !Rd(sel + oSelCount, count)) return 0;
    if (countOut) *countOut = count;
    if (count < 1) return 0;
    uintptr_t arr = Ptr(sel + oSelArray);
    uintptr_t uiUnit = arr ? Ptr(arr) : 0;
    if (uiUnitOut) *uiUnitOut = uiUnit;
    return uiUnit ? Ptr(uiUnit + oUiUnitBattleUnit) : 0;
}
int CharKeyLen(uintptr_t uiUnit) {
    uintptr_t det = uiUnit ? Ptr(uiUnit + oUnitCharDetails) : 0;
    if (!det) return -1;
    uint64_t w = 0;
    if (!Rd(det + oCharKeyStr + 8, w)) return -1;
    if ((w & 0xF000000000000000ull) == 0x8000000000000000ull) return int((w >> 56) & 0xf);
    uint32_t len = 0;
    return Rd(det + oCharKeyStr, len) ? int(len) : -1;
}
bool IsGeneralUiUnit(uintptr_t uiUnit) {
    uint8_t b = 0;
    return uiUnit && Rd(uiUnit + oUiUnitIsGeneral, b) && b != 0;
}
bool ReadVec(uintptr_t a, float v[3]) {
    if (!Rd(a, v[0]) || !Rd(a + 4, v[1]) || !Rd(a + 8, v[2])) return false;
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && std::fabs(v[0]) < 1e6f &&
           std::fabs(v[1]) < 1e6f && std::fabs(v[2]) < 1e6f;
}
constexpr uintptr_t oEntUnit = 0x2e8;
struct EntPath {
    uintptr_t off = 0;
    int index = -1;
};
bool Plausible(uintptr_t p) { return p >= 0x10000 && p < 0x7FFFFFFFFFFFull && !(p & 7); }
bool IsEntityOf(uintptr_t e, uintptr_t unit) {
    float pos[3];
    return Plausible(e) && Ptr(e + oEntUnit) == unit && ReadVec(e + oEntPos, pos);
}
uintptr_t EntityByPath(uintptr_t unit, const EntPath& p) {
    uintptr_t q = Ptr(unit + p.off);
    uintptr_t e = p.index < 0 ? q : (Plausible(q) ? Ptr(q + 8 * static_cast<uintptr_t>(p.index)) : 0);
    return IsEntityOf(e, unit) ? e : 0;
}
uintptr_t FindEntity(uintptr_t unit, EntPath* out, unsigned* countOut) {
    if (!Plausible(unit)) return 0;
    {
        EntPath p;
        p.off = 0xa8;
        p.index = 0;
        uintptr_t e = EntityByPath(unit, p);
        if (e) {
            *out = p;
            if (countOut) *countOut = 1;
            return e;
        }
    }
    unsigned count = 0;
    Rd(unit + oUnitEntCount, count);
    uintptr_t arr = Ptr(unit + oUnitEntArray);
    uintptr_t e0 = Plausible(arr) ? Ptr(arr) : 0;
    if (countOut) *countOut = count;
    if (count >= 1 && count <= 2000 && IsEntityOf(e0, unit)) {
        out->off = oUnitEntArray;
        out->index = 0;
        return e0;
    }
    Log("entity: known path failed (unit=%p vt=+0x%llx count@+0x134=%u arr@+0x138=%p arr[0]=%p arr[0]+0x2e8=%p); scanning",
        (void*)unit, (unsigned long long)(Ptr(unit) - g_base), count, (void*)arr, (void*)e0,
        (void*)(Plausible(e0) ? Ptr(e0 + oEntUnit) : 0));
    for (uintptr_t off = 0; off < 0x6000; off += 8) {
        uintptr_t q = Ptr(unit + off);
        if (!Plausible(q) || q == unit) continue;
        if (IsEntityOf(q, unit)) {
            out->off = off;
            out->index = -1;
            Log("entity: found direct pointer at unit+0x%llx -> %p", (unsigned long long)off, (void*)q);
            return q;
        }
        for (int i = 0; i < 4; ++i) {
            uintptr_t e = Ptr(q + 8 * static_cast<uintptr_t>(i));
            if (IsEntityOf(e, unit)) {
                out->off = off;
                out->index = i;
                Log("entity: found array at unit+0x%llx [%d] -> %p", (unsigned long long)off, i, (void*)e);
                return e;
            }
        }
    }
    Log("entity: none of unit %p's first 0x6000 bytes points to an entity whose +0x2e8 is the unit", (void*)unit);
    return 0;
}
struct TpRec {
    double t;
    float x, z;
};
struct State {
    volatile bool active = false;
    uintptr_t mgr = 0, battle = 0, fpCtrl = 0, cam = 0, prevActive = 0, unit = 0, entity = 0;
    float yaw = 0, pitch = 0;
    float outPos[3] = {}, from[3] = {}, to[3] = {}, lastRaw[3] = {};
    double tChange = 0, tickDur = 0.1, tLastFrame = 0;
    unsigned tracerLogs = 0, movingLogs = 0;
    unsigned aimShots = 0, aimSoldier = 0, aimGround = 0, aimRange = 0, aimMoving = 0;
    float aimSpeedSum = 0, aimSpeedMax = 0, aimLeadSum = 0, aimSelfSpeedSum = 0;
    double tAimStat = 0;
    bool lastShotFast = false;
    bool havePos = false, useRaw = false, rawRegistered = false, skipFirstCursor = true;
    unsigned entCount = 0;
    EntPath entPath;
    double nextShot = 0;
    bool fireChain = false;
    bool sprint = false, tac = false, ads = false;
    bool shiftPrev = false;
    float adsT = 0;
    double fireBlock = 0;
    int adsKind = 0;
    bool exitTac = false;
    float baseFov = 0, lastFov = 0;
    unsigned stanceLogs = 0;
    kick::CamKick camKick;
    unsigned shots = 0, shotsFailed = 0, shotLogs = 0;
    struct ShotRec {
        double t;
        float ax, az, dx, dz;
    };
    ShotRec shotHist[16] = {};
    unsigned shotHead = 0;
    unsigned tpGuarded = 0, tpGuardSkipped = 0, guardLogs = 0;
    uintptr_t fpProj = 0;
    int32_t fpProjId = -1;
    int projFor = -1;
    int32_t meleeProjId[vmgame::kStrikeKinds] = {-1, -1, -1, -1, -1};
    unsigned meleeStrikes = 0, meleeFailed = 0, strikeLogs = 0;
    bool speedApplied = false;
    uintptr_t speedRec = 0;
    float speedOrig = 0, massOrig = 0;
    bool ammoApplied = false;
    uintptr_t ammoUnit = 0, ammoEntity = 0;
    int32_t ammoOrig[2][2] = {};
    int32_t lastAct = 0;
    unsigned actLogs = 0;
    bool moving = false, lastRun = true;
    double nextMove = 0;
    float lastMoveDir[2] = {0, 0};
    unsigned moves = 0, halts = 0, moveLogs = 0;
    float lead[2] = {0, 0}, prevOut[3] = {}, prevCam[3] = {}, speedEst = 6.f, camSpeed = 0;
    bool havePrev = false;
    double tMoveBegan = 0;
    double tMoveStart = -1, tHalt = -1, tSig = 0;
    float startPos[3] = {}, haltPos[3] = {}, sigPos[3] = {};
    unsigned latLogs = 0;
    double tWatch = -1;
    float watchPos[3] = {};
    bool unitSync = false;
    bool kinInit = false;
    float kinPos[2] = {0, 0}, kinVel[2] = {0, 0}, kinY = 0, lastTp[2] = {0, 0};
    float footY = 0, vy = 0, airGap = 0, airStartY = 0, landY = 0, landV = 0, landPunch = 0;
    uintptr_t footBlk = 0;
    bool air = false, airFar = false, spacePrev = false;
    double tAir = 0, tLand = -1, tWallCheck = 0;
    unsigned jumps = 0, drops = 0, lands = 0, blocks = 0, wallLogs = 0;
    bool dropActive = false;
    float dropP0[2] = {0, 0}, dropDir[2] = {0, 0}, dropS = 0, dropTarget = 0, dropSpeed = 0;
    float dropPoly[16][2] = {};
    int dropPolyN = 0, dropKept = 0;
    float dropGap = 0;
    bool bldInside = false, bldRecheck = false;
    double tBldCheck = 0;
    int bldBlocks = 0, bldLogs = 0;
    int prompt = 0, wallUps = 0, wallUpLogs = 0;
    bool ePrev = true, wallUpOk = false;
    float wallUpTo[3] = {};
    uintptr_t wallUpBlk = 0;
    double tWallScan = 0;
    int wallUpWhy = 0, wallWhyLogs = 0;
    double tWallWhy = -10;
    bool bodyInside = false;
    int bodyLogs = 0, censusLogs = 0;
    unsigned netPass = 0;
    double tNetLog = -10;
    int netLogs = 0;
    double tLastTp = 0;
    unsigned tpSent = 0, tpFailed = 0, tpLogs = 0, leashHits = 0;
    int lastMode = -1;
    bool flyer = false;
    TpRec tpHist[8] = {};
    unsigned tpHead = 0;
    bool stuck = false;
    double tStuck = 0, tNoFollow = -1;
    float stuckPos[3] = {};
    float leadPos[2] = {0, 0};
    double tGuard = -1, tLeadLog = 0;
    double tLeashHit = -10, tBump = -10;
    unsigned bumpSig = 0, leashAhead = 0;
    float leadOffSum = 0;
    int leadOffN = 0;
    bool pinned = false;
    int32_t pinAct = 0;
    double tPinned = 0;
    unsigned pinLogs = 0;
    bool loggedProj = false;
    float entVel[2] = {0, 0};
    double tAliveCheck = 0;
    bool ghostWanted = false, ghost = false;
    unsigned ghostLogs = 0;
    int exitStage = 0;
    double tExitRelease = 0, tExitBegan = 0;
    float exitPos[2] = {0, 0};
    int exitMode = 0;
    uintptr_t exitBattle = 0, exitUnit = 0;
    uintptr_t revealed[kMaxRevealed] = {};
    unsigned nRevealed = 0, revealLogs = 0;
    uintptr_t revealBattle = 0, revealMgr = 0;
    double tRevealScan = 0, tRevealHold = 0;
    struct HiddenUic {
        uintptr_t uic;
        uint8_t orig;
        char name[48];
    } uiHidden[kMaxHideUi] = {};
    int uiCount = 0;
    bool uiApplied = false;
    uintptr_t uiRoot = 0;
    unsigned uiRehides = 0, uiLogs = 0;
    bool cursorOff = false;
    uint8_t cursorOrig = 1;
    unsigned cursorRehides = 0;
    bool cursorHidden = false;
    uint8_t cursorVisOrig = 1;
    unsigned cursorVisRehides = 0;
    static constexpr int kMaxEdgeBytes = 512;
    bool edgeApplied = false, edgeWarned = false;
    uintptr_t edgeUic[2] = {0, 0};
    int edgeCount = 0;
    uintptr_t edgeAddr[kMaxEdgeBytes] = {};
    uint8_t edgeOrig[kMaxEdgeBytes] = {};
    unsigned edgeRewrites = 0;
    bool pathOff = false, pathWarned = false;
    uintptr_t pathMgr = 0;
    int32_t pathOrig[2] = {0, 0};
    unsigned pathRewrites = 0;
    uintptr_t meleeUnit = 0, meleeBattle = 0, meleeSkipUnit = 0;
    unsigned meleeLogs = 0;
    int heroAlliance = -1;
    double tStat = 0;
    unsigned frames = 0, writes = 0, yields = 0;
    long sumDx = 0, sumDy = 0;
} S;
volatile LONG g_reqToggle = 0, g_reqExit = 0;
volatile LONG g_staleFp = 0;
volatile LONG g_camBeat = 0;
bool g_fpDetached = false;
uintptr_t g_detachMgr = 0;
struct TacStamina {
    float st = 1.f;
    double lastTac = -1e9;
    uintptr_t unit = 0;
    int logs = 0;
} g_tac;
bool TacStaminaOk() { return vmgame::InfTacStamina() || g_tac.st >= cfg.tacStaminaMin; }
void TacStaminaStep(double now, float dt) {
    if (S.unit != g_tac.unit) g_tac.unit = S.unit, g_tac.st = 1.f, g_tac.lastTac = -1e9;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.25f) dt = 0.25f;
    if (vmgame::InfTacStamina()) {
        g_tac.st = 1.f;
        if (S.tac) g_tac.lastTac = now;
        return;
    }
    if (S.tac) {
        g_tac.lastTac = now;
        g_tac.st -= dt / cfg.tacStamina;
        if (g_tac.st <= 0.f) {
            g_tac.st = 0.f;
            S.tac = false;
            if (g_tac.logs++ < 6) Log("stance: tactical sprint stamina empty - back to sprint (%.0f s to refill)", cfg.tacStaminaRegen);
        }
    } else if (now - g_tac.lastTac >= cfg.tacStaminaDelay) {
        g_tac.st += dt / cfg.tacStaminaRegen;
        if (g_tac.st > 1.f) g_tac.st = 1.f;
    }
}
struct CastState {
    volatile LONG armed = 0;
    volatile LONG q[16] = {};
    volatile LONG head = 0, tail = 0;
    bool lPrev = false, rPrev = false;
    bool lHeld = false, rHeld = false;
    bool lmb = false, rmb = false;
    volatile LONG unswallow = 0;
    volatile LONG viewSeq = 0;
    float eye[3] = {}, fwd[3] = {};
    LONG events = 0;
} g_cast;
struct CastMarkerShared {
    SRWLOCK lock = SRWLOCK_INIT;
    cm::Req req;
    uintptr_t unit = 0;
    LONG gen = 0;
    bool aimOk = false;
    float aim[3] = {}, aimDist = 0, aimRange = -1;
    bool aimEdge = false;
} g_cms;
void CastPush(LONG e) {
    LONG h = g_cast.head;
    if (h - g_cast.tail >= 16) return;
    g_cast.q[h & 15] = e;
    InterlockedExchange(&g_cast.head, h + 1);
    if (++g_cast.events <= 6) Log("cast: input event %ld (%s)", e, e == 101 ? "click while armed" : e == 102 ? "cancel while armed" : "Alt+slot key");
}
void CastMarkerClear();
void CastReset() {
    InterlockedExchange(&g_cast.armed, 0);
    InterlockedExchange(&g_cast.tail, g_cast.head);
    CastMarkerClear();
}
int CastSlotKey(WPARAM w) {
    if (w >= '1' && w <= '9') return static_cast<int>(w - '0');
    if (w == '0') return 10;
    if (w == VK_OEM_MINUS) return 11;
    if (w == VK_OEM_PLUS) return 12;
    return 0;
}
void CastMouse(bool l, bool r) {
    bool armed = g_cast.armed != 0;
    if (armed && l && !g_cast.lPrev && !g_cast.lHeld) CastPush(101);
    if (armed && r && !g_cast.rPrev && !g_cast.rHeld) CastPush(102);
    if (armed && l) g_cast.lHeld = true;
    if (armed && r) g_cast.rHeld = true;
    if (!l) g_cast.lHeld = false;
    if (!r) g_cast.rHeld = false;
    g_cast.lPrev = l;
    g_cast.rPrev = r;
    g_cast.lmb = l && !g_cast.lHeld;
    g_cast.rmb = r && !g_cast.rHeld;
}
void CastView(const float eye[3], const float fwd[3]) {
    InterlockedIncrement(&g_cast.viewSeq);
    memcpy(g_cast.eye, eye, sizeof g_cast.eye);
    memcpy(g_cast.fwd, fwd, sizeof g_cast.fwd);
    InterlockedIncrement(&g_cast.viewSeq);
}
volatile LONG g_rawDx = 0, g_rawDy = 0;
HWND g_hwnd = nullptr;
WNDPROC g_prevProc = nullptr;
HWND g_childHwnd = nullptr;
WNDPROC g_prevChildProc = nullptr;
unsigned g_childSwallowLogs = 0;
DWORD g_windowThread = 0;
uintptr_t* g_vtCopy = nullptr;
uintptr_t g_installedMgr = 0;
bool g_loggedUpdateThread = false;
LARGE_INTEGER g_qpf;
double Now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(g_qpf.QuadPart);
}
bool Foreground() { return g_hwnd && GetForegroundWindow() == g_hwnd; }
POINT ClientCenterScreen() {
    RECT r{};
    GetClientRect(g_hwnd, &r);
    POINT c{(r.right - r.left) / 2, (r.bottom - r.top) / 2};
    ClientToScreen(g_hwnd, &c);
    return c;
}
void SetupMouse() {
    S.useRaw = false;
    UINT n = 0;
    GetRegisteredRawInputDevices(nullptr, &n, sizeof(RAWINPUTDEVICE));
    bool mouseTaken = false;
    if (n > 0 && n < 64) {
        RAWINPUTDEVICE list[64];
        UINT m = n;
        if (GetRegisteredRawInputDevices(list, &m, sizeof(RAWINPUTDEVICE)) != static_cast<UINT>(-1))
            for (UINT i = 0; i < m; ++i)
                if (list[i].usUsagePage == 1 && list[i].usUsage == 2) mouseTaken = true;
    }
    if (mouseTaken) {
        Log("mouse: raw input already registered in process (%u devices) -> cursor-delta fallback", n);
        return;
    }
    RAWINPUTDEVICE rid{1, 2, 0, g_hwnd};
    if (RegisterRawInputDevices(&rid, 1, sizeof rid)) {
        S.useRaw = true;
        S.rawRegistered = true;
        Log("mouse: raw input registered on hwnd=%p", g_hwnd);
    } else {
        Log("mouse: RegisterRawInputDevices failed err=%lu -> cursor-delta fallback", GetLastError());
    }
}
void ReleaseMouse() {
    if (S.rawRegistered) {
        RAWINPUTDEVICE rid{1, 2, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&rid, 1, sizeof rid);
        S.rawRegistered = false;
    }
    S.useRaw = false;
    InterlockedExchange(&g_rawDx, 0);
    InterlockedExchange(&g_rawDy, 0);
}
bool IssueHalt();
bool IssueDesiredFlying(bool fly);
bool StopMoving();
int MoveMode();
void HookRenderChild();
void ShotRestoreSpeed(const char* why);
uintptr_t FindProjectile(const char* key, int32_t* idOut);
void ProbeInit();
float StepPeriod();
double LastStepTime();
uint32_t LastStep();
void VelReset();
void LogUnitLists(uintptr_t battle);
bool FastFireInstall(uintptr_t battle);
void FastFireUninstall(const char* why);
bool EarlyFlightInstall(uintptr_t battle, uintptr_t heroEntity);
namespace herofx {
bool Install(uintptr_t battle, uintptr_t heroEntity, uintptr_t projRecord);
void Uninstall(const char* why);
void Track(uintptr_t projRecord);
}
void EarlyFlightUninstall(const char* why);
long EarlyFlightCalls();
long EarlyFlightPromoted();
void FastFireStats(bool force);
const char* ModeName(int m);
void ReleaseHero(int mode, const char* why);
void ExitSequenceStep(uintptr_t mgr);
void LauncherReset();
void Exit(uintptr_t mgr, const char* why, bool release = true) {
    if (!S.active) return;
    LauncherReset();
    FastFireUninstall(why);
    EarlyFlightUninstall(why);
    herofx::Uninstall(why);
    int mode = S.lastMode >= 0 ? S.lastMode : MoveMode();
    if (!release) {
        Log("move: hero not released (%s)", why);
    } else if (S.ghost) {
        float pos[3];
        if (S.kinInit) {
            S.exitPos[0] = S.kinPos[0];
            S.exitPos[1] = S.kinPos[1];
        } else if (ReadVec(S.entity + oEntPos, pos)) {
            S.exitPos[0] = pos[0];
            S.exitPos[1] = pos[2];
        }
        S.exitMode = 0;
        S.exitBattle = S.battle;
        S.exitUnit = S.unit;
        S.tExitBegan = Now();
        S.exitStage = 1;
        ExitSequenceStep(mgr);
    } else if (mode != 1) {
        ReleaseHero(mode, "exit");
    } else if (S.moving) {
        StopMoving();
    }
    S.moving = false;
    S.ghost = S.ghostWanted = false;
    CastReset();
    S.active = false;
    uintptr_t active = Ptr(mgr + oMgrActive);
    bool restored = false;
    if (active == S.cam && S.prevActive && InExe(Ptr(S.prevActive))) restored = Wr(mgr + oMgrActive, S.prevActive);
    ReleaseMouse();
    vmgame::SetFirstPerson(false);
    Log("FPS exit reason=%s restoredPrev=%d active=%p prev=%p", why, restored, (void*)active,
        (void*)S.prevActive);
}
bool HeroRouting(uintptr_t unit, int* state = nullptr) {
    int32_t st = -1;
    if (state) *state = -1;
    if (!g_routOk || !Plausible(unit) || !Rd(unit + oUnitMorale + oMoraleState, st)) return false;
    if (state) *state = st;
    using FnRouting = char (*)(uintptr_t morale);
    return reinterpret_cast<FnRouting>(g_base + kFnUnitRouting904)(unit + oUnitMorale) != 0;
}
void Enter(uintptr_t mgr) {
    uintptr_t battle = Ptr(mgr + oMgrBattle);
    uintptr_t ui = Ptr(g_base + kGlobalUi);
    uintptr_t battle2 = ui ? Ptr(ui + oUiBattle) : 0;
    uintptr_t fpCtrl = battle ? Ptr(battle + oBattleFpCtrl) : 0;
    uintptr_t cam = fpCtrl ? fpCtrl + oFpCtrlCamera : 0;
    if (!battle || battle != battle2 || !fpCtrl) {
        Refuse(kRefuseOther);
        Log("enter refused: battle=%p ui.battle=%p fpCtrl=%p", (void*)battle, (void*)battle2, (void*)fpCtrl);
        return;
    }
    if (Ptr(fpCtrl + oFpCtrlEntity)) {
        Refuse(kRefuseArtillery);
        Log("enter refused: native artillery first-person mode is active");
        return;
    }
    if (!FpCamOk(cam)) {
        Refuse(kRefuseOther);
        Log("enter refused: camera vtable mismatch %p", (void*)Ptr(cam));
        return;
    }
    int selCount = 0;
    uintptr_t uiUnit = 0;
    uintptr_t unit = SelectedBattleUnit(&selCount, &uiUnit);
    unsigned entCount = 0;
    EntPath path;
    uintptr_t entity = FindEntity(unit, &path, &entCount);
    float pos[3];
    if (!unit || !entity || !ReadVec(entity + oEntPos, pos)) {
        Refuse(kRefuseSelect);
        Log("enter refused: selection count=%d unit=%p entity=%p (select one unit first)", selCount, (void*)unit,
            (void*)entity);
        return;
    }
    if (cfg.heroesOnly && !g_charOk) {
        Refuse(kRefuseOther);
        Log("enter refused: cannot tell lords / heroes apart on this exe (IsCharacter fingerprint mismatch); [hero] heroes_only=0 lets any unit in");
        return;
    }
    if (cfg.heroesOnly) {
        int keyLen = CharKeyLen(uiUnit);
        bool general = IsGeneralUiUnit(uiUnit);
        if (keyLen <= 0 && !general) {
            Refuse(kRefuseNotHero);
            Log("enter refused: unit %p (ui %p) is not a lord or hero (IsCharacter key len %d, IsGeneral 0; first person is lords / heroes only; [hero] heroes_only=0 lets any unit in)",
                (void*)unit, (void*)uiUnit, keyLen);
            return;
        }
        Log("enter: lord / hero check passed (IsCharacter key len %d, IsGeneral %d)", keyLen, general ? 1 : 0);
    }
    int moraleState = -1;
    if (HeroRouting(unit, &moraleState)) {
        Refuse(kRefuseRouting);
        Log("enter refused: unit %p is routing (morale state %d; 6 routing / 7 shattered) - first person is back once it rallies", (void*)unit,
            moraleState);
        return;
    }
    bool flyer = false, flyAlways = false;
    if (cfg.fly && g_flyOk && g_moveOk && cfg.moveEnabled) {
        uint8_t canFly = 0, always = 0, canToggle = 0, isFly = 0;
        Rd(uiUnit + oUiCanFly, canFly);
        Rd(uiUnit + oUiAlwaysFly, always);
        Rd(uiUnit + oUiCanToggleFly, canToggle);
        Rd(uiUnit + oUiIsFlying, isFly);
        flyer = canFly != 0 || always != 0;
        flyAlways = always != 0;
        if (flyer)
            Log("fly: flying hero (CanFly %d, CanAlwaysFlying %d, CanToggleFlying %d, IsFlying %d, entity y %.2f) -> unit move orders, "
                "camera at the hero's own height, no sprint, no walking bob / footsteps",
                canFly, always, canToggle, isFly, pos[1]);
    }
    if (S.exitStage && unit != S.exitUnit) {
        Refuse(kRefusePaused);
        Log("enter refused: previous hero %p is still being restored (exit stage %d); unpause and try again", (void*)S.exitUnit,
            S.exitStage);
        return;
    }
    uintptr_t prev = Ptr(mgr + oMgrActive);
    if (!prev || !InExe(Ptr(prev))) {
        Refuse(kRefuseOther);
        Log("enter refused: active camera %p looks invalid", (void*)prev);
        return;
    }
    float cp[3], ct[3];
    S.yaw = 0;
    if (ReadVec(prev + oCamPos, cp) && ReadVec(prev + oCamTarget, ct)) {
        float dx = ct[0] - cp[0], dz = ct[2] - cp[2];
        if (dx * dx + dz * dz > 1e-6f) S.yaw = std::atan2(dx, dz);
    }
    S.pitch = 0;
    float fov = 0, farClip = 0, prevNear = 0;
    Rd(prev + oCamFov, fov);
    Rd(prev + oCamFar, farClip);
    Rd(prev + oCamNear, prevNear);
    if (cfg.fovOverride > 0) fov = cfg.fovOverride;
    if (std::isfinite(fov) && fov > 0) CamSetF(cam, vtSetFov, fov);
    S.baseFov = S.lastFov = (std::isfinite(fov) && fov > 0.2f && fov < 2.8f) ? fov : 0.f;
    S.sprint = S.tac = S.ads = false;
    S.shiftPrev = true;
    S.adsT = 0;
    S.fireBlock = 0;
    S.adsKind = 0;
    S.exitTac = false;
    S.camKick = kick::CamKick();
    S.camKick.rng.Seed(GetTickCount() ^ 0x5bd1e995u);
    CamSetF(cam, vtSetNear, cfg.nearClip);
    if (std::isfinite(farClip) && farClip > 10.f) CamSetF(cam, vtSetFar, farClip);
    unsigned char done = 0;
    Rd(cam + oCamDone, done);
    if (done) Wr<unsigned char>(cam + oCamDone, 0);
    S.mgr = mgr;
    S.battle = battle;
    S.fpCtrl = fpCtrl;
    S.cam = cam;
    S.prevActive = prev;
    S.unit = unit;
    S.flyer = flyer;
    S.entity = entity;
    {
        static float s_cfgEye = -1.f, s_lastAuto = -1.f;
        if (s_cfgEye < 0.f || cfg.eyeHeight != s_lastAuto) s_cfgEye = cfg.eyeHeight;
        float h = 0.f, eye = s_cfgEye;
        uintptr_t rec = Ptr(entity + oEntRecord);
        if (cfg.eyeFromHeight && !flyer && rec && Rd(rec + oRecHeight, h)) eye = EyeFromHeight(h, s_cfgEye);
        cfg.eyeHeight = s_lastAuto = eye;
        Log("ghost: entity height %.2f m -> eye height %.2f m (config %.2f%s)", h, eye, s_cfgEye,
            !cfg.eyeFromHeight ? ", eye_from_height off" : flyer ? ", flyer" : "");
        Log("ghost: learned teleport lead by speed band (walk / run / sprint / tactical sprint): %.3f %.3f %.3f %.3f s",
            cfg.tpLead + g_leadAdj[0], cfg.tpLead + g_leadAdj[1], cfg.tpLead + g_leadAdj[2], cfg.tpLead + g_leadAdj[3]);
    }
    S.entCount = entCount;
    S.entPath = path;
    S.havePos = false;
    S.skipFirstCursor = true;
    S.moving = false;
    S.lead[0] = S.lead[1] = 0;
    S.havePrev = false;
    S.speedEst = cfg.predictSpeed;
    S.camSpeed = 0;
    S.tMoveStart = S.tHalt = S.tWatch = -1;
    S.unitSync = false;
    S.kinInit = false;
    S.kinVel[0] = S.kinVel[1] = 0;
    S.lastMode = -1;
    memset(S.tpHist, 0, sizeof S.tpHist);
    S.stuck = false;
    S.tNoFollow = -1;
    S.pinned = false;
    S.pinLogs = 0;
    S.meleeSkipUnit = 0;
    S.meleeLogs = 0;
    S.loggedProj = false;
    S.tAliveCheck = 0;
    S.shotLogs = S.moveLogs = S.latLogs = S.tpLogs = 0;
    S.tpSent = S.tpFailed = S.leashHits = 0;
    memset(S.shotHist, 0, sizeof S.shotHist);
    S.shotHead = 0;
    S.tpGuarded = S.tpGuardSkipped = S.guardLogs = 0;
    S.lastAct = 0;
    S.actLogs = 0;
    S.tStat = Now();
    S.frames = S.writes = S.yields = 0;
    S.sumDx = S.sumDy = 0;
    InterlockedExchange(&g_rawDx, 0);
    InterlockedExchange(&g_rawDy, 0);
    SetupMouse();
    CastReset();
    S.active = true;
    vmgame::SetFirstPerson(true);
    if (S.exitStage == 2) {
        Log("ghost: pending release cancelled - first person entered again");
        S.exitStage = 0;
    }
    S.ghostWanted = cfg.ghost && g_ghostOk && g_tpOk && cfg.moveEnabled && MoveMode() == 0;
    S.ghost = false;
    S.ghostLogs = 0;
    S.heroAlliance = -1;
    S.revealLogs = 0;
    if (S.ghostWanted && cfg.revealEnemies && !g_revealOk)
        Log("reveal: always-visible command not verified on this exe; enemies may stay hidden by fog of war while the hero is disabled");
    if (cfg.ghost && !S.ghostWanted)
        Log("ghost: not used (needs mode=teleport and verified commands: ghost=%d teleport=%d move=%d mode=%s)", g_ghostOk, g_tpOk,
            cfg.moveEnabled, ModeName(MoveMode()));
    if (!S.ghostWanted)
        Log("hero: stays in battle (enemies see, chase, shoot and melee it; dies = first person ends; camera locks onto it "
            "while it is engaged)");
    ProbeInit();
    VelReset();
    {
        uintptr_t sbf = Ptr(g_base + kGlobalScript);
        if (cfg.fastFire && g_shotOk) FastFireInstall(sbf ? Ptr(sbf + oScriptBattle) : 0);
        if (cfg.earlyFlight && g_shotOk && cfg.fireShooter) EarlyFlightInstall(sbf ? Ptr(sbf + oScriptBattle) : 0, entity);
        if (sbf && Ptr(sbf + oScriptBattle)) LogUnitLists(Ptr(sbf + oScriptBattle));
    }
    S.fpProj = 0;
    S.fpProjId = -1;
    S.projFor = vmgame::CurrentWeapon();
    for (int32_t& id : S.meleeProjId) id = -1;
    S.strikeLogs = 0;
    S.tracerLogs = 0;
    S.movingLogs = 0;
    S.aimShots = S.aimSoldier = S.aimGround = S.aimRange = S.aimMoving = 0;
    S.aimSpeedSum = S.aimSpeedMax = S.aimLeadSum = S.aimSelfSpeedSum = 0;
    S.tAimStat = Now();
    const char* wantProj = vmgame::WeaponProjectile(S.projFor)[0] ? vmgame::WeaponProjectile(S.projFor) : cfg.fireProjectile;
    if (g_shotOk && wantProj[0]) {
        S.fpProj = FindProjectile(wantProj, &S.fpProjId);
        if (S.fpProj && S.fpProjId >= 0) {
            float sp = 0;
            int32_t ap = 0, n = 0;
            Rd(S.fpProj + 0x64, sp);
            Rd(S.fpProj + 0x74, ap);
            Rd(S.fpProj + 0x4c, n);
            Log("fire: first-person gun projectile '%s' id=%d record %p (speed %.0f, ap %d, per shot %d)", wantProj,
                S.fpProjId, (void*)S.fpProj, sp, ap, n);
        } else {
            S.fpProj = 0;
            int32_t vid = -1;
            uintptr_t vrec = FindProjectile("wh2_cst_rifle_bullet", &vid);
            Log("fire: projectile '%s' not found (is hero_fps.pack enabled in the launcher?) - using the hero's own weapon; "
                "lookup self-test: vanilla 'wh2_cst_rifle_bullet' -> %s (id %d, record %p)",
                wantProj, vrec ? "found" : "NOT FOUND", vid, (void*)vrec);
        }
    }
    {
        uintptr_t sb2 = Ptr(g_base + kGlobalScript), bt = sb2 ? Ptr(sb2 + oScriptBattle) : 0;
        uintptr_t w440 = bt ? Ptr(bt + 0x440) : 0, yf0 = w440 ? Ptr(w440 + 0xf0) : 0, lis = yf0 ? yf0 + 0x3d8 : 0;
        uintptr_t lvt = lis ? Ptr(lis) : 0, l10 = lvt ? Ptr(lvt + 0x10) : 0, yvt = yf0 ? Ptr(yf0) : 0;
        auto rva = [](uintptr_t a) { return (unsigned long long)(a > g_base ? a - g_base : 0); };
        Log("probe: launch-event receiver obj=%p (vt +%llx) member +0x3d8 vt +%llx, [+0x10] = +%llx, [+0x18] = +%llx, [+0x8] = +%llx",
            (void*)yf0, rva(yvt), rva(lvt), rva(l10), rva(lvt ? Ptr(lvt + 0x18) : 0), rva(lvt ? Ptr(lvt + 0x8) : 0));
    }
    if (cfg.fireShooter && entity) {
        uintptr_t sb3 = Ptr(g_base + kGlobalScript);
        herofx::Install(sb3 ? Ptr(sb3 + oScriptBattle) : 0, entity, S.fpProj);
    }
    if (cfg.fireShooter && entity) {
        uintptr_t evt = Ptr(entity), f48 = evt ? Ptr(evt + 0x48) : 0;
        Log("fire: shooter = hero entity (kills counted, own bullets pass through it); muzzle launch point %s "
            "(entity vt +%llx, [+0x48] = +%llx)",
            f48 == g_base + kFnRetZero ? "kept" : "REPLACED by the hero model's gun", (unsigned long long)(evt > g_base ? evt - g_base : 0),
            (unsigned long long)(f48 > g_base ? f48 - g_base : 0));
    }
    HookRenderChild();
    if (g_moveOk && cfg.moveEnabled && (MoveMode() == 2 || MoveMode() == 0)) IssueHalt();
    if (S.flyer && cfg.flyTakeoff && !flyAlways) IssueDesiredFlying(true);
    Log("FPS enter mgr=%p battle=%p fpCtrl=%p cam=%p prevActive=%p(vt %p) unit=%p entities=%u entity=%p "
        "pos=%.2f,%.2f,%.2f yaw=%.1f fov=%.4f near=%.3f(prev %.3f) far=%.1f camDone=%u selCount=%d",
        (void*)mgr, (void*)battle, (void*)fpCtrl, (void*)cam, (void*)prev, (void*)Ptr(prev), (void*)unit,
        entCount, (void*)entity, pos[0], pos[1], pos[2], S.yaw * 180.f / kPi, fov, cfg.nearClip, prevNear,
        farClip, done, selCount);
}
uintptr_t FindProjectile(const char* key, int32_t* idOut) {
    *idOut = -1;
    if (!key || !key[0]) return 0;
    uintptr_t sb = Ptr(g_base + kGlobalScript);
    uintptr_t battle = sb ? Ptr(sb + oScriptBattle) : 0;
    uintptr_t dbo = battle ? Ptr(battle + 0x430) : 0;
    uintptr_t db = dbo ? Ptr(dbo + 0x20) : 0;
    if (!db) return 0;
    using FnTable = uintptr_t (*)(uintptr_t);
    using FnFind = uint32_t (*)(uintptr_t, void*);
    using FnAt = uintptr_t (*)(uintptr_t, uint32_t);
    using FnStr = void* (*)(void*, const char*);
    using FnStrDel = void (*)(void*);
    uintptr_t table = reinterpret_cast<FnTable>(g_base + kFnProjTable)(db);
    if (!table) return 0;
    alignas(16) unsigned char str[64] = {};
    reinterpret_cast<FnStr>(g_base + kFnStrCtor)(str, key);
    uint32_t idx = reinterpret_cast<FnFind>(g_base + kFnTableFind)(table, str);
    reinterpret_cast<FnStrDel>(g_base + kFnStrDtor)(str);
    uintptr_t rec = reinterpret_cast<FnAt>(g_base + kFnTableAt)(table, idx);
    if (rec) Rd(rec + 8, *idOut);
    return rec;
}
float SegDist2D(float px, float pz, const float* a, const float* b) {
    float ux = b[0] - a[0], uz = b[2] - a[2], wx = px - a[0], wz = pz - a[2];
    float uu = ux * ux + uz * uz;
    float k = uu > 1e-6f ? (wx * ux + wz * uz) / uu : 0.f;
    if (k < 0.f) k = 0.f;
    if (k > 1.f) k = 1.f;
    float dx = wx - ux * k, dz = wz - uz * k;
    return std::sqrt(dx * dx + dz * dz);
}
bool MakeDestAt(float x, float z, float* dest, uintptr_t hint = 0);
struct Surf {
    float h;
    uintptr_t blk;
};
constexpr int kMaxSurf = 8;
constexpr uintptr_t kNoSurf = ~uintptr_t(0);
int SurfacesAt(float x, float z, Surf* out);
bool BlockNear(uintptr_t blk, float x, float z, float margin);
int PickSurface(const Surf* s, int n, uintptr_t cur, float foot, float up);
int WalkPick(const Surf* s, int n, uintptr_t cur, float foot, float up, float down, bool* edge);
bool SurfaceNear(float x, float z, float refY, float* h, uintptr_t* blk = nullptr);
struct AimHit {
    float dist = 0;
    const char* what = "range";
    uintptr_t ent = 0;
    float vel[2] = {0, 0};
    unsigned units = 0, ents = 0;
    unsigned snapCulled = 0;
};
AimHit TraceAim(const float eye[3], const float fwd[3], float range, float dispLag);
constexpr uintptr_t kSimTickCall904 = 0x2dd393f;
const unsigned char kSimTickCallBytes[20] = {0x48, 0x8b, 0x87, 0x30, 0x04, 0x00, 0x00, 0x48, 0x8b, 0x48,
                                             0x58, 0x48, 0x8b, 0x01, 0xff, 0x90, 0xb0, 0x01, 0x00, 0x00};
constexpr uintptr_t kSimTickSlot = 0x1b0;
struct FastFire {
    int siteState = 0;
    uintptr_t obj = 0, origVt = 0, battle = 0;
    uintptr_t* vtCopy = nullptr;
    uintptr_t vtCopyOf = 0;
    SRWLOCK lock = SRWLOCK_INIT;
    unsigned char buf[0x2000];
    uint32_t n = 0;
    double tPend[64] = {};
    int nPend = 0;
    volatile LONG ticks = 0, injected = 0, multiBlock = 0, dropped = 0, undone = 0;
    volatile LONG64 lastTickQpc = 0;
    uint32_t maxBlocks = 0;
    double latSum = 0, latMax = 0;
    unsigned latN = 0;
    volatile DWORD simTid = 0;
    uintptr_t lastPacket = 0, lastBlock = 0;
    uint32_t lastOldSize = 0, lastNewSize = 0, lastOldBlocks = 0;
    unsigned char lastHead[16] = {};
    double tStat = 0;
} g_ff;
alignas(16) unsigned char g_fakeQueue[0x5010];
bool CodeRange(uintptr_t* lo, uintptr_t* hi) {
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    uintptr_t probe = g_base + kFnIssueCmd;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        uintptr_t a = g_base + sec[i].VirtualAddress, b = a + sec[i].Misc.VirtualSize;
        if (probe >= a && probe < b) {
            *lo = a;
            *hi = b;
            return true;
        }
    }
    return false;
}
bool FastFireInstall(uintptr_t battle) {
    if (!cfg.fastFire || !battle) return false;
    if (g_ff.siteState == 0) {
        unsigned char b[20] = {};
        for (int i = 0; i < 20; ++i) Rd(g_base + kSimTickCall904 + i, b[i]);
        g_ff.siteState = memcmp(b, kSimTickCallBytes, 20) ? -1 : 1;
        if (g_ff.siteState > 0) g_sim_tick_ret = g_base + kSimTickCall904 + 20;
        Log("fast-fire: sim_step call site %s (+%llx)", g_ff.siteState > 0 ? "verified" : "NOT matching (other game version) - fast fire off",
            (unsigned long long)kSimTickCall904);
    }
    if (g_ff.siteState < 0) return false;
    uintptr_t ctx = Ptr(battle + 0x430), obj = ctx ? Ptr(ctx + 0x58) : 0, vt = obj ? Ptr(obj) : 0;
    uintptr_t lo = 0, hi = 0;
    if (!vt || !CodeRange(&lo, &hi)) {
        Log("fast-fire: sim tick object not found (ctx %p obj %p vt %p)", (void*)ctx, (void*)obj, (void*)vt);
        return false;
    }
    if (g_ff.vtCopy && vt == reinterpret_cast<uintptr_t>(g_ff.vtCopy + 1)) {
        g_ff.obj = obj;
        g_ff.battle = battle;
        return true;
    }
    size_t slots = 0;
    while (slots < 1024) {
        uintptr_t f = Ptr(vt + 8 * slots);
        if (f < lo || f >= hi) break;
        ++slots;
    }
    if (slots <= kSimTickSlot / 8) {
        Log("fast-fire: sim tick object %p vtable %p has only %zu slots - not hooking", (void*)obj, (void*)vt, slots);
        return false;
    }
    if (!g_ff.vtCopy || g_ff.vtCopyOf != vt) {
        size_t copyN = slots + 16 > 256 ? slots + 16 : 256;
        auto* c = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (copyN + 1) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!c) return false;
        c[0] = Ptr(vt - 8);
        for (size_t i = 0; i < copyN; ++i) c[1 + i] = Ptr(vt + 8 * i);
        g_orig_sim_tick = reinterpret_cast<void*>(c[1 + kSimTickSlot / 8]);
        c[1 + kSimTickSlot / 8] = reinterpret_cast<uintptr_t>(&hfps_sim_tick_thunk);
        DWORD old;
        VirtualProtect(c, (copyN + 1) * sizeof(uintptr_t), PAGE_READONLY, &old);
        g_ff.vtCopy = c;
        g_ff.vtCopyOf = vt;
    }
    AcquireSRWLockExclusive(&g_ff.lock);
    g_ff.n = 0;
    g_ff.nPend = 0;
    g_ff.lastPacket = 0;
    g_ff.battle = battle;
    ReleaseSRWLockExclusive(&g_ff.lock);
    g_ff.multiBlock = g_ff.dropped = g_ff.injected = g_ff.ticks = g_ff.undone = 0;
    g_ff.maxBlocks = 0;
    g_ff.latSum = g_ff.latMax = 0;
    g_ff.latN = 0;
    g_ff.tStat = Now();
    if (!Wr(obj, reinterpret_cast<uintptr_t>(g_ff.vtCopy + 1))) {
        Log("fast-fire: vtable write failed on %p", (void*)obj);
        g_ff.battle = 0;
        return false;
    }
    g_ff.obj = obj;
    g_ff.origVt = vt;
    Log("fast-fire: hooked sim tick object %p (vt +%llx, %zu slots, [+0x1b0] = +%llx) via vtable copy %p", (void*)obj,
        (unsigned long long)(vt - g_base), slots, (unsigned long long)(reinterpret_cast<uintptr_t>(g_orig_sim_tick) - g_base),
        (void*)(g_ff.vtCopy + 1));
    return true;
}
void FastFireStats(bool force) {
    double now = Now();
    if (!g_ff.obj || (!force && now - g_ff.tStat < 3.0)) return;
    g_ff.tStat = now;
    AcquireSRWLockExclusive(&g_ff.lock);
    double avg = g_ff.latN ? g_ff.latSum / g_ff.latN : 0, mx = g_ff.latMax;
    unsigned nl = g_ff.latN;
    g_ff.latSum = g_ff.latMax = 0;
    g_ff.latN = 0;
    ReleaseSRWLockExclusive(&g_ff.lock);
    Log("fast-fire: sim ticks %ld, shots injected %ld (fire -> executed avg %.0f ms, max %.0f ms over %u), dropped %ld, undone %ld, "
        "packet blocks max %u%s, sim thread %lu; early flight: post_step %ld, bullets moved %ld",
        g_ff.ticks, g_ff.injected, avg, mx, nl, g_ff.dropped, g_ff.undone, g_ff.maxBlocks,
        g_ff.multiBlock ? " (MULTI-BLOCK: multiplayer? fast fire off)" : "", (unsigned long)g_ff.simTid, EarlyFlightCalls(),
        EarlyFlightPromoted());
}
void FastFireUninstall(const char* why) {
    if (!g_ff.obj) return;
    FastFireStats(true);
    if (g_ff.vtCopy && Ptr(g_ff.obj) == reinterpret_cast<uintptr_t>(g_ff.vtCopy + 1)) Wr(g_ff.obj, g_ff.origVt);
    AcquireSRWLockExclusive(&g_ff.lock);
    g_ff.battle = 0;
    g_ff.n = 0;
    g_ff.nPend = 0;
    g_ff.lastPacket = 0;
    ReleaseSRWLockExclusive(&g_ff.lock);
    Log("fast-fire: unhooked sim tick object %p (%s)", (void*)g_ff.obj, why);
    g_ff.obj = 0;
}
bool FastFireReady(uintptr_t battle) {
    if (!g_ff.obj || g_ff.battle != battle || g_ff.multiBlock) return false;
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart - g_ff.lastTickQpc < g_qpf.QuadPart / 2;
}
constexpr uintptr_t kPostStepCall904 = 0x2dda892, kPostStepRet904 = 0x2dda8b8, kProjMergeCall904 = 0x30ba92c, kProjMerge904 = 0x132c544,
                    kProjUpdCall904 = 0x2dda9a7, kProjUpd904 = 0x30ba7a8;
const unsigned char kPostStepCallBytes[38] = {0x48, 0x8b, 0x43, 0x08, 0x4c, 0x8b, 0x88, 0x40, 0x04, 0x00, 0x00, 0x8b, 0x83,
                                              0x98, 0x00, 0x00, 0x00, 0x49, 0x8b, 0x89, 0xf0, 0x00, 0x00, 0x00, 0x48, 0x83,
                                              0xc1, 0x58, 0x89, 0x44, 0x24, 0x60, 0x48, 0x8b, 0x01, 0xff, 0x50, 0x10};
struct EarlyFlight {
    int siteState = 0;
    uintptr_t obj = 0, origVt = 0, world = 0, hero = 0;
    uintptr_t* vtCopy = nullptr;
    uintptr_t vtCopyOf = 0;
    volatile LONG calls = 0, promoted = 0, maxPending = 0;
    volatile LONG64 lastQpc = 0;
} g_ef;
bool EarlyFlightInstall(uintptr_t battle, uintptr_t heroEntity) {
    if (!cfg.earlyFlight || !battle || !heroEntity) return false;
    if (g_ef.siteState == 0) {
        unsigned char b[38] = {};
        for (int i = 0; i < 38; ++i) Rd(g_base + kPostStepCall904 + i, b[i]);
        int32_t r1 = 0, r2 = 0;
        unsigned char o1 = 0, o2 = 0;
        Rd(g_base + kProjMergeCall904, o1);
        Rd(g_base + kProjMergeCall904 + 1, r1);
        Rd(g_base + kProjUpdCall904 + 7, o2);
        Rd(g_base + kProjUpdCall904 + 8, r2);
        uint32_t off1c8 = 0;
        Rd(g_base + kProjUpdCall904 + 3, off1c8);
        bool ok = !memcmp(b, kPostStepCallBytes, 38) && o1 == 0xe8 && kProjMergeCall904 + 5 + r1 == kProjMerge904 && o2 == 0xe8 &&
                  kProjUpdCall904 + 12 + r2 == kProjUpd904 && off1c8 == 0x1c8;
        g_ef.siteState = ok ? 1 : -1;
        if (ok) g_post_step_ret = g_base + kPostStepRet904;
        Log("early-flight: post_step call site / projectile merge %s", ok ? "verified" : "NOT matching (other game version) - off");
    }
    if (g_ef.siteState < 0) return false;
    uintptr_t world = Ptr(battle + 0x440), y = world ? Ptr(world + 0xf0) : 0, obj = y ? y + 0x58 : 0, vt = obj ? Ptr(obj) : 0;
    uintptr_t lo = 0, hi = 0;
    if (!vt || !CodeRange(&lo, &hi)) {
        Log("early-flight: dispatcher not found (world %p [+0xf0] %p vt %p)", (void*)world, (void*)y, (void*)vt);
        return false;
    }
    if (g_ef.vtCopy && vt == reinterpret_cast<uintptr_t>(g_ef.vtCopy + 1)) {
        g_ef.obj = obj;
        g_ef.world = world;
        g_ef.hero = heroEntity;
        return true;
    }
    size_t slots = 0;
    while (slots < 1024) {
        uintptr_t f = Ptr(vt + 8 * slots);
        if (f < lo || f >= hi) break;
        ++slots;
    }
    if (slots < 3) {
        Log("early-flight: dispatcher %p vtable %p has only %zu slots - not hooking", (void*)obj, (void*)vt, slots);
        return false;
    }
    if (!g_ef.vtCopy || g_ef.vtCopyOf != vt) {
        size_t copyN = slots + 16 > 256 ? slots + 16 : 256;
        auto* c = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (copyN + 1) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!c) return false;
        c[0] = Ptr(vt - 8);
        for (size_t i = 0; i < copyN; ++i) c[1 + i] = Ptr(vt + 8 * i);
        g_orig_post_step = reinterpret_cast<void*>(c[1 + 2]);
        c[1 + 2] = reinterpret_cast<uintptr_t>(&hfps_post_step_thunk);
        DWORD old;
        VirtualProtect(c, (copyN + 1) * sizeof(uintptr_t), PAGE_READONLY, &old);
        g_ef.vtCopy = c;
        g_ef.vtCopyOf = vt;
    }
    g_ef.world = world;
    g_ef.hero = heroEntity;
    g_ef.calls = g_ef.promoted = g_ef.maxPending = 0;
    if (!Wr(obj, reinterpret_cast<uintptr_t>(g_ef.vtCopy + 1))) {
        Log("early-flight: vtable write failed on %p", (void*)obj);
        g_ef.world = 0;
        return false;
    }
    g_ef.obj = obj;
    g_ef.origVt = vt;
    Log("early-flight: hooked post_step dispatcher %p (vt +%llx, %zu slots, [+0x10] = +%llx) via vtable copy %p; hero entity %p", (void*)obj,
        (unsigned long long)(vt - g_base), slots, (unsigned long long)(reinterpret_cast<uintptr_t>(g_orig_post_step) - g_base),
        (void*)(g_ef.vtCopy + 1), (void*)heroEntity);
    return true;
}
namespace hitprobe {
void OnExit(const char* why);
}
void EarlyFlightUninstall(const char* why) {
    hitprobe::OnExit(why);
    if (!g_ef.obj) return;
    if (g_ef.vtCopy && Ptr(g_ef.obj) == reinterpret_cast<uintptr_t>(g_ef.vtCopy + 1)) Wr(g_ef.obj, g_ef.origVt);
    g_ef.world = 0;
    Log("early-flight: unhooked (%s): post_step calls %ld, our bullets moved to the in-flight list %ld (pending list max %ld)", why,
        g_ef.calls, g_ef.promoted, g_ef.maxPending);
    g_ef.obj = 0;
}
long EarlyFlightCalls() { return g_ef.calls; }
long EarlyFlightPromoted() { return g_ef.promoted; }
bool EarlyFlightActive() {
    if (!g_ef.obj || !g_ef.world) return false;
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart - g_ef.lastQpc < g_qpf.QuadPart / 2;
}
bool FastFireQueue(FireCmd* c) {
    using FnIssue = unsigned char (*)(uintptr_t queue, FireCmd* cmd);
    *reinterpret_cast<uint32_t*>(g_fakeQueue + 0x5008) = 0;
    unsigned char ok = reinterpret_cast<FnIssue>(g_base + kFnIssueCmd)(reinterpret_cast<uintptr_t>(g_fakeQueue), c);
    uint32_t n = *reinterpret_cast<uint32_t*>(g_fakeQueue + 0x5008);
    if (!ok || n < 7 || n > 0x400) return false;
    bool queued = false;
    AcquireSRWLockExclusive(&g_ff.lock);
    if (g_ff.battle && g_ff.n + n <= sizeof g_ff.buf && g_ff.nPend < 64) {
        memcpy(g_ff.buf + g_ff.n, g_fakeQueue + 8, n);
        g_ff.n += n;
        g_ff.tPend[g_ff.nPend++] = Now();
        queued = true;
    }
    ReleaseSRWLockExclusive(&g_ff.lock);
    return queued;
}
struct MeleeKind {
    const char* proj;
    float reach;
};
const float kMeleeFlight = 1.5f;
const float kMeleeBurstUp = 0.4f;
const MeleeKind kMeleeKinds[vmgame::kStrikeKinds] = {
    {"tenzin_fps_melee_stab", 2.0f},
    {"tenzin_fps_melee_sweep", 1.6f},
    {"tenzin_fps_melee_heavy_sweep", 1.7f},
    {"tenzin_fps_melee_heavy", 2.0f},
    {"tenzin_fps_melee_bash", 1.3f},
};
bool MeleeStrike(int kind, const float eye[3], const float fwd[3]) {
    if (kind < 0 || kind >= vmgame::kStrikeKinds) return false;
    const MeleeKind& mk = kMeleeKinds[kind];
    uintptr_t sb = Ptr(g_base + kGlobalScript);
    uintptr_t battle = sb ? Ptr(sb + oScriptBattle) : 0;
    uintptr_t queue = battle ? Ptr(battle + oBattleCmdQueue) : 0;
    unsigned char no1 = 1, no2 = 1;
    if (battle) {
        Rd(battle + oBattleNoCmd1, no1);
        Rd(battle + oBattleNoCmd2, no2);
    }
    int32_t entId = 0;
    if (S.entity) Rd(S.entity + oEntId, entId);
    int32_t& id = S.meleeProjId[kind];
    if (id == -1 && battle) {
        int32_t got = -1;
        uintptr_t rec = FindProjectile(mk.proj, &got);
        id = rec && got >= 0 ? got : -2;
        if (id >= 0) herofx::Track(rec);
        Log("melee: strike projectile '%s' -> %s (id %d)", mk.proj, id >= 0 ? "found" : "NOT FOUND (hero_fps.pack out of date?)", got);
    }
    if (!battle || !queue || no1 || no2 || id < 0 || !entId) {
        if (S.strikeLogs++ < 8)
            Log("melee: strike refused: battle=%p queue=%p blocked=%u/%u projectile id=%d entityId=%d", (void*)battle, (void*)queue, no1,
                no2, id, entId);
        return false;
    }
    float hx = fwd[0], hz = fwd[2], hl = std::sqrt(hx * hx + hz * hz);
    if (hl < 1e-3f) return false;
    hx /= hl;
    hz /= hl;
    FireCmd c;
    memset(&c, 0, sizeof c);
    c.vt = g_base + kVtFireCmd;
    c.projId = id;
    c.f0c = 6.2831855f;
    float burst[3] = {eye[0] + hx * mk.reach, eye[1] - 1.3f, eye[2] + hz * mk.reach};
    float gh = 0.f;
    bool ground = SurfaceNear(burst[0], burst[2], eye[1] - cfg.eyeHeight, &gh) && gh < eye[1] + 3.f && gh > eye[1] - 10.f;
    if (ground) burst[1] = gh + kMeleeBurstUp;
    float dir[3] = {burst[0] - eye[0], burst[1] - (eye[1] - 0.5f), burst[2] - eye[2]};
    float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (dl < 0.1f) return false;
    for (int i = 0; i < 3; ++i) {
        dir[i] /= dl;
        c.launch[i] = burst[i] - dir[i] * kMeleeFlight;
        c.target[i] = burst[i];
    }
    c.tgtIsPos = 1;
    c.shooter = S.entity;
    using FnIssue = unsigned char (*)(uintptr_t queue, FireCmd* cmd);
    bool viaFast = FastFireReady(battle) && FastFireQueue(&c);
    unsigned char ok = viaFast ? 1 : reinterpret_cast<FnIssue>(g_base + kFnIssueCmd)(queue, &c);
    if (S.strikeLogs++ < 60)
        Log("melee: strike %s %s via %s: launch %.2f,%.2f,%.2f -> burst %.2f,%.2f,%.2f (%s)", mk.proj, ok ? "issued" : "REJECTED",
            viaFast ? "fast queue" : "command queue", c.launch[0], c.launch[1], c.launch[2], c.target[0], c.target[1], c.target[2],
            ground ? "ground + 0.4 m" : "no ground hit, eye - 1.3 m");
    return ok != 0;
}
unsigned UnitEntities(uintptr_t u, uintptr_t* ents, unsigned cap);
bool EntPos(uintptr_t e, float* q);
struct MeleeArc {
    float reach;
    float halfDeg;
    float dmg;
    int maxHit;
    bool damage;
};
const MeleeArc kMeleeArcs[vmgame::kStrikeKinds] = {
    {2.7f, 25.f, 1.0f, 2, true},
    {2.5f, 65.f, 0.8f, 4, true},
    {2.6f, 80.f, 1.2f, 8, true},
    {2.8f, 40.f, 1.6f, 3, true},
    {2.2f, 60.f, 0.0f, 5, false},
};
struct MeleeKindCfg {
    float reach;
    float halfDeg;
    float dmg;
    int maxHit;
    bool damage;
    float splashR;
    float splashDmg;
    int splashMax;
};
struct MeleeCatCfg {
    MeleeKindCfg k[vmgame::kStrikeKinds];
    float damageScale;
    float fallback;
    bool alwaysHit;
    float blockAngle;
};
MeleeCatCfg g_meleeCat[vmgame::kMeleeCats];
const wchar_t* const kMeleeKindKey[vmgame::kStrikeKinds] = {L"stab", L"sweep", L"heavy_sweep", L"heavy", L"bash"};
void LoadMeleeCats(const wchar_t* ini) {
    for (int c = 0; c < vmgame::kMeleeCats; ++c) {
        const wchar_t* sec = vmgame::MeleeCatSection(c);
        MeleeCatCfg& m = g_meleeCat[c];
        char line[640];
        int ln = snprintf(line, sizeof line, "melee-cat: %ls", sec);
        for (int k = 0; k < vmgame::kStrikeKinds; ++k) {
            const MeleeArc& a = kMeleeArcs[k];
            const bool heavy = k == vmgame::kStrikeHeavySweep || k == vmgame::kStrikeHeavy;
            auto get = [&](const wchar_t* what, float def, float lo, float hi) {
                wchar_t key[48];
                swprintf(key, 48, L"%ls_%ls", kMeleeKindKey[k], what);
                float v = IniF(sec, key, def, ini);
                return (v >= lo && v <= hi) ? v : def;
            };
            MeleeKindCfg& o = m.k[k];
            o.reach = get(L"reach", a.reach * (heavy ? cfg.meleeHeavyReach : 1.f) + 0.3f, 0.3f, 12.f);
            o.halfDeg = get(L"arc", a.halfDeg * 2.f, 1.f, 360.f) * 0.5f;
            o.dmg = get(L"damage", a.damage ? a.dmg : 0.f, 0.f, 20.f);
            o.damage = o.dmg > 0.f;
            o.maxHit = static_cast<int>(get(L"max", static_cast<float>(a.maxHit), 0.f, 16.f) + 0.5f);
            o.splashR = get(L"splash_radius", heavy ? cfg.meleeHeavySplashRadius : cfg.meleeSplashRadius, 0.f, 6.f);
            o.splashDmg = get(L"splash_damage", heavy ? cfg.meleeHeavySplashDamage : cfg.meleeSplashDamage, 0.f, 5.f);
            o.splashMax = static_cast<int>(get(L"splash_max", static_cast<float>(heavy ? cfg.meleeHeavySplashMax : cfg.meleeSplashMax), 0.f, 32.f) + 0.5f);
            if (ln > 0 && ln < static_cast<int>(sizeof line))
                ln += snprintf(line + ln, sizeof line - ln, " | %ls %.2fm %.0fdeg x%.2f n%d s%.1f/%.2f/%d", kMeleeKindKey[k], o.reach, o.halfDeg * 2.f,
                               o.dmg, o.maxHit, o.splashR, o.splashDmg, o.splashMax);
        }
        float ds = IniF(sec, L"damage_scale", cfg.meleeDamage, ini);
        m.damageScale = (ds >= 0.f && ds <= 20.f) ? ds : cfg.meleeDamage;
        float fb = IniF(sec, L"fallback_damage", cfg.meleeFallback, ini);
        m.fallback = (fb >= 0.f && fb <= 5000.f) ? fb : cfg.meleeFallback;
        m.alwaysHit = GetPrivateProfileIntW(sec, L"always_hit", cfg.meleeAlwaysHit ? 1 : 0, ini) != 0;
        float ba = IniF(sec, L"block_angle", -1.f, ini);
        m.blockAngle = (ba >= 0.f && ba <= 180.f) ? ba : -1.f;
        Log("%s | scale %.2f fallback %.0f always_hit %d block_angle %.0f", line, m.damageScale, m.fallback, m.alwaysHit ? 1 : 0, m.blockAngle);
    }
}
float ShieldAngleNow() {
    if (vmgame::MeleeActive()) {
        int c = vmgame::CurrentMeleeCat();
        if (c >= 0 && c < vmgame::kMeleeCats && g_meleeCat[c].blockAngle >= 0.f) return g_meleeCat[c].blockAngle;
    }
    return cfg.shieldAngle;
}
struct MeleeReq {
    int kind;
    int cat;
    float ox, oy, oz;
    float dx, dz;
    uintptr_t hero;
    double t;
};
struct MeleeQueue {
    SRWLOCK lock = SRWLOCK_INIT;
    MeleeReq q[8];
    int n = 0;
    volatile LONG strikes = 0, hits = 0, kills = 0, forced = 0, logs = 0, fallback = 0, diag = 0;
} g_mq;
struct MeleeAtk {
    uintptr_t attacker;
    uintptr_t zero[3];
    uint16_t attackIdx;
    uint16_t pad0;
    float one;
    float ten;
    uint32_t pad1;
    uint8_t flags;
    uint8_t pad2[7];
};
static_assert(sizeof(MeleeAtk) == 0x38 && __builtin_offsetof(MeleeAtk, attackIdx) == 0x20 && __builtin_offsetof(MeleeAtk, flags) == 0x30,
              "melee attacker block layout");
bool MeleeEngineReady() { return cfg.meleeEngine && g_meleeHitOk && EarlyFlightActive() && g_ef.world && g_ef.hero == S.entity; }
bool MeleeEngineQueue(int kind, int cat, const float eye[3], const float fwd[3]) {
    if (cat < 0 || cat >= vmgame::kMeleeCats) cat = vmgame::kMeleeCatSword;
    float hx = fwd[0], hz = fwd[2], hl = std::sqrt(hx * hx + hz * hz);
    if (kind < 0 || kind >= vmgame::kStrikeKinds || hl < 1e-3f || !S.entity) return false;
    MeleeReq r = {kind, cat, eye[0], eye[1], eye[2], hx / hl, hz / hl, S.entity, Now()};
    AcquireSRWLockExclusive(&g_mq.lock);
    bool ok = g_mq.n < 8;
    if (ok) g_mq.q[g_mq.n++] = r;
    ReleaseSRWLockExclusive(&g_mq.lock);
    return ok;
}
int MeleeEngineHit(uintptr_t world, uintptr_t hero, uintptr_t victim, const MeleeKindCfg& arc, const MeleeCatCfg& cc, float frac, int* dmgOut) {
    using FnCalc = int32_t* (*)(uintptr_t combat, int32_t* out, uintptr_t attacker, uintptr_t victim, uint32_t splitTargets);
    using FnCtor = void* (*)(void* hitInfo, uintptr_t victim);
    using FnDtor = void (*)(void* hitInfo);
    using FnKill = uint16_t* (*)(void* hitInfo, uint16_t* out, uint32_t damage, MeleeAtk* atk, char selfDeath0, int attackType,
                                 uint32_t p7, const char* debugName);
    using FnReact = void (*)(uintptr_t self, uintptr_t attacker, float* delta);
    using FnFx = void (*)(uintptr_t victim, uintptr_t fxMgr, uintptr_t attacker, int kind, float* pos, float* dir, float radius,
                          uint8_t* result, char flag);
    float hp[3], vp[3];
    if (!EntPos(hero, hp) || !EntPos(victim, vp)) return 0;
    int32_t r[2] = {0, 0};
    if (arc.damage) {
        uintptr_t combat = Ptr(world + 0xf8);
        if (!combat) return 0;
        uint32_t a58 = 0;
        Rd(hero + 0xa58, a58);
        reinterpret_cast<FnCalc>(g_base + kFnMeleeHitCalc904)(combat, r, hero, victim, 1);
        if (g_mq.diag < 40) {
            InterlockedIncrement(&g_mq.diag);
            int32_t rv[2] = {0, 0};
            uint32_t vhp = 0, vmax = 0;
            Rd(victim + 0xb1c, vhp);
            Rd(victim + 0xb18, vmax);
            reinterpret_cast<FnCalc>(g_base + kFnMeleeHitCalc904)(combat, rv, victim, hero, 1);
            Log("melee-engine diag: hero->enemy kind %d dmg %d | enemy->hero kind %d dmg %d | victim hp %u/%u | hero a58 %u", r[0], r[1],
                rv[0], rv[1], vhp, vmax, a58);
        }
        if (r[0] == 0) {
            if (!cc.alwaysHit) return 0;
            InterlockedIncrement(&g_mq.forced);
        }
    }
    alignas(16) unsigned char hi[0x100];
    memset(hi, 0, sizeof hi);
    reinterpret_cast<FnCtor>(g_base + kFnHitInfoCtor904)(hi, victim);
    uint16_t res = 0;
    int dmg = 0;
    if (arc.damage) {
        float base = static_cast<float>(r[1]);
        if (base <= 1.f && cc.fallback > 0.f) {
            base = cc.fallback;
            InterlockedIncrement(&g_mq.fallback);
        }
        float d = base * arc.dmg * frac * cc.damageScale;
        dmg = d < 1.f ? 1 : static_cast<int>(d + 0.5f);
        MeleeAtk atk;
        memset(&atk, 0, sizeof atk);
        atk.attacker = hero;
        atk.attackIdx = 0x8d4;
        atk.one = 1.f;
        atk.ten = 10.f;
        atk.flags = 0x10;
        reinterpret_cast<FnKill>(g_base + kFnKillResolve904)(hi, &res, static_cast<uint32_t>(dmg), &atk, 0, 0, 0, "HFPS MELEE ATTACK");
    }
    bool killed = (res & 0xff) != 0;
    float delta[3] = {vp[0] - hp[0], vp[1] - hp[1], vp[2] - hp[2]};
    if (!killed && cfg.meleeReact) {
        uintptr_t vt = Ptr(victim), fn = vt ? Ptr(vt + 0x3a8) : 0;
        if (fn >= g_base && fn < g_base + g_imageSize) reinterpret_cast<FnReact>(fn)(victim, hero, delta);
    }
    if (cfg.meleeEffects && (dmg > 0 || !arc.damage)) {
        uintptr_t fx = Ptr(world + 0xf0);
        float hh = 0.f, vh = 0.f, hr = 0.f, vr = 0.f;
        Rd(hero + 0x9c, hh);
        Rd(victim + 0x9c, vh);
        Rd(hero + 0xa0, hr);
        Rd(victim + 0xa0, vr);
        float h = hh < vh ? hh : vh, rad = hr < vr ? hr : vr;
        if (!(h > 0.3f && h < 20.f)) h = 1.8f;
        if (!(rad > 0.f && rad < 20.f)) rad = 0.5f;
        float pos[3] = {vp[0], vp[1] + h * 0.55f, vp[2]};
        if (fx) reinterpret_cast<FnFx>(g_base + kFnHitEffects904)(victim, fx, hero, 1, pos, delta, rad, reinterpret_cast<uint8_t*>(&res), 1);
    }
    reinterpret_cast<FnDtor>(g_base + kFnHitInfoDtor904)(hi);
    *dmgOut = dmg;
    return killed ? 2 : 1;
}
struct MeleeUnitExt {
    uintptr_t u;
    uint64_t sig;
    uint32_t n, step;
    float r;
};
uint64_t MeleeRosterSig(const uintptr_t* ents, unsigned n) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned i = 0; i < n; ++i) h = (h ^ ents[i]) * 1099511628211ull;
    return h;
}
constexpr unsigned kMeleeExtSlots = 2048;
constexpr float kMeleeStepSpread = 5.f;
float g_meleeTargetRadiusMax = 4.f;
constexpr float kMeleeBodyRef = 0.6f;
constexpr float kMeleeMountSlack = 1.f;
float MeleeBigSlack() { return (g_meleeTargetRadiusMax > kMeleeBodyRef ? g_meleeTargetRadiusMax - kMeleeBodyRef : 0.f) + kMeleeMountSlack; }
float MeleeTargetExtra(uintptr_t e) {
    if (!(g_meleeTargetRadiusMax > kMeleeBodyRef)) return 0.f;
    float r = 0.f;
    if (!Rd(e + 0xa0, r) || !(r > kMeleeBodyRef && r < 50.f)) return 0.f;
    return (r < g_meleeTargetRadiusMax ? r : g_meleeTargetRadiusMax) - kMeleeBodyRef;
}
float MeleeWideCos(float halfDeg, float extra, float d) {
    float a = halfDeg + (extra >= d ? 90.f : std::asin(extra / d) * 180.f / kPi);
    return a >= 180.f ? -2.f : std::cos(a * kPi / 180.f);
}
int g_meleeBigLogs = 0;
int g_meleeCrewLogs = 0;
MeleeUnitExt g_mext[kMeleeExtSlots];
uintptr_t g_mextWorld = 0;
struct MeleeSoldier {
    uintptr_t e;
    float q[3];
};
constexpr unsigned kMeleeGatherCap = 65536;
MeleeSoldier g_mgather[kMeleeGatherCap];
MeleeUnitExt* MeleeExt(uintptr_t u, bool create) {
    unsigned h = static_cast<unsigned>((u >> 4) * 2654435761u) & (kMeleeExtSlots - 1);
    for (unsigned k = 0; k < 32; ++k) {
        MeleeUnitExt& x = g_mext[(h + k) & (kMeleeExtSlots - 1)];
        if (x.u == u) return &x;
        if (!x.u) {
            if (!create) return nullptr;
            memset(&x, 0, sizeof x);
            x.u = u;
            return &x;
        }
    }
    return nullptr;
}
unsigned MeleeGather(uintptr_t world, uintptr_t battle, uintptr_t hero, uintptr_t heroUnit, const float (*org)[3], int no, unsigned* unitsSkipped,
                     unsigned* unitsAll) {
    static uintptr_t ents[1024];
    unsigned ng = 0;
    *unitsSkipped = *unitsAll = 0;
    if (world != g_mextWorld) {
        memset(g_mext, 0, sizeof g_mext);
        g_mextWorld = world;
    }
    uint32_t step = 0;
    const bool haveStep = battle && Rd(battle + 0x64720, step);
    uintptr_t list = Ptr(world + oWorldAlliances);
    uint32_t na = 0;
    if (!list || !Rd(list + oAlliancesCount, na) || na > 16) return 0;
    uintptr_t alliances = Ptr(list + oAlliancesArray);
    for (uint32_t i = 0; i < na && alliances; ++i) {
        uintptr_t al = Ptr(alliances + 8 * i);
        uint32_t nArmies = 0;
        if (!al || !Rd(al + oAllianceArmyCount, nArmies) || nArmies > 64) continue;
        uintptr_t armies = Ptr(al + oAllianceArmyArray);
        for (uint32_t j = 0; j < nArmies && armies; ++j) {
            uintptr_t army = Ptr(armies + 8 * j);
            uint32_t nu = 0;
            if (!army || !Rd(army + oArmyUnitCount, nu) || nu > 1024) continue;
            uintptr_t units = Ptr(army + oArmyUnitArray);
            for (uint32_t k = 0; k < nu && units; ++k) {
                uintptr_t u = Ptr(units + 8 * k);
                if (!u || u == heroUnit) continue;
                unsigned ne = UnitEntities(u, ents, 1024);
                if (!ne) continue;
                ++*unitsAll;
                float q0[3];
                const bool p0 = ents[0] != hero && Plausible(ents[0]) && EntPos(ents[0], q0);
                const uint64_t sig = MeleeRosterSig(ents, ne);
                if (p0 && haveStep) {
                    const MeleeUnitExt* x = MeleeExt(u, false);
                    if (x && x->sig == sig && x->n == ne && step >= x->step && step - x->step < 600) {
                        float slack = x->r + kMeleeStepSpread * static_cast<float>(step - x->step + 1) + 1.f;
                        bool reach = false;
                        for (int o = 0; o < no && !reach; ++o) {
                            float dx = q0[0] - org[o][0], dz = q0[2] - org[o][1];
                            reach = std::sqrt(dx * dx + dz * dz) - slack <= org[o][2];
                        }
                        if (!reach) {
                            ++*unitsSkipped;
                            continue;
                        }
                    }
                }
                float r = 0.f;
                bool allRead = p0;
                for (unsigned m = 0; m < ne; ++m) {
                    uintptr_t e = ents[m];
                    float q[3];
                    if (m == 0 && p0) {
                        memcpy(q, q0, sizeof q);
                    } else if (e == hero || !Plausible(e) || !EntPos(e, q)) {
                        if (e != hero) allRead = false;
                        continue;
                    }
                    if (p0) {
                        float dx = q[0] - q0[0], dz = q[2] - q0[2], d = std::sqrt(dx * dx + dz * dz);
                        if (d > r) r = d;
                    }
                    if (ng < kMeleeGatherCap) {
                        g_mgather[ng++] = {e, {q[0], q[1], q[2]}};
                    } else {
                        static bool s_full = false;
                        if (!s_full) Log("melee-engine: more than %u soldiers to check - the rest of this strike's scan dropped", kMeleeGatherCap);
                        s_full = true;
                    }
                }
                if (allRead && haveStep) {
                    if (MeleeUnitExt* x = MeleeExt(u, true)) *x = {u, sig, ne, step, r};
                } else if (MeleeUnitExt* x = MeleeExt(u, false)) {
                    x->n = 0;
                }
            }
        }
    }
    return ng;
}
void MeleeEngineStep(uintptr_t world, uintptr_t hero) {
    if (!g_mq.n) return;
    MeleeReq reqs[8];
    AcquireSRWLockExclusive(&g_mq.lock);
    int nr = g_mq.n;
    memcpy(reqs, g_mq.q, sizeof(MeleeReq) * nr);
    g_mq.n = 0;
    ReleaseSRWLockExclusive(&g_mq.lock);
    uintptr_t outer = Ptr(hero + 0x58);
    if (!outer || Ptr(outer + 0x440) != world) {
        if (g_mq.logs++ < 8) Log("melee-engine: hero %p world mismatch ([+0x58]=%p [+0x440]=%p, hook world %p) - skipped", (void*)hero,
                                 (void*)outer, (void*)Ptr(outer + 0x440), (void*)world);
        return;
    }
    uint32_t heroHp = 0, heroL6c = 0;
    Rd(hero + 0xb1c, heroHp);
    Rd(hero + 0x6c, heroL6c);
    uintptr_t heroSide = Ptr(hero + 0x80), heroUnit = Ptr(hero + 0x2e8);
    if (!heroHp) return;
    using FnCanHit = char (*)(uintptr_t e);
    float org[8][3];
    int no = 0;
    for (int ri = 0; ri < nr; ++ri) {
        const MeleeReq& rq = reqs[ri];
        if (rq.hero != hero || Now() - rq.t > 0.6) continue;
        const int cat = rq.cat >= 0 && rq.cat < vmgame::kMeleeCats ? rq.cat : vmgame::kMeleeCatSword;
        const MeleeKindCfg& arc = g_meleeCat[cat].k[rq.kind];
        org[no][0] = rq.ox, org[no][1] = rq.oz;
        org[no][2] = arc.reach + (arc.splashR > 0.f && arc.splashMax > 0 ? arc.splashR : 0.f) + MeleeBigSlack();
        ++no;
    }
    unsigned unitsSkipped = 0, unitsAll = 0;
    const double tGather = Now();
    const unsigned ng = no ? MeleeGather(world, outer, hero, heroUnit, org, no, &unitsSkipped, &unitsAll) : 0;
    const double gatherMs = (Now() - tGather) * 1000.0;
    for (int ri = 0; ri < nr; ++ri) {
        const MeleeReq& rq = reqs[ri];
        if (rq.hero != hero || Now() - rq.t > 0.6) {
            vmgame::MeleeHitFeedback(0);
            continue;
        }
        const int cat = rq.cat >= 0 && rq.cat < vmgame::kMeleeCats ? rq.cat : vmgame::kMeleeCatSword;
        const MeleeCatCfg& cc = g_meleeCat[cat];
        const MeleeKindCfg& arc = cc.k[rq.kind];
        const bool heavy = rq.kind == vmgame::kStrikeHeavySweep || rq.kind == vmgame::kStrikeHeavy;
        const float splashR = arc.splashR, splashDmg = arc.splashDmg;
        const int splashMax = arc.splashMax;
        float cosHalf = std::cos(arc.halfDeg * kPi / 180.f), reach = arc.reach;
        const bool splash = splashR > 0.f && splashMax > 0;
        const float scanR = reach + (splash ? splashR : 0.f);
        struct Cand {
            uintptr_t e;
            float d, x, z;
            bool inArc, done;
        };
        static Cand cand[96];
        int nc = 0;
        unsigned scanned = 0;
        const float bigSlack = MeleeBigSlack();
        int nMount = 0, nBig = 0;
        for (unsigned g = 0; g < ng; ++g) {
            const uintptr_t rider = g_mgather[g].e;
            const float* q = g_mgather[g].q;
            ++scanned;
            float vx = q[0] - rq.ox, vz = q[2] - rq.oz, d = std::sqrt(vx * vx + vz * vz);
            if (d > scanR + bigSlack || std::fabs(q[1] - (rq.oy - 1.7f)) > 3.f) continue;
            uintptr_t e = rider;
            float tx = q[0], tz = q[2];
            const uintptr_t carrier = Ptr(rider + 0x2e0);
            const bool crew = carrier && reinterpret_cast<FnCanHit>(g_base + kFnCanBeHit904)(rider);
            const uintptr_t mount = crew ? 0 : carrier;
            if (mount) {
                float mq[3];
                if (!Plausible(mount) || Ptr(rider + 0x80) == heroSide || !EntPos(mount, mq)) continue;
                bool dup = false;
                for (int c = 0; c < nc && !dup; ++c) dup = cand[c].e == mount;
                if (dup) continue;
                e = mount, tx = mq[0], tz = mq[2];
                vx = tx - rq.ox, vz = tz - rq.oz, d = std::sqrt(vx * vx + vz * vz);
            }
            const float extra = MeleeTargetExtra(e), de = d - extra;
            if (de > scanR) continue;
            bool inArc = de <= reach && (d <= 0.6f || (vx * rq.dx + vz * rq.dz) / d >= (extra > 0.f ? MeleeWideCos(arc.halfDeg, extra, d) : cosHalf));
            if (!inArc && !splash) continue;
            uint32_t hp = 0, l6c = 0;
            uint8_t f108 = 0, f68 = 0;
            if (!Rd(e + 0xb1c, hp) || !hp || !Rd(e + 0x108, f108) || !(f108 & 0x10) || !Rd(e + 0x68, f68) || !(f68 & 1) ||
                !Rd(e + 0x6c, l6c) || Ptr(e + 0x80) == heroSide || !(l6c & heroL6c & 2) ||
                !reinterpret_cast<FnCanHit>(g_base + kFnCanBeHit904)(e)) {
                if ((mount || extra > 0.f) && inArc && Ptr(e + 0x80) != heroSide && g_meleeBigLogs < 24) {
                    ++g_meleeBigLogs;
                    Log("melee-engine: %s %p (radius +%.2f m, %.1f m to its edge) skipped: hp %u f108 0x%x f68 0x%x l6c 0x%x (hero 0x%x)",
                        mount ? "mount" : "big target", (void*)e, extra, de, hp, f108, f68, l6c, heroL6c);
                }
                continue;
            }
            if (mount) ++nMount;
            if (extra > 0.f) ++nBig;
            if (crew && inArc && g_meleeCrewLogs < 8) {
                ++g_meleeCrewLogs;
                uint8_t b10 = 0;
                Rd(rider + 0xb10, b10);
                Log("melee-engine: crew %p in reach (%.1f m from the camera) on carrier %p (vt %p, +0xb10 0x%x) - hit the man, not the carrier", (void*)e,
                    d, (void*)carrier, (void*)Ptr(carrier), b10);
            }
            if ((mount || extra > 0.f) && inArc && g_meleeBigLogs < 24) {
                ++g_meleeBigLogs;
                Log("melee-engine: %s %p in reach (radius +%.2f m, %.1f m from the camera, %.1f m to its edge)", mount ? "mount" : "big target",
                    (void*)e, extra, d, de);
            }
            Cand cd = {e, de, tx, tz, inArc, false};
            if (nc < 96) {
                cand[nc++] = cd;
            } else {
                int worst = 0;
                for (int c = 1; c < 96; ++c)
                    if (cand[c].d > cand[worst].d) worst = c;
                if (d < cand[worst].d) cand[worst] = cd;
            }
        }
        for (int a = 1; a < nc; ++a)
            for (int b = a; b > 0 && cand[b].d < cand[b - 1].d; --b) {
                Cand tmp = cand[b];
                cand[b] = cand[b - 1];
                cand[b - 1] = tmp;
            }
        int hits = 0, kills = 0, inArc = 0, prim[16], np = 0;
        char dl[160] = "";
        int dn = 0;
        for (int c = 0; c < nc; ++c) {
            if (!cand[c].inArc) continue;
            ++inArc;
            if (np >= arc.maxHit || np >= 16) continue;
            prim[np++] = c;
            cand[c].done = true;
            int dmg = 0, rr = MeleeEngineHit(world, hero, cand[c].e, arc, cc, 1.f, &dmg);
            if (rr) ++hits;
            if (rr == 2) ++kills;
            if (dn < 140) dn += snprintf(dl + dn, sizeof dl - dn, "%s%d%s@%.1f", np > 1 ? " " : "", dmg, rr == 2 ? "k" : rr ? "" : "x", cand[c].d);
        }
        int sHits = 0, sKills = 0;
        char sl[160] = "";
        int sn = 0;
        if (splash && np > 0) {
            struct SCand {
                int c;
                float d;
            } sc[96];
            int ns = 0;
            float r2 = splashR * splashR;
            for (int c = 0; c < nc; ++c) {
                if (cand[c].done) continue;
                float best = 1e9f;
                for (int k = 0; k < np; ++k) {
                    float dx = cand[c].x - cand[prim[k]].x, dz = cand[c].z - cand[prim[k]].z, dd = dx * dx + dz * dz;
                    if (dd < best) best = dd;
                }
                if (best <= r2 && ns < 96) sc[ns++] = {c, std::sqrt(best)};
            }
            for (int a = 1; a < ns; ++a)
                for (int b = a; b > 0 && sc[b].d < sc[b - 1].d; --b) {
                    SCand tmp = sc[b];
                    sc[b] = sc[b - 1];
                    sc[b - 1] = tmp;
                }
            int ms = ns < splashMax ? ns : splashMax;
            for (int s = 0; s < ms; ++s) {
                Cand& cd = cand[sc[s].c];
                cd.done = true;
                int dmg = 0, rr = MeleeEngineHit(world, hero, cd.e, arc, cc, splashDmg, &dmg);
                if (rr) ++sHits;
                if (rr == 2) ++sKills;
                if (sn < 140) sn += snprintf(sl + sn, sizeof sl - sn, "%s%d%s@%.1f", s ? " " : "", dmg, rr == 2 ? "k" : rr ? "" : "x", sc[s].d);
            }
        }
        InterlockedIncrement(&g_mq.strikes);
        InterlockedExchangeAdd(&g_mq.hits, hits + sHits);
        InterlockedExchangeAdd(&g_mq.kills, kills + sKills);
        vmgame::MeleeHitFeedback(hits + sHits);
        if (g_mq.logs++ < 200)
            Log("melee-engine: cat %d kind %d %s (reach %.2f m, arc %.0f, splash %.1f m x%d) (%.0f ms after queue) scanned %u (units %u, %u skipped far, %.2f ms), "
                "in arc %d (candidates: %d mounts, %d big), hit %d, killed %d, damage [%s] | splash hit %d, killed %d [%s] (forced hits so far %ld, fallback damage so far %ld)",
                cat, rq.kind, heavy ? "heavy" : "light", reach, arc.halfDeg * 2.f, splashR, splashMax, (Now() - rq.t) * 1000.0, scanned, unitsAll,
                unitsSkipped, gatherMs, inArc, nMount, nBig, hits, kills, dl, sHits, sKills, sl, g_mq.forced, g_mq.fallback);
    }
}
#include "hero_fire_fx.inc"
#include "hit_probe.inc"
struct HitFxLog {
    unsigned shots = 0, hits = 0, kills = 0, logs = 0;
} g_hfx;
void HitFeedback(const AimHit& hit, const float eye[3], const float fwd[3]) {
    ++g_hfx.shots;
    uint32_t hp = 0;
    if (!hit.ent || !Rd(hit.ent + 0xb1c, hp) || !hp) return;
    uintptr_t side = Ptr(hit.ent + 0x80);
    if (!side || side == Ptr(S.entity + 0x80)) return;
    int32_t dmg = 0, ap = 0;
    if (S.fpProj) {
        Rd(S.fpProj + 0x70, dmg);
        Rd(S.fpProj + 0x74, ap);
    }
    bool kill = dmg >= 0 && ap >= 0 && static_cast<int64_t>(dmg) + ap >= static_cast<int64_t>(hp);
    float p[3] = {eye[0] + fwd[0] * hit.dist, eye[1] + fwd[1] * hit.dist, eye[2] + fwd[2] * hit.dist};
    vmgame::HitFx(p, fwd, kill);
    ++g_hfx.hits;
    if (kill) ++g_hfx.kills;
    if (g_hfx.logs++ < 30)
        Log("hit-fx: shot on enemy %p at %.1f m, hp %u vs damage %d + ap %d -> %s (so far %u hits / %u kills of %u shots)", (void*)hit.ent,
            hit.dist, hp, dmg, ap, kill ? "kill (bigger blood)" : "hit", g_hfx.hits, g_hfx.kills, g_hfx.shots);
}
bool FireShot(const float eye[3], const float fwd[3], const float right[3], const float up[3], const float (*bodies)[3],
              int nBodies, float clearance) {
    uintptr_t sb = Ptr(g_base + kGlobalScript);
    uintptr_t battle = sb ? Ptr(sb + oScriptBattle) : 0;
    unsigned char no1 = 1, no2 = 1;
    uintptr_t queue = battle ? Ptr(battle + oBattleCmdQueue) : 0;
    uintptr_t proj = S.fpProj ? S.fpProj : Ptr(S.unit + oUnitProjectile);
    int32_t projId = -1, entId = 0;
    if (S.fpProj)
        projId = S.fpProjId;
    else if (proj)
        Rd(proj + 8, projId);
    Rd(S.entity + oEntId, entId);
    if (battle) {
        Rd(battle + oBattleNoCmd1, no1);
        Rd(battle + oBattleNoCmd2, no2);
    }
    if (!battle || !queue || no1 || no2 || projId < 0 || (cfg.fireShooter && !entId)) {
        if (S.shotLogs++ < 5)
            Log("fire refused: battle=%p (camera's %p) queue=%p blocked=%u/%u projectile=%p id=%d entityId=%d", (void*)battle,
                (void*)S.battle, (void*)queue, no1, no2, (void*)proj, projId, entId);
        return false;
    }
    if (!S.loggedProj && proj) {
        S.loggedProj = true;
        float f[48] = {};
        for (int i = 0; i < 48; ++i) Rd(proj + 4 * static_cast<uintptr_t>(i), f[i]);
        char line[1024];
        int n = 0;
        for (int i = 0; i < 48 && n < 1000; ++i) n += snprintf(line + n, sizeof line - n, "%s%g", i ? " " : "", f[i]);
        Log("fire: projectile record %p id=%d floats[0..47]: %s", (void*)proj, projId, line);
    }
    FireCmd c;
    memset(&c, 0, sizeof c);
    c.vt = g_base + kVtFireCmd;
    c.projId = projId;
    c.f0c = 6.2831855f;
    float nx = 0, ny = 0, aspect = 1, fov = 0;
    bool muzzle = vmgame::Muzzle(&nx, &ny, &aspect);
    float ray[3];
    if (muzzle) {
        if (!Rd(S.cam + oCamFov, fov) || !(fov > 0.2f && fov < 2.8f)) fov = 1.1345f;
        float t = std::tan(fov * 0.5f);
        for (int i = 0; i < 3; ++i) ray[i] = fwd[i] + right[i] * (nx * t * aspect) + up[i] * (ny * t);
    } else {
        for (int i = 0; i < 3; ++i) ray[i] = fwd[i];
    }
    const float wRange = vmgame::WeaponFireRange(vmgame::CurrentWeapon()), range = wRange > 0.f ? wRange : cfg.fireRange;
    for (int i = 0; i < 3; ++i) c.target[i] = eye[i] + fwd[i] * range;
    float d = muzzle ? cfg.muzzleDepth : cfg.fireForward, minGap = 0;
    bool clear = cfg.fireShooter != 0 || nBodies == 0;
    for (; !clear && d <= 6.0f; d += 0.1f) {
        float a[3], b[3], dir[3], len2 = 0;
        for (int i = 0; i < 3; ++i) {
            a[i] = eye[i] + ray[i] * d;
            dir[i] = c.target[i] - a[i];
            len2 += dir[i] * dir[i];
        }
        float inv = len2 > 1e-6f ? 4.f / std::sqrt(len2) : 0.f;
        for (int i = 0; i < 3; ++i) b[i] = a[i] + dir[i] * inv;
        minGap = 1e9f;
        for (int k = 0; k < nBodies; ++k) {
            float g = SegDist2D(bodies[k][0], bodies[k][2], a, b);
            if (g < minGap) minGap = g;
        }
        clear = minGap >= clearance;
        if (clear) break;
    }
    if (!clear) {
        if (S.shotLogs++ < 8) Log("fire: skipped - own hero in the line of fire (gap %.2f m < clearance %.2f)", minGap, clearance);
        return false;
    }
    for (int i = 0; i < 3; ++i) c.launch[i] = eye[i] + ray[i] * d;
    float vis[3] = {c.launch[0], c.launch[1], c.launch[2]};
    float groundY = 0, gh = 0;
    if (SurfaceNear(c.launch[0], c.launch[2], eye[1] - cfg.eyeHeight, &gh) && c.launch[1] < gh + 0.25f && c.launch[1] > gh - 5.f) {
        groundY = gh;
        c.launch[1] = gh + 0.25f;
    }
    c.tgtIsPos = 1;
    c.shooter = cfg.fireShooter ? S.entity : 0;
    double now = Now();
    float period = StepPeriod();
    float toNext = LastStepTime() > 0 ? static_cast<float>(LastStepTime() + period - now) : period * 0.5f;
    if (toNext < 0.f) toNext = 0.f;
    if (toNext > period) toNext = period;
    bool fast = FastFireReady(battle);
    LARGE_INTEGER q0, q1;
    QueryPerformanceCounter(&q0);
    float dispLag = cfg.displayLag - (period - toNext);
    if (dispLag < 0.f) dispLag = 0.f;
    AimHit hit = TraceAim(eye, fwd, range, dispLag);
    QueryPerformanceCounter(&q1);
    float aim[3] = {eye[0] + fwd[0] * hit.dist, eye[1] + fwd[1] * hit.dist, eye[2] + fwd[2] * hit.dist};
    float leadT = 0.f, lead[2] = {0.f, 0.f};
    if (cfg.autoLead && hit.ent && (hit.vel[0] != 0.f || hit.vel[1] != 0.f)) {
        float steps = (fast ? 1.0f : 2.5f) - (EarlyFlightActive() ? 1.0f : 0.0f);
        leadT = (toNext + period * steps + cfg.displayLag + hit.dist / (cfg.bulletSpeed > 100.f ? cfg.bulletSpeed : 2000.f)) * cfg.leadScale +
                cfg.leadExtra;
        if (leadT < 0.f) leadT = 0.f;
        lead[0] = hit.vel[0] * leadT;
        lead[1] = hit.vel[1] * leadT;
        aim[0] += lead[0];
        aim[2] += lead[1];
        float dv[3] = {aim[0] - c.launch[0], aim[1] - c.launch[1], aim[2] - c.launch[2]};
        float dl = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
        if (dl > 0.5f)
            for (int i = 0; i < 3; ++i) c.target[i] = c.launch[i] + dv[i] / dl * range;
    }
    using FnIssue = unsigned char (*)(uintptr_t queue, FireCmd* cmd);
    unsigned char ok = 0;
    bool viaFast = fast && FastFireQueue(&c);
    if (viaFast)
        ok = 1;
    else
        ok = reinterpret_cast<FnIssue>(g_base + kFnIssueCmd)(queue, &c);
    S.lastShotFast = viaFast;
    if (ok) {
        vmgame::Tracer(vis, aim);
        hitprobe::OnShot(hit, now);
        HitFeedback(hit, eye, fwd);
        float tsp = std::sqrt(hit.vel[0] * hit.vel[0] + hit.vel[1] * hit.vel[1]), lsz = std::sqrt(lead[0] * lead[0] + lead[1] * lead[1]);
        ++S.aimShots;
        S.aimSelfSpeedSum += S.camSpeed;
        if (hit.ent) {
            ++S.aimSoldier;
            S.aimSpeedSum += tsp;
            if (tsp > S.aimSpeedMax) S.aimSpeedMax = tsp;
            S.aimLeadSum += lsz;
            if (tsp > 1.5f) ++S.aimMoving;
        } else if (hit.what[0] == 'g') {
            ++S.aimGround;
        } else {
            ++S.aimRange;
        }
        if (hit.ent && tsp > 1.5f && S.movingLogs++ < 40)
            Log("aim: moving target %p at %.1f m, speed %.1f m/s (vel %.1f,%.1f), lead %.2f m (%.0f ms = (next step %.0f ms + step + display lag) x %.1f %+.2f s), "
                "self speed %.1f m/s, %s",
                (void*)hit.ent, hit.dist, tsp, hit.vel[0], hit.vel[1], lsz, leadT * 1000.f, toNext * 1000.f, cfg.leadScale, cfg.leadExtra,
                S.camSpeed,
                viaFast ? "fast fire" : "local queue");
        if (now - S.tAimStat >= 3.0 && S.aimShots) {
            Log("aim: %u shots in %.1f s: on soldier %u (moving > 1.5 m/s: %u; speed avg %.1f max %.1f m/s; lead avg %.2f m), ground %u, "
                "range %u; self speed avg %.1f m/s",
                S.aimShots, now - S.tAimStat, S.aimSoldier, S.aimMoving, S.aimSoldier ? S.aimSpeedSum / S.aimSoldier : 0.f, S.aimSpeedMax,
                S.aimSoldier ? S.aimLeadSum / S.aimSoldier : 0.f, S.aimGround, S.aimRange, S.aimSelfSpeedSum / S.aimShots);
            S.aimShots = S.aimSoldier = S.aimGround = S.aimRange = S.aimMoving = 0;
            S.aimSpeedSum = S.aimSpeedMax = S.aimLeadSum = S.aimSelfSpeedSum = 0;
            S.tAimStat = now;
        }
        if (S.tracerLogs++ < 12)
            Log("tracer: aim hit %s%s%p at %.1f m, target speed %.1f m/s, lead %.2f m (%.0f ms), %s; units %u soldiers %u (snapshot-culled %u) (trace %.2f ms)",
                hit.what, hit.ent ? " " : "", (void*)hit.ent, hit.dist, std::sqrt(hit.vel[0] * hit.vel[0] + hit.vel[1] * hit.vel[1]),
                std::sqrt(lead[0] * lead[0] + lead[1] * lead[1]), leadT * 1000.f, viaFast ? "fast fire" : "local queue", hit.units,
                hit.ents, hit.snapCulled, (q1.QuadPart - q0.QuadPart) * 1000.0 / static_cast<double>(g_qpf.QuadPart));
    }
    if (ok && !c.shooter) {
        float hx = c.target[0] - c.launch[0], hz = c.target[2] - c.launch[2];
        float hl = std::sqrt(hx * hx + hz * hz);
        auto& r = S.shotHist[S.shotHead++ % (sizeof S.shotHist / sizeof S.shotHist[0])];
        r.t = Now();
        r.ax = c.launch[0];
        r.az = c.launch[2];
        r.dx = hl > 1e-4f ? hx / hl : 0.f;
        r.dz = hl > 1e-4f ? hz / hl : 0.f;
    }
    if (S.shotLogs++ < 8)
        Log("fire: issued=%u projectile id=%d shooter=%p muzzle=%d ndc=%.3f,%.3f aspect=%.3f fov=%.4f depth=%.1f gap=%.2f "
            "ground=%.2f eye=%.2f,%.2f,%.2f launch=%.2f,%.2f,%.2f target=%.2f,%.2f,%.2f hero=%.2f,%.2f,%.2f",
            ok, projId, (void*)c.shooter, muzzle, nx, ny, aspect, fov, d, minGap, groundY, eye[0], eye[1], eye[2],
            c.launch[0], c.launch[1], c.launch[2], c.target[0], c.target[1], c.target[2], nBodies ? bodies[0][0] : 0.f,
            nBodies ? bodies[0][1] : 0.f, nBodies ? bodies[0][2] : 0.f);
    return ok != 0;
}
constexpr double kShotGuardWindow = 0.35;
constexpr float kShotFirstLeg = 24.f;
float ShotGap(float x, float z, double now) {
    float best = 1e9f;
    for (const auto& r : S.shotHist) {
        if (r.t <= 0 || now - r.t > kShotGuardWindow) continue;
        const float a[3] = {r.ax, 0.f, r.az}, b[3] = {r.ax + r.dx * kShotFirstLeg, 0.f, r.az + r.dz * kShotFirstLeg};
        float g = SegDist2D(x, z, a, b);
        if (g < best) best = g;
    }
    return best;
}
uintptr_t CmdQueue(uintptr_t* battleOut) {
    uintptr_t sb = Ptr(g_base + kGlobalScript);
    uintptr_t battle = sb ? Ptr(sb + oScriptBattle) : 0;
    unsigned char no1 = 1, no2 = 1;
    if (battle) {
        Rd(battle + oBattleNoCmd1, no1);
        Rd(battle + oBattleNoCmd2, no2);
    }
    if (battleOut) *battleOut = battle;
    return (battle && !no1 && !no2) ? Ptr(battle + oBattleCmdQueue) : 0;
}
unsigned MakeSelection(SelBuf& sel) {
    using FnCtor = void* (*)(void*);
    using FnAdd = void (*)(void*, uintptr_t, char);
    reinterpret_cast<FnCtor>(g_base + kFnSelCtor)(&sel);
    reinterpret_cast<FnAdd>(g_base + kFnSelAdd)(&sel, S.unit, 0);
    unsigned n = 0;
    memcpy(&n, sel.b + oSelUnitCount, 4);
    return n;
}
void FreeSelection(void* p) {
    using FnDtor = void (*)(void*);
    reinterpret_cast<FnDtor>(g_base + kFnSelDtor)(p);
}
bool IssueMove(float x, float z, bool run) {
    uintptr_t battle = 0;
    uintptr_t queue = CmdQueue(&battle);
    if (!queue) return false;
    SelBuf sel;
    unsigned n = MakeSelection(sel);
    bool ok = false;
    if (n) {
        using FnSize = unsigned long long (*)(void*);
        using FnDest = void* (*)(uintptr_t, void*, const float*, uintptr_t, int);
        using FnCtor = void* (*)(void*, void*, void*, unsigned char, unsigned char);
        using FnIssue = unsigned char (*)(uintptr_t, void*);
        uintptr_t pm = Ptr(Ptr(battle + oBattlePath) + oPathMgr);
        int size = static_cast<int>(reinterpret_cast<FnSize>(g_base + kFnSizeParam)(&sel));
        const float xz[2] = {x, z};
        alignas(16) float dest[8] = {};
        if (pm) {
            reinterpret_cast<FnDest>(g_base + kFnMakeDest)(pm, dest, xz, 0, size);
        } else if (size == 2) {
            dest[0] = x;
            dest[2] = z;
        } else {
            if (S.moveLogs++ < 6) Log("move: no path manager (battle=%p) and size=%d; skipped", (void*)battle, size);
            FreeSelection(&sel);
            return false;
        }
        MoveCmdBuf cmd;
        memset(&cmd, 0, sizeof cmd);
        reinterpret_cast<FnCtor>(g_base + kFnMoveCtor)(&cmd, &sel, dest, run ? 1 : 0, 0);
        ok = reinterpret_cast<FnIssue>(g_base + kFnIssueMove)(queue, &cmd) != 0;
        FreeSelection(cmd.b + 8);
        if (S.moveLogs++ < 6)
            Log("move: issued=%d units=%u size=%d pathMgr=%p target=%.2f,%.2f dest=%.2f,%.2f,%.2f run=%d", ok, n, size,
                (void*)pm, x, z, dest[0], dest[1], dest[2], run);
    } else if (S.moveLogs++ < 6) {
        Log("move: the hero's unit was not accepted into a selection (not controllable?)");
    }
    FreeSelection(&sel);
    return ok;
}
bool IssueHalt() {
    uintptr_t queue = CmdQueue(nullptr);
    if (!queue) return false;
    SelBuf sel;
    unsigned n = MakeSelection(sel);
    bool ok = false;
    if (n) {
        using FnCopy = void* (*)(void*, void*);
        using FnIssue = unsigned char (*)(uintptr_t, void*);
        HaltCmdBuf cmd;
        memset(&cmd, 0, sizeof cmd);
        uintptr_t vt = g_base + kVtHaltCmd;
        memcpy(cmd.b, &vt, sizeof vt);
        reinterpret_cast<FnCopy>(g_base + kFnSelCopy)(cmd.b + 8, &sel);
        ok = reinterpret_cast<FnIssue>(g_base + kFnIssueHalt)(queue, &cmd) != 0;
        FreeSelection(cmd.b + 8);
    }
    FreeSelection(&sel);
    if (S.moveLogs++ < 12) Log("move: halt issued=%d units=%u", ok, n);
    return ok;
}
bool IssueDesiredFlying(bool fly) {
    uintptr_t queue = CmdQueue(nullptr);
    if (!g_flyOk || !queue) return false;
    SelBuf sel;
    unsigned n = MakeSelection(sel);
    bool ok = false;
    if (n) {
        using FnCtor = void* (*)(void*, void*, unsigned char);
        using FnIssue = unsigned char (*)(uintptr_t, void*);
        HaltCmdBuf cmd;
        memset(&cmd, 0, sizeof cmd);
        reinterpret_cast<FnCtor>(g_base + kFnFlyCmdCtor904)(&cmd, &sel, fly ? 1 : 0);
        ok = reinterpret_cast<FnIssue>(g_base + kFnFlyCmdIssue904)(queue, &cmd) != 0;
        FreeSelection(cmd.b + 8);
    }
    FreeSelection(&sel);
    Log("fly: desired flying state %d issued=%d units=%u", fly ? 1 : 0, ok, n);
    return ok;
}
using FnBsWrite = void (*)(void* w, char tag, const void* p, unsigned n);
using FnDestWrite = void* (*)(void* w, const void* dest);
using FnCmdDesc = uintptr_t (*)(const uint32_t* type);
using FnCmdBegin = void (*)(void* w, uintptr_t buf, const uint32_t* type, uint32_t flag);
using FnCmdEnd = void (*)(void* w);
void BsWrite(void* w, char tag, const void* p, unsigned n) { reinterpret_cast<FnBsWrite>(g_base + kFnBsWrite)(w, tag, p, n); }
template <class Body>
bool IssueOverride(uintptr_t typeGlobal, Body body) {
    uintptr_t queue = CmdQueue(nullptr);
    uint32_t type = 0, flag = 0;
    if (!queue || !Rd(g_base + typeGlobal, type) || !type) return false;
    uintptr_t desc = reinterpret_cast<FnCmdDesc>(g_base + kFnCmdDesc)(&type);
    if (!desc || !Rd(desc + 0x10, flag)) return false;
    alignas(16) unsigned char w[32] = {};
    uint32_t type2 = type;
    reinterpret_cast<FnCmdBegin>(g_base + kFnCmdBegin)(w, queue + 8, &type2, flag);
    body(w);
    reinterpret_cast<FnCmdEnd>(g_base + kFnCmdEnd)(w);
    return w[0] == 0 && w[1] == 0;
}
bool IssueOvrMove(float x, float z) {
    uintptr_t battle = 0;
    if (!CmdQueue(&battle)) return false;
    int32_t entId = 0;
    uintptr_t pm = Ptr(Ptr(battle + oBattlePath) + oPathMgr);
    if (!Rd(S.entity + oEntId, entId) || !entId || !pm) {
        if (S.moveLogs++ < 6) Log("move(entity): refused entityId=%d pathMgr=%p", entId, (void*)pm);
        return false;
    }
    using FnDest = void* (*)(uintptr_t, void*, const float*, uintptr_t, int);
    const float xz[2] = {x, z};
    alignas(16) float dest[8] = {};
    reinterpret_cast<FnDest>(g_base + kFnMakeDest)(pm, dest, xz, 0, 1);
    const int32_t i0 = 0;
    const float f0 = 0;
    bool ok = IssueOverride(kTypeOvrMove, [&](void* w) {
        BsWrite(w, 0x23, &entId, 4);
        reinterpret_cast<FnDestWrite>(g_base + kFnDestWrite)(w, dest);
        BsWrite(w, 0x04, &i0, 4);
        BsWrite(w, 0x0a, &f0, 4);
    });
    if (S.moveLogs++ < 6) {
        uintptr_t n1 = 0, n2 = 0;
        memcpy(&n1, dest + 4, 8);
        memcpy(&n2, dest + 6, 8);
        Log("move(entity): issued=%d entityId=%d target=%.2f,%.2f dest=%.2f,%.2f,%.2f nav=%p/%p", ok, entId, x, z, dest[0],
            dest[1], dest[2], (void*)n1, (void*)n2);
    }
    return ok;
}
bool IssueOvrHalt() {
    int32_t entId = 0;
    if (!Rd(S.entity + oEntId, entId) || !entId) return false;
    bool ok = IssueOverride(kTypeOvrHalt, [&](void* w) { BsWrite(w, 0x23, &entId, 4); });
    if (S.moveLogs++ < 12) Log("move(entity): halt issued=%d entityId=%d", ok, entId);
    return ok;
}
bool MakeDestPm(uintptr_t pm, float x, float z, float* dest, uintptr_t hint) {
    using FnDest = void* (*)(uintptr_t, void*, const float*, uintptr_t, int);
    const float xz[2] = {x, z};
    memset(dest, 0, 32);
    reinterpret_cast<FnDest>(g_base + kFnMakeDest)(pm, dest, xz, hint, 1);
    return std::isfinite(dest[0]) && std::isfinite(dest[1]) && std::isfinite(dest[2]);
}
bool MakeDestAt(float x, float z, float* dest , uintptr_t hint) {
    uintptr_t battle = 0;
    if (!CmdQueue(&battle)) return false;
    uintptr_t pm = Ptr(Ptr(battle + oBattlePath) + oPathMgr);
    if (!pm) return false;
    return MakeDestPm(pm, x, z, dest, g_wallOk ? hint : 0);
}
struct NavCtx {
    bool ok = false, grid = false;
    uintptr_t pm = 0, cells = 0, ents = 0;
    float scale = 0;
    int32_t off = 0;
    uint32_t maxIdx = 0, shift = 0;
};
void NavCtxGet(NavCtx* c) {
    *c = NavCtx();
    uintptr_t battle = 0;
    if (!CmdQueue(&battle)) return;
    c->ok = true;
    uintptr_t world = Ptr(battle + oBattlePath);
    c->pm = Ptr(world + oPathMgr);
    if (!g_wallOk || !cfg.wall) return;
    uintptr_t grid = world ? Ptr(world + oWorldNavGrid) : 0, pool = world ? Ptr(world + oWorldNavEntries) : 0;
    uintptr_t cells = grid ? Ptr(grid) : 0, ents = pool ? Ptr(pool + 8) : 0;
    float scale = 0;
    int32_t off = 0;
    uint32_t maxIdx = 0, shift = 0;
    if (!cells || !ents || !Rd(grid + 0x2c, scale) || !Rd(grid + 0x18, off) || !Rd(grid + 0x1c, maxIdx) || !Rd(grid + 0x10, shift) ||
        !(scale > 0.f && scale < 100.f) || maxIdx > (1u << 20))
        return;
    shift &= 31;
    static uintptr_t s_loggedGrid = 0;
    if (s_loggedGrid != grid) {
        s_loggedGrid = grid;
        Log("wall: nav block grid %p (%.3f cells/m, offset %d, max %u, shift %u), entries %p", (void*)grid, scale, off, maxIdx, shift,
            (void*)ents);
    }
    c->grid = true;
    c->cells = cells, c->ents = ents, c->scale = scale, c->off = off, c->maxIdx = maxIdx, c->shift = shift;
}
int SurfacesAtCtx(const NavCtx& c, float x, float z, Surf* out, float* terrXZ);
int SurfacesAt(float x, float z, Surf* out) {
    NavCtx c;
    NavCtxGet(&c);
    return SurfacesAtCtx(c, x, z, out, nullptr);
}
int SurfacesAtCtx(const NavCtx& c, float x, float z, Surf* out, float* terrXZ) {
    alignas(16) float d[8];
    int n = 0;
    if (!c.ok) return 0;
    if (c.pm && MakeDestPm(c.pm, x, z, d, 0)) {
        out[n++] = {d[1], 0};
        if (terrXZ) terrXZ[0] = d[0], terrXZ[1] = d[2];
    }
    if (!c.grid) return n;
    const uintptr_t cells = c.cells, ents = c.ents;
    const float scale = c.scale;
    const int32_t off = c.off;
    const uint32_t maxIdx = c.maxIdx, shift = c.shift;
    uint32_t head = 0;
    uint32_t xi = static_cast<uint32_t>(static_cast<int32_t>(std::floor(scale * x)) + off);
    uint32_t zi = static_cast<uint32_t>(static_cast<int32_t>(std::floor(scale * z)) + off);
    if (xi > maxIdx) xi = maxIdx;
    if (zi > maxIdx) zi = maxIdx;
    if (!Rd(cells + (static_cast<uintptr_t>(xi) + (static_cast<uintptr_t>(zi) << shift)) * 4, head)) return n;
    using FnValid = char (*)(uintptr_t);
    using FnContains = char (*)(uintptr_t, const float*, float);
    const float xz[2] = {x, z};
    for (int guard = 0; head != 0xffffff && guard < 256; ++guard) {
        uintptr_t e = ents + static_cast<uintptr_t>(head) * 0x18;
        uint64_t link = 0;
        if (!Rd(e + 0x10, link)) break;
        uintptr_t blk = Ptr(e + 8);
        if ((link & 0xffff) == 0x100 && blk && n < kMaxSurf && reinterpret_cast<FnValid>(g_base + kFnNavEntryValid904)(e) == 1) {
            uintptr_t poly = Ptr(blk + oBlkPoly);
            float pl[4] = {};
            bool dup = false;
            for (int i = 0; i < n; ++i) dup = dup || out[i].blk == blk;
            if (!dup && poly && Rd(blk + oBlkPlane, pl) && std::fabs(pl[1]) > 1e-4f &&
                reinterpret_cast<FnContains>(g_base + kFnBlockContains904)(poly, xz, 0.f) == 1) {
                float h = -(pl[0] * x + pl[2] * z + pl[3]) / pl[1];
                if (std::isfinite(h)) out[n++] = {h, blk};
            }
        }
        head = static_cast<uint32_t>(link >> 40);
    }
    return n;
}
int PickSurface(const Surf* s, int n, uintptr_t cur, float foot, float up) {
    int same = -1, best = -1;
    for (int i = 0; i < n; ++i) {
        if (s[i].blk == cur) same = i;
        if (s[i].h <= foot + up && (best < 0 || s[i].h > s[best].h)) best = i;
    }
    if (same >= 0 && s[same].h > foot + up) return same;
    return best >= 0 ? best : same;
}
int WalkPick(const Surf* s, int n, uintptr_t cur, float foot, float up, float down, bool* edge) {
    *edge = false;
    int i = PickSurface(s, n, cur, foot, up);
    if (i >= 0 && foot - s[i].h > down) {
        for (int j = 0; j < n; ++j)
            if (s[j].h > foot + up) return -1;
        *edge = true;
    }
    return i;
}
bool BlockNear(uintptr_t blk, float x, float z, float margin) {
    uintptr_t poly = blk ? Ptr(blk + oBlkPoly) : 0;
    if (!poly || !g_wallOk) return false;
    using FnContains = char (*)(uintptr_t, const float*, float);
    const float xz[2] = {x, z};
    return reinterpret_cast<FnContains>(g_base + kFnBlockContains904)(poly, xz, margin) == 1;
}
bool StructureOver(const Surf* s, int n, float foot, float up, float down) {
    bool high = false, stand = false;
    for (int i = 0; i < n; ++i) {
        if (!s[i].blk) continue;
        if (s[i].h > foot + up)
            high = true;
        else if (s[i].h >= foot - down)
            stand = true;
    }
    return high && !stand;
}
bool InsideStructure(float x, float z, float foot, float up = -1.f) {
    Surf s[kMaxSurf];
    int n = SurfacesAt(x, z, s);
    return StructureOver(s, n, foot, up > 0.f ? up : cfg.stepUp, cfg.stepDown);
}
constexpr uintptr_t oWorldRegionNet = 0x168, oNetCells = 0x0, oNetShift = 0x10, oNetDim = 0x14, oNetOff = 0x18, oNetMax = 0x1c, oNetScale = 0x2c,
                    oNetRegions = 0x50, oRegCount = 0x44, oRegArray = 0x48, oRegPoly = 0x10, oRegSub = 0x18, oRegIdx = 0x3c, oRegBlk = 0x48,
                    oRegOff = 0xb4, oEdgeNext = 0x10, oEdgeXZ = 0x18, oEdgeFlags = 0x50, kVtRegionSub904 = 0x3ad5ec0, kFnRegionOn904 = 0x3110ff0;
constexpr uint8_t kEdgePortal = 0x2;
bool NetContains(uintptr_t poly, float x, float z);
struct RegionNet {
    uintptr_t cells = 0, regs = 0;
    uint32_t count = 0, dim = 0, shift = 0, maxIdx = 0;
    int32_t off = 0;
    float scale = 0;
};
bool RegionNetRead(uintptr_t world, RegionNet* n) {
    *n = RegionNet();
    uintptr_t g = world ? Ptr(world + oWorldRegionNet) : 0, c = g ? Ptr(g + oNetRegions) : 0;
    if (!g || !c || !Rd(g + oNetCells, n->cells) || !Rd(g + oNetShift, n->shift) || !Rd(g + oNetDim, n->dim) || !Rd(g + oNetOff, n->off) ||
        !Rd(g + oNetMax, n->maxIdx) || !Rd(g + oNetScale, n->scale) || !Rd(c + oRegCount, n->count) || !Rd(c + oRegArray, n->regs))
        return false;
    n->shift &= 31;
    return n->cells && n->regs && n->count > 0 && n->count < 0xfffe && n->scale > 0.f && n->scale < 100.f && n->dim > 0 && n->dim <= 0x10000 &&
           n->maxIdx < 0x10000;
}
uint32_t NetCell(const RegionNet& n, float v) {
    uint32_t i = static_cast<uint32_t>(static_cast<int32_t>(std::floor(n.scale * v)) + n.off);
    return i > n.maxIdx ? n.maxIdx : i;
}
uintptr_t GroundRegionAt(const RegionNet& n, float x, float z, uintptr_t* any) {
    *any = 0;
    const uint32_t xi = NetCell(n, x), zi = NetCell(n, z);
    uintptr_t cand[32];
    int nc = 0;
    auto add = [&](uintptr_t r) {
        for (int i = 0; i < nc; ++i)
            if (cand[i] == r) return;
        if (r && nc < 32) cand[nc++] = r;
    };
    for (uint32_t cz = zi; cz < zi + 2; ++cz)
        for (uint32_t cx = xi; cx < xi + 2; ++cx) {
            if (cx >= n.dim || cz >= n.dim) continue;
            const uintptr_t cell = n.cells + ((static_cast<uintptr_t>(cz) << n.shift) + cx) * 0x10;
            uint16_t v = 0xffff;
            if (!Rd(cell + 8, v)) continue;
            if (v < 0xfffe) {
                if (v < n.count) add(Ptr(n.regs + static_cast<uintptr_t>(v) * 8));
            } else if (v == 0xfffe) {
                uintptr_t node = Ptr(cell);
                for (int guard = 0; node && guard < 64; ++guard, node = Ptr(node + 8)) add(Ptr(node));
            }
        }
    for (int i = 0; i < nc; ++i) {
        const uintptr_t r = cand[i], poly = Ptr(r + oRegPoly);
        uint32_t offCount = 1;
        if (Ptr(r + oRegSub) != g_base + kVtRegionSub904 || Ptr(r + oRegBlk) || !poly || !NetContains(poly, x, z)) continue;
        if (!*any) *any = r;
        if (Rd(r + oRegOff, offCount) && offCount == 0) return r;
    }
    return 0;
}
bool RegionWallCrossed(uintptr_t r, float ax, float az, float bx, float bz) {
    const uintptr_t poly = Ptr(r + oRegPoly);
    auto side = [](float px, float pz, float qx, float qz, float x, float z) { return (qx - px) * (z - pz) - (qz - pz) * (x - px); };
    uintptr_t p = poly;
    for (int guard = 0; p && guard < 256; ++guard) {
        const uintptr_t q = Ptr(p + oEdgeNext);
        float e0[2], e1[2];
        uint8_t fl = 0;
        if (!q || !Rd(p + oEdgeXZ, e0) || !Rd(q + oEdgeXZ, e1) || !Rd(p + oEdgeFlags, fl)) return false;
        if (!(fl & kEdgePortal)) {
            const float d1 = side(ax, az, bx, bz, e0[0], e0[1]), d2 = side(ax, az, bx, bz, e1[0], e1[1]);
            const float d3 = side(e0[0], e0[1], e1[0], e1[1], ax, az), d4 = side(e0[0], e0[1], e1[0], e1[1], bx, bz);
            if ((d1 > 0.f) != (d2 > 0.f) && (d3 > 0.f) != (d4 > 0.f)) return true;
        }
        p = q;
        if (p == poly) break;
    }
    return false;
}
int NetStep(const RegionNet& n, float ax, float az, float bx, float bz, uintptr_t* hit) {
    uintptr_t anyB = 0, anyA = 0;
    const uintptr_t B = GroundRegionAt(n, bx, bz, &anyB);
    *hit = B ? B : anyB;
    if (!B) return anyB ? 2 : 1;
    const uintptr_t A = GroundRegionAt(n, ax, az, &anyA);
    if (!A) {
        *hit = anyA;
        return 4;
    }
    if (A == B) return 0;
    if (RegionWallCrossed(A, ax, az, bx, bz)) {
        *hit = A;
        return 3;
    }
    if (RegionWallCrossed(B, ax, az, bx, bz)) return 3;
    return 0;
}
bool NetContains(uintptr_t poly, float x, float z) {
    using FnContains = char (*)(uintptr_t, const float*, float);
    const float xz[2] = {x, z};
    return reinterpret_cast<FnContains>(g_base + kFnBlockContains904)(poly, xz, 0.f) == 1;
}
bool RegionNetNow(RegionNet* n) {
    uintptr_t battle = 0;
    return CmdQueue(&battle) && battle && RegionNetRead(Ptr(battle + oBattlePath), n);
}
bool InsideWallBody(float x, float z, float foot) {
    if (!InsideStructure(x, z, foot)) return false;
    RegionNet net;
    uintptr_t hit = 0;
    return !(cfg.wallBodyNet && g_netOk && RegionNetNow(&net)) || NetStep(net, x, z, x, z, &hit) != 0;
}
const char* NetWhyText(bool netOk, int why) {
    if (!netOk) return "net off (old rule)";
    switch (why) {
        case 1: return "no ground region here";
        case 2: return "ground region disabled";
        case 3: return "crosses a closed edge of region";
        case 4: return "start not on the net (old rule)";
    }
    return "?";
}
struct SegQuery {
    int32_t kind;
    float a[3], b[3];
};
struct HitList {
    uint32_t cap, count;
    uint8_t* data;
};
bool SegHitsBuilding(const float a[3], const float b[3], uint32_t* hitType, float hitPos[3]) {
    if (!g_buildOk) return false;
    uintptr_t battle = 0;
    if (!CmdQueue(&battle)) return false;
    uintptr_t world = Ptr(battle + oBattlePath), mgr = world ? Ptr(world + oWorldCollide) : 0;
    if (!mgr) return false;
    static HitList hits = {0, 0, nullptr};
    const SegQuery q = {2, {a[0], a[1], a[2]}, {b[0], b[1], b[2]}};
    using FnSeg = uint32_t (*)(uintptr_t, HitList*, const SegQuery*, uint32_t, uint8_t, uint32_t, uintptr_t);
    hits.count = 0;
    reinterpret_cast<FnSeg>(g_base + kFnSegQuery904)(mgr, &hits, &q, 0, 1, kBuildMask, 0);
    bool solid = false;
    for (uint32_t i = 0; i < hits.count && hits.data && !solid; ++i) {
        const uint8_t* r = hits.data + static_cast<size_t>(i) * 0xe0;
        uintptr_t e = 0, obj = 0;
        uint64_t link = 0;
        memcpy(&e, r, 8);
        if (!e || !Rd(e + 0x10, link) || !Rd(e + 8, obj) || !obj) continue;
        uint32_t type = static_cast<uint32_t>(link & 0xffff);
        uintptr_t mat = type == 0x800 ? Ptr(Ptr(obj + 0x60) + 200) : type == 0x4000 ? Ptr(Ptr(obj + 0x10) + 0x18) : 0;
        int32_t mt = 0;
        if (mat && Rd(mat + 0x7c, mt) && mt == 5) continue;
        *hitType = type;
        memcpy(hitPos, r + 8, 12);
        solid = true;
    }
    hits.count = 0;
    return solid;
}
bool SegNearestHit(const float a[3], const float b[3], uint32_t mask, uint32_t* hitType, float hitPos[3], float* dist) {
    if (!g_buildOk) return false;
    uintptr_t battle = 0;
    if (!CmdQueue(&battle)) return false;
    uintptr_t world = Ptr(battle + oBattlePath), mgr = world ? Ptr(world + oWorldCollide) : 0;
    if (!mgr) return false;
    static HitList hits = {0, 0, nullptr};
    const SegQuery q = {2, {a[0], a[1], a[2]}, {b[0], b[1], b[2]}};
    using FnSeg = uint32_t (*)(uintptr_t, HitList*, const SegQuery*, uint32_t, uint8_t, uint32_t, uintptr_t);
    hits.count = 0;
    reinterpret_cast<FnSeg>(g_base + kFnSegQuery904)(mgr, &hits, &q, 0, 1, mask, 0);
    bool any = false;
    float best = 1e30f;
    for (uint32_t i = 0; i < hits.count && hits.data; ++i) {
        const uint8_t* r = hits.data + static_cast<size_t>(i) * 0xe0;
        uintptr_t e = 0, obj = 0;
        uint64_t link = 0;
        memcpy(&e, r, 8);
        if (!e || !Rd(e + 0x10, link) || !Rd(e + 8, obj) || !obj) continue;
        uint32_t type = static_cast<uint32_t>(link & 0xffff);
        uintptr_t mat = type == 0x800 ? Ptr(Ptr(obj + 0x60) + 200) : type == 0x4000 ? Ptr(Ptr(obj + 0x10) + 0x18) : 0;
        int32_t mt = 0;
        if (mat && Rd(mat + 0x7c, mt) && mt == 5) continue;
        float p[3];
        memcpy(p, r + 8, 12);
        float d = std::sqrt((p[0] - a[0]) * (p[0] - a[0]) + (p[1] - a[1]) * (p[1] - a[1]) + (p[2] - a[2]) * (p[2] - a[2]));
        if (!(d < best)) continue;
        best = d;
        *hitType = type;
        memcpy(hitPos, p, 12);
        any = true;
    }
    hits.count = 0;
    if (any && dist) *dist = best;
    return any;
}
void CollisionCensus(const char* why, float x, float z, float foot) {
    uintptr_t battle = 0;
    if (!CmdQueue(&battle)) return;
    uintptr_t world = Ptr(battle + oBattlePath);
    uintptr_t grid = world ? Ptr(world + 0x140) : 0, pool = world ? Ptr(world + oWorldNavEntries) : 0;
    uintptr_t cells = grid ? Ptr(grid) : 0, ents = pool ? Ptr(pool + 8) : 0;
    float scale = 0;
    int32_t off = 0;
    uint32_t maxIdx = 0, shift = 0;
    Log("census (%s) at %.2f,%.2f foot %.2f: building grid %p cells %p entries %p", why, x, z, foot, (void*)grid, (void*)cells, (void*)ents);
    const float r = 20.f;
    if (cells && ents && Rd(grid + 0x2c, scale) && Rd(grid + 0x18, off) && Rd(grid + 0x1c, maxIdx) && Rd(grid + 0x10, shift) &&
        scale > 0.f && scale < 100.f && maxIdx <= (1u << 20)) {
        shift &= 31;
        auto idx = [&](float v) {
            int32_t i = static_cast<int32_t>(std::floor(scale * v)) + off;
            return i < 0 ? 0u : static_cast<uint32_t>(i) > maxIdx ? maxIdx : static_cast<uint32_t>(i);
        };
        uint32_t x0 = idx(x - r), x1 = idx(x + r), z0 = idx(z - r), z1 = idx(z + r);
        Log("census: grid %.3f cells/m offset %d max %u shift %u, cells x %u..%u z %u..%u", scale, off, maxIdx, shift, x0, x1, z0, z1);
        static uintptr_t seen[256];
        int nSeen = 0, logged = 0;
        for (uint32_t zi = z0; zi <= z1 && zi - z0 < 200; ++zi)
            for (uint32_t xi = x0; xi <= x1 && xi - x0 < 200; ++xi) {
                uint32_t head = 0;
                if (!Rd(cells + (static_cast<uintptr_t>(xi) + (static_cast<uintptr_t>(zi) << shift)) * 4, head)) continue;
                for (int guard = 0; head != 0xffffff && guard < 256; ++guard) {
                    uintptr_t e = ents + static_cast<uintptr_t>(head) * 0x18;
                    uint64_t link = 0;
                    if (!Rd(e + 0x10, link)) break;
                    head = static_cast<uint32_t>(link >> 40);
                    uint32_t type = static_cast<uint32_t>(link & 0xffff);
                    uintptr_t obj = Ptr(e + 8);
                    if (!obj || !(type & 0x4800)) continue;
                    bool dup = false;
                    for (int k = 0; k < nSeen && !dup; ++k) dup = seen[k] == obj;
                    if (dup || nSeen >= 256) continue;
                    seen[nSeen++] = obj;
                    if (logged >= 48) continue;
                    ++logged;
                    if (type == 0x800) {
                        float bb[6] = {};
                        Rd(obj + 0xb8, bb);
                        uintptr_t m = Ptr(Ptr(obj + 0x60) + 200);
                        uint8_t on = 0;
                        int32_t mt = -1;
                        if (m) Rd(m + 0x59, on), Rd(m + 0x7c, mt);
                        Log("census:   0x800 obj %p (vt +%llx) box x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f collide %u body %p material %d", (void*)obj,
                            (unsigned long long)(Ptr(obj) - g_base), bb[0], bb[3], bb[1], bb[4], bb[2], bb[5], on, (void*)Ptr(obj + 0x90), mt);
                    } else {
                        Log("census:   0x%x obj %p (vt +%llx) body %p", type, (void*)obj, (unsigned long long)(Ptr(obj) - g_base),
                            (void*)Ptr(obj + 0x28));
                    }
                }
            }
        Log("census: %d building objects within %.0f m (%d logged)", nSeen, r, logged);
    }
    for (int lv = 0; lv < 2; ++lv) {
        char line[600];
        int len = snprintf(line, sizeof line, "census: rays at foot+%.1f:", lv ? 2.5f : 1.f);
        for (int k = 0; k < 16 && len > 0 && len < static_cast<int>(sizeof line) - 24; ++k) {
            float ang = static_cast<float>(k) * 3.14159265f / 8.f, h = foot + (lv ? 2.5f : 1.f);
            const float a[3] = {x, h, z}, b[3] = {x + std::sin(ang) * 12.f, h, z + std::cos(ang) * 12.f};
            uint32_t t = 0;
            float hp[3] = {}, d = 0;
            if (SegNearestHit(a, b, 0xeb05, &t, hp, &d))
                len += snprintf(line + len, sizeof line - len, " %d:0x%x@%.1f", k, t, d);
            else
                len += snprintf(line + len, sizeof line - len, " %d:-", k);
        }
        Log("%s", line);
    }
}
bool TallWallBetween(const float p0[2], const float p1[2], float foot, uint32_t* hitType, float hitPos[3]) {
    float dx = p1[0] - p0[0], dz = p1[1] - p0[1], d = std::sqrt(dx * dx + dz * dz);
    if (d < 1e-4f) return false;
    float k = (d + cfg.bldSkin) / d, lo = foot + cfg.bldLow, hi = foot + std::fmax(cfg.bldHigh, cfg.eyeHeight + 0.3f);
    const float a1[3] = {p0[0], lo, p0[1]}, b1[3] = {p0[0] + dx * k, lo, p0[1] + dz * k};
    if (!SegHitsBuilding(a1, b1, hitType, hitPos)) return false;
    const float a2[3] = {p0[0], hi, p0[1]}, b2[3] = {p0[0] + dx * k, hi, p0[1] + dz * k};
    uint32_t t2 = 0;
    float h2[3] = {};
    return SegHitsBuilding(a2, b2, &t2, h2);
}
bool InsideBuilding(float x, float z, float foot) {
    uint32_t t = 0;
    float h[3] = {};
    const float up0[3] = {x, foot + cfg.bldLow, z}, up1[3] = {x, foot + 40.f, z};
    if (!SegHitsBuilding(up0, up1, &t, h)) return false;
    static const float kDirs[4][2] = {{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}};
    const float p[2] = {x, z};
    for (const auto& dir : kDirs) {
        const float q[2] = {x + dir[0] * 30.f, z + dir[1] * 30.f};
        if (!TallWallBetween(p, q, foot, &t, h)) return false;
    }
    return true;
}
float PolyDist(const float (*pts)[2], int n, float x, float z) {
    float d = -1e30f;
    for (int i = 0; i < n; ++i) {
        const float* a = pts[i];
        const float* b = pts[(i + 1) % n];
        float ex = b[0] - a[0], ez = b[1] - a[1], len = std::sqrt(ex * ex + ez * ez);
        if (len < 1e-6f) continue;
        d = std::fmax(d, (ex * (z - a[1]) - ez * (x - a[0])) / len);
    }
    return d;
}
float PolyKeepOut(const float (*pts)[2], int n, float pos[2], float need, float nrm[2]) {
    float best = -1e30f;
    for (int i = 0; i < n; ++i) {
        const float* a = pts[i];
        const float* b = pts[(i + 1) % n];
        float ex = b[0] - a[0], ez = b[1] - a[1], len = std::sqrt(ex * ex + ez * ez);
        if (len < 1e-6f) continue;
        float c = (ex * (pos[1] - a[1]) - ez * (pos[0] - a[0])) / len;
        if (c > best) {
            best = c;
            nrm[0] = -ez / len;
            nrm[1] = ex / len;
        }
    }
    if (n < 3 || best >= need) return 0.f;
    pos[0] += nrm[0] * (need - best);
    pos[1] += nrm[1] * (need - best);
    return need - best;
}
int CopyBlockPoly(uintptr_t blk, float (*out)[2], int maxN) {
    uintptr_t poly = blk ? Ptr(blk + oBlkPoly) : 0, p = poly;
    for (int n = 0; p && n < maxN; ++n) {
        if (!Rd(p + 0x18, out[n])) return 0;
        p = Ptr(p + 0x10);
        if (p == poly) return n + 1 >= 3 ? n + 1 : 0;
    }
    return 0;
}
bool DropMinProgress(float pos[2], const float p0[2], const float dir[2], float target, float speed, float t) {
    float along = (pos[0] - p0[0]) * dir[0] + (pos[1] - p0[1]) * dir[1];
    float need = std::fmin(target, speed * t);
    if (along < need) {
        pos[0] += dir[0] * (need - along);
        pos[1] += dir[1] * (need - along);
        along = need;
    }
    return along >= target - 1e-4f;
}
template <class InGap, class Surfs>
float FindDropPush(float x0, float z0, float dx, float dz, float foot, float stepDown, float maxPush, InGap inGap, Surfs surfs, float* landH) {
    for (float s = 0.f; s <= maxPush + 1e-3f; s += 0.25f) {
        float px = x0 + dx * s, pz = z0 + dz * s;
        if (inGap(px, pz)) continue;
        Surf o[kMaxSurf];
        int n = surfs(px, pz, o), i = n ? PickSurface(o, n, kNoSurf, foot - stepDown, 0.05f) : -1;
        if (i < 0) continue;
        bool covered = false;
        for (int j = 0; j < n; ++j) covered = covered || o[j].h > o[i].h + 2.f;
        if (covered) continue;
        *landH = o[i].h;
        return s;
    }
    return -1.f;
}
bool SurfaceNear(float x, float z, float refY, float* h, uintptr_t* blk) {
    Surf s[kMaxSurf];
    int n = SurfacesAt(x, z, s);
    if (!n) return false;
    int i = PickSurface(s, n, kNoSurf, refY, 1.0f);
    if (i < 0)
        for (int j = 0; j < n; ++j)
            if (i < 0 || s[j].h < s[i].h) i = j;
    *h = s[i].h;
    if (blk) *blk = s[i].blk;
    return true;
}
bool IssueTeleport(const float* dest) {
    int32_t entId = 0;
    if (!Rd(S.entity + oEntId, entId) || !entId) return false;
    const unsigned char place = 1;
    bool ok = IssueOverride(kTypeOvrTeleport, [&](void* w) {
        BsWrite(w, 0x23, &entId, 4);
        reinterpret_cast<FnDestWrite>(g_base + kFnDestWrite)(w, dest);
        BsWrite(w, 0x01, &place, 1);
    });
    if (S.tpLogs++ < 4) {
        uintptr_t n1 = 0, n2 = 0;
        memcpy(&n1, dest + 4, 8);
        memcpy(&n2, dest + 6, 8);
        Log("move(teleport): issued=%d entityId=%d dest=%.2f,%.2f,%.2f nav=%p/%p", ok, entId, dest[0], dest[1], dest[2],
            (void*)n1, (void*)n2);
    }
    return ok;
}
int MoveMode() {
    if (S.flyer && g_moveOk) return 1;
    if (cfg.moveMode == 0 && g_tpOk) return 0;
    if (cfg.moveMode == 2 && g_ovrOk) return 2;
    return 1;
}
const char* ModeName(int m) { return m == 0 ? "teleport" : m == 2 ? "entity" : "unit"; }
bool UseOverride() { return MoveMode() == 2; }
bool StartOrSteer(float x, float z, bool run) { return UseOverride() ? IssueOvrMove(x, z) : IssueMove(x, z, run); }
bool StopMoving() { return UseOverride() ? IssueOvrHalt() : IssueHalt(); }
void ReleaseHero(int mode, const char* why) {
    bool h = false, u = false;
    if (mode != 1 && g_haltOk) h = IssueOvrHalt();
    if (g_moveOk) u = IssueHalt();
    Log("move: release hero (%s, mode %s) entity-halt=%d unit-halt=%d", why, ModeName(mode), h, u);
}
bool IssueUnitEnabled(bool enabled) {
    uintptr_t sb = Ptr(g_base + kGlobalScript);
    if (!g_ghostOk || !sb || !S.unit || !CmdQueue(nullptr)) return false;
    EnableCmd c;
    memset(&c, 0, sizeof c);
    c.vt = g_base + kVtUnitEnableCmd;
    c.unit = S.unit;
    c.enabled = enabled ? 1 : 0;
    using Fn = void (*)(uintptr_t battleField, EnableCmd* cmd);
    reinterpret_cast<Fn>(g_base + kFnScriptIssueEnable)(sb + oScriptBattle, &c);
    if (S.ghostLogs++ < 12)
        Log("ghost: unit %p %s", (void*)S.unit, enabled ? "enabled (visible again)" : "disabled (hidden, no melee, untouchable)");
    return true;
}
void ExitSequenceStep(uintptr_t mgr) {
    if (!S.exitStage) return;
    double now = Now();
    uintptr_t ent = EntityByPath(S.unit, S.entPath);
    if (ent != S.entity || Ptr(mgr + oMgrBattle) != S.exitBattle || now - S.tExitBegan > 60.0) {
        Log("ghost: exit sequence abandoned at stage %d (hero %s, battle %s)", S.exitStage, ent == S.entity ? "ok" : "gone",
            Ptr(mgr + oMgrBattle) == S.exitBattle ? "same" : "changed");
        S.exitStage = 0;
        S.tRevealHold = now + 1.0;
        return;
    }
    if (S.exitStage == 1) {
        alignas(16) float dest[8];
        uintptr_t eb = 0;
        float eh = 0.f;
        if (!(S.kinInit && SurfaceNear(S.exitPos[0], S.exitPos[1], S.footY, &eh, &eb))) eb = 0;
        if (!CmdQueue(nullptr) || !MakeDestAt(S.exitPos[0], S.exitPos[1], dest, eb)) return;
        if (!IssueUnitEnabled(true)) return;
        bool tp = g_tpOk && IssueTeleport(dest);
        Log("ghost: hero re-enabled and placed at the camera %.2f,%.2f (teleport %d)", S.exitPos[0], S.exitPos[1], tp);
        if (S.active) {
            S.exitStage = 0;
            S.tRevealHold = now + 1.0;
            return;
        }
        S.exitStage = 2;
        S.tExitRelease = now + 0.3;
    } else if (S.exitStage == 2 && now >= S.tExitRelease) {
        if (!CmdQueue(nullptr)) return;
        ReleaseHero(S.exitMode, "exit-ghost");
        S.exitStage = 0;
        S.tRevealHold = now + 1.0;
    }
}
struct BattleUnit {
    uintptr_t unit;
    int alliance;
};
BattleUnit g_units[1024];
unsigned CollectUnits(uintptr_t battle, int* heroAlliance) {
    *heroAlliance = -1;
    uintptr_t list = Ptr(Ptr(battle + oBattlePath) + oWorldAlliances);
    uint32_t na = 0;
    if (!list || !Rd(list + oAlliancesCount, na) || na > 16) return 0;
    uintptr_t alliances = Ptr(list + oAlliancesArray);
    unsigned n = 0;
    for (uint32_t i = 0; i < na && alliances; ++i) {
        uintptr_t al = Ptr(alliances + 8 * i);
        uint32_t nArmies = 0;
        if (!al || !Rd(al + oAllianceArmyCount, nArmies) || nArmies > 64) continue;
        uintptr_t armies = Ptr(al + oAllianceArmyArray);
        for (uint32_t j = 0; j < nArmies && armies; ++j) {
            uintptr_t army = Ptr(armies + 8 * j);
            uint32_t nu = 0;
            if (!army || !Rd(army + oArmyUnitCount, nu) || nu > 1024) continue;
            uintptr_t units = Ptr(army + oArmyUnitArray);
            for (uint32_t k = 0; k < nu && units && n < sizeof g_units / sizeof g_units[0]; ++k) {
                uintptr_t u = Ptr(units + 8 * k);
                if (!u) continue;
                if (u == S.unit) *heroAlliance = static_cast<int>(i);
                g_units[n++] = {u, static_cast<int>(i)};
            }
        }
    }
    return n;
}
struct VelEnt {
    uintptr_t e;
    float p[3], pp[3];
    uint32_t s, ps;
};
constexpr unsigned kVelSlots = 16384;
VelEnt g_vel[kVelSlots];
uint32_t g_velScanStep = 0xffffffffu;
uintptr_t g_velBattle = 0;
void VelReset() {
    memset(g_vel, 0, sizeof g_vel);
    g_velScanStep = 0xffffffffu;
    g_velBattle = 0;
}
VelEnt* VelSlot(uintptr_t e, bool create, uint32_t step = 0) {
    unsigned h = static_cast<unsigned>((e >> 4) * 2654435761u) & (kVelSlots - 1);
    VelEnt* stale = nullptr;
    for (unsigned k = 0; k < 64; ++k) {
        VelEnt& v = g_vel[(h + k) & (kVelSlots - 1)];
        if (v.e == e) return &v;
        if (!v.e) {
            if (!create) return nullptr;
            VelEnt* t = stale ? stale : &v;
            t->e = e;
            t->s = t->ps = 0xffffffffu;
            return t;
        }
        if (create && !stale && v.s != 0xffffffffu && step - v.s > 3) stale = &v;
    }
    if (!stale) return nullptr;
    stale->e = e;
    stale->s = stale->ps = 0xffffffffu;
    return stale;
}
bool VelOf(uintptr_t e, float* v) {
    VelEnt* x = VelSlot(e, false);
    v[0] = v[1] = 0.f;
    if (!x || x->s == 0xffffffffu || x->ps == 0xffffffffu || x->s <= x->ps || x->s - x->ps > 3) return false;
    if (LastStep() - x->s > 2) return false;
    float dt = 0.1f * static_cast<float>(x->s - x->ps);
    float vx = (x->p[0] - x->pp[0]) / dt, vz = (x->p[2] - x->pp[2]) / dt;
    if (vx * vx + vz * vz > 225.f) return false;
    v[0] = vx;
    v[1] = vz;
    return true;
}
unsigned UnitEntities(uintptr_t u, uintptr_t* ents, unsigned cap) {
    const uintptr_t lists[2][2] = {{0xa4, 0xa8}, {oUnitEntCount, oUnitEntArray}};
    for (const auto& l : lists) {
        uint32_t cnt = 0;
        Rd(u + l[0], cnt);
        uintptr_t arr = Ptr(u + l[1]);
        if (cnt < 1 || !Plausible(arr)) continue;
        if (cnt > cap) cnt = cap;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(arr), ents, cnt * 8, &got) || got != cnt * 8) continue;
        if (Plausible(ents[0]) && Ptr(ents[0] + oEntUnit) == u) return cnt;
    }
    return 0;
}
bool EntPos(uintptr_t e, float* q) {
    float t[3];
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(e + oEntPos), t, sizeof t, &got) || got != sizeof t) return false;
    if (!(std::isfinite(t[0]) && std::isfinite(t[1]) && std::isfinite(t[2]) && std::fabs(t[0]) < 1e6f && std::fabs(t[1]) < 1e6f &&
          std::fabs(t[2]) < 1e6f))
        return false;
    memcpy(q, t, sizeof t);
    return true;
}
void LogUnitLists(uintptr_t battle) {
    int heroAl = -1;
    unsigned n = CollectUnits(battle, &heroAl), shown = 0;
    static uintptr_t ents[1024];
    for (unsigned i = 0; i < n && shown < 4; ++i) {
        uintptr_t u = g_units[i].unit;
        if (u == S.unit) continue;
        ++shown;
        uint32_t ca = 0, cb = 0;
        Rd(u + 0xa4, ca);
        Rd(u + oUnitEntCount, cb);
        uintptr_t aa = Ptr(u + 0xa8), ab = Ptr(u + oUnitEntArray);
        uintptr_t a0 = Plausible(aa) ? Ptr(aa) : 0, b0 = Plausible(ab) ? Ptr(ab) : 0;
        float pa[3] = {0, 0, 0};
        EntPos(a0, pa);
        Log("units: unit %p alliance %d: [+0xa4]=%u [+0xa8]=%p [0]=%p(back=%d pos %.1f,%.1f,%.1f) | [+0x134]=%u [+0x138]=%p [0]=%p(back=%d) -> using %u soldiers",
            (void*)u, g_units[i].alliance, ca, (void*)aa, (void*)a0, Plausible(a0) && Ptr(a0 + oEntUnit) == u, pa[0], pa[1], pa[2], cb,
            (void*)ab, (void*)b0, Plausible(b0) && Ptr(b0 + oEntUnit) == u, UnitEntities(u, ents, 1024));
    }
}
void VelStep(uintptr_t battle, const float eye[3], const float fwd[3]) {
    if (!cfg.autoLead || !battle) return;
    uint32_t step = 0;
    if (!Rd(battle + 0x64720, step) || step == g_velScanStep) return;
    if (battle != g_velBattle) {
        VelReset();
        g_velBattle = battle;
    }
    g_velScanStep = step;
    int heroAl = -1;
    unsigned n = CollectUnits(battle, &heroAl);
    static uintptr_t ents[1024];
    for (unsigned i = 0; i < n; ++i) {
        uintptr_t u = g_units[i].unit;
        if (u == S.unit) continue;
        unsigned ne = UnitEntities(u, ents, 1024);
        if (!ne) continue;
        float q[3];
        if (!EntPos(ents[0], q)) continue;
        float v[3] = {q[0] - eye[0], q[1] - eye[1], q[2] - eye[2]};
        float d2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (d2 > 190.f * 190.f) continue;
        float t = v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2];
        if (d2 > 30.f * 30.f && t < 0.5f * std::sqrt(d2)) continue;
        float c[3] = {v[0] - fwd[0] * t, v[1] - fwd[1] * t, v[2] - fwd[2] * t};
        if (d2 > 30.f * 30.f && c[0] * c[0] + c[1] * c[1] + c[2] * c[2] > 45.f * 45.f && ((step ^ static_cast<uint32_t>(u >> 4)) & 1)) continue;
        for (unsigned k = 0; k < ne; ++k) {
            if (!Plausible(ents[k]) || !EntPos(ents[k], q)) continue;
            VelEnt* x = VelSlot(ents[k], true, step);
            if (!x) continue;
            if (x->s != step) {
                if (x->s != 0xffffffffu) {
                    memcpy(x->pp, x->p, sizeof x->p);
                    x->ps = x->s;
                }
                memcpy(x->p, q, sizeof q);
                x->s = step;
            }
        }
    }
}
AimHit TraceAim(const float eye[3], const float fwd[3], float range, float dispLag) {
    AimHit h;
    h.dist = range;
    uintptr_t battle = 0;
    if (!CmdQueue(&battle)) return h;
    NavCtx nav;
    NavCtxGet(&nav);
    auto below = [&](float t, bool* known) {
        float p[3] = {eye[0] + fwd[0] * t, eye[1] + fwd[1] * t, eye[2] + fwd[2] * t};
        Surf s[kMaxSurf];
        float g[2] = {0.f, 0.f};
        int n = SurfacesAtCtx(nav, p[0], p[2], s, g);
        *known = n > 0 && (s[0].blk || (std::fabs(g[0] - p[0]) < 1.5f && std::fabs(g[1] - p[2]) < 1.5f));
        if (!*known) return false;
        for (int i = 0; i < n; ++i)
            if (p[1] <= s[i].h && (!s[i].blk || s[i].h <= eye[1] + 0.05f)) return true;
        return false;
    };
    float prev = 0.5f;
    for (float t = 2.f; t <= range; t += 3.f) {
        bool known = false;
        if (below(t, &known)) {
            float lo = prev, hi = t;
            for (int k = 0; k < 6; ++k) {
                float mid = (lo + hi) * 0.5f;
                bool kn = false;
                if (below(mid, &kn))
                    hi = mid;
                else
                    lo = mid;
            }
            h.dist = hi;
            h.what = "ground";
            break;
        }
        if (known) prev = t;
    }
    int heroAl = -1;
    unsigned n = CollectUnits(battle, &heroAl);
    h.units = n;
    static uintptr_t ents[1024];
    float hx = fwd[0], hz = fwd[2], hh = hx * hx + hz * hz;
    constexpr float kAimSnapSlack = 2.5f;
    uint32_t curStep = 0;
    const bool snap = hh > 1e-6f && battle == g_velBattle && Rd(battle + 0x64720, curStep) && curStep == g_velScanStep;
    const float snapT = snap ? kAimSnapSlack / std::sqrt(hh) : 0.f;
    auto test = [&](uintptr_t e) {
        float q[3];
        if (!Plausible(e)) return;
        float vel[2] = {0.f, 0.f};
        if (snap) {
            const VelEnt* x = VelSlot(e, false);
            if (x && x->s == curStep) {
                VelOf(e, vel);
                float qx = x->p[0] - vel[0] * dispLag, qz = x->p[2] - vel[1] * dispLag;
                float t = ((qx - eye[0]) * hx + (qz - eye[2]) * hz) / hh;
                float px = eye[0] + fwd[0] * t - qx, pz = eye[2] + fwd[2] * t - qz;
                const float out = 0.55f + kAimSnapSlack;
                if (px * px + pz * pz > out * out || t < 1.f - snapT || t >= h.dist + snapT) {
                    ++h.ents;
                    ++h.snapCulled;
                    return;
                }
            }
        }
        if (!EntPos(e, q)) return;
        ++h.ents;
        vel[0] = vel[1] = 0.f;
        bool hasVel = VelOf(e, vel);
        float qx = q[0] - vel[0] * dispLag, qz = q[2] - vel[1] * dispLag;
        float v[3] = {qx - eye[0], q[1] - eye[1], qz - eye[2]};
        float t = hh > 1e-6f ? (v[0] * hx + v[2] * hz) / hh : v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2];
        if (t < 1.f || t >= h.dist) return;
        float px = eye[0] + fwd[0] * t - qx, pz = eye[2] + fwd[2] * t - qz, py = eye[1] + fwd[1] * t;
        if (px * px + pz * pz > 0.55f * 0.55f || py < q[1] || py > q[1] + 2.1f) return;
        h.dist = t;
        h.what = "soldier";
        h.ent = e;
        h.vel[0] = hasVel ? vel[0] : 0.f;
        h.vel[1] = hasVel ? vel[1] : 0.f;
    };
    for (unsigned i = 0; i < n; ++i) {
        uintptr_t u = g_units[i].unit;
        if (u == S.unit) continue;
        unsigned ne = UnitEntities(u, ents, 1024);
        if (!ne) continue;
        float q[3];
        if (EntPos(ents[0], q)) {
            float v[3] = {q[0] - eye[0], q[1] - eye[1], q[2] - eye[2]};
            float t = v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2];
            float c[3] = {v[0] - fwd[0] * t, v[1] - fwd[1] * t, v[2] - fwd[2] * t};
            if (t < -40.f || t > h.dist + 40.f || c[0] * c[0] + c[1] * c[1] + c[2] * c[2] > 1600.f) continue;
        }
        for (unsigned k = 0; k < ne; ++k) test(ents[k]);
    }
    return h;
}
void IssueUnitVisible(uintptr_t sb, uintptr_t unit, bool visible) {
    VisibleCmd c;
    memset(&c, 0, sizeof c);
    c.vt = g_base + kVtUnitVisibleCmd;
    c.unit = unit;
    c.visible = visible ? 1 : 0;
    using Fn = void (*)(uintptr_t battleField, VisibleCmd* cmd);
    reinterpret_cast<Fn>(g_base + kFnScriptIssueVisible)(sb + oScriptBattle, &c);
}
struct CastMarkerLocal {
    LONG gen = -2;
    bool unit = false;
    float eye[3] = {}, fwd[3] = {}, hero[3] = {}, uc[3] = {}, us = 0, ut = 0;
    cm::Aim aim;
    cm::Frame frame;
    bool shown = false;
    double tStat = 0, ms = 0;
    int frames = 0, rebuilt = 0, queries = 0, logs = 0, lostLogs = 0;
} g_cml;
void CastMarkerClear() {
    AcquireSRWLockExclusive(&g_cms.lock);
    g_cms.req = cm::Req();
    g_cms.unit = 0;
    g_cms.aimOk = false;
    ++g_cms.gen;
    ReleaseSRWLockExclusive(&g_cms.lock);
    if (g_cml.shown) vmgame::CastMarkerSet(nullptr);
    g_cml.shown = false;
    g_cml.gen = -2;
}
bool CmHeight(void* ctx, float x, float z, float* h) {
    const NavCtx* c = static_cast<const NavCtx*>(ctx);
    alignas(16) float d[8];
    if (!c->ok || !c->pm || !MakeDestPm(c->pm, x, z, d, 0)) return false;
    if (std::fabs(d[0] - x) > 1.5f || std::fabs(d[2] - z) > 1.5f) return false;
    *h = d[1];
    return true;
}
bool CastTargetShape(uintptr_t unit, const NavCtx& nav, float* centre, float* spread, float* top) {
    uintptr_t battle = 0;
    if (!unit || !CmdQueue(&battle)) return false;
    int heroAl = -1;
    unsigned n = CollectUnits(battle, &heroAl);
    bool found = false;
    for (unsigned i = 0; i < n && !found; ++i) found = g_units[i].unit == unit;
    if (!found) return false;
    static uintptr_t ents[1024];
    unsigned ne = UnitEntities(unit, ents, 1024);
    float sx = 0, sz = 0, ymax = -1e9f, ysum = 0;
    static float pos[1024][3];
    unsigned k = 0;
    for (unsigned i = 0; i < ne; ++i)
        if (Plausible(ents[i]) && EntPos(ents[i], pos[k])) sx += pos[k][0], sz += pos[k][2], ysum += pos[k][1], ymax = std::fmax(ymax, pos[k][1]), ++k;
    if (!k) return false;
    centre[0] = sx / k, centre[2] = sz / k, centre[1] = ysum / k;
    float s2 = 0;
    for (unsigned i = 0; i < k; ++i) {
        float dx = pos[i][0] - centre[0], dz = pos[i][2] - centre[2];
        s2 = std::fmax(s2, dx * dx + dz * dz);
    }
    *spread = std::fmin(std::sqrt(s2), 60.f);
    *top = ymax;
    float h;
    if (CmHeight(const_cast<NavCtx*>(&nav), centre[0], centre[2], &h)) centre[1] = h;
    return true;
}
void CastMarkerStep(const float eye[3], const float fwd[3], const float hero[3]) {
    if (!cfg.castMarker) return;
    const double t0 = Now();
    cm::Req q;
    uintptr_t unit;
    LONG gen;
    AcquireSRWLockShared(&g_cms.lock);
    q = g_cms.req, unit = g_cms.unit, gen = g_cms.gen;
    ReleaseSRWLockShared(&g_cms.lock);
    if (q.shape == cm::kNone && !unit) {
        if (g_cml.shown) vmgame::CastMarkerSet(nullptr);
        g_cml.shown = false;
        g_cml.gen = gen;
        return;
    }
    NavCtx nav;
    NavCtxGet(&nav);
    if (!nav.ok || !nav.pm) return;
    int queries = 0;
    if (unit) {
        q.unit = CastTargetShape(unit, nav, q.unitCentre, &q.unitSpread, &q.unitTop);
        if (!q.unit && g_cml.lostLogs++ < 6) Log("cast-marker: target unit %p is gone / has no soldiers - no marker over it", (void*)unit);
    }
    auto moved = [](const float* a, const float* b, float eps) {
        return std::fabs(a[0] - b[0]) > eps || std::fabs(a[1] - b[1]) > eps || std::fabs(a[2] - b[2]) > eps;
    };
    const bool ground = q.shape == cm::kRing || q.shape == cm::kLane || q.shape == cm::kLaneFromHero || q.shape == cm::kPoint;
    const bool sameReq = gen == g_cml.gen;
    const bool sameView = sameReq && !moved(eye, g_cml.eye, 0.01f) && !moved(fwd, g_cml.fwd, 2e-5f) && !moved(hero, g_cml.hero, 0.01f);
    const bool sameUnit = q.unit == g_cml.unit && (!q.unit || (!moved(q.unitCentre, g_cml.uc, 0.15f) && std::fabs(q.unitSpread - g_cml.us) < 0.3f &&
                                                               std::fabs(q.unitTop - g_cml.ut) < 0.15f));
    if (ground && !sameView) {
        g_cml.aim = cm::AimGround(eye, fwd, hero, q.range, CmHeight, &nav);
        queries += g_cml.aim.queries;
        AcquireSRWLockExclusive(&g_cms.lock);
        g_cms.aimOk = g_cml.aim.ok;
        memcpy(g_cms.aim, g_cml.aim.p, sizeof g_cms.aim);
        g_cms.aimDist = g_cml.aim.dist, g_cms.aimEdge = g_cml.aim.edge, g_cms.aimRange = q.range;
        ReleaseSRWLockExclusive(&g_cms.lock);
    } else if (!ground) {
        g_cml.aim = cm::Aim();
    }
    if (!sameView || !sameUnit || !g_cml.shown) {
        cm::Style st;
        st.pillarH = cfg.castMarkerPillar;
        int bq = 0;
        cm::Build(g_cml.frame, q, g_cml.aim, hero, st, CmHeight, &nav, &bq);
        queries += bq;
        vmgame::CastMarkerSet(&g_cml.frame);
        g_cml.shown = true;
        ++g_cml.rebuilt;
        if (g_cml.logs < 12 && !sameReq)
            ++g_cml.logs, Log("cast-marker: shape %d r %.1f / %.1f len %.1f range %.0f%s%s -> %d points, %d lines, %d fill tris%s%s; aim %s %.1f m%s, %d height queries",
                              q.shape, q.r0, q.r1, q.len, q.range, q.over ? " overcast" : "", q.unit ? " + target unit" : "", g_cml.frame.nPts,
                              g_cml.frame.nLines, g_cml.frame.nTris, g_cml.frame.pillar ? ", pillar" : "", g_cml.frame.chevron ? ", chevron" : "",
                              g_cml.aim.ok ? "ok" : "none", g_cml.aim.dist, g_cml.aim.edge ? " (range edge)" : "", queries);
    }
    memcpy(g_cml.eye, eye, sizeof g_cml.eye), memcpy(g_cml.fwd, fwd, sizeof g_cml.fwd), memcpy(g_cml.hero, hero, sizeof g_cml.hero);
    memcpy(g_cml.uc, q.unitCentre, sizeof g_cml.uc), g_cml.us = q.unitSpread, g_cml.ut = q.unitTop, g_cml.unit = q.unit;
    g_cml.gen = gen;
    const double now = Now();
    g_cml.ms += (now - t0) * 1000.0, g_cml.queries += queries, ++g_cml.frames;
    if (now - g_cml.tStat >= 5.0) {
        if (g_cml.tStat > 0 && g_cml.frames)
            Log("cast-marker: %d frames, avg %.3f ms, %.1f height queries / frame, rebuilt %d", g_cml.frames, g_cml.ms / g_cml.frames,
                static_cast<double>(g_cml.queries) / g_cml.frames, g_cml.rebuilt);
        g_cml.tStat = now, g_cml.ms = 0, g_cml.frames = g_cml.queries = g_cml.rebuilt = 0;
    }
}
bool Revealed(uintptr_t u) {
    for (unsigned i = 0; i < S.nRevealed; ++i)
        if (S.revealed[i] == u) return true;
    return false;
}
void RevealStep(uintptr_t mgr) {
    double now = Now();
    bool on = cfg.revealEnemies && g_revealOk;
    bool scan = on && S.active && S.ghostWanted;
    if (!scan && !S.nRevealed) return;
    uintptr_t battle = 0;
    uintptr_t queue = CmdQueue(&battle);
    if (S.nRevealed && (battle != S.revealBattle || mgr != S.revealMgr)) {
        Log("reveal: battle changed, forgetting %u always-visible units", S.nRevealed);
        S.nRevealed = 0;
    }
    bool keep = on && (scan || S.exitStage != 0 || now < S.tRevealHold);
    if ((keep && !scan) || !queue || now < S.tRevealScan) return;
    S.tRevealScan = now + (scan ? 0.5 : 0.1);
    uintptr_t sb = Ptr(g_base + kGlobalScript);
    int heroAl = -1;
    unsigned n = CollectUnits(battle, &heroAl);
    if (scan) {
        if (heroAl >= 0)
            S.heroAlliance = heroAl;
        else
            heroAl = S.heroAlliance;
        if (heroAl < 0) {
            if (S.revealLogs++ < 6) Log("reveal: hero %p not found among %u battle units; enemies left as they are", (void*)S.unit, n);
            return;
        }
        if (!S.nRevealed) {
            S.revealBattle = battle;
            S.revealMgr = mgr;
        }
        unsigned added = 0, enemies = 0;
        for (unsigned i = 0; i < n && S.nRevealed < kMaxRevealed; ++i) {
            uintptr_t u = g_units[i].unit;
            int32_t st = -1;
            if (g_units[i].alliance == heroAl) continue;
            ++enemies;
            if (!Rd(u + oUnitVisState, st) || st != kVisNormal || Revealed(u)) continue;
            IssueUnitVisible(sb, u, true);
            S.revealed[S.nRevealed++] = u;
            ++added;
        }
        if (added && S.revealLogs++ < 24)
            Log("reveal: %u enemy units set always-visible (total %u; %u battle units, %u enemy, hero alliance %d)", added,
                S.nRevealed, n, enemies, heroAl);
        return;
    }
    unsigned restored = 0, changed = 0;
    for (unsigned i = 0; i < n; ++i) {
        uintptr_t u = g_units[i].unit;
        int32_t st = -1;
        if (!Revealed(u)) continue;
        if (Rd(u + oUnitVisState, st) && st == kVisAlways) {
            IssueUnitVisible(sb, u, false);
            ++restored;
        } else {
            ++changed;
        }
    }
    Log("reveal: %u enemy units back to normal visibility (%u changed by scripts meanwhile, left alone; %u gone)", restored, changed,
        S.nRevealed - restored - changed);
    S.nRevealed = 0;
}
bool IssueStatAttr(uintptr_t queue, uintptr_t unit, int32_t attr, bool on) {
    if (!queue || !unit) return false;
    StatAttrCmd c;
    memset(&c, 0, sizeof c);
    c.vt = g_base + kVtStatAttrCmd;
    c.unit = unit;
    c.attr = attr;
    c.on = on ? 1 : 0;
    using Fn = unsigned char (*)(uintptr_t queue, StatAttrCmd* cmd);
    return reinterpret_cast<Fn>(g_base + kFnIssueStatAttr)(queue, &c) != 0;
}
void MeleeStep() {
    bool want = S.active && !S.ghostWanted && cfg.noMelee && g_meleeOk && S.unit;
    if (!want && !S.meleeUnit) return;
    uintptr_t battle = 0;
    uintptr_t queue = CmdQueue(&battle);
    if (S.meleeUnit && battle != S.meleeBattle) {
        Log("melee: battle changed, forgetting melee_disabled on %p", (void*)S.meleeUnit);
        S.meleeUnit = 0;
    }
    if (S.meleeUnit && (!want || S.meleeUnit != S.unit)) {
        if (!queue) return;
        bool ok = IssueStatAttr(queue, S.meleeUnit, kAttrMeleeDisabled, false);
        if (S.meleeLogs++ < 20) Log("melee: hero %p melee_disabled removed (issued %d)", (void*)S.meleeUnit, ok);
        S.meleeUnit = 0;
    }
    if (!want || S.meleeUnit || S.meleeSkipUnit == S.unit || !queue) return;
    uint64_t bits = 0;
    if (Rd(S.unit + oUnitAttrScript, bits) && (bits >> kAttrMeleeDisabled) & 1) {
        S.meleeSkipUnit = S.unit;
        Log("melee: hero %p already has melee_disabled; left as it is", (void*)S.unit);
        return;
    }
    bool ok = IssueStatAttr(queue, S.unit, kAttrMeleeDisabled, true);
    if (ok) {
        S.meleeUnit = S.unit;
        S.meleeBattle = battle;
    }
    if (S.meleeLogs++ < 20)
        Log("melee: hero %p melee_disabled set (no melee, no fighting back; enemies can still hit it) issued %d", (void*)S.unit, ok);
}
constexpr uintptr_t kVtKnockBack = 0x540, kVtKnockDown = 0x548, kVtKnockFlying = 0x550;
struct NoKnock {
    uintptr_t ent = 0, origVt = 0;
    uintptr_t* copy = nullptr;
    uintptr_t copyOf = 0;
    bool copyKnock = false, copyShield = false;
    bool knock = false, shield = false;
    bool copyArmor = false, armor = false;
    volatile LONG armorSkipped = 0;
    LONG armorLogged = 0;
    LONG gen = 0;
    size_t slots = 0;
    volatile LONG blocked[3] = {0, 0, 0};
    LONG logged = 0;
    unsigned logs = 0;
} g_nk;
void ShieldBracketClose(uintptr_t self);
uint64_t KnockBackStub(uintptr_t self, void*) { return ShieldBracketClose(self), InterlockedIncrement(&g_nk.blocked[0]), 0; }
uint64_t KnockDownStub(uintptr_t self, void*) { return ShieldBracketClose(self), InterlockedIncrement(&g_nk.blocked[1]), 0; }
uint64_t KnockFlyingStub(uintptr_t self, void*) { return ShieldBracketClose(self), InterlockedIncrement(&g_nk.blocked[2]), 0; }
struct ShieldGameState {
    uintptr_t ent = 0;
    uintptr_t orig310 = 0, orig3a8 = 0, orig558 = 0;
    SRWLOCK lock = SRWLOCK_INIT;
    uintptr_t hpEnt = 0;
    uint32_t hpPrev = 0;
    uint32_t exempt = 0;
    uint32_t bracketHp = 0;
    bool bracket = false;
    LONG64 lastFxQpc = 0;
    volatile LONG missileFull = 0, missilePart = 0, meleeFull = 0, meleePart = 0, meleeMiss = 0, side = 0, stagger = 0, refunds = 0, refundHp = 0;
    volatile LONG logs = 0;
    LONG statSum = 0;
    unsigned statLogs = 0;
    volatile LONG hookTid = 0, stepTid = 0;
} g_sb;
float ShieldFrac(uintptr_t self) {
    return self && self == g_sb.ent && S.active && cfg.shieldGame ? vmgame::ShieldBlockFraction() : 0.f;
}
bool ShieldLog() { return InterlockedIncrement(&g_sb.logs) <= 120; }
void ShieldBracketCloseLocked(uintptr_t self) {
    if (!g_sb.bracket) return;
    g_sb.bracket = false;
    uint32_t hp = 0;
    if (Rd(self + 0xb1c, hp) && hp < g_sb.bracketHp) g_sb.exempt += g_sb.bracketHp - hp;
}
void ShieldBracketClose(uintptr_t self) {
    if (!self || self != g_sb.ent || !g_sb.bracket) return;
    AcquireSRWLockExclusive(&g_sb.lock);
    ShieldBracketCloseLocked(self);
    ReleaseSRWLockExclusive(&g_sb.lock);
}
using FnHeroProjHit = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uint64_t HeroProjHit(uintptr_t self, uintptr_t proj, uintptr_t list, uintptr_t flag, uintptr_t out) {
    auto orig = reinterpret_cast<FnHeroProjHit>(g_sb.orig310);
    ShieldBracketClose(self);
    float f = ShieldFrac(self);
    if (!(f > 0.f) || !proj || !out) return orig(self, proj, list, flag, out);
    uint8_t alive = 0;
    uintptr_t mount = 1;
    if (!Rd(self + 0x108, alive) || !(alive & 0x10) || !Rd(self + 0x2e0, mount) || mount) return orig(self, proj, list, flag, out);
    using FnCanHit = char (*)(uintptr_t, uintptr_t, uintptr_t);
    if (!reinterpret_cast<FnCanHit>(g_base + kFnProjCanHit904)(self, proj, flag & 0xff)) return orig(self, proj, list, flag, out);
    float v[3] = {}, h[3] = {}, src[3] = {};
    for (int i = 0; i < 3; ++i) Rd(proj + 0x100 + 4 * i, v[i]), Rd(self + oEntPos + 4 * i, h[i]);
    float dx = -v[0], dz = -v[2];
    if (dx * dx + dz * dz < 1.f) {
        uintptr_t shooter = Ptr(proj + 0x20);
        if (shooter ? ReadVec(shooter + oEntPos, src) : ReadVec(proj + 0xf4, src)) dx = src[0] - h[0], dz = src[2] - h[2];
    }
    float ang = 0.f;
    bool front = shieldhit::Frontal(S.yaw, dx, dz, ShieldAngleNow(), &ang);
    float m[4] = {}, rec[2] = {};
    for (int i = 0; i < 4; ++i) Rd(proj + 0x78 + 4 * i, m[i]);
    uintptr_t r = Ptr(proj + 0x90);
    if (r) Rd(r + 0x70, rec[0]), Rd(r + 0x74, rec[1]);
    float raw = rec[0] * m[0] + m[1] + rec[1] * m[2] + m[3];
    float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!front) {
        InterlockedIncrement(&g_sb.side);
        if (ShieldLog()) Log("shield-game: missile from the side (%.0f deg > %.0f) not blocked, raw %.0f, speed %.1f", ang, ShieldAngleNow(), raw, speed);
        return orig(self, proj, list, flag, out);
    }
    vmgame::ShieldHit();
    if (!g_sb.hookTid) InterlockedExchange(&g_sb.hookTid, static_cast<LONG>(GetCurrentThreadId()));
    if (shieldhit::Full(f)) {
        *reinterpret_cast<uint16_t*>(out) = 0;
        InterlockedIncrement(&g_sb.missileFull);
        if (ShieldLog())
            Log("shield-game: missile blocked 100%% (raw %.0f, speed %.1f, from %.0f deg, shooter %p, projectile %p) - eaten by the shield", raw, speed, ang,
                (void*)Ptr(proj + 0x20), (void*)proj);
        return 1;
    }
    float* pm = reinterpret_cast<float*>(proj + 0x78);
    float save[4] = {pm[0], pm[1], pm[2], pm[3]};
    shieldhit::ScaleProjectile(pm, f);
    uint32_t hp0 = 0, hp1 = 0;
    Rd(self + 0xb1c, hp0);
    uint64_t res = orig(self, proj, list, flag, out);
    for (int i = 0; i < 4; ++i) pm[i] = save[i];
    Rd(self + 0xb1c, hp1);
    if (hp1 < hp0) {
        AcquireSRWLockExclusive(&g_sb.lock);
        g_sb.exempt += hp0 - hp1;
        ReleaseSRWLockExclusive(&g_sb.lock);
    }
    InterlockedIncrement(&g_sb.missilePart);
    if (ShieldLog())
        Log("shield-game: missile blocked %.0f%% (raw %.0f -> %.0f, speed %.1f, from %.0f deg) hp %u -> %u", f * 100.f, raw, raw * (1.f - f), speed, ang, hp0,
            hp1);
    return res;
}
extern "C" void hfps_hero_melee(uintptr_t self, uintptr_t pend) {
    ShieldBracketClose(self);
    float f = ShieldFrac(self);
    uintptr_t victim = 0;
    if (!(f > 0.f) || !pend || !Rd(pend + 0x10, victim) || victim != self) return;
    if (!g_sb.hookTid) InterlockedExchange(&g_sb.hookTid, static_cast<LONG>(GetCurrentThreadId()));
    int32_t kind = 0;
    uint32_t dmg = 0;
    Rd(pend, kind);
    Rd(pend + 4, dmg);
    uintptr_t attacker = pend - 0xaf0;
    float a[3] = {}, h[3] = {};
    ReadVec(attacker + oEntPos, a);
    ReadVec(self + oEntPos, h);
    float ang = 0.f;
    if (!shieldhit::Frontal(S.yaw, a[0] - h[0], a[2] - h[2], ShieldAngleNow(), &ang)) {
        InterlockedIncrement(&g_sb.side);
        if (ShieldLog()) Log("shield-game: melee from the side (%.0f deg > %.0f) not blocked, kind %d damage %u", ang, ShieldAngleNow(), kind, dmg);
        return;
    }
    shieldhit::MeleeRewrite rw = shieldhit::RewriteMelee(kind, dmg, f);
    if (!rw.feedback) return;
    uint32_t hp = 0;
    Rd(self + 0xb1c, hp);
    *reinterpret_cast<volatile int32_t*>(pend) = rw.kind;
    *reinterpret_cast<volatile uint32_t*>(pend + 4) = rw.damage;
    if (rw.partial) {
        AcquireSRWLockExclusive(&g_sb.lock);
        g_sb.bracket = true;
        g_sb.bracketHp = hp;
        ReleaseSRWLockExclusive(&g_sb.lock);
    }
    InterlockedIncrement(shieldhit::Full(f) ? &g_sb.meleeFull : kind == shieldhit::kMiss ? &g_sb.meleeMiss : &g_sb.meleePart);
    if (ShieldLog())
        Log("shield-game: melee blocked %.0f%% kind %d -> %d, damage %u -> %u (attacker %p, from %.0f deg, hero hp %u)", f * 100.f, kind, rw.kind, dmg, rw.damage,
            (void*)attacker, ang, hp);
    vmgame::ShieldHit();
}
using FnHeroReact = uint64_t (*)(uintptr_t, uintptr_t, uintptr_t);
uint64_t HeroHitReact(uintptr_t self, uintptr_t attacker, uintptr_t delta) {
    ShieldBracketClose(self);
    return reinterpret_cast<FnHeroReact>(g_sb.orig3a8)(self, attacker, delta);
}
using FnHeroStagger = uint64_t (*)(uintptr_t, uintptr_t);
uint64_t HeroStagger(uintptr_t self, uintptr_t info) {
    if (g_nk.armor && cfg.superArmor && S.active) {
        InterlockedIncrement(&g_nk.armorSkipped);
        return 0;
    }
    float f = ShieldFrac(self);
    if (f > 0.f && info && shieldhit::EatStagger(f, cfg.shieldStagger)) {
        float v[3] = {};
        for (int i = 0; i < 3; ++i) Rd(info + 8 + 4 * i, v[i]);
        float ang = 0.f;
        if (shieldhit::Frontal(S.yaw, -v[0], -v[2], ShieldAngleNow(), &ang)) {
            InterlockedIncrement(&g_sb.stagger);
            uint8_t flag = 0;
            Rd(info + 0x14, flag);
            if (ShieldLog()) Log("shield-game: hit reaction (state 18, flag %u) eaten by the shield (%.0f%%, from %.0f deg)", flag, f * 100.f, ang);
            return 0;
        }
    }
    return reinterpret_cast<FnHeroStagger>(g_sb.orig558)(self, info);
}
void ShieldHpStep() {
    uintptr_t e = g_sb.ent;
    float f = ShieldFrac(e);
    if (!g_sb.stepTid) InterlockedExchange(&g_sb.stepTid, static_cast<LONG>(GetCurrentThreadId()));
    AcquireSRWLockExclusive(&g_sb.lock);
    if (!e || !S.active || !cfg.shieldGame) {
        g_sb.hpEnt = 0;
        g_sb.bracket = false;
        g_sb.exempt = 0;
        ReleaseSRWLockExclusive(&g_sb.lock);
        return;
    }
    ShieldBracketCloseLocked(e);
    uint32_t hp = 0, mx = 0;
    uint8_t alive = 0;
    if (!Rd(e + 0xb1c, hp) || !Rd(e + 0xb18, mx) || !Rd(e + 0x108, alive)) {
        ReleaseSRWLockExclusive(&g_sb.lock);
        return;
    }
    if (g_sb.hpEnt != e) {
        g_sb.hpEnt = e;
        g_sb.hpPrev = hp;
        g_sb.exempt = 0;
        ReleaseSRWLockExclusive(&g_sb.lock);
        return;
    }
    uint32_t prev = g_sb.hpPrev, ex = g_sb.exempt;
    uint32_t add = cfg.shieldRefund && (alive & 0x10) ? shieldhit::Refund(prev, hp, ex, f, mx) : 0;
    if (add) Wr(e + 0xb1c, hp + add);
    g_sb.hpPrev = hp + add;
    g_sb.exempt = 0;
    ReleaseSRWLockExclusive(&g_sb.lock);
    if (hp < prev && f > 0.f && ShieldLog())
        Log("shield-game: hero hp %u -> %u this step (already reduced by hooks %u, refund %u at %.0f%%) -> %u / %u", prev, hp, ex, add, f * 100.f, hp + add, mx);
    if (!add) return;
    InterlockedIncrement(&g_sb.refunds);
    InterlockedExchangeAdd(&g_sb.refundHp, static_cast<LONG>(add));
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    if (q.QuadPart - g_sb.lastFxQpc > g_qpf.QuadPart / 4) {
        g_sb.lastFxQpc = q.QuadPart;
        vmgame::ShieldHit();
    }
}
void NoKnockRemove(const char* why) {
    if (!g_nk.ent) return;
    bool ours = g_nk.copy && Ptr(g_nk.ent) == reinterpret_cast<uintptr_t>(g_nk.copy + 1);
    if (ours) Wr(g_nk.ent, g_nk.origVt);
    g_sb.ent = 0;
    Log("hero: no-knockdown%s%s off for %p (%s)%s - blocked knock back %ld, knock down %ld, knock flying %ld, hit reactions skipped %ld",
        g_nk.shield ? " + shield-game" : "", g_nk.armor ? " + super armor" : "", (void*)g_nk.ent, why, ours ? "" : " (vtable was not ours any more - left alone)",
        g_nk.blocked[0], g_nk.blocked[1], g_nk.blocked[2], g_nk.armorSkipped);
    g_nk.armor = false;
    if (g_nk.shield)
        Log("shield-game: so far missile 100%% %ld / partial %ld, melee 100%% %ld / partial %ld / miss %ld, side %ld, stagger eaten %ld, hp refunds %ld (+%ld hp)",
            g_sb.missileFull, g_sb.missilePart, g_sb.meleeFull, g_sb.meleePart, g_sb.meleeMiss, g_sb.side, g_sb.stagger, g_sb.refunds, g_sb.refundHp);
    g_nk.ent = 0;
}
bool NoKnockInstall(uintptr_t ent) {
    uintptr_t vt = Ptr(ent), lo = 0, hi = 0;
    if (!vt || vt < g_base || vt >= g_base + g_imageSize || !CodeRange(&lo, &hi)) {
        if (g_nk.logs++ < 8) Log("hero: no-knockdown: entity %p vtable %p not in the game image - not hooking", (void*)ent, (void*)vt);
        return false;
    }
    if (g_nk.copy && vt == reinterpret_cast<uintptr_t>(g_nk.copy + 1)) return true;
    bool knock = cfg.noKnock && g_noKnockOk;
    bool shield = cfg.shieldGame && g_shieldGameOk && vt == g_base + kVtSoldier904 && Ptr(vt + 0x310) == g_base + kFnProjHit904 &&
                  Ptr(vt + 0x1c8) == g_base + kFnRet0Of1c8_904 && Ptr(vt + 0x3a8) == g_base + kFnHitReact904 && Ptr(vt + 0x558) == g_base + kFnStagger904;
    if (cfg.shieldGame && g_shieldGameOk && !shield && g_nk.logs++ < 8)
        Log("shield-game: entity %p vtable +0x%llx is not the soldier class +0x%llx (or its slots differ) - shield blocks only the debug key", (void*)ent,
            (unsigned long long)(vt - g_base), (unsigned long long)kVtSoldier904);
    bool armor = cfg.superArmor && g_armorOk && Ptr(vt + 0x558) == g_base + kFnStagger904;
    if (!knock && !shield && !armor) return false;
    if (!g_nk.copy || g_nk.copyOf != vt || g_nk.copyKnock != knock || g_nk.copyShield != shield || g_nk.copyArmor != armor) {
        size_t slots = 0;
        while (slots < 4096) {
            uintptr_t f = Ptr(vt + 8 * slots);
            if (f < lo || f >= hi) break;
            ++slots;
        }
        if (slots <= kVtKnockFlying / 8) {
            if (g_nk.logs++ < 8) Log("hero: no-knockdown: entity vtable %p has only %zu slots - not hooking", (void*)vt, slots);
            return false;
        }
        size_t n = slots + 64;
        auto* c = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (n + 1) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!c) return false;
        c[0] = Ptr(vt - 8);
        for (size_t i = 0; i < n; ++i) c[1 + i] = Ptr(vt + 8 * i);
        if (knock) {
            c[1 + kVtKnockBack / 8] = reinterpret_cast<uintptr_t>(&KnockBackStub);
            c[1 + kVtKnockDown / 8] = reinterpret_cast<uintptr_t>(&KnockDownStub);
            c[1 + kVtKnockFlying / 8] = reinterpret_cast<uintptr_t>(&KnockFlyingStub);
        }
        if (shield) {
            g_sb.orig310 = c[1 + 0x310 / 8];
            g_sb.orig3a8 = c[1 + 0x3a8 / 8];
            g_sb.orig558 = c[1 + 0x558 / 8];
            g_orig_hero_1c8 = reinterpret_cast<void*>(c[1 + 0x1c8 / 8]);
            c[1 + 0x310 / 8] = reinterpret_cast<uintptr_t>(&HeroProjHit);
            c[1 + 0x1c8 / 8] = reinterpret_cast<uintptr_t>(&hfps_hero_1c8_thunk);
            c[1 + 0x3a8 / 8] = reinterpret_cast<uintptr_t>(&HeroHitReact);
            c[1 + 0x558 / 8] = reinterpret_cast<uintptr_t>(&HeroStagger);
        }
        if (armor && !shield) {
            g_sb.orig558 = c[1 + 0x558 / 8];
            c[1 + 0x558 / 8] = reinterpret_cast<uintptr_t>(&HeroStagger);
        }
        g_nk.copy = c;
        g_nk.copyArmor = armor;
        g_nk.copyOf = vt;
        g_nk.copyKnock = knock;
        g_nk.copyShield = shield;
        g_nk.slots = slots;
    }
    g_nk.origVt = vt;
    g_nk.ent = ent;
    g_nk.knock = knock;
    g_nk.shield = shield;
    g_nk.armor = armor;
    g_nk.armorSkipped = g_nk.armorLogged = 0;
    g_nk.gen = g_cfgGen;
    if (shield) g_sb.ent = ent;
    bool ok = Wr(ent, reinterpret_cast<uintptr_t>(g_nk.copy + 1));
    Log("hero: no-knockdown on for entity %p (vtable +0x%llx -> copy %p, %zu slots; knock back / down / flying slots %s; shield-game slots "
        "+0x310 / +0x1c8 / +0x3a8 / +0x558 %s) %s",
        (void*)ent, (unsigned long long)(vt - g_base), (void*)(g_nk.copy + 1), g_nk.slots, knock ? "replaced" : "kept", shield ? "replaced" : "kept",
        ok ? "" : "WRITE FAILED");
    if (!ok) g_nk.ent = 0, g_sb.ent = 0;
    return ok;
}
void NoKnockStep() {
    bool want = S.active && S.entity && ((cfg.noKnock && g_noKnockOk) || (cfg.shieldGame && g_shieldGameOk) || (cfg.superArmor && g_armorOk));
    if (g_nk.ent && (!want || g_nk.ent != S.entity || g_nk.gen != g_cfgGen))
        NoKnockRemove(!want ? "first person off / switched off" : g_nk.ent != S.entity ? "hero changed" : "ini reloaded");
    if (!want) return;
    if (g_nk.ent == S.entity && g_nk.copy && Ptr(S.entity) != reinterpret_cast<uintptr_t>(g_nk.copy + 1)) {
        if (g_nk.logs++ < 8) Log("hero: no-knockdown: entity %p vtable changed to +0x%llx by the game - re-hooking", (void*)S.entity,
                                 (unsigned long long)(Ptr(S.entity) - g_base));
        g_nk.ent = 0;
        g_sb.ent = 0;
    }
    if (g_nk.ent != S.entity) NoKnockInstall(S.entity);
    if (g_nk.armorSkipped != g_nk.armorLogged && g_nk.logs++ < 60) {
        g_nk.armorLogged = g_nk.armorSkipped;
        Log("hero: super armor - hit reaction (state 18) skipped, so far %ld this time", g_nk.armorLogged);
    }
    LONG total = g_nk.blocked[0] + g_nk.blocked[1] + g_nk.blocked[2];
    if (total != g_nk.logged && g_nk.logs++ < 60) {
        g_nk.logged = total;
        Log("hero: knock blocked (so far: back %ld, down %ld, flying %ld)", g_nk.blocked[0], g_nk.blocked[1], g_nk.blocked[2]);
    }
    LONG sum = g_sb.missileFull + g_sb.missilePart + g_sb.meleeFull + g_sb.meleePart + g_sb.meleeMiss + g_sb.stagger + g_sb.refunds;
    if (sum != g_sb.statSum && g_sb.statLogs++ < 60) {
        g_sb.statSum = sum;
        Log("shield-game: so far missile 100%% %ld / partial %ld, melee 100%% %ld / partial %ld / miss %ld, side %ld, stagger eaten %ld, hp refunds %ld "
            "(+%ld hp); hook thread %ld, step thread %ld",
            g_sb.missileFull, g_sb.missilePart, g_sb.meleeFull, g_sb.meleePart, g_sb.meleeMiss, g_sb.side, g_sb.stagger, g_sb.refunds, g_sb.refundHp,
            g_sb.hookTid, g_sb.stepTid);
    }
}
bool UicName(uintptr_t uic, char* out, size_t n) {
    out[0] = 0;
    uintptr_t p = Ptr(uic + oUicId);
    if (!p || n < 2) return false;
    char buf[64] = {};
    size_t want = n - 1 < sizeof buf - 1 ? n - 1 : sizeof buf - 1;
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(p), buf, want, &got) || !got) {
        for (got = 0; got < want && Rd(p + got, buf[got]) && buf[got]; ++got) {}
    }
    buf[got < sizeof buf ? got : sizeof buf - 1] = 0;
    memcpy(out, buf, n - 1);
    out[n - 1] = 0;
    return true;
}
uintptr_t FindUic(uintptr_t node, const char* name, int depth, int* budget, bool visibleOnly = false) {
    if (!node || --*budget < 0) return 0;
    if (visibleOnly) {
        uint8_t vis = 0;
        if (!Rd(node + oUicVisible, vis) || !vis) return 0;
    }
    char nm[64];
    if (UicName(node, nm, sizeof nm) && !strcmp(nm, name)) return node;
    if (depth <= 0) return 0;
    uint32_t cnt = 0;
    uintptr_t arr = Ptr(node + oUicChildren);
    if (!Rd(node + oUicChildCount, cnt) || !arr || cnt > 4096) return 0;
    for (uint32_t i = 0; i < cnt; ++i) {
        uintptr_t r = FindUic(Ptr(arr + 8 * i), name, depth - 1, budget, visibleOnly);
        if (r) return r;
    }
    return 0;
}
constexpr uintptr_t oUicParent = 0x2e8;
bool UicUnder(uintptr_t uic, uintptr_t root) {
    for (int i = 0; i < 64 && uic; ++i) {
        if (uic == root) return true;
        uic = Ptr(uic + oUicParent);
    }
    return false;
}
void DumpUi(uintptr_t node, int depth, int* lines) {
    if (!node || *lines <= 0 || depth > 30) return;
    uint8_t vis = 0;
    if (!Rd(node + oUicVisible, vis) || !vis) return;
    char nm[64];
    UicName(node, nm, sizeof nm);
    float x = 0, y = 0, w = 0, h = 0;
    Rd(node + oUicPos, x);
    Rd(node + oUicPos + 4, y);
    uintptr_t lay = Ptr(node + oUicLayout);
    if (lay) {
        Rd(lay + oLayoutW, w);
        Rd(lay + oLayoutH, h);
    }
    uint32_t cnt = 0;
    Rd(node + oUicChildCount, cnt);
    char pad[64];
    int d = depth < 30 ? depth : 30;
    memset(pad, ' ', d * 2);
    pad[d * 2] = 0;
    Log("ui-dump %s%s  pos=%.0f,%.0f size=%.0fx%.0f children=%u", pad, nm, x, y, w, h, cnt);
    --*lines;
    uintptr_t arr = Ptr(node + oUicChildren);
    if (!arr || cnt > 4096) return;
    for (uint32_t i = 0; i < cnt && *lines > 0; ++i) DumpUi(Ptr(arr + 8 * i), depth + 1, lines);
}
bool g_uiDumped = false;
void UiStep() {
    bool want = S.active && cfg.hideHud && g_uiOk && cfg.hideCount > 0;
    if (!want && !S.uiApplied) return;
    uintptr_t ui = Ptr(g_base + kGlobalUi);
    uintptr_t root = Ptr(g_base + kGlobalUiRoot);
    if (!root && ui) root = Ptr(ui + oUiHudRoot);
    using FnVis = void (*)(uintptr_t uic, bool visible);
    FnVis setVis = reinterpret_cast<FnVis>(g_base + kFnUicSetVisible);
    if (want && !S.uiApplied) {
        if (!root) {
            if (S.uiLogs++ < 3) Log("ui: battle hud root not available (ui=%p)", (void*)ui);
            return;
        }
        char rn[64];
        UicName(root, rn, sizeof rn);
        S.uiCount = 0;
        for (int i = 0; i < cfg.hideCount; ++i) {
            int budget = 200000;
            uintptr_t c = FindUic(root, cfg.hideNames[i], 30, &budget, true);
            if (!c) {
                budget = 200000;
                c = FindUic(root, cfg.hideNames[i], 30, &budget);
            }
            if (!c) {
                if (S.uiLogs++ < 12) Log("ui: '%s' not found under '%s'", cfg.hideNames[i], rn);
                continue;
            }
            uint8_t v = 1;
            Rd(c + oUicVisible, v);
            auto& h = S.uiHidden[S.uiCount++];
            h.uic = c;
            h.orig = v;
            memcpy(h.name, cfg.hideNames[i], sizeof h.name);
            if (v) setVis(c, false);
        }
        S.uiApplied = true;
        S.uiRoot = root;
        S.uiRehides = 0;
        Log("ui: hid %d of %d battle hud parts under '%s' (%p)", S.uiCount, cfg.hideCount, rn, (void*)root);
        if (S.uiCount < cfg.hideCount && !g_uiDumped) {
            g_uiDumped = true;
            int lines = 1500;
            DumpUi(root, 0, &lines);
            Log("ui-dump end (%d lines left)", lines);
        }
        return;
    }
    if (want) {
        if (root != S.uiRoot) {
            S.uiApplied = false;
            S.uiCount = 0;
            return;
        }
        for (int i = 0; i < S.uiCount; ++i) {
            uint8_t v = 0;
            if (Rd(S.uiHidden[i].uic + oUicVisible, v) && v) {
                setVis(S.uiHidden[i].uic, false);
                if (S.uiRehides++ < 8) Log("ui: game showed '%s' again; hidden", S.uiHidden[i].name);
            }
        }
        return;
    }
    int restored = 0;
    if (root && root == S.uiRoot) {
        for (int i = 0; i < S.uiCount; ++i) {
            if (!UicUnder(S.uiHidden[i].uic, root)) continue;
            setVis(S.uiHidden[i].uic, S.uiHidden[i].orig != 0);
            ++restored;
        }
    }
    Log("ui: restored %d of %d battle hud parts", restored, S.uiCount);
    S.uiApplied = false;
    S.uiCount = 0;
}
void CursorStep() {
    using FnBool = void (*)(bool);
    FnBool setEnabled = reinterpret_cast<FnBool>(g_base + kFnSetCursorEnabled);
    FnBool setVisible = reinterpret_cast<FnBool>(g_base + kFnSetCursorVisible);
    bool want = S.active && cfg.hideCursor && g_uiOk;
    if (want) {
        uint8_t on = 0, vis = 0;
        Rd(g_base + kGlobalCursorEnabled, on);
        Rd(g_base + kGlobalCursorVisible, vis);
        if (!S.cursorOff) {
            S.cursorOrig = on;
            S.cursorOff = true;
            S.cursorRehides = 0;
            setEnabled(false);
            Log("ui: cursor disabled (was %u)", on);
        } else if (on) {
            setEnabled(false);
            if (S.cursorRehides++ < 8) Log("ui: game enabled the cursor again; disabled");
        }
        if (!S.cursorHidden) {
            S.cursorVisOrig = vis;
            S.cursorHidden = true;
            S.cursorVisRehides = 0;
            setVisible(false);
            Log("ui: cursor visibility off (was %u) - no battlefield hover / unit tooltip", vis);
        } else if (vis) {
            setVisible(false);
            if (S.cursorVisRehides++ < 8) Log("ui: game made the cursor visible again; hidden");
        }
        return;
    }
    if (S.cursorHidden) {
        setVisible(S.cursorVisOrig != 0);
        S.cursorHidden = false;
        Log("ui: cursor visibility restored (%u)", S.cursorVisOrig);
    }
    if (S.cursorOff) {
        setEnabled(S.cursorOrig != 0);
        S.cursorOff = false;
        Log("ui: cursor restored (%u)", S.cursorOrig);
    }
}
constexpr uintptr_t oBattleUiVis = 0x64600, oUiVisBattle = 0x28, oUiVisPathMgr = 0x1a0, oPathMgrBattle = 0x10;
constexpr uintptr_t oPathMode[2] = {0x540, 0x548};
uintptr_t PathMgr(uintptr_t battle) {
    if (!battle) return 0;
    uintptr_t v = Ptr(battle + oBattleUiVis);
    if (!v || Ptr(v + oUiVisBattle) != battle) return 0;
    uintptr_t mgr = Ptr(v + oUiVisPathMgr);
    if (!mgr || Ptr(mgr + oPathMgrBattle) != battle) return 0;
    return mgr;
}
constexpr uintptr_t oUicStateCount = 0x1dc, oUicStates = 0x1e0, oStateImgCount = 0x34, oStateImgs = 0x38;
constexpr uintptr_t oImgAlpha = 0xa3, oImgAlphaBase = 0xbc, oStateText = 0x138, oTextAlpha = 0xef;
void EdgeCollect(uintptr_t uic, int depth) {
    auto add = [](uintptr_t a) {
        uint8_t v = 0;
        if (S.edgeCount < S.kMaxEdgeBytes && Rd(a, v)) {
            S.edgeAddr[S.edgeCount] = a;
            S.edgeOrig[S.edgeCount] = v;
            ++S.edgeCount;
        }
    };
    uint32_t ns = 0;
    uintptr_t states = Ptr(uic + oUicStates);
    if (Rd(uic + oUicStateCount, ns) && states && ns <= 64) {
        for (uint32_t i = 0; i < ns; ++i) {
            uintptr_t st = Ptr(states + 8 * i);
            if (!st) continue;
            uint32_t ni = 0;
            uintptr_t imgs = Ptr(st + oStateImgs);
            if (Rd(st + oStateImgCount, ni) && imgs && ni <= 64) {
                for (uint32_t j = 0; j < ni; ++j) {
                    uintptr_t img = Ptr(imgs + 8 * j);
                    if (!img) continue;
                    add(img + oImgAlpha);
                    add(img + oImgAlphaBase);
                }
            }
            uintptr_t txt = Ptr(st + oStateText);
            if (txt) add(txt + oTextAlpha);
        }
    }
    if (depth <= 0) return;
    uint32_t nc = 0;
    uintptr_t kids = Ptr(uic + oUicChildren);
    if (Rd(uic + oUicChildCount, nc) && kids && nc <= 64)
        for (uint32_t i = 0; i < nc; ++i) EdgeCollect(Ptr(kids + 8 * i), depth - 1);
}
void EdgeStep() {
    bool want = S.active && cfg.hideEdgeArrows && g_uiOk;
    uintptr_t root = Ptr(g_base + kGlobalUiRoot);
    if (want) {
        if (!S.edgeApplied) {
            uint32_t nc = 0;
            uintptr_t kids = root ? Ptr(root + oUicChildren) : 0;
            int found = 0;
            if (kids && Rd(root + oUicChildCount, nc) && nc <= 4096) {
                for (uint32_t i = 0; i < nc && found < 2; ++i) {
                    uintptr_t c = Ptr(kids + 8 * i);
                    char nm[64];
                    if (!c || !UicName(c, nm, sizeof nm) || (strcmp(nm, "left") && strcmp(nm, "right"))) continue;
                    int budget = 64;
                    if (!FindUic(c, "button_flame", 2, &budget)) continue;
                    S.edgeUic[found++] = c;
                }
            }
            if (found == 0) {
                if (!S.edgeWarned) {
                    S.edgeWarned = true;
                    Log("ui: edge arrows (left / right under the ui root) not found; not hidden");
                }
                return;
            }
            S.edgeCount = 0;
            for (int k = 0; k < found; ++k) EdgeCollect(S.edgeUic[k], 4);
            const uint8_t zero = 0;
            for (int k = 0; k < S.edgeCount; ++k) Wr(S.edgeAddr[k], zero);
            S.edgeApplied = true;
            S.edgeRewrites = 0;
            Log("ui: edge arrows off (%d components, %d opacity bytes -> 0)", found, S.edgeCount);
            return;
        }
        int changed = 0;
        for (int k = 0; k < S.edgeCount; ++k) {
            uint8_t v = 0;
            if (Rd(S.edgeAddr[k], v) && v) {
                const uint8_t zero = 0;
                Wr(S.edgeAddr[k], zero);
                ++changed;
            }
        }
        if (changed && S.edgeRewrites++ < 8) Log("ui: game changed %d edge arrow opacity bytes; set to 0 again", changed);
        return;
    }
    if (S.edgeApplied) {
        S.edgeApplied = false;
        bool still = root && S.edgeUic[0] && Ptr(S.edgeUic[0] + oUicParent) == root;
        if (still) {
            for (int k = 0; k < S.edgeCount; ++k) Wr(S.edgeAddr[k], S.edgeOrig[k]);
            Log("ui: edge arrows restored (%d opacity bytes)", S.edgeCount);
        } else {
            Log("ui: edge arrows gone (battle ended?); nothing to restore");
        }
        S.edgeCount = 0;
        S.edgeUic[0] = S.edgeUic[1] = 0;
    }
}
constexpr uintptr_t oProjSpeed = 0x64, oProjMass = 0xc0, oUnitAmmo = 0x3d90, oAmmoNoTake = 0xc, kAmmoSlot = 0x10;
void ShotRestoreSpeed(const char* why) {
    if (!S.speedApplied) return;
    S.speedApplied = false;
    Wr(S.speedRec + oProjSpeed, S.speedOrig);
    Wr(S.speedRec + oProjMass, S.massOrig);
    Log("fire: bullet speed restored (%.0f m/s, mass %.2f) - %s", S.speedOrig, S.massOrig, why);
}
void ShotStep() {
    bool active = S.active && g_shotOk && g_fireOk && cfg.fireEnabled;
    uintptr_t rec = active && cfg.bulletSpeed > 0.f && S.unit ? (S.fpProj ? S.fpProj : Ptr(S.unit + oUnitProjectile)) : 0;
    if (S.speedApplied && rec != S.speedRec) ShotRestoreSpeed(rec ? "weapon changed" : "first person ended");
    if (rec && !S.speedApplied) {
        float sp = 0, m = 0;
        if (Rd(rec + oProjSpeed, sp) && Rd(rec + oProjMass, m) && std::fabs(sp - cfg.bulletSpeed) < 1.f) {
            S.speedRec = rec;
            S.speedOrig = sp;
            S.massOrig = m;
            S.speedApplied = true;
            Log("fire: bullet speed already %.0f m/s (mass %.2f) - unchanged", sp, m);
        } else if (Rd(rec + oProjSpeed, sp) && Rd(rec + oProjMass, m) && sp > 1.f && sp < 20000.f && m >= 0.f && m < 1e5f) {
            float ns = cfg.bulletSpeed, nm = m * sp / ns;
            S.speedRec = rec;
            S.speedOrig = sp;
            S.massOrig = m;
            S.speedApplied = true;
            Wr(rec + oProjSpeed, ns);
            Wr(rec + oProjMass, nm);
            Log("fire: bullet speed %.0f -> %.0f m/s, mass %.2f -> %.3f (same impact momentum)", sp, ns, m, nm);
        } else if (S.shotLogs++ < 8) {
            Log("fire: projectile record %p speed %.2f mass %.2f look wrong; bullet speed unchanged", (void*)rec, sp, m);
        }
    }
    bool wantAmmo = active && cfg.fireShooter && cfg.unlimitedAmmo && S.unit && S.entity;
    if (wantAmmo && !S.ammoApplied) {
        S.ammoUnit = S.unit;
        S.ammoEntity = S.entity;
        for (int w = 0; w < 2; ++w) {
            uintptr_t slot = S.unit + oUnitAmmo + w * kAmmoSlot;
            Rd(slot, S.ammoOrig[w][0]);
            Rd(slot + oAmmoNoTake, S.ammoOrig[w][1]);
            const int32_t one = 1;
            Wr(slot + oAmmoNoTake, one);
            if (S.ammoOrig[w][0] == 0) Wr(slot, one);
        }
        S.ammoApplied = true;
        Log("fire: unlimited ammo on (ammo %d/%d, no-take flags were %d/%d)", S.ammoOrig[0][0], S.ammoOrig[1][0], S.ammoOrig[0][1],
            S.ammoOrig[1][1]);
    } else if (wantAmmo) {
        for (int w = 0; w < 2; ++w) {
            uintptr_t slot = S.unit + oUnitAmmo + w * kAmmoSlot;
            int32_t f = 1, n = 1;
            Rd(slot + oAmmoNoTake, f);
            Rd(slot, n);
            const int32_t one = 1;
            if (f != 1) Wr(slot + oAmmoNoTake, one);
            if (n == 0) Wr(slot, one);
        }
    } else if (S.ammoApplied) {
        S.ammoApplied = false;
        if (Ptr(S.ammoEntity + oEntUnit) == S.ammoUnit) {
            for (int w = 0; w < 2; ++w) {
                uintptr_t slot = S.ammoUnit + oUnitAmmo + w * kAmmoSlot;
                Wr(slot + oAmmoNoTake, S.ammoOrig[w][1]);
                int32_t n = 0;
                Rd(slot, n);
                if (S.ammoOrig[w][0] == 0 && n == 1) Wr(slot, S.ammoOrig[w][0]);
            }
            Log("fire: unlimited ammo off (restored ammo %d, no-take flags %d/%d)", S.ammoOrig[0][0], S.ammoOrig[0][1], S.ammoOrig[1][1]);
        } else {
            Log("fire: hero unit gone; ammo flags not restored");
        }
    }
}
void PathStep() {
    bool want = S.active && cfg.hidePaths && g_uiOk;
    if (want) {
        uintptr_t mgr = PathMgr(S.battle);
        int32_t m[2] = {-1, -1};
        if (mgr) {
            Rd(mgr + oPathMode[0], m[0]);
            Rd(mgr + oPathMode[1], m[1]);
        }
        if (!S.pathOff) {
            if (!mgr || m[0] < 0 || m[0] > 2 || m[1] < 0 || m[1] > 2) {
                if (!S.pathWarned) {
                    S.pathWarned = true;
                    Log("ui: unit path display manager not verified (battle %p mgr %p modes %d/%d); path arrows not hidden",
                        (void*)S.battle, (void*)mgr, m[0], m[1]);
                }
                return;
            }
            S.pathMgr = mgr;
            S.pathOrig[0] = m[0];
            S.pathOrig[1] = m[1];
            S.pathOff = true;
            S.pathRewrites = 0;
            const int32_t zero = 0;
            Wr(mgr + oPathMode[0], zero);
            Wr(mgr + oPathMode[1], zero);
            Log("ui: unit path arrows off (manager %p, modes %d/%d -> 0)", (void*)mgr, m[0], m[1]);
        } else if (mgr == S.pathMgr && (m[0] != 0 || m[1] != 0)) {
            const int32_t zero = 0;
            Wr(mgr + oPathMode[0], zero);
            Wr(mgr + oPathMode[1], zero);
            if (S.pathRewrites++ < 8) Log("ui: path display modes changed by the game (%d/%d); set to 0 again", m[0], m[1]);
        }
        return;
    }
    if (S.pathOff) {
        S.pathOff = false;
        if (PathMgr(S.battle) == S.pathMgr) {
            Wr(S.pathMgr + oPathMode[0], S.pathOrig[0]);
            Wr(S.pathMgr + oPathMode[1], S.pathOrig[1]);
            Log("ui: unit path arrows restored (modes %d/%d)", S.pathOrig[0], S.pathOrig[1]);
        } else {
            Log("ui: unit path display manager gone (battle ended?); nothing to restore");
        }
    }
}
bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
void RecordTp(double now, float x, float z) {
    TpRec& r = S.tpHist[S.tpHead++ % (sizeof S.tpHist / sizeof S.tpHist[0])];
    r.t = now;
    r.x = x;
    r.z = z;
}
struct DelayProbe {
    bool armed = false;
    double tShot = 0, tQ = 0, tP = 0;
    uint32_t stepShot = 0, stepQ = 0, stepP = 0;
    bool sawQ = false, sawP = false;
    uint32_t lastStep = 0;
    double tLastStep = 0, stepMin = 1e9, stepMax = 0, stepSum = 0;
    unsigned stepN = 0, logged = 0;
    unsigned samples = 0, simMark = 0;
    uint32_t simMarkVal = 0;
    unsigned char type[4] = {};
    bool typeOk = false;
} g_dp;
void ProbeInit() {
    g_dp = DelayProbe();
    unsigned char op[6] = {};
    for (int i = 0; i < 6; ++i) Rd(g_base + kFnIssueCmd + 0xA + i, op[i]);
    if (op[0] != 0x8B || op[1] != 0x05) return;
    int32_t disp = 0;
    memcpy(&disp, op + 2, 4);
    uint32_t t = 0;
    if (!Rd(g_base + kFnIssueCmd + 0x10 + disp, t) || !t) return;
    g_dp.type[0] = static_cast<unsigned char>(t >> 24);
    g_dp.type[1] = static_cast<unsigned char>(t >> 16);
    g_dp.type[2] = static_cast<unsigned char>(t >> 8);
    g_dp.type[3] = static_cast<unsigned char>(t);
    g_dp.typeOk = true;
}
bool HasFireCmd(uintptr_t p, uint32_t n) {
    if (!g_dp.typeOk || !p || n < 8 || n > 0x5000) return false;
    static unsigned char buf[0x5000];
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(p), buf, n, &got) || got != n) return false;
    for (uint32_t i = 0; i + 7 <= n; ++i)
        if (buf[i + 2] == g_dp.type[0] && buf[i + 3] == g_dp.type[1] && buf[i + 4] == g_dp.type[2] && buf[i + 5] == g_dp.type[3])
            return true;
    return false;
}
bool QueueHasFire(uintptr_t battle) {
    uintptr_t q = Ptr(battle + 0x644e8);
    uint32_t n = 0;
    return q && Rd(q + 0x5008, n) && HasFireCmd(q + 8, n);
}
bool PacketHasFire(uintptr_t battle) {
    uint32_t blocks = 0;
    if (!Rd(battle + 0x468 + 0x64054, blocks) || blocks > 20) return false;
    for (uint32_t i = 0; i < blocks; ++i) {
        uintptr_t b = battle + 0x468 + 4 + static_cast<uintptr_t>(i) * 0x5004;
        uint32_t n = 0;
        if (Rd(b + 0x5000, n) && HasFireCmd(b, n)) return true;
    }
    return false;
}
float StepPeriod() {
    float p = g_dp.stepN ? static_cast<float>(g_dp.stepSum / g_dp.stepN) * 0.001f : 0.1f;
    return (p > 0.03f && p < 0.3f) ? p : 0.1f;
}
double LastStepTime() { return g_dp.tLastStep; }
uint32_t LastStep() { return g_dp.lastStep; }
void ProbeShot(uintptr_t battle, double now) {
    if (g_dp.armed || g_dp.logged >= 10) return;
    g_dp.armed = true;
    g_dp.tShot = now;
    Rd(battle + 0x64720, g_dp.stepShot);
    g_dp.sawQ = QueueHasFire(battle);
    g_dp.sawP = false;
    g_dp.tQ = g_dp.tP = 0;
}
void ProbeFrame(uintptr_t battle, double now) {
    if (!battle) return;
    uint32_t step = 0;
    Rd(battle + 0x64720, step);
    if (step != g_dp.lastStep) {
        if (g_dp.tLastStep > 0 && step == g_dp.lastStep + 1) {
            double d = (now - g_dp.tLastStep) * 1000.0;
            g_dp.stepSum += d;
            ++g_dp.stepN;
            if (d < g_dp.stepMin) g_dp.stepMin = d;
            if (d > g_dp.stepMax) g_dp.stepMax = d;
        }
        g_dp.lastStep = step;
        g_dp.tLastStep = now;
    }
    if (kFnIssueCmd == 0x2e9b9e4) {
        uint32_t m = 0;
        ++g_dp.samples;
        if (Rd(g_base + 0x3f9e158, m) && m) {
            ++g_dp.simMark;
            g_dp.simMarkVal = m;
        }
    }
    if (!g_dp.armed) return;
    bool inQ = QueueHasFire(battle), inP = PacketHasFire(battle);
    if (g_dp.sawQ && !inQ && g_dp.tQ == 0) {
        g_dp.tQ = now;
        g_dp.stepQ = step;
    }
    if (inP && !g_dp.sawP) {
        g_dp.sawP = true;
        g_dp.tP = now;
        g_dp.stepP = step;
    }
    bool done = g_dp.sawP && !inP;
    bool timeout = now - g_dp.tShot > 1.5;
    if (done || timeout) {
        auto ms = [&](double t) { return t > 0 ? (t - g_dp.tShot) * 1000.0 : -1.0; };
        Log("delay-probe: shot %u issued at step %u; in local queue=%d; queue taken %+.0f ms (step %u); in stored packet "
            "%+.0f ms (step %u); %s %+.0f ms (step %u); steps avg %.1f ms (min %.1f max %.1f, n %u); sim-thread marker seen "
            "%u/%u (value %u, our thread %lu)",
            g_dp.logged + 1, g_dp.stepShot, g_dp.sawQ, ms(g_dp.tQ), g_dp.stepQ, ms(g_dp.tP), g_dp.stepP,
            done ? "executed (left the packet)" : "TIMEOUT", (now - g_dp.tShot) * 1000.0, step,
            g_dp.stepN ? g_dp.stepSum / g_dp.stepN : 0.0, g_dp.stepN ? g_dp.stepMin : 0.0, g_dp.stepMax, g_dp.stepN,
            g_dp.simMark, g_dp.samples, g_dp.simMarkVal, GetCurrentThreadId());
        ++g_dp.logged;
        g_dp.armed = false;
    }
}
volatile LONG g_reqLauncher = 0;
volatile LONG g_heroLauncher = 0;
struct LauncherGame {
    unsigned casts = 0, refused = 0, busy = 0;
    int logs = 0;
    int32_t id[vmgame::kLauncherKinds] = {-2, -2, -2};
    int kind = -1;
    uintptr_t inst = 0;
    double tIssue = 0, windup = 1.0;
    bool sawWind = false;
    double tHeroCheck = -1;
    uintptr_t heroUnit = 0;
} g_lg;
int32_t AbilityId(int kind) {
    int32_t& id = g_lg.id[kind];
    if (id != -2) return id;
    const char* key = vmgame::LauncherAbility(kind);
    uintptr_t sb = Ptr(g_base + kGlobalScript);
    uintptr_t battle = sb ? Ptr(sb + oScriptBattle) : 0;
    uintptr_t dbo = battle ? Ptr(battle + 0x430) : 0;
    uintptr_t db = dbo ? Ptr(dbo + 0x20) : 0;
    if (!key[0] || !db) return -1;
    using FnTable = uintptr_t (*)(uintptr_t);
    using FnFind = uint32_t (*)(uintptr_t, void*);
    using FnAt = uintptr_t (*)(uintptr_t, uint32_t);
    using FnStr = void* (*)(void*, const char*);
    using FnStrDel = void (*)(void*);
    uintptr_t table = reinterpret_cast<FnTable>(g_base + kFnAbilityTable904)(db);
    if (!table) return -1;
    alignas(16) unsigned char str[64] = {};
    reinterpret_cast<FnStr>(g_base + kFnStrCtor)(str, key);
    uint32_t idx = reinterpret_cast<FnFind>(g_base + kFnTableFind)(table, str);
    reinterpret_cast<FnStrDel>(g_base + kFnStrDtor)(str);
    uintptr_t rec = reinterpret_cast<FnAt>(g_base + kFnTableAt)(table, idx);
    int32_t v = -1;
    if (rec) Rd(rec + oAbRecId, v);
    id = rec ? v : -1;
    Log("launcher: ability %s -> record %p id %d", key, (void*)rec, id);
    return id;
}
bool FindAbility(int32_t id, uintptr_t* inst, uintptr_t* def) {
    uintptr_t mgr = S.unit ? Ptr(S.unit + oUnitAbilityMgr) : 0;
    if (!mgr) return false;
    uintptr_t end = mgr + oAbMgrEnd, n = Ptr(mgr + oAbMgrFirst);
    for (int guard = 0; n && n != end && guard < 256; ++guard, n = Ptr(n + oAbNodeNext)) {
        uintptr_t d = Ptr(n + oAbNodeDef);
        int32_t did = -1;
        if (d && Rd(d + oAbDefId, did) && did == id) {
            *inst = n + oAbNodeInst, *def = d;
            return true;
        }
    }
    return false;
}
bool LauncherTryCast(int kind, const float eye[3], const float fwd[3]) {
    const char* key = vmgame::LauncherAbility(kind);
    auto refuse = [&](const char* why) {
        ++g_lg.refused;
        if (g_lg.logs < 30) ++g_lg.logs, Log("launcher: %s not cast - %s", key, why);
        return false;
    };
    if (!key[0]) return refuse("no ability key in [launcher]");
    if (vmgame::LauncherBusy() || g_lg.kind >= 0) {
        ++g_lg.busy;
        return false;
    }
    if (!g_abilityOk) return refuse("ability cast code does not match this exe");
    if (S.ghost) return refuse("ghost mode (hero disabled)");
    uintptr_t battle = 0, queue = CmdQueue(&battle);
    if (!queue) return refuse("battle paused / no command queue");
    int32_t id = AbilityId(kind);
    if (id < 0) return refuse("ability not in the special ability table");
    uintptr_t A = 0, D = 0;
    if (!FindAbility(id, &A, &D)) return refuse("this hero does not have it");
    int32_t steps = 0, cool = 0, uses = 0;
    unsigned char flags = 0;
    float windup = 1.f, range = -1.f;
    Rd(A + oAbWindSteps, steps), Rd(A + oAbCooldown, cool), Rd(A + oAbUses, uses), Rd(A + oAbFlags, flags);
    Rd(D + oAbDefWindup, windup), Rd(D + oAbDefRange, range);
    char why[96];
    if (steps > 0 || (flags & 1) || cool >= 1 || uses == 0) {
        snprintf(why, sizeof why, "not ready (wind-up steps %d, active %d, cooldown steps %d, uses %d)", steps, flags & 1, cool, uses);
        return refuse(why);
    }
    float H[3] = {0, 0, 0};
    if (!S.entity || !Rd(S.entity + oEntPos, H)) return refuse("no hero entity position");
    float lim = range > 0.f ? range * 0.95f : 120.f;
    AimHit hit = TraceAim(eye, fwd, lim + 30.f, 0.f);
    float P[3] = {eye[0] + fwd[0] * hit.dist, eye[1] + fwd[1] * hit.dist, eye[2] + fwd[2] * hit.dist};
    for (int pass = 0; pass < 3; ++pass) {
        float dx = P[0] - H[0], dz = P[2] - H[2], h = std::sqrt(dx * dx + dz * dz), dy = P[1] - H[1];
        float d3 = std::sqrt(h * h + dy * dy);
        if (pass > 0 && d3 <= lim) break;
        float hmax = lim * lim > dy * dy ? std::sqrt(lim * lim - dy * dy) : 0.f;
        if (pass == 0) hmax = lim;
        if (h > hmax && h > 1e-3f) P[0] = H[0] + dx * hmax / h, P[2] = H[2] + dz * hmax / h;
        alignas(16) float dest[8];
        float sh = 0.f;
        if (MakeDestAt(P[0], P[2], dest) && std::fabs(dest[0] - P[0]) < 3.f && std::fabs(dest[2] - P[2]) < 3.f)
            P[1] = SurfaceNear(P[0], P[2], P[1], &sh) ? sh : dest[1];
        else if (pass == 0)
            P[1] = H[1];
    }
    float ang = std::atan2(P[0] - H[0], P[2] - H[2]) * 10430.378f;
    unsigned angle = static_cast<unsigned>(static_cast<uint16_t>(static_cast<int16_t>(std::lround(ang))));
    SelBuf sel;
    unsigned n = MakeSelection(sel);
    bool ok = false;
    if (n) {
        alignas(16) unsigned char cmd[0xc0] = {};
        using FnCtor = void* (*)(void*, void*, int, int, uintptr_t, const float*, unsigned, float);
        using FnIssue = unsigned char (*)(uintptr_t, void*);
        reinterpret_cast<FnCtor>(g_base + kFnAbilityGroundCtor904)(cmd, &sel, id, 0, 0, P, angle, 0.f);
        ok = reinterpret_cast<FnIssue>(g_base + kFnAbilityIssue904)(queue, cmd) != 0;
        FreeSelection(cmd + 8);
    }
    FreeSelection(&sel);
    if (g_lg.logs < 30)
        ++g_lg.logs, Log("launcher: %s id %d -> %s (units %u) target %.1f,%.1f,%.1f (aim %s %.1f m, range %.0f m) hero %.1f,%.1f,%.1f angle %u wind-up %.2f s",
                         key, id, ok ? "issued" : "NOT issued", n, P[0], P[1], P[2], hit.what, hit.dist, range, H[0], H[1], H[2], angle, windup);
    if (!ok) return refuse(n ? "command queue refused" : "hero not accepted into a selection");
    ++g_lg.casts;
    g_lg.kind = kind, g_lg.inst = A, g_lg.tIssue = Now(), g_lg.windup = windup, g_lg.sawWind = false;
    vmgame::LauncherCast(kind, true, windup);
    return true;
}
struct LauncherWatch {
    uintptr_t inst[vmgame::kLauncherKinds] = {}, def[vmgame::kLauncherKinds] = {}, unit = 0;
    double tScan = -1;
    int busySkips = 0;
} g_lw;
void LauncherWatchOtherCasts(double now) {
    if (!g_heroLauncher || !g_abilityOk || !vmgame::LauncherConfigured()) return;
    if (S.unit != g_lw.unit || g_lw.tScan < 0 || now - g_lw.tScan >= 1.0) {
        for (int k = 0; k < vmgame::kLauncherKinds; ++k) {
            uintptr_t a = 0, d = 0;
            int32_t id = AbilityId(k);
            bool has = id >= 0 && FindAbility(id, &a, &d);
            g_lw.inst[k] = has ? a : 0, g_lw.def[k] = has ? d : 0;
        }
        g_lw.unit = S.unit, g_lw.tScan = now;
    }
    for (int k = 0; k < vmgame::kLauncherKinds; ++k) {
        uintptr_t A = g_lw.inst[k];
        int32_t steps = 0;
        if (!A || !Rd(A + oAbWindSteps, steps) || steps <= 0) continue;
        float windup = vmgame::LauncherWindup(k);
        if (g_lw.def[k]) Rd(g_lw.def[k] + oAbDefWindup, windup);
        if (A == g_lg.inst && now - g_lg.tIssue < windup + 2.5) continue;
        g_lg.inst = A, g_lg.tIssue = now;
        if (vmgame::LauncherBusy()) {
            if (g_lw.busySkips++ < 4) Log("launcher: %s wind-up started while the launcher is still busy - not animated", vmgame::LauncherAbility(k));
            continue;
        }
        g_lg.kind = k, g_lg.windup = windup, g_lg.sawWind = true;
        ++g_lg.casts;
        vmgame::LauncherCast(k, true, windup);
        if (g_lg.logs < 30)
            ++g_lg.logs, Log("launcher: %s wind-up started without G / X / F (ability bar / game button) - first-person launcher follows it", vmgame::LauncherAbility(k));
        return;
    }
}
void LauncherGameStep(double now) {
    if (vmgame::LauncherConfigured() && g_abilityOk && (S.unit != g_lg.heroUnit || g_lg.tHeroCheck < 0 || now - g_lg.tHeroCheck >= 1.0)) {
        bool has = false;
        for (int k = 0; k < vmgame::kLauncherKinds && !has; ++k) {
            uintptr_t a, d;
            int32_t id = AbilityId(k);
            has = id >= 0 && FindAbility(id, &a, &d);
        }
        if (has != (g_heroLauncher != 0) || S.unit != g_lg.heroUnit)
            Log("launcher: hero %p %s the shoulder cannon abilities (G / X / F %s)", (void*)S.unit, has ? "has" : "does not have",
                has ? "are ours" : "go to the game");
        InterlockedExchange(&g_heroLauncher, has ? 1 : 0);
        g_lg.tHeroCheck = now, g_lg.heroUnit = S.unit;
    }
    if (g_lg.kind < 0) LauncherWatchOtherCasts(now);
    if (g_lg.kind < 0) return;
    int32_t steps = 0, cool = 0;
    unsigned char flags = 0;
    bool rd = Rd(g_lg.inst + oAbWindSteps, steps) && Rd(g_lg.inst + oAbCooldown, cool) && Rd(g_lg.inst + oAbFlags, flags);
    const char* what = nullptr;
    if (!rd) {
        vmgame::LauncherAbort(), what = "instance unreadable - folded";
    } else if (steps > 0) {
        g_lg.sawWind = true;
    } else if (g_lg.sawWind) {
        if ((flags & 1) || cool > 0)
            vmgame::LauncherFire(), what = "released";
        else
            vmgame::LauncherAbort(), what = "cancelled - folded";
    } else if (now - g_lg.tIssue > 0.6) {
        vmgame::LauncherAbort(), what = "wind-up never started (out of range / refused?) - folded";
    }
    if (!what && now - g_lg.tIssue > g_lg.windup + 2.0) vmgame::LauncherAbort(), what = "no release in time - folded";
    if (what) {
        if (g_lg.logs < 30) ++g_lg.logs, Log("launcher: %s %s %.2f s after the command", vmgame::LauncherAbility(g_lg.kind), what, now - g_lg.tIssue);
        g_lg.kind = -1;
    }
}
void LauncherReset() {
    g_lg.kind = -1;
    g_lg.tHeroCheck = -1;
    InterlockedExchange(&g_reqLauncher, 0);
    InterlockedExchange(&g_heroLauncher, 0);
}
enum WallUpWhy { kWhyNone = 0, kWhyNoWall, kWhyPaused, kWhyNotGround, kWhyNoTop, kWhyHeight, kWhyGate, kWhyInside, kWhyRoof };
constexpr float kWallUpStep = 0.25f, kWallUpInset = 0.6f, kWallUpThick = 6.f, kWallUpGround = 2.f, kWallUpSameH = 0.5f, kWallUpKeep = 1.f;
const char* WallUpWhyText(int why) {
    switch (why) {
        case kWhyNoTop: return "tall building wall ahead but no wall-top nav block behind it (house / tower without a walkway)";
        case kWhyHeight: return "wall top higher than wall_up_max_height";
        case kWhyGate: return "gate passage near the landing (gatehouse)";
        case kWhyInside: return "landing inside a building";
        case kWhyRoof: return "roof over the landing (tower / gatehouse room)";
        default: return "-";
    }
}
bool GatePassageNear(float x, float z, float fx, float fz, float foot, float top, float r) {
    if (!(r > 0.f)) return false;
    const float mid = foot + 0.5f * (top - foot);
    Surf s[kMaxSurf];
    int run = 0;
    for (float o = -r; o <= r + 1e-3f; o += 0.5f) {
        int n = SurfacesAt(x - fz * o, z + fx * o, s);
        bool low = false, high = false;
        for (int i = 0; i < n; ++i) {
            if (!s[i].blk) continue;
            if (s[i].h <= mid)
                low = true;
            else
                high = true;
        }
        run = low && high ? run + 1 : 0;
        if (run >= 2) return true;
    }
    return false;
}
bool RoofOver(float x, float z, float h) {
    uint32_t t = 0;
    float hp[3] = {};
    const float lo[3] = {x, h + 2.5f, z}, hi[3] = {x, h + 8.f, z};
    return SegHitsBuilding(lo, hi, &t, hp) || SegHitsBuilding(hi, lo, &t, hp);
}
bool FindWallUp(float x, float z, float foot, float fx, float fz, float out[3], uintptr_t* blkOut, int* why) {
    int dummy = 0;
    if (!why) why = &dummy;
    auto no = [why](int w) {
        *why = w;
        return false;
    };
    float fl = std::sqrt(fx * fx + fz * fz);
    if (!(fl > 1e-4f)) return no(kWhyNoWall);
    fx /= fl;
    fz /= fl;
    Surf s[kMaxSurf];
    int n = SurfacesAt(x, z, s), terr = -1;
    if (!n) return no(kWhyPaused);
    for (int i = 0; i < n; ++i)
        if (!s[i].blk) terr = i;
    if (terr < 0 || foot > s[terr].h + kWallUpGround) return no(kWhyNotGround);
    uint32_t t = 0;
    float hp[3] = {};
    const float p0[2] = {x, z}, pr[2] = {x + fx * cfg.wallUpReach, z + fz * cfg.wallUpReach};
    const bool bld = TallWallBetween(p0, pr, foot, &t, hp);
    const float maxD = cfg.wallUpReach + (bld ? kWallUpThick : cfg.wallUpBase);
    float bodyD = -1.f;
    for (float d = kWallUpStep; d <= maxD + 1e-3f; d += kWallUpStep) {
        int m = SurfacesAt(x + fx * d, z + fz * d, s);
        if (!m) return no(kWhyPaused);
        if (StructureOver(s, m, foot, cfg.wallUpMin, cfg.stepDown)) {
            bodyD = d;
            break;
        }
    }
    if (bodyD < 0.f) return no(bld ? kWhyNoTop : kWhyNoWall);
    const float p1[2] = {x + fx * bodyD, z + fz * bodyD};
    n = SurfacesAt(p1[0], p1[1], s);
    int top = -1;
    for (int i = 0; i < n; ++i)
        if (s[i].blk && (top < 0 || s[i].h > s[top].h)) top = i;
    if (top < 0 || s[top].h < foot + cfg.wallUpMin || s[top].h > foot + cfg.wallUpMaxH) return no(kWhyHeight);
    uintptr_t blk = s[top].blk;
    const float topH = s[top].h;
    float land = bodyD, h = topH;
    auto onTop = [&](float d, uintptr_t* b, float* hh) {
        int m = SurfacesAt(x + fx * d, z + fz * d, s), hi = -1;
        for (int i = 0; i < m; ++i)
            if (s[i].blk && (hi < 0 || s[i].h > s[hi].h)) hi = i;
        if (hi < 0 || std::fabs(s[hi].h - topH) > kWallUpSameH) return false;
        *b = s[hi].blk;
        *hh = s[hi].h;
        return true;
    };
    for (float in : {2.f, 1.5f, 1.f, kWallUpInset, kWallUpInset * 0.5f}) {
        uintptr_t b = 0, b2 = 0;
        float hh = 0.f, h2 = 0.f;
        if (!onTop(bodyD + in, &b, &hh)) continue;
        if (in > kWallUpInset && !onTop(bodyD + in + kWallUpKeep, &b2, &h2)) continue;
        land = bodyD + in;
        h = hh;
        blk = b;
        break;
    }
    out[0] = x + fx * land;
    out[1] = h;
    out[2] = z + fz * land;
    if (GatePassageNear(out[0], out[2], fx, fz, foot, h, cfg.wallUpGateR)) return no(kWhyGate);
    if (InsideBuilding(out[0], out[2], h)) return no(kWhyInside);
    if (RoofOver(out[0], out[2], h)) return no(kWhyRoof);
    *blkOut = blk;
    *why = kWhyNone;
    return true;
}
bool WallUpFrame(double now, bool fg, bool paused, bool pinned) {
    bool e = fg && KeyDown(static_cast<int>(cfg.wallUpKey));
    bool press = e && !S.ePrev;
    S.ePrev = e;
    if (!cfg.wallUp || !cfg.wall || !g_wallOk || S.air || pinned || paused || S.stuck || S.ghostWanted || S.flyer) {
        S.prompt = 0;
        S.wallUpOk = false;
        return false;
    }
    if (press || now - S.tWallScan >= 0.1) {
        S.tWallScan = now;
        bool was = S.wallUpOk;
        int why = kWhyNone;
        S.wallUpOk = FindWallUp(S.kinPos[0], S.kinPos[1], S.footY, std::sin(S.yaw), std::cos(S.yaw), S.wallUpTo, &S.wallUpBlk, &why);
        if (why >= kWhyNoTop && why != S.wallUpWhy && now - S.tWallWhy > 2.0 && S.wallWhyLogs < 60) {
            ++S.wallWhyLogs;
            S.tWallWhy = now;
            Log("wall-up: no prompt at %.2f,%.2f foot %.2f facing %.0f deg - %s", S.kinPos[0], S.kinPos[1], S.footY, S.yaw * 57.29578f,
                WallUpWhyText(why));
        }
        S.wallUpWhy = why;
        if (S.wallUpOk && !was && S.wallUpLogs++ < 40)
            Log("wall-up: prompt on - wall top %.2f,%.2f h %.2f (%.1f m above the foot %.2f) on block %p", S.wallUpTo[0], S.wallUpTo[2], S.wallUpTo[1],
                S.wallUpTo[1] - S.footY, S.footY, (void*)S.wallUpBlk);
    }
    S.prompt = S.wallUpOk ? 1 : 0;
    if (!press || !S.wallUpOk) return false;
    const float from[3] = {S.kinPos[0], S.footY, S.kinPos[1]};
    S.kinPos[0] = S.wallUpTo[0];
    S.kinPos[1] = S.wallUpTo[2];
    S.footY = S.wallUpTo[1];
    S.footBlk = S.wallUpBlk;
    S.kinVel[0] = S.kinVel[1] = 0;
    S.air = S.airFar = S.dropActive = false;
    S.vy = S.airGap = 0;
    S.tLastTp = 0;
    S.tLand = now;
    S.bldRecheck = true;
    S.bodyInside = false;
    S.wallUpOk = false;
    S.prompt = 0;
    ++S.wallUps;
    Log("wall-up: E -> onto the wall top at %.2f,%.2f h %.2f on block %p (from %.2f,%.2f foot %.2f, %.1f m up; %d this time)", S.kinPos[0],
        S.kinPos[1], S.footY, (void*)S.footBlk, from[0], from[2], from[1], S.footY - from[1], S.wallUps);
    return true;
}
constexpr float kLeadAdjMax = 0.12f;
bool LeashSkipAhead(float ex, float ez, const float vel[2], float ed, float leash) {
    float v = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1]);
    if (!(v > 1.f) || !(ed > 1e-4f)) return false;
    float along = -(ex * vel[0] + ez * vel[1]) / v;
    return along > 0.5f * ed && ed < leash + v * 0.5f + 2.f;
}
void Apply(uintptr_t mgr) {
    double now = Now();
    uintptr_t ent = EntityByPath(S.unit, S.entPath);
    float raw[3];
    if (ent != S.entity || !ReadVec(S.entity + oEntPos, raw) || Ptr(mgr + oMgrBattle) != S.battle) {
        Exit(mgr, "unit-invalid", false);
        return;
    }
    if (Ptr(S.fpCtrl + oFpCtrlEntity)) {
        Exit(mgr, "artillery-fp-started");
        return;
    }
    if (!FpCamOk(S.cam)) {
        Exit(mgr, "camera-invalid");
        return;
    }
    if (S.ghostWanted && !S.ghost && IssueUnitEnabled(false)) S.ghost = true;
    if (g_moveOk && !S.ghostWanted && !S.ghost && now - S.tAliveCheck >= 1.0) {
        S.tAliveCheck = now;
        SelBuf sel;
        unsigned n = MakeSelection(sel);
        FreeSelection(&sel);
        if (!n) {
            if (g_haltOk && (S.lastMode == 0 || S.lastMode == 2)) IssueOvrHalt();
            Exit(mgr, "hero-not-controllable", false);
            return;
        }
    }
    static double s_tRoutCheck = -1.0;
    if (g_routOk && (now - s_tRoutCheck >= 0.25 || now < s_tRoutCheck)) {
        s_tRoutCheck = now;
        int st = -1;
        if (HeroRouting(S.unit, &st)) {
            Log("hero: routing (morale state %d) - leaving first person, the hero is released to flee", st);
            Exit(mgr, "hero-routing");
            return;
        }
    }
    if (!S.havePos) {
        memcpy(S.outPos, raw, sizeof raw);
        memcpy(S.from, raw, sizeof raw);
        memcpy(S.to, raw, sizeof raw);
        memcpy(S.lastRaw, raw, sizeof raw);
        S.tChange = now;
        S.havePos = true;
    } else if (raw[0] != S.lastRaw[0] || raw[1] != S.lastRaw[1] || raw[2] != S.lastRaw[2]) {
        double interval = now - S.tChange;
        if (interval > 0.02 && interval < 0.5) S.tickDur = S.tickDur * 0.7 + interval * 0.3;
        memcpy(S.from, S.outPos, sizeof raw);
        memcpy(S.to, raw, sizeof raw);
        memcpy(S.lastRaw, raw, sizeof raw);
        S.tChange = now;
    }
    double k = (now - S.tChange) / (S.tickDur > 0.03 ? S.tickDur : 0.03);
    if (k > 1) k = 1;
    for (int i = 0; i < 3; ++i) S.outPos[i] = S.from[i] + static_cast<float>((S.to[i] - S.from[i]) * k);
    long dx = 0, dy = 0;
    if (Foreground()) {
        if (S.useRaw) {
            dx = InterlockedExchange(&g_rawDx, 0);
            dy = InterlockedExchange(&g_rawDy, 0);
        } else {
            POINT c = ClientCenterScreen(), p{};
            if (GetCursorPos(&p) && !S.skipFirstCursor) {
                dx = p.x - c.x;
                dy = p.y - c.y;
            }
            S.skipFirstCursor = false;
            SetCursorPos(c.x, c.y);
        }
        if (S.useRaw) {
            POINT c = ClientCenterScreen();
            SetCursorPos(c.x, c.y);
        }
    } else {
        InterlockedExchange(&g_rawDx, 0);
        InterlockedExchange(&g_rawDy, 0);
        S.skipFirstCursor = true;
    }
    float shieldW = 0.f, shieldMove = 1.f, shieldZoom = 1.f;
    vmgame::ShieldGame(&shieldW, &shieldMove, &shieldZoom);
    float zoomMax = cfg.adsZoom, zoomCurve = S.adsT * S.adsT * (3.f - 2.f * S.adsT);
    {
        float mf = 0.f, mfov = 0.f;
        if (vmgame::CurveAdsState(&mf, &mfov)) {
            zoomCurve = 0.5f - 0.5f * std::cos(kPi * mf);
            if (mfov > 0.f && S.baseFov > 0.f) {
                float hip = std::tan(S.baseFov * 0.5f);
                if (vmgame::CurveAdsFovScaling()) mfov = std::fmin(mfov * 2.f * std::atan(hip / 0.75f) * 180.f / kPi / 65.f, 170.f);
                zoomMax = std::fmax(1.f, hip / (0.75f * std::tan(mfov * kPi / 360.f)));
            }
        }
    }
    float zoomNow = 1.f + (zoomMax - 1.f) * shieldZoom * zoomCurve;
    float rad = cfg.sensitivity * kPi / 180.f / zoomNow;
    S.yaw += dx * rad;
    S.pitch += (cfg.invertY ? dy : -dy) * rad;
    const float lim = 85.f * kPi / 180.f;
    if (S.pitch > lim) S.pitch = lim;
    if (S.pitch < -lim) S.pitch = -lim;
    if (S.yaw > kPi) S.yaw -= 2 * kPi;
    if (S.yaw < -kPi) S.yaw += 2 * kPi;
    S.sumDx += dx;
    S.sumDy += dy;
    float sy = std::sin(S.yaw), cy = std::cos(S.yaw);
    int mode = MoveMode();
    float dt = static_cast<float>(now - S.tLastFrame);
    S.tLastFrame = now;
    if (!(dt > 0.f) || dt > 0.1f) dt = 0.f;
    if (S.lastMode != mode) {
        if (S.lastMode == 0 || S.lastMode == 2)
            ReleaseHero(S.lastMode, "mode-change");
        else if (S.lastMode == 1 && S.moving)
            IssueHalt();
        if (S.lastMode >= 0) Log("move: mode %s -> %s", ModeName(S.lastMode), ModeName(mode));
        S.moving = false;
        S.kinInit = false;
        S.lead[0] = S.lead[1] = 0;
        S.tMoveStart = S.tHalt = S.tWatch = -1;
        S.lastMode = mode;
    }
    bool canMove = cfg.moveEnabled && (mode != 1 || g_moveOk);
    bool fg = Foreground();
    float mx = 0, mz = 0;
    bool run = true;
    if (canMove && fg) {
        float fwdIn = (KeyDown('W') ? 1.f : 0.f) - (KeyDown('S') ? 1.f : 0.f);
        float sideIn = (KeyDown('D') ? 1.f : 0.f) - (KeyDown('A') ? 1.f : 0.f);
        mx = sy * fwdIn + cy * sideIn;
        mz = cy * fwdIn - sy * sideIn;
        float ml = std::sqrt(mx * mx + mz * mz);
        if (ml > 0.01f) {
            mx /= ml;
            mz /= ml;
        } else {
            mx = mz = 0;
        }
        run = true;
    }
    bool wantMove = mx != 0 || mz != 0;
    {
        CastMouse(fg && (GetAsyncKeyState(VK_LBUTTON) & 0x8000), fg && (GetAsyncKeyState(VK_RBUTTON) & 0x8000));
        bool rmb = g_cast.rmb;
        bool lmb = g_cast.lmb;
        bool shift = fg && KeyDown(VK_SHIFT);
        bool tap = shift && !S.shiftPrev;
        S.shiftPrev = shift;
        bool reloading = vmgame::Busy();
        bool canSprint = canMove && fg && mode == 0 && wantMove && KeyDown('W') && !KeyDown('S') && !rmb && !lmb && !reloading &&
                         !S.pinned && !S.stuck;
        bool wasSprint = S.sprint, wasTac = S.tac, wasAds = S.ads;
        if (S.sprint && !canSprint) {
            S.fireBlock = now + (S.tac ? cfg.tacSprintToFire : cfg.sprintToFire);
            S.exitTac = S.tac;
            S.sprint = S.tac = false;
        } else if (tap && canSprint) {
            if (!S.sprint)
                S.sprint = true;
            else if (!vmgame::ShieldWanted() && TacStaminaOk())
                S.tac = true;
        }
        TacStaminaStep(now, dt);
        S.ads = rmb && !S.sprint && !reloading && !vmgame::MeleeActive();
        if (S.ads && !wasAds) S.adsKind = now < S.fireBlock ? (S.exitTac ? 2 : 1) : 0;
        if (!S.ads) S.adsKind = 0;
        float adsTime = vmgame::CurrentAdsTime(S.adsKind);
        if (!(adsTime > 0.f)) adsTime = S.adsKind == 2 ? cfg.adsTimeTac : S.adsKind == 1 ? cfg.adsTimeSprint : cfg.adsTime;
        float step = dt > 0.f ? dt / adsTime : 0.f;
        S.adsT += S.ads ? step : -step;
        if (S.adsT < 0.f) S.adsT = 0.f;
        if (S.adsT > 1.f) S.adsT = 1.f;
        {
            float mf = 0.f, mfov = 0.f;
            if (vmgame::CurveAdsState(&mf, &mfov)) S.adsT = mf;
        }
        if ((wasSprint != S.sprint || wasTac != S.tac || wasAds != S.ads) && S.stanceLogs++ < 80)
            Log("stance: %s%s (fire block %.2f s)", S.tac ? "tactical sprint" : S.sprint ? "sprint" : S.ads ? "ads" : "hip",
                lmb ? " [fire held]" : "", S.fireBlock > now ? S.fireBlock - now : 0.0);
        vmgame::PublishStance(S.ads, S.sprint, S.tac, S.adsKind);
    }
    float camPos[3];
    if (canMove && mode == 0) {
        alignas(16) float dest[8];
        if (!S.kinInit) {
            S.kinPos[0] = raw[0];
            S.kinPos[1] = raw[2];
            S.kinVel[0] = S.kinVel[1] = 0;
            S.kinY = raw[1];
            S.kinInit = true;
            S.footY = raw[1];
            S.footBlk = 0;
            S.air = S.airFar = S.dropActive = false;
            S.vy = S.airGap = S.landY = S.landV = S.landPunch = 0;
            S.tLand = -1;
            S.prompt = 0, S.wallUpOk = false, S.ePrev = true, S.wallUps = 0, S.wallUpWhy = 0, S.wallWhyLogs = 0;
            for (float& a : g_leadAdj) a = std::fmin(a, kLeadAdjMax);
            {
                Surf sf[kMaxSurf];
                int ns = SurfacesAt(raw[0], raw[2], sf), bi = -1;
                for (int i = 0; i < ns; ++i)
                    if (bi < 0 || std::fabs(sf[i].h - raw[1]) < std::fabs(sf[bi].h - raw[1])) bi = i;
                if (bi >= 0 && std::fabs(sf[bi].h - raw[1]) < 3.f) S.footY = sf[bi].h, S.footBlk = sf[bi].blk;
                if (S.wallLogs++ < 80)
                    Log("wall: enter at %.2f,%.2f,%.2f on %s %p (h %.2f, %d layer(s) here%s)", raw[0], raw[1], raw[2],
                        S.footBlk ? "block" : "terrain", (void*)S.footBlk, S.footY, ns, g_wallOk && cfg.wall ? "" : ", nav blocks OFF");
                S.bldBlocks = 0;
                S.bldRecheck = false;
                S.bldInside = cfg.building && g_buildOk && InsideBuilding(raw[0], raw[2], S.footY);
                S.tBldCheck = now;
                if (S.bldInside && S.bldLogs++ < 30) Log("wall: entered inside a building - its walls let through until out");
                S.bodyInside = cfg.wallBody && cfg.wall && g_wallOk && InsideWallBody(raw[0], raw[2], S.footY);
                if (S.bodyInside && S.bodyLogs++ < 20) Log("wall: entered inside a wall body / tower footprint - let through until out");
                if (S.censusLogs++ < 6) CollisionCensus("enter", raw[0], raw[2], S.footY);
            }
            if (MakeDestAt(raw[0], raw[2], dest, S.footBlk) && IssueTeleport(dest)) {
                ++S.tpSent;
                RecordTp(now, dest[0], dest[2]);
            } else {
                ++S.tpFailed;
            }
            S.lastTp[0] = raw[0];
            S.lastTp[1] = raw[2];
            S.tLastTp = now;
        }
        int32_t act = 0;
        Rd(S.entity + oEntActivity, act);
        if (act != S.lastAct) {
            if (S.actLogs++ < 60) {
                const char* nm = act == 0 ? "idle" : act == 1 ? "fight" : act == 14 ? "knocked back" : act == 15 ? "knocked down"
                               : act == 16 ? "knocked flying" : act == 18 ? "hit reaction / parry" : "?";
                if (act == 18) {
                    uintptr_t o = S.entity + 0x330;
                    uintptr_t vt = Ptr(o), attacker = Ptr(o + 0x38);
                    uint8_t flag = 0xff;
                    float v[3] = {0, 0, 0};
                    Rd(o + 0x4c, flag);
                    Rd(o + 0x40, v);
                    Log("hero: activity %d -> %d (%s) state vt=+%llx attacker=%p flag=%u vel=%.1f,%.1f,%.1f%s", S.lastAct, act, nm,
                        (unsigned long long)(vt > g_base ? vt - g_base : 0), (void*)attacker, flag, v[0], v[1], v[2],
                        flag == 0 && !attacker ? " <- projectile without shooter (ours?)" : "");
                } else {
                    Log("hero: activity %d -> %d (%s)", S.lastAct, act, nm);
                }
            }
            S.lastAct = act;
        }
        bool pinned = !S.ghostWanted && act != 0;
        if (pinned != S.pinned) {
            if (pinned) {
                S.tPinned = now;
                S.pinAct = act;
                if (S.pinLogs++ < 40) Log("move(teleport): hero engaged (activity %d) - camera locked onto it", act);
            } else if (S.pinLogs++ < 40) {
                Log("move(teleport): hero free again after %.1f s (activity was %d)", now - S.tPinned, S.pinAct);
            }
            S.pinned = pinned;
        }
        bool judged = false, follow = false;
        bool unbound = S.ghostWanted || S.airFar || S.air || (S.tLand > 0 && now - S.tLand < 0.5);
        if (unbound || pinned) judged = true, follow = true;
        for (const TpRec& r : S.tpHist) {
            if (unbound || pinned) break;
            if (r.t <= 0 || now - r.t > 0.45) continue;
            float fx = raw[0] - r.x, fz = raw[2] - r.z;
            if (fx * fx + fz * fz < 0.16f) follow = true;
            if (now - r.t >= 0.25) judged = true;
        }
        if (!judged) follow = true;
        if (pinned) {
            S.stuck = false;
            S.tNoFollow = -1;
        } else if (S.stuck) {
            float sx = raw[0] - S.stuckPos[0], sz = raw[2] - S.stuckPos[2];
            if (sx * sx + sz * sz > 0.3f * 0.3f) {
                S.stuck = false;
                S.tNoFollow = -1;
                Log("move(teleport): hero follows again after %.1f s (activity %d)", now - S.tStuck, act);
            }
        } else if (follow) {
            S.tNoFollow = -1;
        } else if (S.tNoFollow < 0) {
            S.tNoFollow = now;
        } else if (now - S.tNoFollow >= 0.25) {
            S.stuck = true;
            S.tStuck = now;
            memcpy(S.stuckPos, raw, sizeof raw);
            S.kinVel[0] = S.kinVel[1] = 0;
            Log("move(teleport): hero stopped following teleports (activity %d) at %.2f,%.2f; camera pulled back onto it", act, raw[0],
                raw[2]);
        }
        bool paused = CmdQueue(nullptr) == 0;
        float spd = S.tac ? cfg.tacSprintSpeed
                    : S.sprint ? cfg.sprintSpeed : cfg.runSpeed * (1.f - (1.f - cfg.adsMoveScale) * S.adsT);
        spd *= shieldMove;
        (void)run;
        bool space = fg && KeyDown(VK_SPACE);
        if (space && !S.spacePrev && !S.air && !pinned && !S.stuck && !paused && cfg.jumpHeight > 0.f) {
            S.air = true;
            S.vy = std::sqrt(2.f * cfg.gravity * cfg.jumpHeight);
            S.airStartY = S.footY;
            S.airGap = 0;
            S.tAir = now;
            ++S.jumps;
            vmgame::MoveImpulse(1.f, 0.f);
            if (S.wallLogs++ < 80)
                Log("wall: jump at %.2f,%.2f foot %.2f on %s, up %.2f m/s", S.kinPos[0], S.kinPos[1], S.footY, S.footBlk ? "block" : "terrain",
                    S.vy);
        }
        S.spacePrev = space;
        if (WallUpFrame(now, fg, paused, pinned)) unbound = true;
        if (pinned && S.air) {
            S.air = S.airFar = S.dropActive = false;
            S.vy = 0;
        }
        const float prevKin[2] = {S.kinPos[0], S.kinPos[1]};
        if (pinned) {
            float kp = dt > 0.f ? 1.f - std::exp(-dt / 0.08f) : 1.f;
            S.kinPos[0] += (S.outPos[0] - S.kinPos[0]) * kp;
            S.kinPos[1] += (S.outPos[2] - S.kinPos[1]) * kp;
            S.kinVel[0] = S.kinVel[1] = 0;
        } else if (dt > 0.f && !paused) {
            float tau = S.air ? cfg.airControl : cfg.accelTau;
            float a = tau > 0.001f ? 1.f - std::exp(-dt / tau) : 1.f;
            S.kinVel[0] += (mx * spd - S.kinVel[0]) * a;
            S.kinVel[1] += (mz * spd - S.kinVel[1]) * a;
            if (!wantMove && S.kinVel[0] * S.kinVel[0] + S.kinVel[1] * S.kinVel[1] < 0.01f) S.kinVel[0] = S.kinVel[1] = 0;
            S.kinPos[0] += S.kinVel[0] * dt;
            S.kinPos[1] += S.kinVel[1] * dt;
        } else if (paused) {
            S.kinVel[0] = S.kinVel[1] = 0;
        }
        if (S.dropActive && dt > 0.f && !paused && !pinned)
            S.dropActive = !DropMinProgress(S.kinPos, S.dropP0, S.dropDir, S.dropTarget, S.dropSpeed, static_cast<float>(now - S.tAir));
        if (S.air && S.dropPolyN && dt > 0.f && !paused && !pinned) {
            float nrm[2] = {0, 0};
            float need = std::fmin(S.dropGap, S.dropSpeed * static_cast<float>(now - S.tAir));
            if (PolyKeepOut(S.dropPoly, S.dropPolyN, S.kinPos, need, nrm) > 1e-3f) {
                float vn = S.kinVel[0] * nrm[0] + S.kinVel[1] * nrm[1];
                if (vn < 0.f) {
                    S.kinVel[0] -= vn * nrm[0];
                    S.kinVel[1] -= vn * nrm[1];
                }
                ++S.dropKept;
            }
        }
        if (cfg.building && g_buildOk && !pinned && !paused && dt > 0.f && !S.flyer) {
            if (S.bldRecheck || (S.bldInside && now - S.tBldCheck > 0.5)) {
                bool was = S.bldInside;
                S.bldInside = InsideBuilding(S.kinPos[0], S.kinPos[1], S.footY);
                S.tBldCheck = now;
                S.bldRecheck = false;
                if (was != S.bldInside && S.bldLogs++ < 30)
                    Log("wall: %s a building at %.2f,%.2f (foot %.2f) - its walls %s", S.bldInside ? "landed inside" : "out of",
                        S.kinPos[0], S.kinPos[1], S.footY, S.bldInside ? "let through until out" : "solid again");
            }
            uint32_t ht = 0;
            float hp[3] = {};
            if (!S.bldInside && TallWallBetween(prevKin, S.kinPos, S.footY, &ht, hp)) {
                const float tryPos[2][2] = {{S.kinPos[0], prevKin[1]}, {prevKin[0], S.kinPos[1]}};
                bool slid = false;
                for (int k = 0; k < 2 && !slid; ++k) {
                    if (tryPos[k][0] == prevKin[0] && tryPos[k][1] == prevKin[1]) continue;
                    uint32_t t2 = 0;
                    float h2[3] = {};
                    if (!TallWallBetween(prevKin, tryPos[k], S.footY, &t2, h2)) {
                        S.kinPos[0] = tryPos[k][0];
                        S.kinPos[1] = tryPos[k][1];
                        S.kinVel[k == 0 ? 1 : 0] = 0;
                        slid = true;
                    }
                }
                if (!slid) {
                    S.kinPos[0] = prevKin[0];
                    S.kinPos[1] = prevKin[1];
                    S.kinVel[0] = S.kinVel[1] = 0;
                }
                ++S.bldBlocks;
                if (S.bldLogs++ < 30)
                    Log("wall: building wall (type 0x%x) hit at %.2f,%.2f,%.2f going from %.2f,%.2f (foot %.2f%s) - %s", ht, hp[0], hp[1], hp[2],
                        prevKin[0], prevKin[1], S.footY, S.air ? ", in the air" : "", slid ? "slid along one axis" : "held");
            }
        }
        float leash = S.stuck ? cfg.stuckLeash : cfg.leash;
        float ex = S.kinPos[0] - raw[0], ez = S.kinPos[1] - raw[2];
        float ed = std::sqrt(ex * ex + ez * ez);
        const bool skipAhead = !S.stuck && ed > leash && LeashSkipAhead(ex, ez, S.kinVel, ed, leash);
        if (skipAhead && S.leashAhead++ < 5)
            Log("move(teleport): hero %.1f m ahead of the camera (teleport lead) - leash not pulling the camera forward", ed);
        if (ed > leash && !unbound && !pinned && !skipAhead) {
            float k2 = dt > 0.f && S.stuck ? 1.f - std::exp(-dt / 0.1f) : 1.f;
            S.kinPos[0] -= ex * (1.f - leash / ed) * k2;
            S.kinPos[1] -= ez * (1.f - leash / ed) * k2;
            ++S.leashHits;
            S.tLeashHit = now;
        }
        Surf sf[kMaxSurf];
        int ns = SurfacesAt(S.kinPos[0], S.kinPos[1], sf), si = -1;
        if (ns && pinned) {
            for (int i = 0; i < ns; ++i)
                if (si < 0 || std::fabs(sf[i].h - S.outPos[1]) < std::fabs(sf[si].h - S.outPos[1])) si = i;
        } else if (ns && !S.air) {
            bool edge = false;
            si = WalkPick(sf, ns, S.footBlk, S.footY, cfg.stepUp, cfg.stepDown, &edge);
            if (si < 0) {
                const float tryPos[2][2] = {{S.kinPos[0], prevKin[1]}, {prevKin[0], S.kinPos[1]}};
                bool slid = false;
                for (int t = 0; t < 2 && !slid; ++t) {
                    if (tryPos[t][0] == prevKin[0] && tryPos[t][1] == prevKin[1]) continue;
                    Surf s2[kMaxSurf];
                    bool e2 = false;
                    int n2 = SurfacesAt(tryPos[t][0], tryPos[t][1], s2);
                    int i2 = n2 ? WalkPick(s2, n2, S.footBlk, S.footY, cfg.stepUp, cfg.stepDown, &e2) : -1;
                    if (i2 >= 0) {
                        S.kinPos[0] = tryPos[t][0];
                        S.kinPos[1] = tryPos[t][1];
                        S.kinVel[t == 0 ? 1 : 0] = 0;
                        memcpy(sf, s2, sizeof sf);
                        ns = n2;
                        si = i2;
                        edge = e2;
                        slid = true;
                    }
                }
                if (!slid) {
                    S.kinPos[0] = prevKin[0];
                    S.kinPos[1] = prevKin[1];
                    S.kinVel[0] = S.kinVel[1] = 0;
                    ns = SurfacesAt(S.kinPos[0], S.kinPos[1], sf);
                    si = ns ? WalkPick(sf, ns, S.footBlk, S.footY, cfg.stepUp, cfg.stepDown, &edge) : -1;
                }
                if (S.blocks++ < 20) Log("wall: blocked at %.2f,%.2f (foot %.2f, nothing within step_up %.2f) - %s", prevKin[0], prevKin[1],
                                         S.footY, cfg.stepUp, slid ? "slid along one axis" : "held");
            }
            if (si >= 0 && !edge && !sf[si].blk && cfg.wallBody && cfg.wall && g_wallOk) {
                float bx = S.kinPos[0] - prevKin[0], bz = S.kinPos[1] - prevKin[1], bl = std::sqrt(bx * bx + bz * bz);
                if (S.bodyInside) {
                    if (!InsideStructure(S.kinPos[0], S.kinPos[1], S.footY)) {
                        S.bodyInside = false;
                        if (S.bodyLogs++ < 20) Log("wall: out of the wall body / tower footprint at %.2f,%.2f - solid again", S.kinPos[0], S.kinPos[1]);
                    }
                } else if (bl > 1e-4f) {
                    RegionNet net;
                    const bool netOk = cfg.wallBodyNet && g_netOk && RegionNetNow(&net);
                    int netWhy = 0;
                    uintptr_t netHit = 0;
                    auto wallAt = [&](float px, float pz, float up) {
                        if (!InsideStructure(px, pz, S.footY, up)) return false;
                        if (!netOk) return true;
                        uintptr_t hit = 0;
                        const int v = NetStep(net, prevKin[0], prevKin[1], px, pz, &hit);
                        if (v != 0) {
                            netWhy = v, netHit = hit;
                            return true;
                        }
                        ++S.netPass;
                        if (now - S.tNetLog >= 1.0 && S.netLogs < 60) {
                            uint32_t idx = 0;
                            Rd(hit + oRegIdx, idx);
                            ++S.netLogs;
                            Log("wall: walkway overhead but the game's ground net lets it through at %.2f,%.2f (region #%u, %u step(s) since last line) - arch / open passage",
                                px, pz, idx, S.netPass);
                            S.tNetLog = now, S.netPass = 0;
                        }
                        return false;
                    };
                    auto bodyAt = [&](float px, float pz, float ux, float uz) {
                        return wallAt(px, pz, -1.f) ||
                               (cfg.wallBodyMargin > 0.f && wallAt(px + ux * cfg.wallBodyMargin, pz + uz * cfg.wallBodyMargin, 2.f));
                    };
                    if (bodyAt(S.kinPos[0], S.kinPos[1], bx / bl, bz / bl)) {
                        const float tryPos[2][2] = {{S.kinPos[0], prevKin[1]}, {prevKin[0], S.kinPos[1]}};
                        const float tryDir[2][2] = {{bx > 0.f ? 1.f : -1.f, 0.f}, {0.f, bz > 0.f ? 1.f : -1.f}};
                        bool slid = false;
                        for (int t = 0; t < 2 && !slid; ++t) {
                            if (tryPos[t][0] == prevKin[0] && tryPos[t][1] == prevKin[1]) continue;
                            Surf s2[kMaxSurf];
                            bool e2 = false;
                            int n2 = SurfacesAt(tryPos[t][0], tryPos[t][1], s2);
                            int i2 = n2 ? WalkPick(s2, n2, S.footBlk, S.footY, cfg.stepUp, cfg.stepDown, &e2) : -1;
                            if (i2 >= 0 && !e2 && (s2[i2].blk || !bodyAt(tryPos[t][0], tryPos[t][1], tryDir[t][0], tryDir[t][1]))) {
                                S.kinPos[0] = tryPos[t][0];
                                S.kinPos[1] = tryPos[t][1];
                                S.kinVel[t == 0 ? 1 : 0] = 0;
                                memcpy(sf, s2, sizeof sf);
                                ns = n2;
                                si = i2;
                                slid = true;
                            }
                        }
                        if (!slid) {
                            S.kinPos[0] = prevKin[0];
                            S.kinPos[1] = prevKin[1];
                            S.kinVel[0] = S.kinVel[1] = 0;
                            ns = SurfacesAt(S.kinPos[0], S.kinPos[1], sf);
                            si = ns ? WalkPick(sf, ns, S.footBlk, S.footY, cfg.stepUp, cfg.stepDown, &edge) : -1;
                        }
                        if (S.bodyLogs++ < 20) {
                            uint32_t idx = 0;
                            if (netHit) Rd(netHit + oRegIdx, idx);
                            Log("wall: wall body / tower footprint (nav block overhead, ground under it; ground net: %s #%u) at %.2f,%.2f going from %.2f,%.2f "
                                "(foot %.2f) - %s",
                                NetWhyText(netOk, netWhy), idx, S.kinPos[0], S.kinPos[1], prevKin[0], prevKin[1], S.footY, slid ? "slid along one axis" : "held");
                        }
                        if (S.bodyLogs == 1 && S.censusLogs++ < 6) CollisionCensus("first wall body block", prevKin[0], prevKin[1], S.footY);
                    }
                }
            }
            if (si >= 0 && edge) {
                float dx = S.kinPos[0] - prevKin[0], dz = S.kinPos[1] - prevKin[1], dl = std::sqrt(dx * dx + dz * dz);
                if (dl < 1e-4f) dx = S.kinVel[0], dz = S.kinVel[1], dl = std::sqrt(dx * dx + dz * dz);
                if (dl < 1e-4f) dx = sy, dz = cy, dl = 1.f;
                dx /= dl;
                dz /= dl;
                float drop0 = S.footY - sf[si].h, gap = std::fmin(cfg.dropClearance, 0.5f + 0.3f * drop0), landH = sf[si].h;
                float v0 = std::sqrt(S.kinVel[0] * S.kinVel[0] + S.kinVel[1] * S.kinVel[1]);
                float range = std::fmax(cfg.dropMaxPush, v0 * std::sqrt(2.f * std::fmax(drop0, 0.1f) / cfg.gravity));
                float push = cfg.dropClearance <= 0.f ? 0.f
                           : FindDropPush(S.kinPos[0], S.kinPos[1], dx, dz, S.footY, cfg.stepDown, range,
                                          [&](float px, float pz) { return BlockNear(S.footBlk, px, pz, gap); },
                                          [&](float px, float pz, Surf* o) { return SurfacesAt(px, pz, o); }, &landH);
                bool clear = push >= 0.f;
                if (clear && cfg.building && g_buildOk && !S.bldInside) {
                    float ahead = std::fmax(push, 1.5f) + 0.5f;
                    const float q2[2] = {S.kinPos[0] + dx * ahead, S.kinPos[1] + dz * ahead};
                    uint32_t t3 = 0;
                    float h3[3] = {};
                    if (TallWallBetween(S.kinPos, q2, S.footY, &t3, h3)) {
                        clear = false;
                        if (S.blocks++ < 20)
                            Log("wall: edge at %.2f,%.2f (foot %.2f) runs into a tall building wall (type 0x%x) at %.2f,%.2f - treated as a wall, not a drop",
                                S.kinPos[0], S.kinPos[1], S.footY, t3, h3[0], h3[2]);
                    }
                }
                if (!clear) {
                    if (S.blocks++ < 20)
                        Log("wall: edge at %.2f,%.2f (foot %.2f, drop %.1f m) but no clear landing within %.1f m (wall / tower below) - held",
                            S.kinPos[0], S.kinPos[1], S.footY, drop0, range);
                    S.kinPos[0] = prevKin[0];
                    S.kinPos[1] = prevKin[1];
                    S.kinVel[0] = S.kinVel[1] = 0;
                    si = -1;
                } else {
                    S.air = true;
                    S.vy = 0;
                    S.airStartY = S.footY;
                    S.tAir = now;
                    ++S.drops;
                    float tFall = std::sqrt(2.f * std::fmax(S.footY - landH, 0.1f) / cfg.gravity);
                    S.dropActive = push > 0.01f;
                    S.dropP0[0] = S.kinPos[0];
                    S.dropP0[1] = S.kinPos[1];
                    S.dropDir[0] = dx;
                    S.dropDir[1] = dz;
                    S.dropS = 0;
                    S.dropTarget = push;
                    S.dropSpeed = std::fmin(20.f, std::fmax(cfg.dropPushSpeed, push / (0.8f * tFall)));
                    S.dropPolyN = S.footBlk && push > 0.01f ? CopyBlockPoly(S.footBlk, S.dropPoly, 16) : 0;
                    S.dropGap = gap;
                    S.dropKept = 0;
                    if (S.wallLogs++ < 80)
                        Log("wall: walked off the edge at %.2f,%.2f foot %.2f (%s) at %.1f m/s -> clear ground %.2f m out (clearance %.2f, below %.2f), "
                            "min push %.1f m/s", S.kinPos[0], S.kinPos[1], S.footY, S.footBlk ? "block" : "terrain", v0, push, gap, landH, S.dropSpeed);
                }
            }
        }
        if (S.air && !S.dropActive && ns) {
            auto covered = [&](const Surf* s, int n) {
                for (int j = 0; j < n; ++j)
                    if (s[j].h > S.footY + 0.3f) return true;
                return false;
            };
            if (covered(sf, ns)) {
                const float tryPos[2][2] = {{S.kinPos[0], prevKin[1]}, {prevKin[0], S.kinPos[1]}};
                bool slid = false;
                for (int k = 0; k < 2 && !slid; ++k) {
                    if (tryPos[k][0] == prevKin[0] && tryPos[k][1] == prevKin[1]) continue;
                    Surf s5[kMaxSurf];
                    int n5 = SurfacesAt(tryPos[k][0], tryPos[k][1], s5);
                    if (n5 && !covered(s5, n5)) {
                        S.kinPos[0] = tryPos[k][0];
                        S.kinPos[1] = tryPos[k][1];
                        S.kinVel[k == 0 ? 1 : 0] = 0;
                        memcpy(sf, s5, sizeof sf);
                        ns = n5;
                        slid = true;
                    }
                }
                if (!slid) {
                    S.kinPos[0] = prevKin[0];
                    S.kinPos[1] = prevKin[1];
                    S.kinVel[0] = S.kinVel[1] = 0;
                    ns = SurfacesAt(S.kinPos[0], S.kinPos[1], sf);
                }
                if (S.blocks++ < 20) Log("wall: in the air, ran into a structure at %.2f,%.2f (foot %.2f) - %s", prevKin[0], prevKin[1], S.footY,
                                         slid ? "slid along one axis" : "held");
            }
        }
        if (S.air) {
            float prev = S.footY;
            if (dt > 0.f && !paused) {
                S.vy -= cfg.gravity * dt;
                S.footY += S.vy * dt;
            }
            si = ns ? PickSurface(sf, ns, kNoSurf, prev, 0.05f) : -1;
            if (si < 0)
                for (int i = 0; i < ns; ++i)
                    if (si < 0 || sf[i].h < sf[si].h) si = i;
            if (si >= 0) S.airGap = S.footY - sf[si].h;
            bool timeout = now - S.tAir > 8.0;
            if (si >= 0 && (S.footY <= sf[si].h || timeout)) {
                float impact = -S.vy, s = (impact - 3.f) / 15.f;
                s = s < 0.f ? 0.f : s > 1.f ? 1.f : s;
                S.footY = sf[si].h;
                S.footBlk = sf[si].blk;
                S.air = false;
                S.dropActive = false;
                S.vy = S.airGap = 0;
                S.tLastTp = 0;
                S.tLand = now;
                S.bldRecheck = true;
                S.bodyInside = cfg.wallBody && cfg.wall && g_wallOk && InsideWallBody(S.kinPos[0], S.kinPos[1], S.footY);
                ++S.lands;
                S.landV += kick::Impulse(-cfg.landDip * (0.25f + 0.75f * s), 5.f, 0.55f);
                S.landPunch -= 0.4f + 2.0f * s;
                vmgame::MoveImpulse(0.f, s > 0.05f ? s : 0.05f);
                if (S.wallLogs++ < 80)
                    Log("wall: landed on %s %p at %.2f,%.2f h %.2f after %.2f s (from %.2f, impact %.1f m/s, strength %.2f), %.1f m from the edge, "
                        "%.1f m out from the wall top (kept off it %d frames)%s", S.footBlk ? "block" : "terrain", (void*)S.footBlk, S.kinPos[0],
                        S.kinPos[1], S.footY, now - S.tAir, S.airStartY, impact, s, std::hypot(S.kinPos[0] - S.dropP0[0], S.kinPos[1] - S.dropP0[1]),
                        S.dropPolyN ? PolyDist(S.dropPoly, S.dropPolyN, S.kinPos[0], S.kinPos[1]) : -1.f, S.dropKept, timeout ? " [timeout]" : "");
            }
        } else if (si >= 0) {
            if (sf[si].blk != S.footBlk && S.wallLogs++ < 80)
                Log("wall: now on %s %p h %.2f (was %s %p h %.2f) at %.2f,%.2f", sf[si].blk ? "block" : "terrain", (void*)sf[si].blk, sf[si].h,
                    S.footBlk ? "block" : "terrain", (void*)S.footBlk, S.footY, S.kinPos[0], S.kinPos[1]);
            S.footY = sf[si].h;
            S.footBlk = sf[si].blk;
        }
        S.airFar = S.air && S.airGap > cfg.airTpMax;
        if (S.footBlk && !S.air && !pinned && std::fabs(raw[1] - S.footY) > 1.5f && now - S.tWallCheck > 1.0 && S.wallLogs < 80) {
            S.tWallCheck = now;
            ++S.wallLogs;
            Log("wall: hero entity y %.2f but camera foot %.2f on block %p at %.2f,%.2f (hero not on the block?)", raw[1], S.footY,
                (void*)S.footBlk, raw[0], raw[2]);
        }
        uintptr_t hint = S.air ? (si >= 0 ? sf[si].blk : 0) : S.footBlk;
        bool haveDest = MakeDestAt(S.kinPos[0], S.kinPos[1], dest, hint);
        if (S.air)
            S.kinY = S.footY;
        else
            S.kinY += (S.footY - S.kinY) * (dt > 0.f ? 1.f - std::exp(-dt / 0.05f) : 1.f);
        {
            static const float kLandSpring[2] = {5.f, 0.55f};
            kick::StepSprings(&S.landY, &S.landV, 1, kLandSpring, dt);
        }
        camPos[0] = S.kinPos[0];
        camPos[1] = S.kinY + S.landY;
        camPos[2] = S.kinPos[1];
        {
            unsigned sig = static_cast<unsigned>(S.bldBlocks) + S.blocks + static_cast<unsigned>(S.bodyLogs);
            if (sig != S.bumpSig) S.bumpSig = sig, S.tBump = now;
        }
        if (cfg.leadAuto && cfg.tpLead > 0.f && dt > 0.f && !S.stuck && !paused && !unbound && !pinned && !S.air && now - S.tGuard > 0.5 &&
            (S.tLand < 0 || now - S.tLand > 0.5) && now - S.tLeashHit > 1.0 && now - S.tBump > 0.5 &&
            std::fabs(S.outPos[1] - S.footY) < 0.8f) {
            float v = std::sqrt(S.kinVel[0] * S.kinVel[0] + S.kinVel[1] * S.kinVel[1]);
            float tvx = mx * spd, tvz = mz * spd, tv = std::sqrt(tvx * tvx + tvz * tvz);
            if (v > 1.5f && tv > 1.5f && std::fabs(v - tv) < 0.12f * tv) {
                float d = ((S.outPos[0] - S.kinPos[0]) * S.kinVel[0] + (S.outPos[2] - S.kinPos[1]) * S.kinVel[1]) / v;
                int b = LeadBand(tv);
                g_leadAdj[b] = LeadAutoStep(g_leadAdj[b], d, v, dt, cfg.leadAutoTau, -cfg.tpLead, kLeadAdjMax);
                S.leadOffSum += d;
                ++S.leadOffN;
                if (now - S.tLeadLog > 5.0 && S.leadOffN > 30) {
                    Log("ghost: hero %+.2f m ahead of the camera on average at %.1f m/s (band %d) -> lead %.3f s", S.leadOffSum / S.leadOffN, tv, b,
                        cfg.tpLead + g_leadAdj[b]);
                    S.tLeadLog = now;
                    S.leadOffSum = 0;
                    S.leadOffN = 0;
                }
            }
        }
        S.leadPos[0] = S.kinPos[0];
        S.leadPos[1] = S.kinPos[1];
        if (!S.stuck && !paused && !unbound && !pinned && !S.air && cfg.tpLead > 0.f) {
            float L = cfg.tpLead, tau = cfg.accelTau;
            float vtx = mx * spd, vtz = mz * spd;
            if (cfg.leadAuto) L = std::fmin(cfg.tpLead + kLeadAdjMax, std::fmax(0.f, cfg.tpLead + g_leadAdj[LeadBand(std::sqrt(vtx * vtx + vtz * vtz))]));
            float f = tau > 0.001f ? tau * (1.f - std::exp(-L / tau)) : 0.f;
            S.leadPos[0] += vtx * L + (S.kinVel[0] - vtx) * f;
            S.leadPos[1] += vtz * L + (S.kinVel[1] - vtz) * f;
        }
        float tx = S.leadPos[0] - S.lastTp[0], tz = S.leadPos[1] - S.lastTp[1];
        if (haveDest && !S.airFar && tx * tx + tz * tz > 0.02f * 0.02f && now - S.tLastTp >= cfg.tpInterval) {
            float gx = S.leadPos[0], gz = S.leadPos[1];
            bool skipTp = false;
            if (cfg.tpShotGuard > 0.f && !S.ghost && !cfg.fireShooter) {
                float gap0 = ShotGap(gx, gz, now);
                if (gap0 < cfg.tpShotGuard) {
                    bool found = false;
                    float k = 0.9f, gap1 = gap0;
                    for (; k > -0.05f; k -= 0.1f) {
                        float px = S.kinPos[0] + (S.leadPos[0] - S.kinPos[0]) * k;
                        float pz = S.kinPos[1] + (S.leadPos[1] - S.kinPos[1]) * k;
                        gap1 = ShotGap(px, pz, now);
                        if (gap1 >= cfg.tpShotGuard) {
                            gx = px;
                            gz = pz;
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        ++S.tpGuarded;
                        S.tGuard = now;
                        if (S.guardLogs++ < 12)
                            Log("move(teleport): guard - target %.2f m from a fresh bullet path, pulled back to %.0f%% of the lead "
                                "(gap %.2f m)", gap0, (k < 0.f ? 0.f : k) * 100.f, gap1);
                    } else {
                        skipTp = true;
                        ++S.tpGuardSkipped;
                        S.tGuard = now;
                        if (S.guardLogs++ < 12)
                            Log("move(teleport): guard - even the camera spot is %.2f m from a fresh bullet path; teleport held", gap1);
                    }
                }
            }
            if (!skipTp) {
                alignas(16) float td[8];
                bool haveTd = true;
                if (gx == S.kinPos[0] && gz == S.kinPos[1]) {
                    memcpy(td, dest, sizeof td);
                } else {
                    Surf s3[kMaxSurf];
                    bool e3 = false;
                    int n3 = SurfacesAt(gx, gz, s3), i3 = n3 ? WalkPick(s3, n3, S.footBlk, S.footY, cfg.stepUp, cfg.stepDown, &e3) : -1;
                    if (i3 >= 0 && !e3) {
                        haveTd = MakeDestAt(gx, gz, td, s3[i3].blk);
                    } else {
                        memcpy(td, dest, sizeof td);
                        gx = S.kinPos[0];
                        gz = S.kinPos[1];
                    }
                }
                if (haveTd && IssueTeleport(td)) {
                    ++S.tpSent;
                    RecordTp(now, td[0], td[2]);
                } else {
                    ++S.tpFailed;
                }
                S.lastTp[0] = gx;
                S.lastTp[1] = gz;
                S.tLastTp = now;
            }
        }
        S.camSpeed = std::sqrt(S.kinVel[0] * S.kinVel[0] + S.kinVel[1] * S.kinVel[1]);
        S.moving = wantMove;
    } else {
        float vx = 0, vz = 0;
        if (S.havePrev && dt > 0.f) {
            vx = (S.outPos[0] - S.prevOut[0]) / dt;
            vz = (S.outPos[2] - S.prevOut[2]) / dt;
        }
        memcpy(S.prevOut, S.outPos, sizeof S.prevOut);
        S.havePrev = true;
        S.entVel[0] = vx;
        S.entVel[1] = vz;
        float vlen = std::sqrt(vx * vx + vz * vz);
        if (wantMove && S.moving && dt > 0.f && now - S.tMoveBegan > 0.8 && vlen > 0.5f * S.speedEst &&
            vlen < 3.f * S.speedEst)
            S.speedEst += (vlen - S.speedEst) * (1.f - std::exp(-dt / 1.0f));
        if (cfg.predict && !S.flyer && dt > 0.f) {
            S.lead[0] += (mx * S.speedEst - vx) * dt;
            S.lead[1] += (mz * S.speedEst - vz) * dt;
            bool atRest = !wantMove && vlen < 0.3f;
            float decay = std::exp(-dt / (atRest ? cfg.predictTau : 3.0f));
            S.lead[0] *= decay;
            S.lead[1] *= decay;
            float ll = std::sqrt(S.lead[0] * S.lead[0] + S.lead[1] * S.lead[1]);
            if (ll > cfg.predictMax) {
                S.lead[0] *= cfg.predictMax / ll;
                S.lead[1] *= cfg.predictMax / ll;
            }
        } else if (!cfg.predict || S.flyer) {
            S.lead[0] = S.lead[1] = 0;
        }
        camPos[0] = S.outPos[0] + S.lead[0];
        camPos[1] = S.outPos[1];
        camPos[2] = S.outPos[2] + S.lead[1];
        if (dt > 0.f) {
            float cx = camPos[0] - S.prevCam[0], cz = camPos[2] - S.prevCam[2];
            S.camSpeed += (std::sqrt(cx * cx + cz * cz) / dt - S.camSpeed) * (1.f - std::exp(-dt / 0.12f));
        }
        memcpy(S.prevCam, camPos, sizeof camPos);
    }
    kick::Params kp;
    vmgame::GetKick(&kp);
    float adsW = S.adsT * S.adsT * (3.f - 2.f * S.adsT);
    S.camKick.Step(kp, now, dt, &S.pitch);
    if (float hk = vmgame::ShieldCamKick())
        S.camKick.v[0] += kick::Impulse(hk, kp.punchSpring[0], kp.punchSpring[1]);
    if (S.landPunch != 0.f) {
        S.camKick.v[0] += kick::Impulse(S.landPunch, kp.punchSpring[0], kp.punchSpring[1]);
        S.landPunch = 0.f;
    }
    kick::View kv = kick::CamView(S.camKick, S.pitch, S.yaw, lim);
    const float vRoll = kv.roll;
    const float* vFwd = kv.fwd;
    const float* vRight = kv.right;
    const float* vUp = kv.up;
    vmgame::PublishView(S.yaw, S.pitch, (S.flyer || (mode == 0 && S.air)) ? 0.f : S.camSpeed);
    float eye[3] = {camPos[0] + sy * cfg.eyeForward, camPos[1] + (S.flyer ? cfg.flyEyeHeight : cfg.eyeHeight),
                    camPos[2] + cy * cfg.eyeForward};
    float target[3] = {eye[0] + vFwd[0] * 10.f, eye[1] + vFwd[1] * 10.f, eye[2] + vFwd[2] * 10.f};
    float up[3] = {0.f, 1.f, 0.f};
    if (vRoll != 0.f) memcpy(up, vUp, sizeof up);
    CamSetVec(S.cam, vtSetUp, up);
    CamSetVec(S.cam, vtSetPos, eye);
    if (S.baseFov > 0.f) {
        float f = 2.f * std::atan(std::tan(S.baseFov * 0.5f) / zoomNow);
        if (std::fabs(f - S.lastFov) > 1e-4f) {
            CamSetF(S.cam, vtSetFov, f);
            S.lastFov = f;
        }
    }
    {
        float fov = 0;
        if (!Rd(S.cam + oCamFov, fov) || !(fov > 0.2f && fov < 2.8f)) fov = 1.1345f;
        vmgame::PublishCamera(eye, vRight, vUp, vFwd, fov);
        CastView(eye, vFwd);
        CastMarkerStep(eye, vFwd, camPos);
    }
    CamSetVec(S.cam, vtSetTarget, target);
    uintptr_t active = Ptr(mgr + oMgrActive);
    uintptr_t trans = Ptr(mgr + oMgrTransition), special = Ptr(mgr + oMgrSpecial);
    if (active != S.cam) {
        if (active && (active == trans || active == special)) {
            ++S.yields;
        } else if (Wr(mgr + oMgrActive, S.cam)) {
            ++S.writes;
        }
    }
    ++S.frames;
    if (canMove && mode != 0) {
        const float ahead = S.flyer ? cfg.flyAhead : cfg.moveAhead, turnDeg = S.flyer ? cfg.flyTurnDeg : cfg.moveTurnDeg;
        const double interval = S.flyer ? cfg.flyInterval : cfg.moveInterval;
        if (wantMove) {
            float dot = mx * S.lastMoveDir[0] + mz * S.lastMoveDir[1];
            bool changed = !S.moving || dot < std::cos(turnDeg * kPi / 180.f) || run != S.lastRun;
            if (changed || now >= S.nextMove) {
                if (!S.moving) {
                    S.tMoveBegan = now;
                    S.tMoveStart = now;
                    memcpy(S.startPos, raw, sizeof raw);
                    S.tHalt = -1;
                }
                if (StartOrSteer(raw[0] + mx * ahead, raw[2] + mz * ahead, run)) ++S.moves;
                S.nextMove = now + interval;
                S.lastMoveDir[0] = mx;
                S.lastMoveDir[1] = mz;
                S.lastRun = run;
                S.moving = true;
            }
        } else if (S.moving) {
            if (StopMoving()) ++S.halts;
            S.moving = false;
            S.tHalt = now;
            memcpy(S.haltPos, raw, sizeof raw);
            S.tMoveStart = -1;
        }
    }
    {
        float ddx = raw[0] - S.sigPos[0], ddz = raw[2] - S.sigPos[2];
        if (ddx * ddx + ddz * ddz > 0.03f * 0.03f) {
            memcpy(S.sigPos, raw, sizeof raw);
            S.tSig = now;
        }
        if (S.tMoveStart >= 0) {
            float sx = raw[0] - S.startPos[0], sz = raw[2] - S.startPos[2];
            if (sx * sx + sz * sz > 0.05f * 0.05f) {
                if (S.latLogs++ < 20)
                    Log("latency: start %.0f ms (%s) lead=%.2f,%.2f speedEst=%.2f", (now - S.tMoveStart) * 1000.0,
                        UseOverride() ? "entity" : "unit", S.lead[0], S.lead[1], S.speedEst);
                S.tMoveStart = -1;
            } else if (now - S.tMoveStart > 3.0) {
                if (S.latLogs++ < 20) Log("latency: entity did not move within 3 s of the first move order");
                S.tMoveStart = -1;
            }
        }
        if (S.tHalt >= 0 && now - S.tSig > 0.3) {
            float ox = raw[0] - S.haltPos[0], oz = raw[2] - S.haltPos[2];
            double stopMs = (S.tSig > S.tHalt ? S.tSig - S.tHalt : 0.0) * 1000.0;
            if (S.latLogs++ < 20)
                Log("latency: stop %.0f ms after release (%s), slid %.2f m, lead=%.2f,%.2f", stopMs,
                    UseOverride() ? "entity" : "unit", std::sqrt(ox * ox + oz * oz), S.lead[0], S.lead[1]);
            S.tHalt = -1;
            if (UseOverride()) {
                if (S.unitSync && g_moveOk) IssueHalt();
                S.tWatch = now;
                memcpy(S.watchPos, raw, sizeof raw);
            }
        } else if (S.tHalt >= 0 && now - S.tHalt > 4.0) {
            if (S.latLogs++ < 20) Log("latency: entity still moving 4 s after release");
            S.tHalt = -1;
        }
        if (S.moving) {
            S.tWatch = -1;
        } else if (S.tWatch >= 0) {
            float wx = raw[0] - S.watchPos[0], wz = raw[2] - S.watchPos[2];
            if (wx * wx + wz * wz > 0.8f * 0.8f) {
                Log("move(entity): hero moved %.2f m on its own within %.1f s after stopping (unit pulling it back?) -> "
                    "unit halt now and after every stop this session",
                    std::sqrt(wx * wx + wz * wz), now - S.tWatch);
                if (g_moveOk) IssueHalt();
                S.unitSync = true;
                S.tWatch = -1;
            } else if (now - S.tWatch > 3.0) {
                S.tWatch = -1;
            }
        }
    }
    {
        uintptr_t sbp = Ptr(g_base + kGlobalScript), bp = sbp ? Ptr(sbp + oScriptBattle) : 0;
        ProbeFrame(bp, now);
        VelStep(bp, eye, vFwd);
        FastFireStats(false);
    }
    bool held = g_cast.lmb;
    if (vmgame::MeleeActive()) {
        bool blockKey = g_cast.rmb;
        vmgame::MeleeInput(held && !S.sprint && now >= S.fireBlock, blockKey && !S.sprint);
        int strikeCat = vmgame::kMeleeCatSword;
        int strike = vmgame::TakeMeleeStrike(&strikeCat);
        if (strike >= 0 && g_fireOk && cfg.fireEnabled) {
            bool engine = MeleeEngineReady();
            if (engine ? MeleeEngineQueue(strike, strikeCat, eye, vFwd) : MeleeStrike(strike, eye, vFwd))
                ++S.meleeStrikes;
            else
                ++S.meleeFailed;
            if (S.strikeLogs < 4 && S.meleeStrikes == 1)
                Log("melee: first strike via %s", engine ? "engine melee hit (sim thread)" : "splash explosion");
        }
        held = false;
    }
    if (LONG lr = InterlockedExchange(&g_reqLauncher, 0)) LauncherTryCast(static_cast<int>(lr - 1), eye, vFwd);
    LauncherGameStep(now);
    double fireGap = vmgame::FireInterval() > 0 ? vmgame::FireInterval() : cfg.fireInterval;
    vmgame::SetTrigger(held && g_fireOk && cfg.fireEnabled && !S.sprint && now >= S.fireBlock);
    if (!held) S.fireChain = false;
    if (held && g_fireOk && cfg.fireEnabled && now >= S.nextShot && !S.sprint && now >= S.fireBlock && vmgame::CanFire()) {
        const float* fwd = vFwd;
        const float* right = vRight;
        const float* camUp = vUp;
        float bodies[4][3];
        int nb = 0;
        memcpy(bodies[nb++], raw, sizeof raw);
        float clearance = cfg.fireClearance;
        if (S.ghost) {
            nb = 0;
        } else if (mode == 0 && S.pinned) {
            clearance = cfg.pinnedClearance;
        } else if (mode == 0 && S.kinInit) {
            const float pts[3][2] = {{S.lastTp[0], S.lastTp[1]},
                                     {S.leadPos[0], S.leadPos[1]},
                                     {S.kinPos[0] + S.kinVel[0] * cfg.fireLookahead, S.kinPos[1] + S.kinVel[1] * cfg.fireLookahead}};
            for (const auto& q : pts) {
                bodies[nb][0] = q[0];
                bodies[nb][1] = raw[1];
                bodies[nb][2] = q[1];
                ++nb;
            }
        } else {
            bodies[nb][0] = raw[0] + S.entVel[0] * cfg.fireLookahead;
            bodies[nb][1] = raw[1];
            bodies[nb][2] = raw[2] + S.entVel[1] * cfg.fireLookahead;
            ++nb;
        }
        if (vmgame::CurrentWeapon() != S.projFor && g_shotOk) {
            S.projFor = vmgame::CurrentWeapon();
            const char* want = vmgame::WeaponProjectile(S.projFor)[0] ? vmgame::WeaponProjectile(S.projFor) : cfg.fireProjectile;
            int32_t id = -1;
            uintptr_t rec = want[0] ? FindProjectile(want, &id) : 0;
            S.fpProj = rec && id >= 0 ? rec : 0;
            S.fpProjId = S.fpProj ? id : -1;
            herofx::Track(S.fpProj);
            Log("fire: weapon %d projectile '%s' -> %s (id %d)", S.projFor, want, S.fpProj ? "found" : "NOT FOUND (hero's own)", S.fpProjId);
        }
        float sfwd[3];
        vmgame::SpreadDir(fwd, right, camUp, sfwd);
        if (FireShot(eye, sfwd, right, camUp, bodies, nb, clearance)) {
            ++S.shots;
            vmgame::Fire();
            S.camKick.OnShot(kp, adsW, now, &S.pitch, &S.yaw, lim);
            uintptr_t sbp = Ptr(g_base + kGlobalScript);
            if (sbp && !S.lastShotFast) ProbeShot(Ptr(sbp + oScriptBattle), now);
        } else {
            ++S.shotsFailed;
        }
        S.nextShot = S.fireChain && now - S.nextShot < fireGap ? S.nextShot + fireGap : now + fireGap;
        S.fireChain = true;
    }
    if ((now - S.tStat) * 1000.0 >= cfg.logIntervalMs) {
        Log("sec frames=%u activeWrites=%u yields=%u ent=%.2f,%.2f,%.2f eye=%.2f,%.2f,%.2f yaw=%.1f pitch=%.1f "
            "dx=%ld dy=%ld input=%s tick=%.3f active=%p shots=%u failed=%u moves=%u halts=%u mode=%s lead=%.2f,%.2f "
            "speedEst=%.2f camSpeed=%.2f teleports=%u/%u failed leash=%u heroLag=%.2f guard=%u/%u",
            S.frames, S.writes, S.yields, raw[0], raw[1], raw[2], eye[0], eye[1], eye[2], S.yaw * 180.f / kPi,
            S.pitch * 180.f / kPi, S.sumDx, S.sumDy, S.useRaw ? "raw" : "cursor", S.tickDur,
            (void*)Ptr(mgr + oMgrActive), S.shots, S.shotsFailed, S.moves, S.halts, ModeName(mode), S.lead[0],
            S.lead[1], S.speedEst, S.camSpeed, S.tpSent, S.tpFailed, S.leashHits,
            std::sqrt((camPos[0] - raw[0]) * (camPos[0] - raw[0]) + (camPos[2] - raw[2]) * (camPos[2] - raw[2])), S.tpGuarded,
            S.tpGuardSkipped);
        S.tStat = now;
        S.frames = S.writes = S.yields = 0;
        S.sumDx = S.sumDy = 0;
        S.shots = S.shotsFailed = 0;
        S.moves = S.halts = 0;
        S.tpSent = S.tpFailed = S.leashHits = 0;
        S.tpGuarded = S.tpGuardSkipped = 0;
    }
}
bool InstallHook() {
    uintptr_t ui = Ptr(g_base + kGlobalUi);
    uintptr_t battle = ui ? Ptr(ui + oUiBattle) : 0;
    uintptr_t mgr = battle ? Ptr(battle + oBattleCamMgr) : 0;
    if (!mgr) {
        Refuse(kRefuseOther);
        Log("toggle ignored: no battle camera manager (ui=%p battle=%p)", (void*)ui, (void*)battle);
        return false;
    }
    uintptr_t vt = Ptr(mgr);
    uintptr_t ours = g_vtCopy ? reinterpret_cast<uintptr_t>(g_vtCopy + 2) : 0;
    if (ours && vt == ours) return true;
    if (vt != g_base + kVtCamMgr) {
        Refuse(kRefuseOther);
        Log("camera manager %p has unexpected vtable %p; not hooking", (void*)mgr, (void*)vt);
        return false;
    }
    if (!g_vtCopy) {
        g_vtCopy = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (kVtCopySlots + 2) * sizeof(uintptr_t),
                                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!g_vtCopy) return false;
        for (size_t i = 0; i < kVtCopySlots + 2; ++i) g_vtCopy[i] = Ptr(g_base + kVtCamMgr - 16 + i * 8);
        g_orig_cam_update = reinterpret_cast<void*>(g_vtCopy[2 + vtMgrUpdate / 8]);
        g_vtCopy[2 + vtMgrUpdate / 8] = reinterpret_cast<uintptr_t>(&hfps_cam_update_thunk);
        DWORD old;
        VirtualProtect(g_vtCopy, (kVtCopySlots + 2) * sizeof(uintptr_t), PAGE_READONLY, &old);
        ours = reinterpret_cast<uintptr_t>(g_vtCopy + 2);
    }
    if (!Wr(mgr, ours)) {
        Refuse(kRefuseOther);
        Log("hook write failed on camera manager %p", (void*)mgr);
        return false;
    }
    g_installedMgr = mgr;
    Log("hooked battle camera manager %p (battle %p) via vtable copy %p", (void*)mgr, (void*)battle, (void*)ours);
    if (S.active || g_fpDetached) {
        InterlockedExchange(&g_staleFp, 1);
        Log("FPS: first-person state left over from camera manager %p (%s) - dropped on the next frame", (void*)S.mgr,
            S.active ? "battle ended while in first person?" : "already released on the window side");
    }
    return true;
}
constexpr double kStaleHookSecs = 3.0, kStaleHookGap = 1.0;
struct BeatWatch {
    LONG beat = 0;
    double since = -1, last = -1;
    bool Step(LONG b, double now, double limit, double gap) {
        if (since < 0 || b != beat || now - last > gap) {
            beat = b;
            since = last = now;
            return false;
        }
        last = now;
        return now - since >= limit;
    }
    void Reset() { since = last = -1; }
} g_beatWatch;
void WindowSideRestore() {
    if (!S.active && S.cursorHidden) {
        reinterpret_cast<void (*)(bool)>(g_base + kFnSetCursorVisible)(S.cursorVisOrig != 0);
        S.cursorHidden = false;
    }
    if (!S.active && S.speedApplied) ShotRestoreSpeed("window fallback");
    if (!S.active && S.cursorOff) {
        reinterpret_cast<void (*)(bool)>(g_base + kFnSetCursorEnabled)(S.cursorOrig != 0);
        S.cursorOff = false;
    }
}
void StaleHookCheck() {
    if (!S.active) {
        g_beatWatch.Reset();
        return;
    }
    if (!g_beatWatch.Step(g_camBeat, Now(), kStaleHookSecs, kStaleHookGap)) return;
    g_beatWatch.Reset();
    S.active = false;
    ReleaseMouse();
    vmgame::SetFirstPerson(false);
    const LONG oldExit = InterlockedExchange(&g_reqExit, 0), oldLauncher = InterlockedExchange(&g_reqLauncher, 0);
    g_fpDetached = true;
    g_detachMgr = S.mgr;
    Log("FPS state dropped on the window side: no camera update for %.0f s (battle ended while in first person?) - cursor, mouse and keys "
        "released (camera manager %p, exit request %ld, launcher request %ld cleared)",
        kStaleHookSecs, (void*)S.mgr, oldExit, oldLauncher);
}
LRESULT CALLBACK HfpsWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    StaleHookCheck();
    WindowSideRestore();
    switch (m) {
    case WM_INPUT:
        if (S.active && S.useRaw) {
            RAWINPUT ri;
            UINT size = sizeof ri;
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(l), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) !=
                    static_cast<UINT>(-1) &&
                ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                InterlockedExchangeAdd(&g_rawDx, ri.data.mouse.lLastX);
                InterlockedExchangeAdd(&g_rawDy, ri.data.mouse.lLastY);
            }
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (w == cfg.toggleKey) {
            if (!(l & (1L << 30)) && InstallHook()) InterlockedExchange(&g_reqToggle, 1);
            return 0;
        }
        if (vmgame::Enabled() && w == vmgame::TestKey()) {
            if (!(l & (1L << 30))) vmgame::ToggleTestLayer();
            return 0;
        }
        if (vmgame::LayerActive() && w == vmgame::ReloadKey()) {
            if (!(l & (1L << 30))) {
                vmgame::RequestReload();
                LoadConfig();
            }
            return 0;
        }
        if (vmgame::LayerActive() && w == vmgame::AnimKey()) {
            if (!(l & (1L << 30))) vmgame::CycleAnim();
            return 0;
        }
        if (vmgame::LayerActive() && w == vmgame::ModelKey()) {
            if (!(l & (1L << 30))) vmgame::CycleModel();
            return 0;
        }
        if (w == VK_ESCAPE && S.active) InterlockedExchange(&g_reqExit, 1);
        if (S.active && w == 'R') {
            if (!(l & (1L << 30))) vmgame::StartMagReload();
            return 0;
        }
        if (S.active && cfg.castKeys && (GetKeyState(VK_MENU) & 0x8000)) {
            if (int slot = CastSlotKey(w)) {
                if (!(l & (1L << 30))) CastPush(slot);
                InterlockedOr(&g_cast.unswallow, 1L << slot);
                return 0;
            }
        }
        if (S.active && w >= '1' && w <= '9') {
            if (!(l & (1L << 30))) vmgame::SelectWeapon(static_cast<int>(w - '0'));
            return 0;
        }
        if (S.active && vmgame::ShieldKey() && w == vmgame::ShieldKey()) {
            if (!(l & (1L << 30))) vmgame::ToggleShield();
            return 0;
        }
        if (S.active && vmgame::ShieldHitKey() && w == vmgame::ShieldHitKey()) {
            if (!(l & (1L << 30))) vmgame::DebugShieldHit();
            return 0;
        }
        if (S.active && g_heroLauncher && vmgame::LauncherConfigured()) {
            if (w == vmgame::LauncherSwitchKey()) {
                if (!(l & (1L << 30))) vmgame::LauncherSwitch();
                return 0;
            }
            if (w == vmgame::LauncherFireKey() || w == vmgame::LauncherFlameKey()) {
                int kind = w == vmgame::LauncherFlameKey() ? vmgame::kLauncherFlame : vmgame::LauncherSelected();
                if (!(l & (1L << 30))) InterlockedExchange(&g_reqLauncher, kind + 1);
                return 0;
            }
        }
        if (S.active && (w == 'W' || w == 'A' || w == 'S' || w == 'D')) return 0;
        if (S.active && cfg.wallUp && w == cfg.wallUpKey) return 0;
        if (S.active && w == VK_SPACE && cfg.jumpHeight > 0.f) return 0;
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (w == cfg.toggleKey) return 0;
        if (vmgame::Enabled() && w == vmgame::TestKey()) return 0;
        if (vmgame::LayerActive() && (w == vmgame::ReloadKey() || w == vmgame::AnimKey() || w == vmgame::ModelKey()))
            return 0;
        if (int slot = CastSlotKey(w)) {
            if (g_cast.unswallow & (1L << slot)) {
                InterlockedAnd(&g_cast.unswallow, ~(1L << slot));
                return 0;
            }
        }
        if (S.active && (w == 'W' || w == 'A' || w == 'S' || w == 'D' || w == 'R' || (w >= '1' && w <= '9'))) return 0;
        if (S.active && w == VK_SPACE && cfg.jumpHeight > 0.f) return 0;
        if (S.active && ((vmgame::ShieldKey() && w == vmgame::ShieldKey()) || (vmgame::ShieldHitKey() && w == vmgame::ShieldHitKey()))) return 0;
        if (S.active && g_heroLauncher && vmgame::LauncherConfigured() &&
            (w == vmgame::LauncherFireKey() || w == vmgame::LauncherSwitchKey() || w == vmgame::LauncherFlameKey()))
            return 0;
        break;
    case WM_SYSCHAR:
        if (S.active && cfg.castKeys && ((w >= '0' && w <= '9') || w == '-' || w == '=')) return 0;
        break;
    case WM_MOUSEWHEEL:
        if (S.active) {
            vmgame::CycleWeapon(GET_WHEEL_DELTA_WPARAM(w) < 0 ? 1 : -1);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        if (S.active) return 0;
        break;
    case WM_SETCURSOR:
        if (S.active && cfg.hideCursor && LOWORD(l) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_ACTIVATEAPP:
        if (!w && S.active) InterlockedExchange(&g_reqExit, 1);
        break;
    default:
        break;
    }
    return CallWindowProcW(g_prevProc, h, m, w, l);
}
LRESULT CALLBACK HfpsChildWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    StaleHookCheck();
    WindowSideRestore();
    switch (m) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        if (S.active) {
            if (g_childSwallowLogs < 12) {
                ++g_childSwallowLogs;
                Log("input: swallowed mouse button message 0x%03X on render child (first person)", m);
            }
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (S.active) {
            vmgame::CycleWeapon(GET_WHEEL_DELTA_WPARAM(w) < 0 ? 1 : -1);
            return 0;
        }
        break;
    default:
        break;
    }
    return CallWindowProcW(g_prevChildProc, h, m, w, l);
}
void HookRenderChild() {
    if (!g_hwnd) return;
    wchar_t cls[128] = {};
    if (!GetClassNameW(g_hwnd, cls, 128)) return;
    HWND c = FindWindowExW(g_hwnd, nullptr, cls, L"Warscape");
    if (!c) c = FindWindowExW(g_hwnd, nullptr, cls, nullptr);
    if (!c) {
        if (g_childSwallowLogs < 12) {
            ++g_childSwallowLogs;
            Log("window: render child not found under %p (class %ls); mouse buttons are not blocked", (void*)g_hwnd, cls);
        }
        return;
    }
    if (c == g_childHwnd) return;
    WNDPROC prev = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(c, GWLP_WNDPROC));
    if (prev == &HfpsChildWndProc) {
        g_childHwnd = c;
        return;
    }
    prev = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(c, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HfpsChildWndProc)));
    if (!prev) {
        Log("window: render child %p subclass failed err=%lu", (void*)c, GetLastError());
        return;
    }
    wchar_t title[64] = {};
    GetWindowTextW(c, title, 64);
    RECT rc{};
    GetClientRect(c, &rc);
    g_childHwnd = c;
    g_prevChildProc = prev;
    Log("window: render child hwnd=%p title=%ls size=%ldx%ld prevProc=%p (mouse buttons are blocked here in first person)",
        (void*)c, title, rc.right - rc.left, rc.bottom - rc.top, (void*)prev);
}
struct FindCtx {
    DWORD pid;
    HWND best;
    long area;
};
BOOL CALLBACK FindMain(HWND h, LPARAM p) {
    auto* c = reinterpret_cast<FindCtx*>(p);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT r{};
    GetClientRect(h, &r);
    long a = (r.right - r.left) * (r.bottom - r.top);
    if (a > c->area) {
        c->area = a;
        c->best = h;
    }
    return TRUE;
}
bool CodeMatches(const CodePrint& cp) {
    unsigned long long h = 0xcbf29ce484222325ull;
    for (uintptr_t i = 0; i < cp.len; ++i) {
        unsigned char b = 0;
        if (!Rd(g_base + cp.rva + i, b)) return false;
        h = (h ^ b) * 0x100000001b3ull;
    }
    return h == cp.fnv;
}
bool CheckRegStub(uintptr_t stub, uintptr_t factory, uintptr_t type, const char* name) {
    unsigned char b[29];
    for (size_t i = 0; i < sizeof b; ++i)
        if (!Rd(g_base + stub + i, b[i])) return false;
    auto rel = [&](size_t at) {
        int32_t r;
        memcpy(&r, b + at, 4);
        return static_cast<uintptr_t>(static_cast<intptr_t>(stub + at + 4) + r);
    };
    if (b[0] != 0x4C || b[1] != 0x8D || b[2] != 0x0D || rel(3) != factory) return false;
    if (b[7] != 0x45 || b[8] != 0x33 || b[9] != 0xC0) return false;
    if (b[10] != 0x48 || b[11] != 0x8D || b[12] != 0x15) return false;
    if (b[17] != 0x48 || b[18] != 0x8D || b[19] != 0x0D || rel(20) != type) return false;
    if (b[24] != 0xE9 || rel(25) != kFnCmdRegister) return false;
    uintptr_t s = rel(13);
    for (size_t i = 0;; ++i) {
        char c = 0;
        if (!Rd(g_base + s + i, c) || c != name[i]) return false;
        if (!c) return true;
    }
}
bool HostIsWh3() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(GetModuleHandleW(nullptr), path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"Warhammer3.exe") == 0;
}
bool PrintsMatch(const PrintSet& s, const char* what) {
    for (size_t i = 0; i < s.n; ++i)
        if (!CodeMatches(s.p[i])) {
            Log("%s: code fingerprint %zu/%zu mismatch at +0x%llx (len 0x%llx)", what, i + 1, s.n,
                (unsigned long long)s.p[i].rva, (unsigned long long)s.p[i].len);
            return false;
        }
    return s.n > 0;
}
bool VerifyGame() {
    HMODULE exe = GetModuleHandleW(nullptr);
    g_base = reinterpret_cast<uintptr_t>(exe);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g_base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
    DWORD stamp = nt->FileHeader.TimeDateStamp, size = nt->OptionalHeader.SizeOfImage;
    g_imageSize = size;
    const GameAddrs* game = nullptr;
    for (const GameAddrs& a : kGames)
        if (a.stamp == stamp && a.size == size) game = &a;
    if (!game) {
        Log("Warhammer3.exe version not supported: stamp=0x%08lX size=0x%lX (this build knows %s 0x%08lX/0x%lX); "
            "module inactive - the game was probably updated, addresses need porting",
            stamp, size, kGames[0].name, kGames[0].stamp, kGames[0].size);
        g_status = kStatusVersion;
        return false;
    }
    UseGame(*game);
    Log("Warhammer3.exe %s (stamp 0x%08lX size 0x%lX)", game->name, stamp, size);
    bool ok = Ptr(g_base + kVtCamMgr + vtMgrUpdate) == g_base + kFnCamMgrUpdate && PrintsMatch(game->cam, "camera");
    if (!ok) {
        Log("Warhammer3.exe camera signatures do not match; module inactive");
        g_status = kStatusSignatures;
    }
    if (ok) vmgame::VerifyStatic(g_base, &Log, game->render);
    if (ok) {
        bool f = Ptr(g_base + kVtFireCmd) == g_base + kFnFireCmdWrite && PrintsMatch(game->fire, "fire");
        g_fireOk = f;
        if (!f) Log("fire: command signatures do not match this exe; firing disabled");
        bool m = f && PrintsMatch(game->move, "unit move");
        g_moveOk = m;
        if (!m) Log("move: goto_location / halt code fingerprints do not match this exe; unit-order movement disabled");
        bool base = f && PrintsMatch(game->cmd, "command writer");
        bool h = base && CheckRegStub(kRegOvrHalt, kFnOvrHaltFactory, kTypeOvrHalt, "BCQ_ENTITY_USER_OVERRIDE_ORDER_HALT") &&
                 PrintsMatch(game->halt, "entity halt");
        bool o = h && CheckRegStub(kRegOvrMove, kFnOvrMoveFactory, kTypeOvrMove, "BCQ_ENTITY_USER_OVERRIDE_ORDER_MOVE") &&
                 PrintsMatch(game->ovr, "entity move");
        bool t = h &&
                 CheckRegStub(kRegOvrTeleport, kFnOvrTeleportFactory, kTypeOvrTeleport,
                              "BCQ_ENTITY_USER_OVERRIDE_ORDER_TELEPORT") &&
                 Ptr(g_base + kVtOvrTeleport + 8) == g_base + kFnOvrTeleportStart && PrintsMatch(game->tp, "entity teleport");
        g_haltOk = h;
        g_ovrOk = o;
        g_tpOk = t;
        g_ghostOk = f && Ptr(g_base + kVtUnitEnableCmd) == g_base + kFnUnitEnableWrite && PrintsMatch(game->ghost, "ghost (unit enable)");
        Log("ghost mode (unit disable) %s", g_ghostOk ? "ok" : "OFF");
        g_revealOk = g_ghostOk && Ptr(g_base + kVtUnitVisibleCmd) == g_base + kFnUnitVisibleWrite &&
                     PrintsMatch(game->reveal, "reveal (always visible)");
        Log("ghost: reveal enemies (always visible) %s", g_revealOk ? "ok" : "OFF");
        g_meleeOk = f && Ptr(g_base + kVtStatAttrCmd) == g_base + kFnStatAttrWrite && PrintsMatch(game->melee, "no melee (stat attribute)");
        Log("hero: no-melee switch (melee_disabled) %s", g_meleeOk ? "ok" : "OFF");
        g_uiOk = PrintsMatch(game->ui, "battle hud hide");
        Log("ui: battle hud hiding %s", g_uiOk ? "ok" : "OFF");
        g_shotOk = PrintsMatch(game->shot, "fire owner / bullet speed");
        Log("fire: owner + unlimited ammo + bullet speed %s", g_shotOk ? "ok" : "OFF");
        g_meleeHitOk = g_shotOk && PrintsMatch(Prints(kMeleeHitPrints904), "engine melee hit");
        Log("melee: engine melee hit %s", g_meleeHitOk ? "ok" : "OFF (melee weapon falls back to splash explosions)");
        g_noKnockOk = PrintsMatch(Prints(kNoKnockPrints904), "hero no knockdown");
        Log("hero: no-knockdown switch %s", g_noKnockOk ? "ok" : "OFF");
        g_shieldGameOk = PrintsMatch(Prints(kShieldGamePrints904), "shield block (game hits)") &&
                         Ptr(g_base + kVtSoldier904 + 0x310) == g_base + kFnProjHit904 && Ptr(g_base + kVtSoldier904 + 0x1c8) == g_base + kFnRet0Of1c8_904 &&
                         Ptr(g_base + kVtSoldier904 + 0x3a8) == g_base + kFnHitReact904 && Ptr(g_base + kVtSoldier904 + 0x558) == g_base + kFnStagger904;
        if (g_shieldGameOk) g_hero_1c8_ret1 = g_base + kMeleeRet1_904, g_hero_1c8_ret2 = g_base + kMeleeRet2_904;
        Log("shield-game: shield blocks real hits (missile +0x310 / melee +0x1c8 / stagger +0x558 / hp refund) %s", g_shieldGameOk ? "ok" : "OFF");
        g_armorOk = PrintsMatch(Prints(kArmorPrints904), "super armor (hit reaction state 18)") && Ptr(g_base + kVtSoldier904 + 0x558) == g_base + kFnStagger904;
        Log("hero: super armor (no hit-reaction stagger in first person) %s", g_armorOk ? "ok" : "OFF - hits still stagger the hero");
        g_abilityOk = m && PrintsMatch(Prints(kAbilityPrints904), "launcher ability cast") &&
                      Ptr(g_base + kVtAbilityCmd904) == g_base + kFnAbilityCmdWrite904;
        Log("launcher: ability cast (special ability on ground) %s", g_abilityOk ? "ok" : "OFF");
        g_charOk = PrintsMatch(Prints(kCharPrints904), "lord / hero check (IsCharacter)");
        Log("hero: lords / heroes only check %s (heroes_only=%d)", g_charOk ? "ok" : "OFF - nobody can enter while heroes_only=1",
            cfg.heroesOnly);
        g_routOk = PrintsMatch(Prints(kRoutPrints904), "hero routing check (is_routing)");
        Log("hero: leave first person when the hero routs %s", g_routOk ? "ok" : "OFF - a routing hero stays controllable");
        g_arrowOk = PrintsMatch(Prints(kArrowPrints904), "stuck arrows (arrow visual manager)") &&
                    Ptr(g_base + kVtArrowMgr904) == g_base + kFnArrowUpdate904 && Ptr(g_base + kVtArrowMgr904 + 8) == g_base + kFnArrowDraw904;
        Log("arrows: hide arrows stuck in the hero %s", g_arrowOk ? "ok" : "OFF - arrows that hit the hero stay visible in first person");
        g_flyOk = PrintsMatch(Prints(kFlyPrints904), "flying heroes (CanFly / desired flying state)");
        Log("fly: flying heroes %s (fly=%d takeoff=%d ahead=%.0f interval=%.2f turn=%.0f eye=%.2f)",
            g_flyOk ? "ok" : "OFF - flying heroes use the normal movement", cfg.fly, cfg.flyTakeoff, cfg.flyAhead, cfg.flyInterval,
            cfg.flyTurnDeg, cfg.flyEyeHeight);
        g_wallOk = PrintsMatch(Prints(kWallPrints904), "wall nav blocks (teleport hint)");
        g_buildOk = PrintsMatch(Prints(kBuildPrints904), "building collision (segment query)");
        g_netOk = PrintsMatch(Prints(kNetPrints904), "ground region net (arches / passages)") &&
                  Ptr(g_base + kVtRegionSub904 + 0x10) == g_base + kFnRegionOn904;
        Log("wall: wall body asks the game's ground region net %s (wall_body_net=%d)",
            g_netOk ? "ok - arches / open passages under walkways are walkable" : "OFF - every walkway shadow on the ground counts as a wall body", cfg.wallBodyNet);
        g_gameVolOk = PrintsMatch(Prints(kGameVolPrints904), "game volume (audio manager)") &&
                      Ptr(g_base + kVtAudioMgr904 + 0x478) == g_base + kFnAudioSetOn904 &&
                      Ptr(g_base + kVtAudioMgr904 + 0x488) == g_base + kFnAudioSetVol904 &&
                      Ptr(g_base + kVtAudioMgr904 + 0x498) == g_base + kFnAudioSetScale904;
        Log("audio: follow game master x sfx volume %s", g_gameVolOk ? "ok" : "OFF - first-person sounds stay at fixed -5 dB");
        Log("wall: building walls %s (building=%d)", g_buildOk ? "solid" : "OFF - can walk through buildings", cfg.building);
        Log("wall: city walls / building nav blocks %s (wall=%d)", g_wallOk ? "ok" : "OFF - terrain only, jumping still works", cfg.wall);
        Log("move: unit orders %s, entity halt %s, entity move %s, entity teleport %s (unavailable modes fall back to unit orders)",
            m ? "ok" : "OFF", h ? "ok" : "OFF", o ? "ok" : "OFF", t ? "ok" : "OFF");
    }
    return ok;
}
bool ReadableRange(const unsigned char* p, size_t n) {
    while (n) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        size_t left = static_cast<size_t>(static_cast<const unsigned char*>(mbi.BaseAddress) + mbi.RegionSize - p);
        if (left >= n) return true;
        p += left;
        n -= left;
    }
    return true;
}
bool IsOurOldProxy(HMODULE m) {
    static const unsigned char kSigX[] = {0x72, 0x2C, 0x3F, 0x28, 0x29, 0x33, 0x35, 0x34, 0x74, 0x3E, 0x36, 0x36,
                                          0x7A, 0x2A, 0x28, 0x35, 0x22, 0x23, 0x73, 0x7A, 0x2A, 0x33, 0x3E, 0x67};
    volatile unsigned char key = 0x5A;
    char kSig[sizeof kSigX + 1] = {};
    for (size_t i = 0; i < sizeof kSigX; ++i) kSig[i] = static_cast<char>(kSigX[i] ^ key);
    const size_t sn = sizeof kSigX;
    auto* base = reinterpret_cast<const unsigned char*>(reinterpret_cast<uintptr_t>(m) & ~static_cast<uintptr_t>(3));
    if (!ReadableRange(base, sizeof(IMAGE_DOS_HEADER))) return false;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || !ReadableRange(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS64)))
        return false;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    WORD ns = nt->FileHeader.NumberOfSections;
    if (!ReadableRange(reinterpret_cast<const unsigned char*>(sec), ns * sizeof *sec)) return false;
    for (WORD i = 0; i < ns; ++i) {
        DWORD ch = sec[i].Characteristics;
        if (!(ch & IMAGE_SCN_MEM_READ) || (ch & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_DISCARDABLE))) continue;
        size_t n = sec[i].Misc.VirtualSize < sec[i].SizeOfRawData ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        const unsigned char* p = base + sec[i].VirtualAddress;
        if (n < sn || !ReadableRange(p, n)) continue;
        const unsigned char* end = p + n - sn + 1;
        for (const unsigned char* q = p; q < end; ++q) {
            q = static_cast<const unsigned char*>(memchr(q, kSig[0], static_cast<size_t>(end - q)));
            if (!q) break;
            if (memcmp(q, kSig, sn) == 0) return true;
        }
    }
    return false;
}
DWORD WINAPI InitThread(LPVOID) {
    if (!HostIsWh3()) return 0;
    OpenLog();
#ifdef HFPS_LUA_MODULE
    Log("==== tenzin_fps native loaded (hero_fps.pack Lua loader, require tenzin_fps_native) pid=%lu", GetCurrentProcessId());
    {
        wchar_t vp[MAX_PATH] = {}, want[MAX_PATH];
        HMODULE vm = GetModuleHandleW(L"version.dll");
        if (vm) GetModuleFileNameW(vm, vp, MAX_PATH);
        swprintf(want, MAX_PATH, L"%ls\\version.dll", g_dir);
        bool inDir = vm && _wcsicmp(vp, want) == 0;
        if (inDir && IsOurOldProxy(vm)) {
            Log("loader: old version.dll proxy %ls is also loaded (version.dll route retired 10-18 - remove it) - this copy stays idle", vp);
            g_status = kStatusOldProxy;
            return 0;
        }
        Log("loader: version.dll = %ls (%s), module dir %ls", vp[0] ? vp : L"(none)",
            inDir ? "in the game folder, another tool's (no hero_fps / tenzin_fps proxy signature) - carrying on" : "not ours", g_dir);
    }
#ifdef HFPS_PACK_ASSETS
    if (!packx::ExtractFromPack(g_dir, &Log)) {
        Log("onepack: assets not available - first person stays off");
        g_status = kStatusAssets;
        return 0;
    }
#endif
#else
    Log("==== tenzin_fps native M1 + viewmodel test loaded (version.dll proxy) pid=%lu", GetCurrentProcessId());
#endif
    QueryPerformanceFrequency(&g_qpf);
    if (!VerifyGame()) return 0;
    LoadConfig();
    if (!cfg.enabled) {
        Log("disabled by tenzin_fps.ini");
        g_status = kStatusDisabled;
        return 0;
    }
    vmgame::Setup(g_base, g_dir, &Log);
    g_coreOk = true;
    FindCtx ctx{GetCurrentProcessId(), nullptr, 0};
    for (int i = 0; i < 6000 && !ctx.best; ++i) {
        Sleep(100);
        ctx.area = 0;
        EnumWindows(FindMain, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.best && ctx.area < 320 * 200) ctx.best = nullptr;
    }
    if (!ctx.best) {
        Log("game window not found; module idle");
        g_status = kStatusNoWindow;
        return 0;
    }
    Sleep(1500);
    wchar_t cls[128] = {}, title[128] = {};
    GetClassNameW(ctx.best, cls, 128);
    GetWindowTextW(ctx.best, title, 128);
    g_windowThread = GetWindowThreadProcessId(ctx.best, nullptr);
    g_hwnd = ctx.best;
    g_prevProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(ctx.best, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HfpsWndProc)));
    Log("window hwnd=%p class=%ls title=%ls thread=%lu prevProc=%p", ctx.best, cls, title, g_windowThread,
        (void*)g_prevProc);
    if (!g_prevProc) {
        Log("subclass failed err=%lu", GetLastError());
        g_status = kStatusNoWindow;
        return 0;
    }
    Log("ready: select one unit in battle and press toggle key 0x%02X (viewmodel test key 0x%02X, reload 0x%02X)",
        cfg.toggleKey, vmgame::TestKey(), vmgame::ReloadKey());
    g_status = kStatusReady;
    return 0;
}
bool LoadRealVersion() {
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (!n || n > MAX_PATH - 20) return false;
    wcscat(sys, L"\\version.dll");
    HMODULE real = LoadLibraryW(sys);
    if (!real) return false;
#define RESOLVE(name) p_ ##name = reinterpret_cast<void*>(GetProcAddress(real, #name))
    RESOLVE(GetFileVersionInfoA);
    RESOLVE(GetFileVersionInfoByHandle);
    RESOLVE(GetFileVersionInfoExA);
    RESOLVE(GetFileVersionInfoExW);
    RESOLVE(GetFileVersionInfoSizeA);
    RESOLVE(GetFileVersionInfoSizeExA);
    RESOLVE(GetFileVersionInfoSizeExW);
    RESOLVE(GetFileVersionInfoSizeW);
    RESOLVE(GetFileVersionInfoW);
    RESOLVE(VerFindFileA);
    RESOLVE(VerFindFileW);
    RESOLVE(VerInstallFileA);
    RESOLVE(VerInstallFileW);
    RESOLVE(VerLanguageNameA);
    RESOLVE(VerLanguageNameW);
    RESOLVE(VerQueryValueA);
    RESOLVE(VerQueryValueW);
#undef RESOLVE
    return true;
}
unsigned PromoteHeroBullets(uintptr_t world, uintptr_t* ours) {
    uintptr_t pm = *reinterpret_cast<uintptr_t*>(world + 0x1c8);
    if (!pm) return 0;
    uint32_t pc = *reinterpret_cast<uint32_t*>(pm + 0x24);
    auto* pa = *reinterpret_cast<uintptr_t**>(pm + 0x28);
    if (!pc || !pa || pc > 8192) return 0;
    if (static_cast<LONG>(pc) > g_ef.maxPending) g_ef.maxPending = static_cast<LONG>(pc);
    unsigned k = 0, w = 0;
    for (uint32_t i = 0; i < pc; ++i) {
        uintptr_t p = pa[i];
        if (k < 64 && p && *reinterpret_cast<uintptr_t*>(p + 0x20) == g_ef.hero)
            ours[k++] = p;
        else
            pa[w++] = p;
    }
    if (!k) return 0;
    *reinterpret_cast<uint32_t*>(pm + 0x24) = w;
    uint32_t ac = *reinterpret_cast<uint32_t*>(pm + 0x14);
    uintptr_t aa = *reinterpret_cast<uintptr_t*>(pm + 0x18);
    using FnInsert = void (*)(uintptr_t container, uintptr_t pos, const uintptr_t* first, const uintptr_t* last);
    reinterpret_cast<FnInsert>(g_base + kProjMerge904)(pm + 0x10, aa + static_cast<uintptr_t>(ac) * 8, ours, ours + k);
    InterlockedExchangeAdd(&g_ef.promoted, static_cast<LONG>(k));
    return k;
}
}
extern "C" void hfps_post_step_begin(uintptr_t self, uintptr_t world) {
    (void)self;
    InterlockedIncrement(&g_ef.calls);
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    g_ef.lastQpc = q.QuadPart;
    if (!world || world != g_ef.world || !g_ef.hero) return;
    hitprobe::Observe(world, g_ef.hero);
    ShieldHpStep();
    MeleeEngineStep(world, g_ef.hero);
    uintptr_t ours[64];
    unsigned k = PromoteHeroBullets(world, ours);
    hitprobe::Arm(world, g_ef.hero, ours, k);
}
extern "C" void hfps_sim_tick(uintptr_t self, uintptr_t packet, uintptr_t battle) {
    (void)self;
    InterlockedIncrement(&g_ff.ticks);
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    g_ff.lastTickQpc = q.QuadPart;
    g_ff.simTid = GetCurrentThreadId();
    if (!battle || !packet || battle != g_ff.battle) return;
    auto u32 = [](uintptr_t a) -> volatile uint32_t& { return *reinterpret_cast<volatile uint32_t*>(a); };
    AcquireSRWLockExclusive(&g_ff.lock);
    if (g_ff.lastPacket == packet && u32(packet + 0x64054) == (g_ff.lastOldBlocks ? g_ff.lastOldBlocks : 1) &&
        u32(g_ff.lastBlock + 0x5000) == g_ff.lastNewSize &&
        !memcmp(reinterpret_cast<const void*>(g_ff.lastBlock + g_ff.lastOldSize), g_ff.lastHead, sizeof g_ff.lastHead)) {
        u32(g_ff.lastBlock + 0x5000) = g_ff.lastOldSize;
        u32(packet + 0x64054) = g_ff.lastOldBlocks;
        InterlockedIncrement(&g_ff.undone);
    }
    g_ff.lastPacket = 0;
    uint32_t blocks = u32(packet + 0x64054);
    if (blocks > g_ff.maxBlocks && blocks <= 20) g_ff.maxBlocks = blocks;
    if (g_ff.n) {
        if (blocks > 1) {
            InterlockedExchange(&g_ff.multiBlock, 1);
            InterlockedIncrement(&g_ff.dropped);
        } else {
            uint32_t nb = blocks ? blocks : 1;
            uintptr_t blk = packet + 4;
            uint32_t sz = blocks ? u32(blk + 0x5000) : 0;
            if (sz + g_ff.n <= 0x5000) {
                memcpy(reinterpret_cast<void*>(blk + sz), g_ff.buf, g_ff.n);
                u32(blk + 0x5000) = sz + g_ff.n;
                u32(packet + 0x64054) = nb;
                g_ff.lastPacket = packet;
                g_ff.lastBlock = blk;
                g_ff.lastOldSize = sz;
                g_ff.lastNewSize = sz + g_ff.n;
                g_ff.lastOldBlocks = blocks;
                memcpy(g_ff.lastHead, g_ff.buf, g_ff.n < sizeof g_ff.lastHead ? g_ff.n : sizeof g_ff.lastHead);
                if (g_ff.n < sizeof g_ff.lastHead) memset(g_ff.lastHead + g_ff.n, 0, sizeof g_ff.lastHead - g_ff.n);
                double t = Now();
                for (int i = 0; i < g_ff.nPend; ++i) {
                    double l = (t - g_ff.tPend[i]) * 1000.0;
                    g_ff.latSum += l;
                    if (l > g_ff.latMax) g_ff.latMax = l;
                    ++g_ff.latN;
                }
                InterlockedExchangeAdd(&g_ff.injected, g_ff.nPend);
            } else {
                InterlockedExchangeAdd(&g_ff.dropped, g_ff.nPend);
            }
        }
        g_ff.n = 0;
        g_ff.nPend = 0;
    }
    ReleaseSRWLockExclusive(&g_ff.lock);
}
constexpr uintptr_t oBattleVisRoot = 0x64600, oVisRootMgr = 0x1a0, oVisMgrUnitCount = 0x164, oVisMgrUnits = 0x168, oUnitVisUnit = 0xf8,
                    oUnitVisSoldierCount = 0x34, oUnitVisSoldiers = 0x38, oSoldierVisEntity = 0x18, oSoldierVisInstance = 0xf8,
                    oInstPartCount = 0x8, oInstParts = 0x10, kInstPartStride = 0x48;
constexpr uint32_t kHideMaxUnits = 4096, kHideMaxSoldiers = 1024;
constexpr int kHideMaxParts = 32;
struct HideSelf {
    uintptr_t sv = 0, inst = 0, parts = 0;
    int n = 0;
    uint32_t want[kHideMaxParts] = {};
    double tSearch = -1, tCheck = 0;
    unsigned rewrites = 0;
    bool warned = false;
} g_hide;
uintptr_t HeroSoldierVisual(uintptr_t battle, uintptr_t unit, uintptr_t entity) {
    uintptr_t root = battle ? Ptr(battle + oBattleVisRoot) : 0, mgr = root ? Ptr(root + oVisRootMgr) : 0;
    uintptr_t units = mgr ? Ptr(mgr + oVisMgrUnits) : 0;
    uint32_t nu = 0;
    if (!units || !unit || !entity || !Rd(mgr + oVisMgrUnitCount, nu) || nu > kHideMaxUnits) return 0;
    for (uint32_t i = 0; i < nu; ++i) {
        uintptr_t uv = Ptr(units + 8 * i);
        uint32_t ns = 0;
        if (!uv || Ptr(uv + oUnitVisUnit) != unit || !Rd(uv + oUnitVisSoldierCount, ns) || ns > kHideMaxSoldiers) continue;
        uintptr_t sol = Ptr(uv + oUnitVisSoldiers);
        for (uint32_t j = 0; sol && j < ns; ++j) {
            uintptr_t sv = Ptr(sol + 8 * j);
            if (sv && Ptr(sv + oSoldierVisEntity) == entity) return sv;
        }
    }
    return 0;
}
uintptr_t HideParts(uintptr_t inst, int* nOut) {
    uint32_t n = 0;
    uintptr_t parts = inst ? Ptr(inst + oInstParts) : 0;
    if (!inst || !InExe(Ptr(inst)) || !parts || !Rd(inst + oInstPartCount, n) || n == 0 || n > (uint32_t)kHideMaxParts) return 0;
    *nOut = (int)n;
    return parts;
}
void HideSelfDrop() {
    g_hide.sv = g_hide.inst = g_hide.parts = 0;
    g_hide.n = 0;
}
void HideSelfRestore(const char* why) {
    if (!g_hide.inst) return;
    int n = 0;
    bool still = HeroSoldierVisual(S.battle, S.unit, S.entity) == g_hide.sv && Ptr(g_hide.sv + oSoldierVisInstance) == g_hide.inst &&
                 HideParts(g_hide.inst, &n) == g_hide.parts && n == g_hide.n;
    if (still)
        for (int k = 0; k < g_hide.n; ++k) Wr(g_hide.parts + kInstPartStride * k, g_hide.want[k]);
    Log("hide-self: hero model %s (%s; %d parts, game re-showed parts %u times)", still ? "shown again" : "gone - nothing restored", why, g_hide.n,
        g_hide.rewrites);
    HideSelfDrop();
}
void HideSelfStep() {
    if (!(S.active && cfg.hideSelf && S.entity)) {
        HideSelfRestore("first person ended");
        g_hide.warned = false;
        g_hide.tSearch = -1;
        return;
    }
    double now = Now();
    if (g_hide.inst) {
        int n = 0;
        bool same = Ptr(g_hide.sv + oSoldierVisInstance) == g_hide.inst && HideParts(g_hide.inst, &n) == g_hide.parts && n == g_hide.n;
        if (same && now - g_hide.tCheck > 1.0) {
            g_hide.tCheck = now;
            same = HeroSoldierVisual(S.battle, S.unit, S.entity) == g_hide.sv;
        }
        if (!same) {
            Log("hide-self: hero render instance changed (died / model swapped?); looking again");
            HideSelfDrop();
        }
    }
    if (!g_hide.inst) {
        if (g_hide.tSearch >= 0 && now - g_hide.tSearch < 0.5) return;
        g_hide.tSearch = now;
        uintptr_t sv = HeroSoldierVisual(S.battle, S.unit, S.entity);
        uintptr_t inst = sv ? Ptr(sv + oSoldierVisInstance) : 0;
        int n = 0;
        uintptr_t parts = HideParts(inst, &n);
        if (!parts) {
            if (!g_hide.warned) {
                g_hide.warned = true;
                Log("hide-self: hero render instance not found (battle %p unit %p entity %p visual %p instance %p); model not hidden", (void*)S.battle,
                    (void*)S.unit, (void*)S.entity, (void*)sv, (void*)inst);
            }
            return;
        }
        char flags[kHideMaxParts * 2 + 1] = "";
        for (int k = 0; k < n; ++k) {
            uint32_t v = 0;
            Rd(parts + kInstPartStride * k, v);
            g_hide.want[k] = v;
            flags[2 * k] = v > 9 ? '+' : (char)('0' + v);
            flags[2 * k + 1] = k + 1 < n ? ' ' : 0;
            if (v) Wr(parts + kInstPartStride * k, uint32_t(0));
        }
        g_hide.sv = sv;
        g_hide.inst = inst;
        g_hide.parts = parts;
        g_hide.n = n;
        g_hide.tCheck = now;
        g_hide.rewrites = 0;
        g_hide.warned = false;
        Log("hide-self: hero model hidden in our view (visual %p instance %p, %d parts, flags were %s)", (void*)sv, (void*)inst, n, flags);
        return;
    }
    for (int k = 0; k < g_hide.n; ++k) {
        uint32_t v = 0;
        if (Rd(g_hide.parts + kInstPartStride * k, v) && v) {
            g_hide.want[k] = v;
            Wr(g_hide.parts + kInstPartStride * k, uint32_t(0));
            if (g_hide.rewrites++ < 8) Log("hide-self: game showed part %d again (flag %u); hidden again", k, v);
        }
    }
}
constexpr uint32_t kArrowBoneTag = 0xF0000000u, kArrowMaxList = 1u << 16;
constexpr uint8_t kArrowAttached = 0x80;
constexpr int kArrowMaxHide = 64;
using FnArrowDraw = void (*)(uintptr_t mgr);
struct ArrowHide {
    uintptr_t mgr = 0, battle = 0, origVt = 0;
    uintptr_t* vtCopy = nullptr;
    FnArrowDraw orig = nullptr;
    volatile uintptr_t sv = 0;
    uintptr_t svLooked = 0;
    double tSearch = -1, tTry = -1, tCheck = -1;
    volatile LONG draws = 0, skipped = 0, most = 0;
    bool logged = false, warned = false;
} g_arrow;
void ArrowDraw(uintptr_t mgr) {
    uint32_t* bones[kArrowMaxHide];
    int n = 0;
    const uintptr_t sv = g_arrow.sv, vt2 = g_base + kVtArrowVisual2_904;
    if (sv) {
        const uint32_t cnt = *reinterpret_cast<const uint32_t*>(mgr + oArrowMgrCount);
        const uintptr_t* list = *reinterpret_cast<const uintptr_t* const*>(mgr + oArrowMgrList);
        for (uint32_t i = 0; list && cnt < kArrowMaxList && i < cnt && n < kArrowMaxHide; ++i) {
            const uintptr_t v = list[i];
            if (!v || *reinterpret_cast<const uintptr_t*>(v + oArrowSoldierVis) != sv || *reinterpret_cast<const uintptr_t*>(v + oArrowVt2) != vt2 ||
                !(*reinterpret_cast<const uint8_t*>(v + oArrowFlags) & kArrowAttached))
                continue;
            uint32_t* b = reinterpret_cast<uint32_t*>(v + oArrowBone);
            if (*b >= 0x10000u) continue;
            *b |= kArrowBoneTag;
            bones[n++] = b;
        }
    }
    g_arrow.orig(mgr);
    for (int k = 0; k < n; ++k)
        if ((*bones[k] & 0xFFFF0000u) == kArrowBoneTag) *bones[k] &= 0xFFFFu;
    InterlockedIncrement(&g_arrow.draws);
    if (n) {
        InterlockedExchangeAdd(&g_arrow.skipped, n);
        if (n > g_arrow.most) g_arrow.most = n;
    }
}
uintptr_t ArrowCurrentBattle() {
    uintptr_t ui = Ptr(g_base + kGlobalUi);
    return ui ? Ptr(ui + oUiBattle) : 0;
}
void ArrowUnhook(const char* why) {
    g_arrow.sv = 0;
    if (!g_arrow.mgr) return;
    const bool same = g_arrow.battle && ArrowCurrentBattle() == g_arrow.battle &&
                      Ptr(Ptr(g_arrow.battle + oBattleVisRoot) + oVisRootArrowMgr) == g_arrow.mgr;
    const bool ours = same && g_arrow.vtCopy && Ptr(g_arrow.mgr) == reinterpret_cast<uintptr_t>(g_arrow.vtCopy + 1);
    if (ours) Wr(g_arrow.mgr, g_arrow.origVt);
    Log("arrows: %s arrow visual manager %p (%s): draw calls %ld, stuck arrows skipped %ld (at most %ld at once)", ours ? "unhooked" : "dropped",
        (void*)g_arrow.mgr, why, g_arrow.draws, g_arrow.skipped, g_arrow.most);
    g_arrow.mgr = g_arrow.battle = g_arrow.origVt = 0;
    g_arrow.draws = g_arrow.skipped = g_arrow.most = 0;
}
bool ArrowHook(uintptr_t battle, uintptr_t mgr) {
    const uintptr_t vt = Ptr(mgr);
    uintptr_t lo = 0, hi = 0;
    if (vt != g_base + kVtArrowMgr904 || Ptr(vt + 8) != g_base + kFnArrowDraw904 || !CodeRange(&lo, &hi)) {
        if (!g_arrow.warned) {
            g_arrow.warned = true;
            Log("arrows: arrow visual manager %p vtable %p / [+8] %p not the 9.0.4 ones - stuck arrows stay visible", (void*)mgr, (void*)vt,
                (void*)(vt ? Ptr(vt + 8) : 0));
        }
        return false;
    }
    size_t slots = 0;
    while (slots < 1024) {
        uintptr_t f = Ptr(vt + 8 * slots);
        if (f < lo || f >= hi) break;
        ++slots;
    }
    if (slots < 2) return false;
    if (!g_arrow.vtCopy) {
        const size_t copyN = slots + 16 > 64 ? slots + 16 : 64;
        auto* c = static_cast<uintptr_t*>(VirtualAlloc(nullptr, (copyN + 1) * sizeof(uintptr_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!c) return false;
        c[0] = Ptr(vt - 8);
        for (size_t i = 0; i < copyN; ++i) c[1 + i] = i < slots ? Ptr(vt + 8 * i) : 0;
        g_arrow.orig = reinterpret_cast<FnArrowDraw>(c[1 + 1]);
        c[1 + 1] = reinterpret_cast<uintptr_t>(&ArrowDraw);
        DWORD old;
        VirtualProtect(c, (copyN + 1) * sizeof(uintptr_t), PAGE_READONLY, &old);
        g_arrow.vtCopy = c;
    }
    g_arrow.draws = g_arrow.skipped = g_arrow.most = 0;
    if (!Wr(mgr, reinterpret_cast<uintptr_t>(g_arrow.vtCopy + 1))) {
        Log("arrows: vtable write failed on arrow visual manager %p", (void*)mgr);
        return false;
    }
    g_arrow.mgr = mgr;
    g_arrow.battle = battle;
    g_arrow.origVt = vt;
    Log("arrows: hooked arrow visual manager %p (battle %p, vt +%llx, %zu slots) via vtable copy %p: arrows stuck in the hero are skipped while "
        "drawing (bone index masked during the draw call only)",
        (void*)mgr, (void*)battle, (unsigned long long)(vt - g_base), slots, (void*)(g_arrow.vtCopy + 1));
    return true;
}
void ArrowHideStep() {
    if (!(S.active && cfg.hideArrows && g_arrowOk && S.entity && S.battle)) {
        if (g_arrow.mgr) ArrowUnhook("first person ended");
        g_arrow.sv = 0;
        g_arrow.svLooked = 0;
        g_arrow.tSearch = g_arrow.tTry = g_arrow.tCheck = -1;
        g_arrow.logged = false;
        return;
    }
    const double now = Now();
    if (!g_arrow.mgr || g_arrow.tCheck < 0 || now - g_arrow.tCheck >= 0.5) {
        g_arrow.tCheck = now;
        const uintptr_t root = Ptr(S.battle + oBattleVisRoot), mgr = root ? Ptr(root + oVisRootArrowMgr) : 0;
        if (g_arrow.mgr && (mgr != g_arrow.mgr || S.battle != g_arrow.battle || Ptr(mgr) != reinterpret_cast<uintptr_t>(g_arrow.vtCopy + 1)))
            ArrowUnhook("arrow visual manager changed");
        if (!g_arrow.mgr && mgr && (g_arrow.tTry < 0 || now - g_arrow.tTry >= 2.0)) {
            g_arrow.tTry = now;
            ArrowHook(S.battle, mgr);
        }
    }
    uintptr_t sv = g_hide.sv;
    if (!sv) {
        if (g_arrow.tSearch < 0 || now - g_arrow.tSearch >= 0.5) {
            g_arrow.tSearch = now;
            g_arrow.svLooked = HeroSoldierVisual(S.battle, S.unit, S.entity);
        }
        sv = g_arrow.svLooked;
    }
    g_arrow.sv = g_arrow.mgr ? sv : 0;
    if (g_arrow.skipped && !g_arrow.logged) {
        g_arrow.logged = true;
        Log("arrows: first stuck arrow hidden in our view (hero soldier visual %p)", (void*)sv);
    }
}
struct GameVolState {
    float applied = -1.f;
    int fail = -1;
    bool logPending = false;
    double changedAt = 0.0;
    gamevol::Channel master{}, sfx{};
} g_gv;
void GameVolumeStep() {
    if (!g_gameVolOk) return;
    gamevol::Records recs{};
    gamevol::Channel m{}, s{};
    const uintptr_t mgr = Ptr(g_base + kGlobalAudioMgr904);
    int fail = !mgr ? 1 : Ptr(mgr) != g_base + kVtAudioMgr904 ? 2 : !Rd(mgr + gamevol::oChannels, recs) ? 3 : 0;
    float f = 1.f;
    if (!fail) fail = gamevol::Read(recs, &m, &s, &f);
    if (fail) f = 1.f;
    const double now = Now();
    if (fail != g_gv.fail) {
        if (fail)
            Log("audio: game volume not readable (%s, audio manager %p) - first-person sounds at fixed -5 dB", gamevol::FailName(fail), (void*)mgr);
        else
            g_gv.logPending = true, g_gv.changedAt = now;
        g_gv.fail = fail;
    }
    if (std::fabs(f - g_gv.applied) > 1e-4f) {
        vm::GunSound::SetGameGain(f);
        g_gv.applied = f, g_gv.changedAt = now, g_gv.logPending = !fail;
    }
    if (!fail) g_gv.master = m, g_gv.sfx = s;
    if (g_gv.logPending && now - g_gv.changedAt >= 0.5) {
        g_gv.logPending = false;
        char line[200];
        gamevol::Describe(g_gv.master, g_gv.sfx, g_gv.applied, -5.f, line, sizeof line);
        Log("audio: %s", line);
    }
}
extern "C" void hfps_after_cam_update(uintptr_t mgr) {
    const double tGameFrame = Now();
    GameVolumeStep();
    if (!g_loggedUpdateThread) {
        g_loggedUpdateThread = true;
        Log("camera update runs on thread %lu (window thread %lu)", GetCurrentThreadId(), g_windowThread);
    }
    const bool staleFp = InterlockedExchange(&g_staleFp, 0) != 0;
    InterlockedIncrement(&g_camBeat);
    if (g_fpDetached) {
        g_fpDetached = false;
        if (mgr == g_detachMgr && !staleFp) {
            S.active = true;
            Exit(mgr, "camera update resumed after the window-side drop");
        } else {
            S.exitStage = 0;
            Log("FPS: window-side drop confirmed - camera manager %p hooked anew (old %p); old battle left untouched", (void*)mgr,
                (void*)g_detachMgr);
        }
    }
    if (S.active && (S.mgr != mgr || staleFp)) {
        S.active = false;
        ReleaseMouse();
        vmgame::SetFirstPerson(false);
        const LONG oldExit = InterlockedExchange(&g_reqExit, 0), oldLauncher = InterlockedExchange(&g_reqLauncher, 0);
        S.exitStage = 0;
        Log("FPS state dropped: %s %p -> %p (stale exit request %ld, launcher request %ld cleared)",
            S.mgr != mgr ? "camera manager changed" : "camera manager re-created at the same address", (void*)S.mgr, (void*)mgr, oldExit, oldLauncher);
    }
    if (S.exitStage && (!S.active || S.exitStage == 1)) ExitSequenceStep(mgr);
    if (InterlockedExchange(&g_reqToggle, 0)) {
        if (S.active)
            Exit(mgr, "toggle-key");
        else
            Enter(mgr);
    }
    if (InterlockedExchange(&g_reqExit, 0)) Exit(mgr, "escape-or-focus");
    if (S.active) Apply(mgr);
    RevealStep(mgr);
    MeleeStep();
    NoKnockStep();
    UiStep();
    CursorStep();
    PathStep();
    EdgeStep();
    ShotStep();
    HideSelfStep();
    ArrowHideStep();
    vmgame::GameThreadTime((Now() - tGameFrame) * 1000.0);
}
#ifdef HFPS_LUA_MODULE
namespace luaapi {
constexpr int kTUserdata = 7;
constexpr int kTNumber = 3;
typedef int (*CFn)(void*);
struct Fns {
    void (*createtable)(void*, int, int);
    void (*pushcclosure)(void*, CFn, int);
    void (*setfield)(void*, int, const char*);
    void (*pushboolean)(void*, int);
    void (*pushnumber)(void*, float);
    void (*pushstring)(void*, const char*);
    void (*pushnil)(void*);
    void* (*touserdata)(void*, int);
    float (*tonumber)(void*, int);
    int (*type)(void*, int);
    size_t (*objlen)(void*, int);
} f;
int g_state = -1;
char g_missing[48] = "";
volatile LONG g_opens = 0, g_matchLogged = 0;
bool Resolve() {
    if (g_state >= 0) return g_state == 1;
    HMODULE exe = GetModuleHandleW(nullptr);
    const struct { void* slot; const char* name; } t[] = {
        {&f.createtable, "lua_createtable"}, {&f.pushcclosure, "lua_pushcclosure"}, {&f.setfield, "lua_setfield"},
        {&f.pushboolean, "lua_pushboolean"}, {&f.pushnumber, "lua_pushnumber"}, {&f.pushstring, "lua_pushstring"},
        {&f.pushnil, "lua_pushnil"}, {&f.touserdata, "lua_touserdata"}, {&f.tonumber, "lua_tonumber"}, {&f.type, "lua_type"},
        {&f.objlen, "lua_objlen"}};
    for (const auto& e : t) {
        FARPROC p = exe ? GetProcAddress(exe, e.name) : nullptr;
        if (!p) {
            snprintf(g_missing, sizeof g_missing, "%s", e.name);
            g_state = 0;
            return false;
        }
        memcpy(e.slot, &p, sizeof p);
    }
    g_state = 1;
    return true;
}
int Api(void* L) {
    f.pushnumber(L, 5.f);
    return 1;
}
int Active(void* L) {
    f.pushboolean(L, S.active && S.unit ? 1 : 0);
    return 1;
}
int IsHero(void* L) {
    const uintptr_t unit = S.unit, ent = S.entity;
    int how = 0;
    if (S.active && unit && f.type(L, 1) == kTUserdata && f.objlen(L, 1) >= sizeof(uintptr_t)) {
        const void* blk = f.touserdata(L, 1);
        uintptr_t wrap = 0;
        if (blk) memcpy(&wrap, blk, sizeof wrap);
        const uintptr_t p = wrap ? Ptr(wrap + 8) : 0;
        if (p && p == unit) {
            how = 1;
        } else if (p && ent) {
            uint32_t n = 0;
            uintptr_t arr = 0, first = 0;
            if (Rd(p + 0xa4, n) && n > 0 && n < 10000 && Rd(p + 0xa8, arr) && Rd(arr, first) && first == ent) how = 2;
        }
        if (!how && wrap && wrap == unit) how = 3;
        if (how && InterlockedCompareExchange(&g_matchLogged, how, 0) == 0)
            Log("lua-api: is_hero matched %s (unit %p, entity %p)",
                how == 1 ? "by [wrapper+8] unit pointer" : how == 2 ? "by first soldier" : "by wrapper pointer", (void*)unit, (void*)ent);
    }
    f.pushboolean(L, how ? 1 : 0);
    return 1;
}
int HudEnabled(void* L) {
    f.pushboolean(L, cfg.hudEnabled ? 1 : 0);
    return 1;
}
int SetHudRect(void* L) {
    float v[6] = {};
    bool ok = cfg.hudEnabled && cfg.hudCover;
    for (int i = 0; ok && i < 6; ++i) {
        if (f.type(L, i + 1) != kTNumber) ok = false;
        else v[i] = f.tonumber(L, i + 1);
    }
    if (ok && v[4] > 16.f && v[5] > 16.f && v[2] > 1.f && v[3] > 1.f)
        vmgame::SetHudCover(v[0] / v[4], v[1] / v[5], (v[0] + v[2]) / v[4], (v[1] + v[3]) / v[5]);
    else
        vmgame::SetHudCover(0, 0, 0, 0);
    return 0;
}
int SetUiCoversApi(void* L) {
    float r[32 * 4];
    int n = 0;
    float sw = 0, sh = 0;
    if (cfg.hudEnabled && cfg.hudCover && f.type(L, 1) == kTNumber && f.type(L, 2) == kTNumber) sw = f.tonumber(L, 1), sh = f.tonumber(L, 2);
    for (int i = 0; sw > 16.f && sh > 16.f && n < 32; ++i) {
        float v[4];
        bool ok = true;
        for (int k = 0; ok && k < 4; ++k) {
            if (f.type(L, 3 + i * 4 + k) != kTNumber) ok = false;
            else v[k] = f.tonumber(L, 3 + i * 4 + k);
        }
        if (!ok) break;
        if (v[2] <= 1.f || v[3] <= 1.f) continue;
        r[n * 4] = v[0] / sw, r[n * 4 + 1] = v[1] / sh, r[n * 4 + 2] = (v[0] + v[2]) / sw, r[n * 4 + 3] = (v[1] + v[3]) / sh;
        ++n;
    }
    vmgame::SetUiCovers(r, n);
    return 0;
}
int Ammo(void* L) {
    int ammo = 0, size = 0, reserve = -1;
    bool reloading = false;
    vmgame::AmmoInfo(&ammo, &size, &reloading, &reserve);
    f.pushnumber(L, static_cast<float>(ammo));
    f.pushnumber(L, static_cast<float>(size));
    f.pushboolean(L, reloading ? 1 : 0);
    f.pushboolean(L, vmgame::MeleeActive() ? 1 : 0);
    f.pushnumber(L, static_cast<float>(reserve));
    return 5;
}
int TacStaminaApi(void* L) {
    f.pushnumber(L, S.active ? g_tac.st : 1.f);
    f.pushboolean(L, TacStaminaOk() ? 1 : 0);
    f.pushboolean(L, vmgame::InfTacStamina() ? 1 : 0);
    f.pushboolean(L, S.active && S.tac ? 1 : 0);
    return 4;
}
int CastEvent(void* L) {
    LONG t = g_cast.tail, e = 0;
    if (t != g_cast.head) {
        e = g_cast.q[t & 15];
        InterlockedExchange(&g_cast.tail, t + 1);
    }
    f.pushnumber(L, static_cast<float>(S.active ? e : 0));
    return 1;
}
int RefillAmmoApi(void* L) {
    if (f.type(L, 1) == kTNumber) {
        float frac = f.tonumber(L, 1);
        Log("lua-api: refill_ammo(%.3f)", frac);
        vmgame::RefillAmmo(frac);
    }
    return 0;
}
int SetArmed(void* L) {
    bool on = f.type(L, 1) == kTNumber && f.tonumber(L, 1) != 0.f;
    InterlockedExchange(&g_cast.armed, on && S.active ? 1 : 0);
    return 0;
}
int View(void* L) {
    float e[3] = {}, d[3] = {};
    LONG s1 = 0, s2 = 0;
    for (int i = 0; i < 4; ++i) {
        s1 = g_cast.viewSeq;
        memcpy(e, g_cast.eye, sizeof e);
        memcpy(d, g_cast.fwd, sizeof d);
        s2 = g_cast.viewSeq;
        if (s1 == s2 && !(s1 & 1)) break;
    }
    if (!S.active || s1 <= 0 || s1 != s2 || (s1 & 1)) {
        f.pushnil(L);
        return 1;
    }
    for (float v : e) f.pushnumber(L, v);
    for (float v : d) f.pushnumber(L, v);
    return 6;
}
int CastMarkerApi(void* L) {
    float v[6] = {};
    for (int i = 0; i < 6; ++i) v[i] = f.type(L, i + 1) == kTNumber ? f.tonumber(L, i + 1) : 0.f;
    int shape = static_cast<int>(v[0]);
    AcquireSRWLockExclusive(&g_cms.lock);
    g_cms.req.shape = S.active && shape >= cm::kRing && shape <= cm::kSelfRing ? shape : cm::kNone;
    g_cms.req.r0 = std::fmax(0.f, v[1]), g_cms.req.r1 = std::fmax(0.f, v[2]), g_cms.req.len = std::fmax(0.f, v[3]);
    g_cms.req.range = std::fmax(0.f, v[4]), g_cms.req.over = v[5] != 0.f;
    if (g_cms.req.shape == cm::kNone) g_cms.aimOk = false;
    ++g_cms.gen;
    ReleaseSRWLockExclusive(&g_cms.lock);
    return 0;
}
int CastTargetApi(void* L) {
    uintptr_t unit = 0;
    if (S.active && f.type(L, 1) == kTUserdata && f.objlen(L, 1) >= sizeof(uintptr_t)) {
        const void* blk = f.touserdata(L, 1);
        uintptr_t wrap = 0;
        if (blk) memcpy(&wrap, blk, sizeof wrap);
        unit = wrap ? Ptr(wrap + 8) : 0;
    }
    int tone = f.type(L, 2) == kTNumber ? static_cast<int>(f.tonumber(L, 2)) : 0;
    float ring = f.type(L, 3) == kTNumber ? f.tonumber(L, 3) : 0.f;
    AcquireSRWLockExclusive(&g_cms.lock);
    if (unit != g_cms.unit) ++g_cms.gen;
    g_cms.unit = unit;
    g_cms.req.unitTone = tone == 1 ? cm::kWhite : tone == 2 ? cm::kGrey : cm::kRed;
    g_cms.req.unitRing = std::fmax(0.f, ring);
    ReleaseSRWLockExclusive(&g_cms.lock);
    return 0;
}
int CastPointApi(void* L) {
    float p[3], d, r;
    bool ok, edge;
    AcquireSRWLockShared(&g_cms.lock);
    ok = g_cms.aimOk && S.active, memcpy(p, g_cms.aim, sizeof p), d = g_cms.aimDist, edge = g_cms.aimEdge, r = g_cms.aimRange;
    ReleaseSRWLockShared(&g_cms.lock);
    if (!ok) {
        f.pushnil(L);
        return 1;
    }
    for (float v : p) f.pushnumber(L, v);
    f.pushnumber(L, d);
    f.pushboolean(L, edge ? 1 : 0);
    f.pushnumber(L, r);
    return 6;
}
int Prompt(void* L) {
    f.pushnumber(L, S.active && S.lastMode == 0 ? static_cast<float>(S.prompt) : 0.f);
    return 1;
}
int Refused(void* L) {
    f.pushnumber(L, static_cast<float>(g_refuseSeq));
    f.pushnumber(L, static_cast<float>(g_refuseCode));
    return 2;
}
int Status(void* L) {
    f.pushnumber(L, static_cast<float>(g_status));
    return 1;
}
int ExitFirstPerson(void* L) {
    (void)L;
    if (S.active) {
        InterlockedExchange(&g_reqExit, 1);
        Log("cast: script asked to leave first person (transformation ability)");
    }
    return 0;
}
int PushTable(void* L) {
    LONG n = InterlockedIncrement(&g_opens);
    if (!Resolve()) {
        Log("lua-api: %s not exported by the game - no interface table (luaopen #%ld)", g_missing, n);
        return 0;
    }
    f.createtable(L, 0, 11);
    const struct { const char* name; CFn fn; } fns[] = {{"api", Api}, {"active", Active}, {"is_hero", IsHero}, {"hud_enabled", HudEnabled},
                                                        {"set_hud_rect", SetHudRect}, {"set_ui_covers", SetUiCoversApi}, {"ammo", Ammo}, {"cast_event", CastEvent},
                                                        {"set_armed", SetArmed}, {"view", View}, {"exit_first_person", ExitFirstPerson},
                                                        {"tac_stamina", TacStaminaApi}, {"refill_ammo", RefillAmmoApi}, {"prompt", Prompt}, {"refused", Refused}, {"status", Status},
                                                        {"cast_marker", CastMarkerApi}, {"cast_target", CastTargetApi}, {"cast_point", CastPointApi}};
    for (const auto& e : fns) {
        f.pushcclosure(L, e.fn, 0);
        f.setfield(L, -2, e.name);
    }
    Log("lua-api: interface table v4 returned (luaopen #%ld, lua state %p)", n, L);
    return 1;
}
}
static volatile LONG g_luaStarted = 0;
static bool g_pinned = false;
extern "C" __declspec(dllexport) int luaopen_tenzin_fps_native(void* L) {
    if (InterlockedCompareExchange(&g_luaStarted, 1, 0) == 0) {
        HMODULE self = nullptr;
        g_pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                      reinterpret_cast<LPCWSTR>(&luaopen_tenzin_fps_native), &self) != FALSE;
        if (!g_pinned) return 0;
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return g_pinned && L ? luaapi::PushTable(L) : 0;
}
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, g_dir, MAX_PATH);
        wchar_t* slash = wcsrchr(g_dir, L'\\');
        if (slash) *slash = 0;
    } else if (reason == DLL_PROCESS_DETACH && !reserved && g_log) {
        fprintf(g_log, "loader: DLL unloaded by FreeLibrary (pinned=%d) - hooks would dangle\n", g_pinned ? 1 : 0);
        fflush(g_log);
    }
    return TRUE;
}
#else
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameW(inst, g_dir, MAX_PATH);
        wchar_t* slash = wcsrchr(g_dir, L'\\');
        if (slash) *slash = 0;
        if (!LoadRealVersion()) return FALSE;
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
#endif
