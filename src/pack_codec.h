#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <string>
#include <vector>
#include "mszip_decode.h"
namespace packcodec {
constexpr unsigned kAlgMsZip = 2;
enum class Kind { Plain, Compressed, Alias };
inline Kind Sniff(const char* head, size_t n) {
    if (n >= 16 && memcmp(head, "TZC1", 4) == 0) return Kind::Compressed;
    if (n >= 14 && memcmp(head, "TZA1", 4) == 0) return Kind::Alias;
    return Kind::Plain;
}
inline unsigned U32(const std::string& s, size_t o) {
    unsigned v;
    memcpy(&v, s.data() + o, 4);
    return v;
}
inline unsigned Crc32(const void* data, size_t n) {
    static const std::vector<unsigned> table = [] {
        std::vector<unsigned> t(256);
        for (unsigned i = 0; i < 256; ++i) {
            unsigned c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    unsigned c = 0xFFFFFFFFu;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
inline std::string& BuiltinReason() {
    static std::string r;
    return r;
}
inline bool Builtin(const char* in, size_t n, char* out, size_t outN, const char* why, std::string& err) {
    if (BuiltinReason().empty()) BuiltinReason() = why;
    return mszip::Decode(reinterpret_cast<const unsigned char*>(in), n, reinterpret_cast<unsigned char*>(out), outN, err);
}
inline bool MsZip(const char* in, size_t n, char* out, size_t outN, std::string& err) {
#ifdef PACKCODEC_FORCE_BUILTIN
    return Builtin(in, n, out, outN, "forced by PACKCODEC_FORCE_BUILTIN (offline test)", err);
#else
    using CreateFn = BOOL(WINAPI*)(DWORD, void*, void**);
    using DecompressFn = BOOL(WINAPI*)(void*, const void*, SIZE_T, void*, SIZE_T, SIZE_T*);
    using CloseFn = BOOL(WINAPI*)(void*);
    static HMODULE mod = LoadLibraryExW(L"cabinet.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!mod) return Builtin(in, n, out, outN, "cabinet.dll not available", err);
    auto create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(mod, "CreateDecompressor")));
    auto decompress = reinterpret_cast<DecompressFn>(reinterpret_cast<void*>(GetProcAddress(mod, "Decompress")));
    auto close = reinterpret_cast<CloseFn>(reinterpret_cast<void*>(GetProcAddress(mod, "CloseDecompressor")));
    if (!create || !decompress || !close) return Builtin(in, n, out, outN, "cabinet.dll has no Decompress API", err);
    void* h = nullptr;
    if (!create(kAlgMsZip, nullptr, &h)) {
        err = "CreateDecompressor failed (err " + std::to_string(GetLastError()) + ")";
        return false;
    }
    SIZE_T got = 0;
    BOOL ok = decompress(h, in, n, out, outN, &got);
    DWORD e = GetLastError();
    close(h);
    if (!ok || got != outN) {
        err = "Decompress failed (err " + std::to_string(ok ? 0 : e) + ", got " + std::to_string(got) + " of " + std::to_string(outN) + " bytes)";
        return false;
    }
    return true;
#endif
}
inline bool Decode(const std::string& stored, std::string& raw, std::string& err) {
    if (stored.size() < 16 || memcmp(stored.data(), "TZC1", 4) != 0) {
        err = "not a TZC1 container";
        return false;
    }
    unsigned alg = U32(stored, 4), size = U32(stored, 8), crc = U32(stored, 12);
    if (alg != kAlgMsZip) {
        err = "unknown algorithm " + std::to_string(alg);
        return false;
    }
    raw.assign(size, '\0');
    if (size && !MsZip(stored.data() + 16, stored.size() - 16, &raw[0], size, err)) return false;
    if (Crc32(raw.data(), raw.size()) != crc) {
        err = "CRC32 differs after decompress";
        return false;
    }
    return true;
}
inline bool ParseAlias(const std::string& stored, std::string& rel, unsigned& size, unsigned& crc, std::string& err) {
    if (stored.size() < 14 || memcmp(stored.data(), "TZA1", 4) != 0) {
        err = "not a TZA1 alias";
        return false;
    }
    size = U32(stored, 4);
    crc = U32(stored, 8);
    unsigned short n;
    memcpy(&n, stored.data() + 12, 2);
    if (!n || 14u + n > stored.size()) {
        err = "alias path out of range";
        return false;
    }
    rel.assign(stored.data() + 14, n);
    return true;
}
inline std::string Resolve(const std::string& self, const std::string& rel) {
    size_t cut = self.find_last_of('/');
    std::string path = (cut == std::string::npos ? std::string() : self.substr(0, cut + 1)) + rel;
    std::vector<std::string> parts;
    size_t a = 0;
    while (a <= path.size()) {
        size_t b = path.find('/', a);
        if (b == std::string::npos) b = path.size();
        std::string seg = path.substr(a, b - a);
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(seg);
        }
        a = b + 1;
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? "/" : "") + parts[i];
    return out;
}
}
