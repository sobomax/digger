/* Digger Reloaded
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef NETSIM_FRIENDS_H
#define NETSIM_FRIENDS_H

#include <stdbool.h>
#include <stddef.h>

#include "netsim_sip.h"

void netsim_friends_reset(void);
void netsim_friends_load(void);
void netsim_friends_save(void);
void netsim_friends_configure(const struct usipy_str *primary_friend);
void netsim_friend_registered(const char *name);
void netsim_friend_touch(const char *name);
const char *netsim_friend_selected_name(void);

size_t netsim_friend_count(void);
size_t netsim_friend_selected(void);
bool netsim_friend_get(size_t index, char *namebuf, size_t namebuf_len,
  unsigned int *games_playedp);
void netsim_friend_move(int delta);

#endif
