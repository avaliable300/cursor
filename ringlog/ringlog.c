#define _POSIX_C_SOURCE 200809L
#include "ringlog.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// File layout:
// [Header][Slot0][Slot1]...[SlotN-1]
// Header contains magic, version, write_index, wrapped flag.
// Each slot is a fixed-size block of RINGLOG_SLOT_BYTES bytes:
//   uint16_t used_len;  // bytes used in payload (<= RINGLOG_SLOT_BYTES - 2)
//   char payload[...];
//   Remaining bytes are zero-filled.

#define RINGLOG_MAGIC 0x524C4F47u  // 'RLOG'
#define RINGLOG_VERSION 1u

#pragma pack(push, 1)
typedef struct RingLogHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t write_index; // 0..RINGLOG_MAX_LINES-1
    uint8_t wrapped;      // 0 or 1
    uint8_t pad[5];       // pad to 16 bytes
} RingLogHeader;
#pragma pack(pop)

typedef struct SlotHeader {
    uint16_t used_len; // includes any trailing '\n'
} SlotHeader;

struct RingLog {
    int fd;
    size_t file_size;
    RingLogHeader *hdr; // mmapped
    uint8_t *slots_base; // mmapped, start of first slot
};

static size_t calc_file_size(void) {
    size_t header_size = sizeof(RingLogHeader);
    size_t slot_size = sizeof(SlotHeader) + (size_t)RINGLOG_SLOT_BYTES - sizeof(SlotHeader);
    return header_size + (size_t)RINGLOG_MAX_LINES * slot_size;
}

static off_t offset_of_slot(uint32_t index) {
    size_t header_size = sizeof(RingLogHeader);
    size_t slot_size = RINGLOG_SLOT_BYTES;
    return (off_t)(header_size + (size_t)index * slot_size);
}

static int ensure_file_size(int fd, size_t size) {
    struct stat st;
    if (fstat(fd, &st) != 0) return -errno;
    if ((size_t)st.st_size == size) return 0;
    if (ftruncate(fd, (off_t)size) != 0) return -errno;
    return 0;
}

static void zero_range(int fd, off_t off, size_t len) {
    // Use pwrite with a zero buffer to clear range
    static uint8_t zeros[4096];
    size_t remaining = len;
    off_t pos = off;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
        ssize_t w = pwrite(fd, zeros, chunk, pos);
        if (w <= 0) break;
        remaining -= (size_t)w;
        pos += w;
    }
    (void)remaining; // best-effort
}

static int map_file(int fd, size_t file_size, RingLogHeader **out_hdr, uint8_t **out_slots_base) {
    void *addr = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) return -errno;
    *out_hdr = (RingLogHeader *)addr;
    *out_slots_base = (uint8_t *)addr + sizeof(RingLogHeader);
    return 0;
}

static void unmap_file(void *addr, size_t len) {
    if (addr && len) munmap(addr, len);
}

static int init_header_if_needed(int fd, RingLogHeader *hdr) {
    if (hdr->magic != RINGLOG_MAGIC || hdr->version != RINGLOG_VERSION) {
        // initialize new header
        RingLogHeader new_hdr = {0};
        new_hdr.magic = RINGLOG_MAGIC;
        new_hdr.version = RINGLOG_VERSION;
        new_hdr.write_index = 0;
        new_hdr.wrapped = 0;
        // write header
        if (pwrite(fd, &new_hdr, sizeof(new_hdr), 0) != (ssize_t)sizeof(new_hdr)) {
            return -errno;
        }
        // zero all slots
        zero_range(fd, (off_t)sizeof(RingLogHeader), (size_t)RINGLOG_MAX_LINES * RINGLOG_SLOT_BYTES);
        // refresh mapping will reflect zeroed content on next access
    }
    return 0;
}

