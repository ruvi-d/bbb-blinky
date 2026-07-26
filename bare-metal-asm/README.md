# bare-metal-asm

The smallest possible baremetal app for the BeagleBone Black (AM335x,
Cortex-A8): it blinks **GPIO1_28** forever and does nothing else.

GPIO1_28 is brought out on ball **V18/U18**, pin-mux name **GPMC_BEn1**.

The project [mvduin/bbb-asm-demo](https://github.com/mvduin/bbb-asm-demo) was used as a reference.

## What it does

1. **Enable the GPIO1 module clock** — `CM_PER_GPIO1_CLKCTRL` @ `0x44E000AC`
   ← `MODULEMODE=ENABLE`, then wait for `IDLEST[17:16] == Func`.
2. **Pin mux** — `conf_gpmc_be1n` @ `0x44E10878` ← `0x27`
   (mmode=7 → gpio1_28, rxactive=1).
3. **Direction** — `GPIO1_OE` @ `0x4804C134`, clear bit 28 (read-modify-write)
   to make it an output.
4. **Blink loop** — `GPIO1_SETDATAOUT` @ `0x4804C194` ← `0x10000000` (HIGH),
   busy-wait, `GPIO1_CLEARDATAOUT` @ `0x4804C190` ← `0x10000000` (LOW),
   busy-wait, repeat. Both registers are self-masking, so no read-modify-write.

The blink rate is set by a busy-wait (`BLINK_DELAY` in [blinky.S](blinky.S)).

## Link map ([blinky.ld](blinky.ld))

The code is linked to run at `0x402f0400` — not an arbitrary choice, but the
ROM bootloader's hardcoded peripheral-boot load address in Cortex-A8 local
RAM. Which is why `LOAD_ADDR` in the Makefile must match this address.

There is no `.bss` or stack region: `blinky.S` never touches `sp` or holds
any mutable state, so the linker script doesn't reserve one.

## Build

The Makefile looks for a `arm-linux-gnueabi-` toolchain.

```bash
make clean
make all
```

## Artifacts (in `bin/`)

| File | Use |
|------|-----|
| `blinky.elf` | JTAG upload / debugging |
| `blinky.bin` | peripheral boot (UART / Ethernet / USB) and XIP |
| `blinky.MLO` | memory boot from µSD / eMMC / SPI |
| `blinky.img` | raw image to `dd` straight onto a µSD card |

## How the SD card image is built (`tools/`)

The AM335x ROM bootloader expects specific framing around raw code before it
will load it from a µSD/eMMC card — plain `blinky.bin` on its own isn't
bootable. `blinky.MLO` and `blinky.img` add that framing, using two files in
`tools/` (both copied as-is from `bbb-asm-demo`, unmodified):

- **Sector 0** may hold a "Configuration Header" (CH): a small TOC followed
  by a `CHSETTINGS` section the ROM parses. `tools/raw-mmc-header.img` is a
  pre-built 288-byte blob containing an empty/disabled CH — it exists purely
  so the ROM finds a valid (if inert) header instead of garbage, and so this
  boot method can coexist with a partition table in the same sector if
  needed. It is static data, not something regenerated per build.

- **Sector 1 onward** holds the actual image, which the ROM expects to start
  with a "GP header" (Generic Prototype header): an 8-byte
  `{ size, load_address }` pair immediately followed by the raw code.
  `tools/mk-gpimage` is a small Perl script that prepends this header to
  `blinky.bin` to produce `blinky.MLO`.

The Makefile wires these together as:

```makefile
$(BIN)/blinky.MLO: $(BIN)/blinky.bin
	$(TOOLS)/mk-gpimage $(LOAD_ADDR) $< $@

$(BIN)/blinky.img: $(BIN)/blinky.MLO
	cp $(TOOLS)/raw-mmc-header.img $@
	dd if=$< of=$@ iflag=fullblock conv=sync seek=1 status=none
```

i.e. `mk-gpimage` builds `blinky.MLO` (GP header + code), then `blinky.img`
is assembled by placing the CH in sector 0 and the MLO starting at sector 1
(`seek=1`, 512-byte blocks). `LOAD_ADDR` (`0x402f0400`) must match the
load address in [blinky.ld](blinky.ld), since that's where the ROM will
actually copy and jump to the code.

### Boot from µSD (raw MMC)

**Caution: this overwrites the start of the card.**

```bash
dd if=bin/blinky.img of=/dev/mmcblkX   # replace X with your card
```

Load address is `0x402f0400` (Cortex-A8 local RAM), matching
[blinky.ld](blinky.ld) and the `mk-gpimage` argument in the
[Makefile](Makefile).
