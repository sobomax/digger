#include <stdlib.h>
#include <string.h>

#include "usipy_debug.h"
#include "usipy_types.h"
#include "public/usipy_str.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_onetoken.h"

union usipy_sip_hdr_parsed
usipy_sip_hdr_1token_parse(struct usipy_msg_heap *hp, const struct usipy_str *hvp)
{
    union usipy_sip_hdr_parsed ru;

    ru.generic = hvp;
    return (ru);
}

int
usipy_sip_hdr_1token_build(const union usipy_sip_hdr_parsed *up, char *buf, size_t len)
{
    USIPY_DASSERT(up->generic != NULL);
    if (up->generic->l > len) {
        return (-1);
    }
    memcpy(buf, up->generic->s.ro, up->generic->l);
    return ((int)up->generic->l);
}
