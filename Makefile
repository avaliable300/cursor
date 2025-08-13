# 编译器
CC = gcc
# 编译选项，C99 标准、开启警告、调试信息
CFLAGS = -std=c99 -Wall -Wextra -g -DLOG_USE_COLOR
# 可执行文件名称
TARGET = log_demo  
# 源文件列表
SRCS = log.c main.c  

# 默认目标，编译可执行文件
all: $(TARGET)

# 链接生成可执行文件
$(TARGET): $(SRCS:.c=.o)
	$(CC) $(CFLAGS) $^ -o $@

# 编译源文件为目标文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理生成的目标文件和可执行文件
clean:
	rm -f $(TARGET) *.o 

# 伪目标，避免目录中有同名文件时影响功能
.PHONY: all clean
