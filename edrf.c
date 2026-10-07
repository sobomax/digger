/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

/*
 * eDRF body, after the same header as a DRF but for the maps (version,
 * mode, bonus score): records, one per line, in the order of the game, so
 * that a recording can be written as the game goes and read as it is
 * played back:
 *
 *   I<slots> <runs>...
 *                   controls on each game tick of a chunk of EDRF_CHUNK
 *                   ticks (or fewer, before another record), of the input
 *                   slots listed (e.g. I01 for slots 0 and 1), for each one
 *                   as runs of '@'+bits (INPUT_CTRL_*) optionally followed
 *                   by a repeat count; the slots left out had no controls
 *                   on as many ticks, and with none listed ("I A5@27") the
 *                   runs are those of every slot (a slot not read on a tick
 *                   could have had any)
 *   L <level> [<n>] a level starting, on map <n> (they are numbered from 1
 *    <row>          as they come), or a new one given after it, a row of it
 *                   per line, after a space; a playback plays the level on
 *                   it
 *   R <hex>         random seed of the round starting
 *   C <hex>         game state hash every EDRF_CKPT_EVERY ticks
 *   E <hex>         game state hash at the end of a round, the tick it ends
 *                   on in it too
 *   Z <tick> <hex>  game state hash at the end of the game
 *
 * A recording that ends without its Z (the program was stopped while it
 * was being made) plays back as far as it goes.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "def.h"
#include "edrf.h"
#include "game.h"
#include "input.h"
#include "main.h"
#include "state_hash.h"
#include "record.h"
#include "scores.h"

#define EDRF_SLOTS 2
#define EDRF_CHUNK 32
#define EDRF_CKPT_EVERY (8 * EDRF_CHUNK)
#define EDRF_UNREAD 0x80 /* A slot's controls on a tick it wasn't read */

bool edrf_playing=false;
bool edrf_failed=false;
bool edrf_stopped=false;
bool edrf_truncated=false;
bool edrf_checked=false;
bool edrf_feeding=false;
static int feedslot=-1;

/* A queue: pushed at the end, popped from the front */
struct vec {
  uint32_t *v;
  size_t len, cap, pos;
};

/* Being recorded: the chunk's controls, streamed to recfp */
static struct vec rec_in[EDRF_SLOTS];
static FILE *recfp;
static bool slotread[EDRF_SLOTS]; /* On this tick */
static uint32_t rec_endtick, rec_endhash;
/* Played back: what has been read from plyfp, and not used yet */
static struct vec ply_in[EDRF_SLOTS], ply_rand, ply_ckpt, ply_rend;
static FILE *plyfp;
static bool plyeof, ply_hasend;
/* Levels read: the number, then the map; and the one being read */
static struct vec ply_lev;
static uint8_t lev_new[MSIZE];
static uint32_t lev_newno;
static int lev_rows = -1;

/* The maps a stream has had, written or read */
struct maps {
  uint8_t (*m)[MSIZE];
  int n, cap;
};
static struct maps rec_maps, ply_maps;
static int8_t ply_savedmaps[8][MHEIGHT][MWIDTH + 1];

static int
maps_add(struct maps *mp, const uint8_t *map)
{
  uint8_t (*nm)[MSIZE];

  if (mp->n == mp->cap) {
    mp->cap = mp->cap ? mp->cap * 2 : 16;
    nm = realloc(mp->m, mp->cap * sizeof(*nm));
    if (nm == NULL)
      abort();
    mp->m = nm;
  }
  memcpy(mp->m[mp->n], map, MSIZE);
  return (++mp->n);
}

/* As a map is written: anything but the letters is nothing, e.g. the NUL a
   short line of a DRF's header leaves */
static uint8_t
mapch(int c)
{

  return (c > ' ' && c <= '~' ? c : ' ');
}


static int
maps_find(const struct maps *mp, const uint8_t *map)
{
  int i;

  for (i = 0; i < mp->n; i++)
    if (memcmp(mp->m[i], map, MSIZE) == 0)
      return (i + 1);
  return (0);
}

