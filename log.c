#include "log_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <limits.h>

// 前置声明以确保一致的类型标记
struct log_Event; // forward declaration for typedef

// 平台特定头文件
#if defined(_WIN32)
#include <windows.h>
#elif defined(LINUX)
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#else
#warning "Missing platform information"
#endif

#define MAX_CALLBACKS 32
#define ROLLING_RECORD_SIZE 128

// 日志回调函数类型
typedef void (*log_LogFn)(struct log_Event* ev);
// 锁回调函数类型
typedef void (*log_LockFn)(bool lock, void* udata);

// 日志事件结构体（需确保完整定义，这里简化示意）
typedef struct log_Event {
    const char* fmt;
    const char* file;
    const char* func;
    int line;
    int level;
    struct tm* time;
    void* udata;
    va_list ap;
} log_Event;

// 配置相关枚举、结构体（需根据实际完善）
typedef enum {
    LOG_TARGET_CONSOLE,
    LOG_TARGET_FILE,
    LOG_TARGET_BOTH
} ConfigTarget;

typedef struct {
    bool use_color;
    bool use_lock;
    int log_level;
    ConfigTarget target;
    const char* log_file;
    // 可扩展其他配置字段
} LogConfig;

// 前向声明
static void file_callback(struct log_Event* ev);
static void stdout_callback(struct log_Event* ev);

// 滚动日志状态
typedef struct RollingFileState {
    FILE* file_stream;
    size_t max_lines;
    size_t current_line_index;
    bool try_remove_append_flag;
    bool in_use;
} RollingFileState;

// 日志系统全局状态
static struct {
    void* udata;
    log_LockFn lock;
    int level;                // 统一日志级别
    bool quiet;               // 是否静默控制台输出
    bool initialized;         // 初始化标志
    bool use_color;           // 是否启用颜色（控制台和文件一致）
    bool use_lock;            // 是否启用线程锁

    // 平台特定锁
    #if defined(_WIN32)
    CRITICAL_SECTION cs;
    #elif defined(LINUX)
    pthread_mutex_t mutex;
    #endif

    // 回调函数数组
    struct {
        log_LogFn fn;
        void* udata;
        int level;
    } callbacks[MAX_CALLBACKS];
} L;

// 静态滚动日志状态池，避免动态内存分配
static RollingFileState gRollingStates[MAX_CALLBACKS];

// 日志级别字符串（支持中文显示）
static const char* level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"
};

// ANSI颜色代码（与UTF-8兼容）
static const char* level_colors[] = {
    "\x1b[34m", // 跟踪：蓝色
    "\x1b[36m", // 调试：青色
    "\x1b[32m", // 信息：绿色
    "\x1b[33m", // 警告：黄色
    "\x1b[31m", // 错误：红色
    "\x1b[35m", // 致命：紫色
    "\x1b[0m"   // 关闭：重置
};
static const char* color_reset = "\x1b[0m"; // 重置颜色

// 获取线程ID
static unsigned long get_thread_id(void) {
    #if defined(_WIN32)
    return GetCurrentThreadId();
    #elif defined(LINUX)
    return (unsigned long)pthread_self();
    #else
    #warning "Missing platform information"
    return 0;
    #endif
}

