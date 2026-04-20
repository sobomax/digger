#pragma once

#include <stddef.h>
#include <string.h>

static inline void *
memmem(const void *haystackp, size_t haystack_len, const void *needlep,
  size_t needle_len)
{
    const unsigned char *haystack = haystackp;
    const unsigned char *needle = needlep;

    if (needle_len == 0) {
        return ((void *)haystackp);
    }
    if (haystackp == NULL || needlep == NULL || haystack_len < needle_len) {
        return (NULL);
    }
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (haystack[i] == needle[0] &&
          memcmp(haystack + i, needle, needle_len) == 0) {
            return ((void *)(haystack + i));
        }
    }
    return (NULL);
}
