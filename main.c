/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "def.h"
#include "digger_types.h"
#include "hardware.h"
#include "sound.h"
#include "sprite.h"
#include "input.h"
#include "scores.h"
#include "drawing.h"
#include "digger.h"
#include "monster.h"
#include "monster_obj.h"
#include "digger_obj.h"
#include "bags.h"
#include "record.h"
#include "main.h"
#include "newsnd.h"
#include "ini.h"
#include "draw_api.h"

/* global variables */
char pldispbuf[14];
int16_t curplayer=0,nplayers=1,penalty=0,diggers=1,startlev=1;
bool unlimlives=false, gauntlet=false,timeout=false,synchvid=false;
int gtime=0;

static struct game
{
  int16_t level;
  bool levdone;
} gamedat[2];

static bool levnotdrawn=false,alldead=false;
bool started;

char levfname[132];
bool levfflag=false;
FILE *digger_log;

void shownplayers(void);
void switchnplayers(void);
static void drawscreen(struct digger_draw_api *);
void initchars(void);
void checklevdone(void);
int16_t levno(void);
void testpause(void);
void calibrate(void);
void parsecmd(int argc,char *argv[]);
void patchcga(void);
void initlevel(void);
void finish(void);
void inir(void);
void redefkeyb(bool allf);
int getalllives(void);

int8_t leveldat[8][MHEIGHT][MWIDTH]=
{{"S   B     HHHHS",
  "V  CC  C  V B  ",
  "VB CC  C  V    ",
  "V  CCB CB V CCC",
  "V  CC  C  V CCC",
  "HH CC  C  V CCC",
  " V    B B V    ",
  " HHHH     V    ",
  "C   V     V   C",
  "CC  HHHHHHH  CC"},
 {"SHHHHH  B B  HS",
  " CC  V       V ",
  " CC  V CCCCC V ",
  "BCCB V CCCCC V ",
  "CCCC V       V ",
  "CCCC V B  HHHH ",
  " CC  V CC V    ",
  " BB  VCCCCV CC ",
  "C    V CC V CC ",
  "CC   HHHHHH    "},
 {"SHHHHB B BHHHHS",
  "CC  V C C V BB ",
  "C   V C C V CC ",
  " BB V C C VCCCC",
  "CCCCV C C VCCCC",
  "CCCCHHHHHHH CC ",
  " CC  C V C  CC ",
  " CC  C V C     ",
  "C    C V C    C",
  "CC   C H C   CC"},
 {"SHBCCCCBCCCCBHS",
  "CV  CCCCCCC  VC",
  "CHHH CCCCC HHHC",
  "C  V  CCC  V  C",
  "   HHH C HHH   ",
  "  B  V B V  B  ",
  "  C  VCCCV  C  ",
  " CCC HHHHH CCC ",
  "CCCCC CVC CCCCC",
  "CCCCC CHC CCCCC"},
 {"SHHHHHHHHHHHHHS",
  "VBCCCCBVCCCCCCV",
  "VCCCCCCV CCBC V",
  "V CCCC VCCBCCCV",
  "VCCCCCCV CCCC V",
  "V CCCC VBCCCCCV",
  "VCCBCCCV CCCC V",
  "V CCBC VCCCCCCV",
  "VCCCCCCVCCCCCCV",
  "HHHHHHHHHHHHHHH"},
 {"SHHHHHHHHHHHHHS",
  "VCBCCV V VCCBCV",
  "VCCC VBVBV CCCV",
  "VCCCHH V HHCCCV",
  "VCC V CVC V CCV",
  "VCCHH CVC HHCCV",
  "VC V CCVCC V CV",
  "VCHHBCCVCCBHHCV",
  "VCVCCCCVCCCCVCV",
  "HHHHHHHHHHHHHHH"},
 {"SHCCCCCVCCCCCHS",
  " VCBCBCVCBCBCV ",
  "BVCCCCCVCCCCCVB",
  "CHHCCCCVCCCCHHC",
  "CCV CCCVCCC VCC",
  "CCHHHCCVCCHHHCC",
  "CCCCV CVC VCCCC",
  "CCCCHH V HHCCCC",
  "CCCCCV V VCCCCC",
  "CCCCCHHHHHCCCCC"},
 {"HHHHHHHHHHHHHHS",
  "V CCBCCCCCBCC V",
  "HHHCCCCBCCCCHHH",
  "VBV CCCCCCC VBV",
  "VCHHHCCCCCHHHCV",
  "VCCBV CCC VBCCV",
  "VCCCHHHCHHHCCCV",
  "VCCCC V V CCCCV",
  "VCCCCCV VCCCCCV",
  "HHHHHHHHHHHHHHH"}};

