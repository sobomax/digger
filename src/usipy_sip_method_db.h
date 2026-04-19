#pragma once

#include "public/usipy_str.h"
#include "public/usipy_sip_method_types.h"

struct usipy_method_db_entr {
    struct usipy_str name;
    unsigned char cantype;
};

extern const struct usipy_method_db_entr usipy_method_db[USIPY_SIP_METHOD_max + 1];

const struct usipy_method_db_entr *usipy_method_db_lookup(const struct usipy_str *);
