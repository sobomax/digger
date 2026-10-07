/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

void openplay(char *name);
void recstart(void);
void recname(char *name);
void playgetdir(int16_t *dir,bool *fire);
void recinit(void);
void recputrand(uint32_t randv);
uint32_t playgetrand(void);
void recputinit(char *init);
void recputeol(void);
void recputeog(void);
void playskipeol(void);
void recputdir(int16_t dir,bool fire);
void recsavedrf(void);
void playtakeover(void);

extern bool playing,savedrf,gotname,gotgame,drfvalid,kludge;
extern bool playend; /* The playback got to the end of the recording */
extern bool playbad; /* The recording couldn't be read */
extern int playerrno; /* Why, if it couldn't be opened, or 0 */
extern bool playtaken; /* The player took the game over from the playback */
extern uint32_t playtakeat; /* The tick to take it over on (/T), or 0 */
extern uint32_t playtakeftime; /* The speed to play it at then, or 0 */
