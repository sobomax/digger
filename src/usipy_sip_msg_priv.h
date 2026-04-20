#pragma once

#include <stddef.h>

#include "public/usipy_msg_heap.h"
#include "usipy_sip_hdr.h"

#define USIPY_SIP_MSG_NHDRS_HINT (30)

static inline size_t
usipy_sip_msg_extra_heap_size(size_t raw_len)
{
    const size_t min_extra = sizeof(struct usipy_sip_hdr) *
      USIPY_SIP_MSG_NHDRS_HINT;

    return (USIPY_ALIGNED_SIZE(raw_len < min_extra ? min_extra : raw_len));
}
