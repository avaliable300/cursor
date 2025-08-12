#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Log levels
enum {
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
};

// Forward declaration for callback event
typedef struct log_Event {
    const char *fmt;
    const char *file;
    int line;
    int level;
    void *udata;          // user data for the destination (FILE*, context, etc.)
    struct tm *time;      // filled lazily
    va_list ap;           // varargs for the formatted message
} log_Event;

// Type aliases for lock and log callbacks
typedef void (*log_LockFn)(bool lock, void *udata);
typedef void (*log_LogFn)(log_Event *ev);

// Public API
const char *log_level_string(int level);

void log_set_lock(log_LockFn fn, void *udata);
void log_set_level(int level);
void log_set_quiet(bool enable);

int log_add_callback(log_LogFn fn, void *udata, int level);
int log_add_fp(FILE *fp, int level);

// Enable fixed-size (128B) rolling log to a file path without changing other structures
int add_rolling_log(const char *path, size_t max_lines, int level);

// Core log function
void log_log(int level, const char *file, int line, const char *fmt, ...);

// Convenience macros
#define log_trace(...) log_log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) log_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)  log_log(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...)  log_log(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) log_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(...) log_log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // LOG_H