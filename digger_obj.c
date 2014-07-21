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
#include "digger_obj.h"
#include "sprite.h"

static int
digger_obj_put(struct digger_obj *self)
{

  movedrawspr(FIRSTDIGGER + self->d_id, self->x, self->y);
  return (0);
}

static int
digger_obj_animate(struct digger_obj *self)
{

  drawdigger(self->d_id, self->dir, self->x, self->y, self->can_fire);
  return (0);
}

static int
digger_obj_discharge(struct digger_obj *self)
{

  assert(self->can_fire);
  self->can_fire = false;
  return (0);
}

static int
digger_obj_recharge(struct digger_obj *self)
{

  assert(!self->can_fire);
  self->can_fire = true;
  return (0);
}

void
digger_obj_init(struct digger_obj *self, uint16_t d_id, int16_t dir, int16_t x, int16_t y)
{

  memset(self, '\0', sizeof(struct digger_obj));
  self->dir = dir;
  self->x = x;
  self->y = y;
  self->put = &digger_obj_put;
  self->animate = &digger_obj_animate;
  self->discharge = &digger_obj_discharge;
  self->recharge = &digger_obj_recharge;
  self->d_id = d_id;
  self->alive = true;
  self->zombie = false;
  self->can_fire = true;
}
