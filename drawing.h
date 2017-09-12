/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#define MAX_TEXT_LEN (MAX_W / CHR_W)

struct digger_draw_api;

void outtext(struct digger_draw_api *, const char *p,int16_t x,int16_t y,int16_t c);
void erasetext(struct digger_draw_api *ddap, int16_t n, int16_t x, int16_t y, int16_t c);

void creatembspr(void);
void initmbspr(void);
void drawdigger(int n,int16_t t,int16_t x,int16_t y,bool f);
void drawgold(int16_t n,int16_t t,int16_t x,int16_t y);
void drawemerald(int16_t x,int16_t y);
void eraseemerald(int16_t x,int16_t y);
void drawbonus(int16_t x,int16_t y);
void drawlives(struct digger_draw_api *);
void savefield(void);
void makefield(void);
void drawstatics(struct digger_draw_api *);
void drawfire(int n,int16_t x,int16_t y,int16_t t);
void eatfield(int16_t x,int16_t y,int16_t dir);
void drawrightblob(int16_t x,int16_t y);
void drawleftblob(int16_t x,int16_t y);
void drawtopblob(int16_t x,int16_t y);
void drawbottomblob(int16_t x,int16_t y);
void drawfurryblob(int16_t x,int16_t y);
void drawsquareblob(int16_t x,int16_t y);

extern int16_t field[];
