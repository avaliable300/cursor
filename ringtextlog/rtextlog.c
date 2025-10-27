#define _POSIX_C_SOURCE 200809L
#include "rtextlog.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

/* File layout (text only):
 * - Line 0: header text, e.g., "# RTEXT V1 idx=42 wrapped=1" padded with spaces then '\n'.
 * - Lines 1..RTEXT_MAX_LINES: fixed-width text logs (padded with spaces) ending with '\n'.
 */

struct RTextLog {
    int fd;
};

static size_t file_size_bytes(void) {
    return (size_t)(RTEXT_MAX_LINES + 1) * (size_t)RTEXT_LINE_BYTES;
}

static off_t line_offset(int line_no) {
    /* line_no in [0, RTEXT_MAX_LINES] */
    return (off_t)line_no * (off_t)RTEXT_LINE_BYTES;
}

static int ensure_size(int fd, size_t size) {
    struct stat st;
    if (fstat(fd, &st) != 0) return -errno;
    if ((size_t)st.st_size == size) return 0;
    if (ftruncate(fd, (off_t)size) != 0) return -errno;
    return 0;
}

static void pad_and_terminate(char *buf, size_t len_no_nl) {
    /* Fill with spaces up to RTEXT_LINE_BYTES-1, then put '\n' */
    if (len_no_nl > RTEXT_LINE_BYTES - 1) len_no_nl = RTEXT_LINE_BYTES - 1;
    memset(buf + len_no_nl, ' ', (RTEXT_LINE_BYTES - 1) - len_no_nl);
    buf[RTEXT_LINE_BYTES - 1] = '\n';
}

static int write_line(int fd, int line_no, const char *data, size_t len) {
    char line[RTEXT_LINE_BYTES];
    size_t used = len;
    if (used > RTEXT_LINE_BYTES - 1) used = RTEXT_LINE_BYTES - 1;
    memcpy(line, data, used);
    pad_and_terminate(line, used);
    ssize_t w = pwrite(fd, line, sizeof(line), line_offset(line_no));
    if (w != (ssize_t)sizeof(line)) return -errno;
    return 0;
}

static int read_line(int fd, int line_no, char *out) {
    ssize_t r = pread(fd, out, RTEXT_LINE_BYTES, line_offset(line_no));
    if (r != (ssize_t)RTEXT_LINE_BYTES) return -errno;
    return 0;
}

static int parse_header(const char *hdr_line, int *out_idx, int *out_wrapped) {
    /* Expected: "# RTEXT V1 idx=<d> wrapped=<d>" (padding spaces before \n) */
    int idx = 0, wrapped = 0;
    int matched = 0;
    /* Use strstr to find tokens to be tolerant to padding */
    const char *pidx = strstr(hdr_line, "idx=");
    const char *pwrap = strstr(hdr_line, "wrapped=");
    if (pidx) {
        matched += (sscanf(pidx, "idx=%d", &idx) == 1);
    }
    if (pwrap) {
        matched += (sscanf(pwrap, "wrapped=%d", &wrapped) == 1);
    }
    if (strstr(hdr_line, "# RTEXT V1") == NULL) return -1;
    if (matched < 2) return -1;
    if (idx < 0 || idx >= RTEXT_MAX_LINES) idx = 0;
    if (wrapped != 0 && wrapped != 1) wrapped = 0;
    *out_idx = idx;
    *out_wrapped = wrapped;
    return 0;
}

static void build_header_line(char *buf, int idx, int wrapped) {
    int n = snprintf(buf, RTEXT_LINE_BYTES, "# RTEXT V1 idx=%d wrapped=%d", idx, wrapped);
    size_t used = (n < 0) ? 0u : (size_t)n;
    pad_and_terminate(buf, used);
}

static int init_file_if_needed(int fd) {
    char hdr[RTEXT_LINE_BYTES];
    if (pread(fd, hdr, sizeof(hdr), line_offset(0)) != (ssize_t)sizeof(hdr)) {
        return -errno;
    }
    int idx = 0, wrapped = 0;
    if (parse_header(hdr, &idx, &wrapped) != 0) {
        /* initialize fresh */
        build_header_line(hdr, 0, 0);
        if (pwrite(fd, hdr, sizeof(hdr), line_offset(0)) != (ssize_t)sizeof(hdr)) return -errno;
        /* fill all data lines with blank */
        char blank[RTEXT_LINE_BYTES];
        pad_and_terminate(blank, 0);
        for (int i = 1; i <= RTEXT_MAX_LINES; ++i) {
            if (pwrite(fd, blank, sizeof(blank), line_offset(i)) != (ssize_t)sizeof(blank)) return -errno;
        }
    }
    return 0;
}

