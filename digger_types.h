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

#ifndef __DIGGER_TYPES_H
#define __DIGGER_TYPES_H

struct obj_position
{
  int16_t dir;
  int16_t x;
  int16_t y;
};

#define CALL_METHOD(obj, method, args...) (obj)->method(obj, ## args)

#define DIR2STR(s, opp) \
  switch ((opp)->dir) { \
  case DIR_NONE: \
    s = "NONE"; \
    break; \
  case DIR_RIGHT: \
    s = "RIGHT"; \
    break; \
  case DIR_UP: \
    s = "UP"; \
    break; \
  case DIR_LEFT: \
    s = "LEFT"; \
    break; \
  case DIR_DOWN: \
    s = "DOWN"; \
    break; \
  default: \
    abort(); \
  }

#endif
