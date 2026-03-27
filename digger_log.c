#include <stdatomic.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

#include "digger_log.h"
#include "spinlock.h"

static struct spinlock *digger_log_spin = NULL;
static atomic_flag digger_log_init = ATOMIC_FLAG_INIT;
static _Atomic bool digger_log_ready = false;

static struct spinlock *
digger_log_getlock(void)
{
  struct spinlock *sp;

  if (digger_log_ready)
    return (digger_log_spin);
  while (atomic_flag_test_and_set_explicit(&digger_log_init,
    memory_order_acquire)) {
    if (digger_log_ready)
      return (digger_log_spin);
  }
  if (!digger_log_ready) {
    digger_log_spin = spinlock_ctor();
    digger_log_ready = true;
  }
  atomic_flag_clear_explicit(&digger_log_init, memory_order_release);
  sp = digger_log_spin;
  return (sp);
}

static void
digger_log_lock(void)
{
  struct spinlock *sp;

  sp = digger_log_getlock();
  if (sp == NULL)
    return;
  spinlock_lock(sp);
}

static void
digger_log_unlock(void)
{
  struct spinlock *sp;

  sp = digger_log_spin;
  if (sp == NULL)
    return;
  spinlock_unlock(sp);
}

void
digger_log_vprintf(const char *fmt, va_list ap)
{
  FILE *fp;

  fp = digger_log != NULL ? digger_log : stderr;
  digger_log_lock();
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  vfprintf(fp, fmt, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  fflush(fp);
  digger_log_unlock();
}

void
digger_log_printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  digger_log_vprintf(fmt, ap);
  va_end(ap);
}
