#include "log.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    log_set_level(LOG_TRACE);

    // Example config: write a number like 5 into logs.cfg to set segment count (capped to 16)
    FILE *cfg = fopen("logs.cfg", "w");
    if (cfg) { fprintf(cfg, "5\n"); fclose(cfg); }

    if (add_segmented_log_from_config("logs", "app", "logs.cfg", LOG_TRACE) != 0) {
        log_error("Failed to enable segmented log from config");
    }

    for (int i = 0; i < 1200; i++) {
        log_info("seg %04d: The quick brown fox jumps over the lazy dog.", i);
    }

    log_warn("Done. Check folder logs/ with N×1KB files as per logs.cfg");

    return 0;
}