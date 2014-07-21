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

#ifndef __MONSTER_OBJ_H
#define __MONSTER_OBJ_H

struct monster_obj;
struct monster_obj_private;
struct obj_position;

typedef int (*mmethod_0_t)(struct monster_obj *);
typedef int (*mmethod_xetpos)(struct monster_obj *, struct obj_position *pos);
typedef bool (*mmethod_getflag)(struct monster_obj *);

struct monster_obj {
    mmethod_0_t dtor;
    mmethod_0_t put;
    mmethod_0_t mutate;
    mmethod_0_t animate;
    mmethod_0_t damage;
    mmethod_0_t kill;
    mmethod_xetpos getpos;
    mmethod_xetpos setpos;
    mmethod_getflag isalive;
    mmethod_getflag isnobbin;
    struct monster_obj_private *priv;
};

struct monster_obj *monster_obj_ctor(uint16_t m_id,
 bool nobf, int16_t dir, int16_t x, int16_t y);

#define MON_NOBBIN true 
#define MON_HOBBIN false

#endif