int16_t getlevch(int16_t x,int16_t y,int16_t l)
{
  if ((l==3 || l==4) && !levfflag && diggers==2 && y==9 && (x==6 || x==8))
    return 'H';
  return leveldat[l-1][y][x];
}

#ifdef INTDRF
extern FILE *info;
#endif

extern struct digger_draw_api *ddap;

void game(void)
{
  int16_t t,c,i;
  bool flashplayer=false;
  if (gauntlet) {
    cgtime=gtime*1193181l;
    timeout=false;
  }
  initlives();
  gamedat[0].level=startlev;
  if (nplayers==2)
    gamedat[1].level=startlev;
  alldead=false;
  ddap->gclear();
  curplayer=0;
  initlevel();
  curplayer=1;
  initlevel();
  zeroscores();
  bonusvisible=true;
  if (nplayers==2)
    flashplayer=true;
  curplayer=0;
  while (getalllives()!=0 && !escape && !timeout) {
    while (!alldead && !escape && !timeout) {
      initmbspr();

      if (playing)
        randv=playgetrand();
      else
        randv=getlrt();
#ifdef INTDRF
      fprintf(info,"%lu\n",randv);
      frame=0;
#endif
      recputrand(randv);
      if (levnotdrawn) {
        levnotdrawn=false;
        drawscreen(ddap);
        if (flashplayer) {
          flashplayer=false;
          strcpy(pldispbuf,"PLAYER ");
          if (curplayer==0)
            strcat(pldispbuf,"1");
          else
            strcat(pldispbuf,"2");
          cleartopline();
          for (t=0;t<15;t++)
            for (c=1;c<=3;c++) {
              outtext(ddap, pldispbuf,108,0,c);
              writecurscore(ddap, c);
              newframe();
              if (escape)
                return;
            }
          drawscores(ddap);
          for (i=0;i<diggers;i++)
            addscore(ddap, i,0);
        }
      }
      else
        initchars();
      outtext(ddap, "        ",108,0,3);
      initscores(ddap);
      drawlives(ddap);
      music(1);

      flushkeybuf();
      for (i=0;i<diggers;i++)
        readdirect(i);
      while (!alldead && !gamedat[curplayer].levdone && !escape && !timeout) {
        penalty=0;
        if (toggle) {
          switchmode();
          toggle=false;
        } 
        dodigger(ddap);
        domonsters(ddap);
        dobags(ddap);
        if (penalty>8)
          incmont(penalty-8);
        testpause();
        checklevdone();
      }
      erasediggers();
      musicoff();
      t=20;
      while ((getnmovingbags()!=0 || t!=0) && !escape && !timeout) {
        if (t!=0)
          t--;
        penalty=0;
        dobags(ddap);
        dodigger(ddap);
        domonsters(ddap);
        if (penalty<8)
          t=0;
      }
      soundstop();
      for (i=0;i<diggers;i++)
        killfire(i);
      erasebonus(ddap);
      cleanupbags();
      savefield();
      erasemonsters();
      recputeol();
      if (playing)
        playskipeol();
      if (escape)
        recputeog();
      if (gamedat[curplayer].levdone)
        soundlevdone();
      if (countem()==0 || gamedat[curplayer].levdone) {
#ifdef INTDRF
        fprintf(info,"%i\n",frame);
#endif
        for (i=curplayer;i<diggers+curplayer;i++)
          if (getlives(i)>0 && !digalive(i))
            declife(i);
        drawlives(ddap);
        gamedat[curplayer].level++;
        if (gamedat[curplayer].level>1000)
          gamedat[curplayer].level=1000;
        initlevel();
      }
      else
        if (alldead) {
#ifdef INTDRF
          fprintf(info,"%i\n",frame);
#endif
          for (i=curplayer;i<curplayer+diggers;i++)
            if (getlives(i)>0)
              declife(i);
          drawlives(ddap);
        }
      if ((alldead && getalllives()==0 && !gauntlet && !escape) || timeout)
        endofgame(ddap);
    }
    alldead=false;
    if (nplayers==2 && getlives(1-curplayer)!=0) {
      curplayer=1-curplayer;
      flashplayer=levnotdrawn=true;
    }
  }
#ifdef INTDRF
  fprintf(info,"-1\n%lu\n%i",getscore0(),gamedat[0].level);
#endif
}

