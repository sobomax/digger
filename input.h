/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

void detectjoy(void);
bool teststart(void);
void readdirect(int n);
int16_t getdirect(int n);
void checkkeyb(void);
void flushkeybuf(void);
void findkey(int kn);
void clearfire(int n);

extern bool firepflag,fire2pflag,escape;
extern int8_t keypressed;
extern int16_t akeypressed;

#define NKEYS 19

#define DKEY_CHT 10 /* Cheat */
#define DKEY_SUP 11 /* Increase speed */
#define DKEY_SDN 12 /* Decrease speed */
#define DKEY_MTG 13 /* Toggle music */
#define DKEY_STG 14 /* Toggle sound */
#define DKEY_EXT 15 /* Exit */
#define DKEY_PUS 16 /* Pause */
#define DKEY_MCH 17 /* Mode change */
#define DKEY_SDR 18 /* Save DRF */

extern int keycodes[NKEYS][5];
extern bool krdf[NKEYS];
extern bool pausef,mode_change;
