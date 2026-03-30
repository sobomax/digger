/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <stdatomic.h>

void initsound(void);
void soundpreinit(void);
void soundstop(void);
enum music_request {
  MUSIC_BONUS = 0,
  MUSIC_MAIN = 1,
  MUSIC_DIRGE = 2
};

uint16_t musicwithack(int16_t music, double dfac);
void music(int16_t music, double dfac);
void musicoff(void);
void soundlevdone(void);
void sound1up(void);
void soundwakeup(void);
void soundpause(void);
void soundpauseoff(void);
void setsoundt2(void);
void soundbonus(void);
void soundbonusoff(void);
void soundfire(int n);
void soundexplode(int n);
void soundfireoff(int n);
void soundem(void);
void soundemerald(int emn);
void soundeatm(void);
void soundddie(void);
void soundwobble(void);
void soundwobbleoff(void);
void soundfall(void);
void soundfalloff(void);
void soundbreak(void);
void soundgold(void);
void togglesound(void);
void togglemusic(void);

void soundint(void);

/*
void soundoff(void);
void timer2(uint16_t t2v);
*/

extern _Atomic bool soundflag,musicflag;
extern int16_t volume,timerrate,spkrmode,pulsewidth;
bool soundackready(uint16_t ack_id);

extern void (*setupsound)(void);
extern void (*killsound)(void);
extern void (*soundoff)(void);
extern void (*setspkrt2)(void);
extern void (*timer0)(uint16_t t0v);
extern void (*timer2)(uint16_t t2v, bool mode);
extern void (*soundkillglob)(void);
