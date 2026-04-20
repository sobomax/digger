#pragma once

#include <stddef.h>
#include <string.h>

#define USIPY_APPEND_MEM_OR_GOTO(label, dst, off, cap, bp, blen) do { \
        const size_t usipy_append_len = (size_t)(blen); \
        if ((off) + usipy_append_len > (cap)) \
            goto label; \
        memcpy((dst) + (off), (bp), usipy_append_len); \
        (off) += usipy_append_len; \
    } while (0)

#define USIPY_APPEND_MEM_OR_RETURN(retval, dst, off, cap, bp, blen) do { \
        const size_t usipy_append_len = (size_t)(blen); \
        if ((off) + usipy_append_len > (cap)) \
            return (retval); \
        memcpy((dst) + (off), (bp), usipy_append_len); \
        (off) += usipy_append_len; \
    } while (0)

#define USIPY_APPEND_STR_OR_GOTO(label, dst, off, cap, sp) \
    USIPY_APPEND_MEM_OR_GOTO(label, dst, off, cap, (sp)->s.ro, (sp)->l)

#define USIPY_APPEND_STR_OR_RETURN(retval, dst, off, cap, sp) \
    USIPY_APPEND_MEM_OR_RETURN(retval, dst, off, cap, (sp)->s.ro, (sp)->l)

#define USIPY_APPEND_CH_OR_GOTO(label, dst, off, cap, ch) do { \
        if ((off) + 1 > (cap)) \
            goto label; \
        (dst)[(off)++] = (ch); \
    } while (0)

#define USIPY_APPEND_CH_OR_RETURN(retval, dst, off, cap, ch) do { \
        if ((off) + 1 > (cap)) \
            return (retval); \
        (dst)[(off)++] = (ch); \
    } while (0)
