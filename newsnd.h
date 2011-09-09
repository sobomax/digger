#include "device.h"

void soundinitglob(int port,int irq,int dma,Uint4 bufsize,Uint4 samprate);
void s1setupsound(void);
void s1killsound(void);
void s1fillbuffer(void);
void s1settimer2(Uint4 t2);
void s1soundoff(void);
void s1setspkrt2(void);
void s1settimer0(Uint4 t0);
void s1timer0(Uint4 t0);
void s1timer2(Uint4 t2);

samp getsample(void);
