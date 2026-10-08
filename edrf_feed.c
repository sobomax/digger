/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "edrf_feed.h"

#ifdef DIGGER_INSTRUMENTATION
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "def.h"
#include "edrf.h"
#include "game.h"
#include "record.h"
#include "scores.h"

bool edrf_feeding=false;
static int feedslot=-1;

/*
 * Replaying a two Digger recording over NetSim: the recording's header sets
 * the game up, then each peer sends its own player's recorded controls
 * (edrf_feedpeek()) instead of the keyboard's, and checks both players'
 * controls as received (edrf_input()) and the state checkpoints against the
 * recording. Fails with errno set if it can't be read, 0 if it isn't one.
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
  errno = 0; /* Not one to replay, see main() */
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

  return (edrf_peekinput(feedslot));
}

/* Both players' controls, as received, must match the recording. */
void
edrf_feed_checkinput(int slot, uint8_t bits, uint32_t recorded)
{

  if (recorded != (bits & 0x1f) && !edrf_failed) {
    fprintf(stderr, "eDRF: player %d controls %02X on tick %u, recorded "
      "%02X\n", slot + 1, (unsigned)(bits & 0x1f),
      (unsigned)dgstate.ticks + 1, (unsigned)recorded);
    edrf_failed = true;
  }
}

/* A peer's recorded quit is left to that peer to initiate. */
bool
edrf_feed_quitting(int slot)
{

  return (!edrf_feeding || slot < 0 || slot == feedslot);
}
#endif
