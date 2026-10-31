#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace fhook {
constexpr size_t kPad = 16;
constexpr size_t kMaxHookBytes = 16;
constexpr int kMaxHooks = 4;
struct Hook {
    size_t at;
    size_t len;
    uint64_t target;
};
inline size_t JumpAt(const uint8_t* m, size_t n, size_t i, uint64_t base, uint64_t* target) {
    if (i + 5 <= n && m[i] == 0xE9) {
        int32_t rel;
        memcpy(&rel, m + i + 1, 4);
        *target = base + i + 5 + static_cast<int64_t>(rel);
        return 5;
    }
    if (i + 14 <= n && m[i] == 0xFF && m[i + 1] == 0x25 && !m[i + 2] && !m[i + 3] && !m[i + 4] && !m[i + 5]) {
        memcpy(target, m + i + 6, 8);
        return 14;
    }
    if (i + 12 <= n && m[i] == 0x48 && m[i + 1] == 0xB8 && m[i + 10] == 0xFF && m[i + 11] == 0xE0) {
        memcpy(target, m + i + 2, 8);
        return 12;
    }
    if (i + 13 <= n && m[i] == 0x49 && m[i + 1] == 0xBB && m[i + 10] == 0x41 && m[i + 11] == 0xFF && m[i + 12] == 0xE3) {
        memcpy(target, m + i + 2, 8);
        return 13;
    }
    return 0;
}
inline size_t NopAt(const uint8_t* m, size_t n, size_t i) {
    if (i >= n) return 0;
    if (m[i] == 0x90 || m[i] == 0xCC) return 1;
    static const uint8_t kNops[][10] = {
        {2, 0x66, 0x90},
        {3, 0x0F, 0x1F, 0x00},
        {4, 0x0F, 0x1F, 0x40, 0x00},
        {5, 0x0F, 0x1F, 0x44, 0x00, 0x00},
        {6, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00},
        {7, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00},
        {8, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {9, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    for (const auto& k : kNops)
        if (i + k[0] <= n && !memcmp(m + i, k + 1, k[0])) return k[0];
    return 0;
}
inline int Classify(const uint8_t* mem, const uint8_t* disk, size_t n, uint64_t base, size_t keepLo, size_t keepHi, uint64_t imgLo,
                    uint64_t imgHi, Hook* out) {
    int count = 0;
    size_t i = 0;
    while (i < n) {
        if (mem[i] == disk[i]) {
            ++i;
            continue;
        }
        size_t start = 0, len = 0;
        uint64_t tgt = 0;
        for (size_t back = 0; back <= 4 && back <= i; ++back) {
            size_t j = i - back;
            uint64_t t = 0;
            size_t l = JumpAt(mem, n, j, base, &t);
            if (l && j + l > i && (t < imgLo || t >= imgHi)) {
                start = j, len = l, tgt = t;
                break;
            }
        }
        if (!len) {
            if (i >= keepLo && i < keepHi) return -1;
            ++i;
            continue;
        }
        size_t end = start + len;
        while (end < n && mem[end] != disk[end]) {
            size_t l = NopAt(mem, n, end);
            if (!l) break;
            end += l;
        }
        if (start < keepHi && end > keepLo) {
            if (end - start > kMaxHookBytes || count >= kMaxHooks) return -1;
            out[count++] = Hook{start, end - start, tgt};
        }
        i = end;
    }
    return count;
}
}
