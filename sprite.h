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

extern void (*ginit)(void);
extern void (*gclear)(void);
extern void (*gpal)(int16_t pal);
extern void (*ginten)(int16_t inten);
extern void (*gputi)(int16_t x,int16_t y,uint8_t *p,int16_t w,int16_t h);
extern void (*ggeti)(int16_t x,int16_t y,uint8_t *p,int16_t w,int16_t h);
extern void (*gputim)(int16_t x,int16_t y,int16_t ch,int16_t w,int16_t h);
extern int16_t (*ggetpix)(int16_t x,int16_t y);
extern void (*gtitle)(void);
extern void (*gwrite)(int16_t x,int16_t y,int16_t ch,int16_t c);

#if defined(_SDL) || defined(_VGL)
extern void doscreenupdate(void);
#endif

extern int first[TYPES],coll[SPRITES];
