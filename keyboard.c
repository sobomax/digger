/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "draw_api.h"
#include "drawing.h"
#include "hardware.h"
#include "input.h"
#include "ini.h"
#include "keyboard.h"
#include "main.h"

const char *keynames[NKEYS]={"Right","Up","Left","Down","Fire",
                    "Right","Up","Left","Down","Fire",
                    "Cheat","Accel","Brake","Music","Sound","Exit","Pause",
                    "Mode Change","Save DRF"};

#define FINDKEY_EX(i) {if (prockey(i) == -1) return;}

static int prockey(int kn)
{
  int16_t key;

  key = getkey(true);
  if (kn != DKEY_EXT && key == keycodes[DKEY_EXT][0])
    return -1;
  keycodes[kn][0] = key;
  return (0);
}

void redefkeyb(struct digger_draw_api *ddap, bool allf)
{
  int i,j,k,l,keyrow,errorrow1,errorrow2,playerrow,color;
  char kbuf[80],vbuf[80];

  maininit();

  outtextcentered(ddap, "D I G G E R",2,3);
  outtextcentered(ddap, "REDEFINE KEYBOARD",3*CHR_H,1);

  playerrow=5*CHR_H;
  keyrow=8*CHR_H;
  errorrow1=11*CHR_H;
  errorrow2=13*CHR_H;
  color=3;

  for (i=0;i<NKEYS;i++) {
    eraseline(ddap, playerrow);
    eraseline(ddap, keyrow);

    if (i < 5)
      outtextcentered(ddap, "PLAYER 1",playerrow,2);
    else if (i < 10)
      outtextcentered(ddap, "PLAYER 2",playerrow,2);
    else
      outtextcentered(ddap, "MISELLANEOUS",playerrow,2);

    outtextcentered(ddap, keynames[i],keyrow,color);

    FINDKEY_EX(i);

    eraseline(ddap, errorrow1);
    eraseline(ddap, errorrow2);
    color=3;

    for (j=0;j<i;j++) { /* Note: only check keys just pressed (I hate it when
                           this is done wrong, and it often is.) */
      if (keycodes[i][0]==keycodes[j][0] && keycodes[i][0]!=0) {
        i--;
        color=2;
        outtextcentered(ddap, "THIS KEY IS ALREADY USED",errorrow1,2);
        outtextcentered(ddap, "CHOOSE ANOTHER KEY",errorrow2,2);
        break;
      }
      for (k=2;k<5;k++)
        for (l=2;l<5;l++)
          if (keycodes[i][k]==keycodes[j][l] && keycodes[i][k]!=-2) {
            j=i;
            k=5;
            i--;
            color=2;
            outtextcentered(ddap, "THIS KEY IS ALREADY USED",errorrow1,2);
            outtextcentered(ddap, "CHOOSE ANOTHER KEY",errorrow2,2);
            break; /* Try again if this key already used */
          }
    }
  }
  for (i=0;i<NKEYS;i++)
    if (krdf[i]) {
      sprintf(kbuf,"%s%c",keynames[i],(i>=5 && i<10) ? '2' : 0);
      sprintf(vbuf,"%i/%i/%i/%i/%i",keycodes[i][0],keycodes[i][1],
              keycodes[i][2],keycodes[i][3],keycodes[i][4]);
      WriteINIString(INI_KEY_SETTINGS,kbuf,vbuf,ININAME);
    }
}
