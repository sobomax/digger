#include <stdatomic.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "def.h"
#include "digger_log.h"
#include "netsim_platform.h"
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

/* The monotonic clock (ns), NetSim's where it has one */
static uint64_t
digger_log_mono_ns(void)
{
#if NETSIM_PLATFORM_SUPPORTED
  return (netsim_monotonic_ns());
#else
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec);
#endif
}

/* What the log times count from: when the program started (main()), or
   the monotonic clock time in DIGGER_LOG_T0 (seconds; the same for two
   programs on a machine makes their logs go together, 0 is the clock's) */
static uint64_t digger_log_t0;

void
digger_log_start(void)
{
  const char *envp;
  char *ep;
  double t0;

  envp = getenv("DIGGER_LOG_T0");
  if (envp != NULL && *envp != '\0') {
    t0 = strtod(envp, &ep);
    if (*ep == '\0' && t0 >= 0) {
      digger_log_t0 = (uint64_t)(t0 * 1e9);
      return;
    }
  }
  digger_log_t0 = digger_log_mono_ns();
}

/* The time since then, as sec.msec, for log lines */
const char *
digger_log_ts(char *buf, size_t len)
{
  uint64_t now, ms;

  now = digger_log_mono_ns();
  if (now < digger_log_t0) {
    ms = (digger_log_t0 - now) / 1000000;
    snprintf(buf, len, "-%llu.%03u", (unsigned long long)(ms / 1000),
      (unsigned int)(ms % 1000));
  } else {
    ms = (now - digger_log_t0) / 1000000;
    snprintf(buf, len, "%llu.%03u", (unsigned long long)(ms / 1000),
      (unsigned int)(ms % 1000));
  }
  return (buf);
}
