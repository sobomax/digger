/*
 * Copyright (c) 2002-2017 Maxim Sobolev <sobomax@FreeBSD.org>
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

bool GetAsyncKeyState(int);

#define rightpressed  (GetAsyncKeyState(keycodes[0][0]))
#define uppressed     (GetAsyncKeyState(keycodes[1][0]))
#define leftpressed   (GetAsyncKeyState(keycodes[2][0]))
#define downpressed   (GetAsyncKeyState(keycodes[3][0]))
#define f1pressed     (GetAsyncKeyState(keycodes[4][0]))
#define right2pressed (GetAsyncKeyState(keycodes[5][0]))
#define up2pressed    (GetAsyncKeyState(keycodes[6][0]))
#define left2pressed  (GetAsyncKeyState(keycodes[7][0]))
#define down2pressed  (GetAsyncKeyState(keycodes[8][0]))
#define f12pressed    (GetAsyncKeyState(keycodes[9][0]))

