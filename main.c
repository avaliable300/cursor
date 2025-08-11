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

    // Only use log_info/log_debug etc.; rolling logger is internal and automatic

    for (int i = 0; i < 150; i++) {
        log_info("message %03d: The quick brown fox jumps over the lazy dog.", i);
    }

    log_warn("Done. Check normal.log and rolling.log");

    return 0;
}