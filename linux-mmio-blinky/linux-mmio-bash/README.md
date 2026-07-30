# linux-mmio-bash

A shell script that blinks **GPIO1_28** on a BeagleBone Black by driving the
GPIO and pin-mux registers with `devmem2` — the same registers, in the same
order, as [../../bare-metal-blinky](../../bare-metal-blinky),
[../linux-mmio-kernel](../linux-mmio-kernel) and
[../linux-mmio-c](../linux-mmio-c), but with no compiler involved anywhere.

GPIO1_28 is brought out on ball **V18/U18**, pin-mux name **GPMC_BEn1**.

> **This is not how an application should drive a GPIO.** It reaches past the
> drivers that own these registers. It is here to show that the register
> sequence is small enough to type out in a shell — see
> [Why this is wrong](#why-this-is-wrong) for what to do instead.

## What it does

1. **Pin mux** — `conf_gpmc_be1n` @ `0x44E10878` ← `0x27` (mmode=7 → gpio1_28,
   rxactive=1).
2. **Direction** — `GPIO1_OE` @ `0x4804C134`, clear bit 28 to make it an
   output. `devmem2` has no read-modify-write primitive, so this is a read,
   an arithmetic step in the shell, and a separate write.
3. **Blink** — alternates `GPIO1_SETDATAOUT` @ `0x4804C194` and
   `GPIO1_CLEARDATAOUT` @ `0x4804C190` ← `0x10000000` every 500 ms (`sleep`),
   i.e. a 1 Hz on/off cycle, forever. Both registers are self-masking, so a
   single write per toggle, no read-modify-write, and no chance of disturbing
   the other 31 pins of the bank (four of which are the on-board user LEDs,
   GPIO1_21..24).

Like the other versions it never returns, and it restores nothing: Ctrl-C it
and the mux and `OE` registers keep the values set above, with the LED left
wherever the last toggle put it.

## Differences from the C version

Against [../linux-mmio-c](../linux-mmio-c), which does the same thing with one
long-lived mapping:

| | the C version | this script |
|---|---|---|
| Mapping lifetime | one `mmap()` held for the whole run | one `mmap()` per register access, inside `devmem2` |
| Cost per toggle | a single store to a mapped page | `fork`/`exec`, `open("/dev/mem")`, `mmap()`, store, teardown |
| Direction register | `REG32(...) &= ~PIN28`, one instruction | read, shell arithmetic, write — three steps |
| Build | `make`, cross toolchain | none |

Everything the C version's README says about `/dev/mem`, `CONFIG_IO_STRICT_DEVMEM`,
`pinctrl-single` and the GPIO1 clock applies here unchanged — the mechanism is
identical, only the packaging differs.

The one thing that is genuinely worse here is the direction register. The C
version's read-modify-write of `GPIO1_OE` already races with `gpio-omap`;
doing it as two separate `devmem2` invocations widens that window from a few
instructions to two process lifetimes. Nothing stops the kernel from writing
`OE` in between, and if it does, this script silently puts the old value back.

## Why this is wrong

The supported equivalent is a device-tree node that sets the mux, plus
libgpiod to drive the line — no `/dev/mem`, no root, no fighting the drivers:

```bash
gpioset --chip gpiochip1 28=1     # libgpiod 2.x
gpioset gpiochip1 28=1            # libgpiod 1.x
```

That goes through `gpio-omap`, which arbitrates between users, keeps the clock
awake while the line is held, and knows whether something else has already
claimed the pin. See
[../linux-mmio-c/README.md](../linux-mmio-c/README.md#why-this-is-wrong) for
the longer version.

## Run

There is nothing to build. Copy it over and run it as root — `/dev/mem` is
`0600 root:root`:

```bash
scp blinky.sh root@<target>:/tmp/
ssh root@<target> sh /tmp/blinky.sh
```

It needs `devmem2` on `$PATH` on the target, which is not part of a stock
rootfs — on Yocto, add `devmem2` to `IMAGE_INSTALL`. The script checks for it
and exits with a message if it is missing.

It prints one line and then blinks until interrupted:

```
blinking GPIO1_28 at 1 Hz
```

The rate is fixed at 1 Hz (`HALF_PERIOD` in [blinky.sh](blinky.sh)) — there are
no command-line options.

The script targets POSIX `sh`, so it runs under the busybox shell on a minimal
rootfs; it does not need bash.

Do not run this at the same time as
[../linux-mmio-kernel](../linux-mmio-kernel) or
[../linux-mmio-c](../linux-mmio-c): all three drive the same pin, and the
kernel module will restore the mux and `OE` values it saw on load — which, if
this script went first, are this script's, not the kernel's.
