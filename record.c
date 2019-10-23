/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "def.h"
#include "record.h"
#include "hardware.h"
#include "sound.h"
#include "input.h"
#include "main.h"
#include "scores.h"
#include "sprite.h"
#include "game.h"

static char huge *recb,huge *plb,huge *plp;

bool playing=false,savedrf=false,gotname=false,gotgame=false,drfvalid=true,
     kludge=false;

static char rname[128];

static int reccc=0,recrl=0,rlleft=0;
static uint32_t recp=0;
static char recd,rld;

static void mprintf(const char *f,...) __attribute__((format(printf, 1, 2)));;
static void makedir(int16_t *dir,bool *fire,char d);
static char maked(int16_t dir,bool fire);

#define DEFAULTSN "DIGGER.DRF"

#ifdef INTDRF
FILE *info;
#endif

static char *
smart_fgets(char * restrict str, int size, FILE * restrict stream)
{
  char *rval;
  int len;

  rval = fgets(str, size, stream);
  if (rval == NULL) {
    return (NULL);
  }
  len = strlen(rval);
  if (len > 0 && rval[len - 1] == '\n') {
    if (len > 1 && rval[len - 2] == '\r') {
      len--;
    }
    len--;
    rval[len] = '\0';
  }
  return (rval);
}

void openplay(char *name)
{
  FILE *playf=fopen(name,"rb");
  int32_t l,i;
  char buf[80];
  int c,x,y,n,origgtime=dgstate.gtime;
  bool origg=dgstate.gauntlet;
  int16_t origstartlev=dgstate.startlev,orignplayers=dgstate.nplayers,origdiggers=dgstate.diggers;
#ifdef INTDRF
  info=fopen("DRFINFO.TXT","wt");
#endif
  if (playf==NULL) {
    escape=true;
    return;
  }
  dgstate.gauntlet=false;
  dgstate.startlev=1;
  dgstate.nplayers=1;
  dgstate.diggers=1;
  /* The file is in two distint parts. In the first, line breaks are used as
     separators. In the second, they are ignored. This is the first. */

  /* Get id string */
  if (smart_fgets(buf, 80, playf) == NULL) {
    goto out_0;
  }
  if (buf[0]!='D' || buf[1]!='R' || buf[2]!='F') {
    goto out_0;
  }
  /* Get version for kludge switches */
  if (smart_fgets(buf, 80, playf) == NULL) {
    goto out_0;
  }
  if (atol(buf+7)<=19981125l)
    kludge=true;
  /* Get mode */
  if (smart_fgets(buf, 80, playf) == NULL) {
    goto out_0;
  }
  if (*buf=='1') {
    dgstate.nplayers=1;
    x=1;
  }
  else
    if (*buf=='2') {
      dgstate.nplayers=2;
      x=1;
    }
    else {
      if (*buf=='M') {
        dgstate.diggers=buf[1]-'0';
        x=2;
      }
      else
        x=0;
      if (buf[x]=='G') {
        dgstate.gauntlet=true;
        x++;
        dgstate.gtime=atoi(buf+x);
        while (buf[x]>='0' && buf[x]<='9')
          x++;
      }
    }
  if (buf[x]=='U') /* Unlimited lives are ignored on playback. */
    x++;
  if (buf[x]=='I')
    dgstate.startlev=atoi(buf+x+1);
  /* Get bonus score */
  if (smart_fgets(buf, 80, playf) == NULL) {
    goto out_0;
  }
  bonusscore=atoi(buf);
  for (n=0;n<8;n++)
    for (y=0;y<10;y++) {
      for (x=0;x<15;x++)
        buf[x]=' ';
      /* Get a line of map */
      if (smart_fgets(buf, 80, playf) == NULL) {
        goto out_0;
      }
      for (x=0;x<15;x++)
        dgstate.leveldat[n][y][x]=buf[x];
    }

  /* This is the second. The line breaks here really are only so that the file
     can be emailed. */

  i=ftell(playf);
  if (i < 0 || fseek(playf,0,SEEK_END) < 0)
    goto out_0;
  l=ftell(playf)-i;
  if (l < 0 || fseek(playf,i,SEEK_SET) < 0)
    goto out_0;
  plb=plp=(char huge *)farmalloc(l);
  if (plb==(char huge *)NULL) {
    goto out_0;
  }

  for (i=0;i<l;i++) {
    c=fgetc(playf); /* Get everything that isn't line break into 1 string */
    if (c == EOF)
      goto out_0;
    if (c>=' ')
      *(plp++)= (char)c;
  }
  fclose(playf);
  plp=plb;

  playing=true;
  recinit();
  game();
  gotgame=true;
  playing=false;
  farfree(plb);
  dgstate.gauntlet=origg;
  dgstate.gtime=origgtime;
  kludge=false;
  dgstate.startlev=origstartlev;
  dgstate.diggers=origdiggers;
  dgstate.nplayers=orignplayers;
  return;
out_0:
  if (playf != NULL) {
    fclose(playf);
  }
  escape = true;
}

