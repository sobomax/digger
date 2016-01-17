/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

struct digger_draw_api;

void dobags(struct digger_draw_api *);
int16_t getnmovingbags(void);
void cleanupbags(void);
void initbags(void);
void drawbags(void);
bool pushbags(struct digger_draw_api *, int16_t dir,int *clfirst,int *clcoll);
bool pushudbags(struct digger_draw_api *, int *clfirst,int *clcoll);
int16_t bagy(int16_t bag);
int16_t getbagdir(int16_t bag);
void removebags(int *clfirst,int *clcoll);
bool bagexist(int bag);

