#define _POSIX_C_SOURCE 200809L
#include "ringtxt.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_LINES 100

static int ensure_newline_write(int fd, const char *line) {
    size_t len = strlen(line);
    ssize_t w;
    if (len > 0 && line[len - 1] == '\n') {
        w = write(fd, line, len);
        if (w != (ssize_t)len) return -errno;
        return 0;
    }
    if (len > 0) {
        w = write(fd, line, len);
        if (w != (ssize_t)len) return -errno;
    }
    char nl = '\n';
    w = write(fd, &nl, 1);
    if (w != 1) return -errno;
    return 0;
}

static int count_lines_and_first_nl_offset(int fd, off_t *first_nl_off, size_t *out_lines) {
    char buf[8192];
    off_t pos = 0;
    ssize_t r;
    size_t lines = 0;
    off_t first_off = -1;
    if (lseek(fd, 0, SEEK_SET) < 0) return -errno;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; ++i) {
            if (buf[i] == '\n') {
                if (first_off == -1) first_off = pos + i;
                lines++;
            }
        }
        pos += r;
    }
    if (r < 0) return -errno;
    *first_nl_off = first_off; /* -1 if none */
    *out_lines = lines;
    return 0;
}

static int make_temp_path(const char *orig, char *out, size_t out_cap) {
    const char *suffix = ".tmp";
    size_t lo = strlen(orig), ls = strlen(suffix);
    if (lo + ls + 1 > out_cap) return -ENAMETOOLONG;
    memcpy(out, orig, lo);
    memcpy(out + lo, suffix, ls + 1);
    return 0;
}

int ringtxt_append_line(const char *path, const char *line) {
    if (!path || !line) return -EINVAL;

    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -errno;

    /* Serialize writers on this file */
    if (flock(fd, LOCK_EX) != 0) { int e = -errno; close(fd); return e; }

    /* Count lines and find first newline offset */
    off_t first_nl_off = -1;
    size_t cur_lines = 0;
    int rc = count_lines_and_first_nl_offset(fd, &first_nl_off, &cur_lines);
    if (rc != 0) { flock(fd, LOCK_UN); close(fd); return rc; }

    if (cur_lines < MAX_LINES) {
        /* Append directly */
        if (lseek(fd, 0, SEEK_END) < 0) { rc = -errno; flock(fd, LOCK_UN); close(fd); return rc; }
        rc = ensure_newline_write(fd, line);
        /* Best-effort flush */
        fsync(fd);
        flock(fd, LOCK_UN);
        close(fd);
        return rc;
    }

    /* Need to drop the first line and append new line. Use temp + rename for atomicity. */
    struct stat st;
    if (fstat(fd, &st) != 0) { rc = -errno; flock(fd, LOCK_UN); close(fd); return rc; }

    char tmp_path[4096];
    rc = make_temp_path(path, tmp_path, sizeof(tmp_path));
    if (rc != 0) { flock(fd, LOCK_UN); close(fd); return rc; }

    int tfd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    if (tfd < 0) { rc = -errno; flock(fd, LOCK_UN); close(fd); return rc; }

    /* Copy from byte after first newline to EOF */
    off_t start_off = (first_nl_off >= 0) ? (first_nl_off + 1) : 0;
    off_t to_copy_off = start_off;
    if (lseek(fd, to_copy_off, SEEK_SET) < 0) { rc = -errno; goto done; }

    char buf[8192];
    ssize_t rr;
    while ((rr = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t wp = 0;
        while (wp < rr) {
            ssize_t ww = write(tfd, buf + wp, (size_t)(rr - wp));
            if (ww < 0) { rc = -errno; goto done; }
            wp += ww;
        }
    }
    if (rr < 0) { rc = -errno; goto done; }

    /* Append the new line */
    rc = ensure_newline_write(tfd, line);
    if (rc != 0) goto done;

    if (fsync(tfd) != 0) { rc = -errno; goto done; }

    /* Replace original atomically */
    if (rename(tmp_path, path) != 0) { rc = -errno; goto done; }

    rc = 0;

 done:
    {
        int saved = errno;
        close(tfd);
        if (rc != 0) unlink(tmp_path);
        errno = saved;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return rc;
}