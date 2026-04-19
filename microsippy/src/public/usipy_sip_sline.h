#pragma once

struct usipy_method_db_entr;
struct usipy_msg_heap;
struct usipy_sip_uri;

struct usipy_sip_status {
    unsigned int code;
    struct usipy_str reason_phrase;
};

struct usipy_sip_status_line {
    struct usipy_str version;
    struct usipy_sip_status status;
};

struct usipy_sip_request_line {
    struct usipy_str version;
    struct {
        struct usipy_str method;
        struct usipy_str ruri;
        struct usipy_str version;
    } onwire;
    struct usipy_sip_uri *ruri;
    const struct usipy_method_db_entr *method;
};

struct usipy_sip_sline {
    struct usipy_str onwire;
    union {
        struct usipy_sip_status_line sl;
        struct usipy_sip_request_line rl;
    } parsed;
};

enum usipy_sip_msg_kind usipy_sip_sline_parse(struct usipy_msg_heap *,
  struct usipy_sip_sline *);

#define USIPY_SIP_SCODE_MIN 100
#define USIPY_SIP_SCODE_MAX 999
