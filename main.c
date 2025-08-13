#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

int main(void) {
    log_set_level(LOG_TRACE);
    log_set_quiet(false);
    FILE *fp = fopen("log.txt", "w");
    log_add_fp(fp, LOG_TRACE);
    log_info("Opened log file %s", "log.txt");
    log_debug("This is a debug message");
    log_warn("This is a warning message");
    log_error("This is an error message");
    fclose(fp);
    return 0;
}
