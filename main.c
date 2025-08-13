#include "log.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    log_set_level(LOG_TRACE);

    // Segmented logs: create dir and write 3 files x 1KB rotating
    if (add_segmented_log("logs", "app", LOG_TRACE) != 0) {
        log_error("Failed to enable segmented log");
    }

    // Optional also normal file log
    FILE *normal = fopen("normal.log", "w+");
    if (normal) {
        log_add_fp(normal, LOG_TRACE);
    }

    for (int i = 0; i < 300; i++) {
        log_info("seg %03d: The quick brown fox jumps over the lazy dog %d.", i, i);
    }

    log_warn("Done. Check folder logs/ with app.1.log, app.2.log, app.3.log");

    return 0;
}