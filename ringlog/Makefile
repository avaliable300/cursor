CC := gcc
CFLAGS := -Wall -Wextra -O2 -std=c11
LDFLAGS := 

TARGET := ringlog_demo
OBJS := ringlog.o main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

ringlog.o: ringlog.c ringlog.h
	$(CC) $(CFLAGS) -c ringlog.c

main.o: main.c ringlog.h
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean