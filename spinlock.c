/*
 * Copyright (c) 2019 Sippy Software, Inc., http://www.sippysoft.com
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
 */

#if defined(DIGGER_DEBUG)
#include <assert.h>
#include <stdbool.h>
#endif
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "spinlock.h"

struct spinlock {
  atomic_flag flag;
#if defined(spinlock_test)
  atomic_uintmax_t nspins;
#endif
};

struct spinlock *
spinlock_ctor(void)
{
  struct spinlock *sp;

  sp = malloc(sizeof(*sp));
  if (sp == NULL)
    return (NULL);
  memset(sp, '\0', sizeof(*sp));
  atomic_flag_clear(&sp->flag);
#if defined(spinlock_test)
  atomic_init(&sp->nspins, 0);
#endif
  return (sp);
}

void
spinlock_dtor(struct spinlock *sp)
{

  free(sp);
}

void
spinlock_lock(struct spinlock *sp)
{
#if defined(spinlock_test)
  uintmax_t nspins;

  nspins = 0;
#endif
  while (atomic_flag_test_and_set_explicit(&sp->flag, memory_order_acquire)) {
#if defined(spinlock_test)
    nspins++;
#endif
    continue;
  }
#if defined(spinlock_test)
  if (nspins > 0)
    atomic_fetch_add(&sp->nspins, nspins);
#endif
}

void
spinlock_unlock(struct spinlock *sp)
{
#if defined(DIGGER_DEBUG)
  bool locked;

  locked = atomic_flag_test_and_set_explicit(&sp->flag, memory_order_acquire);
  assert(locked == true);
#endif
  atomic_flag_clear_explicit(&sp->flag, memory_order_release);
}

#if defined(spinlock_test)
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

struct targ {
  struct spinlock *sp;
  uint64_t wrkvar;
};

#define ITERS (20000000)

void *
wrkthr(void *ap)
{
  struct targ *tp;
  int i;

  tp = (struct targ *)ap;
  for (i = 0; i < ITERS; i++) {
    spinlock_lock(tp->sp);
    tp->wrkvar += 1;
    spinlock_unlock(tp->sp);
  }
  return (NULL);
}

int
spinlock_test()
{
  pthread_t thr;
  struct targ t;
  int i;

  memset(&t, '\0', sizeof(t));
  t.sp = spinlock_ctor();
  assert(t.sp != NULL);
  assert(pthread_create(&thr, NULL, wrkthr, &t) == 0);
  for (i = 0; i < ITERS; i++) {
    spinlock_lock(t.sp);
    t.wrkvar -= 1;
    spinlock_unlock(t.sp);
    if ((i & 0b11111111111111111111) == 0) {
      printf("\r%d", i);
      fflush(NULL);
    }
  }
  assert(pthread_join(thr, NULL) == 0);
  assert(t.wrkvar == 0);
  printf("\nspinned %llu cycles\n", (unsigned long long)atomic_load(&t.sp->nspins));
  return (0);
}
#endif
