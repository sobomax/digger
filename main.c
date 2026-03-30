/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

static const char copyright[]="Portions Copyright(c) 1983 Windmill Software Inc.";

#include <ctype.h>
#include <errno.h>
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
#include "keyboard.h"
#include "monster.h"
#include "bags.h"
#include "record.h"
#include "main.h"
#include "newsnd.h"
#include "ini.h"
#include "draw_api.h"
#include "game.h"
#include "digger_log.h"
#include "netsim.h"
#include "netsim_debug.h"
#include "title_anim.h"

static struct game
{
  int16_t level;
  bool levdone;
} gamedat[2];

static bool levnotdrawn=false,alldead=false;
bool started;
static int16_t penalty=0;

FILE *digger_log = NULL;

static void shownplayers(void);
static void switchnplayers(void);
static void drawscreen(struct digger_draw_api *);
static void initchars(void);
static void checklevdone(void);
static int16_t levno(void);
static void calibrate(void);
static void parsecmd(int argc,char *argv[]);
static void initlevel(void);
static void inir(void);
static int getalllives(void);
static void run_pause(bool allow_local_pause, int remote_player);
static void sync_netsim_waiter(void);

int16_t getlevch(int16_t x,int16_t y,int16_t l)
{
  if ((l==3 || l==4) && !dgstate.levfflag && dgstate.diggers==2 && y==9 && (x==6 || x==8))
    return 'H';
  return dgstate.leveldat[l-1][y][x];
}

#ifdef INTDRF
extern FILE *info;
#endif

extern struct digger_draw_api *ddap;

static void
game_dbg_info_emit(void)
{

  printf("score=%d level=%d frames=%u\n", gettscore(0), levno(),
   (unsigned int)getframe());
}

