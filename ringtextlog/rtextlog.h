#ifndef RTEXTLOG_H
#define RTEXTLOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTEXT_MAX_LINES   100
#define RTEXT_LINE_BYTES  512   /* including trailing '\n'; content padded with spaces */

/* Opaque type */
typedef struct RTextLog RTextLog;

/* Open or create a text ring log at path.
 * Layout: line 0 is a text header with index, lines 1..100 are fixed-width text lines.
 */
RTextLog *rtext_open(const char *path);

void rtext_close(RTextLog *log);

/* Append one formatted line. A trailing '\n' is ensured; content longer than line capacity is truncated. */
int rtext_appendf(RTextLog *log, const char *fmt, ...);
int rtext_appendfv(RTextLog *log, const char *fmt, va_list ap);

/* Dump current logical content to a stream (for debugging/demo) */
int rtext_dump(RTextLog *log, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* RTEXTLOG_H */