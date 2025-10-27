#ifndef RINGTXT_H
#define RINGTXT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Append one logical line (variable-length) to a plain text file.
 * If the file has fewer than 100 lines, append.
 * If it already has 100 or more lines, drop the oldest line and append the new one,
 * rewriting the file atomically (via temp file + rename).
 * Returns 0 on success, negative errno on failure.
 */
int ringtxt_append_line(const char *path, const char *line);

#ifdef __cplusplus
}
#endif

#endif /* RINGTXT_H */