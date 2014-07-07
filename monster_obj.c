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
#include <string.h>

#include "def.h"
#include "drawing.h"
#include "monster_obj.h"
#include "sprite.h"

static void monster_obj_updspr(struct monster_obj *self);

static void
__drawmon(struct monster_obj *mop)
{
  int16_t sprid;

  sprid = FIRSTMONSTER + mop->m_id;

  mop->monspr += mop->monspd;
  if (mop->monspr == 2 || mop->monspr == 0)
    mop->monspd =- mop->monspd;
  if (mop->monspr > 2)
    mop->monspr = 2;
  if (mop->monspr < 0)
    mop->monspr = 0;
  monster_obj_updspr(mop);
  drawspr(sprid, mop->x, mop->y);
}

static void
__drawmondie(struct monster_obj *mop)
{
  int16_t sprid;

  sprid = FIRSTMONSTER + mop->m_id;

  monster_obj_updspr(mop);
  drawspr(sprid, mop->x, mop->y);
}

static void
monster_obj_updspr(struct monster_obj *self)
{
  int16_t sprid;

  sprid = FIRSTMONSTER + self->m_id;

  if (self->alive) {
    if (self->nobf) {
      initspr(sprid, self->monspr + 69, 4, 15, 0, 0);
    } else {
      switch (self->dir) {
      case DIR_RIGHT:
        initspr(sprid, self->monspr + 73, 4, 15, 0, 0);
        break;
      case DIR_LEFT:
        initspr(sprid, self->monspr + 77, 4, 15, 0, 0);
      }
    }
  } else if (self->zombie) {
    if (self->nobf) {
      initspr(sprid, 72, 4, 15, 0, 0);
    } else {
      switch(self->dir) {
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
monster_obj_pop(struct monster_obj *self)
{

  monster_obj_updspr(self);
  movedrawspr(FIRSTMONSTER + self->m_id, self->x, self->y);
  return (0);
}

static int
monster_obj_mutate(struct monster_obj *self)
{

  self->nobf = (self->nobf ? 0 : 1);
  monster_obj_updspr(self);
  drawspr(FIRSTMONSTER + self->m_id, self->x, self->y);
  return (0);
}

static int
monster_obj_damage(struct monster_obj *self)
{

   if (!self->alive) {
      /* We can only damage live thing or try to damage zombie */
      assert(self->zombie);
   }
   self->zombie = true;
   self->alive = false;
   monster_obj_updspr(self);
   drawspr(FIRSTMONSTER + self->m_id, self->x, self->y);
   return (0);
}

static int
monster_obj_kill(struct monster_obj *self)
{

  if (!self->alive) {
      /* No, you can't kill me twice */
      assert(self->zombie);
  }
  self->alive = false;
  self->zombie = false;
  erasespr(FIRSTMONSTER + self->m_id);
  return (0);
}

static int
monster_obj_animate(struct monster_obj *self)
{

  if (self->alive) {
    __drawmon(self);
  } else if (self->zombie) {
    __drawmondie(self);
  }
  return (0);
}

void
monster_obj_init(struct monster_obj *mp, uint16_t m_id, bool nobf, int16_t dir, int16_t x, int16_t y)
{

    memset(mp, '\0', sizeof(struct monster_obj));
    mp->nobf = nobf;
    mp->dir = dir;
    mp->x = x;
    mp->y = y;
    mp->pop = &monster_obj_pop;
    mp->animate = &monster_obj_animate;
    mp->mutate = &monster_obj_mutate;
    mp->kill = &monster_obj_kill;
    mp->damage = &monster_obj_damage;
    mp->m_id = m_id;
    mp->alive = true;
    mp->zombie = false;
    mp->monspr = 0;
    mp->monspd = 1;
}
