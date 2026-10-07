/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef __EDRF_H
#define __EDRF_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * eDRF, the extended Digger Recording Format: instead of the directions
 * Digger ended up moving in, it records the controls every input slot read
 * on every game tick, and checkpoints of the whole game state, so that a
 * recording can be played back (locally or over NetSim) and checked for
 * being reproduced exactly.
 */

#define EDRF_MAGIC "eDRF 2"

extern bool edrf_playing;	/* Playing back an eDRF (playing is set too) */
extern bool edrf_failed;	/* The playback diverged from the recording */
extern bool edrf_stopped;	/* The playback was stopped before its end */
extern bool edrf_truncated;	/* The recording ends before its game does */
extern bool edrf_checked;	/* The playback got to the end, as recorded */
extern bool edrf_feeding;	/* Feeding a recording to a NetSim game */

void edrf_recreset(void);
void edrf_recopen(FILE *fp);
uint8_t edrf_input(int slot, uint8_t bits);
void edrf_movement(int slot, int16_t dir, bool fire);
bool edrf_exhausted(void);
bool edrf_quitting(void);
void edrf_quit(void);
void edrf_level(int level, int plan);
void edrf_putrand(uint32_t randv);
uint32_t edrf_getrand(void);
void edrf_tick(void);
void edrf_roundend(void);
void edrf_gameend(void);
void edrf_playopen(FILE *fp);
void edrf_stopplay(void);
void edrf_takeover(void);
bool edrf_netfeed_open(const char *name);
void edrf_feedslot(int slot);
uint8_t edrf_feedpeek(void);

#endif