static void vec_push(struct vec *vp, uint32_t x);

/* A level read, on map <map> */
static void
levread(uint32_t level, const uint8_t *map)
{
  int i;

  vec_push(&ply_lev, level);
  for (i = 0; i < MSIZE; i++)
    vec_push(&ply_lev, map[i]);
}
static uint32_t ply_endtick, ply_endhash;

static void
vec_push(struct vec *vp, uint32_t x)
{
  uint32_t *nv;
  size_t ncap;

  if (vp->len == vp->cap) {
    ncap = vp->cap ? vp->cap * 2 : 256;
    nv = realloc(vp->v, ncap * sizeof(*nv));
    if (nv == NULL)
      abort();
    vp->v = nv;
    vp->cap = ncap;
  }
  vp->v[vp->len++] = x;
}

static bool
vec_empty(const struct vec *vp)
{

  return (vp->pos >= vp->len);
}

static bool
vec_pop(struct vec *vp, uint32_t *xp)
{

  if (vec_empty(vp))
    return (false);
  *xp = vp->v[vp->pos++];
  if (vec_empty(vp))
    vp->len = vp->pos = 0;
  return (true);
}

static void
vec_clear(struct vec *vp)
{

  vp->len = vp->pos = 0;
}

static bool
reading(void)
{

  return (edrf_playing || edrf_feeding);
}

/* No controls on any of the ticks, read or not */
static bool
idle(const struct vec *vp)
{
  size_t i;

  for (i = vp->pos; i < vp->len; i++)
    if ((vp->v[i] & ~EDRF_UNREAD) != 0)
      return (false);
  return (true);
}

static void
putruns(const struct vec *vp)
{
  size_t i, j;

  fputc(' ', recfp);
  for (i = 0; i < vp->len; i = j) {
    for (j = i + 1; j < vp->len && vp->v[j] == vp->v[i]; j++)
      continue;
    fputc('@' + (int)(vp->v[i] & ~EDRF_UNREAD), recfp);
    if (j - i > 1)
      fprintf(recfp, "%u", (unsigned)(j - i));
  }
}

/* The same controls for every slot (unread ones going with any), into vp */
static bool
allsame(struct vec *vp)
{
  uint32_t x, y;
  size_t i;
  int s;

  vec_clear(vp);
  for (s = 1; s < EDRF_SLOTS; s++)
    if (rec_in[s].len != rec_in[0].len)
      return (false);
  for (i = 0; i < rec_in[0].len; i++) {
    x = EDRF_UNREAD;
    for (s = 0; s < EDRF_SLOTS; s++) {
      y = rec_in[s].v[i];
      if (y == EDRF_UNREAD)
        continue;
      if (x != EDRF_UNREAD && x != y)
        return (false);
      x = y;
    }
    vec_push(vp, x == EDRF_UNREAD ? 0 : x);
  }
  return (true);
}

/* Write the controls of the chunk so far: the same for every slot, or of
   the slots that had some */
static void
flushin(void)
{
  static struct vec same;
  bool list[EDRF_SLOTS], samelen = true;
  int s;

  for (s = 1; s < EDRF_SLOTS; s++)
    if (rec_in[s].len != rec_in[0].len)
      samelen = false;
  if (recfp != NULL && rec_in[0].len != 0) {
    fputc('I', recfp);
    if (allsame(&same))
      putruns(&same);
    else {
      for (s = 0; s < EDRF_SLOTS; s++) {
        list[s] = !samelen || !idle(&rec_in[s]);
        if (list[s])
          fputc('0' + s, recfp);
      }
      for (s = 0; s < EDRF_SLOTS; s++)
        if (list[s])
          putruns(&rec_in[s]);
    }
    fputc('\n', recfp);
  }
  for (s = 0; s < EDRF_SLOTS; s++)
    vec_clear(&rec_in[s]);
}

/* Write a record of the game state, after the controls before it */
static void
putstate(char what, uint32_t hash)
{

  flushin();
  if (recfp == NULL)
    return;
  fprintf(recfp, "%c %08X\n", what, (unsigned)hash);
  fflush(recfp);
}

