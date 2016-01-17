/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

void setretr(bool f);
void movedrawspr(int16_t n,int16_t x,int16_t y);
void erasespr(int16_t n);
void createspr(int16_t n,int16_t ch,uint8_t *mov,int16_t wid,int16_t hei,int16_t bwid,
               int16_t bhei);
void initspr(int16_t n,int16_t ch,int16_t wid,int16_t hei,int16_t bwid,int16_t bhei);
void drawspr(int16_t n,int16_t x,int16_t y);
void initmiscspr(int16_t x,int16_t y,int16_t wid,int16_t hei);
void getis(void);
void drawmiscspr(int16_t x,int16_t y,int16_t ch,int16_t wid,int16_t hei);

struct digger_draw_api;
#if 0
extern struct digger_draw_api *ddap;
#endif

#if defined(_SDL) || defined(_VGL)
extern void doscreenupdate(void);
#endif

extern int first[TYPES],coll[SPRITES];
