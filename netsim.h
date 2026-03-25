/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef NETSIM_H
#define NETSIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETSIM_CTRL_PAUSE  0x20U
#define NETSIM_CTRL_FREEZE 0x80U

bool netsim_configure(const char *spec);
bool netsim_configured(void);
bool netsim_begin_wait(void);
bool netsim_start_session(bool initiated_locally);
void netsim_stop_session(bool send_exit);
void netsim_shutdown(void);
bool netsim_sync_frame(uint32_t frame, uint8_t local_bits, bool local_freeze,
  uint8_t *remote_bits, bool *remote_freeze);
int netsim_local_player(void);
bool netsim_session_active(void);
bool netsim_peer_exited(void);
bool netsim_remote_start_requested(void);

#endif
