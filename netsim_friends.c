/* Digger Reloaded
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "netsim_friends.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NETSIM_FRIENDS_MAX NETSIM_SIP_MAX_REGISTERED
#define NETSIM_FRIENDS_FILE_VERSION 2U

struct netsim_friend {
  char name_buf[NETSIM_SIP_USER_BUFSIZE];
  struct usipy_str name;
  unsigned int games_played;
  uint64_t last_play_ts;
};

struct netsim_friend_view {
  const struct netsim_friend *friendp;
};

static struct netsim_friend g_friends[NETSIM_FRIENDS_MAX];
static size_t g_friend_count = 0;
static size_t g_friend_selected = 0;
static int g_friends_loaded = 0;

static const char *
netsim_friends_path(char *pathbuf, size_t pathbuf_len)
{

#if defined FREEBSD && defined _VGL
  (void)pathbuf;
  (void)pathbuf_len;
  return ("/var/games/digger/digger.frd");
#elif defined UNIX && !defined _VGL && !defined(__EMSCRIPTEN__)
  const char *home;

  home = getenv("HOME");
  if (home == NULL || home[0] == '\0')
    return (NULL);
  snprintf(pathbuf, pathbuf_len, "%s/.digger.frd", home);
  return (pathbuf);
#else
  (void)pathbuf;
  (void)pathbuf_len;
  return ("DIGGER.FRD");
#endif
}

static bool
netsim_friend_names_equal(const struct usipy_str *name_ap,
  const struct usipy_str *name_bp)
{

  return (usipy_str_eq(name_ap, name_bp) != 0);
}

static int
netsim_friend_names_cmp(const struct usipy_str *name_ap,
  const struct usipy_str *name_bp)
{
  size_t cmplen;
  int rval;

  cmplen = name_ap->l < name_bp->l ? name_ap->l : name_bp->l;
  rval = memcmp(name_ap->s.ro, name_bp->s.ro, cmplen);
  if (rval != 0)
    return (rval);
  if (name_ap->l < name_bp->l)
    return (-1);
  if (name_ap->l > name_bp->l)
    return (1);
  return (0);
}

static void
netsim_friend_rebind(struct netsim_friend *friendp)
{

  friendp->name = friendp->name.l != 0 ? (struct usipy_str){
    .s.ro = friendp->name_buf,
    .l = friendp->name.l
  } : USIPY_STR_NULL;
}

static bool
netsim_friend_set_name(struct netsim_friend *friendp,
  const struct usipy_str *name)
{

  assert(name != NULL);
  if (name->l == 0 || name->l >= sizeof(friendp->name_buf))
    return (false);
  memcpy(friendp->name_buf, name->s.ro, name->l);
  friendp->name_buf[name->l] = '\0';
  friendp->name = (struct usipy_str){
    .s.ro = friendp->name_buf,
    .l = name->l
  };
  return (true);
}

static void
netsim_friend_copy(struct netsim_friend *dstp,
  const struct netsim_friend *srcp)
{

  memset(dstp, '\0', sizeof(*dstp));
  if (srcp->name.l != 0) {
    memcpy(dstp->name_buf, srcp->name.s.ro, srcp->name.l);
    dstp->name_buf[srcp->name.l] = '\0';
    dstp->name.l = srcp->name.l;
  }
  dstp->games_played = srcp->games_played;
  dstp->last_play_ts = srcp->last_play_ts;
  netsim_friend_rebind(dstp);
}

static int
netsim_friend_view_cmp(const void *ap, const void *bp)
{
  const struct netsim_friend_view *view_a;
  const struct netsim_friend_view *view_b;
  const struct netsim_friend *friend_a;
  const struct netsim_friend *friend_b;

  view_a = ap;
  view_b = bp;
  friend_a = view_a->friendp;
  friend_b = view_b->friendp;
  if (friend_a->last_play_ts < friend_b->last_play_ts)
    return (1);
  if (friend_a->last_play_ts > friend_b->last_play_ts)
    return (-1);
  return (netsim_friend_names_cmp(&friend_a->name, &friend_b->name));
}

static uint64_t
netsim_friends_now(void)
{

  return ((uint64_t)time(NULL));
}

static int
netsim_friend_find(const struct usipy_str *name)
{
  size_t i;

  for (i = 0; i < g_friend_count; i++) {
    if (netsim_friend_names_equal(&g_friends[i].name, name))
      return ((int)i);
  }
  return (-1);
}

static void
netsim_friend_remove(size_t index)
{
  size_t i;

  if (index >= g_friend_count)
    return;
  for (i = index + 1; i < g_friend_count; i++)
    netsim_friend_copy(&g_friends[i - 1], &g_friends[i]);
  memset(&g_friends[g_friend_count - 1], '\0', sizeof(g_friends[0]));
  g_friend_count--;
  if (g_friend_count == 0) {
    g_friend_selected = 0;
    return;
  }
  if (g_friend_selected >= g_friend_count)
    g_friend_selected = g_friend_count - 1;
}

static bool
netsim_friend_add(const struct usipy_str *name)
{

  if (g_friend_count >= NETSIM_FRIENDS_MAX)
    return (false);
  if (!netsim_friend_set_name(&g_friends[g_friend_count], name))
    return (false);
  g_friends[g_friend_count].games_played = 0;
  g_friends[g_friend_count].last_play_ts = 0;
  g_friend_count++;
  return (true);
}

static void
netsim_friends_sort(void)
{
  struct netsim_friend_view view[NETSIM_FRIENDS_MAX];
  struct netsim_friend sorted[NETSIM_FRIENDS_MAX];
  struct usipy_str selected_name = USIPY_STR_NULL;
  char selected_name_buf[NETSIM_SIP_USER_BUFSIZE];
  size_t i;
  int selected_index;

  if (g_friend_count <= 1)
    return;
  selected_name_buf[0] = '\0';
  if (g_friend_count != 0) {
    memcpy(selected_name_buf, g_friends[g_friend_selected].name.s.ro,
      g_friends[g_friend_selected].name.l);
    selected_name_buf[g_friends[g_friend_selected].name.l] = '\0';
    selected_name = (struct usipy_str){
      .s.ro = selected_name_buf,
      .l = g_friends[g_friend_selected].name.l
    };
  }
  for (i = 0; i < g_friend_count; i++)
    view[i].friendp = &g_friends[i];
  qsort(view, g_friend_count, sizeof(view[0]), netsim_friend_view_cmp);
  for (i = 0; i < g_friend_count; i++)
    netsim_friend_copy(&sorted[i], view[i].friendp);
  memcpy(g_friends, sorted, sizeof(sorted[0]) * g_friend_count);
  for (i = 0; i < g_friend_count; i++)
    netsim_friend_rebind(&g_friends[i]);
  if (selected_name.l == 0)
    return;
  selected_index = netsim_friend_find(&selected_name);
  if (selected_index >= 0)
    g_friend_selected = (size_t)selected_index;
}

static bool
netsim_friend_ensure_present(const struct usipy_str *name, int *indexp)
{
  int index;

  index = netsim_friend_find(name);
  if (index < 0) {
    if (g_friend_count >= NETSIM_FRIENDS_MAX)
      netsim_friend_remove(g_friend_count - 1);
    if (!netsim_friend_add(name))
      return (false);
    index = (int)(g_friend_count - 1);
  }
  if (indexp != NULL)
    *indexp = index;
  return (true);
}

static void
netsim_friends_ensure_loaded(void)
{

  if (g_friends_loaded)
    return;
  netsim_friends_load();
}

void
netsim_friends_reset(void)
{

  memset(g_friends, '\0', sizeof(g_friends));
  g_friend_count = 0;
  g_friend_selected = 0;
}

void
netsim_friends_load(void)
{
  FILE *inf;
  char linebuf[256];
  char pathbuf[256];
  const char *path;
  unsigned long version;

  path = netsim_friends_path(pathbuf, sizeof(pathbuf));
  netsim_friends_reset();
  g_friends_loaded = 1;
  if (path == NULL)
    return;
  inf = fopen(path, "r");
  if (inf == NULL)
    return;
  if (fgets(linebuf, sizeof(linebuf), inf) == NULL)
    goto bad_file;
  if (sscanf(linebuf, "DIGGER_FRIENDS %lu", &version) != 1 ||
      (version != 1U && version != NETSIM_FRIENDS_FILE_VERSION))
    goto bad_file;
  while (g_friend_count < NETSIM_FRIENDS_MAX &&
      fgets(linebuf, sizeof(linebuf), inf) != NULL) {
    char *tsp;
    char *namep;
    char *endp;
    unsigned long games_played;
    unsigned long long last_play_ts;

    namep = strchr(linebuf, '\t');
    if (namep == NULL)
      continue;
    *namep++ = '\0';
    tsp = strchr(namep, '\t');
    if (tsp != NULL) {
      *tsp++ = '\0';
      last_play_ts = strtoull(namep, &endp, 10);
      if (namep[0] == '\0' || *endp != '\0')
        continue;
      namep = tsp;
    } else {
      last_play_ts = 0;
    }
    endp = strpbrk(namep, "\r\n");
    if (endp != NULL)
      *endp = '\0';
    if (namep[0] == '\0')
      continue;
    games_played = strtoul(linebuf, &endp, 10);
    if (linebuf[0] == '\0' || *endp != '\0')
      continue;
    if (!netsim_friend_add(&(const struct usipy_str){
          .s.ro = namep, .l = strlen(namep)}))
      continue;
    g_friends[g_friend_count - 1].games_played = (unsigned int)games_played;
    g_friends[g_friend_count - 1].last_play_ts = (uint64_t)last_play_ts;
  }
  netsim_friends_sort();
  fclose(inf);
  return;

bad_file:
  fclose(inf);
  netsim_friends_reset();
}

void
netsim_friends_save(void)
{
  FILE *outf;
  char pathbuf[256];
  const char *path;
  size_t i;

  netsim_friends_ensure_loaded();
  path = netsim_friends_path(pathbuf, sizeof(pathbuf));
  if (path == NULL)
    return;
  outf = fopen(path, "w");
  if (outf == NULL)
    return;
  fprintf(outf, "DIGGER_FRIENDS %u\n", NETSIM_FRIENDS_FILE_VERSION);
  for (i = 0; i < g_friend_count; i++) {
    fprintf(outf, "%u\t%llu\t%.*s\n", g_friends[i].games_played,
      (unsigned long long)g_friends[i].last_play_ts,
      USIPY_SFMT(&g_friends[i].name));
  }
  fclose(outf);
}

void
netsim_friends_configure(const struct usipy_str *primary_friend)
{
  int selected_index;

  netsim_friends_ensure_loaded();
  if (primary_friend == NULL || primary_friend->l == 0) {
    g_friend_selected = 0;
    return;
  }
  if (primary_friend->l >= NETSIM_SIP_USER_BUFSIZE)
    return;
  if (!netsim_friend_ensure_present(primary_friend, NULL))
    return;
  netsim_friends_sort();
  selected_index = netsim_friend_find(primary_friend);
  g_friend_selected = selected_index >= 0 ? (size_t)selected_index : 0;
}

void
netsim_friend_registered(const struct usipy_str *name)
{
  assert(name != NULL);
  if (name->l == 0)
    return;
  if (name->l >= NETSIM_SIP_USER_BUFSIZE)
    return;
  netsim_friends_ensure_loaded();
  if (!netsim_friend_ensure_present(name, NULL))
    return;
  netsim_friends_sort();
}

void
netsim_friend_touch(const struct usipy_str *name)
{
  int index;

  assert(name != NULL);
  if (name->l == 0)
    return;
  if (name->l >= NETSIM_SIP_USER_BUFSIZE)
    return;
  netsim_friends_ensure_loaded();
  if (!netsim_friend_ensure_present(name, &index))
    return;
  g_friends[index].games_played++;
  g_friends[index].last_play_ts = netsim_friends_now();
  netsim_friends_sort();
  index = netsim_friend_find(name);
  if (index >= 0)
    g_friend_selected = (size_t)index;
}

const struct usipy_str *
netsim_friend_selected_name(void)
{
  static const struct usipy_str empty_name = USIPY_STR_NULL;

  if (g_friend_count == 0)
    return (&empty_name);
  return (&g_friends[g_friend_selected].name);
}

size_t
netsim_friend_count(void)
{

  return (g_friend_count);
}

size_t
netsim_friend_selected(void)
{

  return (g_friend_selected);
}

bool
netsim_friend_get(size_t index, char *namebuf, size_t namebuf_len,
  unsigned int *games_playedp)
{

  if (index >= g_friend_count || namebuf == NULL || namebuf_len == 0)
    return (false);
  snprintf(namebuf, namebuf_len, "%s", g_friends[index].name_buf);
  if (games_playedp != NULL)
    *games_playedp = g_friends[index].games_played;
  return (true);
}

void
netsim_friend_move(int delta)
{
  int next;

  if (g_friend_count == 0 || delta == 0)
    return;
  next = (int)g_friend_selected + delta;
  if (next < 0)
    next = 0;
  if (next >= (int)g_friend_count)
    next = (int)g_friend_count - 1;
  g_friend_selected = (size_t)next;
}
