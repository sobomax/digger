/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef NETSIM_H
#define NETSIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETSIM_CTRL_PAUSE  0x20U
#define NETSIM_CTRL_FREEZE 0x80U

enum netsim_title_status {
  NETSIM_TITLE_OFF = 0,
  NETSIM_TITLE_STARTING,
  NETSIM_TITLE_RUNNING,
  NETSIM_TITLE_REGISTERED
};

bool netsim_configure(const char *spec);
bool netsim_configured(void);
void netsim_sync_enabled(bool enabled);
size_t netsim_friend_count(void);
size_t netsim_friend_selected(void);
bool netsim_friend_get(size_t index, char *namebuf, size_t namebuf_len,
  unsigned int *games_playedp);
void netsim_friend_move(int delta);
bool netsim_start_session(bool initiated_locally);
void netsim_stop_session(bool send_exit);
void netsim_shutdown(void);
bool netsim_sync_frame(uint32_t frame, uint8_t local_bits, bool local_freeze,
  uint8_t *remote_bits, bool *remote_freeze, int *remote_lead_ms);
int netsim_local_player(void);
bool netsim_session_active(void);
bool netsim_peer_exited(void);
bool netsim_pump_title_events(void);
bool netsim_remote_start_requested(void);
enum netsim_title_status netsim_title_status_get(void);

#endif