RingLog *ringlog_open(const char *filepath) {
    int fd = open(filepath, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return NULL;

    size_t file_size = calc_file_size();
    if (ensure_file_size(fd, file_size) != 0) {
        close(fd);
        return NULL;
    }

    RingLogHeader *hdr = NULL; 
    uint8_t *slots_base = NULL;
    if (map_file(fd, file_size, &hdr, &slots_base) != 0) {
        close(fd);
        return NULL;
    }

    if (init_header_if_needed(fd, hdr) != 0) {
        unmap_file(hdr, file_size);
        close(fd);
        return NULL;
    }

    RingLog *log = (RingLog *)calloc(1, sizeof(RingLog));
    if (!log) {
        unmap_file(hdr, file_size);
        close(fd);
        return NULL;
    }
    log->fd = fd;
    log->file_size = file_size;
    log->hdr = hdr;
    log->slots_base = slots_base;
    return log;
}

void ringlog_close(RingLog *log) {
    if (!log) return;
    if (log->hdr) {
        msync(log->hdr, log->file_size, MS_SYNC);
        unmap_file(log->hdr, log->file_size);
    }
    if (log->fd >= 0) close(log->fd);
    free(log);
}

static uint8_t *slot_ptr(RingLog *log, uint32_t index) {
    return log->slots_base + (size_t)index * RINGLOG_SLOT_BYTES;
}

static void write_slot(int fd, uint32_t index, const char *data, size_t len) {
    if (len > (size_t)(RINGLOG_SLOT_BYTES - sizeof(SlotHeader))) {
        len = (size_t)(RINGLOG_SLOT_BYTES - sizeof(SlotHeader));
    }
    SlotHeader sh = { .used_len = (uint16_t)len };
    off_t off = offset_of_slot(index);
    // write header then payload
    pwrite(fd, &sh, sizeof(sh), off);
    if (len > 0) {
        pwrite(fd, data, len, off + sizeof(SlotHeader));
    }
    // zero remaining payload bytes for cleanliness
    size_t total_payload = (size_t)RINGLOG_SLOT_BYTES - sizeof(SlotHeader);
    if (len < total_payload) {
        static uint8_t zeros[4096];
        size_t remaining = total_payload - len;
        off_t pos = off + sizeof(SlotHeader) + (off_t)len;
        while (remaining > 0) {
            size_t chunk = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
            pwrite(fd, zeros, chunk, pos);
            remaining -= chunk;
            pos += (off_t)chunk;
        }
    }
}

int ringlog_append(RingLog *log, const char *line) {
    if (!log || !line) return -EINVAL;

    size_t input_len = strlen(line);
    bool ends_with_nl = input_len > 0 && line[input_len - 1] == '\n';

    char buffer[RINGLOG_SLOT_BYTES];
    size_t to_copy = input_len;
    if (!ends_with_nl) {
        // Reserve a byte for '\n' if space allows
        if (to_copy + 1 > sizeof(buffer)) {
            to_copy = sizeof(buffer) - 1; // leave space for '\n'
        }
        memcpy(buffer, line, to_copy);
        buffer[to_copy] = '\n';
        to_copy += 1;
    } else {
        if (to_copy > sizeof(buffer)) to_copy = sizeof(buffer);
        memcpy(buffer, line, to_copy);
    }

    uint32_t idx = log->hdr->write_index;
    write_slot(log->fd, idx, buffer, to_copy);

    // advance index
    idx += 1;
    if (idx >= RINGLOG_MAX_LINES) {
        idx = 0;
        log->hdr->wrapped = 1;
    }
    log->hdr->write_index = idx;

    // persist header quickly
    pwrite(log->fd, log->hdr, sizeof(*log->hdr), 0);

    return 0;
}

int ringlog_dump(RingLog *log, FILE *out) {
    if (!log || !out) return -EINVAL;

    uint32_t start = 0;
    uint32_t count = 0;
    if (log->hdr->wrapped) {
        start = log->hdr->write_index;
        count = RINGLOG_MAX_LINES;
    } else {
        start = 0;
        count = log->hdr->write_index;
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = (start + i) % RINGLOG_MAX_LINES;
        uint8_t *sp = slot_ptr(log, idx);
        SlotHeader *sh = (SlotHeader *)sp;
        const char *payload = (const char *)(sp + sizeof(SlotHeader));
        if (sh->used_len > 0 && sh->used_len <= (RINGLOG_SLOT_BYTES - sizeof(SlotHeader))) {
            fwrite(payload, 1, sh->used_len, out);
        }
    }
    fflush(out);
    return 0;
}