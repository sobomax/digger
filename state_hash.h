/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "microsippy/src/external/mackron_md5/md5.h"

/*
 * Checksums of the game state: an MD5 of the values that matter for
 * determinism, each fed in at its own (fixed) size, little-endian so that it
 * comes out the same on any host, of which the first 32 bits are kept.
 */

struct state_hash {
  md5_context ctx;
};

void state_hash_init(struct state_hash *shp);
void state_hash_array(struct state_hash *shp, const void *p, size_t sz,
  size_t n);
void state_hash_mix(struct state_hash *shp, const void *p, size_t sz);
uint32_t state_hash_final(struct state_hash *shp);

/* A variable, or an array of them, as it is */
#define STATE_HASH_VAR(shp, v) state_hash_mix((shp), &(v), sizeof(v))
#define STATE_HASH_ARR(shp, a, n) \
  state_hash_array((shp), (a), sizeof((a)[0]), (n))

/* Each module feeds its own part of the state */
void digger_debug_hash_append(struct state_hash *shp);
void bags_debug_hash_append(struct state_hash *shp);
void monster_debug_hash_append(struct state_hash *shp);

uint32_t field_hash(void);
uint32_t digger_debug_hash(void);
uint32_t bags_debug_hash(void);
uint32_t monster_debug_hash(void);
uint32_t game_state_hash(void);
uint32_t game_state_hash_tick(uint32_t tick);
