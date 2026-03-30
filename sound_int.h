/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#ifndef SOUND_INT_H
#define SOUND_INT_H

#include <stdbool.h>
#include <stdint.h>

enum sound_cmd_type {
  SOUND_CMD_STOP = 0,
  SOUND_CMD_WAKEUP,
  SOUND_CMD_LEVDONE_START,
  SOUND_CMD_LEVDONE_OFF,
  SOUND_CMD_FALL_ON,
  SOUND_CMD_FALL_OFF,
  SOUND_CMD_BREAK,
  SOUND_CMD_WOBBLE_ON,
  SOUND_CMD_WOBBLE_OFF,
  SOUND_CMD_FIRE_ON,
  SOUND_CMD_FIRE_OFF,
  SOUND_CMD_EXPLODE,
  SOUND_CMD_BONUS_ON,
  SOUND_CMD_BONUS_OFF,
  SOUND_CMD_EM,
  SOUND_CMD_EMERALD,
  SOUND_CMD_GOLD,
  SOUND_CMD_EATM,
  SOUND_CMD_DDIE,
  SOUND_CMD_1UP,
  SOUND_CMD_MUSIC,
  SOUND_CMD_MUSIC_OFF,
  SOUND_CMD_SOUND_TOGGLE,
  SOUND_CMD_MUSIC_TOGGLE,
  SOUND_CMD_PAUSE_ON,
  SOUND_CMD_PAUSE_OFF
};

struct sound_cmd {
  enum sound_cmd_type type;
  int argi;
  double argd;
  uint16_t done_ack_id;
};

bool sound_queue_init(void);
void sound_queue_post(enum sound_cmd_type type, int argi, double argd);
void sound_queue_push_done(enum sound_cmd_type type, int argi, double argd,
  uint16_t done_ack_id);
void sound_queue_drain(void (*apply)(const struct sound_cmd *cmdp));

uint16_t sound_ack_alloc(void);
void sound_ack_push(uint16_t ack_id);
bool sound_ack_poll(uint16_t ack_id);

void sound_backend_apply(const struct sound_cmd *cmdp);
bool sound_backend_local_sound_available(void);

#endif
