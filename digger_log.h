#include <stdarg.h>
#include <stdio.h>

extern FILE *digger_log;
void digger_log_printf(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
void digger_log_vprintf(const char *fmt, va_list ap);