void
edrf_recreset(void)
{
  int i;

  for (i = 0; i < EDRF_SLOTS; i++) {
    vec_clear(&rec_in[i]);
    slotread[i] = false;
  }
  if (recfp != NULL) {
    fclose(recfp);
    recfp = NULL;
  }
  rec_endtick = rec_endhash = 0;
}

/* Stream the recording of the game to fp, which already has the header */
void
edrf_recopen(FILE *fp)
{

  recfp = fp;
  rec_maps.n = 0;
}

/* Read runs into vp, up to the end of the field; how many there were */
static bool
load_runs(const char **pp, struct vec *vp, uint32_t *lenp)
{
  const char *p = *pp;
  unsigned long n;
  char *ep;
  int bits;

  *lenp = 0;
  while (*p != '\0' && *p != ' ') {
    if (*p < '@' || *p > '_')
      return (false);
    bits = *p++ - '@';
    n = 1;
    if (*p >= '0' && *p <= '9') {
      n = strtoul(p, &ep, 10);
      p = ep;
    }
    *lenp += n;
    while (n-- > 0)
      vec_push(vp, (uint32_t)bits);
  }
  *pp = p;
  return (*lenp != 0);
}

/* An I record: the slots listed, then their runs, or the runs of all */
static bool
load_slots(const char *p)
{
  static struct vec all;
  bool list[EDRF_SLOTS] = { false };
  uint32_t len, len0 = 0;
  size_t i;
  int s, last = -1;

  for (; *p >= '0' && *p <= '9'; p++) {
    s = *p - '0';
    if (s >= EDRF_SLOTS || s <= last)
      return (false);
    list[s] = true;
    last = s;
  }
  if (last == -1) {
    vec_clear(&all);
    if (*p++ != ' ' || !load_runs(&p, &all, &len) || *p != '\0')
      return (false);
    for (s = 0; s < EDRF_SLOTS; s++)
      for (i = 0; i < all.len; i++)
        vec_push(&ply_in[s], all.v[i]);
    return (true);
  }
  for (s = 0; s < EDRF_SLOTS; s++) {
    if (!list[s])
      continue;
    if (*p++ != ' ' || !load_runs(&p, &ply_in[s], &len))
      return (false);
    if (len0 == 0)
      len0 = len;
  }
  if (*p != '\0')
    return (false);
  for (s = 0; s < EDRF_SLOTS; s++)
    if (!list[s])
      for (len = 0; len < len0; len++)
        vec_push(&ply_in[s], 0);
  return (true);
}

/* Read the next record of the recording played back, if there is any */
static bool
readrec(void)
{
  char line[512], c;
  unsigned a, b;
  size_t l;

  while (plyfp != NULL && !plyeof) {
    if (fgets(line, sizeof(line), plyfp) == NULL) {
      plyeof = true;
      break;
    }
    l = strlen(line);
    if (line[l - 1] != '\n') {
      /* Cut short by the program having been stopped while recording */
      plyeof = true;
      break;
    }
    while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = '\0';
    /* A row of a map being read, which an empty line is, all of its
       spaces gone */
    if (lev_rows >= 0) {
      const char *rp;

      if (l == 0)
        rp = "";
      else if (line[0] == ' ')
        rp = line + 1;
      else
        goto bad;
      if (strlen(rp) > MWIDTH)
        goto bad;
      for (a = 0; a < MWIDTH; a++)
        lev_new[lev_rows * MWIDTH + a] = a < strlen(rp) ? rp[a] : ' ';
      if (++lev_rows == MHEIGHT) {
        /* All of the map: the level can be played */
        maps_add(&ply_maps, lev_new);
        levread(lev_newno, lev_new);
        lev_rows = -1;
      }
      return (true);
    }
    if (l == 0)
      continue;
    switch (line[0]) {
      case 'I':
        if (!load_slots(line + 1))
          goto bad;
        return (true);
      case 'R':
        if (sscanf(line + 1, "%x", &a) != 1)
          goto bad;
        vec_push(&ply_rand, a);
        return (true);
      case 'C':
      case 'E':
        /* Just the hash, nothing after it */
        if (sscanf(line + 1, "%x %c", &a, &c) != 1)
          goto bad;
        vec_push(line[0] == 'C' ? &ply_ckpt : &ply_rend, a);
        return (true);
      case 'Z':
        if (sscanf(line + 1, "%u %x", &a, &b) != 2)
          goto bad;
        ply_endtick = a;
        ply_endhash = b;
        ply_hasend = true;
        return (true);
      case 'L':
        switch (sscanf(line + 1, "%u %u", &a, &b)) {
          case 2: /* On a map had before */
            if (b < 1 || (int)b > ply_maps.n)
              goto bad;
            levread(a, ply_maps.m[b - 1]);
            return (true);
          case 1: /* On the map that follows */
            lev_newno = a;
            lev_rows = 0;
            return (true);
        }
        goto bad;
    }
bad:
    fprintf(stderr, "eDRF: bad record \"%s\"\n", line);
    edrf_failed = true;
    plyeof = true;
  }
  return (false);
}

