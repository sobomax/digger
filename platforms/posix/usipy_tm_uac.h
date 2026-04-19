#pragma once

#include <stdint.h>

#include "public/microsippy.h"

#define USIPY_TM_UAC_ID_SEED_HEXLEN 16u

struct usipy_tm_uac_production_ids {
    char branch_seed[USIPY_TM_UAC_ID_SEED_HEXLEN + 1];
    char local_tag[USIPY_TM_UAC_ID_SEED_HEXLEN + 1];
    char call_id[96];
    struct usipy_str call_id_s;
};

uint64_t usipy_tm_uac_mono_ms(void);
void usipy_tm_uac_sleep_until_ms(uint64_t);

int usipy_tm_uac_register_reply_auth(struct usipy_sip_tm *, size_t,
  const struct usipy_msg *, const struct usipy_str *, const struct usipy_str *,
  const struct usipy_str *, const struct usipy_sip_tm_extra_header *, size_t);
int usipy_tm_uac_extract_register_expires(const struct usipy_msg *,
  const struct usipy_str *, unsigned int *);

int usipy_tm_uac_production_ids_init(struct usipy_tm_uac_production_ids *);
int usipy_tm_uac_production_id_policy(void *, struct usipy_msg_heap *,
  const struct usipy_sip_tm_id_policy_in *,
  struct usipy_sip_tm_id_policy_out *);
