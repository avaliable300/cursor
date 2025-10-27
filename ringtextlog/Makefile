CC := gcc
CFLAGS := -Wall -Wextra -O2 -std=c11

TARGET := ringtext_demo
OBJS := rtextlog.o main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

rtextlog.o: rtextlog.c rtextlog.h
	$(CC) $(CFLAGS) -c rtextlog.c

main.o: main.c rtextlog.h
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean