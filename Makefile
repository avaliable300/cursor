CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2
LDFLAGS =
TARGET = app
OBJS = main.o log.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) *.log

.PHONY: all clean