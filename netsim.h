/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef NETSIM_H
#define NETSIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETSIM_CTRL_PAUSE  0x20U
#define NETSIM_CTRL_QUIT   0x40U
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
/* A call before its game, see netsim_call_poll() */
enum netsim_call_state {
  NETSIM_CALL_WAITING = 0,
  NETSIM_CALL_RINGING, /* Put to the peer's player, ours */
  NETSIM_CALL_ANSWERED, /* Ours, its game to start */
  NETSIM_CALL_GONE /* Hung up, declined or failed */
};

bool netsim_call_start(void);
enum netsim_call_state netsim_call_poll(void);
void netsim_call_ring(void);
void netsim_call_decline(void);
void netsim_call_hangup(void);
void netsim_call_peer(char *namebuf, size_t namebuf_len);
bool netsim_start_session(bool initiated_locally);
void netsim_stop_session(void);
bool netsim_await_peer_hangup(void);
void netsim_shutdown(void);
bool netsim_sync_frame(uint32_t frame, uint8_t local_bits, bool local_freeze,
  uint8_t *remote_bits, bool *remote_freeze, int *remote_lead_ms,
  bool (*quit_requested)(void));
int netsim_local_player(void);
uint64_t netsim_session_nonce(void);
bool netsim_session_active(void);
#ifdef DIGGER_INSTRUMENTATION
bool netsim_no_ack_seen(void);
#endif
bool netsim_peer_exited(void);
bool netsim_pump_title_events(void);
bool netsim_remote_start_requested(void);
enum netsim_title_status netsim_title_status_get(void);

#endif
