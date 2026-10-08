#include <stdarg.h>
#include <stdio.h>

extern FILE *digger_log;
void digger_log_printf(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
void digger_log_vprintf(const char *fmt, va_list ap);
void digger_log_start(void);
const char *digger_log_ts(char *buf, size_t len);

/* The size of a digger_log_ts() buffer */
#define DIGGER_LOG_TSLEN 24
