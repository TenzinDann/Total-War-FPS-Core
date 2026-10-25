#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmreg.h>
struct IXAudio2SourceVoice;
namespace vm {
class GunSound {
public:
    bool Load(const wchar_t* dir, const wchar_t* list, int maxVoices, char* err, size_t errLen);
    void Play(float volume, bool loop = false, float pitch = 1.f);
    void FadeAll();
    void Update();
    void Release();
    static bool ReadPcm(const wchar_t* path, WAVEFORMATEX* fmt, unsigned char** pcm, unsigned* bytes, char* err, size_t errLen);
    static int Prefetch(const wchar_t* dir, const wchar_t* list, void** handles, int cap);
    static void Unprefetch(void* handle);
    static bool WarmEngine(char* err, size_t errLen);
    static float MasterVolume();
    static void SetGameGain(float gain);
    static float GameGain();
    bool Loaded() const { return nPcm_ > 0; }
    int Variants() const { return nPcm_; }
    float Seconds() const;
private:
    static constexpr int kVoices = 24;
    static constexpr int kMaxVariants = 24;
    bool LoadOne(const wchar_t* wavPath, char* err, size_t errLen);
    static bool AcquirePcm(const wchar_t* path, WAVEFORMATEX* fmt, unsigned char** pcm, unsigned* bytes, char* err, size_t errLen);
    static void ReleasePcm(unsigned char* pcm);
    WAVEFORMATEX fmt_ = {};
    unsigned char* pcmV_[kMaxVariants] = {};
    unsigned bytesV_[kMaxVariants] = {};
    int nPcm_ = 0, last_ = -1, maxVoices_ = kVoices;
    unsigned rng_ = 0;
    IXAudio2SourceVoice* voices_[kVoices] = {};
    double started_[kVoices] = {};
    double fadeAt_[kVoices] = {};
    float vol_[kVoices] = {};
    int nVoices_ = 0;
};
}
