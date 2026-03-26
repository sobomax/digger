/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <assert.h>

#include "def.h"
#include "sound.h"
#include "device.h"
#include "hardware.h"
#include "main.h"
#include "digger.h"
#include "input.h"
#include "game.h"
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
#include <SDL.h>
#include "sdl_snd.h"

static int16_t wavetype=0,musvol=0;
static uint16_t t2val=0,t0val=0;
static bool restore_musicflag_after_death=false;

static void soundlevdoneoff(void);
static void soundlevdoneupdate(void);
static void soundfallupdate(void);
static void soundbreakoff(void);
static void soundbreakupdate(void);
static void soundwobbleupdate(void);
static void soundfireupdate(void);
static void soundexplodeoff(int n);
static void soundexplodeupdate(void);
static void soundbonusupdate(void);
static void soundemoff(void);
static void soundemupdate(void);
static void soundemeraldoff(void);
static void soundemeraldupdate(void);
static void soundgoldoff(void);
static void soundgoldupdate(void);
static void soundeatmoff(void);
static void soundeatmupdate(void);
static void soundddieoff(void);
static void soundddieupdate(void);
static void sound1upoff(void);
static void sound1upupdate(void);
static void musicupdate(void);
static void soundwait(void);
static void soundstop_apply(void);
static void soundlevdone_start_apply(uint16_t done_ack_id);
static void soundfall_apply(void);
static void soundfalloff_apply(void);
static void soundbreak_apply(void);
static void soundwobble_apply(void);
static void soundwobbleoff_apply(void);
static void soundfire_apply(int n);
static void soundfireoff_apply(int n);
static void soundexplode_apply(int n);
static void soundbonus_apply(void);
static void soundbonusoff_apply(void);
static void soundem_apply(void);
static void soundemerald_apply(int n);
static void soundgold_apply(void);
static void soundeatm_apply(void);
static void soundddie_apply(void);
static void sound1up_apply(void);
static void music_apply(int16_t tune, double dfac, uint16_t done_ack_id);
static void musicoff_apply(void);
static void togglesound_apply(void);
static void togglemusic_apply(void);
static void soundpause_apply(bool paused);
static void sett0(bool);
static void setsoundmode(void);
static void sett2val(int16_t t2v, bool mode);

static _Atomic bool sndflag=false;
static bool soundpausedflag=false;

#define SOUND_CMD_QUEUE_LEN 4096
#define SOUND_CMD_DRAIN_MAX 64
#define SOUND_ACK_QUEUE_LEN 64

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

static bool
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

static uint16_t
sound_ack_alloc(void)
{
  uint16_t ack_id;

  ack_id = ++sound_ackq.next_id;
  if (ack_id == 0)
    ack_id = ++sound_ackq.next_id;
  return (ack_id);
}

static void
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

static bool
sound_ack_poll(uint16_t ack_id)
{
  struct sound_ack_queue *qp;
  uint16_t pending_ack;
  bool found;

  assert(ack_id != 0);
  qp = &sound_ackq;
  assert(qp->lock != NULL);
  found = false;
  spinlock_lock(qp->lock);
  if (qp->len > 0) {
    pending_ack = qp->items[qp->head];
    qp->head = (qp->head + 1) % SOUND_ACK_QUEUE_LEN;
    qp->len--;
    assert(pending_ack == ack_id);
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

static void
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

static void
sound_queue_post(enum sound_cmd_type type, int argi, double argd)
{

  sound_queue_push(type, argi, argd);
}

static void
sound_queue_apply(const struct sound_cmd *cmdp)
{

  switch (cmdp->type) {
    case SOUND_CMD_STOP:
      soundstop_apply();
      break;
    case SOUND_CMD_WAKEUP:
      break;
    case SOUND_CMD_LEVDONE_START:
      soundlevdone_start_apply(cmdp->done_ack_id);
      break;
    case SOUND_CMD_LEVDONE_OFF:
      soundlevdoneoff();
      break;
    case SOUND_CMD_FALL_ON:
      soundfall_apply();
      break;
    case SOUND_CMD_FALL_OFF:
      soundfalloff_apply();
      break;
    case SOUND_CMD_BREAK:
      soundbreak_apply();
      break;
    case SOUND_CMD_WOBBLE_ON:
      soundwobble_apply();
      break;
    case SOUND_CMD_WOBBLE_OFF:
      soundwobbleoff_apply();
      break;
    case SOUND_CMD_FIRE_ON:
      soundfire_apply(cmdp->argi);
      break;
    case SOUND_CMD_FIRE_OFF:
      soundfireoff_apply(cmdp->argi);
      break;
    case SOUND_CMD_EXPLODE:
      soundexplode_apply(cmdp->argi);
      break;
    case SOUND_CMD_BONUS_ON:
      soundbonus_apply();
      break;
    case SOUND_CMD_BONUS_OFF:
      soundbonusoff_apply();
      break;
    case SOUND_CMD_EM:
      soundem_apply();
      break;
    case SOUND_CMD_EMERALD:
      soundemerald_apply(cmdp->argi);
      break;
    case SOUND_CMD_GOLD:
      soundgold_apply();
      break;
    case SOUND_CMD_EATM:
      soundeatm_apply();
      break;
    case SOUND_CMD_DDIE:
      soundddie_apply();
      break;
    case SOUND_CMD_1UP:
      sound1up_apply();
      break;
    case SOUND_CMD_MUSIC:
      music_apply(cmdp->argi, cmdp->argd, cmdp->done_ack_id);
      break;
    case SOUND_CMD_MUSIC_OFF:
      musicoff_apply();
      break;
    case SOUND_CMD_SOUND_TOGGLE:
      togglesound_apply();
      break;
    case SOUND_CMD_MUSIC_TOGGLE:
      togglemusic_apply();
      break;
    case SOUND_CMD_PAUSE_ON:
      soundpause_apply(true);
      break;
    case SOUND_CMD_PAUSE_OFF:
      soundpause_apply(false);
      break;
  }
}

static void
sound_queue_drain(void)
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
      sound_queue_apply(&local[i]);
  } while (n == SOUND_CMD_DRAIN_MAX);
}

