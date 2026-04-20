#include <assert.h>
#include <stdarg.h>

#include "public/usipy_platform.h"

static const struct usipy_str _usipy_platform_default_agent = USIPY_2STR("uSippy");

const struct usipy_str *
usipy_platform_default_get_user_agent(void)
{

    return (&_usipy_platform_default_agent);
}

const struct usipy_str *
usipy_platform_default_get_server(void)
{

    return (&_usipy_platform_default_agent);
}

uint64_t
usipy_platform_delegate_mono_ms(void)
{
    const struct usipy_platform *pp = USIPY_PLATFORM.fallback;

    assert(pp != NULL);
    return (pp->mono_ms());
}

void
usipy_platform_delegate_sleep_until_ms(uint64_t when_ms)
{
    const struct usipy_platform *pp = USIPY_PLATFORM.fallback;

    assert(pp != NULL);
    pp->sleep_until_ms(when_ms);
}

int
usipy_platform_delegate_random_fill(void *buf, size_t len)
{
    const struct usipy_platform *pp = USIPY_PLATFORM.fallback;

    assert(pp != NULL);
    return (pp->random_fill(buf, len));
}

void
usipy_platform_delegate_log_vwrite(int lvl, const char *tag, const char *fmt,
  va_list ap)
{
    const struct usipy_platform *pp = USIPY_PLATFORM.fallback;

    assert(pp != NULL);
    pp->log_vwrite(lvl, tag, fmt, ap);
}

uint64_t
usipy_platform_mono_ms(void)
{

    assert(USIPY_PLATFORM.mono_ms != NULL);
    return (USIPY_PLATFORM.mono_ms());
}

void
usipy_platform_sleep_until_ms(uint64_t when_ms)
{

    assert(USIPY_PLATFORM.sleep_until_ms != NULL);
    USIPY_PLATFORM.sleep_until_ms(when_ms);
}

int
usipy_platform_random_fill(void *buf, size_t len)
{

    assert(USIPY_PLATFORM.random_fill != NULL);
    return (USIPY_PLATFORM.random_fill(buf, len));
}

void
usipy_platform_log_vwrite(int lvl, const char *tag, const char *fmt, va_list ap)
{

    assert(USIPY_PLATFORM.log_vwrite != NULL);
    USIPY_PLATFORM.log_vwrite(lvl, tag, fmt, ap);
}

void
usipy_log_write(int lvl, const char *tag, const char *fmt , ...)
{
    va_list ap;

    va_start(ap, fmt);
    usipy_platform_log_vwrite(lvl, tag, fmt, ap);
    va_end(ap);
}