/* Read the recording on until vp has something, or there's no more */
static bool
fill(const struct vec *vp)
{

  while (vec_empty(vp))
    if (!readrec())
      return (false);
  return (true);
}

/* The next recorded controls of a slot, read on as needed */
static uint32_t
takein(int slot)
{
  uint32_t x;

  fill(&ply_in[slot]);
  if (!vec_pop(&ply_in[slot], &x))
    x = 0;
  return (x);
}

/*
 * Controls of input slot <slot> for this tick: recorded as they are, or
 * replaced with the recorded ones when playing an eDRF back. When playing
 * back a plain DRF they are not known yet: edrf_movement() fills them in
 * from the direction that DRF says the slot ended up moving in.
 */
uint8_t
edrf_input(int slot, uint8_t bits)
{
  uint32_t x;

  if (slot < 0 || slot >= EDRF_SLOTS)
    return (bits);
  slotread[slot] = true;
  if (edrf_playing)
    bits = (uint8_t)takein(slot);
  else if (edrf_feeding) {
    /* Both players' controls, as they came out of the network, have to be
       the recorded ones */
    x = takein(slot);
    if (x != (bits & 0x1f) && !edrf_failed) {
      fprintf(stderr, "eDRF: player %d controls %02X on tick %u, recorded "
        "%02X\n", slot + 1, (unsigned)(bits & 0x1f),
        (unsigned)dgstate.ticks + 1, (unsigned)x);
      edrf_failed = true;
    }
  }
  else if (playing)
    bits = 0;
  vec_push(&rec_in[slot], bits & 0x1f);
  return (bits);
}

void
edrf_movement(int slot, int16_t dir, bool fire)
{
  uint32_t bits = 0;

  if (!playing || edrf_playing || slot < 0 || slot >= EDRF_SLOTS ||
      vec_empty(&rec_in[slot]))
    return;
  switch (dir) {
    case DIR_UP:    bits = INPUT_CTRL_UP; break;
    case DIR_DOWN:  bits = INPUT_CTRL_DOWN; break;
    case DIR_LEFT:  bits = INPUT_CTRL_LEFT; break;
    case DIR_RIGHT: bits = INPUT_CTRL_RIGHT; break;
  }
  if (fire)
    bits |= INPUT_CTRL_FIRE;
  rec_in[slot].v[rec_in[slot].len - 1] = bits;
}

/* Nothing left to play back: the recording ends here */
bool
edrf_exhausted(void)
{
  int i;

  if (!reading())
    return (false);
  for (;;) {
    for (i = 0; i < EDRF_SLOTS; i++)
      if (!vec_empty(&ply_in[i]))
        return (false);
    if (!readrec())
      return (true);
  }
}

/*
 * A level starts: record its map, or (playing back) play it on the
 * recorded one, which goes to plan <plan>'s, where the game takes it from.
 */
