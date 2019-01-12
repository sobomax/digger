/*
 * Copyright (c) 1983 Windmill Software
 * Copyright (c) 1989-2002 Andrew Jenner <aj@digger.org>
 * Copyright (c) 2002-2014 Maxim Sobolev <sobomax@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#if defined(DIGGER_DEBUG)
#include <stdio.h>
#endif
#include <string.h>
#include <stdlib.h>

#include "def.h"
#include "digger_types.h"
#include "drawing.h"
#include "monster_obj.h"
#include "sprite.h"

struct monster_obj_private
{
  uint16_t m_id;
  bool nobf;
  bool alive;
  bool zombie;
  struct obj_position pos;
  int16_t monspr;
  int16_t monspd;
};

struct monster_obj_full
{
  struct monster_obj pub;
  struct monster_obj_private priv;
};

static void monster_obj_updspr(struct monster_obj_private *);

static void
__drawmon(struct monster_obj_private *mop)
{
  int16_t sprid;

  sprid = FIRSTMONSTER + mop->m_id;

  mop->monspr += mop->monspd;
  if (mop->monspr == 2 || mop->monspr == 0)
    mop->monspd = -(mop->monspd);
  if (mop->monspr > 2)
    mop->monspr = 2;
  if (mop->monspr < 0)
    mop->monspr = 0;
  monster_obj_updspr(mop);
  drawspr(sprid, mop->pos.x, mop->pos.y);
}

static void
__drawmondie(struct monster_obj_private *mop)
{
  int16_t sprid;

  sprid = FIRSTMONSTER + mop->m_id;

  monster_obj_updspr(mop);
  drawspr(sprid, mop->pos.x, mop->pos.y);
}

static void
monster_obj_updspr(struct monster_obj_private *mop)
{
  int16_t sprid;

  sprid = FIRSTMONSTER + mop->m_id;

  if (mop->alive) {
    if (mop->nobf) {
      initspr(sprid, mop->monspr + 69, 4, 15, 0, 0);
    } else {
      switch (mop->pos.dir) {
      case DIR_RIGHT:
        initspr(sprid, mop->monspr + 73, 4, 15, 0, 0);
        break;
      case DIR_LEFT:
        initspr(sprid, mop->monspr + 77, 4, 15, 0, 0);
      }
    }
  } else if (mop->zombie) {
    if (mop->nobf) {
      initspr(sprid, 72, 4, 15, 0, 0);
    } else {
      switch(mop->pos.dir) {
      case DIR_RIGHT:
        initspr(sprid, 76, 4, 15, 0, 0);
        break;
      case DIR_LEFT:
        initspr(sprid, 80, 4, 14, 0, 0);
      }
    }
  }
}

static int
monster_obj_put(struct monster_obj *self)
{
  struct monster_obj_private *mop;

  mop = self->priv;

  monster_obj_updspr(mop);
  movedrawspr(FIRSTMONSTER + mop->m_id, mop->pos.x, mop->pos.y);
  return (0);
}

static int
monster_obj_mutate(struct monster_obj *self)
{
  struct monster_obj_private *mop;

  mop = self->priv;

  mop->nobf = (mop->nobf ? 0 : 1);
  monster_obj_updspr(mop);
  drawspr(FIRSTMONSTER + mop->m_id, mop->pos.x, mop->pos.y);
  return (0);
}

static int
monster_obj_damage(struct monster_obj *self)
{
  struct monster_obj_private *mop;

  mop = self->priv;

   if (!mop->alive) {
      /* We can only damage live thing or try to damage zombie */
      assert(mop->zombie);
   }
   mop->zombie = true;
   mop->alive = false;
   monster_obj_updspr(mop);
   drawspr(FIRSTMONSTER + mop->m_id, mop->pos.x, mop->pos.y);
   return (0);
}

static int
monster_obj_kill(struct monster_obj *self)
{
  struct monster_obj_private *mop;

  mop = self->priv;

  if (!mop->alive) {
      /* No, you can't kill me twice */
      assert(mop->zombie);
  }
  mop->alive = false;
  mop->zombie = false;
  erasespr(FIRSTMONSTER + mop->m_id);
  return (0);
}

static int
monster_obj_animate(struct monster_obj *self)
{
  struct monster_obj_private *mop;

  mop = self->priv;

  if (mop->alive) {
    __drawmon(mop);
  } else if (mop->zombie) {
    __drawmondie(mop);
  }
  return (0);
}

static int
monster_obj_getpos(struct monster_obj *self, struct obj_position *pos)
{

  memcpy(pos, &self->priv->pos, sizeof(struct obj_position));
  return (0);
}

static int
monster_obj_setpos(struct monster_obj *self, struct obj_position *pos)
{
#if defined(DIGGER_DEBUG)
  int dx, dy;
  const char *dsold, *dsnew;

  dx = self->priv->pos.x - pos->x;
  dy = self->priv->pos.y - pos->y;
  if (dx != 0 || dy != 0) {
    fprintf(stderr, "monster(%d): moved by %d,%d\n", self->priv->m_id, dx, dy);
  }
  if (self->priv->pos.dir != pos->dir) {
    DIR2STR(dsold, &self->priv->pos);
    DIR2STR(dsnew, pos);
    fprintf(stderr, "monster(%d): changed direction from %s to %s\n",
     self->priv->m_id, dsold, dsnew);
  }
#endif
  memcpy(&self->priv->pos, pos, sizeof(struct obj_position));
  return (0);
}

static bool
monster_obj_isalive(struct monster_obj *self)
{

  return (self->priv->alive);
}

static bool
monster_obj_isnobbin(struct monster_obj *self)
{

  return (self->priv->nobf);
}

int
monster_obj_dtor(struct monster_obj *self)
{

  free(self);
  return (0);
}

struct monster_obj *
monster_obj_ctor(uint16_t m_id, bool nobf, int16_t dir, int16_t x, int16_t y)
{
  struct monster_obj_full *mofp;
  struct monster_obj_private *mp;
  struct monster_obj *mpub;

  mofp = (struct monster_obj_full *)malloc(sizeof(struct monster_obj_full));
  if (mofp == NULL) {
    return (NULL);
  }
  memset(mofp, '\0', sizeof(struct monster_obj_full));
  mp = &(mofp->priv);
  mpub = &(mofp->pub);
  mp->nobf = nobf;
  mp->pos.dir = dir;
  mp->pos.x = x;
  mp->pos.y = y;
  mp->m_id = m_id;
  mp->alive = true;
  mp->zombie = false;
  mp->monspr = 0;
  mp->monspd = 1;

  mpub->put = &monster_obj_put;
  mpub->animate = &monster_obj_animate;
  mpub->mutate = &monster_obj_mutate;
  mpub->kill = &monster_obj_kill;
  mpub->damage = &monster_obj_damage;
  mpub->getpos = &monster_obj_getpos;
  mpub->setpos = &monster_obj_setpos;
  mpub->isalive = &monster_obj_isalive;
  mpub->isnobbin = &monster_obj_isnobbin;
  mpub->dtor = &monster_obj_dtor;
  mpub->priv = mp;
  assert((void *)mpub == (void *)mofp);
  return (mpub);
  
}
