CC := gcc
CFLAGS := -Wall -Wextra -O2 -std=c11

TARGET := vringtext_demo
OBJS := ringtxt.o main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

ringtxt.o: ringtxt.c ringtxt.h
	$(CC) $(CFLAGS) -c ringtxt.c

main.o: main.c ringtxt.h
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean