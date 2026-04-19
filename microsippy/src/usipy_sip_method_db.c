#include <stdlib.h>
#include <string.h>

#include "bits/turbocompare.h"

#include "public/usipy_str.h"
#include "public/usipy_sip_method_types.h"
#include "usipy_sip_method_db.h"

const struct usipy_method_db_entr usipy_method_db[USIPY_SIP_METHOD_max + 1] = {
    [USIPY_SIP_METHOD_generic] = {.cantype = USIPY_SIP_METHOD_generic},
    [USIPY_SIP_METHOD_ACK] = {.cantype = USIPY_SIP_METHOD_ACK, .name = USIPY_2STR("ACK")},
    [USIPY_SIP_METHOD_BYE] = {.cantype = USIPY_SIP_METHOD_BYE, .name = USIPY_2STR("BYE")},
    [USIPY_SIP_METHOD_CANCEL] = {.cantype = USIPY_SIP_METHOD_CANCEL, .name = USIPY_2STR("CANCEL")},
    [USIPY_SIP_METHOD_INFO] = {.cantype = USIPY_SIP_METHOD_INFO, .name = USIPY_2STR("INFO")},
    [USIPY_SIP_METHOD_INVITE] = {.cantype = USIPY_SIP_METHOD_INVITE, .name = USIPY_2STR("INVITE")},
    [USIPY_SIP_METHOD_NOTIFY] = {.cantype = USIPY_SIP_METHOD_NOTIFY, .name = USIPY_2STR("NOTIFY")},
    [USIPY_SIP_METHOD_OPTIONS] = {.cantype = USIPY_SIP_METHOD_OPTIONS, .name = USIPY_2STR("OPTIONS")},
    [USIPY_SIP_METHOD_PRACK] = {.cantype = USIPY_SIP_METHOD_PRACK, .name = USIPY_2STR("PRACK")},
    [USIPY_SIP_METHOD_REFER] = {.cantype = USIPY_SIP_METHOD_REFER, .name = USIPY_2STR("REFER")},
    [USIPY_SIP_METHOD_REGISTER] = {.cantype = USIPY_SIP_METHOD_REGISTER, .name = USIPY_2STR("REGISTER")},
    [USIPY_SIP_METHOD_SUBSCRIBE] = {.cantype = USIPY_SIP_METHOD_SUBSCRIBE, .name = USIPY_2STR("SUBSCRIBE")},
};

#define TOTAL_KEYWORDS 10
#define MIN_WORD_LENGTH 3
#define MAX_WORD_LENGTH 9
#define MIN_HASH_VALUE 3
#define MAX_HASH_VALUE 13
/* maximum key range = 11, duplicates = 0 */

static unsigned int
method_hash(const struct usipy_str *sp)
{
    static unsigned char asso_values[] = {
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 10,  0,  5, 14, 14,
      14, 14, 14,  0, 14, 14, 14, 14, 14,  0,
       5, 14,  0,  0, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 10,  0,  5,
      14, 14, 14, 14, 14,  0, 14, 14, 14, 14,
      14,  0,  5, 14,  0,  0, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
      14, 14, 14, 14, 14, 14
    };
    return (sp->l + asso_values[(unsigned char)sp->s.ro[0]]);
}

const struct usipy_method_db_entr *
usipy_method_db_lookup(const struct usipy_str *tp)
{
    static const struct usipy_method_db_entr *wordlist[] = {
      &usipy_method_db[USIPY_SIP_METHOD_generic],
      &usipy_method_db[USIPY_SIP_METHOD_generic],
      &usipy_method_db[USIPY_SIP_METHOD_generic],
      &usipy_method_db[USIPY_SIP_METHOD_BYE],
      &usipy_method_db[USIPY_SIP_METHOD_INFO],
      &usipy_method_db[USIPY_SIP_METHOD_REFER],
      &usipy_method_db[USIPY_SIP_METHOD_INVITE],
      &usipy_method_db[USIPY_SIP_METHOD_OPTIONS],
      &usipy_method_db[USIPY_SIP_METHOD_REGISTER],
      &usipy_method_db[USIPY_SIP_METHOD_SUBSCRIBE],
      &usipy_method_db[USIPY_SIP_METHOD_PRACK],
      &usipy_method_db[USIPY_SIP_METHOD_CANCEL],
      &usipy_method_db[USIPY_SIP_METHOD_generic],
      &usipy_method_db[USIPY_SIP_METHOD_ACK],
    };

    if (tp->l == sizeof("NOTIFY") - 1 &&
      turbo_casebcmp(tp->s.ro, "NOTIFY", sizeof("NOTIFY") - 1) == 0) {
        return (&usipy_method_db[USIPY_SIP_METHOD_NOTIFY]);
    }
    if (tp->l <= MAX_WORD_LENGTH && tp->l >= MIN_WORD_LENGTH) {
        int key = method_hash(tp);

        if (key <= MAX_HASH_VALUE && key >= 0) {
            const struct usipy_method_db_entr *wp = wordlist[key];

            if (wp->name.l == tp->l && turbo_casebcmp(tp->s.ro, wp->name.s.ro, wp->name.l) == 0)
                return (wp);
        }
    }
    return (&usipy_method_db[USIPY_SIP_METHOD_generic]);
}
