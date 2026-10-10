#include "bs/core/log.h"

#include <stdarg.h>
#include <stdio.h>

static bs_log_level g_level = BS_LOG_INFO;

void bs_log_set_level(bs_log_level level)
{
    g_level = level;
}

void bs_log(bs_log_level level, const char *fmt, ...)
{
    static const char *const prefix[] = { "debug", "info", "warn", "error" };
    if (level < g_level)
        return;

    fprintf(stderr, "[%s] ", prefix[level]);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