static bool quiet=false;
static sound_rate,sound_length;

#if defined(UNIX) && defined(_SDL)
#include "sdl_vid.h"
#endif

void maininit(void)
{
  static int maininited = 0;

  if (maininited != 0) {
    return;
  }
  calibrate();
  ddap->ginit();
  ddap->gpal(0);
  setretr(true);
  initkeyb();
  detectjoy();
  inir();
  initsound();
  recstart();
  maininited = 1;
}

int main(int argc,char *argv[])
{
  parsecmd(argc,argv);
  maininit();
  return mainprog();
}

int mainprog(void)
{
  int16_t frame,t;
  struct monster_obj *nobbin, *hobbin;
  struct digger_obj odigger;
  struct obj_position newpos;
  loadscores();
  escape=false;
  nobbin = NULL;
  hobbin = NULL;
  do {
    soundstop();
    creatembspr();
    detectjoy();
    ddap->gclear();
    ddap->gtitle();
    outtext(ddap, "D I G G E R",100,0,3);
    shownplayers();
    showtable(ddap);
    started=false;
    frame=0;
    newframe();
    teststart();
    while (!started) {
      started=teststart();
      if (akeypressed==27 || akeypressed=='n' || akeypressed=='N') {
        switchnplayers();
        shownplayers();
        akeypressed=0;
      }
      if (frame==0)
        for (t=54;t<174;t+=12)
          outtext(ddap, "            ",164,t,0);
      if (frame==50) {
        if (nobbin != NULL) {
          CALL_METHOD(nobbin, dtor);
        }
        nobbin = monster_obj_ctor(0, MON_NOBBIN, DIR_LEFT, 292, 63);
        CALL_METHOD(nobbin, put);
      }
      if (frame>50 && frame<=77) {
        CALL_METHOD(nobbin, getpos, &newpos);
        newpos.x -= 4;
        if (frame == 77) {
          newpos.dir = DIR_RIGHT;
        }
        CALL_METHOD(nobbin, setpos, &newpos);
      }
      if (frame > 50) {
        CALL_METHOD(nobbin, animate);
      }

      if (frame==83)
        outtext(ddap, "NOBBIN",216,64,2);
      if (frame==90) {
        if (hobbin != NULL) {
          CALL_METHOD(hobbin, dtor);
        }
        hobbin = monster_obj_ctor(1, MON_NOBBIN, DIR_LEFT, 292, 82);
        CALL_METHOD(hobbin, put);
        CALL_METHOD(hobbin, mutate);
      }
      if (frame>90 && frame<=117) {
        CALL_METHOD(hobbin, getpos, &newpos);
        newpos.x -= 4;
        if (frame == 117) { 
          newpos.dir = DIR_RIGHT;
        }
        CALL_METHOD(hobbin, setpos, &newpos);
      }
      if (frame > 90) {
        CALL_METHOD(hobbin, animate);
      }
      if (frame==123)
        outtext(ddap, "HOBBIN",216,83,2);
      if (frame==130) {
        digger_obj_init(&odigger, 0, DIR_LEFT, 292, 101);
        CALL_METHOD(&odigger, put);
      }
      if (frame>130 && frame<=157) {
        odigger.x -= 4;
      }
      if (frame>157) {
        odigger.dir = DIR_RIGHT;
      }
      if (frame >= 130) {
        CALL_METHOD(&odigger, animate);
      }
      if (frame==163)
        outtext(ddap, "DIGGER",216,102,2);
      if (frame==178) {
        movedrawspr(FIRSTBAG,184,120);
        drawgold(0,0,184,120);
      }
      if (frame==183)
        outtext(ddap, "GOLD",216,121,2);
      if (frame==198)
        drawemerald(184,141);
      if (frame==203)
        outtext(ddap, "EMERALD",216,140,2);
      if (frame==218)
        drawbonus(184,158);
      if (frame==223)
        outtext(ddap, "BONUS",216,159,2);
      newframe();
      frame++;
      if (frame>250)
        frame=0;
    }
    if (toggle) {
      switchmode();
      toggle=false;
      continue;
    }
    if (savedrf) {
      if (gotgame) {
        recsavedrf();
        gotgame=false;
      }
      savedrf=false;
      continue;
    }
    if (escape)
      break;
    recinit();
    game();
    gotgame=true;
    if (gotname) {
      recsavedrf();
      gotgame=false;
    }
    savedrf=false;
    escape=false;
    toggle=false;
  } while (!escape);
  finish();
  return 0;
}

