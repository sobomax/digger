/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "draw_api.h"
#include "drawing.h"
#include "input.h"
#include "ini.h"
#include "keyboard.h"
#include "main.h"

const char *keynames[NKEYS]={"Right","Up","Left","Down","Fire",
                    "Right","Up","Left","Down","Fire",
                    "Cheat","Accel","Brake","Music","Sound","Exit","Pause"};

void redefkeyb(struct digger_draw_api *ddap, bool allf)
{
  int i,j,k,l,z,y=0,x;
  bool f;
  char kbuf[80],vbuf[80];

  maininit();

  if (diggers==2) {
    outtext(ddap, "PLAYER 1:",0,y,3);
    y+=CHR_H;
  }

  outtext(ddap, "PRESS NEW KEY FOR",0,y,3);
  y+=CHR_H;

/* Step one: redefine keys that are always redefined. */

  for (i=0;i<5;i++) {
    outtext(ddap, keynames[i],0,y,2); /* Red first */
    findkey(i);
    outtext(ddap, keynames[i],0,y,1); /* Green once got */
    y+=CHR_H;
    for (j=0;j<i;j++) { /* Note: only check keys just pressed (I hate it when
                           this is done wrong, and it often is.) */
      if (keycodes[i][0]==keycodes[j][0] && keycodes[i][0]!=0) {
        i--;
        y-=CHR_H;
        break;
      }
      for (k=2;k<5;k++)
        for (l=2;l<5;l++)
          if (keycodes[i][k]==keycodes[j][l] && keycodes[i][k]!=-2) {
            j=i;
            k=5;
            i--;
            y-=CHR_H;
            break; /* Try again if this key already used */
          }
    }
  }

  if (diggers==2) {
    outtext(ddap, "PLAYER 2:",0,y,3);
    y+=CHR_H;
    for (i=5;i<10;i++) {
      outtext(ddap, keynames[i],0,y,2); /* Red first */
      findkey(i);
      outtext(ddap, keynames[i],0,y,1); /* Green once got */
      y+=CHR_H;
      for (j=0;j<i;j++) { /* Note: only check keys just pressed (I hate it when
                             this is done wrong, and it often is.) */
        if (keycodes[i][0]==keycodes[j][0] && keycodes[i][0]!=0) {
          i--;
          y-=CHR_H;
          break;
        }
        for (k=2;k<5;k++)
          for (l=2;l<5;l++)
            if (keycodes[i][k]==keycodes[j][l] && keycodes[i][k]!=-2) {
              j=i;
              k=5;
              i--;
              y-=CHR_H;
              break; /* Try again if this key already used */
            }
      }
    }
  }

/* Step two: redefine other keys which step one has caused to conflict */

  z=0;
  x=0;
  y-=CHR_H;
  for (i=10;i<NKEYS;i++) {
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
        y+=CHR_H;
      if (y >= MAX_H - CHR_H) {
        y = 0;
        x = (MAX_TEXT_LEN / 2) * CHR_W;
      }
      outtext(ddap, keynames[i],x,y,2); /* Red first */
      findkey(i);
      outtext(ddap, keynames[i],x,y,1); /* Green once got */
      z=i;
      i--;
    }
  }

/* Step three: save the INI file */

  for (i=0;i<NKEYS;i++)
    if (krdf[i]) {
      sprintf(kbuf,"%s%c",keynames[i],(i>=5 && i<10) ? '2' : 0);
      sprintf(vbuf,"%i/%i/%i/%i/%i",keycodes[i][0],keycodes[i][1],
              keycodes[i][2],keycodes[i][3],keycodes[i][4]);
      WriteINIString(INI_KEY_SETTINGS,kbuf,vbuf,ININAME);
    }
}