RTextLog *rtext_open(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return NULL;
    if (ensure_size(fd, file_size_bytes()) != 0) { close(fd); return NULL; }
    if (init_file_if_needed(fd) != 0) { close(fd); return NULL; }
    RTextLog *log = (RTextLog*)calloc(1, sizeof(*log));
    if (!log) { close(fd); return NULL; }
    log->fd = fd;
    return log;
}

void rtext_close(RTextLog *log) {
    if (!log) return;
    if (log->fd >= 0) close(log->fd);
    free(log);
}

static int get_header(RTextLog *log, int *idx, int *wrapped) {
    char hdr[RTEXT_LINE_BYTES];
    if (pread(log->fd, hdr, sizeof(hdr), line_offset(0)) != (ssize_t)sizeof(hdr)) return -errno;
    if (parse_header(hdr, idx, wrapped) != 0) return -EINVAL;
    return 0;
}

static int set_header(RTextLog *log, int idx, int wrapped) {
    char hdr[RTEXT_LINE_BYTES];
    build_header_line(hdr, idx, wrapped);
    if (pwrite(log->fd, hdr, sizeof(hdr), line_offset(0)) != (ssize_t)sizeof(hdr)) return -errno;
    return 0;
}

int rtext_appendfv(RTextLog *log, const char *fmt, va_list ap) {
    if (!log || !fmt) return -EINVAL;

    /* Format to temp buffer */
    char tmp[4096];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap2);
    va_end(ap2);
    if (n < 0) return -EINVAL;

    /* Ensure trailing '\n' */
    size_t used = (size_t)n;
    if (used == 0 || tmp[used - 1] != '\n') {
        if (used + 1 < sizeof(tmp)) {
            tmp[used++] = '\n';
            tmp[used] = '\0';
        } else {
            tmp[sizeof(tmp) - 2] = '\n';
            tmp[sizeof(tmp) - 1] = '\0';
            used = sizeof(tmp) - 1;
        }
    }

    int idx = 0, wrapped = 0;
    int rc = get_header(log, &idx, &wrapped);
    if (rc != 0) return rc;

    /* Compute data line number (1..RTEXT_MAX_LINES) */
    int line_no = 1 + idx;
    if (line_no > RTEXT_MAX_LINES) line_no = 1; /* should not happen */

    rc = write_line(log->fd, line_no, tmp, used);
    if (rc != 0) return rc;

    /* advance index */
    idx += 1;
    if (idx >= RTEXT_MAX_LINES) { idx = 0; wrapped = 1; }

    rc = set_header(log, idx, wrapped);
    if (rc != 0) return rc;

    return 0;
}

int rtext_appendf(RTextLog *log, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int rc = rtext_appendfv(log, fmt, ap);
    va_end(ap);
    return rc;
}

static void rtrim_spaces(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\n')) {
        if (s[len - 1] == '\n') break; /* keep newline */
        s[len - 1] = '\0';
        len--;
    }
}

int rtext_dump(RTextLog *log, FILE *out) {
    if (!log || !out) return -EINVAL;
    int idx = 0, wrapped = 0;
    int rc = get_header(log, &idx, &wrapped);
    if (rc != 0) return rc;

    int start = wrapped ? idx : 0; /* number of filled lines if not wrapped is idx */
    int count = wrapped ? RTEXT_MAX_LINES : idx;

    char line[RTEXT_LINE_BYTES + 1];
    line[RTEXT_LINE_BYTES] = '\0';

    for (int i = 0; i < count; ++i) {
        int data_idx = (start + i) % RTEXT_MAX_LINES; /* 0..99 */
        int line_no = 1 + data_idx;
        if (read_line(log->fd, line_no, line) != 0) return -errno;
        line[RTEXT_LINE_BYTES - 1] = '\n';
        line[RTEXT_LINE_BYTES] = '\0';
        rtrim_spaces(line);
        fputs(line, out);
    }
    fflush(out);
    return 0;
}