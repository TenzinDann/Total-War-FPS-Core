#include "pack_extract.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <vector>
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
Marker ReadMarker(const std::wstring& root) {
    Marker m;
    std::string text;
    if (!ReadWhole(root + L"\\" + kMarker, text, 16u << 20)) return m;
    m.token = Field(text, "token");
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string ln = text.substr(pos, e - pos);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
        pos = e + 1;
        if (ln.size() > 2 && (ln[0] == 'f' || ln[0] == 'i') && ln[1] == ' ') {
            size_t sp = ln.find(' ', 2);
            if (sp == std::string::npos) continue;
            std::string val = ln.substr(2, sp - 2), rel = ln.substr(sp + 1);
            if (ln[0] == 'f') m.sizes[rel] = strtoull(val.c_str(), nullptr, 10);
            else m.iniHash[rel] = val;
        }
    }
    return m;
}
std::string Stamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char b[32];
    snprintf(b, sizeof b, "%04u%02u%02u_%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}
}
bool ExtractFromPack(const wchar_t* gameRoot, LogFn log) {
    std::wstring root = gameRoot;
    ULONGLONG t0 = GetTickCount64();
    std::string tokText;
    if (!ReadWhole(root + L"\\" + kTokenFile, tokText, 4096) || Field(tokText, "token").empty()) {
        log("onepack: %ls missing - the Lua loader in hero_fps.pack did not run first", kTokenFile);
        return false;
    }
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
    std::string packA = Narrow(pack.path);
    std::vector<const Entry*> ours;
    unsigned long long bytes = 0;
    for (const Entry& e : pack.files)
        if (e.name.compare(0, sizeof kPrefix - 1, kPrefix) == 0) ours.push_back(&e), bytes += e.size;
    Marker prev = ReadMarker(root);
    if (prev.token == token) {
        bool all = true;
        for (const Entry* e : ours) {
            std::string rel = e->name.substr(sizeof kPrefix - 1);
            long long sz = FileSize(Target(root, rel));
            if (sz < 0 || (!IsIni(rel) && sz != static_cast<long long>(e->size))) {
                all = false;
                break;
            }
        }
        if (all) {
            log("onepack: %s token=%s - %zu files (%.1f MB) already extracted, nothing written (%llu ms)", packA.c_str(), token.c_str(), ours.size(),
                bytes / 1048576.0, GetTickCount64() - t0);
            return true;
        }
    }
    HANDLE src = CreateFileW(pack.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (src == INVALID_HANDLE_VALUE) {
        log("onepack: cannot open %s (err %lu)", packA.c_str(), GetLastError());
        return false;
    }
    std::vector<char> buf(1 << 20);
    size_t written = 0, failed = 0, kept = 0;
    std::set<std::string> now;
    std::map<std::string, std::string> iniHash;
    std::string stamp = Stamp();
    for (const Entry* e : ours) {
        std::string rel = e->name.substr(sizeof kPrefix - 1);
        now.insert(rel);
        if (e->compressed) {
            log("onepack: %s is compressed in the pack - not supported", e->name.c_str());
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
                        log("onepack: %s differs from the pack and from what was extracted last time - kept a copy as %s.user_%s", rel.c_str(),
                            rel.c_str(), stamp.c_str());
                    }
                }
            }
        }
        HANDLE out = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) {
            log("onepack: cannot write %s (err %lu)", rel.c_str(), GetLastError());
            ++failed;
            continue;
        }
        bool ok = true;
        unsigned long long off = e->offset;
        DWORD left = e->size;
        while (left && ok) {
            DWORD n = left < buf.size() ? left : static_cast<DWORD>(buf.size());
            DWORD w = 0;
            ok = ReadAt(src, off, buf.data(), n) && WriteFile(out, buf.data(), n, &w, nullptr) && w == n;
            off += n;
            left -= n;
        }
        CloseHandle(out);
        if (ok) ++written;
        else {
            ++failed;
            log("onepack: write failed for %s", rel.c_str());
        }
    }
    CloseHandle(src);
    if (failed) {
        log("onepack: %zu of %zu files failed - not marking as extracted", failed, ours.size());
        return false;
    }
    size_t removed = 0;
    for (const auto& kv : prev.sizes) {
        if (now.count(kv.first)) continue;
        std::wstring p = Target(root, kv.first);
        if (FileSize(p) == static_cast<long long>(kv.second) && DeleteFileW(p.c_str())) ++removed;
    }
    std::wstring marker = root + L"\\" + kMarker;
    if (FILE* f = _wfopen(marker.c_str(), L"wb")) {
        fprintf(f, "token=%s\r\npack=%s\r\n", token.c_str(), packA.c_str());
        for (const Entry* e : ours) fprintf(f, "f %lu %s\r\n", static_cast<unsigned long>(e->size), e->name.c_str() + sizeof kPrefix - 1);
        for (const auto& kv : iniHash) fprintf(f, "i %s %s\r\n", kv.second.c_str(), kv.first.c_str());
        fclose(f);
    }
    log("onepack: extracted %zu files (%.1f MB) from %s token=%s in %llu ms (previous token %s; %zu stale files removed, %zu edited ini kept)",
        written, bytes / 1048576.0, packA.c_str(), token.c_str(), GetTickCount64() - t0, prev.token.empty() ? "(none)" : prev.token.c_str(), removed,
        kept);
    return true;
}
}
