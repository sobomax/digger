#pragma once

#include <stdint.h>

#include_next <strings.h>

static inline int
usipy_ffsl_uintptr(uintptr_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    if (v == 0) {
        return (0);
    }
    return (__builtin_ffsll((long long)v));
#else
    int bit = 1;

    if (v == 0) {
        return (0);
    }
    while ((v & 1u) == 0) {
        v >>= 1;
        bit++;
    }
    return (bit);
#endif
}

#define ffsl(v) usipy_ffsl_uintptr((uintptr_t)(v))
