#include "rtextlog.h"

#include <stdio.h>
#include <time.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/workspace/ringtextlog/ring.txt";
    RTextLog *log = rtext_open(path);
    if (!log) {
        fprintf(stderr, "Failed to open %s\n", path);
        return 1;
    }

    /* Append 120 lines */
    for (int i = 0; i < 120; ++i) {
        rtext_appendf(log, "line %03d: demo log", i);
    }

    printf("Dump (should show last 100 lines in order):\n");
    rtext_dump(log, stdout);

    rtext_close(log);
    return 0;
}