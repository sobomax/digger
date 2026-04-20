#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "public/usipy_platform.h"

#if defined(__GNUC__) || defined(__clang__)
#define USIPY_WEAK __attribute__((weak))
#else
#define USIPY_WEAK
#endif

static struct {
    atomic_uint next_ticket;
    atomic_uint now_serving;
} _log_lock = {ATOMIC_VAR_INIT(0), ATOMIC_VAR_INIT(0)};

static const unsigned char _color_pre[] = "\x1b[0;32m";
static const unsigned char _color_post[] = "\x1b[0m\n";

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

static uint64_t
usipy_platform_default_mono_ms(void)
{
    struct timespec ts;

    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return ((uint64_t)ts.tv_sec * 1000u) + ((uint64_t)ts.tv_nsec / 1000000u);
}

static void
usipy_platform_default_sleep_until_ms(uint64_t when_ms)
{
    struct timespec ts;

    ts.tv_sec = when_ms / 1000u;
    ts.tv_nsec = (long)((when_ms % 1000u) * 1000000u);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) != 0) {
        assert(errno == EINTR);
    }
}

static void
usipy_platform_default_seed_fallback(unsigned char *buf, size_t len)
{
    struct timespec rts;
    struct timespec mts;
    uint64_t state;

    clock_gettime(CLOCK_REALTIME, &rts);
    clock_gettime(CLOCK_MONOTONIC, &mts);
    state = ((uint64_t)rts.tv_sec << 32) ^ (uint64_t)rts.tv_nsec ^
      ((uint64_t)mts.tv_sec << 16) ^ (uint64_t)mts.tv_nsec ^
      (uint64_t)getpid() ^ (uintptr_t)buf;
    for (size_t i = 0; i < len; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        buf[i] = (unsigned char)(state >> ((i & 7u) * 8u));
    }
}

static int
usipy_platform_default_random_fill(void *buf, size_t len)
{
    int fd;
    unsigned char *bp = buf;
    size_t off = 0;

    if (buf == NULL) {
        return (-1);
    }
    fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        while (off < len) {
            ssize_t got = read(fd, bp + off, len - off);

            if (got > 0) {
                off += (size_t)got;
                continue;
            }
            if (got < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        close(fd);
    }
    if (off < len) {
        usipy_platform_default_seed_fallback(bp + off, len - off);
    }
    return (0);
}

static void usipy_platform_default_log_vwrite(int lvl, const char *tag,
  const char *fmt, va_list ap) __attribute__ ((format (printf, 3, 0)));

static void
usipy_platform_default_log_vwrite(int lvl, const char *tag, const char *fmt,
  va_list ap)
{
    int ch = (lvl == 0 ? 'I' : 'E');

    _usipy_log_lock();
    fwrite(_color_pre, sizeof(_color_pre) - 1, 1, stderr);
    fprintf(stderr, "%c %s: ", ch, tag);
    vfprintf(stderr, fmt, ap);
    fwrite(_color_post, sizeof(_color_post) - 1, 1, stderr);
    fflush(stderr);
    _usipy_log_unlock();
}

const struct usipy_platform usipy_platform_default = {
  .mono_ms = usipy_platform_default_mono_ms,
  .sleep_until_ms = usipy_platform_default_sleep_until_ms,
  .random_fill = usipy_platform_default_random_fill,
  .log_vwrite = usipy_platform_default_log_vwrite,
  .get_user_agent = usipy_platform_default_get_user_agent,
  .get_server = usipy_platform_default_get_server,
};

const struct usipy_platform usipy_platform USIPY_WEAK = {
  .mono_ms = usipy_platform_default_mono_ms,
  .sleep_until_ms = usipy_platform_default_sleep_until_ms,
  .random_fill = usipy_platform_default_random_fill,
  .log_vwrite = usipy_platform_default_log_vwrite,
  .get_user_agent = usipy_platform_default_get_user_agent,
  .get_server = usipy_platform_default_get_server,
};