void finish(void)
{
  killsound();
  soundoff();
  soundkillglob();
  restorekeyb();
  graphicsoff();
}

void shownplayers(void)
{
  outtext(ddap, "          ",180,25,3);
  outtext(ddap, "            ",170,39,3);

  if (diggers==2)
    if (gauntlet) {
      outtext(ddap, "TWO PLAYER",180,25,3);
      outtext(ddap, "GAUNTLET",192,39,3);
    }
    else {
      outtext(ddap, "TWO PLAYER",180,25,3);
      outtext(ddap, "SIMULTANEOUS",170,39,3);
    }
  else
    if (gauntlet) {
      outtext(ddap, "GAUNTLET",192,25,3);
      outtext(ddap, "MODE",216,39,3);
    }
    else
      if (nplayers==1) {
        outtext(ddap, "ONE",220,25,3);
        outtext(ddap, " PLAYER ",192,39,3);
      }
      else {
        outtext(ddap, "TWO",220,25,3);
        outtext(ddap, " PLAYERS",184,39,3);
      }
}

int getalllives(void)
{
  int t=0,i;
  for (i=curplayer;i<diggers+curplayer;i++)
    t+=getlives(i);
  return t;
}

void switchnplayers(void)
{
  if (!gauntlet && nplayers==1 && diggers==1) {
    nplayers=2;
  } else if (!gauntlet && nplayers==2 && diggers==1) {
    diggers=2;
  } else if (!gauntlet && nplayers==2 && diggers==2) {
    gauntlet=true;
    diggers=1;
    nplayers=1;
  } else if (gauntlet && nplayers==1 && diggers==1) {
    diggers=2;
  } else {
    gauntlet=false;
    nplayers=1;
    diggers=1;
  }
}

void initlevel(void)
{
  gamedat[curplayer].levdone=false;
  makefield();
  makeemfield();
  initbags();
  levnotdrawn=true;
}

void drawscreen(struct digger_draw_api *ddap)
{
  creatembspr();
  drawstatics(ddap);
  drawbags();
  drawemeralds();
  initdigger();
  initmonsters();
}

void initchars(void)
{
  initmbspr();
  initdigger();
  initmonsters();
}

void checklevdone(void)
{
  if ((countem()==0 || monleft()==0) && isalive())
    gamedat[curplayer].levdone=true;
  else
    gamedat[curplayer].levdone=false;
}

void incpenalty(void)
{
  penalty++;
}

void cleartopline(void)
{
  outtext(ddap, "                          ",0,0,3);
  outtext(ddap, " ",308,0,3);
}

