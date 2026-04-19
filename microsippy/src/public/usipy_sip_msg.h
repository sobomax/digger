#pragma once

#include <stdint.h>

#include "usipy_str.h"
#include "usipy_msg_heap.h"
#include "usipy_sip_sline.h"

struct usipy_sip_hdr;
struct usipy_sip_tid;

struct usipy_sip_hdr_match {
    size_t hdrslen;
    size_t nhdrs;
    const struct usipy_sip_hdr *hdrsp[];
};

#define USIPY_SIP_HDR_MATCH_SIZE(nhdrs) \
  (sizeof(struct usipy_sip_hdr_match) + \
  (sizeof(const struct usipy_sip_hdr *) * (nhdrs)))

enum usipy_sip_msg_kind {
  USIPY_SIP_MSG_UNKN = -1,
  USIPY_SIP_MSG_REQ = 0,
  USIPY_SIP_MSG_RES = 1
};

struct usipy_msg {
   struct usipy_str onwire;
   struct usipy_str body;
   enum usipy_sip_msg_kind kind;
   struct usipy_sip_sline sline;
   struct usipy_sip_hdr *hdrs;
   unsigned int nhdrs;
   struct usipy_msg_heap heap;
   struct {
       uint64_t present;
       uint64_t parsed;
   } hdr_masks;
   char _storage[0];
};

struct usipy_codeptr {
    const char *fname;
    int linen;
    const char *funcn;
};

struct usipy_msg_parse_err {
    int erRNo;
    struct usipy_codeptr loc;
    const char *reason;
};

#define USIPY_MSG_PARSE_ERR_init (struct usipy_msg_parse_err){ \
  .erRNo = 0, .loc.fname = NULL, .loc.linen = 0, .loc.funcn = NULL \
}

struct usipy_msg *usipy_sip_msg_ctor_fromwire(const char *, size_t,
  struct usipy_msg_parse_err *);
struct usipy_msg *usipy_sip_msg_build_fromwire(struct usipy_msg_heap *,
  const char *, size_t, struct usipy_msg_parse_err *);
void usipy_sip_msg_dtor(struct usipy_msg *);
int usipy_sip_msg_build(struct usipy_msg_heap *, struct usipy_msg *,
  struct usipy_str *);
void usipy_sip_msg_dump(const struct usipy_msg *, const char *);
int usipy_sip_msg_parse_hdrs(struct usipy_msg *, uint64_t, int);
int usipy_sip_msg_parse_hdrs_get(struct usipy_msg *, uint64_t, int,
  struct usipy_sip_hdr_match *);
int usipy_sip_msg_get_tid(struct usipy_msg *, struct usipy_sip_tid *);

#define USIPY_HFT_MASK(hft) ((uint64_t)1 << (hft))
#define USIPY_HF_MASK(shp) (USIPY_HFT_MASK((shp)->hf_type->cantype))
#define USIPY_HF_ISMSET(msk, h) ((msk) & USIPY_HFT_MASK(h))

#define USIPY_MSG_HDR_PARSED(msp, h) (USIPY_HF_ISMSET(msp->hdr_masks.parsed, (h)))
#define USIPY_MSG_HDR_PRESENT(msp, h) (USIPY_HF_ISMSET(msp->hdr_masks.present, (h)))
