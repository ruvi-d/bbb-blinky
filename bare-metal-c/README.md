# bare-metal-c

A C port of [../bare-metal-asm](../bare-metal-asm): the smallest reasonable
baremetal C app for the BeagleBone Black (AM335x, Cortex-A8). It blinks
**GPIO1_28** forever and does nothing else — same behavior as the asm
version, same register pokes, same blink rate.

GPIO1_28 is brought out on ball **V18/U18**, pin-mux name **GPMC_BEn1**.

## What it does

Identical to `bare-metal-asm`, see [main.c](main.c):

1. **Enable the GPIO1 module clock** — `CM_PER_GPIO1_CLKCTRL` @ `0x44E000AC`
   ← `MODULEMODE=ENABLE`, then wait for `IDLEST[17:16] == Func`.
2. **Pin mux** — `conf_gpmc_be1n` @ `0x44E10878` ← `0x27`
   (mmode=7 → gpio1_28, rxactive=1).
3. **Direction** — `GPIO1_OE` @ `0x4804C134`, clear bit 28 (read-modify-write)
   to make it an output.
4. **Blink loop** — `GPIO1_SETDATAOUT` @ `0x4804C194` ← `0x10000000` (HIGH),
   busy-wait, `GPIO1_CLEARDATAOUT` @ `0x4804C190` ← `0x10000000` (LOW),
   busy-wait, repeat.

The blink rate is set by a busy-wait (`BLINK_DELAY` in [main.c](main.c)).

## Why this needs more than the asm version

`blinky.S` never sets up a stack — it holds no state and its `bl`/`ret` pair
doesn't need one. C code can't make that assumption: even though nothing
here uses stack memory for locals in any meaningful way, GCC's calling
convention still expects a valid `sp` before the first `bl`. So `main.c`
adds one thing the asm version didn't need: a tiny naked `_start` that loads
`sp` from a linker-provided `__stack_top` symbol and branches into `blink()`,
which then does the exact same four steps as the asm version.

`blink()` is marked `__attribute__((used))` because it's only referenced
from inline asm inside `_start` — invisible to the optimizer, which would
otherwise treat it as dead code and drop it (confirmed: without this, the
linker fails with `undefined reference to 'blink'`).

## Link map ([blinky.ld](blinky.ld))

Same as `bare-metal-asm/blinky.ld` — same `a8ram` region, same
`0x402f0400` load address — plus one addition:

```
__stack_top = ORIGIN( a8ram ) + LENGTH( a8ram );
```

Still no `.bss`: there are no mutable globals, only the stack.

## Build

The Makefile looks for a `arm-linux-gnueabi-` toolchain. If it's not on your
host `$PATH`, build inside the `hw101:latest` Docker image:

```bash
docker run --rm -v "$PWD/..":/src -w /src/bare-metal-c hw101:latest make clean all
```

or, with the toolchain available directly:

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

## Verifying against the asm version

```bash
arm-linux-gnueabi-objdump -d bin/blinky.elf
arm-linux-gnueabi-objdump -d ../bare-metal-asm/bin/blinky.elf
```

Both hit the same four MMIO addresses with the same immediate values in the
same order, and `_start` is first in both images. The C binary comes out
larger than the asm one (GCC inlines `delay()` at both call sites, and the
`volatile` loop counter forces a stack spill/reload every iteration instead
of staying in a register) — that overhead is expected and doesn't change
behavior.

## Boot tooling (`tools/`)

Same `mk-gpimage` / `raw-mmc-header.img` as `bare-metal-asm/tools/` (copied
verbatim) — see [../bare-metal-asm/README.md](../bare-metal-asm/README.md) for
how the SD card image framing works.

### Boot from µSD (raw MMC)

**Caution: this overwrites the start of the card.**

```bash
dd if=bin/blinky.img of=/dev/mmcblkX   # replace X with your card
```
