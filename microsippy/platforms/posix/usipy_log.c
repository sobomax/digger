#define _WITH_DPRINTF

#include <stdarg.h>
#include <stdio.h>
#include <stdatomic.h>
#include <unistd.h>

#include "public/usipy_str.h"

#include "usipy_port/log.h"

static struct {
    atomic_uint next_ticket;
    atomic_uint now_serving;
} _log_lock = {ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0)};

static void
_usipy_log_lock(void)
{
    unsigned int my_ticket;

    my_ticket = atomic_fetch_add_explicit(&(_log_lock.next_ticket), 1,
      memory_order_acq_rel);
    while (atomic_load_explicit(&(_log_lock.now_serving),
      memory_order_acquire) != my_ticket) {
        usleep(1);
    }
}

static void
_usipy_log_unlock(void)
{

    atomic_fetch_add_explicit(&(_log_lock.now_serving), 1,
      memory_order_release);
}

const static unsigned char _color_pre[]  = {0x1b, 0x5b, 0x30, 0x3b, 0x33, 0x32, 0x6d};
const static unsigned char _color_post[] = {0x1b, 0x5b, 0x30, 0x6d, '\n'};

const struct usipy_str color_pre = USIPY_B2STR(_color_pre);
const struct usipy_str color_post = USIPY_B2STR(_color_post);

void
usipy_log_write(int lvl, const char *tag, const char *fmt , ...)
{
    va_list ap;

    _usipy_log_lock();
    fwrite(color_pre.s.ro, color_pre.l, 1, stderr);
    fprintf(stderr, "I %s: ", tag);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fwrite(color_post.s.ro, color_post.l, 1, stderr);
    fflush(stderr);
    _usipy_log_unlock();
    va_end(ap);
    return;
}