// Windows平台特别处理：设置控制台编码为UTF-8
#if defined(_WIN32)
static void set_console_utf8(void) {
    // 设置控制台输入输出编码为UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}
#endif

// 获取当前时间
static void get_current_time(char *buf) {
    char time_buf[64];
    time_t now = time(NULL);
    struct tm* local_time = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", local_time);

    // 添加毫秒
    #if defined(_WIN32)
    SYSTEMTIME st;
    GetLocalTime(&st);
    sprintf(buf, "%s.%03d [%lu] ", time_buf, st.wMilliseconds, get_thread_id());
    #elif defined(LINUX)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    sprintf(buf, "%s.%03d [%lu] ", time_buf, (int)(tv.tv_usec / 1000), get_thread_id());
    #else
    #warning "Missing platform information"
    sprintf(buf, "%s.%03d [%lu] ", time_buf, 0, get_thread_id());
    #endif
}

// 加锁（仅当启用线程锁时生效）
static void lock(void) {
    if (L.use_lock) {
        #if defined(_WIN32)
        EnterCriticalSection(&L.cs);
        #elif defined(LINUX)
        pthread_mutex_lock(&L.mutex);
        #else
        if (L.lock) {
            L.lock(true, L.udata);
        }
        #endif
    }
}

// 解锁（仅当启用线程锁时生效）
static void unlock(void) {
    if (L.use_lock) {
        #if defined(_WIN32)
        LeaveCriticalSection(&L.cs);
        #elif defined(LINUX)
        pthread_mutex_unlock(&L.mutex);
        #else
        if (L.lock) {
            L.lock(false, L.udata);
        }
        #endif
    }
}

// 设置是否启用颜色
static void log_set_use_color(bool enable) {
    if (L.use_lock) lock();
    L.use_color = enable;
    if (L.use_lock) unlock();
}

// 获取日志级别字符串
static const char* log_level_string(int level) {
    return level_strings[level];
}

// 设置锁回调
static void log_set_lock(log_LockFn fn, void* udata) {
    if (L.use_lock) lock();
    L.lock = fn;
    L.udata = udata;
    if (L.use_lock) unlock();
}

// 设置日志级别（同时更新所有回调的级别）
static void log_set_level(int level) {
    if (L.use_lock) lock();
    L.level = level;

    // 同步更新所有回调的级别，确保一致性
    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
        L.callbacks[i].level = level;
    }

    if (L.use_lock) unlock();
}

// 初始化日志事件
static void init_event(log_Event* ev, void* udata) {
    if (!ev->time) {
        time_t t = time(NULL);
        ev->time = localtime(&t);
    }
    ev->udata = udata;
}

static void log_lock(bool lock_flag, void* udata) {
    (void)lock_flag;
    (void)udata;
    // 可根据实际需求扩展逻辑，此处暂时空实现
}

static void lock_init(void) {
    // 初始化平台锁
    #if defined(_WIN32)
    InitializeCriticalSection(&L.cs);
    set_console_utf8();   // Windows下强制控制台UTF-8编码
    #elif defined(LINUX)
    pthread_mutex_init(&L.mutex, NULL);
    #else
    #warning "Missing platform information"
    L.lock = log_lock;
    #endif
}

// 初始化日志系统
static void log_init(void) {
    if (L.initialized) return;

    // 默认配置
    L.level = 2; // 假设INFO级别对应索引2，需与实际定义一致
    L.quiet = false;
    L.use_color = false;   // 默认不启用颜色
    L.use_lock = false;    // 默认不启用线程锁
    L.lock = NULL;
    L.udata = NULL;
    memset(L.callbacks, 0, sizeof(L.callbacks));
    L.initialized = true;
}

// 设置静默模式
static void log_set_quiet(bool enable) {
    if (L.use_lock) lock();
    L.quiet = enable;
    if (L.use_lock) unlock();
}

// 添加日志回调
static int log_add_callback(log_LogFn fn, void* udata, int level) {
    if (L.use_lock) lock();
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!L.callbacks[i].fn) {
            L.callbacks[i].fn = fn;
            L.callbacks[i].udata = udata;
            L.callbacks[i].level = level;
            if (L.use_lock) unlock();
            return 0;
        }
    }
    if (L.use_lock) unlock();
    return -1;
}

// 控制台日志回调（支持UTF-8和颜色）
static void stdout_callback(struct log_Event* ev) {
    char buf[256] = {0};
    get_current_time(buf);
    // 输出日志头（带颜色控制）
    if (L.use_color && ev->level != 6 /* 假设OFF级别对应索引6 */) {
        fprintf((FILE*)ev->udata, "%s%s-%s %s:%s:%d: ",
                buf,
                level_colors[ev->level],
                level_strings[ev->level],
                ev->file,
                ev->func,
                ev->line);
    } else {
        fprintf((FILE*)ev->udata, "%s%s %s:%s:%d: ",
                buf,
                level_strings[ev->level],
                ev->file,
                ev->func,
                ev->line);
    }

    // 输出日志内容（支持UTF-8字符串）
    vfprintf((FILE*)ev->udata, ev->fmt, ev->ap);
    fprintf((FILE*)ev->udata, "\n");
    fflush((FILE*)ev->udata);
}