static int32_t randvs;

static int16_t randnos(int16_t n)
{
  randvs=randvs*0x15a4e35l+1;
  return (int16_t)((randvs&0x7fffffffl)%n);
}

static void sett2val(int16_t t2v, bool mode)
{
  if (sndflag)
    timer2(t2v, mode);
}

static bool soundlevdoneactive=false;
static uint16_t soundlevdone_done_ack_id=0;
static uint16_t soundmusic_done_ack_id=0;

static bool
sound_local_sound_available(void)
{

  return (sndflag && wave_device_available);
}

static void
soundwait(void)
{
  gethrt(false, 1);
}

static bool
soundlevdone_device_unavailable(void)
{

  return (!wave_device_available);
}

static void
soundlevdone_wait_finish(uint16_t done_ack_id)
{
  while (!escape) {
    if (soundlevdone_device_unavailable()) {
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

void soundint(void)
{
  sound_queue_drain();
  if (soundflag && !sndflag)
    sndflag=musicflag=true;
  if (!soundflag && sndflag) {
    sndflag=false;
    timer2(40, false);
    setsoundt2();
    soundoff();
  }
  if (sndflag && !soundpausedflag) {
    t0val=0x7d00;
    t2val=40;
    if (musicflag)
      musicupdate();
#if !defined(NO_SND_EFFECTS)
    soundemeraldupdate();
    soundwobbleupdate();
    soundddieupdate();
    soundbreakupdate();
    soundgoldupdate();
    soundemupdate();
    soundexplodeupdate();
    soundfireupdate();
    soundeatmupdate();
    soundfallupdate();
    sound1upupdate();
    soundbonusupdate();
#endif
    if (t0val==0x7d00 || t2val!=40)
      setsoundt2();
    else {
      setsoundmode();
      sett0(false);
    }
    sett2val(t2val, false);
  }
  if (soundlevdoneactive)
    soundlevdoneupdate();
}

void soundstop(void)
{
  sound_queue_post(SOUND_CMD_STOP, 0, 0.0);
}

void soundwakeup(void)
{
  sound_queue_post(SOUND_CMD_WAKEUP, 0, 0.0);
}

static int16_t nljpointer=0,nljnoteduration=0;

void soundlevdone(void)
{
  uint16_t done_ack_id;
  bool local_sound;

  soundstop();
  local_sound = sound_local_sound_available();
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

static void
soundlevdoneoff(void)
{

  soundlevdoneactive = false;
  soundpausedflag = false;
  if (soundlevdone_done_ack_id != 0) {
    sound_ack_push(soundlevdone_done_ack_id);
    soundlevdone_done_ack_id = 0;
  }
}

static void
soundstop_apply(void)
{
  int i;

  soundlevdoneoff();
  if (soundmusic_done_ack_id != 0) {
    sound_ack_push(soundmusic_done_ack_id);
    soundmusic_done_ack_id = 0;
  }
  soundfalloff_apply();
  soundwobbleoff_apply();
  for (i=0;i<FIREBALLS;i++)
    soundfireoff_apply(i);
  musicoff_apply();
  soundbonusoff_apply();
  for (i=0;i<FIREBALLS;i++)
    soundexplodeoff(i);
  soundbreakoff();
  soundemoff();
  soundemeraldoff();
  soundgoldoff();
  soundeatmoff();
  soundddieoff();
  sound1upoff();
}

static void
soundlevdone_start_apply(uint16_t done_ack_id)
{

  if (sndflag) {
    nljpointer=0;
    nljnoteduration=20;
    soundlevdoneactive = true;
    soundlevdone_done_ack_id = done_ack_id;
    soundpausedflag = true;
  } else {
    soundlevdoneactive = false;
    soundlevdone_done_ack_id = 0;
    sound_ack_push(done_ack_id);
  }
}

static const int16_t newlevjingle[11]={0x8e8,0x712,0x5f2,0x7f0,0x6ac,0x54c,
                        0x712,0x5f2,0x4b8,0x474,0x474};

static void soundlevdoneupdate(void)
{

  if (sndflag) {
    if (nljpointer<11)
      t2val=newlevjingle[nljpointer];
    t0val=t2val+35;
    musvol=50;
    setsoundmode();
    sett0(true);
    sett2val(t2val, true);
    if (nljnoteduration>0)
      nljnoteduration--;
    else {
      nljnoteduration=20;
      nljpointer++;
      if (nljpointer>10)
        soundlevdoneoff();
    }
  }
  else
    soundlevdoneoff();
}


static bool soundfallflag=false,soundfallf=false;
static int16_t soundfallvalue,soundfalln=0;

static void
soundfall_apply(void)
{
  soundfallvalue=1000;
  soundfallflag=true;
}

void soundfall(void)
{
  sound_queue_post(SOUND_CMD_FALL_ON, 0, 0.0);
}

static void
soundfalloff_apply(void)
{
  soundfallflag=false;
  soundfalln=0;
}

void soundfalloff(void)
{
  sound_queue_post(SOUND_CMD_FALL_OFF, 0, 0.0);
}

static void soundfallupdate(void)
{
  if (soundfallflag) {
    if (soundfalln<1) {
      soundfalln++;
      if (soundfallf)
        t2val=soundfallvalue;
    }
    else {
      soundfalln=0;
      if (soundfallf) {
        soundfallvalue+=50;
        soundfallf=false;
      }
      else
        soundfallf=true;
    }
  }
}


static bool soundbreakflag=false;
static int16_t soundbreakduration=0,soundbreakvalue=0;

static void
soundbreak_apply(void)
{
  soundbreakduration=3;
  if (soundbreakvalue<15000)
    soundbreakvalue=15000;
  soundbreakflag=true;
}

void soundbreak(void)
{
  sound_queue_post(SOUND_CMD_BREAK, 0, 0.0);
}

static void soundbreakoff(void)
{
  soundbreakflag=false;
}

static void soundbreakupdate(void)
{
  if (soundbreakflag) {
    if (soundbreakduration!=0) {
      soundbreakduration--;
      t2val=soundbreakvalue;
    }
    else
      soundbreakflag=false;
  }
}


static bool soundwobbleflag=false;
static int16_t soundwobblen=0;

static void
soundwobble_apply(void)
{
  soundwobbleflag=true;
}

void soundwobble(void)
{
  sound_queue_post(SOUND_CMD_WOBBLE_ON, 0, 0.0);
}

static void
soundwobbleoff_apply(void)
{
  soundwobbleflag=false;
  soundwobblen=0;
}

void soundwobbleoff(void)
{
  sound_queue_post(SOUND_CMD_WOBBLE_OFF, 0, 0.0);
}

static void soundwobbleupdate(void)
{
  if (soundwobbleflag) {
    soundwobblen++;
    if (soundwobblen>63)
      soundwobblen=0;
    switch (soundwobblen) {
      case 0:
        t2val=0x7d0;
        break;
      case 16:
      case 48:
        t2val=0x9c4;
        break;
      case 32:
        t2val=0xbb8;
        break;
    }
  }
}


static bool soundfireflag[FIREBALLS]={false,false},sff[FIREBALLS];
static int16_t soundfirevalue[FIREBALLS],soundfiren[FIREBALLS]={0,0};
static int soundfirew=0;

static void
soundfire_apply(int n)
{
  soundfirevalue[n]=500;
  soundfireflag[n]=true;
}

void soundfire(int n)
{
  sound_queue_post(SOUND_CMD_FIRE_ON, n, 0.0);
}

static void
soundfireoff_apply(int n)
{
  soundfireflag[n]=false;
  soundfiren[n]=0;
}

void soundfireoff(int n)
{
  sound_queue_post(SOUND_CMD_FIRE_OFF, n, 0.0);
}

static void soundfireupdate(void)
{
  int n;
  bool f=false;
  for (n=0;n<FIREBALLS;n++) {
    sff[n]=false;
    if (soundfireflag[n]) {
      if (soundfiren[n]==1) {
        soundfiren[n]=0;
        soundfirevalue[n]+=soundfirevalue[n]/55;
        sff[n]=true;
        f=true;
        if (soundfirevalue[n]>30000)
          soundfireoff_apply(n);
      }
      else
        soundfiren[n]++;
    }
  }
  if (f) {
    do {
      n=soundfirew++;
      if (soundfirew==FIREBALLS)
        soundfirew=0;
    } while (!sff[n]);
    t2val=soundfirevalue[n]+randnos(soundfirevalue[n]>>3);
  }
}


static bool soundexplodeflag[FIREBALLS]={false,false},sef[FIREBALLS];
static int16_t soundexplodevalue[FIREBALLS],soundexplodeduration[FIREBALLS];
static int soundexplodew=0;

static void
soundexplode_apply(int n)
{
  soundexplodevalue[n]=1500;
  soundexplodeduration[n]=10;
  soundexplodeflag[n]=true;
  soundfireoff_apply(n);
}

void soundexplode(int n)
{
  sound_queue_post(SOUND_CMD_EXPLODE, n, 0.0);
}

static void soundexplodeoff(int n)
{
  soundexplodeflag[n]=false;
}

static void soundexplodeupdate(void)
{
  int n;
  bool f=false;
  for (n=0;n<FIREBALLS;n++) {
    sef[n]=false;
    if (soundexplodeflag[n]) {
      if (soundexplodeduration[n]!=0) {
        soundexplodevalue[n]=soundexplodevalue[n]-(soundexplodevalue[n]>>3);
        soundexplodeduration[n]--;
        sef[n]=true;
        f=true;
      }
      else
        soundexplodeflag[n]=false;
    }
  }
  if (f) {
    do {
      n=soundexplodew++;
      if (soundexplodew==FIREBALLS)
        soundexplodew=0;
    } while (!sef[n]);
    t2val=soundexplodevalue[n];
  }
}


static bool soundbonusflag=false;
static int16_t soundbonusn=0;

static void
soundbonus_apply(void)
{
  soundbonusflag=true;
}

void soundbonus(void)
{
  sound_queue_post(SOUND_CMD_BONUS_ON, 0, 0.0);
}

static void
soundbonusoff_apply(void)
{
  soundbonusflag=false;
  soundbonusn=0;
}

void soundbonusoff(void)
{
  sound_queue_post(SOUND_CMD_BONUS_OFF, 0, 0.0);
}

static void soundbonusupdate(void)
{
  if (soundbonusflag) {
    soundbonusn++;
    if (soundbonusn>15)
      soundbonusn=0;
    if (soundbonusn>=0 && soundbonusn<6)
      t2val=0x4ce;
    if (soundbonusn>=8 && soundbonusn<14)
      t2val=0x5e9;
  }
}


static bool soundemflag=false;

static void
soundem_apply(void)
{
  soundemflag=true;
}

void soundem(void)
{
  sound_queue_post(SOUND_CMD_EM, 0, 0.0);
}

static void soundemoff(void)
{
  soundemflag=false;
}

static void soundemupdate(void)
{
  if (soundemflag) {
    t2val=1000;
    soundemoff();
  }
}


static bool soundemeraldflag=false;
static int16_t soundemeraldduration,emerfreq,soundemeraldn;

static const int16_t emfreqs[8]={0x8e8,0x7f0,0x712,0x6ac,0x5f2,0x54c,0x4b8,0x474};

static void
soundemerald_apply(int n)
{
  emerfreq=emfreqs[n];
  soundemeraldduration=7;
  soundemeraldn=0;
  soundemeraldflag=true;
}

void soundemerald(int n)
{
  sound_queue_post(SOUND_CMD_EMERALD, n, 0.0);
}

static void soundemeraldoff(void)
{
  soundemeraldflag=false;
}

static void soundemeraldupdate(void)
{
  if (soundemeraldflag) {
    if (soundemeraldduration!=0) {
      if (soundemeraldn==0 || soundemeraldn==1)
        t2val=emerfreq;
      soundemeraldn++;
      if (soundemeraldn>7) {
        soundemeraldn=0;
        soundemeraldduration--;
      }
    }
    else
      soundemeraldoff();
  }
}


static bool soundgoldflag=false,soundgoldf=false;
static int16_t soundgoldvalue1,soundgoldvalue2,soundgoldduration;

static void
soundgold_apply(void)
{
  soundgoldvalue1=500;
  soundgoldvalue2=4000;
  soundgoldduration=30;
  soundgoldf=false;
  soundgoldflag=true;
}

void soundgold(void)
{
  sound_queue_post(SOUND_CMD_GOLD, 0, 0.0);
}

static void soundgoldoff(void)
{
  soundgoldflag=false;
}

static void soundgoldupdate(void)
{
  if (soundgoldflag) {
    if (soundgoldduration!=0)
      soundgoldduration--;
    else
      soundgoldflag=false;
    if (soundgoldf) {
      soundgoldf=false;
      t2val=soundgoldvalue1;
    }
    else {
      soundgoldf=true;
      t2val=soundgoldvalue2;
    }
    soundgoldvalue1+=(soundgoldvalue1>>4);
    soundgoldvalue2-=(soundgoldvalue2>>4);
  }
}



static bool soundeatmflag=false;
static int16_t soundeatmvalue,soundeatmduration,soundeatmn;

static void
soundeatm_apply(void)
{
  soundeatmduration=20;
  soundeatmn=3;
  soundeatmvalue=2000;
  soundeatmflag=true;
}

void soundeatm(void)
{
  sound_queue_post(SOUND_CMD_EATM, 0, 0.0);
}

static void soundeatmoff(void)
{
  soundeatmflag=false;
}

static void soundeatmupdate(void)
{
  if (soundeatmflag) {
    if (soundeatmn!=0) {
      if (soundeatmduration!=0) {
        if ((soundeatmduration%4)==1)
          t2val=soundeatmvalue;
        if ((soundeatmduration%4)==3)
          t2val=soundeatmvalue-(soundeatmvalue>>4);
        soundeatmduration--;
        soundeatmvalue-=(soundeatmvalue>>4);
      }
      else {
        soundeatmduration=20;
        soundeatmn--;
        soundeatmvalue=2000;
      }
    }
    else
      soundeatmflag=false;
  }
}


static bool soundddieflag=false;
static int16_t soundddien,soundddievalue;

static void
soundddie_apply(void)
{
  soundddien=0;
  soundddievalue=20000;
  soundddieflag=true;
}

void soundddie(void)
{
  sound_queue_post(SOUND_CMD_DDIE, 0, 0.0);
}

static void soundddieoff(void)
{
  soundddieflag=false;
}

static void soundddieupdate(void)
{
  if (soundddieflag) {
    soundddien++;
    if (soundddien==1)
      musicoff_apply();
    if (soundddien>=1 && soundddien<=10)
      soundddievalue=20000-soundddien*1000;
    if (soundddien>10)
      soundddievalue+=500;
    if (soundddievalue>30000)
      soundddieoff();
    t2val=soundddievalue;
  }
}


static bool sound1upflag=false;
static int16_t sound1upduration=0;

static void
sound1up_apply(void)
{
  sound1upduration=96;
  sound1upflag=true;
}

void sound1up(void)
{
  sound_queue_post(SOUND_CMD_1UP, 0, 0.0);
}

static void sound1upoff(void)
{
  sound1upflag=false;
}

static void sound1upupdate(void)
{
  if (sound1upflag) {
    if ((sound1upduration/3)%2!=0)
      t2val=(sound1upduration<<2)+600;
    sound1upduration--;
    if (sound1upduration<1)
      sound1upflag=false;
  }
}


static bool musicplaying=false;
static int16_t musicp=0,tuneno=0,noteduration=0,notevalue=0,musicmaxvol=0,
      musicattackrate=0,musicsustainlevel=0,musicdecayrate=0,musicnotewidth=0,
      musicreleaserate=0,musicstage=0,musicn=0,musicdfac=0;

static void
music_apply(int16_t tune, double dfac, uint16_t done_ack_id)
{
  if (soundmusic_done_ack_id != 0) {
    sound_ack_push(soundmusic_done_ack_id);
    soundmusic_done_ack_id = 0;
  }
  tuneno=tune;
  musicp=0;
  noteduration=0;

  if (!sndflag) {
    if (done_ack_id != 0)
      sound_ack_push(done_ack_id);
    return;
  }
  switch (tune) {
    case 0:
      musicmaxvol=50;
      musicattackrate=20;
      musicsustainlevel=20;
      musicdecayrate=10;
      musicreleaserate=4;
      musicdfac = 3.0 * dfac;
      break;
    case 1:
      musicmaxvol=50;
      musicattackrate=50;
      musicsustainlevel=8;
      musicdecayrate=15;
      musicreleaserate=1;
      musicdfac = 6.0 * dfac;
      break;
    case 2:
      if (!musicflag) {
        musicflag=true;
        restore_musicflag_after_death=true;
      }
      musicmaxvol=50;
      musicattackrate=50;
      musicsustainlevel=25;
      musicdecayrate=5;
      musicreleaserate=1;
      musicdfac = 10.0 * dfac;
  }
  musicplaying=true;
  if (tune==2) {
    soundddieoff();
    if (!wave_device_available) {
      if (done_ack_id != 0)
        sound_ack_push(done_ack_id);
      return;
    }
    assert(soundmusic_done_ack_id == 0 || soundmusic_done_ack_id == done_ack_id);
    soundmusic_done_ack_id = done_ack_id;
  }
}

uint16_t
musicwithack(int16_t tune, double dfac)
{
  uint16_t done_ack_id;

  if (!sound_local_sound_available()) {
    music(tune, dfac);
    return (0);
  }
  done_ack_id = sound_ack_alloc();
  sound_queue_push_done(SOUND_CMD_MUSIC, tune, dfac, done_ack_id);
  return (done_ack_id);
}

void music(int16_t tune, double dfac)
{
  sound_queue_push_done(SOUND_CMD_MUSIC, tune, dfac, 0);
}

static void
musicoff_apply(void)
{
  musicplaying=false;
  musicp=0;
  if (soundmusic_done_ack_id != 0) {
    sound_ack_push(soundmusic_done_ack_id);
    soundmusic_done_ack_id = 0;
  }
  if (restore_musicflag_after_death) {
    musicflag=false;
    restore_musicflag_after_death=false;
  }
}

void musicoff(void)
{
  sound_queue_post(SOUND_CMD_MUSIC_OFF, 0, 0.0);
}

static const int16_t bonusjingle[321]={
  0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,
   0xd59,4, 0xbe4,4, 0xa98,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,
  0x11d1,4, 0xd59,2, 0xa98,2, 0xbe4,4, 0xe24,4,0x11d1,4,0x11d1,2,0x11d1,2,
  0x11d1,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2, 0xd59,4, 0xbe4,4,
   0xa98,4, 0xd59,2, 0xa98,2, 0x8e8,10,0xa00,2, 0xa98,2, 0xbe4,2, 0xd59,4,
   0xa98,4, 0xd59,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,0x11d1,4,
  0x11d1,2,0x11d1,2, 0xd59,4, 0xbe4,4, 0xa98,4,0x11d1,2,0x11d1,2,0x11d1,4,
  0x11d1,2,0x11d1,2,0x11d1,4, 0xd59,2, 0xa98,2, 0xbe4,4, 0xe24,4,0x11d1,4,
  0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,
   0xd59,4, 0xbe4,4, 0xa98,4, 0xd59,2, 0xa98,2, 0x8e8,10,0xa00,2, 0xa98,2,
   0xbe4,2, 0xd59,4, 0xa98,4, 0xd59,4, 0xa98,2, 0xa98,2, 0xa98,4, 0xa98,2,
   0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4, 0x7f0,4, 0xa98,4, 0x7f0,4,
   0xa98,4, 0x7f0,4, 0xa98,4, 0xbe4,4, 0xd59,4, 0xe24,4, 0xfdf,4, 0xa98,2,
   0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4,
   0x7f0,4, 0xa98,4, 0x7f0,4, 0xa98,4, 0x7f0,4, 0x8e8,4, 0x970,4, 0x8e8,4,
   0x970,4, 0x8e8,4, 0xa98,2, 0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4,
   0xa98,2, 0xa98,2, 0xa98,4, 0x7f0,4, 0xa98,4, 0x7f0,4, 0xa98,4, 0x7f0,4,
   0xa98,4, 0xbe4,4, 0xd59,4, 0xe24,4, 0xfdf,4, 0xa98,2, 0xa98,2, 0xa98,4,
   0xa98,2, 0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4, 0x7f0,4, 0xa98,4,
   0x7f0,4, 0xa98,4, 0x7f0,4, 0x8e8,4, 0x970,4, 0x8e8,4, 0x970,4, 0x8e8,4,
  0x7d64};

static const int16_t backgjingle[291]={
   0xfdf,2,0x11d1,2, 0xfdf,2,0x1530,2,0x1ab2,2,0x1530,2,0x1fbf,4, 0xfdf,2,
  0x11d1,2, 0xfdf,2,0x1530,2,0x1ab2,2,0x1530,2,0x1fbf,4, 0xfdf,2, 0xe24,2,
   0xd59,2, 0xe24,2, 0xd59,2, 0xfdf,2, 0xe24,2, 0xfdf,2, 0xe24,2,0x11d1,2,
   0xfdf,2,0x11d1,2, 0xfdf,2,0x1400,2, 0xfdf,4, 0xfdf,2,0x11d1,2, 0xfdf,2,
  0x1530,2,0x1ab2,2,0x1530,2,0x1fbf,4, 0xfdf,2,0x11d1,2, 0xfdf,2,0x1530,2,
  0x1ab2,2,0x1530,2,0x1fbf,4, 0xfdf,2, 0xe24,2, 0xd59,2, 0xe24,2, 0xd59,2,
   0xfdf,2, 0xe24,2, 0xfdf,2, 0xe24,2,0x11d1,2, 0xfdf,2,0x11d1,2, 0xfdf,2,
   0xe24,2, 0xd59,4, 0xa98,2, 0xbe4,2, 0xa98,2, 0xd59,2,0x11d1,2, 0xd59,2,
  0x1530,4, 0xa98,2, 0xbe4,2, 0xa98,2, 0xd59,2,0x11d1,2, 0xd59,2,0x1530,4,
   0xa98,2, 0x970,2, 0x8e8,2, 0x970,2, 0x8e8,2, 0xa98,2, 0x970,2, 0xa98,2,
   0x970,2, 0xbe4,2, 0xa98,2, 0xbe4,2, 0xa98,2, 0xd59,2, 0xa98,4, 0xa98,2,
   0xbe4,2, 0xa98,2, 0xd59,2,0x11d1,2, 0xd59,2,0x1530,4, 0xa98,2, 0xbe4,2,
   0xa98,2, 0xd59,2,0x11d1,2, 0xd59,2,0x1530,4, 0xa98,2, 0x970,2, 0x8e8,2,
   0x970,2, 0x8e8,2, 0xa98,2, 0x970,2, 0xa98,2, 0x970,2, 0xbe4,2, 0xa98,2,
   0xbe4,2, 0xa98,2, 0xd59,2, 0xa98,4, 0x7f0,2, 0x8e8,2, 0xa98,2, 0xd59,2,
  0x11d1,2, 0xd59,2,0x1530,4, 0xa98,2, 0xbe4,2, 0xa98,2, 0xd59,2,0x11d1,2,
   0xd59,2,0x1530,4, 0xa98,2, 0x970,2, 0x8e8,2, 0x970,2, 0x8e8,2, 0xa98,2,
   0x970,2, 0xa98,2, 0x970,2, 0xbe4,2, 0xa98,2, 0xbe4,2, 0xd59,2, 0xbe4,2,
   0xa98,4,0x7d64};

static const int16_t dirge[]={
  0x7d00, 2,0x11d1, 6,0x11d1, 4,0x11d1, 2,0x11d1, 6, 0xefb, 4, 0xfdf, 2,
   0xfdf, 4,0x11d1, 2,0x11d1, 4,0x12e0, 2,0x11d1,12,0x7d00,16,0x7d00,16,
  0x7d00,16,0x7d00,16,0x7d00,16,0x7d00,16,0x7d00,16,0x7d00,16,0x7d00,16,
  0x7d00,16,0x7d00,16,0x7d00,16,0x7d64};

static void musicupdate(void)
{
  if (!musicplaying)
    return;
  if (noteduration!=0)
    noteduration--;
  else {
    musicstage=musicn=0;
    switch (tuneno) {
      case 0:
        noteduration=bonusjingle[musicp+1]*musicdfac;
        musicnotewidth=noteduration-musicdfac;
        notevalue=bonusjingle[musicp];
        musicp+=2;
        if (bonusjingle[musicp]==0x7d64)
          musicp=0;
        break;
      case 1:
        noteduration=backgjingle[musicp+1]*musicdfac;
        musicnotewidth=musicdfac * 2;
        notevalue=backgjingle[musicp];
        musicp+=2;
        if (backgjingle[musicp]==0x7d64)
          musicp=0;
        break;
      case 2:
        noteduration=dirge[musicp+1]*musicdfac;
        musicnotewidth=noteduration-musicdfac;
        notevalue=dirge[musicp];
	if (musicp > 0 && notevalue==0x7d00) {
	  if (soundmusic_done_ack_id != 0) {
	    sound_ack_push(soundmusic_done_ack_id);
	    soundmusic_done_ack_id = 0;
	  }
	  if (restore_musicflag_after_death) {
	    musicflag=false;
	    restore_musicflag_after_death=false;
	  }
	}
        musicp+=2;
        if (dirge[musicp]==0x7d64)
          musicp=0;
        break;
    }
  }
  musicn++;
  wavetype=1;
  t0val=notevalue;
  if (musicn>=musicnotewidth)
    musicstage=2;
  switch(musicstage) {
    case 0:
      if (musvol+musicattackrate>=musicmaxvol) {
        musicstage=1;
        musvol=musicmaxvol;
        break;
      }
      musvol+=musicattackrate;
      break;
    case 1:
      if (musvol-musicdecayrate<=musicsustainlevel) {
        musvol=musicsustainlevel;
        break;
      }
      musvol-=musicdecayrate;
      break;
    case 2:
      if (musvol-musicreleaserate<=1) {
        musvol=1;
        break;
      }
      musvol-=musicreleaserate;
  }
  if (musvol==1)
    t0val=0x7d00;
}


void soundpause(void)
{
  sound_queue_post(SOUND_CMD_PAUSE_ON, 0, 0.0);
}

void soundpauseoff(void)
{
  sound_queue_post(SOUND_CMD_PAUSE_OFF, 0, 0.0);
}

static void
soundpause_apply(bool paused)
{

  if (soundpausedflag == paused)
    return;
  soundpausedflag = paused;
  if (paused && sndflag) {
    timer2(40, false);
    setsoundt2();
    soundoff();
  }
}

static void
togglesound_apply(void)
{

  soundflag = !soundflag;
}

void
togglesound(void)
{

  sound_queue_post(SOUND_CMD_SOUND_TOGGLE, 0, 0.0);
}

static void
togglemusic_apply(void)
{

  musicflag = !musicflag;
}

void
togglemusic(void)
{

  sound_queue_post(SOUND_CMD_MUSIC_TOGGLE, 0, 0.0);
}

static void sett0(bool mode)
{
  if (sndflag) {
    if (!mode)
      timer2(t2val, mode);
    if (t0val<1000 && (wavetype==1 || wavetype==2))
      t0val=1000;
    if (musvol<1)
      musvol=1;
    if (musvol>50)
      musvol=50;
    pulsewidth=musvol*volume;
    timer0(t0val);
    setsoundmode();
  }
}

static bool soundt0flag=false;

void setsoundt2(void)
{
  if (soundt0flag) {
    spkrmode=0;
    soundt0flag=false;
    setspkrt2();
  }
}

void setsoundmode(void)
{
  spkrmode=wavetype;
  if (!soundt0flag && sndflag) {
    soundt0flag=true;
    setspkrt2();
  }
}

void initsound(void)
{
  assert(sound_cmdq.lock != NULL);
  assert(sound_ackq.lock != NULL);
  timer2(40, false);
  setspkrt2();
  timer0(0);
  wavetype=2;
  t0val=12000;
  musvol=8;
  t2val=40;
  soundt0flag=true;
  sndflag=true;
  spkrmode=0;
  setsoundt2();
  soundstop_apply();
  setupsound();
  timer0(0x4000);
  randvs=0;
}

void soundpreinit(void)
{

  if (!sound_queue_init())
    abort();
}
#else
void soundpreinit(void) {}
void initsound(void) {}
void soundstop(void) {}
uint16_t musicwithack(int16_t tune, double dfac) {(void)tune; (void)dfac; return (0);}
void music(int16_t tune, double dfac) {(void)tune; (void)dfac;}
void musicoff(void) {}
void soundlevdone(void) {}
void sound1up(void) {}
void soundwakeup(void) {}
void soundpause(void) {}
void soundpauseoff(void) {}
void setsoundt2(void) {}
void soundbonus(void) {}
void soundbonusoff(void) {}
void soundfire(int n) {(void)n;}
void soundexplode(int n) {(void)n;}
void soundfireoff(int n) {(void)n;}
void soundem(void) {}
void soundemerald(int n) {(void)n;}
void soundeatm(void) {}
void soundddie(void) {}
void soundwobble(void) {}
void soundwobbleoff(void) {}
void soundfall(void) {}
void soundfalloff(void) {}
void soundbreak(void) {}
void soundgold(void) {}
void togglesound(void) {}
void togglemusic(void) {}
void soundint(void) {}
bool soundackready(uint16_t ack_id) {(void)ack_id; return (true);}
#endif

static void s0killsound(void)
{
  setsoundt2();
  timer2(40, false);
}

static void s0setupsound(void)
{
  inittimer();
}
