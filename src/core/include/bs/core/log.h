#ifndef BS_CORE_LOG_H
#define BS_CORE_LOG_H

typedef enum bs_log_level {
    BS_LOG_DEBUG,
    BS_LOG_INFO,
    BS_LOG_WARN,
    BS_LOG_ERROR
} bs_log_level;

#if defined(__GNUC__) || defined(__clang__)
#define BS_PRINTF_FMT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define BS_PRINTF_FMT(fmt_idx, args_idx)
#endif

/* Messages below the threshold are dropped. Default: BS_LOG_INFO. */
void bs_log_set_level(bs_log_level level);

void bs_log(bs_log_level level, const char *fmt, ...) BS_PRINTF_FMT(2, 3);

#define BS_DEBUG(...) bs_log(BS_LOG_DEBUG, __VA_ARGS__)
#define BS_INFO(...)  bs_log(BS_LOG_INFO, __VA_ARGS__)
#define BS_WARN(...)  bs_log(BS_LOG_WARN, __VA_ARGS__)
#define BS_ERROR(...) bs_log(BS_LOG_ERROR, __VA_ARGS__)

#endif