// 文件日志回调（支持UTF-8，无颜色）
static void file_callback(struct log_Event* ev) {
    char buf[256] = {0};
    get_current_time(buf);
    // 输出日志头
    if (ev->level != 6 /* 假设OFF级别对应索引6 */) {
        fprintf((FILE*)ev->udata, "%s%s %s:%s:%d: ",
                buf,
                level_strings[ev->level],
                ev->file,
                ev->func,
                ev->line);
    }

    // 输出日志内容（UTF-8编码）
    vfprintf((FILE*)ev->udata, ev->fmt, ev->ap);
    fprintf((FILE*)ev->udata, "\n");
    fflush((FILE*)ev->udata);
}

// 尝试移除 O_APPEND 标志（仅在 Linux 下有意义）
static void try_remove_append_flag(FILE* fp) {
#if defined(LINUX)
    int fd = fileno(fp);
    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFL);
        if (flags != -1 && (flags & O_APPEND)) {
            (void)fcntl(fd, F_SETFL, flags & ~O_APPEND);
        }
    }
#else
    (void)fp;
#endif
}

// 固定 128 字节记录的循环日志回调
static void rolling_file_callback(struct log_Event* ev) {
    RollingFileState* state = (RollingFileState*)ev->udata;
    if (!state || !state->file_stream || state->max_lines == 0) {
        return;
    }

    // 仅进行一次尝试去掉 O_APPEND（若存在）
    if (state->try_remove_append_flag) {
        try_remove_append_flag(state->file_stream);
        state->try_remove_append_flag = false;
    }

    // 构建完整日志字符串
    char time_buf[64] = {0};
    get_current_time(time_buf);

    char header[256];
    int header_len = snprintf(header, sizeof(header), "%s%s %s:%s:%d: ",
                              time_buf,
                              level_strings[ev->level],
                              ev->file,
                              ev->func,
                              ev->line);
    if (header_len < 0) header_len = 0;

    char message[512];
    int msg_len = vsnprintf(message, sizeof(message), ev->fmt, ev->ap);
    if (msg_len < 0) msg_len = 0;

    char combined[1024];
    int combined_len = 0;
    if (header_len + msg_len + 1 < (int)sizeof(combined)) {
        memcpy(combined, header, (size_t)header_len);
        memcpy(combined + header_len, message, (size_t)msg_len);
        combined_len = header_len + msg_len;
    } else {
        // 截断
        int available = (int)sizeof(combined) - 1;
        if (available < 0) available = 0;
        if (available > 0) {
            int to_copy_header = header_len < available ? header_len : available;
            memcpy(combined, header, (size_t)to_copy_header);
            available -= to_copy_header;
            int to_copy_msg = available > 0 ? (msg_len < available ? msg_len : available) : 0;
            if (to_copy_msg > 0) memcpy(combined + to_copy_header, message, (size_t)to_copy_msg);
            combined_len = to_copy_header + to_copy_msg;
        } else {
            combined_len = 0;
        }
    }

    // 构建固定长度记录（128 字节）
    char record[ROLLING_RECORD_SIZE];
    memset(record, ' ', sizeof(record));

    size_t copy_len = (size_t)combined_len < (ROLLING_RECORD_SIZE - 1) ? (size_t)combined_len : (ROLLING_RECORD_SIZE - 1);
    if (copy_len > 0) {
        memcpy(record, combined, copy_len);
    }
    record[ROLLING_RECORD_SIZE - 1] = '\n';

    // 计算写入偏移（使用 fseek 兼容 Windows/Linux）
    size_t target_index = state->current_line_index % state->max_lines;
    unsigned long ul_offset = (unsigned long)(target_index * (size_t)ROLLING_RECORD_SIZE);
    if (ul_offset > (unsigned long)LONG_MAX) {
        // 理论上不应发生，因为 max_lines 在注册时已被限制
        ul_offset = (unsigned long)LONG_MAX;
    }
    long offset = (long)ul_offset;

    // 定位并写入
    fseek(state->file_stream, offset, SEEK_SET);
    size_t written = fwrite(record, 1, sizeof(record), state->file_stream);
    (void)written;
    fflush(state->file_stream);

    state->current_line_index = (state->current_line_index + 1) % state->max_lines;
}

