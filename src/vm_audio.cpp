#include "vm_audio.h"
#include "flac_decode.h"
#include <xaudio2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace vm {
namespace {
IXAudio2* g_xa = nullptr;
IXAudio2MasteringVoice* g_master = nullptr;
bool g_xaTried = false, g_xaOk = false;
constexpr float kMasterGain = 0.56234133f;
float g_gameGain = 1.f;
IXAudio2SourceVoice* g_clock = nullptr;
unsigned g_clockRate = 0;
alignas(16) unsigned char g_zero[65536] = {};
struct PassClock final : IXAudio2EngineCallback {
    volatile LONG seq = 0;
    volatile LONG started = 0;
    volatile LONG endIdx = 0;
    volatile long long endAt = -1;
    volatile long long quantum = 0;
    void STDMETHODCALLTYPE OnProcessingPassStart() override { InterlockedIncrement(&started); }
    void STDMETHODCALLTYPE OnProcessingPassEnd() override {
        if (!g_clock) return;
        XAUDIO2_VOICE_STATE st = {};
        g_clock->GetState(&st, 0);
        long long s = static_cast<long long>(st.SamplesPlayed);
        InterlockedIncrement(&seq);
        if (endAt >= 0 && s > endAt && s - endAt <= 8192) quantum = s - endAt;
        endAt = s;
        endIdx = started;
        InterlockedIncrement(&seq);
    }
    void STDMETHODCALLTYPE OnCriticalError(HRESULT) override {}
} g_pass;
bool EngineLocked(char* err, size_t errLen) {
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
    g_master->SetVolume(kMasterGain * g_gameGain);
    {
        XAUDIO2_VOICE_DETAILS d = {};
        g_master->GetVoiceDetails(&d);
        WAVEFORMATEX cf = {};
        cf.wFormatTag = WAVE_FORMAT_PCM;
        cf.nChannels = 1;
        cf.nSamplesPerSec = d.InputSampleRate >= 8000 ? d.InputSampleRate : 48000;
        cf.wBitsPerSample = 16;
        cf.nBlockAlign = 2;
        cf.nAvgBytesPerSec = cf.nSamplesPerSec * 2;
        if (SUCCEEDED(g_xa->CreateSourceVoice(&g_clock, &cf)) && g_clock) {
            XAUDIO2_BUFFER zb = {};
            zb.AudioBytes = sizeof g_zero;
            zb.pAudioData = g_zero;
            zb.LoopCount = XAUDIO2_LOOP_INFINITE;
            g_clock->SetVolume(0.f);
            if (SUCCEEDED(g_clock->SubmitSourceBuffer(&zb)) && SUCCEEDED(g_clock->Start(0))) {
                g_clockRate = cf.nSamplesPerSec;
                g_xa->RegisterForCallbacks(&g_pass);
            } else {
                g_clock->DestroyVoice();
                g_clock = nullptr;
            }
        } else {
            g_clock = nullptr;
        }
    }
    g_xaOk = true;
    return true;
}
SRWLOCK g_xaLock = SRWLOCK_INIT;
bool Engine(char* err, size_t errLen) {
    AcquireSRWLockExclusive(&g_xaLock);
    bool ok = EngineLocked(err, errLen);
    ReleaseSRWLockExclusive(&g_xaLock);
    return ok;
}
double NowS() {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
}
struct PcmEntry {
    wchar_t path[MAX_PATH * 2];
    DWORD sizeLo;
    FILETIME stamp;
    WAVEFORMATEX fmt;
    unsigned char* pcm;
    unsigned bytes;
    int refs;
};
PcmEntry* g_pcm = nullptr;
int g_pcmN = 0, g_pcmCap = 0;
SRWLOCK g_pcmLock = SRWLOCK_INIT;
int FindPcm(const wchar_t* full, const WIN32_FILE_ATTRIBUTE_DATA& fa) {
    for (int i = 0; i < g_pcmN; ++i)
        if (g_pcm[i].sizeLo == fa.nFileSizeLow && !CompareFileTime(&g_pcm[i].stamp, &fa.ftLastWriteTime) && !_wcsicmp(g_pcm[i].path, full))
            return i;
    return -1;
}
}
bool GunSound::AcquirePcm(const wchar_t* path, WAVEFORMATEX* fmt, unsigned char** pcm, unsigned* bytes, char* err, size_t errLen) {
    wchar_t full[MAX_PATH * 2];
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    DWORD n = GetFullPathNameW(path, MAX_PATH * 2, full, nullptr);
    if (!n || n >= MAX_PATH * 2 || !GetFileAttributesExW(full, GetFileExInfoStandard, &fa))
        return ReadPcm(path, fmt, pcm, bytes, err, errLen);
    AcquireSRWLockExclusive(&g_pcmLock);
    int i = FindPcm(full, fa);
    if (i >= 0) {
        ++g_pcm[i].refs;
        *fmt = g_pcm[i].fmt;
        *pcm = g_pcm[i].pcm;
        *bytes = g_pcm[i].bytes;
        ReleaseSRWLockExclusive(&g_pcmLock);
        return true;
    }
    ReleaseSRWLockExclusive(&g_pcmLock);
    if (!ReadPcm(full, fmt, pcm, bytes, err, errLen)) return false;
    AcquireSRWLockExclusive(&g_pcmLock);
    i = FindPcm(full, fa);
    if (i >= 0) {
        free(*pcm);
        ++g_pcm[i].refs;
        *fmt = g_pcm[i].fmt;
        *pcm = g_pcm[i].pcm;
        *bytes = g_pcm[i].bytes;
    } else {
        if (g_pcmN == g_pcmCap) {
            int cap = g_pcmCap ? g_pcmCap * 2 : 256;
            PcmEntry* grown = static_cast<PcmEntry*>(realloc(g_pcm, sizeof(PcmEntry) * cap));
            if (grown) g_pcm = grown, g_pcmCap = cap;
        }
        if (g_pcmN < g_pcmCap) {
            PcmEntry& e = g_pcm[g_pcmN++];
            wcscpy(e.path, full);
            e.sizeLo = fa.nFileSizeLow;
            e.stamp = fa.ftLastWriteTime;
            e.fmt = *fmt;
            e.pcm = *pcm;
            e.bytes = *bytes;
            e.refs = 1;
        }
    }
    ReleaseSRWLockExclusive(&g_pcmLock);
    return true;
}
void GunSound::ReleasePcm(unsigned char* pcm) {
    if (!pcm) return;
    AcquireSRWLockExclusive(&g_pcmLock);
    for (int i = 0; i < g_pcmN; ++i) {
        if (g_pcm[i].pcm != pcm) continue;
        if (--g_pcm[i].refs <= 0) {
            free(pcm);
            g_pcm[i] = g_pcm[--g_pcmN];
        }
        ReleaseSRWLockExclusive(&g_pcmLock);
        return;
    }
    ReleaseSRWLockExclusive(&g_pcmLock);
    free(pcm);
}
int GunSound::Prefetch(const wchar_t* dir, const wchar_t* list, void** handles, int cap) {
    wchar_t buf[1024];
    wcsncpy(buf, list, 1023);
    buf[1023] = 0;
    int n = 0, files = 0;
    for (wchar_t *tok = buf, *next = nullptr; tok && *tok && files < kMaxVariants && n < cap; tok = next) {
        next = wcschr(tok, L';');
        if (next) *next++ = 0;
        while (*tok == L' ') ++tok;
        if (!*tok) continue;
        ++files;
        wchar_t path[MAX_PATH * 2];
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls", dir, tok);
        WAVEFORMATEX f = {};
        unsigned char* pcm = nullptr;
        unsigned bytes = 0;
        char err[128];
        if (AcquirePcm(path, &f, &pcm, &bytes, err, sizeof err)) handles[n++] = pcm;
    }
    return n;
}
void GunSound::Unprefetch(void* handle) { ReleasePcm(static_cast<unsigned char*>(handle)); }
bool GunSound::WarmEngine(char* err, size_t errLen) { return Engine(err, errLen); }
float GunSound::MasterVolume() {
    AcquireSRWLockExclusive(&g_xaLock);
    float v = -1.f;
    if (g_xaOk && g_master) g_master->GetVolume(&v);
    ReleaseSRWLockExclusive(&g_xaLock);
    return v;
}
void GunSound::SetGameGain(float gain) {
    if (!(gain >= 0.f)) gain = 1.f;
    if (gain > 1.f) gain = 1.f;
    AcquireSRWLockExclusive(&g_xaLock);
    g_gameGain = gain;
    if (g_xaOk && g_master) g_master->SetVolume(kMasterGain * gain);
    ReleaseSRWLockExclusive(&g_xaLock);
}
float GunSound::GameGain() {
    AcquireSRWLockExclusive(&g_xaLock);
    float g = g_gameGain;
    ReleaseSRWLockExclusive(&g_xaLock);
    return g;
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
bool GunSound::ReadPcm(const wchar_t* path, WAVEFORMATEX* fmtOut, unsigned char** pcmOut, unsigned* bytesOut, char* err, size_t errLen) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(err, errLen, "cannot open sound file (%lu)", GetLastError());
        return false;
    }
    DWORD size = GetFileSize(h, nullptr), got = 0;
    unsigned char* b = (size > 44 && size < (64u << 20)) ? static_cast<unsigned char*>(malloc(size)) : nullptr;
    bool rd = b && ReadFile(h, b, size, &got, nullptr) && got == size;
    CloseHandle(h);
    if (rd && !memcmp(b, "fLaC", 4)) {
        unsigned ch = 0, rate = 0;
        unsigned char* pcm = nullptr;
        size_t pcmBytes = 0;
        bool dec = flac::Decode16(b, size, &ch, &rate, &pcm, &pcmBytes, err, errLen);
        free(b);
        if (!dec) return false;
        if (rate < 8000 || rate > 192000 || pcmBytes > 0xFFFFFFFFu) {
            free(pcm);
            snprintf(err, errLen, "flac: unsupported rate %u", rate);
            return false;
        }
        WAVEFORMATEX f = {};
        f.wFormatTag = WAVE_FORMAT_PCM;
        f.nChannels = static_cast<WORD>(ch);
        f.nSamplesPerSec = rate;
        f.wBitsPerSample = 16;
        f.nBlockAlign = static_cast<WORD>(ch * 2);
        f.nAvgBytesPerSec = rate * f.nBlockAlign;
        *fmtOut = f;
        *pcmOut = pcm;
        *bytesOut = static_cast<unsigned>(pcmBytes);
        return true;
    }
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
    unsigned char* pcm = static_cast<unsigned char*>(malloc(dataLen));
    if (!pcm) {
        free(b);
        snprintf(err, errLen, "out of memory");
        return false;
    }
    memcpy(pcm, data, dataLen);
    free(b);
    *fmtOut = f;
    *pcmOut = pcm;
    *bytesOut = dataLen;
    return true;
}
bool GunSound::LoadOne(const wchar_t* wavPath, char* err, size_t errLen) {
    WAVEFORMATEX f = {};
    unsigned char* pcm = nullptr;
    unsigned bytes = 0;
    if (!AcquirePcm(wavPath, &f, &pcm, &bytes, err, errLen)) return false;
    if (nPcm_ > 0 && (f.nChannels != fmt_.nChannels || f.nSamplesPerSec != fmt_.nSamplesPerSec)) {
        ReleasePcm(pcm);
        snprintf(err, errLen, "variant format differs (%u ch %lu Hz vs %u ch %lu Hz)", f.nChannels,
                 static_cast<unsigned long>(f.nSamplesPerSec), fmt_.nChannels, static_cast<unsigned long>(fmt_.nSamplesPerSec));
        return false;
    }
    pcmV_[nPcm_] = pcm;
    bytesV_[nPcm_] = bytes;
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
    if (loop && loopBegin_ > 0.f && fmt_.nBlockAlign) {
        unsigned frames = buf.AudioBytes / fmt_.nBlockAlign;
        unsigned lb = static_cast<unsigned>(loopBegin_ * static_cast<float>(fmt_.nSamplesPerSec));
        if (lb > 0 && lb + 64 < frames) buf.LoopBegin = lb, buf.LoopLength = frames - lb;
    }
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
long long GunSound::ClockSamples() {
    if (!g_clock || !g_clockRate) return -1;
    for (int i = 0; i < 16; ++i) {
        LONG q1 = g_pass.seq;
        MemoryBarrier();
        if (q1 & 1) {
            YieldProcessor();
            continue;
        }
        long long at = g_pass.endAt, qn = g_pass.quantum;
        LONG idx = g_pass.endIdx, s = g_pass.started;
        MemoryBarrier();
        if (g_pass.seq != q1) continue;
        if (at < 0) break;
        if (qn <= 0) qn = g_clockRate / 100;
        return at + static_cast<long long>(s - idx) * qn;
    }
    XAUDIO2_VOICE_STATE st = {};
    g_clock->GetState(&st, 0);
    return static_cast<long long>(st.SamplesPlayed);
}
unsigned GunSound::ClockRate() { return g_clock ? g_clockRate : 0; }
long long GunSound::PlayAt(float volume, long long targetSample) {
    long long clk = ClockSamples();
    if (clk < 0 || targetSample < 0 || !nPcm_ || !nVoices_) {
        Play(volume);
        return 0;
    }
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
    if (volume < 0.f) volume = 0.f;
    if (volume > 2.f) volume = 2.f;
    IXAudio2SourceVoice* v = voices_[pick];
    v->SetVolume(volume);
    v->SetFrequencyRatio(1.f);
    vol_[pick] = volume;
    long long d = targetSample - clk;
    if (d > 0) {
        long long pad = d * static_cast<long long>(fmt_.nSamplesPerSec) / static_cast<long long>(g_clockRate);
        long long cap = static_cast<long long>(fmt_.nSamplesPerSec) / 2;
        if (pad > cap) pad = cap;
        const long long chunk = static_cast<long long>(sizeof g_zero) / fmt_.nBlockAlign;
        while (pad > 0) {
            long long n = pad < chunk ? pad : chunk;
            XAUDIO2_BUFFER z = {};
            z.AudioBytes = static_cast<UINT32>(n * fmt_.nBlockAlign);
            z.pAudioData = g_zero;
            if (FAILED(v->SubmitSourceBuffer(&z))) break;
            pad -= n;
        }
    }
    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = bytesV_[vi];
    buf.pAudioData = pcmV_[vi];
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (SUCCEEDED(v->SubmitSourceBuffer(&buf))) {
        v->Start(0);
        started_[pick] = now;
    }
    return d;
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
        ReleasePcm(pcmV_[k]);
        pcmV_[k] = nullptr;
        bytesV_[k] = 0;
    }
    nPcm_ = 0;
    last_ = -1;
}
}
