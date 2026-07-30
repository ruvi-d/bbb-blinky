# Userspace GPIO1_28 blinky for the BeagleBone Black (AM335x, Cortex-A8),
# driving the registers through /dev/mem.
#
#   make          build bin/blinky (expects arm-linux-gnueabi- in $PATH)
#   make clean    remove build artifacts
#
# Statically linked on purpose: the binary then runs on any AM335x rootfs
# regardless of which libc or float ABI it was built with. Override
# CROSS_COMPILE to build with a toolchain matching your target instead.

CROSS_COMPILE ?= arm-linux-gnueabi-
CC = $(CROSS_COMPILE)gcc

BIN = bin

TARGET_FLAGS = -march=armv7-a -mtune=cortex-a8
CFLAGS  = $(TARGET_FLAGS) -std=c11 -Wall -Wextra -O2 -g
LDFLAGS = -static

.PHONY: all clean

all: $(BIN)/blinky

$(BIN):
	mkdir -p $(BIN)

$(BIN)/blinky: main.c | $(BIN)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	$(RM) $(BIN)/blinky
