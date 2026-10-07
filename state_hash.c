/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "def.h"
#include "drawing.h"
#include "game.h"
#include "scores.h"
#include "state_hash.h"

void
state_hash_init(struct state_hash *shp)
{

  md5_init(&shp->ctx);
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define STATE_HASH_BE 1
/* The platform's own byte swapping, where it has it */
#if defined(__has_include)
#if __has_include(<sys/endian.h>)
#include <sys/endian.h>
#elif __has_include(<endian.h>)
#include <endian.h>
#endif
#endif
#if defined(htole16) && defined(htole32) && defined(htole64)
#define STATE_HASH_HTOLE 1
#endif
#endif

#ifdef STATE_HASH_BE
/* One value of sz bytes, little-endian */
static void
state_hash_le(struct state_hash *shp, const unsigned char *cp, size_t sz)
{
  unsigned char b[sizeof(uint64_t)];
  size_t i;
#ifdef STATE_HASH_HTOLE
  uint16_t v16;
  uint32_t v32;
  uint64_t v64;

  switch (sz) {
  case sizeof(v16):
    memcpy(&v16, cp, sz);
    v16 = htole16(v16);
    md5_update(&shp->ctx, &v16, sz);
    return;

  case sizeof(v32):
    memcpy(&v32, cp, sz);
    v32 = htole32(v32);
    md5_update(&shp->ctx, &v32, sz);
    return;

  case sizeof(v64):
    memcpy(&v64, cp, sz);
    v64 = htole64(v64);
    md5_update(&shp->ctx, &v64, sz);
    return;
  }
#endif

  for (i = 0; i < sz; i++)
    b[i] = cp[sz - 1 - i];
  md5_update(&shp->ctx, b, sz);
}
#endif

/* n values of sz bytes each */
void
state_hash_array(struct state_hash *shp, const void *p, size_t sz, size_t n)
{
#ifdef STATE_HASH_BE
  const unsigned char *cp;

  if (sz > 1 && sz <= sizeof(uint64_t)) {
    for (cp = p; n > 0; n--, cp += sz)
      state_hash_le(shp, cp, sz);
    return;
  }
#endif
  md5_update(&shp->ctx, p, sz * n);
}

/* One value of sz bytes */
void
state_hash_mix(struct state_hash *shp, const void *p, size_t sz)
{

  state_hash_array(shp, p, sz, 1);
}

/* The first 32 bits of the digest, so that it prints as the MD5 starts */
uint32_t
state_hash_final(struct state_hash *shp)
{
  unsigned char d[MD5_SIZE];

  md5_finalize(&shp->ctx, d);
  return (((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
    ((uint32_t)d[2] << 8) | (uint32_t)d[3]);
}

static void
field_hash_append(struct state_hash *shp)
{

  STATE_HASH_ARR(shp, field, MSIZE);
}

static uint32_t
module_hash(void (*append)(struct state_hash *))
{
  struct state_hash sh;

  state_hash_init(&sh);
  append(&sh);
  return (state_hash_final(&sh));
}

uint32_t
field_hash(void)
{

  return (module_hash(field_hash_append));
}

uint32_t
digger_debug_hash(void)
{

  return (module_hash(digger_debug_hash_append));
}

uint32_t
bags_debug_hash(void)
{

  return (module_hash(bags_debug_hash_append));
}

uint32_t
monster_debug_hash(void)
{

  return (module_hash(monster_debug_hash_append));
}

static void
game_state_append(struct state_hash *shp)
{
  int32_t score;
  int i;

  field_hash_append(shp);
  digger_debug_hash_append(shp);
  monster_debug_hash_append(shp);
  bags_debug_hash_append(shp);
  STATE_HASH_VAR(shp, dgstate.randv);
  for (i = 0; i < 2; i++) {
    score = gettscore(i);
    STATE_HASH_VAR(shp, score);
  }
  if (dgstate.gauntlet) {
    STATE_HASH_VAR(shp, dgstate.cgtime);
    STATE_HASH_VAR(shp, dgstate.timeout);
  }
}

/* All of the game state that matters for determinism in one value */
uint32_t
game_state_hash(void)
{

  return (module_hash(game_state_append));
}

/* The same, but also of when it was taken */
uint32_t
game_state_hash_tick(uint32_t tick)
{
  struct state_hash sh;

  state_hash_init(&sh);
  game_state_append(&sh);
  STATE_HASH_VAR(&sh, tick);
  return (state_hash_final(&sh));
}
