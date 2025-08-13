#pragma once

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 日志级别索引
#define LOG_LEVEL_TRACE 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_WARN  3
#define LOG_LEVEL_ERROR 4
#define LOG_LEVEL_FATAL 5
#define LOG_LEVEL_OFF   6

// 基础 API
void log_log(int level, const char* file, const char* func, int line, const char* fmt, ...);
int  log_init_config(const char* config_file, size_t buf_size, int type);
void log_uninit(void);

// 固定 128 字节记录的循环日志注册函数
int log_add_rolling_fp(FILE *fp, int level, size_t max_lines);

// 便捷宏
#ifndef LOG_TRACE
#define LOG_TRACE(fmt, ...) log_log(LOG_LEVEL_TRACE, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) log_log(LOG_LEVEL_DEBUG, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...)  log_log(LOG_LEVEL_INFO,  __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...)  log_log(LOG_LEVEL_WARN,  __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) log_log(LOG_LEVEL_ERROR, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#endif
#ifndef LOG_FATAL
#define LOG_FATAL(fmt, ...) log_log(LOG_LEVEL_FATAL, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif