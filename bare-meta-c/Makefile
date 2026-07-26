# Baremetal GPIO1_28 blinky for the BeagleBone Black (AM335x, Cortex-A8).
# C port of ../bare-meta-asm.
#
#   make          build everything (expects arm-linux-gnueabi- in $PATH)
#   make clean    remove build artifacts
#
# Artifacts land in bin/ :
#   blinky.elf    JTAG upload / debugging
#   blinky.bin    peripheral boot (UART / Ethernet / USB) and XIP
#   blinky.MLO    memory boot from uSD / eMMC / SPI
#   blinky.img    raw image to dd straight onto a uSD card

CROSS_COMPILE ?= arm-linux-gnueabi-
CC       = $(CROSS_COMPILE)gcc
OBJCOPY  = $(CROSS_COMPILE)objcopy

BIN   = bin
TOOLS = tools

TARGET_FLAGS = -mcpu=cortex-a8 -mfloat-abi=soft -marm
CFLAGS   = $(TARGET_FLAGS) -ffreestanding -fno-builtin -Wall -Wextra -O2 -g
LDFLAGS  = -static -nostdlib -nostartfiles -Wl,--build-id=none

# peripheral-boot entry / MLO load address (must match blinky.ld)
LOAD_ADDR = 0x402f0400

.PHONY: all clean

all: $(BIN)/blinky.elf $(BIN)/blinky.bin $(BIN)/blinky.MLO $(BIN)/blinky.img

$(BIN):
	mkdir -p $(BIN)

$(BIN)/blinky.elf: main.c blinky.ld | $(BIN)
	$(CC) $(CFLAGS) $(LDFLAGS) -T blinky.ld main.c -o $@

$(BIN)/blinky.bin: $(BIN)/blinky.elf
	$(OBJCOPY) -O binary $< $@

$(BIN)/blinky.MLO: $(BIN)/blinky.bin
	$(TOOLS)/mk-gpimage $(LOAD_ADDR) $< $@

$(BIN)/blinky.img: $(BIN)/blinky.MLO
	cp $(TOOLS)/raw-mmc-header.img $@
	dd if=$< of=$@ iflag=fullblock conv=sync seek=1 status=none

clean:
	$(RM) $(BIN)/blinky.elf $(BIN)/blinky.bin $(BIN)/blinky.MLO $(BIN)/blinky.img
