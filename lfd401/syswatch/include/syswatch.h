#ifndef SYSWATCH_H
#define SYSWATCH_H

/* When -std=c11 is passed to GCC, glibc defines __STRICT_ANSI__ and 
 * hides non-ISO extensions (POSIX.1-2008 functions like clock_gettime and localtime_r).
 * Defining #define _GNU_SOURCE (or defining -D_GNU_SOURCE in your Makefile's CPPFLAGS) instructs glibc
 * to expose all POSIX, BSD, and Linux system calls and functions across system headers like <time.h>. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>

#ifndef SYSWATCH_VERSION
#define SYSWATCH_VERSION "0.1.0-dev"
#endif

enum sw_log_level {
    SW_LOG_DEBUG = 0,
    SW_LOG_INFO,
    SW_LOG_WARN,
    SW_LOG_ERROR
};

/* Define SW_API to make marked functions public/exported */
#if defined(__GNUC__) && __GNUC__ >= 4
    #define SW_API __attribute__((visibility("default")))
#else
    #define SW_API
#endif

SW_API void sw_log_internal(enum sw_log_level level, const char *file, int line,
                    const char *func, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

SW_API const char *sw_strerror(int errnum, char *buf, size_t buflen);

#define SW_LOG_INFO(fmt, ...) \
    do { sw_log_internal(SW_LOG_INFO, __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__); } while (0)

#define SW_LOG_WARN(fmt, ...) \
    do { sw_log_internal(SW_LOG_WARN, __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__); } while (0)

#define SW_LOG_ERROR(fmt, ...) \
    do { sw_log_internal(SW_LOG_ERROR, __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__); } while (0)

#ifdef ENABLE_DEBUG_LOG
#define SW_LOG_DEBUG(fmt, ...) \
    do { sw_log_internal(SW_LOG_DEBUG, __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__); } while (0)
#else
#define SW_LOG_DEBUG(fmt, ...) \
    do { (void)sizeof(printf(fmt, ##__VA_ARGS__)); } while (0)
#endif

#endif /* SYSWATCH_H */