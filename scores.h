/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

struct digger_draw_api;

void loadscores(void);
void showtable(struct digger_draw_api *);
void zeroscores(void);
void writecurscore(struct digger_draw_api *, int col);
void drawscores(struct digger_draw_api *);
void initscores(struct digger_draw_api *);
void endofgame(struct digger_draw_api *);
void scorekill(struct digger_draw_api *, int n);
void scorekill2(struct digger_draw_api *);
void scoreemerald(struct digger_draw_api *, int n);
void scoreoctave(struct digger_draw_api *, int n);
void scoregold(struct digger_draw_api *, int n);
void scorebonus(struct digger_draw_api *, int n);
void scoreeatm(struct digger_draw_api *, int n,int msc);
void addscore(struct digger_draw_api *, int n,int16_t score);

#ifdef INTDRF
int32_t getscore0(void);
#endif
int32_t gettscore(int n);

extern uint16_t bonusscore;
extern int32_t scoret;

extern char scoreinit[11][4];
