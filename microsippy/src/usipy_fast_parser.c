#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bits/endian.h"

#include "public/usipy_str.h"
#include "usipy_fast_parser.h"

int
usipy_fp_classify(const struct usipy_fast_parser *fp, const struct usipy_str *sp)
{
    uint32_t cval, res;

    res = sp->l;
    for (int i = 0; i < sp->l; i += sizeof(cval)) {
        int remain = sp->l - i;
        if (remain < sizeof(cval)) {
            cval = 0;
            memcpy(&cval, sp->s.ro + i, remain);
        } else {
            memcpy(&cval, sp->s.ro + i, sizeof(cval));
        }
        /* Convert to lower case */
        cval = HTOLE(cval | 0x20202020);
        /* Apply Magick */
        cval ^= fp->magic;
        res += cval;
    }
    if (sp->l > 1)
        res >>= (sp->l - 1);
    return (fp->toid[res & 0xff]);
}