int16_t levplan(void)
{
  int16_t l=levno();
  if (l>8)
    l=(l&3)+5; /* Level plan: 12345678, 678, (5678) 247 times, 5 forever */
  return l;
}

int16_t levof10(void)
{
  if (gamedat[curplayer].level>10)
    return 10;
  return gamedat[curplayer].level;
}

int16_t levno(void)
{
  return gamedat[curplayer].level;
}

void setdead(bool df)
{
  alldead=df;
}

void testpause(void)
{
  int i;
  if (pausef) {
    soundpause();
    sett2val(40);
    setsoundt2();
    cleartopline();
    outtext(ddap, "PRESS ANY KEY",80,0,1);
    getkey(true);
    cleartopline();
    drawscores(ddap);
    for (i=0;i<diggers;i++)
      addscore(ddap, i,0);
    drawlives(ddap);
    if (!synchvid)
      curtime=gethrt();
    pausef=false;
  }
  else
    soundpauseoff();
}

void calibrate(void)
{
  volume=(int16_t)(getkips()/291);
  if (volume==0)
    volume=1;
}

static int
read_levf(char *levfname)
{
  FILE *levf;

  levf = fopen(levfname, "rb");
  if (levf == NULL) {
    strcat(levfname,".DLF");
    levf = fopen(levfname,"rb");
  }
  if (levf == NULL) {
#if defined(DIGGER_DEBUG)
      fprintf(digger_log, "read_levf: levels file open error\n");
#endif
      return (-1);
  }
  if (fread(&bonusscore, 2, 1, levf) < 1) {
#if defined(DIGGER_DEBUG)
    fprintf(digger_log, "read_levf: levels load error 1\n");
#endif
    goto eout_0;
  }
  if (fread(leveldat, 1200, 1, levf) <= 0) {
#if defined(DIGGER_DEBUG)
    fprintf(digger_log, "read_levf: levels load error 2\n");
#endif
    goto eout_0;
  }
  fclose(levf);
  return (0);
eout_0:
  fclose(levf);
  return (-1);
}

static int
getarg(char argch, const char *allargs, bool *hasopt)
{
  char c;
  const char *cp;

  if (isalpha(argch)) {
    c = toupper(argch);
  } else {
    c = argch;
  }
  for (cp = allargs; *cp != '\0'; cp++) {
     if (c == *cp) {
       *hasopt = (*(cp + 1) == ':') ? true : false;
       return (c);
     }
  }
  return (-1);
}

