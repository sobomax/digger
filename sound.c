/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <assert.h>
#include <stdlib.h>

#include "def.h"
#include "sound.h"
#include "device.h"
#include "hardware.h"
#include "main.h"
#include "digger.h"
#include "input.h"
#include "game.h"
#include "netsim.h"
#include "spinlock.h"

int16_t spkrmode=0,pulsewidth=1,volume=0;
_Atomic bool soundflag=true,musicflag=true;

static void s0setupsound(void);
static void s0killsound(void);

void (*setupsound)(void)=s0setupsound;
void (*killsound)(void)=s0killsound;
void (*soundoff)(void)=s0soundoff;
void (*setspkrt2)(void)=s0setspkrt2;
void (*timer0)(uint16_t t0v)=s0timer0;
void (*timer2)(uint16_t t2v, bool mod)=s0timer2;
void (*soundkillglob)(void)=s0soundkillglob;

#if defined _SDL_SOUND
#include "sdl_snd.h"
#include "sound_int.h"

#define SOUND_CMD_QUEUE_LEN 4096
#define SOUND_CMD_DRAIN_MAX 64
#define SOUND_ACK_QUEUE_LEN 64

struct sound_cmd_queue {
  struct spinlock *lock;
  unsigned int head;
  unsigned int tail;
  unsigned int len;
  struct sound_cmd items[SOUND_CMD_QUEUE_LEN];
};

struct sound_ack_queue {
  struct spinlock *lock;
  unsigned int head;
  unsigned int tail;
  unsigned int len;
  uint16_t next_id;
  uint16_t items[SOUND_ACK_QUEUE_LEN];
};

static struct sound_cmd_queue sound_cmdq = {0};
static struct sound_ack_queue sound_ackq = {0};

bool
sound_queue_init(void)
{
  struct spinlock *cmd_lock;
  struct spinlock *ack_lock;

  assert(sound_cmdq.lock == NULL);
  assert(sound_ackq.lock == NULL);
  cmd_lock = spinlock_ctor();
  if (cmd_lock == NULL)
    return (false);
  ack_lock = spinlock_ctor();
  if (ack_lock == NULL) {
    spinlock_dtor(cmd_lock);
    return (false);
  }
  sound_cmdq.lock = cmd_lock;
  sound_ackq.lock = ack_lock;
  return (true);
}

uint16_t
sound_ack_alloc(void)
{
  uint16_t ack_id;

  ack_id = ++sound_ackq.next_id;
  if (ack_id == 0)
    ack_id = ++sound_ackq.next_id;
  return (ack_id);
}

void
sound_ack_push(uint16_t ack_id)
{
  struct sound_ack_queue *qp;

  assert(ack_id != 0);
  qp = &sound_ackq;
  assert(qp->lock != NULL);
  spinlock_lock(qp->lock);
  assert(qp->len < SOUND_ACK_QUEUE_LEN);
  qp->items[qp->tail] = ack_id;
  qp->tail = (qp->tail + 1) % SOUND_ACK_QUEUE_LEN;
  qp->len++;
  spinlock_unlock(qp->lock);
}

bool
sound_ack_poll(uint16_t ack_id)
{
  struct sound_ack_queue *qp;
  bool found;

  assert(ack_id != 0);
  qp = &sound_ackq;
  assert(qp->lock != NULL);
  found = false;
  spinlock_lock(qp->lock);
  if (qp->len > 0) {
    assert(qp->items[qp->head] == ack_id);
    qp->head = (qp->head + 1) % SOUND_ACK_QUEUE_LEN;
    qp->len--;
    found = true;
  }
  spinlock_unlock(qp->lock);
  return (found);
}

bool
soundackready(uint16_t ack_id)
{

  if (ack_id == 0)
    return (true);
  return (sound_ack_poll(ack_id));
}

void
sound_queue_push_done(enum sound_cmd_type type, int argi, double argd,
  uint16_t done_ack_id)
{
  struct sound_cmd_queue *qp;

  qp = &sound_cmdq;
  assert(qp->lock != NULL);
  spinlock_lock(qp->lock);
  assert(qp->len < SOUND_CMD_QUEUE_LEN);
  qp->items[qp->tail].type = type;
  qp->items[qp->tail].argi = argi;
  qp->items[qp->tail].argd = argd;
  qp->items[qp->tail].done_ack_id = done_ack_id;
  qp->tail = (qp->tail + 1) % SOUND_CMD_QUEUE_LEN;
  qp->len++;
  spinlock_unlock(qp->lock);
  if (type == SOUND_CMD_WAKEUP)
    wakesounddevice();
}