void game(void)
{
  int16_t t,c,i;
  bool flashplayer=false;

  resetframe();
  if (dgstate.gauntlet) {
    dgstate.cgtime=dgstate.gtime*1193181l;
    dgstate.timeout=false;
  }
  initlives();
  gamedat[0].level=dgstate.startlev;
  if (dgstate.nplayers==2)
    gamedat[1].level=dgstate.startlev;
  alldead=false;
  ddap->gclear();
  dgstate.curplayer=0;
  initlevel();
  dgstate.curplayer=1;
  initlevel();
  zeroscores();
  bonusvisible=true;
  if (dgstate.nplayers==2)
    flashplayer=true;
  dgstate.curplayer=0;
  while (getalllives()!=0 && !escape && !dgstate.timeout) {
    while (!alldead && !escape && !dgstate.timeout) {
      initmbspr();

      if (playing)
        dgstate.randv=playgetrand();
      else
        dgstate.randv=0;
#ifdef INTDRF
      fprintf(info,"%lu\n",dgstate.randv);
      frame=0;
#endif
      recputrand(dgstate.randv);
      if (levnotdrawn) {
        levnotdrawn=false;
        drawscreen(ddap);
        if (flashplayer) {
          flashplayer=false;
          strcpy(dgstate.pldispbuf,"PLAYER ");
          if (dgstate.curplayer==0)
            strcat(dgstate.pldispbuf,"1");
          else
            strcat(dgstate.pldispbuf,"2");
          cleartopline();
          for (t=0;t<15;t++)
            for (c=1;c<=3;c++) {
              outtext(ddap, dgstate.pldispbuf,108,0,c);
              writecurscore(ddap, c);
              newframe();
              if (escape)
                return;
            }
          drawscores(ddap);
          for (i=0;i<dgstate.diggers;i++)
            addscore(ddap, i,0);
        }
      }
      else
        initchars();
      erasetext(ddap, 8, 108,0,3);
      initscores(ddap);
      drawlives(ddap);
      music(MUSIC_MAIN, 1.0);

      flushkeybuf();
      input_reset_directions();
      while (!alldead && !gamedat[dgstate.curplayer].levdone && !escape && !dgstate.timeout) {
        penalty=0;
        newframe();
        if (escape || dgstate.timeout)
          break;
        netsim_trace_state("pre", gamedat[dgstate.curplayer].levdone, alldead,
          penalty);
        dodigger(ddap);
        netsim_trace_state("post_digger",
          gamedat[dgstate.curplayer].levdone, alldead, penalty);
        domonsters(ddap);
        netsim_trace_state("post_monsters",
          gamedat[dgstate.curplayer].levdone, alldead, penalty);
        dobags(ddap);
        netsim_trace_state("post_bags",
          gamedat[dgstate.curplayer].levdone, alldead, penalty);
        if (penalty>8)
          incmont(penalty-8);
        checklevdone();
        netsim_trace_state("post_tick",
          gamedat[dgstate.curplayer].levdone, alldead, penalty);
        testpause();
        if (escape || dgstate.timeout)
          break;
      }
      erasediggers();
      musicoff();
      t=20;
      while ((getnmovingbags()!=0 || t!=0) && !escape && !dgstate.timeout) {
        if (t!=0)
          t--;
        penalty=0;
        newframe();
        if (escape || dgstate.timeout)
          break;
        netsim_trace_state("cleanup_pre",
          gamedat[dgstate.curplayer].levdone, alldead, penalty);
        dobags(ddap);
        netsim_trace_state("cleanup_post_bags",
          gamedat[dgstate.curplayer].levdone, alldead, penalty);
        dodigger(ddap);
        netsim_trace_state("cleanup_post_digger",
          gamedat[dgstate.curplayer].levdone, alldead, penalty);
        domonsters(ddap);
        netsim_trace_state("cleanup_post_monsters",
          gamedat[dgstate.curplayer].levdone, alldead, penalty);
        if (penalty<8)
          t=0;
        netsim_trace_state("cleanup_post_tick",
          gamedat[dgstate.curplayer].levdone, alldead, penalty);
      }
      soundstop();
      for (i=0;i<dgstate.diggers;i++)
        killfire(i);
      erasebonus(ddap);
      cleanupbags();
      savefield();
      erasemonsters();
      ddap->gflush();
      recputeol();
      if (playing)
        playskipeol();
      if (escape)
        recputeog();
      if (gamedat[dgstate.curplayer].levdone) {
        soundlevdone();
        if (getenv("DIGGER_CI_RUN_DTL") != NULL) {
          game_dbg_info_emit();
        }
      }
      if (countem()==0 || gamedat[dgstate.curplayer].levdone) {
#ifdef INTDRF
        fprintf(info,"%i\n",frame);
#endif
        for (i=dgstate.curplayer;i<dgstate.diggers+dgstate.curplayer;i++)
          if (getlives(i)>0 && !digalive(i))
            declife(i);
        drawlives(ddap);
        gamedat[dgstate.curplayer].level++;
        if (gamedat[dgstate.curplayer].level>1000)
          gamedat[dgstate.curplayer].level=1000;
        initlevel();
      }
      else
        if (alldead) {
#ifdef INTDRF
          fprintf(info,"%i\n",frame);
#endif
          for (i=dgstate.curplayer;i<dgstate.curplayer+dgstate.diggers;i++)
            if (getlives(i)>0)
              declife(i);
          drawlives(ddap);
        }
      if ((alldead && getalllives()==0 && !dgstate.gauntlet && !escape) || dgstate.timeout)
        endofgame(ddap);
    }
    alldead=false;
    if (dgstate.nplayers==2 && getlives(1-dgstate.curplayer)!=0) {
      dgstate.curplayer=1-dgstate.curplayer;
      flashplayer=levnotdrawn=true;
    }
  }
#ifdef INTDRF
  fprintf(info,"-1\n%lu\n%i",getscore0(),gamedat[0].level);
#endif
}

static bool quiet=false;
static uint16_t sound_rate,sound_length;

#if defined(_SDL)
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
  initsound();
  recstart();
  maininited = 1;
}

int main(int argc,char *argv[])
{
  int rval;

  inir();
  parsecmd(argc,argv);
  maininit();
  rval = mainprog();
  if (digger_log != NULL) {
    fflush(digger_log);
    fclose(digger_log);
  }
  return rval;
}

