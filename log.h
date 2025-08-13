#pragma once

#ifndef LOG_H
#define LOG_H

#include "log_config.h"
#include <stdio.h>
#include <stddef.h>



// 日志输出宏（自动填充文件名、函数名、行号）
#define log_debug(...)   log_log(LOG_DEBUG,   __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_info(...)    log_log(LOG_INFO,    __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_warn(...)    log_log(LOG_WARN,    __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_error(...)   log_log(LOG_ERROR,   __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_print(level, ...) log_log(level, __FILE__, __func__, __LINE__, __VA_ARGS__)

// 日志函数声明
// const char* log_level_string(int level);
// void log_set_lock(log_LockFn fn, void* udata);
// void log_set_level(int level);
// void log_set_quiet(bool enable);
// void log_set_use_color(bool enable);
// int log_add_callback(log_LogFn fn, void* udata, int level);
// int log_add_fp(FILE* fp, int level);
void log_log(int level, const char* file, const char* func, int line, const char* fmt, ...);
void log_uninit(void);
int log_init_config(const char* config_file, size_t buf_size, ConfigType type);

#endif // LOGC_H