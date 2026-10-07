#pragma once

#include <stddef.h>

struct usipy_sip_dialog;
struct usipy_msg;

/* Validate and end the dialog for a peer BYE without queuing its response. */
int usipy_sip_dialog_accept_uas_bye(struct usipy_sip_dialog *, size_t,
  const struct usipy_msg *);

/* Told when an accepted call ends for want of the ACK of its 2xx, after the
 * dialog has sent the BYE (its transaction index, or none if it couldn't) */
void usipy_sip_dialog_set_no_ack_handler(struct usipy_sip_dialog *,
  void (*)(void *, size_t), void *);
