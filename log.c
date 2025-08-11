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
    FILE *fp;
    size_t max_lines;
    size_t current_index;
    size_t valid_records; // how many records are valid (<= max_lines)
} RollingFileCtx;

// Keep a simple registry to map FILE* to RollingFileCtx for dump/query
#define MAX_ROLLING_CTX 32
static RollingFileCtx *g_rolling_ctx[MAX_ROLLING_CTX];

// Internal rolling file
static RollingFileCtx *g_default_rolling_ctx = NULL;

static void register_rolling_ctx(RollingFileCtx *ctx) {
    for (int i = 0; i < MAX_ROLLING_CTX; ++i) {
        if (g_rolling_ctx[i] == NULL) {
            g_rolling_ctx[i] = ctx;
            return;
        }
    }
}

static RollingFileCtx *find_rolling_ctx(FILE *fp) {
    for (int i = 0; i < MAX_ROLLING_CTX; ++i) {
        if (g_rolling_ctx[i] && g_rolling_ctx[i]->fp == fp) return g_rolling_ctx[i];
    }
    return NULL;
}

static RollingFileCtx *rolling_ctx_create(FILE *fp, size_t max_lines) {
    RollingFileCtx *ctx = (RollingFileCtx *)malloc(sizeof(RollingFileCtx));
    if (!ctx) return NULL;
    ctx->fp = fp;
    ctx->max_lines = max_lines ? max_lines : 1;
    ctx->current_index = 0;
    ctx->valid_records = 0;

    if (fp) {
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
    }
    register_rolling_ctx(ctx);
    return ctx;
}

static void ensure_default_rolling_initialized(void) {
    if (g_default_rolling_ctx) return;
    FILE *fp = fopen(DEFAULT_ROLLING_PATH, "r+b");
    if (!fp) fp = fopen(DEFAULT_ROLLING_PATH, "w+b");
    if (!fp) return;
    g_default_rolling_ctx = rolling_ctx_create(fp, DEFAULT_ROLLING_MAX_LINES);
}

static void rolling_file_callback(log_Event *ev) {
    RollingFileCtx *ctx = (RollingFileCtx *)ev->udata;
    if (!ctx || !ctx->fp || ctx->max_lines == 0) return;

    // Timestamp similar to other callbacks
    char ts[32];
    time_t timep;
    time(&timep);
    struct tm *pt = gmtime(&timep);
    SNPRINTF(ts, sizeof(ts), "%d-%02d-%02d %02d:%02d:%02d",
             1900 + pt->tm_year, 1 + pt->tm_mon, pt->tm_mday,
             (8 + pt->tm_hour) % 24, pt->tm_min, pt->tm_sec);

    // Format message body first using the va_list
    char msg[512];
    vsnprintf(msg, sizeof(msg), ev->fmt, ev->ap);

    // Compose a fixed-size 128-byte record: pad with spaces and end with '\n'
    char record[LOG_FIXED_RECORD_SIZE];
    memset(record, ' ', sizeof(record));
    // Leave the final byte for '\n'
    SNPRINTF(record, LOG_FIXED_RECORD_SIZE - 1, "%s %-5s %s:%d: %s",
             ts, level_strings[ev->level], ev->file, ev->line, msg);
    record[LOG_FIXED_RECORD_SIZE - 1] = '\n';

    // Seek to the correct slot and overwrite
    long offset = (long)(ctx->current_index % ctx->max_lines) * (long)LOG_FIXED_RECORD_SIZE;
    fseek(ctx->fp, offset, SEEK_SET);
    fwrite(record, 1, sizeof(record), ctx->fp);
    fflush(ctx->fp);

    // Advance circular index and valid count
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

static int log_add_default_rolling_if_needed(void) {
    ensure_default_rolling_initialized();
    if (!g_default_rolling_ctx) return -1;
    // Check if already registered as a callback
    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
        if (L.callbacks[i].fn == rolling_file_callback && L.callbacks[i].udata == g_default_rolling_ctx) {
            return 0;
        }
    }
    return log_add_callback(rolling_file_callback, g_default_rolling_ctx, L.level);
}

// Optionally, for internal debugging: dump chronological to stderr
static void dump_default_rolling_chronological_to(FILE *out) {
    if (!g_default_rolling_ctx || !g_default_rolling_ctx->fp) return;
    RollingFileCtx *ctx = g_default_rolling_ctx;
    size_t total = ctx->valid_records;
    if (total == 0) return;
    size_t start = (ctx->valid_records == ctx->max_lines) ? ctx->current_index : 0;
    fflush(ctx->fp);
    char record[LOG_FIXED_RECORD_SIZE];
    for (size_t i = 0; i < total; ++i) {
        size_t slot = (start + i) % ctx->max_lines;
        long offset = (long)slot * (long)LOG_FIXED_RECORD_SIZE;
        fseek(ctx->fp, offset, SEEK_SET);
        size_t n = fread(record, 1, LOG_FIXED_RECORD_SIZE, ctx->fp);
        if (n != LOG_FIXED_RECORD_SIZE) break;
        fwrite(record, 1, LOG_FIXED_RECORD_SIZE, out);
    }
    fflush(out);
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

    // Ensure default rolling logger is installed
    log_add_default_rolling_if_needed();

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