#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

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

static void
_usipy_log_lock(void)
{
    unsigned int my_ticket;

    my_ticket = atomic_fetch_add_explicit(&(_log_lock.next_ticket), 1,
      memory_order_acq_rel);
    while (atomic_load_explicit(&(_log_lock.now_serving),
      memory_order_acquire) != my_ticket) {
        Sleep(0);
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

    return ((uint64_t)GetTickCount64());
}

static void
usipy_platform_default_sleep_until_ms(uint64_t when_ms)
{
    uint64_t now_ms;
    uint64_t delay_ms;

    for (;;) {
        now_ms = usipy_platform_mono_ms();
        if (now_ms >= when_ms) {
            return;
        }
        delay_ms = when_ms - now_ms;
        if (delay_ms > UINT32_MAX) {
            delay_ms = UINT32_MAX;
        }
        Sleep((DWORD)delay_ms);
    }
}

static void
usipy_platform_default_seed_fallback(unsigned char *buf, size_t len)
{
    FILETIME ft;
    LARGE_INTEGER pc;
    uint64_t state;

    GetSystemTimeAsFileTime(&ft);
    QueryPerformanceCounter(&pc);
    state = ((uint64_t)ft.dwHighDateTime << 32) ^ (uint64_t)ft.dwLowDateTime ^
      (uint64_t)pc.QuadPart ^ usipy_platform_mono_ms() ^
      (uint64_t)GetCurrentProcessId() ^ (uint64_t)GetCurrentThreadId() ^
      (uintptr_t)buf;
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

    if (buf == NULL) {
        return (-1);
    }
    usipy_platform_default_seed_fallback(buf, len);
    return (0);
}

static void
usipy_platform_default_log_vwrite(int lvl, const char *tag, const char *fmt,
  va_list ap)
{
    int ch = (lvl == 0 ? 'I' : 'E');

    _usipy_log_lock();
    fprintf(stderr, "%c %s: ", ch, tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    _usipy_log_unlock();
}

const struct usipy_platform usipy_platform_default = {
  .default_udp_port = &DEFAULT_UDP_PORT_s,
  .default_udp_port_i = DEFAULT_UDP_PORT,
  .mono_ms = usipy_platform_default_mono_ms,
  .sleep_until_ms = usipy_platform_default_sleep_until_ms,
  .random_fill = usipy_platform_default_random_fill,
  .log_vwrite = usipy_platform_default_log_vwrite,
  .get_user_agent = usipy_platform_default_get_user_agent,
  .get_server = usipy_platform_default_get_server,
};

const struct usipy_platform *USIPY_WEAK
usipy_platform_get(void)
{

    return (&usipy_platform_default);
}