void recstart(void)
{
  uint32_t s=MAX_REC_BUFFER;
  do {
    recb=(char huge *)farmalloc(s);
    if (recb==NULL)
      s>>=1;
  } while (recb==(char huge *)NULL && s>1024);
  if (recb==NULL) {
    finish();
    printf("Cannot allocate memory for recording buffer.\n");
    exit(1);
  }
  recp=0;
}

static void mprintf(const char *f,...)
{
  va_list ap;
  char buf[80];
  int i,l;
  va_start(ap,f);
  vsprintf(buf,f,ap);
  va_end(ap);
  l=strlen(buf);
  for (i=0;i<l;i++)
    recb[recp+i]=buf[i];
  recp+=l;
  if (recp>MAX_REC_BUFFER-80)
    recp=0;          /* Give up, file is too long */
}

static void makedir(int16_t *dir,bool *fire,char d)
{
  if (d>='A' && d<='Z') {
    *fire=true;
    d-='A'-'a';
  }
  else
    *fire=false;
  switch (d) {
    case 's': *dir=DIR_NONE; break;
    case 'r': *dir=DIR_RIGHT; break;
    case 'u': *dir=DIR_UP; break;
    case 'l': *dir=DIR_LEFT; break;
    case 'd': *dir=DIR_DOWN; break;
  }
}

void playgetdir(int16_t *dir,bool *fire)
{
  if (rlleft>0) {
    makedir(dir,fire,rld);
    rlleft--;
  }
  else {
    if (*plp=='E' || *plp=='e') {
      escape=true;
      return;
    }
    rld=*(plp++);
    while (*plp>='0' && *plp<='9')
      rlleft=rlleft*10+((*(plp++))-'0');
    makedir(dir,fire,rld);
    if (rlleft>0)
      rlleft--;
  }
}

static char maked(int16_t dir,bool fire)
{
  char d;
  if (dir==DIR_NONE)
    d='s';
  else
    d="ruld"[dir>>1];
  if (fire)
    d+='A'-'a';
  return d;
}

void putrun(void)
{
  if (recrl>1)
    mprintf("%c%i",recd,recrl);
  else
    mprintf("%c",recd);
  reccc++;
  if (recrl>1) {
    reccc++;
    if (recrl>=10) {
      reccc++;
      if (recrl>=100)
        reccc++;
    }
  }
  if (reccc>=60) {
    mprintf("\n");
    reccc=0;
  }
}

void recputdir(int16_t dir,bool fire)
{
  char d=maked(dir,fire);
  if (recrl==0)
    recd=d;
  if (recd!=d) {
    putrun();
    recd=d;
    recrl=1;
  }
  else {
    if (recrl==999) {
      putrun(); /* This probably won't ever happen. */
      recrl=0;
    }
    recrl++;
  }
}

