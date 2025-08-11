#include "log.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // Set log level to show everything
    log_set_level(LOG_TRACE);

    // Log to stderr (default stdout callback uses stderr)
    log_info("Logger initialized");

    // Normal text file logging
    FILE *normal = fopen("normal.log", "w+");
    if (normal) {
        log_add_fp(normal, LOG_TRACE);
    } else {
        log_error("Failed to open normal.log");
    }

    // Rolling fixed-size circular logging: 100 records * 128 B = 12.8 KB
    FILE *rolling = fopen("rolling.log", "r+b");
    if (!rolling) {
        rolling = fopen("rolling.log", "w+b");
    }
    if (rolling) {
        if (log_add_rolling_fp(rolling, LOG_TRACE, 100) != 0) {
            log_error("Failed to register rolling logger");
        }
    } else {
        log_error("Failed to open rolling.log");
    }

    // Generate more than 100 lines to demonstrate wrap-around
    for (int i = 0; i < 150; i++) {
        log_info("message %03d: The quick brown fox jumps over the lazy dog.", i);
    }

    log_warn("Done. Check normal.log and rolling.log");

    return 0;
}