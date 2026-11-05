#include "pack_extract.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "pack_codec.h"
namespace packx {
namespace {
constexpr DWORD kFlagExtendedHeader = 0x100;
constexpr DWORD kFlagIndexTimestamps = 0x40;
constexpr DWORD kFlagEncryptedIndex = 0x80;
constexpr DWORD kFlagEncryptedData = 0x10;
constexpr DWORD kMaxIndexBytes = 64u << 20;
constexpr DWORD kMaxIniBytes = 4u << 20;
const char kManifest[] = "tenzin_fps/manifest.txt";
const char kPrefix[] = "tenzin_fps/files/";
const wchar_t kMarker[] = L"tenzin_fps_pack_extracted.txt";
const wchar_t kTokenFile[] = L"tenzin_fps_pack_token.txt";
const char kAddonDir[] = "tenzin_fps_addon/";
const char kFullId[] = "tenzin_fps";
const char kCoreId[] = "core";
struct Entry {
    std::string name;
    unsigned long long offset;
    DWORD size;
    bool compressed;
};
struct Pack {
    std::wstring path;
    std::vector<Entry> files;
};
struct Marker {
    std::string token;
    std::map<std::string, unsigned long long> sizes;
    std::map<std::string, std::string> iniHash;
};
std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
std::string Fnv64(const std::string& data) {
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char c : data) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[17];
    snprintf(buf, sizeof buf, "%016llx", h);
    return buf;
}
bool IsIni(const std::string& rel) {
    return rel.size() > 4 && _stricmp(rel.c_str() + rel.size() - 4, ".ini") == 0;
}
bool ReadAt(HANDLE f, unsigned long long off, void* buf, DWORD n) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(off);
    if (!SetFilePointerEx(f, li, nullptr, FILE_BEGIN)) return false;
    DWORD got = 0;
    return ReadFile(f, buf, n, &got, nullptr) && got == n;
}
bool ReadWhole(const std::wstring& p, std::string& out, DWORD limit) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(f, &sz) && sz.QuadPart <= limit;
    if (ok) {
        out.assign(static_cast<size_t>(sz.QuadPart), '\0');
        ok = sz.QuadPart == 0 || ReadAt(f, 0, &out[0], static_cast<DWORD>(sz.QuadPart));
    }
    CloseHandle(f);
    return ok;
}
bool ReadIndex(const std::wstring& path, Pack& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    unsigned char head[28];
    do {
        if (!ReadAt(f, 0, head, sizeof head)) break;
        bool v5 = memcmp(head, "PFH5", 4) == 0, v4 = memcmp(head, "PFH4", 4) == 0;
        if (!v5 && !v4) break;
        DWORD hv[6];
        memcpy(hv, head + 4, sizeof hv);
        DWORD flags = hv[0], packIdx = hv[2], nFiles = hv[3], fileIdx = hv[4];
        if (flags & (kFlagEncryptedIndex | kFlagEncryptedData)) break;
        if (packIdx > kMaxIndexBytes || fileIdx > kMaxIndexBytes) break;
        unsigned long long hsize = 28 + ((v5 && (flags & kFlagExtendedHeader)) ? 20 : 0);
        std::vector<unsigned char> idx(fileIdx);
        if (fileIdx && !ReadAt(f, hsize + packIdx, idx.data(), fileIdx)) break;
        unsigned long long data = hsize + packIdx + fileIdx;
        size_t o = 0;
        bool bad = false;
        out.files.clear();
        out.files.reserve(nFiles);
        for (DWORD i = 0; i < nFiles; ++i) {
            if (o + 4 > idx.size()) { bad = true; break; }
            DWORD size;
            memcpy(&size, &idx[o], 4);
            o += 4;
            if (flags & kFlagIndexTimestamps) o += 4;
            bool comp = false;
            if (v5) {
                if (o >= idx.size()) { bad = true; break; }
                comp = idx[o++] != 0;
            }
            size_t e = o;
            while (e < idx.size() && idx[e]) ++e;
            if (e >= idx.size()) { bad = true; break; }
            std::string name(reinterpret_cast<const char*>(&idx[o]), e - o);
            for (char& c : name)
                if (c == '\\') c = '/';
            o = e + 1;
            out.files.push_back(Entry{name, data, size, comp});
            data += size;
        }
        if (bad || o != idx.size()) break;
        out.path = path;
        ok = true;
    } while (false);
    CloseHandle(f);
    return ok;
}
const Entry* Find(const Pack& p, const char* name) {
    for (const Entry& e : p.files)
        if (e.name == name) return &e;
    return nullptr;
}
bool ReadEntry(HANDLE f, const Entry& e, std::string& out) {
    out.assign(e.size, '\0');
    return e.size == 0 || ReadAt(f, e.offset, &out[0], e.size);
}
std::string Field(const std::string& text, const char* key) {
    std::string k = std::string(key) + "=";
    size_t a = text.find(k);
    if (a == std::string::npos) return std::string();
    a += k.size();
    size_t b = a;
    while (b < text.size() && text[b] != '\r' && text[b] != '\n') ++b;
    return text.substr(a, b - a);
}
void ScanDir(const std::wstring& dir, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.pack").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
std::vector<std::wstring> Candidates(const std::wstring& root) {
    std::vector<std::wstring> v;
    ScanDir(root + L"\\data", v);
    std::wstring steamapps = root;
    for (int k = 0; k < 2; ++k) {
        size_t s = steamapps.find_last_of(L"\\/");
        if (s == std::wstring::npos) return v;
        steamapps.resize(s);
    }
    std::wstring ws = steamapps + L"\\workshop\\content\\1142710";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((ws + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') ScanDir(ws + L"\\" + fd.cFileName, v);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return v;
}
bool MakeDirs(const std::wstring& filePath) {
    size_t s = filePath.find_last_of(L'\\');
    if (s == std::wstring::npos) return true;
    std::wstring dir = filePath.substr(0, s);
    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    if (!MakeDirs(dir)) return false;
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
long long FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return -1;
    return (static_cast<long long>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
}
std::wstring Target(const std::wstring& root, const std::string& rel) {
    std::wstring w = Widen(rel);
    for (wchar_t& c : w)
        if (c == L'/') c = L'\\';
    return root + L"\\" + w;
}
Marker ReadMarker(const std::wstring& root, const std::wstring& markerName) {
    Marker m;
    std::string text;
    if (!ReadWhole(root + L"\\" + markerName, text, 16u << 20)) return m;
    m.token = Field(text, "token");
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string ln = text.substr(pos, e - pos);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
        pos = e + 1;
        if (ln.size() > 2 && (ln[0] == 'f' || ln[0] == 'i' || ln[0] == 'r') && ln[1] == ' ') {
            size_t sp = ln.find(' ', 2);
            if (sp == std::string::npos) continue;
            std::string val = ln.substr(2, sp - 2), rel = ln.substr(sp + 1);
            if (ln[0] != 'i') m.sizes[rel] = strtoull(val.c_str(), nullptr, 10);
            else m.iniHash[rel] = val;
        }
    }
    return m;
}
unsigned long long DiskSize(const Marker& m, const std::string& rel, DWORD stored) {
    auto it = m.sizes.find(rel);
    return it == m.sizes.end() ? stored : it->second;
}
bool CopyEntryTo(HANDLE src, const Pack& pack, const Entry& e, HANDLE out, std::vector<char>& buf, unsigned long long& raw, LogFn log) {
    char head[16];
    DWORD hn = e.size < sizeof head ? e.size : static_cast<DWORD>(sizeof head);
    if (hn && !ReadAt(src, e.offset, head, hn)) return false;
    packcodec::Kind k = packcodec::Sniff(head, hn);
    if (k == packcodec::Kind::Plain) {
        bool ok = true;
        unsigned long long off = e.offset;
        DWORD left = e.size;
        while (left && ok) {
            DWORD n = left < buf.size() ? left : static_cast<DWORD>(buf.size());
            DWORD w = 0;
            ok = ReadAt(src, off, buf.data(), n) && WriteFile(out, buf.data(), n, &w, nullptr) && w == n;
            off += n;
            left -= n;
        }
        raw = e.size;
        return ok;
    }
    std::string stored, data, err;
    if (!ReadEntry(src, e, stored)) return false;
    if (k == packcodec::Kind::Alias) {
        std::string rel;
        unsigned size = 0, crc = 0;
        if (!packcodec::ParseAlias(stored, rel, size, crc, err)) {
            log("onepack: tzc1: %s - %s", e.name.c_str(), err.c_str());
            return false;
        }
        std::string name = packcodec::Resolve(e.name, rel);
        const Entry* from = Find(pack, name.c_str());
        if (!from || from->compressed || !ReadEntry(src, *from, stored)) {
            log("onepack: tzc1: %s points to %s, which is missing in the pack", e.name.c_str(), name.c_str());
            return false;
        }
        packcodec::Kind fk = packcodec::Sniff(stored.data(), stored.size());
        if (fk == packcodec::Kind::Alias) err = "alias of an alias";
        else if (fk == packcodec::Kind::Compressed) packcodec::Decode(stored, data, err);
        else data.swap(stored);
        if (err.empty() && (data.size() != size || packcodec::Crc32(data.data(), data.size()) != crc)) err = "size / CRC32 differs from the alias";
    } else {
        packcodec::Decode(stored, data, err);
    }
    if (!err.empty()) {
        log("onepack: tzc1: %s - %s", e.name.c_str(), err.c_str());
        return false;
    }
    DWORD w = 0;
    raw = data.size();
    return data.empty() || (WriteFile(out, data.data(), static_cast<DWORD>(data.size()), &w, nullptr) && w == data.size());
}
struct WriteJob {
    const Entry* e;
    std::string rel;
    std::wstring dst;
    unsigned long long raw = 0;
    bool opened = false, ok = false, removed = false;
    DWORD err = 0;
};
SRWLOCK g_jobLogLock = SRWLOCK_INIT;
LogFn g_jobLog = nullptr;
void JobLog(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    AcquireSRWLockExclusive(&g_jobLogLock);
    g_jobLog("%s", b);
    ReleaseSRWLockExclusive(&g_jobLogLock);
}
struct JobRun {
    const Pack* pack;
    std::vector<WriteJob>* jobs;
    volatile LONG next;
};
DWORD WINAPI JobWorker(LPVOID p) {
    JobRun& r = *static_cast<JobRun*>(p);
    HANDLE src = CreateFileW(r.pack->path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    std::vector<char> buf(1 << 20);
    for (;;) {
        LONG i = InterlockedIncrement(&r.next) - 1;
        if (i >= static_cast<LONG>(r.jobs->size())) break;
        WriteJob& j = (*r.jobs)[i];
        HANDLE out = CreateFileW(j.dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) {
            j.err = GetLastError();
            continue;
        }
        j.opened = true;
        j.raw = j.e->size;
        j.ok = src != INVALID_HANDLE_VALUE && CopyEntryTo(src, *r.pack, *j.e, out, buf, j.raw, JobLog);
        CloseHandle(out);
        if (!j.ok) j.removed = DeleteFileW(j.dst.c_str()) != 0;
    }
    if (src != INVALID_HANDLE_VALUE) CloseHandle(src);
    return 0;
}
void RunJobs(const Pack& pack, std::vector<WriteJob>& jobs, LogFn log) {
    if (jobs.empty()) return;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    size_t want = si.dwNumberOfProcessors > 1 ? si.dwNumberOfProcessors - 1 : 1;
    if (want > 8) want = 8;
    if (want > jobs.size()) want = jobs.size();
    ULONGLONG t0 = GetTickCount64();
    g_jobLog = log;
    JobRun run{&pack, &jobs, 0};
    HANDLE th[8];
    DWORD n = 0;
    for (size_t k = 1; k < want; ++k)
        if ((th[n] = CreateThread(nullptr, 0, JobWorker, &run, 0, nullptr)) != nullptr) ++n;
    JobWorker(&run);
    if (n) WaitForMultipleObjects(n, th, TRUE, INFINITE);
    for (DWORD k = 0; k < n; ++k) CloseHandle(th[k]);
    log("onepack: %zu files written by %lu threads in %llu ms", jobs.size(), n + 1, GetTickCount64() - t0);
}
std::string Stamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[32];
    snprintf(b, sizeof b, "%04u%02u%02u_%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}
bool ExtractPayload(const std::wstring& root, const Pack& pack, const std::string& prefix, const std::wstring& markerName, const std::string& token,
                    const std::set<std::string>& protect, const char* label, LogFn log) {
    ULONGLONG t0 = GetTickCount64();
    std::string packA = Narrow(pack.path);
    std::vector<const Entry*> ours;
    unsigned long long bytes = 0;
    for (const Entry& e : pack.files)
        if (e.name.compare(0, prefix.size(), prefix) == 0 && e.name.size() > prefix.size()) ours.push_back(&e), bytes += e.size;
    Marker prev = ReadMarker(root, markerName);
    if (prev.token == token) {
        bool all = true;
        for (const Entry* e : ours) {
            std::string rel = e->name.substr(prefix.size());
            long long sz = FileSize(Target(root, rel));
            if (sz < 0 || (!IsIni(rel) && sz != static_cast<long long>(DiskSize(prev, rel, e->size)))) {
                all = false;
                break;
            }
        }
        if (all) {
            log("onepack: %s%s token=%s - %zu files (%.1f MB) already extracted, nothing written (%llu ms)", label, packA.c_str(), token.c_str(),
                ours.size(), bytes / 1048576.0, GetTickCount64() - t0);
            return true;
        }
    }
    HANDLE src = CreateFileW(pack.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (src == INVALID_HANDLE_VALUE) {
        log("onepack: %scannot open %s (err %lu)", label, packA.c_str(), GetLastError());
        return false;
    }
    std::vector<char> buf(1 << 20);
    size_t written = 0, failed = 0, kept = 0;
    std::set<std::string> now;
    std::map<std::string, std::string> iniHash;
    std::map<std::string, unsigned long long> rawSize;
    std::string stamp = Stamp();
    std::vector<WriteJob> jobs;
    for (const Entry* e : ours) {
        std::string rel = e->name.substr(prefix.size());
        now.insert(rel);
        if (e->compressed) {
            log("onepack: %s%s is compressed in the pack - not supported", label, e->name.c_str());
            ++failed;
            continue;
        }
        std::wstring dst = Target(root, rel);
        if (!MakeDirs(dst)) {
            ++failed;
            continue;
        }
        if (IsIni(rel) && e->size <= kMaxIniBytes) {
            std::string fresh, cur;
            if (!ReadEntry(src, *e, fresh)) {
                ++failed;
                continue;
            }
            iniHash[rel] = Fnv64(fresh);
            if (ReadWhole(dst, cur, kMaxIniBytes)) {
                std::string ch = Fnv64(cur);
                auto it = prev.iniHash.find(rel);
                bool userEdited = it == prev.iniHash.end() || it->second != ch;
                if (ch != iniHash[rel] && userEdited) {
                    std::wstring keep = dst + L".user_" + Widen(stamp);
                    if (CopyFileW(dst.c_str(), keep.c_str(), FALSE)) {
                        ++kept;
                        log("onepack: %s%s differs from the pack and from what was extracted last time - kept a copy as %s.user_%s", label,
                            rel.c_str(), rel.c_str(), stamp.c_str());
                    }
                }
            }
        }
        jobs.push_back(WriteJob{e, rel, dst});
    }
    CloseHandle(src);
    RunJobs(pack, jobs, log);
    for (const WriteJob& j : jobs) {
        const std::string& rel = j.rel;
        if (!j.opened) {
            SetLastError(j.err);
            log("onepack: %scannot write %s (err %lu)", label, rel.c_str(), GetLastError());
            ++failed;
            continue;
        }
        if (j.ok) {
            ++written;
            if (j.raw != j.e->size) rawSize[rel] = j.raw;
        } else {
            ++failed;
            bool removed = j.removed;
            log("onepack: %swrite failed for %s%s", label, rel.c_str(), removed ? " - partial file removed" : "");
        }
    }
    if (!packcodec::BuiltinReason().empty())
        log("onepack: tzc1: built-in MSZIP decoder used (%s)", packcodec::BuiltinReason().c_str());
    if (failed) {
        log("onepack: %s%zu of %zu files failed - not marking as extracted", label, failed, ours.size());
        return false;
    }
    size_t removed = 0;
    for (const auto& kv : prev.sizes) {
        if (now.count(kv.first) || protect.count(kv.first)) continue;
        std::wstring p = Target(root, kv.first);
        if (FileSize(p) == static_cast<long long>(kv.second) && DeleteFileW(p.c_str())) ++removed;
    }
    std::wstring marker = root + L"\\" + markerName;
    if (FILE* f = _wfopen(marker.c_str(), L"wb")) {
        fprintf(f, "token=%s\r\npack=%s\r\n", token.c_str(), packA.c_str());
        for (const Entry* e : ours) fprintf(f, "f %lu %s\r\n", static_cast<unsigned long>(e->size), e->name.c_str() + prefix.size());
        for (const auto& kv : iniHash) fprintf(f, "i %s %s\r\n", kv.second.c_str(), kv.first.c_str());
        for (const auto& kv : rawSize) fprintf(f, "r %llu %s\r\n", kv.second, kv.first.c_str());
        fclose(f);
    }
    log("onepack: %sextracted %zu files (%.1f MB) from %s token=%s in %llu ms (previous token %s; %zu stale files removed, %zu edited ini kept)",
        label, written, bytes / 1048576.0, packA.c_str(), token.c_str(), GetTickCount64() - t0, prev.token.empty() ? "(none)" : prev.token.c_str(),
        removed, kept);
    return true;
}
bool g_full = true;
std::wstring g_addonIds;
struct Want {
    std::string id, version, token;
    std::string manifest, prefix;
    std::wstring marker;
    int pack = -1;
    bool ok = false;
};
bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > 48) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
bool ExtractMany(const std::wstring& root, const std::string& tokText, LogFn log) {
    ULONGLONG t0 = GetTickCount64();
    std::vector<Want> want;
    size_t pos = 0;
    bool full = false;
    std::string coreVer = Field(tokText, "core");
    while (pos < tokText.size()) {
        size_t e = tokText.find('\n', pos);
        if (e == std::string::npos) e = tokText.size();
        std::string ln = tokText.substr(pos, e - pos);
        pos = e + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
        if (ln.compare(0, 8, "payload=") != 0) continue;
        std::string v = ln.substr(8);
        size_t a = v.find('|'), b = a == std::string::npos ? a : v.find('|', a + 1);
        if (b == std::string::npos) continue;
        Want w;
        w.id = v.substr(0, a);
        w.version = v.substr(a + 1, b - a - 1);
        w.token = v.substr(b + 1);
        if (!ValidId(w.id) || w.token.empty()) {
            log("onepack: bad payload line '%s' - skipped", ln.c_str());
            continue;
        }
        bool dup = false;
        for (const Want& o : want) dup = dup || o.id == w.id;
        if (dup) continue;
        if (w.id == kFullId) {
            full = true;
            w.manifest = kManifest;
            w.prefix = kPrefix;
            w.marker = kMarker;
        } else {
            w.manifest = std::string(kAddonDir) + w.id + "/manifest.txt";
            w.prefix = std::string(kAddonDir) + w.id + "/files/";
            w.marker = L"tenzin_fps_addon_" + Widen(w.id) + L"_extracted.txt";
        }
        want.push_back(w);
    }
    if (full)
        for (size_t i = 0; i < want.size(); ++i)
            if (want[i].id == kCoreId) want.erase(want.begin() + static_cast<long>(i)), --i;
    g_full = full;
    std::sort(want.begin(), want.end(), [](const Want& x, const Want& y) {
        bool mx = x.id == kFullId || x.id == kCoreId, my = y.id == kFullId || y.id == kCoreId;
        if (mx != my) return mx;
        return x.id < y.id;
    });
    if (want.empty() || (want[0].id != kFullId && want[0].id != kCoreId)) {
        log("onepack: token file lists no main payload (tenzin_fps or core) - first person stays off");
        return false;
    }
    std::vector<std::wstring> cands = Candidates(root);
    std::vector<Pack> packs;
    int scanned = 0;
    size_t found = 0;
    for (const std::wstring& c : cands) {
        if (found == want.size()) break;
        Pack p;
        if (!ReadIndex(c, p)) continue;
        ++scanned;
        HANDLE f = INVALID_HANDLE_VALUE;
        int idx = -1;
        for (Want& w : want) {
            if (w.pack >= 0) continue;
            const Entry* m = Find(p, w.manifest.c_str());
            if (!m || m->compressed || m->size > (16u << 20)) continue;
            if (f == INVALID_HANDLE_VALUE)
                f = CreateFileW(c.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (f == INVALID_HANDLE_VALUE) break;
            std::string text;
            if (!ReadEntry(f, *m, text) || Field(text, "token") != w.token) continue;
            if (idx < 0) {
                idx = static_cast<int>(packs.size());
                packs.push_back(p);
            }
            w.pack = idx;
            ++found;
        }
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    }
    std::vector<std::set<std::string>> files(want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        if (want[i].pack < 0) continue;
        for (const Entry& e : packs[want[i].pack].files)
            if (e.name.compare(0, want[i].prefix.size(), want[i].prefix) == 0 && e.name.size() > want[i].prefix.size())
                files[i].insert(e.name.substr(want[i].prefix.size()));
    }
    std::vector<std::wstring> ok;
    for (size_t i = 0; i < want.size(); ++i) {
        Want& w = want[i];
        if (w.pack < 0) {
            log("onepack: payload %s token=%s not found among %d packs (%zu candidates in data\\ and workshop)%s", w.id.c_str(), w.token.c_str(),
                scanned, cands.size(), i == 0 ? " - first person stays off" : " - this weapon pack is skipped");
            if (i == 0) return false;
            continue;
        }
        std::set<std::string> protect;
        for (size_t j = 0; j < want.size(); ++j)
            if (j != i) protect.insert(files[j].begin(), files[j].end());
        char label[64];
        snprintf(label, sizeof label, "[%s %s] ", w.id.c_str(), w.version.c_str());
        w.ok = ExtractPayload(root, packs[w.pack], w.prefix, w.marker, w.token, protect, label, log);
        if (!w.ok && i == 0) return false;
        if (w.ok && i > 0) ok.push_back(Widen(w.id));
    }
    g_addonIds.clear();
    for (const std::wstring& id : ok) g_addonIds += (g_addonIds.empty() ? L"" : L";") + id;
    log("onepack: %s mode, core %s, weapon packs '%ls' (%llu ms total)", full ? "full (tenzin_fps)" : "core-only (no tenzin_fps)",
        coreVer.empty() ? "?" : coreVer.c_str(), g_addonIds.c_str(), GetTickCount64() - t0);
    return true;
}
}
bool FullMod() { return g_full; }
const wchar_t* MainIniName() { return g_full ? L"tenzin_fps.ini" : L"tenzin_fps_core.ini"; }
const wchar_t* AddonIds() { return g_addonIds.c_str(); }
bool ExtractFromPack(const wchar_t* gameRoot, LogFn log) {
    std::wstring root = gameRoot;
    std::string tokText;
    if (!ReadWhole(root + L"\\" + kTokenFile, tokText, 65536) || (Field(tokText, "token").empty() && tokText.find("payload=") == std::string::npos)) {
        log("onepack: %ls missing - the Lua loader in hero_fps.pack did not run first", kTokenFile);
        return false;
    }
    if (tokText.find("payload=") != std::string::npos) return ExtractMany(root, tokText, log);
    g_full = true;
    g_addonIds.clear();
    std::string token = Field(tokText, "token");
    std::vector<std::wstring> cands = Candidates(root);
    Pack pack;
    bool found = false;
    int scanned = 0;
    for (const std::wstring& c : cands) {
        Pack p;
        if (!ReadIndex(c, p)) continue;
        ++scanned;
        const Entry* m = Find(p, kManifest);
        if (!m || m->compressed || m->size > (16u << 20)) continue;
        HANDLE f = CreateFileW(c.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        std::string text;
        bool ok = ReadEntry(f, *m, text);
        CloseHandle(f);
        if (ok && Field(text, "token") == token) {
            pack = std::move(p);
            found = true;
            break;
        }
    }
    if (!found) {
        log("onepack: no pack with tenzin_fps/manifest.txt token=%s among %d packs (%zu candidates in data\\ and workshop)", token.c_str(), scanned,
            cands.size());
        return false;
    }
    return ExtractPayload(root, pack, kPrefix, kMarker, token, std::set<std::string>(), "", log);
}
}