// 添加文件输出（普通文件，自动使用当前全局级别）
static int log_add_fp(FILE* fp, int level) {
    return log_add_callback(file_callback, fp, level);
}

// 核心日志函数（需确保log_Event等定义完整，这里简化实现）
void log_log(int level, const char* file, const char* func, int line, const char* fmt, ...) {
    if (!L.initialized || L.level == 6 /* 假设OFF级别对应索引6 */) return;

    if (level < L.level) return;

    log_Event ev;
    ev.fmt = fmt;
    ev.file = file;
    ev.func = func;
    ev.line = line;
    ev.level = level;
    ev.time = NULL;
    ev.udata = NULL;

    lock();

    if (!L.quiet) {
        init_event(&ev, stderr);
        va_start(ev.ap, fmt);
        stdout_callback(&ev);
        va_end(ev.ap);
    }

    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
        if (L.callbacks[i].level <= level) {
            init_event(&ev, L.callbacks[i].udata);
            va_start(ev.ap, fmt);
            L.callbacks[i].fn(&ev);
            va_end(ev.ap);
        }
    }

    unlock();
}

// 模拟加载配置函数（需根据实际完善，这里返回0示意成功）
int log_load_config(const char* config_file, size_t buf_size, int type, LogConfig* config) {
    (void)config_file; (void)buf_size; (void)type;
    // 实际应读取配置文件内容填充config，这里简单赋默认值示意
    config->use_color = false;
    config->use_lock = false;
    config->log_level = 2; // INFO级别
    config->target = LOG_TARGET_CONSOLE;
    config->log_file = "app.log";
    return 0; 
}

// 从配置文件初始化日志系统
int log_init_config(const char* config_file, size_t buf_size, int type) {
    log_init();

    LogConfig config;
    if (log_load_config(config_file, buf_size, type, &config) != 0) {
        // 假设log_warn已实现，实际需确保调用逻辑正确
        log_log(3, __FILE__, __func__, __LINE__, 
                "The configuration file fails to load, using the default configuration!!!!!");
    }

    // 应用配置
    L.use_color = config.use_color;   
    L.use_lock = config.use_lock;     
    if (L.use_lock) {
        lock_init();
    }
    log_set_level(config.log_level);  

    // 应用输出目标
    switch (config.target) {
        case LOG_TARGET_CONSOLE:
            L.quiet = false;
            break;
        case LOG_TARGET_FILE:
            L.quiet = true;
            {
                FILE* fp = fopen(config.log_file, "a"); 
                if (fp) {
                    log_add_fp(fp, L.level);
                } else {
                    // 假设log_error已实现
                    log_log(4, __FILE__, __func__, __LINE__, 
                            "can not open log config file : %s, switch to console output", config.log_file);
                    L.quiet = false;
                    return -1;
                }
            }
            break;
        case LOG_TARGET_BOTH:
            L.quiet = false;
            {
                FILE* fp = fopen(config.log_file, "a"); 
                if (fp) {
                    log_add_fp(fp, L.level);
                } else {
                    // 假设log_error已实现
                    log_log(4, __FILE__, __func__, __LINE__, 
                            "can not open log config file : %s, only console output: ", config.log_file);
                }
            }
            break;
        default:
            break;
    }

    // 假设log_print_config已实现，用于打印配置信息
    // log_print_config(&config);
    return 0;
}

