#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

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

// Segmented rotating log: N files of 1KB each (append mode)
#define MAX_SEGMENTS 16
#define SEGMENT_SIZE_BYTES 1024

typedef struct {
    bool used;
    char dir[256];
    char base[128];
    FILE *fp[MAX_SEGMENTS];
    size_t size[MAX_SEGMENTS];
    int current; // 0..count-1
    int count;   // number of segments in use
} SegmentedLogCtx;

#define MAX_ROLLING_CTX 32
static RollingFileCtx g_rolling_ctx[MAX_ROLLING_CTX];
static SegmentedLogCtx g_segmented_ctx; // single instance for simplicity

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

static int ensure_dir(const char *dir) {
    if (!dir || !*dir) return -1;
    struct stat st;
    if (stat(dir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        return -1;
    }
    if (mkdir(dir, 0775) == 0) return 0;
    return (errno == EEXIST) ? 0 : -1;
}

static void build_segment_path(const SegmentedLogCtx *ctx, int index, char *out, size_t out_size) {
    SNPRINTF(out, out_size, "%s/%s.%d.log", ctx->dir, ctx->base, index + 1);
}

static void segmented_truncate_and_reopen_append(SegmentedLogCtx *ctx, int index) {
    char path[512];
    build_segment_path(ctx, index, path, sizeof(path));
    // Truncate first
    FILE *tmp = fopen(path, "wb");
    if (tmp) fclose(tmp);
    // Reopen in append mode
    if (ctx->fp[index]) fclose(ctx->fp[index]);
    ctx->fp[index] = fopen(path, "a+b");
    ctx->size[index] = 0;
}

static size_t segmented_probe_size(const SegmentedLogCtx *ctx, int index) {
    char path[512];
    build_segment_path(ctx, index, path, sizeof(path));
    FILE *f = fopen(path, "a+b"); // create if not exist
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz > 0 ? (size_t)sz : 0;
}

static int segmented_init(const char *dir, const char *basename, int count) {
    if (count <= 0 || count > MAX_SEGMENTS) return -1;
    if (ensure_dir(dir) != 0) return -1;

    if (!g_segmented_ctx.used) {
        memset(&g_segmented_ctx, 0, sizeof(g_segmented_ctx));
        g_segmented_ctx.used = true;
    }

    SNPRINTF(g_segmented_ctx.dir, sizeof(g_segmented_ctx.dir), "%s", dir);
    SNPRINTF(g_segmented_ctx.base, sizeof(g_segmented_ctx.base), "%s", basename);
    g_segmented_ctx.count = count;

    for (int i = 0; i < g_segmented_ctx.count; ++i) {
        // Open append and record size
        char path[512];
        build_segment_path(&g_segmented_ctx, i, path, sizeof(path));
        if (g_segmented_ctx.fp[i]) {
            fclose(g_segmented_ctx.fp[i]);
            g_segmented_ctx.fp[i] = NULL;
        }
        g_segmented_ctx.fp[i] = fopen(path, "a+b");
        if (!g_segmented_ctx.fp[i]) return -1;
        fseek(g_segmented_ctx.fp[i], 0, SEEK_END);
        long sz = ftell(g_segmented_ctx.fp[i]);
        g_segmented_ctx.size[i] = sz > 0 ? (size_t)sz : 0;
    }

    // Choose current: first not full, else 0 and truncate it
    g_segmented_ctx.current = 0;
    int found = 0;
    for (int i = 0; i < g_segmented_ctx.count; ++i) {
        if (g_segmented_ctx.size[i] < SEGMENT_SIZE_BYTES) { g_segmented_ctx.current = i; found = 1; break; }
    }
    if (!found) {
        segmented_truncate_and_reopen_append(&g_segmented_ctx, 0);
        g_segmented_ctx.current = 0;
    }

    return 0;
}

static void segmented_advance(SegmentedLogCtx *ctx) {
    ctx->current = (ctx->current + 1) % ctx->count;
    // On rotation, destroy next file and reopen in append with size reset
    segmented_truncate_and_reopen_append(ctx, ctx->current);
}

static void segmented_write_bytes(SegmentedLogCtx *ctx, const char *data, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        if (ctx->size[ctx->current] >= SEGMENT_SIZE_BYTES) {
            segmented_advance(ctx);
        }
        size_t cap = SEGMENT_SIZE_BYTES - ctx->size[ctx->current];
        if (cap == 0) { segmented_advance(ctx); continue; }
        size_t chunk = (len - offset) < cap ? (len - offset) : cap;
        fwrite(data + offset, 1, chunk, ctx->fp[ctx->current]);
        fflush(ctx->fp[ctx->current]);
        ctx->size[ctx->current] += chunk;
        offset += chunk;
    }
}

