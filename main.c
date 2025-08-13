#include "log.h"
#include "log_config.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    LogConfig cfg;

    // 读取配置文件（malloc-free实现）
    log_load_config("logger.cfg", 0, CONFIG_TYPE_FILE, &cfg);
    log_print_config(&cfg);

    // 设置日志级别
    switch (cfg.log_level) {
        case LOG_TRACE: log_set_level(LOG_TRACE); break;
        case LOG_DEBUG: log_set_level(LOG_DEBUG); break;
        case LOG_INFO:  log_set_level(LOG_INFO);  break;
        case LOG_WARN:  log_set_level(LOG_WARN);  break;
        case LOG_ERROR: log_set_level(LOG_ERROR); break;
        case LOG_FATAL: log_set_level(LOG_FATAL); break;
        case -1:        log_set_quiet(true);      break;
        default: break;
    }

    // 输出目标: console、file、both
    if (cfg.target == LOG_TARGET_FILE || cfg.target == LOG_TARGET_BOTH) {
        FILE *f = fopen(cfg.log_file[0] ? cfg.log_file : "log.log", "a+b");
        if (f) log_add_fp(f, LOG_TRACE);
    }

    // 分段日志（按配置）
    if (cfg.segment_count > 0) {
        // 约束最大段数和内存: 段大小固定 1KB，最大段数 = floor(max_memory_MB*1024/1)
        int max_by_mem = (cfg.max_memory > 0) ? (int)(cfg.max_memory * 1024.0f) : cfg.segment_count;
        if (max_by_mem <= 0) max_by_mem = cfg.segment_count;
        if (cfg.segment_count > max_by_mem) cfg.segment_count = max_by_mem;
        if (cfg.segment_count < 1) cfg.segment_count = 1;

        if (add_segmented_log_from_config("logs", "app", "logger.cfg", LOG_TRACE) != 0) {
            // 如果从文件读取失败，退回到显式段数
            if (add_segmented_log("logs", "app", LOG_TRACE) != 0) {
                // 已有默认3段
            }
        }
    }

    for (int i = 0; i < 300; i++) {
        log_info("cfg seg %03d: The quick brown fox jumps over the lazy dog.", i);
    }

    log_warn("Done. Check logs/ and %s", cfg.log_file);
    return 0;
}