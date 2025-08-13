#include "log_config.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // 初始化（使用默认配置，控制台输出）
    log_init_config(NULL, 0, 0);

    // 打开循环日志文件（读写，若不存在则创建）
    FILE* rfp = fopen("rolling.log", "w+b");
    if (!rfp) {
        perror("open rolling.log");
        return 1;
    }

    // 注册循环日志：每条 128 字节，最多 100 条
    if (log_add_rolling_fp(rfp, LOG_LEVEL_TRACE, 100) != 0) {
        fprintf(stderr, "failed to add rolling logger\n");
        fclose(rfp);
        return 1;
    }

    // 输出一些日志
    for (int i = 0; i < 150; ++i) {
        LOG_INFO("message %d: 这是一条测试日志 with some UTF-8 😀", i);
    }

    log_uninit();
    return 0;
}