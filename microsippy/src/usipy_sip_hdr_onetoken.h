struct usipy_str;
struct usipy_msg_heap;
union usipy_sip_hdr_parsed;

union usipy_sip_hdr_parsed usipy_sip_hdr_1token_parse(struct usipy_msg_heap *,
  const struct usipy_str *);
int usipy_sip_hdr_1token_build(const union usipy_sip_hdr_parsed *, char *, size_t);
