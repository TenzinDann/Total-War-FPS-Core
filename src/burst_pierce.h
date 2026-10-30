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
}
