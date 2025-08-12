#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CALLBACKS 32
#define LOG_FIXED_RECORD_SIZE 128
#define DEFAULT_ROLLING_MAX_LINES 100
#define DEFAULT_ROLLING_PATH "rolling.log"

#ifdef _MSC_VER
#define SNPRINTF _snprintf
#else
#define SNPRINTF snprintf
#endif

static size_t safe_strnlen(const char *s, size_t maxlen) {
    size_t i = 0;
    if (!s) return 0;
    for (; i < maxlen; ++i) {
        if (s[i] == '\0') break;
    }
    return i;
}

typedef struct {
    log_LogFn fn;
    void *udata;
    int level;
} Callback;

static struct {
    void *udata;
    log_LockFn lock;
    int level;
    bool quiet;
    Callback callbacks[MAX_CALLBACKS];
} L;

static const char *level_strings[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

#ifdef LOG_USE_COLOR
static const char *level_colors[] = {"\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"};
#endif

static void stdout_callback(log_Event *ev) {
    char buf[32] = {0};
    time_t timep;
    time(&timep);
    struct tm *pt = gmtime(&timep);
    SNPRINTF(buf, sizeof(buf), "%d-%02d-%02d %02d:%02d:%02d",
             1900 + pt->tm_year, 1 + pt->tm_mon, pt->tm_mday,
             (8 + pt->tm_hour) % 24, pt->tm_min, pt->tm_sec);
#ifdef LOG_USE_COLOR
    fprintf((FILE *)ev->udata, "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ",
            buf, level_colors[ev->level], level_strings[ev->level], ev->file, ev->line);
#else
    fprintf((FILE *)ev->udata, "%s %-5s %s:%d: ",
            buf, level_strings[ev->level], ev->file, ev->line);
#endif
    vfprintf((FILE *)ev->udata, ev->fmt, ev->ap);
    fprintf((FILE *)ev->udata, "\n");
    fflush((FILE *)ev->udata);
}

static void file_callback(log_Event *ev) {
    char buf[32] = {0};
    time_t timep;
    time(&timep);
    struct tm *pt = gmtime(&timep);
    SNPRINTF(buf, sizeof(buf), "%d-%02d-%02d %02d:%02d:%02d",
             1900 + pt->tm_year, 1 + pt->tm_mon, pt->tm_mday,
             (8 + pt->tm_hour) % 24, pt->tm_min, pt->tm_sec);
    fprintf((FILE *)ev->udata, "%s %-5s %s:%d: ",
            buf, level_strings[ev->level], ev->file, ev->line);
    vfprintf((FILE *)ev->udata, ev->fmt, ev->ap);
    fprintf((FILE *)ev->udata, "\n");
    fflush((FILE *)ev->udata);
}

// Rolling file context that manages circular 128-byte records
typedef struct {
    bool used;
    FILE *fp;
    size_t max_lines;
    size_t current_index;
    size_t valid_records; // how many records are valid (<= max_lines)
} RollingFileCtx;

#define MAX_ROLLING_CTX 32
static RollingFileCtx g_rolling_ctx[MAX_ROLLING_CTX];

static RollingFileCtx *alloc_rolling_ctx(void) {
    for (int i = 0; i < MAX_ROLLING_CTX; ++i) {
        if (!g_rolling_ctx[i].used) {
            g_rolling_ctx[i].used = true;
            g_rolling_ctx[i].fp = NULL;
            g_rolling_ctx[i].max_lines = 0;
            g_rolling_ctx[i].current_index = 0;
            g_rolling_ctx[i].valid_records = 0;
            return &g_rolling_ctx[i];
        }
    }
    return NULL;
}

static void rolling_file_callback(log_Event *ev) {
    RollingFileCtx *ctx = (RollingFileCtx *)ev->udata;
    if (!ctx || !ctx->used || !ctx->fp || ctx->max_lines == 0) return;

    // Timestamp
    char ts[32];
    time_t timep;
    time(&timep);
    struct tm *pt = gmtime(&timep);
    SNPRINTF(ts, sizeof(ts), "%d-%02d-%02d %02d:%02d:%02d",
             1900 + pt->tm_year, 1 + pt->tm_mon, pt->tm_mday,
             (8 + pt->tm_hour) % 24, pt->tm_min, pt->tm_sec);

    // Build the full line into a temporary buffer (text only)
    char linebuf[1024];
    int header_len = SNPRINTF(linebuf, sizeof(linebuf), "%s %-5s %s:%d: ",
                              ts, level_strings[ev->level], ev->file, ev->line);
    if (header_len < 0) header_len = 0;
    size_t pos = (size_t)header_len;
    if (pos > sizeof(linebuf)) pos = sizeof(linebuf);

    // Append message
    if (pos < sizeof(linebuf)) {
        int remain = (int)(sizeof(linebuf) - pos);
        int wrote = vsnprintf(linebuf + pos, (size_t)remain, ev->fmt, ev->ap);
        if (wrote < 0) wrote = 0;
        // Do not rely on terminating NUL; we will copy explicit length below
    }

    // Create a 128-byte record with no NUL bytes: copy visible chars, pad with spaces, last byte is '\n'
    char record[LOG_FIXED_RECORD_SIZE];
    memset(record, ' ', sizeof(record));

    // Compute visible length (stop at first NUL if any)
    size_t visible_len = safe_strnlen(linebuf, sizeof(linebuf));
    if (visible_len > LOG_FIXED_RECORD_SIZE - 1) visible_len = LOG_FIXED_RECORD_SIZE - 1;
    memcpy(record, linebuf, visible_len);
    record[LOG_FIXED_RECORD_SIZE - 1] = '\n';

    long offset = (long)(ctx->current_index % ctx->max_lines) * (long)LOG_FIXED_RECORD_SIZE;
    fseek(ctx->fp, offset, SEEK_SET);
    fwrite(record, 1, sizeof(record), ctx->fp);
    fflush(ctx->fp);

    ctx->current_index = (ctx->current_index + 1) % ctx->max_lines;
    if (ctx->valid_records < ctx->max_lines) ctx->valid_records++;
}

static void lock(void) {
    if (L.lock) {
        L.lock(true, L.udata);
    }
}

static void unlock(void) {
    if (L.lock) {
        L.lock(false, L.udata);
    }
}

const char *log_level_string(int level) { return level_strings[level]; }

void log_set_lock(log_LockFn fn, void *udata) {
    L.lock = fn;
    L.udata = udata;
}

void log_set_level(int level) { L.level = level; }

void log_set_quiet(bool enable) { L.quiet = enable; }

int log_add_callback(log_LogFn fn, void *udata, int level) {
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!L.callbacks[i].fn) {
            L.callbacks[i].fn = fn;
            L.callbacks[i].udata = udata;
            L.callbacks[i].level = level;
            return 0;
        }
    }
    return -1;
}

