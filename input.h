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
void input_reset_directions(void);
void input_advance_fire_state(void);
bool input_get_fire_active(int n);
uint8_t input_snapshot_primary_controls(void);
bool input_consume_anykey(void);
void input_reset_network(void);
void input_enable_network_mode(void);
void input_set_network_controls(int slot, uint8_t bits);

extern bool firepflag,fire2pflag,escape;
extern int8_t keypressed;
extern int16_t akeypressed;

#define INPUT_CTRL_UP    0x01
#define INPUT_CTRL_DOWN  0x02
#define INPUT_CTRL_RIGHT 0x04
#define INPUT_CTRL_LEFT  0x08
#define INPUT_CTRL_FIRE  0x10

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
