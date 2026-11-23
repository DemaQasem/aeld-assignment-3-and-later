CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g

TARGET = aesdsocket
SRCS = aesdsocket.c

all: $(TARGET)

default: all

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all default clean
