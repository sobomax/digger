#include <stdlib.h>

#include "public/usipy_str.h"

const struct usipy_str usipy_sip_version = USIPY_2STR("SIP/2.0");
#define CHLOWER(ch) ((ch) | 32)
#define CHCASECMP(ch1, ch2) ((ch1) == (ch2) || (ch1) == CHLOWER(ch2))

#define chk_sip_version(ch) ( \
  CHCASECMP((ch)[0], usipy_sip_version.s.ro[0]) && \
  CHCASECMP((ch)[1], usipy_sip_version.s.ro[1]) && \
  CHCASECMP((ch)[2], usipy_sip_version.s.ro[2]) && \
  (ch)[3] == usipy_sip_version.s.ro[3] && \
  (ch)[4] == usipy_sip_version.s.ro[4] && \
  (ch)[5] == usipy_sip_version.s.ro[5] && \
  (ch)[6] == usipy_sip_version.s.ro[6] \
)

int
usipy_verify_sip_version(const struct usipy_str *vp)
{

    return (vp->l == 7 && chk_sip_version(vp->s.ro));
}