static void
sound_queue_push(enum sound_cmd_type type, int argi, double argd)
{

  sound_queue_push_done(type, argi, argd, 0);
}

void
sound_queue_post(enum sound_cmd_type type, int argi, double argd)
{

  sound_queue_push(type, argi, argd);
}

void
sound_queue_drain(void (*apply)(const struct sound_cmd *cmdp))
{
  struct sound_cmd local[SOUND_CMD_DRAIN_MAX];
  struct sound_cmd_queue *qp;
  unsigned int n, i;

  qp = &sound_cmdq;
  assert(qp->lock != NULL);
  do {
    spinlock_lock(qp->lock);
    n = 0;
    while (qp->len > 0 && n < SOUND_CMD_DRAIN_MAX) {
      local[n++] = qp->items[qp->head];
      qp->head = (qp->head + 1) % SOUND_CMD_QUEUE_LEN;
      qp->len--;
    }
    spinlock_unlock(qp->lock);
    for (i = 0; i < n; i++)
      apply(&local[i]);
  } while (n == SOUND_CMD_DRAIN_MAX);
}

static void
soundwait(void)
{
  gethrt(false, 1);
}

static int16_t
music_request_to_tune(int16_t music)
{
  int local_player;

  switch (music) {
    case MUSIC_BONUS:
      return (0);
    case MUSIC_MAIN:
      local_player = dgstate.netsim ? netsim_local_player() : dgstate.curplayer;
      if (getlives(local_player) == 1)
        return (3);
      return (1);
    case MUSIC_DIRGE:
      return (2);
  }
  return (music);
}

void
soundstop(void)
{
  sound_queue_post(SOUND_CMD_STOP, 0, 0.0);
}

void
soundwakeup(void)
{
  sound_queue_post(SOUND_CMD_WAKEUP, 0, 0.0);
}

static void
soundlevdone_wait_finish(uint16_t done_ack_id)
{
  while (!escape) {
    if (!wave_device_available) {
      sound_queue_push_done(SOUND_CMD_LEVDONE_OFF, 0, 0.0, done_ack_id);
      break;
    }
    if (sound_ack_poll(done_ack_id))
      break;
    soundwait();
    checkkeyb();
  }
}

static void
soundlevdone_netsim(bool local_sound)
{
  bool local_freeze;
  bool sent_unfreeze;
  bool remote_freeze;
  uint16_t done_ack_id;

  if (local_sound) {
    done_ack_id = sound_ack_alloc();
    sound_queue_push_done(SOUND_CMD_LEVDONE_START, 0, 0.0, done_ack_id);
  } else {
    done_ack_id = 0;
  }
  local_freeze = local_sound;
  sent_unfreeze = !local_freeze;
  while (!escape) {
    if (local_freeze && sound_ack_poll(done_ack_id))
      local_freeze = false;
    if (!freezeframe(local_freeze, &remote_freeze)) {
      if (local_freeze)
        sound_queue_push_done(SOUND_CMD_LEVDONE_OFF, 0, 0.0, done_ack_id);
      return;
    }
    if (!local_freeze)
      sent_unfreeze = true;
    if (!local_freeze && !remote_freeze && sent_unfreeze)
      break;
  }
  if (local_freeze)
    sound_queue_push_done(SOUND_CMD_LEVDONE_OFF, 0, 0.0, done_ack_id);
}

void
soundlevdone(void)
{
  uint16_t done_ack_id;
  bool local_sound;

  soundstop();
  local_sound = sound_backend_local_sound_available();
  if (dgstate.netsim) {
    soundlevdone_netsim(local_sound);
    return;
  }
  if (!local_sound)
    return;
  done_ack_id = sound_ack_alloc();
  sound_queue_push_done(SOUND_CMD_LEVDONE_START, 0, 0.0, done_ack_id);
  soundlevdone_wait_finish(done_ack_id);
  sound_queue_push_done(SOUND_CMD_LEVDONE_OFF, 0, 0.0, done_ack_id);
}

void
soundfall(void)
{
  sound_queue_post(SOUND_CMD_FALL_ON, 0, 0.0);
}

void
soundfalloff(void)
{
  sound_queue_post(SOUND_CMD_FALL_OFF, 0, 0.0);
}

void
soundbreak(void)
{
  sound_queue_post(SOUND_CMD_BREAK, 0, 0.0);
}

