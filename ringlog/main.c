#include "ringlog.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/workspace/ringlog/log.bin";
    RingLog *log = ringlog_open(path);
    if (!log) {
        fprintf(stderr, "Failed to open ring log at %s\n", path);
        return 1;
    }

    // Append 120 lines to demonstrate wrap-around
    for (int i = 0; i < 120; ++i) {
        char buf[256];
        snprintf(buf, sizeof(buf), "line %03d: this is a demo line\n", i);
        ringlog_append(log, buf);
    }

    printf("Dumping log (should contain last 100 lines in order):\n");
    ringlog_dump(log, stdout);

    ringlog_close(log);
    return 0;
}