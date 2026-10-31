#pragma once
namespace pierce {
inline int Select(const float* t, int n, float firstT, float gap, int maxOut, int* out) {
    int k = 0;
    float last = firstT;
    for (int i = 0; i < n && k < maxOut; ++i) {
        if (t[i] < last + gap) continue;
        out[k++] = i;
        last = t[i];
    }
    return k;
}
inline int SelectSpread(const float (*pos)[3], int n, const float first[3], float gap, int maxOut, int* out) {
    int k = 0;
    for (int i = 0; i < n && k < maxOut; ++i) {
        auto within = [&](const float* p) {
            const float dx = pos[i][0] - p[0], dz = pos[i][2] - p[2];
            return dx * dx + dz * dz < gap * gap;
        };
        bool skip = within(first);
        for (int j = 0; j < k && !skip; ++j) skip = within(pos[out[j]]);
        if (!skip) out[k++] = i;
    }
    return k;
}
}