void
edrf_level(int level, int plan)
{
  uint8_t map[MSIZE];
  uint32_t x = 0;
  int i, n;

  flushin();
  /* Played back: the level is on the recorded map (also once the game has
     gone astray, as getlevch() relies on it) */
  if (reading() && fill(&ply_lev)) {
    vec_pop(&ply_lev, &x);
    if (x != (uint32_t)level) {
      fprintf(stderr, "eDRF: level %d starts, recorded level %u\n", level,
        (unsigned)x);
      edrf_failed = true;
    }
    for (i = 0; i < MSIZE; i++) {
      vec_pop(&ply_lev, &x);
      dgstate.leveldat[plan - 1][i / MWIDTH][i % MWIDTH] = (int8_t)x;
    }
  }
  if (recfp == NULL)
    return;
  for (i = 0; i < MSIZE; i++)
    map[i] = mapch(getlevch(i % MWIDTH, i / MWIDTH, plan));
  if ((n = maps_find(&rec_maps, map)) != 0) {
    fprintf(recfp, "L %d %d\n", level, n);
    return;
  }
  maps_add(&rec_maps, map);
  fprintf(recfp, "L %d\n", level);
  for (i = 0; i < MHEIGHT; i++)
    fprintf(recfp, " %.*s\n", MWIDTH, (const char *)map + i * MWIDTH);
}

void
edrf_putrand(uint32_t randv)
{

  flushin();
  if (recfp != NULL)
    fprintf(recfp, "R %08X\n", (unsigned)randv);
}

uint32_t
edrf_getrand(void)
{
  uint32_t x;

  fill(&ply_rand);
  if (!vec_pop(&ply_rand, &x))
    x = 0;
  return (x);
}

/* Check the state against the recording's next checkpoint of a kind,
   which they come in the order of */
static void
verify(struct vec *vp, const char *what, uint32_t h)
{
  uint32_t hash;

  if (!reading() || edrf_failed || !fill(vp) || !vec_pop(vp, &hash))
    return;
  if (hash != h) {
    fprintf(stderr, "eDRF: the game diverges from the recording before "
      "tick %u (%s state %08X, recorded %08X)\n", (unsigned)dgstate.ticks,
      what, (unsigned)h, (unsigned)hash);
    edrf_failed = true;
  }
}

void
edrf_tick(void)
{
  uint32_t h;
  int s;

  /* A slot not read on this tick has no controls on it */
  for (s = 0; s < EDRF_SLOTS; s++) {
    if (!slotread[s]) {
      vec_push(&rec_in[s], EDRF_UNREAD);
      if (reading())
        (void)takein(s);
    }
    slotread[s] = false;
  }
  if (dgstate.ticks % EDRF_CHUNK == 0) {
    flushin();
    if (recfp != NULL)
      fflush(recfp);
  }
  if (dgstate.ticks % EDRF_CKPT_EVERY == 0) {
    h = game_state_hash();
    putstate('C', h);
    verify(&ply_ckpt, "tick", h);
  }
  /* The player takes over after this one (/T) */
  if (playtakeat != 0 && dgstate.ticks == playtakeat)
    playtakeover();
}

void
edrf_roundend(void)
{
  uint32_t h;

  /* The tick it ends on too, that it be the same */
  h = game_state_hash_tick(dgstate.ticks);
  putstate('E', h);
  verify(&ply_rend, "end of round", h);
}

