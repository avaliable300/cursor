#ifndef RINGLOG_H
#define RINGLOG_H

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configurable parameters
#define RINGLOG_MAX_LINES 100
#define RINGLOG_SLOT_BYTES 1024  // max bytes per line including trailing "\n" if present; content is truncated if longer

// Opaque type
typedef struct RingLog RingLog;

// Create or open a ring log bound to a file path. The log uses fixed-size slots to allow in-place overwrite.
// If the file doesn't exist, it will be created and initialized. If it exists, metadata will be validated.
RingLog *ringlog_open(const char *filepath);

// Close the log. Flushes and closes the underlying file.
void ringlog_close(RingLog *log);

// Append one line (string) to the log. A trailing newline will be added if missing (space permitting).
// Data longer than RINGLOG_SLOT_BYTES-1 will be truncated.
// Returns 0 on success, negative errno-like codes on failure.
int ringlog_append(RingLog *log, const char *line);

// Dump current file content (in logical order) to a stream, primarily for debugging/demo.
int ringlog_dump(RingLog *log, FILE *out);

#ifdef __cplusplus
}
#endif

#endif // RINGLOG_H