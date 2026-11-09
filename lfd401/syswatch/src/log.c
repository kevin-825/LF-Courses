
#include "syswatch.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static const char *level_strings[] = {
    "DEBUG",
    "INFO ",
    "WARN ",
    "ERROR"
};

void sw_log_internal(enum sw_log_level level, const char *file, int line,
                    const char *func, const char *fmt, ...) {
    /* 1. Get high-resolution timestamp (seconds + nanoseconds) */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    /* 2. Convert seconds to broken-down time structure */
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    /* 3. Format main date/time up to seconds */
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    /* 4. Calculate milliseconds from nanoseconds */
    long ms = ts.tv_nsec / 1000000;

    /* 5. Print metadata header with millisecond accuracy: [YYYY-MM-DD HH:MM:SS.ms] */
    fprintf(stderr, "[%s.%03ld] [%s] [%s:%d:%s] ",
            time_buf, ms, level_strings[level], file, line, func);

    /* 6. Print formatted user message */
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    /* 7. Output newline */
    fprintf(stderr, "\n");
}


void sw_log_internal_1(enum sw_log_level level, const char *file, int line,
                    const char *func, const char *fmt, ...) {
    /* 1. Format current timestamp */
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    /* 2. Print metadata header: [Timestamp] [LEVEL] [file:line:func] */
    fprintf(stderr, "[%s] [%s] [%s:%d:%s] ",
            time_buf, level_strings[level], file, line, func);

    /* 3. Print the user log message */
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    /* 4. Append newline */
    fprintf(stderr, "\n");
}