void
edrf_gameend(void)
{

  rec_endtick = dgstate.ticks;
  rec_endhash = game_state_hash();
  flushin();
  if (recfp != NULL)
    fprintf(recfp, "Z %u %08X\n", (unsigned)rec_endtick,
      (unsigned)rec_endhash);
  if (recfp != NULL) {
    fclose(recfp);
    recfp = NULL;
  }
  if (!reading() || edrf_failed)
    return;
  /* Stopped (e.g. with F10, or by the NetSim peer gone) before the end:
     nothing to check it against */
  if (!edrf_exhausted()) {
    fprintf(stderr, "eDRF: %s stopped on tick %u, before the end of the "
      "recording\n", edrf_playing ? "playback" : "NetSim replay",
      (unsigned)rec_endtick);
    edrf_stopped = true;
    return;
  }
  if (!ply_hasend) {
    fprintf(stderr, "eDRF: the recording ends on tick %u, before its game "
      "does\n", (unsigned)rec_endtick);
    edrf_truncated = true;
    return;
  }
  if (ply_endtick != rec_endtick || ply_endhash != rec_endhash) {
    fprintf(stderr, "eDRF: the game ends differently from the recording "
      "(expected %u/%08X, got %u/%08X)\n", (unsigned)ply_endtick,
      (unsigned)ply_endhash, (unsigned)rec_endtick, (unsigned)rec_endhash);
    edrf_failed = true;
    return;
  }
  edrf_checked = true;
}

/* Play back the eDRF body that follows the header in fp, which is read as
   it goes, and closed when done */
void
edrf_playopen(FILE *fp)
{
  int i;

  for (i = 0; i < EDRF_SLOTS; i++)
    vec_clear(&ply_in[i]);
  vec_clear(&ply_rand);
  vec_clear(&ply_ckpt);
  vec_clear(&ply_rend);
  vec_clear(&ply_lev);
  lev_rows = -1;
  ply_maps.n = 0;
  /* The game's maps, which the playback's replace */
  memcpy(ply_savedmaps, dgstate.leveldat, sizeof(ply_savedmaps));
  if (plyfp != NULL)
    fclose(plyfp);
  plyfp = fp;
  plyeof = ply_hasend = false;
  edrf_failed = false;
  edrf_stopped = false;
  edrf_truncated = false;
  edrf_checked = false;
}

/* The player takes over: no more of the recording, nor checks against it */
void
edrf_takeover(void)
{

  edrf_playing = false;
}

void
edrf_stopplay(void)
{

  edrf_playing = false;
  if (plyfp != NULL) {
    fclose(plyfp);
    plyfp = NULL;
    memcpy(dgstate.leveldat, ply_savedmaps, sizeof(ply_savedmaps));
  }
}

/*
 * Replaying a two Digger recording over NetSim: the recording's header sets
 * the game up, then each peer sends its own player's recorded controls
 * (edrf_feedpeek()) instead of the keyboard's, and checks both players'
 * controls as received (edrf_input()) and the state checkpoints against the
 * recording.
 */
bool
edrf_netfeed_open(const char *name)
{
  char line[512];
  FILE *fp;

  fp = fopen(name, "r");
  if (fp == NULL)
    return (false);
#define GETLINE() (fgets(line, sizeof(line), fp) != NULL && \
  (line[strcspn(line, "\r\n")] = '\0', true))
  if (!GETLINE() || strcmp(line, EDRF_MAGIC) != 0)
    goto out;
  if (!GETLINE())
    goto out;
  kludge = atol(line + 7) <= 19981125l;
  /* Only two Digger games make sense over NetSim */
  if (!GETLINE() || strncmp(line, "M2", 2) != 0 ||
      (line[2] != '\0' && line[2] != 'I'))
    goto out;
  dgstate.diggers = 2;
  dgstate.nplayers = 1;
  dgstate.gauntlet = false;
  dgstate.startlev = line[2] == 'I' ? atoi(line + 3) : 1;
  if (!GETLINE())
    goto out;
  bonusscore = atoi(line);
#undef GETLINE
  edrf_playopen(fp);
  edrf_feeding = true;
  feedslot = -1;
  return (true);
out:
  fclose(fp);
  return (false);
}

/* The NetSim session decided which player this peer is */
void
edrf_feedslot(int slot)
{

  feedslot = slot;
}

/* This peer's next recorded controls, sent until the game reads them */
uint8_t
edrf_feedpeek(void)
{
  const struct vec *vp;

  if (feedslot < 0 || feedslot >= EDRF_SLOTS)
    return (0);
  vp = &ply_in[feedslot];
  return (fill(vp) ? (uint8_t)vp->v[vp->pos] : 0);
}
