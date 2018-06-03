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
#include "bullet_obj.h"
#include "sound.h"
#include "sprite.h"

static int
bullet_obj_put(struct bullet_obj *self)
{
  movedrawspr(FIRSTFIREBALL + self->f_id, self->x, self->y);
  soundfire(self->f_id);
  return (0);
}

static int
bullet_obj_animate(struct bullet_obj *self)
{

  assert(self->expsn < 4);
  drawfire(self->f_id, self->x, self->y, self->expsn);
  if (self->expsn > 0) {
    if (self->expsn == 1) {
      soundexplode(self->f_id);
    }
    self->expsn += 1;
  }

  return (0);
}

static int
bullet_obj_remove(struct bullet_obj *self)
{

  erasespr(FIRSTFIREBALL + self->f_id);
  if (self->expsn > 1) {
    soundfireoff(self->f_id);
  }
  self->expsn = 0;
  return (0);
}

static int
bullet_obj_explode(struct bullet_obj *self)
{

  /*assert(self->expsn == 0);*/
  self->expsn = 1;
  return (0);
}

void
bullet_obj_init(struct bullet_obj *self, uint16_t f_id, int16_t dir, int16_t x, int16_t y)
{

  memset(self, '\0', sizeof(struct bullet_obj));
  self->dir = dir;
  self->x = x;
  self->y = y;
  self->expsn = 0;
  self->put = &bullet_obj_put;
  self->animate = &bullet_obj_animate;
  self->remove = &bullet_obj_remove;
  self->explode = &bullet_obj_explode;
  self->f_id = f_id;
}
