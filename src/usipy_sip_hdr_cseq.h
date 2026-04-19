struct usipy_msg_heap;
struct usipy_method_db_entr;

struct usipy_sip_hdr_cseq {
    struct {
        struct usipy_str method;
    } onwire;
    uint32_t val;
    const struct usipy_method_db_entr *method;
};

union usipy_sip_hdr_parsed usipy_sip_hdr_cseq_parse(struct usipy_msg_heap *,
  const struct usipy_str *);
int usipy_sip_hdr_cseq_build(const union usipy_sip_hdr_parsed *, char *, size_t);
void usipy_sip_hdr_cseq_dump1(const struct usipy_sip_hdr_cseq *, const char *,
  const char *, const char *);
void usipy_sip_hdr_cseq_dump(const union usipy_sip_hdr_parsed *, const char *,
  const char *, const char *);