int mainprog(void)
{
  int16_t frame,t;
  struct title_anim title_anim;
  bool started_by_remote;
  loadscores();
  escape=false;
  title_anim_init(&title_anim);
  do {
    soundstop();
    title_anim_cleanup(&title_anim);
    creatembspr();
    detectjoy();
    ddap->gclear();
    ddap->gtitle();
    outtext(ddap, "D I G G E R",100,0,3);
    shownplayers();
    sync_netsim_waiter();
    showtable(ddap);
    started=false;
    started_by_remote=false;
    frame=0;
    newframe();
    teststart();
    while (!started) {
      started=teststart();
      if (!started && dgstate.netsim && netsim_remote_start_requested()) {
        started=true;
        started_by_remote=true;
      }
      if (mode_change) {
        switchnplayers();
        shownplayers();
        sync_netsim_waiter();
        mode_change=false;
      }
      title_anim_step(&title_anim, ddap, frame);
      newframe();
      frame++;
      if (frame>title_anim_last_frame())
        frame=0;
    }
    title_anim_cleanup(&title_anim);
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
    input_reset_network();
    if (dgstate.netsim) {
      cleartopline();
      outtext(ddap, "WAITING FOR PEER",68,0,3);
      ddap->gflush();
      if (!netsim_start_session(!started_by_remote)) {
        soundstop();
        soundddie();
        for (t=0;t<15 && !escape;t++)
          newframe();
        continue;
      }
      input_enable_network_mode();
      input_set_network_controls(1-netsim_local_player(), 0);
    }
    recinit();
    soundwakeup();
    game();
    if (dgstate.netsim)
      netsim_stop_session(escape && !netsim_peer_exited());
    input_reset_network();
    gotgame=true;
    if (gotname) {
      recsavedrf();
      gotgame=false;
    }
    savedrf=false;
    escape=false;
  } while (!escape);
  title_anim_cleanup(&title_anim);
  netsim_shutdown();
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

struct label {
  const char *text;
  int xpos;
};

static const struct game_mode {
  bool gauntlet;
  bool netsim;
  int nplayers;
  int diggers;
  bool last;
  const struct label title[2];
} possible_modes[] = {
  {false, false, 1, 1, false, {{"ONE", 220}, {" PLAYER ", 192}}},
  {false, false, 2, 1, false, {{"TWO", 220}, {" PLAYERS", 184}}},
  {false, false, 2, 2, false, {{"TWO PLAYER", 180}, {"SIMULTANEOUS", 170}}},
  {false, true,  1, 2, false, {{"TWO-PLAYER", 180}, {"NETSIM", 206}}},
  {true,  false, 1, 1, false, {{"GAUNTLET", 192}, {"MODE", 216}}},
  {true,  false, 1, 2, true,  {{"TWO PLAYER", 180}, {"GAUNTLET", 192}}}
};

static bool
mode_available(const struct game_mode *gmp)
{

  if (gmp->netsim && !netsim_configured())
    return (false);
  return (true);
}

static int getnmode(void)
{
  int i;

  for (i = 0; !possible_modes[i].last;i++) {
    if (!mode_available(&possible_modes[i]))
      continue;
    if (possible_modes[i].gauntlet != dgstate.gauntlet)
      continue;
    if (possible_modes[i].netsim != dgstate.netsim)
      continue;
    if (possible_modes[i].nplayers != dgstate.nplayers)
      continue;
    if (possible_modes[i].diggers != dgstate.diggers)
      continue;
    break;
  }
  return i;
}

static void shownplayers(void)
{
  const struct game_mode *gmp;

  erasetext(ddap, 10, 180, 25, 3);
  erasetext(ddap, 12, 170, 39, 3);
  gmp = &possible_modes[getnmode()];
  outtext(ddap, gmp->title[0].text, gmp->title[0].xpos, 25, 3);
  outtext(ddap, gmp->title[1].text, gmp->title[1].xpos, 39, 3);
}

static int getalllives(void)
{
  int t=0,i;
  for (i=dgstate.curplayer;i<dgstate.diggers+dgstate.curplayer;i++)
    t+=getlives(i);
  return t;
}

static void switchnplayers(void)
{
  int i, j;

  i = getnmode();
  j = i;
  do {
    j = possible_modes[j].last ? 0 : j + 1;
  } while (!mode_available(&possible_modes[j]));
  dgstate.gauntlet = possible_modes[j].gauntlet;
  dgstate.netsim = possible_modes[j].netsim;
  dgstate.nplayers = possible_modes[j].nplayers;
  dgstate.diggers = possible_modes[j].diggers;
}

static void
sync_netsim_waiter(void)
{

  if (dgstate.netsim) {
    if (!netsim_begin_wait()) {
      escape=true;
    }
    return;
  }
  netsim_stop_session(false);
}

static void initlevel(void)
{
  gamedat[dgstate.curplayer].levdone=false;
  makefield();
  makeemfield();
  initbags();
  levnotdrawn=true;
}

static void drawscreen(struct digger_draw_api *ddap)
{
  ddap->gclear();
  creatembspr();
  drawstatics(ddap);
  drawbags();
  drawemeralds();
  initdigger();
  initmonsters();
}

static void initchars(void)
{
  initmbspr();
  initdigger();
  initmonsters();
}

static void checklevdone(void)
{
  if ((countem()==0 || monleft()==0) && isalive())
    gamedat[dgstate.curplayer].levdone=true;
  else
    gamedat[dgstate.curplayer].levdone=false;
}

void incpenalty(void)
{
  penalty++;
}

void cleartopline(void)
{
  erasetext(ddap, 26, 0,0,3);
  erasetext(ddap, 1, 308,0,3);
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
  if (gamedat[dgstate.curplayer].level>10)
    return 10;
  return gamedat[dgstate.curplayer].level;
}

static int16_t levno(void)
{
  return gamedat[dgstate.curplayer].level;
}

void setdead(bool df)
{
  alldead=df;
}

void testpause(void)
{
  int remote_player;
  bool allow_local_pause;

  remote_player = dgstate.netsim ? (1 - netsim_local_player()) : 0;
  allow_local_pause = !dgstate.netsim || getlives(netsim_local_player()) > 0;
  if (!allow_local_pause)
    pausef=false;
  if (pausef || (dgstate.netsim && netsim_remote_pause_active()))
    run_pause(allow_local_pause, remote_player);
  else
    soundpauseoff();
}

static void
run_pause(bool allow_local_pause, int remote_player)
{
  int i;
  bool local_pause, remote_pause;
  bool peer_joined;
  bool sent_unpause;

  soundpause();
  cleartopline();
  if (dgstate.netsim && !allow_local_pause)
    outtext(ddap, remote_player == 0 ? "PLAYER 1 AWAY" : "PLAYER 2 AWAY",
      80,0,1);
  else
    outtext(ddap, "PRESS ANY KEY",80,0,1);
  ddap->gflush();
  if (!dgstate.netsim)
    getkey(true);
  else {
    local_pause=allow_local_pause;
    remote_pause=netsim_remote_pause_active();
    peer_joined=remote_pause;
    sent_unpause=false;
    pausef=false;
    (void)input_consume_anykey();
    do {
      if (!pauseframe(local_pause, &remote_pause))
        break;
      if (!local_pause)
        sent_unpause=true;
      if (remote_pause)
        peer_joined=true;
      if (input_consume_anykey()) {
        local_pause=false;
      } else if (peer_joined && !remote_pause)
        local_pause=false;
    } while ((local_pause || remote_pause || !sent_unpause) && !escape);
  }
  cleartopline();
  drawscores(ddap);
  for (i=0;i<dgstate.diggers;i++)
    addscore(ddap, i,0);
  drawlives(ddap);
  gethrt(true, 1);
  pausef=false;
  soundpauseoff();
}

static void calibrate(void)
{
  volume=(int16_t)(getkips()/291);
  if (volume==0)
    volume=1;
}

#define read_levf_fail(s, p) \
  digger_log_printf("read_levf: %s: levels file %s error%s: %s\n", \
    levfname, (s), (p), strerror(errno))

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
      read_levf_fail("open", "");
#endif
      return (-1);
  }
  if (fread(&bonusscore, 2, 1, levf) < 1) {
#if defined(DIGGER_DEBUG)
    read_levf_fail("load", " #1");
#endif
    goto eout_0;
  }
  if (fread(dgstate.leveldat, 1200, 1, levf) <= 0) {
#if defined(DIGGER_DEBUG)
    read_levf_fail("load", " #2");
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

#define BASE_OPTS "OUH?QM2CKVL:R:P:S:E:G:I:N:"
#if defined(HAVE_SDL_X11_WINDOW)
#define X11_OPTS "X:"
#else
#define X11_OPTS ""
#endif
#define SDL_OPTS  "F"

static void parsecmd(int argc,char *argv[])
{
  char *word;
  int argch;
  int16_t arg,i=0,j,speedmul;
  bool sf, gs, norepf, hasopt;

  gs = norepf = false;

  for (arg=1;arg<argc;arg++) {
    word=argv[arg];
    if (word[0]=='/' || word[0]=='-') {
#if defined(UNIX) && defined(_SDL)
      argch = getarg(word[1], (BASE_OPTS X11_OPTS SDL_OPTS), &hasopt);
#else
# if defined(_SDL)
      argch = getarg(word[1], (BASE_OPTS SDL_OPTS), &hasopt);
# else
      argch = getarg(word[1], BASE_OPTS, &hasopt);
# endif
#endif
      i = 2;
      if (argch != -1 && hasopt && word[2] == ':') {
        i = 3;
      }
      if (argch == 'L') {
        j=0;
        while (word[i]!=0)
          dgstate.levfname[j++]=word[i++];
        dgstate.levfname[j]=word[i];
        dgstate.levfflag=true;
      }
#if defined(HAVE_SDL_X11_WINDOW)
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
	if (getenv("DIGGER_CI_RUN") != NULL) {
          game_dbg_info_emit();
	  exit(0);
	}
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
          dgstate.ftime=speedmul*2000l;
        } else {
          dgstate.ftime = 1;
        }
        gs=true;
      }
      if (argch == 'I')
        sscanf(word+i,"%hi",&dgstate.startlev);
      if (argch == 'N') {
        if (!netsim_configure(word+i)) {
          fprintf(stderr, "Invalid /N argument \"%s\"\n", word+i);
          exit(1);
        }
        dgstate.gauntlet=false;
        dgstate.netsim=true;
        dgstate.nplayers=1;
        dgstate.diggers=2;
      }
      if (argch == 'U')
        dgstate.unlimlives=true;
      if (argch == '?' || argch == 'H' || argch == -1) {
        if (argch == -1) {
          fprintf(stderr, "Unknown option \"%c%c\"\n", word[0], word[1]);
        }
        finish();
        printf("DIGGER - %s\n"
               "Restored 1998 by AJ Software\n"
               "http://www.digger.org\n"
               "https://github.com/sobomax/digger\n"
               "Version: "DIGGER_VERSION"\n\n"

               "Command line syntax:\n"
               "  DIGGER [[/S:]speed] [[/L:]level file] [/C] [/Q] [/M] "
                                                         "[/P:playback file]\n"
               "         [/E:playback file] [/R:record file] [/O] [/K[A]] "
                                                           "[/G[:time]] [/2]\n"
               "         [/U] [/I:level] "
               "[/N:[[localip:]localport-]host:port] "

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
               "/Q = Quiet mode (no sound at all)       "
               "/M = No music\n"
               "/R = Record graphics to file\n"
               "/P = Playback and restart program       "
               "/E = Playback and exit program\n"
               "/O = Loop to beginning of command line\n"
               "/K = Redefine keyboard\n"
               "/G = Gauntlet mode\n"
               "/2 = Two player simultaneous mode\n"
               "/N = Enable two-player UDP NetSim mode\n"
#if defined(UNIX) && defined(_SDL)
               "/X = Embed in window\n"
#endif
#if defined(_SDL)
               "/F = Full-Screen\n"
#endif
               "/U = Allow unlimited lives\n"
               "/I = Start on a level other than 1\n", copyright);
        exit(1);
      }
      if (argch == 'Q')
        soundflag=false;
      if (argch == 'M')
        musicflag=false;
      if (argch == '2') {
        dgstate.diggers=2;
        dgstate.netsim=false;
      }
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
          redefkeyb(ddap, true);
        else
          redefkeyb(ddap, false);
      }
      if (argch == 'Q')
        quiet=true;
      if (argch == 'G') {
        dgstate.gtime=0;
        while (word[i]!=0)
          dgstate.gtime=10*dgstate.gtime+word[i++]-'0';
        if (dgstate.gtime>3599)
          dgstate.gtime=3599;
        if (dgstate.gtime==0)
          dgstate.gtime=120;
        dgstate.gauntlet=true;
        dgstate.netsim=false;
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
          dgstate.ftime=speedmul*2000l;
        } else {
          dgstate.ftime = 1;
        }
      }
      else {
        j=0;
        while (word[j]!=0) {
          dgstate.levfname[j]=word[j];
          j++;
        }
        dgstate.levfname[j]=word[j];
        dgstate.levfflag=true;
      }
    }
  }

  if (dgstate.levfflag) {
    if (read_levf(dgstate.levfname) != 0) {
#if defined(DIGGER_DEBUG)
      digger_log_printf("levels load error\n");
      exit(1);
#endif
      dgstate.levfflag = false;
    }
  }
}

