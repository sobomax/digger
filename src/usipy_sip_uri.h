struct usipy_msg_heap;

struct usipy_sip_uri {
    struct usipy_str proto;
    struct usipy_str user;
    struct usipy_str password;
    struct usipy_str host;
    unsigned int port;
    int nparams;
    int nhdrs;
    struct usipy_tvpair *parameters;
    struct usipy_tvpair *headers;
    struct usipy_tvpair _tvpstorage[0];
};

struct usipy_sip_uri *usipy_sip_uri_parse(struct usipy_msg_heap *,
  const struct usipy_str *);
int usipy_sip_uri_build(const struct usipy_sip_uri *, char *, size_t);
void usipy_sip_uri_dump(const struct usipy_sip_uri *, const char *,
  const char *);
