#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "usipy_str.h"

struct usipy_platform;

typedef uint64_t (*usipy_platform_mono_ms_cb)(void);
typedef void (*usipy_platform_sleep_until_ms_cb)(uint64_t);
typedef int (*usipy_platform_random_fill_cb)(void *, size_t);
typedef void (*usipy_platform_log_vwrite_cb)(int, const char *,
  const char *, va_list) __attribute__ ((format (printf, 3, 0)));
typedef const struct usipy_str *(*usipy_platform_get_header_cb)(void);

struct usipy_platform {
    const struct usipy_platform *fallback;
    const struct usipy_str *default_udp_port;
    uint16_t default_udp_port_i;
    usipy_platform_mono_ms_cb mono_ms;
    usipy_platform_sleep_until_ms_cb sleep_until_ms;
    usipy_platform_random_fill_cb random_fill;
    usipy_platform_log_vwrite_cb log_vwrite;
    usipy_platform_get_header_cb get_user_agent;
    usipy_platform_get_header_cb get_server;
};

/*
 * Platforms provide a default definition of the active platform. On Windows we
 * route access via a getter to avoid COFF/LTO data-symbol override issues.
 */
extern const struct usipy_platform usipy_platform_default;
#define DEFAULT_UDP_PORT 5060
#define _STRINGIFY(x) #x
#define _STR(x) USIPY_2STRC(_STRINGIFY(x))
#define DEFAULT_UDP_PORT_s _STR(DEFAULT_UDP_PORT)

#if defined(_WIN32)
const struct usipy_platform *usipy_platform_get(void);
#define USIPY_PLATFORM (*usipy_platform_get())
#else
extern const struct usipy_platform usipy_platform;
#define USIPY_PLATFORM (usipy_platform)
#endif

const struct usipy_str *usipy_platform_default_get_user_agent(void);
const struct usipy_str *usipy_platform_default_get_server(void);
uint64_t usipy_platform_delegate_mono_ms(void);
void usipy_platform_delegate_sleep_until_ms(uint64_t);
int usipy_platform_delegate_random_fill(void *, size_t);
void usipy_platform_delegate_log_vwrite(int, const char *, const char *,
  va_list) __attribute__ ((format (printf, 3, 0)));
uint64_t usipy_platform_mono_ms(void);
void usipy_platform_sleep_until_ms(uint64_t);
int usipy_platform_random_fill(void *, size_t);
void usipy_platform_log_vwrite(int, const char *, const char *, va_list)
  __attribute__ ((format (printf, 3, 0)));
void usipy_log_write(int, const char *, const char *, ...) \
  __attribute__ ((format (printf, 3, 4)));
