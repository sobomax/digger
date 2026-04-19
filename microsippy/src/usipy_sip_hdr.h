struct usipy_hdr_db_entr;
struct usipy_sip_hdr_cseq;
struct usipy_sip_hdr_via;
struct usipy_sip_hdr_nameaddr;
struct usipy_sip_hdr_auth;
struct usipy_sip_hdr_authz;

union usipy_sip_hdr_parsed {
    struct usipy_sip_hdr_via *via;
    struct usipy_sip_hdr_cseq *cseq;
    struct usipy_sip_hdr_auth *auth;
    struct usipy_sip_hdr_authz *authz;
    struct usipy_sip_hdr_nameaddr *to;
    struct usipy_sip_hdr_nameaddr *from;
    struct usipy_sip_hdr_nameaddr *contact;
    struct usipy_sip_hdr_nameaddr *route;
    struct usipy_sip_hdr_nameaddr *recordroute;
    const struct usipy_str *generic;
};

struct usipy_sip_hdr {
    struct {
        struct usipy_str full;
        struct usipy_str name;
        struct usipy_str value;
	const struct usipy_hdr_db_entr *hf_type;
    } onwire;
    const struct usipy_hdr_db_entr *hf_type;
#if 0
    const char *col_offst;
#endif
    union usipy_sip_hdr_parsed parsed;
};

int usipy_sip_hdr_preparse(struct usipy_sip_hdr *, struct usipy_sip_hdr *);
