#include "logc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 定义可用字符集（可根据需要修改）
const char CHARSET[] = "abcdefghijklmnopqrstuvwxyz"
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                       "0123456789"
                       "!@#$%^&*()_-+=<>?";

// 字符集长度
#define CHARSET_LEN (sizeof(CHARSET) - 1)

// 生成指定长度的随机字符串
char* generate_random_string(int length) {
    if (length <= 0) {
        return NULL;
    }
    // 分配内存（包含终止符）
    char* str = (char*)malloc(length + 1);
    if (str == NULL) {
        return NULL;
    }
    // 生成随机字符
    for (int i = 0; i < length; i++) {
        // 从字符集中随机选择一个字符
        int index = rand() % CHARSET_LEN;
        str[i] = CHARSET[index];
    }
    // 添加字符串终止符
    str[length] = '\0';
    return str;
}

// 生成不定长随机字符串（长度在min_len到max_len之间）
char* generate_variable_length_string(int min_len, int max_len) {
    if (min_len <= 0 || max_len < min_len) {
        return NULL;
    }
    // 随机生成长度（包含min_len和max_len）
    int length = min_len + (rand() % (max_len - min_len + 1));
    return generate_random_string(length);
}

int main(void) {
    // 初始化（使用默认配置，控制台输出）
    log_init_config("logger.conf", 0, CONFIG_TYPE_FILE);

    // 初始化随机数种子
    srand((unsigned int)time(NULL));
    
    // 示例：生成5个长度在5到15之间的随机字符串
    const int COUNT = 3456;
    const int MIN_LEN = 10;
    const int MAX_LEN = 25;

    // 输出一些日志
    for (int i = 0; i < COUNT; i++) {
        char* str = generate_variable_length_string(MIN_LEN, MAX_LEN);
        if (str != NULL) {
            log_info("message %d:%s: 这是一条测试日志 with some UTF-8", i+1, str);
            free(str); // 释放内存
        } else {
            printf("字符串%d生成失败\n", i+1);
        }
    }

    log_uninit();
    return 0;
}
