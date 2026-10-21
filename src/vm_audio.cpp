#include "vm_audio.h"
#include <xaudio2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace vm {
namespace {
IXAudio2* g_xa = nullptr;
IXAudio2MasteringVoice* g_master = nullptr;
bool g_xaTried = false, g_xaOk = false;
bool Engine(char* err, size_t errLen) {
    if (g_xaTried) {
        if (!g_xaOk) snprintf(err, errLen, "xaudio2 unavailable");
        return g_xaOk;
    }
    g_xaTried = true;
    if (HMODULE ole = LoadLibraryW(L"ole32.dll")) {
        using FnIncMta = HRESULT(WINAPI*)(void**);
        FnIncMta inc = reinterpret_cast<FnIncMta>(GetProcAddress(ole, "CoIncrementMTAUsage"));
        void* cookie = nullptr;
        if (inc) inc(&cookie);
    }
    HMODULE m = LoadLibraryW(L"xaudio2_9.dll");
    if (!m) {
        snprintf(err, errLen, "xaudio2_9.dll not found (%lu)", GetLastError());
        return false;
    }
    using FnCreate = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    FnCreate create = reinterpret_cast<FnCreate>(GetProcAddress(m, "XAudio2Create"));
    HRESULT hr = create ? create(&g_xa, 0, XAUDIO2_DEFAULT_PROCESSOR) : E_FAIL;
    if (FAILED(hr) || !g_xa) {
        snprintf(err, errLen, "XAudio2Create failed 0x%08lx", static_cast<unsigned long>(hr));
        g_xa = nullptr;
        return false;
    }
    hr = g_xa->CreateMasteringVoice(&g_master);
    if (FAILED(hr) || !g_master) {
        snprintf(err, errLen, "CreateMasteringVoice failed 0x%08lx", static_cast<unsigned long>(hr));
        g_xa->Release();
        g_xa = nullptr;
        return false;
    }
    g_xaOk = true;
    return true;
}
double NowS() {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
}
}
bool GunSound::Load(const wchar_t* dir, const wchar_t* list, int maxVoices, char* err, size_t errLen) {
    Release();
    wchar_t buf[1024];
    wcsncpy(buf, list, 1023);
    buf[1023] = 0;
    for (wchar_t *tok = buf, *next = nullptr; tok && *tok && nPcm_ < kMaxVariants; tok = next) {
        next = wcschr(tok, L';');
        if (next) *next++ = 0;
        while (*tok == L' ') ++tok;
        if (!*tok) continue;
        wchar_t path[MAX_PATH * 2];
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, tok);
        if (!LoadOne(path, err, errLen)) {
            Release();
            return false;
        }
    }
    if (!nPcm_) {
        snprintf(err, errLen, "no sound file in list");
        return false;
    }
    maxVoices_ = maxVoices < 1 ? 1 : maxVoices > kVoices ? kVoices : maxVoices;
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    rng_ = static_cast<unsigned>(c.QuadPart) | 1u;
    if (!Engine(err, errLen)) {
        Release();
        return false;
    }
    const int nv = maxVoices_ + 2 > kVoices ? kVoices : maxVoices_ + 2;
    for (int i = 0; i < nv; ++i) {
        if (FAILED(g_xa->CreateSourceVoice(&voices_[i], &fmt_)) || !voices_[i]) break;
        started_[i] = 0;
        fadeAt_[i] = 0;
        ++nVoices_;
    }
    if (!nVoices_) {
        Release();
        snprintf(err, errLen, "CreateSourceVoice failed");
        return false;
    }
    return true;
}
bool GunSound::LoadOne(const wchar_t* wavPath, char* err, size_t errLen) {
    HANDLE h = CreateFileW(wavPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(err, errLen, "cannot open sound file (%lu)", GetLastError());
        return false;
    }
    DWORD size = GetFileSize(h, nullptr), got = 0;
    unsigned char* b = (size > 44 && size < (64u << 20)) ? static_cast<unsigned char*>(malloc(size)) : nullptr;
    bool rd = b && ReadFile(h, b, size, &got, nullptr) && got == size;
    CloseHandle(h);
    if (!rd || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) {
        free(b);
        snprintf(err, errLen, "not a RIFF/WAVE file");
        return false;
    }
    const unsigned char *fmt = nullptr, *data = nullptr;
    unsigned fmtLen = 0, dataLen = 0;
    for (unsigned i = 12; i + 8 <= size;) {
        unsigned cl = 0;
        memcpy(&cl, b + i + 4, 4);
        if (cl > size - i - 8) cl = size - i - 8;
        if (!memcmp(b + i, "fmt ", 4)) fmt = b + i + 8, fmtLen = cl;
        if (!memcmp(b + i, "data", 4)) data = b + i + 8, dataLen = cl;
        i += 8 + cl + (cl & 1);
    }
    WAVEFORMATEX f = {};
    if (fmt && fmtLen >= 16) memcpy(&f, fmt, 16);
    if (!fmt || !data || f.wFormatTag != WAVE_FORMAT_PCM || f.wBitsPerSample != 16 || f.nChannels < 1 || f.nChannels > 2 ||
        f.nSamplesPerSec < 8000 || f.nSamplesPerSec > 192000 || !dataLen) {
        free(b);
        snprintf(err, errLen, "unsupported wav (need PCM 16-bit mono/stereo): tag %u bits %u ch %u", f.wFormatTag,
                 f.wBitsPerSample, f.nChannels);
        return false;
    }
    f.cbSize = 0;
    f.nBlockAlign = static_cast<WORD>(f.nChannels * 2);
    f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
    dataLen -= dataLen % f.nBlockAlign;
    if (nPcm_ > 0 && (f.nChannels != fmt_.nChannels || f.nSamplesPerSec != fmt_.nSamplesPerSec)) {
        free(b);
        snprintf(err, errLen, "variant format differs (%u ch %lu Hz vs %u ch %lu Hz)", f.nChannels,
                 static_cast<unsigned long>(f.nSamplesPerSec), fmt_.nChannels, static_cast<unsigned long>(fmt_.nSamplesPerSec));
        return false;
    }
    unsigned char* pcm = static_cast<unsigned char*>(malloc(dataLen));
    if (!pcm) {
        free(b);
        snprintf(err, errLen, "out of memory");
        return false;
    }
    memcpy(pcm, data, dataLen);
    free(b);
    pcmV_[nPcm_] = pcm;
    bytesV_[nPcm_] = dataLen;
    ++nPcm_;
    fmt_ = f;
    return true;
}
float GunSound::Seconds() const {
    return fmt_.nAvgBytesPerSec && nPcm_ ? static_cast<float>(bytesV_[0]) / static_cast<float>(fmt_.nAvgBytesPerSec) : 0.f;
}
void GunSound::Play(float volume, bool loop, float pitch) {
    if (!nPcm_ || !nVoices_) return;
    int vi = 0;
    if (nPcm_ > 1) {
        rng_ = rng_ * 1664525u + 1013904223u;
        vi = static_cast<int>((rng_ >> 8) % static_cast<unsigned>(nPcm_ - 1));
        if (last_ >= 0 && vi >= last_) ++vi;
    }
    last_ = vi;
    double now = NowS();
    int busy = 0, oldest = -1, freeV = -1;
    for (int i = 0; i < nVoices_; ++i) {
        XAUDIO2_VOICE_STATE st = {};
        voices_[i]->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued == 0) {
            fadeAt_[i] = 0;
            if (freeV < 0) freeV = i;
        } else if (fadeAt_[i] <= 0) {
            ++busy;
            if (oldest < 0 || started_[i] < started_[oldest]) oldest = i;
        }
    }
    if (busy >= maxVoices_ && oldest >= 0) fadeAt_[oldest] = now;
    int pick = freeV;
    if (pick < 0) {
        for (int i = 0; i < nVoices_; ++i)
            if (pick < 0 || started_[i] < started_[pick]) pick = i;
        voices_[pick]->Stop(0);
        voices_[pick]->FlushSourceBuffers();
    }
    fadeAt_[pick] = 0;
    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = bytesV_[vi];
    buf.pAudioData = pcmV_[vi];
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (loop) buf.LoopCount = XAUDIO2_LOOP_INFINITE;
    if (volume < 0.f) volume = 0.f;
    if (volume > 2.f) volume = 2.f;
    IXAudio2SourceVoice* v = voices_[pick];
    v->SetVolume(volume);
    v->SetFrequencyRatio(pitch < 0.25f ? 0.25f : (pitch > 2.f ? 2.f : pitch));
    vol_[pick] = volume;
    if (SUCCEEDED(v->SubmitSourceBuffer(&buf))) {
        v->Start(0);
        started_[pick] = now;
    }
}
void GunSound::FadeAll() {
    double now = NowS();
    for (int i = 0; i < nVoices_; ++i) {
        XAUDIO2_VOICE_STATE st = {};
        voices_[i]->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued && fadeAt_[i] <= 0) fadeAt_[i] = now;
    }
}
void GunSound::Update() {
    if (!nVoices_) return;
    double now = NowS();
    for (int i = 0; i < nVoices_; ++i) {
        if (fadeAt_[i] <= 0) continue;
        float t = static_cast<float>((now - fadeAt_[i]) / 0.06);
        if (t >= 1.f) {
            voices_[i]->Stop(0);
            voices_[i]->FlushSourceBuffers();
            fadeAt_[i] = 0;
        } else {
            voices_[i]->SetVolume(vol_[i] * (1.f - t));
        }
    }
}
void GunSound::Release() {
    for (int i = 0; i < kVoices; ++i) {
        if (voices_[i]) {
            voices_[i]->DestroyVoice();
            voices_[i] = nullptr;
        }
    }
    nVoices_ = 0;
    for (int k = 0; k < nPcm_; ++k) {
        free(pcmV_[k]);
        pcmV_[k] = nullptr;
        bytesV_[k] = 0;
    }
    nPcm_ = 0;
    last_ = -1;
}
}