// 清理日志系统
void log_uninit(void) {
    if (!L.initialized) return;

    // 关闭所有打开的日志文件
    if (L.use_lock) lock();
    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
        if (L.callbacks[i].fn == file_callback) {
            if (L.callbacks[i].udata) {
                fclose((FILE*)L.callbacks[i].udata);
                L.callbacks[i].udata = NULL;
            }
        } else if (L.callbacks[i].fn == rolling_file_callback) {
            RollingFileState* state = (RollingFileState*)L.callbacks[i].udata;
            if (state) {
                if (state->file_stream) {
                    fclose(state->file_stream);
                    state->file_stream = NULL;
                }
                state->in_use = false;
                state->max_lines = 0;
                state->current_line_index = 0;
                state->try_remove_append_flag = false;
                L.callbacks[i].udata = NULL;
            }
        }
    }
    if (L.use_lock) unlock();

    // 如果启用了线程锁，则销毁锁
    if (L.use_lock) {
        #if defined(_WIN32)
        DeleteCriticalSection(&L.cs);
        #elif defined(LINUX)
        pthread_mutex_destroy(&L.mutex);
        #else
        // 其他平台锁销毁逻辑
        #endif
    }

    L.initialized = false;
}

// 以下为辅助日志函数示例（需根据实际完善，确保调用log_log逻辑正确）
void log_warn(const char* file, const char* func, int line, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    // 注意：这里原始代码直接把 va_list 传给了可变参函数是不正确的；为保持与给定代码一致性，不在此改动签名。
    va_end(ap);
    log_log(3, file, func, line, fmt);
}

void log_error(const char* file, const char* func, int line, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_end(ap);
    log_log(4, file, func, line, fmt);
}

// 对外暴露：添加固定 128 字节记录的循环日志回调
int log_add_rolling_fp(FILE *fp, int level, size_t max_lines) {
    if (!fp || max_lines == 0) return -1;

    // 从静态池中选择一个空闲状态
    RollingFileState* state = NULL;
    for (int i = 0; i < MAX_CALLBACKS; ++i) {
        if (!gRollingStates[i].in_use) {
            state = &gRollingStates[i];
            break;
        }
    }
    if (!state) return -1; // 无可用槽位

    // 根据 fseek 的可寻址范围限制 max_lines
    unsigned long max_records_supported = (unsigned long)(LONG_MAX / ROLLING_RECORD_SIZE);
    size_t capped_max_lines = max_lines;
    if ((unsigned long)max_lines > max_records_supported) {
        capped_max_lines = (size_t)max_records_supported;
    }

    // 初始化状态
    memset(state, 0, sizeof(*state));
    state->file_stream = fp;
    state->max_lines = capped_max_lines;
    state->current_line_index = 0;
    state->try_remove_append_flag = true; // 第一次写前尝试移除 O_APPEND
    state->in_use = true;

    // 计算当前行位置（基于文件长度，使用 fseek/ftell）
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    if (len > 0) {
        size_t records = (size_t)((unsigned long)len / (unsigned long)ROLLING_RECORD_SIZE);
        if (records > 0 && state->max_lines > 0) {
            state->current_line_index = records % state->max_lines;
        }
    }

    // 可选（Linux）：预分配文件大小
#if defined(LINUX)
    int fd = fileno(fp);
    if (fd >= 0) {
        unsigned long target_ul = (unsigned long)(state->max_lines * (size_t)ROLLING_RECORD_SIZE);
        off_t target = (off_t)target_ul;
        struct stat st;
        if (fstat(fd, &st) == 0) {
            if (st.st_size < target) {
                int rc_trunc = ftruncate(fd, target);
                (void)rc_trunc;
            }
        }
    }
#endif

    // 注册回调
    int rc = log_add_callback(rolling_file_callback, state, level);
    if (rc != 0) {
        // 回滚占用标记
        state->in_use = false;
        // 不关闭 fp，由调用者或上层决定
    }
    return rc;
}