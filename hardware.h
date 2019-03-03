void olddelay(int16_t t);
int32_t getkips(void);
void inittimer(void);
void gethrt(bool);

void s0soundoff(void);
void s0setspkrt2(void);
void s0settimer0(uint16_t t0v);
void s0timer0(uint16_t t0v);
void s0settimer2(uint16_t t2v, bool mode);
void s0timer2(uint16_t t2v, bool mode);
void s0soundinitglob(void);
void s0soundkillglob(void);

void initkeyb(void);
void restorekeyb(void);
int16_t getkey(bool);
bool kbhit(void);

void graphicsoff(void);
void gretrace(void);

void cgainit(void);
void cgaclear(void);
void cgapal(int16_t pal);
void cgainten(int16_t inten);
void cgaputi(int16_t x,int16_t y,uint8_t *p,int16_t w,int16_t h);
void cgageti(int16_t x,int16_t y,uint8_t *p,int16_t w,int16_t h);
void cgaputim(int16_t x,int16_t y,int16_t ch,int16_t w,int16_t h);
int16_t cgagetpix(int16_t x,int16_t y);
void cgawrite(int16_t x,int16_t y,int16_t ch,int16_t c);
void cgatitle(void);

void vgainit(void);
void vgaclear(void);
void vgapal(int16_t pal);
void vgainten(int16_t inten);
void vgaputi(int16_t x,int16_t y,uint8_t *p,int16_t w,int16_t h);
void vgageti(int16_t x,int16_t y,uint8_t *p,int16_t w,int16_t h);
void vgaputim(int16_t x,int16_t y,int16_t ch,int16_t w,int16_t h);
int16_t vgagetpix(int16_t x,int16_t y);
void vgawrite(int16_t x,int16_t y,int16_t ch,int16_t c);
void vgatitle(void);