int log_add_fp(FILE *fp, int level) { return log_add_callback(file_callback, fp, level); }

int add_rolling_log(const char *path, size_t max_lines, int level) {
    if (!path || max_lines == 0) return -1;
    FILE *fp = fopen(path, "r+b");
    if (!fp) fp = fopen(path, "w+b");
    if (!fp) return -1;

    RollingFileCtx *ctx = alloc_rolling_ctx();
    if (!ctx) {
        fclose(fp);
        return -1;
    }

    ctx->fp = fp;
    ctx->max_lines = max_lines;
    ctx->current_index = 0;
    ctx->valid_records = 0;

    // Initialize position from existing file size
    long cur = ftell(fp);
    if (cur < 0) cur = 0;
    if (fseek(fp, 0, SEEK_END) == 0) {
        long size = ftell(fp);
        if (size > 0) {
            size_t written_records = (size_t)(size / LOG_FIXED_RECORD_SIZE);
            ctx->current_index = written_records % ctx->max_lines;
            ctx->valid_records = written_records > ctx->max_lines ? ctx->max_lines : written_records;
        }
        fseek(fp, cur, SEEK_SET);
    }

    return log_add_callback(rolling_file_callback, ctx, level);
}

static void init_event(log_Event *ev, void *udata) {
    if (!ev->time) {
        time_t t = time(NULL);
        ev->time = gmtime(&t);
    }
    ev->udata = udata;
}

void log_log(int level, const char *file, int line, const char *fmt, ...) {
    log_Event ev;
    ev.fmt = fmt;
    ev.file = file;
    ev.line = line;
    ev.level = level;
    ev.time = NULL;

    lock();

    if (!L.quiet && level >= L.level) {
        init_event(&ev, stderr);
        va_start(ev.ap, fmt);
        stdout_callback(&ev);
        va_end(ev.ap);
    }

    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
        Callback *cb = &L.callbacks[i];
        if (level >= cb->level) {
            init_event(&ev, cb->udata);
            va_start(ev.ap, fmt);
            cb->fn(&ev);
            va_end(ev.ap);
        }
    }

    unlock();
}