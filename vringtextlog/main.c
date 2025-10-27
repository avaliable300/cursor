#include "ringtxt.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/workspace/vringtextlog/vring.txt";

    /* Write 120 lines with variable lengths */
    for (int i = 0; i < 120; ++i) {
        char buf[1024];
        if (i % 3 == 0) {
            snprintf(buf, sizeof(buf), "[%d] short", i);
        } else if (i % 3 == 1) {
            snprintf(buf, sizeof(buf), "[%d] medium length line with some words", i);
        } else {
            snprintf(buf, sizeof(buf), "[%d] long line: %s %s %s %s %s %s %s", i,
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                     "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                     "ccccccccccccccccccccccccccccccccc",
                     "ddddddddddddddddddddddddddddddddd",
                     "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
                     "fffffffffffffffffffffffffffffffff",
                     "ggggggggggggggggggggggggggggggggg");
        }
        int rc = ringtxt_append_line(path, buf);
        if (rc != 0) {
            fprintf(stderr, "append failed at %d: %d\n", i, rc);
            return 1;
        }
    }

    printf("Last 100 lines in %s:\n", path);
    /* Print file to stdout */
    FILE *fp = fopen(path, "r");
    if (!fp) { perror("fopen"); return 1; }
    char line[4096];
    while (fgets(line, sizeof(line), fp)) fputs(line, stdout);
    fclose(fp);
    return 0;
}