#include "log_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void remove_spaces_inplace(char *s) {
    char *w = s;
    for (char *r = s; *r; ++r) {
        if (*r != ' ') *w++ = *r;
    }
    *w = '\0';
}

static bool parse_boolean(const char* value) {
    return strcmp(value, "true") == 0;
}

static void log_default_config(LogConfig* config) {
    config->log_level = LOG_INFO; // LOG_INFO
    config->target = LOG_TARGET_FILE;
    strcpy(config->log_file,"./log_default.log");
    config->use_color = false;
    config->use_lock = false;
    config->max_line = 1000;
    config->max_memory = 1.0f;
    config->segment_count = 3;
}

static void parse_log_config_line(char* line, LogConfig* config) {
    // 去除行尾换行
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
    if (line[0] == '#' || line[0] == '\0') return;

    char key[64] = {0}, value[192] = {0};
    if (sscanf(line, "%63[^=]=%191[^\n\r]", key, value) != 2) return;

    remove_spaces_inplace(key);
    remove_spaces_inplace(value);

    if (strcmp(key, "level") == 0) {
        if (strcmp(value, "trace") == 0) config->log_level = LOG_TRACE;       // LOG_TRACE
        else if (strcmp(value, "debug") == 0) config->log_level = LOG_DEBUG;  // LOG_DEBUG
        else if (strcmp(value, "info") == 0)  config->log_level = LOG_INFO;  // LOG_INFO
        else if (strcmp(value, "warn") == 0)  config->log_level = LOG_WARN;  // LOG_WARN
        else if (strcmp(value, "error") == 0) config->log_level = LOG_ERROR;  // LOG_ERROR
        else if (strcmp(value, "fatal") == 0) config->log_level = LOG_FATAL;  // LOG_FATAL
        else if (strcmp(value, "off") == 0)   config->log_level = OFF; // OFF
    } else if (strcmp(key, "target") == 0) {
        if (strcmp(value, "console") == 0) config->target = LOG_TARGET_CONSOLE;
        else if (strcmp(value, "file") == 0) config->target = LOG_TARGET_FILE;
        else if (strcmp(value, "both") == 0) config->target = LOG_TARGET_BOTH;
    } else if (strcmp(key, "log_file") == 0) {
        strncpy(config->log_file, value, sizeof(config->log_file) - 1);
        config->log_file[sizeof(config->log_file) - 1] = '\0';
    } else if (strcmp(key, "use_color") == 0) {
        config->use_color = parse_boolean(value);
    } else if (strcmp(key, "use_lock") == 0) {
        config->use_lock = parse_boolean(value);
    } else if (strcmp(key, "max_line") == 0) {
        long v = strtol(value, NULL, 10);
        if (v > 0) config->max_line = (size_t)v;
    } else if (strcmp(key, "max_memory") == 0) {
        float v = (float)strtod(value, NULL);
        if (v > 0) config->max_memory = v;
    } else if (strcmp(key, "segment_count") == 0) {
        int v = (int)strtol(value, NULL, 10);
        if (v > 0) config->segment_count = v;
    }
}

static int log_load_config_file(const char* config_file, LogConfig* config) {
    log_default_config(config);
    FILE* file = fopen(config_file, "r");
    if (!file) {
        fprintf(stderr, "warning: cannot open config file: %s, using defaults\n", config_file);
        return -1;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        parse_log_config_line(line, config);
    }
    fclose(file);
    return 0;
}

static int log_load_config_buf(const char* buf, size_t buf_size, LogConfig* config) {
    log_default_config(config);
    if (!buf || buf_size == 0) {
        fprintf(stderr, "warning: buf is NULL, using defaults\n");
        return -1;
    }
    // 只读不修改原缓冲，按行扫描（无 malloc）
    size_t i = 0; char line[256]; size_t pos = 0;
    while (i < buf_size) {
        char c = buf[i++];
        if (c == '\n' || c == '\r' || pos >= sizeof(line) - 1) {
            line[pos] = '\0';
            if (pos > 0) parse_log_config_line(line, config);
            pos = 0;
            continue;
        }
        line[pos++] = c;
    }
    if (pos > 0) { line[pos] = '\0'; parse_log_config_line(line, config); }
    return 0;
}

int log_load_config(const char* data, size_t buf_size, ConfigType type, LogConfig* config) {
    if (type == CONFIG_TYPE_FILE) {
        return log_load_config_file(data, config);
    } else {
        return log_load_config_buf(data, buf_size, config);
    }
}

void log_print_config(const LogConfig* config) {
    const char *level_str = "UNKNOWN";
    switch (config->log_level) {
        case 0: level_str = "TRACE"; break;
        case 1: level_str = "DEBUG"; break;
        case 2: level_str = "INFO"; break;
        case 3: level_str = "WARN"; break;
        case 4: level_str = "ERROR"; break;
        case 5: level_str = "FATAL"; break;
        case 6: level_str = "OFF"; break;
        default: break;
    }
    static const char* target_strs[] = {"CONSOLE", "FILE", "CONSOLE AND FILE"};

    printf("LOG CONFIG:\n");
    printf("  level:  %s\n", level_str);
    printf("  target: %s\n", target_strs[config->target]);
    printf("  log_path: %s\n", config->log_file);
    printf("  use_color: %s\n", config->use_color ? "true" : "false");
    printf("  use_lock: %s\n", config->use_lock ? "true" : "false");
    printf("  max_line: %zu\n", config->max_line);
    printf("  max_memory: %.2f MB\n", config->max_memory);
    printf("  segment_count: %d\n", config->segment_count);
}