static void segmented_write_line(SegmentedLogCtx *ctx, const char *line) {
    size_t len = safe_strnlen(line, 65536);
    // Ensure newline at end once
    int needs_nl = (len == 0 || line[len - 1] != '\n') ? 1 : 0;
    segmented_write_bytes(ctx, line, len);
    if (needs_nl) segmented_write_bytes(ctx, "\n", 1);
}

static void segmented_callback(log_Event *ev) {
    if (!g_segmented_ctx.used) return;

    // Timestamp + header
    char ts[32];
    time_t timep; time(&timep);
    struct tm *pt = gmtime(&timep);
    SNPRINTF(ts, sizeof(ts), "%d-%02d-%02d %02d:%02d:%02d",
             1900 + pt->tm_year, 1 + pt->tm_mon, pt->tm_mday,
             (8 + pt->tm_hour) % 24, pt->tm_min, pt->tm_sec);

    char linebuf[4096];
    int header_len = SNPRINTF(linebuf, sizeof(linebuf), "%s %-5s %s:%d: ",
                              ts, level_strings[ev->level], ev->file, ev->line);
    if (header_len < 0) header_len = 0;
    size_t pos = (size_t)header_len;
    if (pos > sizeof(linebuf)) pos = sizeof(linebuf);

    if (pos < sizeof(linebuf)) {
        int remain = (int)(sizeof(linebuf) - pos);
        (void)vsnprintf(linebuf + pos, (size_t)remain, ev->fmt, ev->ap);
    }

    segmented_write_line(&g_segmented_ctx, linebuf);
}

static void rolling_file_callback(log_Event *ev) {
    RollingFileCtx *ctx = (RollingFileCtx *)ev->udata;
    if (!ctx || !ctx->used || !ctx->fp || ctx->max_lines == 0) return;

    // Timestamp
    char ts[32];
    time_t timep; time(&timep);
    struct tm *pt = gmtime(&timep);
    SNPRINTF(ts, sizeof(ts), "%d-%02d-%02d %02d:%02d:%02d",
             1900 + pt->tm_year, 1 + pt->tm_mon, pt->tm_mday,
             (8 + pt->tm_hour) % 24, pt->tm_min, pt->tm_sec);

    // Build a full text line
    char linebuf[1024];
    int header_len = SNPRINTF(linebuf, sizeof(linebuf), "%s %-5s %s:%d: ",
                              ts, level_strings[ev->level], ev->file, ev->line);
    if (header_len < 0) header_len = 0;
    size_t pos = (size_t)header_len;
    if (pos > sizeof(linebuf)) pos = sizeof(linebuf);

    if (pos < sizeof(linebuf)) {
        int remain = (int)(sizeof(linebuf) - pos);
        int wrote = vsnprintf(linebuf + pos, (size_t)remain, ev->fmt, ev->ap);
        (void)wrote;
    }

    // Compose fixed 128-byte record, no NUL bytes
    char record[LOG_FIXED_RECORD_SIZE];
    memset(record, ' ', sizeof(record));
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

int add_segmented_log(const char *dir, const char *basename, int level) {
    if (segmented_init(dir, basename, 3) != 0) return -1;
    // Avoid duplicate registration
    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
        if (L.callbacks[i].fn == segmented_callback) return 0;
    }
    return log_add_callback(segmented_callback, NULL, level);
}

static int read_segment_count_from_config(const char *config_path) {
    FILE *f = fopen(config_path, "r");
    if (!f) return -1;
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    // Parse first integer in buffer
    int count = -1;
    for (size_t i = 0; i < n; ++i) {
        if (buf[i] >= '0' && buf[i] <= '9') {
            count = (int)strtol(&buf[i], NULL, 10);
            break;
        }
    }
    if (count < 1) count = 3;
    if (count > MAX_SEGMENTS) count = MAX_SEGMENTS;
    return count;
}

int add_segmented_log_from_config(const char *dir, const char *basename, const char *config_path, int level) {
    int count = read_segment_count_from_config(config_path);
    if (count < 1) count = 3;
    if (segmented_init(dir, basename, count) != 0) return -1;
    // Avoid duplicate registration
    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
        if (L.callbacks[i].fn == segmented_callback) return 0;
    }
    return log_add_callback(segmented_callback, NULL, level);
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