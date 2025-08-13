#ifndef LOG_CONFIG_H
#define LOG_CONFIG_H

#include <stdbool.h>
#include <stdio.h>

// 日志输出目标
typedef enum {
    LOG_TARGET_CONSOLE,
    LOG_TARGET_FILE,
    LOG_TARGET_BOTH
} LogTarget;

// 日志级别
typedef enum {
    LOG_INFO,
    LOG_DEBUG,
    LOG_WARN,
    LOG_ERROR,
    LOG_OFF
} LogLevel;

// 定义类型
typedef enum {
    CONFIG_TYPE_FILE,   // 表示 char* 是文件路径
    CONFIG_TYPE_BUF     // 表示 char* 是内存缓冲区
} ConfigType;

// 日志配置结构体（新增颜色和锁的开关）
typedef struct {
    LogLevel log_level;     // 日志级别
    LogTarget target;       // 输出目标
    char log_file[256];     // 日志文件路径
    bool use_color;         // 是否启用颜色（控制台和文件一致）
    bool use_lock;          // 是否启用线程锁
    size_t max_line;        // 最大行数
    float max_memory;       // 最大内存，单位MB
    int segment_count;      // log文件分割数
} LogConfig;

// 统一接口：根据类型自动选择解析方式
int log_load_config(const char* data, size_t buf_size, ConfigType type, LogConfig* config);

// 打印配置信息
void log_print_config(const LogConfig* config);

#endif // LOG_CONFIG_H