void parsecmd(int argc,char *argv[])
{
  char *word;
  int argch;
  int16_t arg,i=0,j,speedmul;
  bool sf, gs, norepf, hasopt;

  gs = norepf = false;

#if defined(UNIX)
  digger_log = stderr;
#else
  digger_log = fopen("DIGGER.log", "w+");
#endif

  for (arg=1;arg<argc;arg++) {
    word=argv[arg];
    if (word[0]=='/' || word[0]=='-') {
#if defined(UNIX) && defined(_SDL)
      argch = getarg(word[1], "FOUH?QM2CKVL:R:P:S:E:G:X:I:", &hasopt);
#elif defined(_SDL)
      argch = getarg(word[1], "FOUH?QM2CKVL:R:P:S:E:G:I:", &hasopt);
#else
      argch = getarg(word[1], "OUH?QM2CKVL:R:P:S:E:G:I:", &hasopt);
#endif
      i = 2;
      if (argch != -1 && hasopt && word[2] == ':') {
        i = 3;
      }
      if (argch == 'L') {
        j=0;
        while (word[i]!=0)
          levfname[j++]=word[i++];
        levfname[j]=word[i];
        levfflag=true;
      }
#if defined(UNIX) && defined(_SDL)
      if (argch == 'X') {
        unsigned int x11_parent;

        x11_parent = strtol (&word[i], 0, 0);
        sdl_set_x11_parent(x11_parent);
      }
#endif

#if defined(_SDL)
      if (argch == 'F') {
        sdl_enable_fullscreen();
      }
#endif
      if (argch =='R')
        recname(word+i);
      if (argch =='P' || argch =='E') {
        maininit();
        openplay(word+i);
        if (escape)
          norepf=true;
      }
      if (argch == 'E') {
        finish();
        if (escape)
          exit(0);
        exit(1);
      }
      if (argch =='O' && !norepf) {
        arg=0;
        continue;
      }
      if (argch == 'S') {
        speedmul=0;
        while (word[i]!=0)
          speedmul=10*speedmul+word[i++]-'0';
        if (speedmul > 0) {
          ftime=speedmul*2000l;
        } else {
          ftime = 1;
        }
        gs=true;
      }
      if (argch == 'I')
        sscanf(word+i,"%hi",&startlev);
      if (argch == 'U')
        unlimlives=true;
      if (argch == '?' || argch == 'H' || argch == -1) {
        if (argch == -1) {
          fprintf(stderr, "Unknown option \"%c%c\"\n", word[0], word[1]);
        }
        finish();
        printf("DIGGER - Copyright (c) 1983 Windmill software\n"
               "Restored 1998 by AJ Software\n"
#ifdef ARM
               "Acorn port by Julian Brown\n"
#endif
               "http://www.digger.org\n"
               "https://github.com/sobomax/digger\n"
               "Version: "DIGGER_VERSION"\n\n"

               "Command line syntax:\n"
               "  DIGGER [[/S:]speed] [[/L:]level file] [/C] [/Q] [/M] "
                                                         "[/P:playback file]\n"
               "         [/E:playback file] [/R:record file] [/O] [/K[A]] "
                                                           "[/G[:time]] [/2]\n"
               "         [/V] [/U] [/I:level] "

#if defined(UNIX) && defined(_SDL)
                         "[/X:xid] "
#endif

#if defined(_SDL)
                         "[/F]"
#endif
                         "\n\n"
#ifndef UNIX
               "/C = Use CGA graphics\n"
#endif
               "/Q = Quiet mode (no sound at all)\n"
               "/M = No music\n"
               "/R = Record graphics to file\n"
               "/P = Playback and restart program\n"
               "/E = Playback and exit program\n"
               "/O = Loop to beginning of command line\n"
               "/K = Redefine keyboard\n"
               "/G = Gauntlet mode\n"
               "/2 = Two player simultaneous mode\n"
#ifndef UNIX
               "/V = Synchronize timing to vertical retrace\n"
#endif
#if defined(UNIX) && defined(_SDL)
               "/X = Embed in window\n"
#endif
#if defined(_SDL)
               "/F = Full-Screen\n"
#endif
               "/U = Allow unlimited lives\n"
               "/I = Start on a level other than 1\n");
        exit(1);
      }
      if (argch == 'Q')
        soundflag=false;
      if (argch == 'M')
        musicflag=false;
      if (argch == '2')
        diggers=2;
      if (argch == 'B' || argch == 'C') {
        ddap->ginit=cgainit;
        ddap->gpal=cgapal;
        ddap->ginten=cgainten;
        ddap->gclear=cgaclear;
        ddap->ggetpix=cgagetpix;
        ddap->gputi=cgaputi;
        ddap->ggeti=cgageti;
        ddap->gputim=cgaputim;
        ddap->gwrite=cgawrite;
        ddap->gtitle=cgatitle;
        ddap->ginit();
        ddap->gpal(0);
      }
      if (argch == 'K') {
        if (word[2]=='A' || word[2]=='a')
          redefkeyb(true);
        else
          redefkeyb(false);
      }
      if (argch == 'Q')
        quiet=true;
      if (argch == 'V')
        synchvid=true;
      if (argch == 'G') {
        gtime=0;
        while (word[i]!=0)
          gtime=10*gtime+word[i++]-'0';
        if (gtime>3599)
          gtime=3599;
        if (gtime==0)
          gtime=120;
        gauntlet=true;
      }
    }
    else {
      i=strlen(word);
      if (i<1)
        continue;
      sf=true;
      if (!gs)
        for (j=0;j<i;j++)
          if (word[j]<'0' || word[j]>'9') {
            sf=false;
            break;
          }
      if (sf) {
        speedmul=0;
        j=0;
        while (word[j]!=0)
          speedmul=10*speedmul+word[j++]-'0';
        gs=true;
        if (speedmul > 0) {
          ftime=speedmul*2000l;
        } else {
          ftime = 1;
        }
      }
      else {
        j=0;
        while (word[j]!=0) {
          levfname[j]=word[j];
          j++;
        }
        levfname[j]=word[j];
        levfflag=true;
      }
    }
  }

  if (levfflag) {
    if (read_levf(levfname) != 0) {
#if defined(DIGGER_DEBUG)
      fprintf(digger_log, "levels load error\n");
#endif
      levfflag = false;
    }
  }
}

