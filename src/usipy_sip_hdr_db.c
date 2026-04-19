#include <stdint.h>
#include <string.h>

#include "usipy_types.h"
#include "public/usipy_str.h"
#include "usipy_fast_parser.h"
#include "public/usipy_sip_hdr_types.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_tvpair.h"
#include "usipy_sip_hdr_via.h"
#include "usipy_sip_hdr_cseq.h"
#include "usipy_sip_hdr_onetoken.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_hdr_auth.h"
#include "usipy_sip_hdr_authz.h"

#include "bits/turbocompare.h"

#include "usipy_sip_hdr_db_pdata.h"

static const struct usipy_hdr_db_entr usipy_hdr_db[USIPY_HF_max + 1] = {
    [USIPY_HF_generic] = {.cantype = USIPY_HF_generic},
    [USIPY_HF_ACCEPT] = {.cantype = USIPY_HF_ACCEPT, .name = USIPY_2STR("Accept"),
     .flags.csl_allowed = 1},
    [USIPY_HF_ALLOW] = {.cantype = USIPY_HF_ALLOW, .name = USIPY_2STR("Allow"),
     .flags.csl_allowed = 1},
    [USIPY_HF_ALSO] = {.cantype = USIPY_HF_ALSO, .name = USIPY_2STR("Also")},
    [USIPY_HF_AUTHORIZATION] = {
     .cantype = USIPY_HF_AUTHORIZATION,
     .name = USIPY_2STR("Authorization"),
     .build = usipy_sip_hdr_authz_build,
     .parsed_memb_name = "authz",
     .dump = usipy_sip_hdr_authz_dump
    },
    [USIPY_HF_CCDIVERSION] = {.cantype = USIPY_HF_CCDIVERSION,
      .name = USIPY_2STR("CC-Diversion")},
    [USIPY_HF_CSEQ] = {
     .cantype = USIPY_HF_CSEQ,
     .name = USIPY_2STR("CSeq"),
     .dump = usipy_sip_hdr_cseq_dump,
     .parse = usipy_sip_hdr_cseq_parse,
     .build = usipy_sip_hdr_cseq_build,
     .parsed_memb_name = "cseq"
    },
    [USIPY_HF_CALLID] = {
     .cantype = USIPY_HF_CALLID,
     .name = USIPY_2STR("Call-ID"),
     .parse = usipy_sip_hdr_1token_parse,
     .build = usipy_sip_hdr_1token_build,
     .parsed_memb_name = "generic"
    },
    [USIPY_HF_CONTACT] = {
      .cantype = USIPY_HF_CONTACT,
      .name = USIPY_2STR("Contact"),
      .parse = usipy_sip_hdr_nameaddr_parse,
      .build = usipy_sip_hdr_nameaddr_build,
      .parsed_memb_name = "contact",
      .dump = usipy_sip_hdr_nameaddr_dump,
      .flags.csl_allowed = 1
    },
    [USIPY_HF_CONTENTLENGTH] = {.cantype = USIPY_HF_CONTENTLENGTH,
      .name = USIPY_2STR("Content-Length"), .build = usipy_sip_hdr_1token_build},
    [USIPY_HF_CONTENTTYPE] = {
      .cantype = USIPY_HF_CONTENTTYPE,
      .name = USIPY_2STR("Content-Type"),
      .parse = usipy_sip_hdr_1token_parse,
      .build = usipy_sip_hdr_1token_build,
      .parsed_memb_name = "generic"
    },
    [USIPY_HF_DIVERSION] = {.cantype = USIPY_HF_DIVERSION,
      .name = USIPY_2STR("Diversion")},
    [USIPY_HF_EXPIRES] = {
      .cantype = USIPY_HF_EXPIRES,
      .name = USIPY_2STR("Expires"),
      .parse = usipy_sip_hdr_1token_parse,
      .build = usipy_sip_hdr_1token_build,
      .parsed_memb_name = "generic"
    },
    [USIPY_HF_FROM] = {
      .cantype = USIPY_HF_FROM,
      .name = USIPY_2STR("From"),
      .parse = usipy_sip_hdr_nameaddr_parse,
      .build = usipy_sip_hdr_nameaddr_build,
      .parsed_memb_name = "from",
      .dump = usipy_sip_hdr_nameaddr_dump
    },
    [USIPY_HF_MAXFORWARDS] = {.cantype = USIPY_HF_MAXFORWARDS,
      .name = USIPY_2STR("Max-Forwards")},
    [USIPY_HF_PASSERTEDIDENTITY] = {.cantype = USIPY_HF_PASSERTEDIDENTITY,
      .name = USIPY_2STR("P-Asserted-Identity")},
    [USIPY_HF_PROXYAUTHENTICATE] = {
      .cantype = USIPY_HF_PROXYAUTHENTICATE,
      .name = USIPY_2STR("Proxy-Authenticate"),
      .parse = usipy_sip_hdr_auth_parse,
      .build = usipy_sip_hdr_auth_build,
      .parsed_memb_name = "auth",
      .dump = usipy_sip_hdr_auth_dump
    },
    [USIPY_HF_PROXYAUTHORIZATION] = {
      .cantype = USIPY_HF_PROXYAUTHORIZATION,
      .name = USIPY_2STR("Proxy-Authorization"),
      .build = usipy_sip_hdr_authz_build,
      .parsed_memb_name = "authz",
      .dump = usipy_sip_hdr_authz_dump
    },
    [USIPY_HF_RACK] = {.cantype = USIPY_HF_RACK, .name = USIPY_2STR("RAck")},
    [USIPY_HF_RSEQ] = {.cantype = USIPY_HF_RSEQ, .name = USIPY_2STR("RSeq")},
    [USIPY_HF_REASON] = {.cantype = USIPY_HF_REASON, .name = USIPY_2STR("Reason")},
    [USIPY_HF_RECORDROUTE] = {
      .cantype = USIPY_HF_RECORDROUTE,
      .name = USIPY_2STR("Record-Route"),
      .parse = usipy_sip_hdr_nameaddr_parse,
      .parsed_memb_name = "recordroute",
      .dump = usipy_sip_hdr_nameaddr_dump,
      .flags.csl_allowed = 1
    },
    [USIPY_HF_REFERTO] = {.cantype = USIPY_HF_REFERTO,
      .name = USIPY_2STR("Refer-To")},
    [USIPY_HF_REFERREDBY] = {.cantype = USIPY_HF_REFERREDBY,
      .name = USIPY_2STR("Referred-By")},
    [USIPY_HF_REPLACES] = {.cantype = USIPY_HF_REPLACES,
      .name = USIPY_2STR("Replaces")},
    [USIPY_HF_ROUTE] = {
      .cantype = USIPY_HF_ROUTE,
      .name = USIPY_2STR("Route"),
      .parse = usipy_sip_hdr_nameaddr_parse,
      .parsed_memb_name = "route",
      .dump = usipy_sip_hdr_nameaddr_dump,
      .flags.csl_allowed = 1
    },
    [USIPY_HF_SERVER] = {.cantype = USIPY_HF_SERVER, .name = USIPY_2STR("Server")},
    [USIPY_HF_SUPPORTED] = {.cantype = USIPY_HF_SUPPORTED,
      .name = USIPY_2STR("Supported"), .flags.csl_allowed = 1},
    [USIPY_HF_TO] = {
      .cantype = USIPY_HF_TO,
      .name = USIPY_2STR("To"),
      .parse = usipy_sip_hdr_nameaddr_parse,
      .build = usipy_sip_hdr_nameaddr_build,
      .parsed_memb_name = "to",
      .dump = usipy_sip_hdr_nameaddr_dump
    },
    [USIPY_HF_USERAGENT] = {.cantype = USIPY_HF_USERAGENT,
      .name = USIPY_2STR("User-Agent")},
    [USIPY_HF_VIA] = {
      .cantype = USIPY_HF_VIA,
      .name = USIPY_2STR("Via"),
      .dump = usipy_sip_hdr_via_dump,
      .parse = usipy_sip_hdr_via_parse,
      .build = usipy_sip_hdr_via_build,
      .parsed_memb_name = "via",
      .flags.csl_allowed = 1
    },
    [USIPY_HF_WWWAUTHENTICATE] = {
      .cantype = USIPY_HF_WWWAUTHENTICATE,
      .name = USIPY_2STR("WWW-Authenticate"),
      .parse = usipy_sip_hdr_auth_parse,
      .build = usipy_sip_hdr_auth_build,
      .parsed_memb_name = "auth",
      .dump = usipy_sip_hdr_auth_dump
    },
    [USIPY_HF_WARNING] = {.cantype = USIPY_HF_WARNING,
      .name = USIPY_2STR("Warning"), .flags.csl_allowed = 1},
    [USIPY_HF_CALLID_c] = {
      .cantype = USIPY_HF_CALLID,
      .name = USIPY_2STR("i"),
      .parse = usipy_sip_hdr_1token_parse,
      .build = usipy_sip_hdr_1token_build,
      .parsed_memb_name = "generic",
    },
    [USIPY_HF_CONTACT_c] = {
      .cantype = USIPY_HF_CONTACT,
      .name = USIPY_2STR("m"),
      .parse = usipy_sip_hdr_nameaddr_parse,
      .build = usipy_sip_hdr_nameaddr_build,
      .parsed_memb_name = "contact",
      .dump = usipy_sip_hdr_nameaddr_dump,
      .flags.csl_allowed = 1
    },
    [USIPY_HF_CONTENTLENGTH_c] = {.cantype = USIPY_HF_CONTENTLENGTH,
      .name = USIPY_2STR("l"), .build = usipy_sip_hdr_1token_build},
    [USIPY_HF_CONTENTTYPE_c] = {
      .cantype = USIPY_HF_CONTENTTYPE,
      .name = USIPY_2STR("c"),
      .parse = usipy_sip_hdr_1token_parse,
      .build = usipy_sip_hdr_1token_build,
      .parsed_memb_name = "generic",
    },
    [USIPY_HF_FROM_c] = {
      .cantype = USIPY_HF_FROM,
      .name = USIPY_2STR("f"),
      .parse = usipy_sip_hdr_nameaddr_parse,
      .build = usipy_sip_hdr_nameaddr_build,
      .parsed_memb_name = "from",
      .dump = usipy_sip_hdr_nameaddr_dump
    },
    [USIPY_HF_REFERTO_c] = {.cantype = USIPY_HF_REFERTO, .name = USIPY_2STR("r")},
    [USIPY_HF_SUPPORTED_c] = {.cantype = USIPY_HF_SUPPORTED,
      .name = USIPY_2STR("k"), .flags.csl_allowed = 1},
    [USIPY_HF_TO_c] = {
      .cantype = USIPY_HF_TO,
      .name = USIPY_2STR("t"),
      .parse = usipy_sip_hdr_nameaddr_parse,
      .build = usipy_sip_hdr_nameaddr_build,
      .parsed_memb_name = "to",
      .dump = usipy_sip_hdr_nameaddr_dump
    },
    [USIPY_HF_VIA_c] = {
      .cantype = USIPY_HF_VIA,
      .name = USIPY_2STR("v"),
      .dump = usipy_sip_hdr_via_dump,
      .parse = usipy_sip_hdr_via_parse,
      .build = usipy_sip_hdr_via_build,
      .parsed_memb_name = "via",
      .flags.csl_allowed = 1
    }
};

const struct usipy_hdr_db_entr *
usipy_hdr_db_lookup(const struct usipy_str *hname)
{
    int hid;
    const struct usipy_hdr_db_entr *r;

    hid = usipy_fp_classify(&hdr_pdata, hname);
    if (hid == -1)
        return (NULL);
    r = &usipy_hdr_db[hid];
    if (hid != USIPY_HF_generic) {
        if (r->name.l != hname->l || turbo_casebcmp(r->name.s.ro, hname->s.ro, hname->l) != 0) {
            hid = USIPY_HF_generic;
        }
    }
    return (r);
}

const struct usipy_hdr_db_entr *
usipy_hdr_db_byid(int hid)
{

    return (&usipy_hdr_db[hid]);
}