void recinit(void)
{
  int x,y,l;
  recp=0;
  drfvalid=true;

  mprintf("DRF\n"); /* Required at start of DRF */
  if (kludge)
    mprintf("AJ DOS 19981125\n");
  else
    mprintf(DIGGER_VERSION"\n");
  if (dgstate.diggers>1) {
    mprintf("M%i",dgstate.diggers);
    if (dgstate.gauntlet)
      mprintf("G%i",dgstate.gtime);
  }
  else
    if (dgstate.gauntlet)
      mprintf("G%i",dgstate.gtime);
    else
      mprintf("%i",dgstate.nplayers);
/*  if (unlimlives)
    mprintf("U"); */
  if (dgstate.startlev>1)
    mprintf("I%i",dgstate.startlev);
  mprintf("\n%i\n",bonusscore);
  for (l=0;l<8;l++) {
    for (y=0;y<MHEIGHT;y++) {
      for (x=0;x<MWIDTH;x++)
        mprintf("%c",dgstate.leveldat[l][y][x]);
      mprintf("\n");
    }
  }
  reccc=recrl=0;
}

void recputrand(uint32_t randv)
{
  mprintf("%08lX\n", (unsigned long)randv);
  reccc=recrl=0;
}

void recsavedrf(void)
{
  FILE *recf;
  uint32_t i;
  int j;
  bool gotfile=true;
  char nambuf[80],init[4];
  if (!drfvalid)
    return;
  if (gotname) {
    if ((recf=fopen(rname,"wt"))==NULL)
      gotname=false;
    else
      gotfile=true;
  }
  if (!gotname) {
    if (dgstate.nplayers==2)
      recf=fopen(DEFAULTSN,"wt"); /* Should get a name, really */
    else {
      for (j=0;j<3;j++) {
        init[j]=scoreinit[0][j];
        if (!((init[j]>='A' && init[j]<='Z') ||
              (init[j]>='a' && init[j]<='z')))
          init[j]='_';
      }
      init[3]=0;
      if (scoret<100000l)
        sprintf(nambuf,"%s%i",init,scoret);
      else
        if (init[2]=='_')
          sprintf(nambuf,"%c%c%i",init[0],init[1],scoret);
        else
          if (init[0]=='_')
            sprintf(nambuf,"%c%c%i",init[1],init[2],scoret);
          else
            sprintf(nambuf,"%c%c%i",init[0],init[2],scoret);
      strcat(nambuf,".drf");
      recf=fopen(nambuf,"wt");
    }
    if (recf==NULL)
      gotfile=false;
    else
      gotfile=true;
  }
  if (!gotfile)
    return;
  for (i=0;i<recp;i++)
    fputc(recb[i],recf);
  fclose(recf);
}

void playskipeol(void)
{
  plp+=3;
}

uint32_t playgetrand(void)
{
  int i;
  uint32_t r=0;
  char p;
  if ((*plp)=='*')
    plp+=4;
  for (i=0;i<8;i++) {
    p=*(plp++);
    if (p>='0' && p<='9')
      r|=(uint32_t)(p-'0')<<((7-i)<<2);
    if (p>='A' && p<='F')
      r|=(uint32_t)(p-'A'+10)<<((7-i)<<2);
    if (p>='a' && p<='f')
      r|=(uint32_t)(p-'a'+10)<<((7-i)<<2);
  }
  return r;
}

void recputinit(char *init)
{
  mprintf("*%c%c%c\n",init[0],init[1],init[2]);
}

void recputeol(void)
{
  if (recrl>0)
    putrun();
  if (reccc>0)
    mprintf("\n");
  mprintf("EOL\n");
}

void recputeog(void)
{
  mprintf("EOG\n");
}

void recname(char *name)
{
  assert(strlen(name) < sizeof(rname));
  gotname=true;
  strcpy(rname,name);
}