void
soundwobble(void)
{
  sound_queue_post(SOUND_CMD_WOBBLE_ON, 0, 0.0);
}

void
soundwobbleoff(void)
{
  sound_queue_post(SOUND_CMD_WOBBLE_OFF, 0, 0.0);
}

void
soundfire(int n)
{
  sound_queue_post(SOUND_CMD_FIRE_ON, n, 0.0);
}

void
soundfireoff(int n)
{
  sound_queue_post(SOUND_CMD_FIRE_OFF, n, 0.0);
}

void
soundexplode(int n)
{
  sound_queue_post(SOUND_CMD_EXPLODE, n, 0.0);
}

void
soundbonus(void)
{
  sound_queue_post(SOUND_CMD_BONUS_ON, 0, 0.0);
}

void
soundbonusoff(void)
{
  sound_queue_post(SOUND_CMD_BONUS_OFF, 0, 0.0);
}

void
soundem(void)
{
  sound_queue_post(SOUND_CMD_EM, 0, 0.0);
}

void
soundemerald(int n)
{
  sound_queue_post(SOUND_CMD_EMERALD, n, 0.0);
}

void
soundgold(void)
{
  sound_queue_post(SOUND_CMD_GOLD, 0, 0.0);
}

void
soundeatm(void)
{
  sound_queue_post(SOUND_CMD_EATM, 0, 0.0);
}

void
soundddie(void)
{
  sound_queue_post(SOUND_CMD_DDIE, 0, 0.0);
}

void
sound1up(void)
{
  sound_queue_post(SOUND_CMD_1UP, 0, 0.0);
}

uint16_t
musicwithack(int16_t music_request, double dfac)
{
  uint16_t done_ack_id;
  int16_t tune;

  if (!sound_backend_local_sound_available()) {
    music(music_request, dfac);
    return (0);
  }
  tune = music_request_to_tune(music_request);
  done_ack_id = sound_ack_alloc();
  sound_queue_push_done(SOUND_CMD_MUSIC, tune, dfac, done_ack_id);
  return (done_ack_id);
}

void
music(int16_t music_request, double dfac)
{
  int16_t tune;

  tune = music_request_to_tune(music_request);
  sound_queue_push_done(SOUND_CMD_MUSIC, tune, dfac, 0);
}

void
musicoff(void)
{
  sound_queue_post(SOUND_CMD_MUSIC_OFF, 0, 0.0);
}

void
soundpause(void)
{
  sound_queue_post(SOUND_CMD_PAUSE_ON, 0, 0.0);
}

void
soundpauseoff(void)
{
  sound_queue_post(SOUND_CMD_PAUSE_OFF, 0, 0.0);
}

void
togglesound(void)
{
  sound_queue_post(SOUND_CMD_SOUND_TOGGLE, 0, 0.0);
}

void
togglemusic(void)
{
  sound_queue_post(SOUND_CMD_MUSIC_TOGGLE, 0, 0.0);
}

void
soundpreinit(void)
{

  if (!sound_queue_init())
    abort();
}
#else
void
soundpreinit(void)
{
}

void
soundstop(void)
{
}

uint16_t
musicwithack(int16_t tune, double dfac)
{
  (void)tune;
  (void)dfac;
  return (0);
}

void
music(int16_t tune, double dfac)
{
  (void)tune;
  (void)dfac;
}

void
musicoff(void)
{
}

void
soundlevdone(void)
{
}

void
sound1up(void)
{
}

void
soundwakeup(void)
{
}

void
soundpause(void)
{
}

void
soundpauseoff(void)
{
}

void
soundbonus(void)
{
}

void
soundbonusoff(void)
{
}

void
soundfire(int n)
{
  (void)n;
}

void
soundexplode(int n)
{
  (void)n;
}

void
soundfireoff(int n)
{
  (void)n;
}

void
soundem(void)
{
}

void
soundemerald(int n)
{
  (void)n;
}

void
soundeatm(void)
{
}

void
soundddie(void)
{
}

void
soundwobble(void)
{
}

void
soundwobbleoff(void)
{
}

void
soundfall(void)
{
}

void
soundfalloff(void)
{
}

void
soundbreak(void)
{
}

void
soundgold(void)
{
}

void
togglesound(void)
{
}

void
togglemusic(void)
{
}

bool
soundackready(uint16_t ack_id)
{
  (void)ack_id;
  return (true);
}
#endif

static void
s0killsound(void)
{
  setsoundt2();
  timer2(40, false);
}

static void
s0setupsound(void)
{
  inittimer();
}
