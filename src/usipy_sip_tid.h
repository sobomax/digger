struct usipy_str;
struct usipy_sip_hdr_cseq;

struct usipy_sip_tid {
    uint32_t hash;
    const struct usipy_str *call_id;
    const struct usipy_str *from_tag;
    const struct usipy_sip_hdr_cseq *cseq;
    const struct usipy_str *vbranch;
};

#define USIPY_HF_TID_MASK ( \
  USIPY_HFT_MASK(USIPY_HF_CSEQ) | \
  USIPY_HFT_MASK(USIPY_HF_CALLID) | \
  USIPY_HFT_MASK(USIPY_HF_VIA) | \
  USIPY_HFT_MASK(USIPY_HF_FROM) \
)

void usipy_sip_tid_dump(const struct usipy_sip_tid *, const char *,
  const char *);
uint32_t usipy_sip_tid_hash(const struct usipy_sip_tid *);
uint32_t usipy_sip_dialog_hash(const struct usipy_str *,
  const struct usipy_str *, const struct usipy_str *);
uint32_t usipy_sip_dialog_tid_hash(const struct usipy_str *,
  const struct usipy_str *, const struct usipy_str *, uint32_t, uint8_t);
