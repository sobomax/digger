#pragma once

#include <stdint.h>

#include "public/usipy_msg_heap.h"
#include "public/usipy_sip_tm.h"
#include "usipy_tvpair.h"
#include "usipy_sip_hdr_cseq.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_hdr_via.h"
#include "usipy_sip_uri.h"

struct usipy_sip_tm_uac_cache {
    struct usipy_sip_uri *request_uri;
    struct usipy_str from_uri;
    struct usipy_str to_uri;
    struct usipy_str contact_uri;
    struct usipy_str *routes;
    size_t nroutes;
    uint32_t contact_expires;
    uint32_t invite_expires;
    uint8_t include_contact;
    uint8_t _pad0[3];
};

struct usipy_sip_tm_uas_cache {
    struct usipy_str from;
    struct usipy_str to;
    struct usipy_str request_uri;
    struct usipy_str from_uri;
    struct usipy_str to_uri;
    struct usipy_str contact_uri;
    struct usipy_str *vias;
    struct usipy_str *record_routes;
    uint32_t ack_hash;
    uint32_t cancel_hash;
    size_t nvias;
    size_t nrecord_routes;
};

struct usipy_sip_tm_cache {
    struct usipy_str call_id;
    struct usipy_str branch;
    struct usipy_str from_tag;
    struct usipy_str to_tag;
    struct usipy_sip_hdr_cseq cseq;
    uint8_t method_type;
    uint8_t _pad0[3];
    union {
        struct usipy_sip_tm_uac_cache uac;
        struct usipy_sip_tm_uas_cache uas;
    };
};

enum usipy_sip_tm_invite_cancel_state {
    USIPY_SIP_TM_INVITE_CANCEL_NONE = 0,
    USIPY_SIP_TM_INVITE_CANCEL_SCHEDULED,
    USIPY_SIP_TM_INVITE_CANCEL_ONWIRE
};

struct usipy_sip_tm_txi {
    struct usipy_sip_tm_tx pub;
    struct usipy_sip_tm_cache cache;
    struct usipy_sip_tm_uac_callbacks callbacks;
    struct usipy_sip_tm_uas_callbacks uas_callbacks;
    struct {
        size_t checkpoint;
        struct usipy_sip_tm_outbound pub;
    } outbound;
    struct usipy_msg_heap scratch;
    size_t scratch_checkpoints[2];
    size_t parent_index;
    size_t child_index;
    uint64_t invite_timeout_at_ms;
    void *scratch_buf;
    size_t scratch_capacity;
    uint8_t final_reported;
    uint8_t invite_provisional_seen;
    uint8_t invite_cancel_state;
    uint8_t invite_timeout_id;
    uint8_t _pad0[3];
    int active;
};

struct usipy_sip_tm_default_via {
    struct usipy_sip_hdr_via via;
    struct usipy_tvpair params[2];
};

struct usipy_sip_tm_default_nameaddr {
    struct usipy_sip_hdr_nameaddr nameaddr;
    struct usipy_tvpair params[1];
};

struct usipy_sip_tm {
    int sock;
    enum usipy_sip_tm_transport transport;
    size_t max_transactions;
    size_t nactive;
    struct usipy_msg_heap heap;
    void *heap_buf;
    struct usipy_sip_tm_addr laddr;
    struct usipy_str luri;
    struct usipy_sip_tm_default_via default_via;
    struct usipy_sip_tm_default_nameaddr default_from;
    struct usipy_sip_tm_default_nameaddr default_to;
    struct usipy_sip_tm_default_nameaddr default_contact;
    struct usipy_sip_tm_callbacks callbacks;
    struct usipy_sip_tm_id_policy id_policy;
    struct usipy_sip_tm_txi *transactions;
};