int16_t randno(int16_t n)
{
  dgstate.randv=dgstate.randv*0x15a4e35l+1;
  return (int16_t)((dgstate.randv&0x7fffffffl)%n);
}

int dx_sound_volume;
bool g_bWindowed,use_640x480_fullscreen,use_async_screen_updates;

static void inir(void)
{
  char kbuf[80],vbuf[80];
  int i,j,p;
  bool cgaflag;

#if defined(UNIX) || defined(DIGGER_DEBUG)
  digger_log = stderr;
#else
  digger_log = fopen("DIGGER.log", "w+");
#endif

  for (i=0;i<NKEYS;i++) {
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
  dgstate.gtime=(int)GetINIInt(INI_GAME_SETTINGS,"GauntletTime",120,ININAME);
  if (dgstate.ftime == 0) {
      dgstate.ftime=GetINIIntDoc(INI_GAME_SETTINGS,"Speed",80000l,ININAME,
        "number of microseconds in one frame, eq of 12.5Hz");
  }
  dgstate.gauntlet=GetINIBool(INI_GAME_SETTINGS,"GauntletMode",false,ININAME);
  GetINIString(INI_GAME_SETTINGS,"Players","1",vbuf,80,ININAME);
  strupr(vbuf);
  if (vbuf[0]=='2' && vbuf[1]=='S') {
    dgstate.diggers=2;
    dgstate.nplayers=1;
    dgstate.netsim=false;
  }
  else {
    dgstate.diggers=1;
    dgstate.nplayers=atoi(vbuf);
    dgstate.netsim=false;
    if (dgstate.nplayers<1 || dgstate.nplayers>2)
      dgstate.nplayers=1;
  }
  soundflag=GetINIBool(INI_SOUND_SETTINGS,"SoundOn",true,ININAME);
  musicflag=GetINIBool(INI_SOUND_SETTINGS,"MusicOn",true,ININAME);
  sound_rate=(int)GetINIInt(INI_SOUND_SETTINGS,"Rate",44100,ININAME);
  sound_length=(int)GetINIInt(INI_SOUND_SETTINGS,"BufferSize",DEFAULT_BUFFER,ININAME);
  soundpreinit();

#if !defined(UNIX) && !defined(_SDL)
  if (sound_device==1) {
#else
  if (!quiet) {
#endif
    volume=1;
    setupsound=s1setupsound;
    killsound=s1killsound;
    soundoff=s1soundoff;
    setspkrt2=s1setspkrt2;
    timer0=s1timer0;
    timer2=s1timer2;
    soundinitglob(sound_length,sound_rate);
  }
  dx_sound_volume=(int)GetINIInt(INI_SOUND_SETTINGS,"SoundVolume",0,ININAME);
  g_bWindowed=true;
  use_640x480_fullscreen=GetINIBool(INI_GRAPHICS_SETTINGS,"640x480",false,
                                    ININAME);
  use_async_screen_updates=GetINIBool(INI_GRAPHICS_SETTINGS,"Async",true,
                                      ININAME);
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
  dgstate.unlimlives=GetINIBool(INI_GAME_SETTINGS,"UnlimitedLives",false,ININAME);
  dgstate.startlev=(int)GetINIInt(INI_GAME_SETTINGS,"StartLevel",1,ININAME);
}
