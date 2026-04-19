#pragma once

#include <stddef.h>
#include <stdint.h>

#include "usipy_sip_tm.h"

struct usipy_sip_dialog;

struct usipy_sip_dialog *usipy_sip_dialog_uac_ctor(struct usipy_sip_tm *, size_t,
  const struct usipy_msg *);
struct usipy_sip_dialog *usipy_sip_dialog_uas_ctor(struct usipy_sip_tm *, size_t,
  const struct usipy_sip_tm_uas_response_params *);
void usipy_sip_dialog_dtor(struct usipy_sip_dialog *);
int usipy_sip_dialog_matches_uas_transaction(const struct usipy_sip_dialog *,
  const struct usipy_msg *);
int usipy_sip_dialog_handle_uas_transaction(struct usipy_sip_dialog *, size_t,
  const struct usipy_msg *);
int usipy_sip_dialog_end(struct usipy_sip_dialog *,
  const struct usipy_sip_tm_uac_callbacks *, size_t *);
