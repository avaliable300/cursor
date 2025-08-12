#include "log.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    log_set_level(LOG_TRACE);

    // Enable rolling log via single API, without changing other structures
    if (add_rolling_log("rolling.log", 100, LOG_TRACE) != 0) {
        log_error("Failed to enable rolling log");
    }

    // Optional: also log to a normal file
    FILE *normal = fopen("normal.log", "w+");
    if (normal) {
        log_add_fp(normal, LOG_TRACE);
    }

    for (int i = 0; i < 150; i++) {
        log_info("message %03d: The quick brown fox jumps over the lazy dog.", i);
    }

    log_warn("Done. Check normal.log and rolling.log");

    return 0;
}