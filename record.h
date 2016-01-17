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

extern bool playing,savedrf,gotname,gotgame,drfvalid,kludge;