int32_t randv;

int16_t randno(int16_t n)
{
  randv=randv*0x15a4e35l+1;
  return (int16_t)((randv&0x7fffffffl)%n);
}

char *keynames[17]={"Right","Up","Left","Down","Fire",
                    "Right","Up","Left","Down","Fire",
                    "Cheat","Accel","Brake","Music","Sound","Exit","Pause"};

int dx_sound_volume;
bool g_bWindowed,use_640x480_fullscreen,use_async_screen_updates;

void inir(void)
{
  char kbuf[80],vbuf[80];
  int i,j,p;
  bool cgaflag;

  for (i=0;i<17;i++) {
    sprintf(kbuf,"%s%c",keynames[i],(i>=5 && i<10) ? '2' : 0);
    sprintf(vbuf,"%i/%i/%i/%i/%i",keycodes[i][0],keycodes[i][1],
            keycodes[i][2],keycodes[i][3],keycodes[i][4]);
    GetINIString(INI_KEY_SETTINGS,kbuf,vbuf,vbuf,80,ININAME);
    krdf[i]=true;
    p=0;
    for (j=0;j<5;j++) {
      keycodes[i][j]=atoi(vbuf+p);
      while (vbuf[p]!='/' && vbuf[p]!=0)
        p++;
      if (vbuf[p]==0)
        break;
      p++;
    }
  }
  gtime=(int)GetINIInt(INI_GAME_SETTINGS,"GauntletTime",120,ININAME);
  if (ftime == 0) {
      ftime=GetINIInt(INI_GAME_SETTINGS,"Speed",80000l,ININAME);
  }
  sound_rate=(int)GetINIInt(INI_SOUND_SETTINGS,"Rate",22050,ININAME);
  sound_length=(int)GetINIInt(INI_SOUND_SETTINGS,"BufferSize",DEFAULT_BUFFER,ININAME);

#if !defined(UNIX) && !defined(_SDL)
  if (sound_device==1) {
#else
  if (!quiet) {
#endif
    volume=1;
    setupsound=s1setupsound;
    killsound=s1killsound;
    fillbuffer=s1fillbuffer;
    initint8=s1initint8;
    restoreint8=s1restoreint8;
    soundoff=s1soundoff;
    setspkrt2=s1setspkrt2;
    settimer0=s1settimer0;
    timer0=s1timer0;
    settimer2=s1settimer2;
    timer2=s1timer2;
    soundinitglob(sound_length,sound_rate);
  }
  dx_sound_volume=(int)GetINIInt(INI_SOUND_SETTINGS,"SoundVolume",0,ININAME);
  g_bWindowed=true;
  use_640x480_fullscreen=GetINIBool(INI_GRAPHICS_SETTINGS,"640x480",false,
                                    ININAME);
  use_async_screen_updates=GetINIBool(INI_GRAPHICS_SETTINGS,"Async",true,
                                      ININAME);
  synchvid=GetINIBool(INI_GRAPHICS_SETTINGS,"Synch",false,ININAME);
  cgaflag=GetINIBool(INI_GRAPHICS_SETTINGS,"CGA",false,ININAME);
  if (cgaflag) {
    ddap->ginit=cgainit;
    ddap->gpal=cgapal;
    ddap->ginten=cgainten;
    ddap->gclear=cgaclear;
    ddap->ggetpix=cgagetpix;
    ddap->gputi=cgaputi;
    ddap->ggeti=cgageti;
    ddap->gputim=cgaputim;
    ddap->gwrite=cgawrite;
    ddap->gtitle=cgatitle;
    ddap->ginit();
    ddap->gpal(0);
  }
  unlimlives=GetINIBool(INI_GAME_SETTINGS,"UnlimitedLives",false,ININAME);
  startlev=(int)GetINIInt(INI_GAME_SETTINGS,"StartLevel",1,ININAME);
}

void redefkeyb(bool allf)
{
  int i,j,k,l,z,y=0;
  bool f;
  char kbuf[80],vbuf[80];

  maininit();

  if (diggers==2) {
    outtext(ddap, "PLAYER 1:",0,y,3);
    y+=12;
  }

  outtext(ddap, "PRESS NEW KEY FOR",0,y,3);
  y+=12;

/* Step one: redefine keys that are always redefined. */

  for (i=0;i<5;i++) {
    outtext(ddap, keynames[i],0,y,2); /* Red first */
    findkey(i);
    outtext(ddap, keynames[i],0,y,1); /* Green once got */
    y+=12;
    for (j=0;j<i;j++) { /* Note: only check keys just pressed (I hate it when
                           this is done wrong, and it often is.) */
      if (keycodes[i][0]==keycodes[j][0] && keycodes[i][0]!=0) {
        i--;
        y-=12;
        break;
      }
      for (k=2;k<5;k++)
        for (l=2;l<5;l++)
          if (keycodes[i][k]==keycodes[j][l] && keycodes[i][k]!=-2) {
            j=i;
            k=5;
            i--;
            y-=12;
            break; /* Try again if this key already used */
          }
    }
  }

  if (diggers==2) {
    outtext(ddap, "PLAYER 2:",0,y,3);
    y+=12;
    for (i=5;i<10;i++) {
      outtext(ddap, keynames[i],0,y,2); /* Red first */
      findkey(i);
      outtext(ddap, keynames[i],0,y,1); /* Green once got */
      y+=12;
      for (j=0;j<i;j++) { /* Note: only check keys just pressed (I hate it when
                             this is done wrong, and it often is.) */
        if (keycodes[i][0]==keycodes[j][0] && keycodes[i][0]!=0) {
          i--;
          y-=12;
          break;
        }
        for (k=2;k<5;k++)
          for (l=2;l<5;l++)
            if (keycodes[i][k]==keycodes[j][l] && keycodes[i][k]!=-2) {
              j=i;
              k=5;
              i--;
              y-=12;
              break; /* Try again if this key already used */
            }
      }
    }
  }

/* Step two: redefine other keys which step one has caused to conflict */

  z=0;
  y-=12;
  for (i=10;i<17;i++) {
    f=false;
    for (j=0;j<10;j++)
      for (k=0;k<5;k++)
        for (l=2;l<5;l++)
          if (keycodes[i][k]==keycodes[j][l] && keycodes[i][k]!=-2)
            f=true;
    for (j=10;j<i;j++)
      for (k=0;k<5;k++)
        for (l=0;l<5;l++)
          if (keycodes[i][k]==keycodes[j][l] && keycodes[i][k]!=-2)
            f=true;
    if (f || (allf && i!=z)) {
      if (i!=z)
        y+=12;
      outtext(ddap, keynames[i],0,y,2); /* Red first */
      findkey(i);
      outtext(ddap, keynames[i],0,y,1); /* Green once got */
      z=i;
      i--;
    }
  }

/* Step three: save the INI file */

  for (i=0;i<17;i++)
    if (krdf[i]) {
      sprintf(kbuf,"%s%c",keynames[i],(i>=5 && i<10) ? '2' : 0);
      sprintf(vbuf,"%i/%i/%i/%i/%i",keycodes[i][0],keycodes[i][1],
              keycodes[i][2],keycodes[i][3],keycodes[i][4]);
      WriteINIString(INI_KEY_SETTINGS,kbuf,vbuf,ININAME);
    }
}
