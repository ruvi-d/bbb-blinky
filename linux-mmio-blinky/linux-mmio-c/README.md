# linux-mmio-c

An ordinary userspace program that blinks **GPIO1_28** on a BeagleBone Black by
mapping the GPIO and pin-mux registers with `mmap()` on `/dev/mem` — the same
registers, in the same order, as
[../../bare-metal-blinky](../../bare-metal-blinky) and
[../linux-mmio-kernel](../linux-mmio-kernel), but from a process rather than
from kernel context.

GPIO1_28 is brought out on ball **V18/U18**, pin-mux name **GPMC_BEn1**.

> **This is not how an application should drive a GPIO.** It reaches past the
> drivers that own these registers. It is here to make the bare-metal register
> sequence comparable from userspace — see
> [Why this is wrong](#why-this-is-wrong) for what to do instead.

## What it does

1. **Map** — `open("/dev/mem", O_RDWR | O_SYNC)`, then one `mmap()` per 4 KiB
   page: `0x44E10000` (control module) and `0x4804C000` (GPIO1). Both physical
   bases are page-aligned, so the register offsets below carry over unchanged
   from the bare-metal version; only the base addresses become virtual.
2. **Pin mux** — `conf_gpmc_be1n` @ `+0x878` ← `0x27` (mmode=7 → gpio1_28,
   rxactive=1).
3. **Direction** — `GPIO1_OE` @ `+0x134`, clear bit 28 to make it an output.
4. **Blink** — alternates `GPIO1_SETDATAOUT` @ `+0x194` and
   `GPIO1_CLEARDATAOUT` @ `+0x190` ← `0x10000000` every 500 ms
   (`nanosleep()`), i.e. a 1 Hz on/off cycle, forever. Both registers are
   self-masking, so a single write per toggle, no read-modify-write, and no
   chance of disturbing the other 31 pins of the bank (four of which are the
   on-board user LEDs, GPIO1_21..24).

Like the bare-metal version it never returns, and it restores nothing: kill it
and the mux and `OE` registers keep the values set above, with the LED left
wherever the last toggle put it. The kernel module undoes its writes on
`rmmod`; there is no equivalent here.

## Differences from the bare-metal version

| | bare metal | this program |
|---|---|---|
| Addresses | physical, no MMU | virtual, `mmap()` of `/dev/mem` |
| GPIO1 module clock | enabled explicitly via `CM_PER_GPIO1_CLKCTRL` | left to `gpio-omap` (see [The clock caveat](#the-clock-caveat)) |
| Blink timing | busy-wait loop | `nanosleep()`, 500 ms half-period (1 Hz) |
| Termination | never returns | never returns |

Registers are reached through `volatile uint32_t` accesses — userspace has no
`readl()`/`writel()`. `volatile` keeps the compiler from merging, reordering or
hoisting them; `O_SYNC` is what keeps the mapping uncached, so a store is not
left sitting in a write-back cache line. Between them that is enough for an
LED. Nothing here orders these writes against what `gpio-omap` may be doing to
the same bank concurrently.

## Why `/dev/mem` works at all

Both register ranges are already claimed by drivers, as `/proc/iomem` on the
target shows:

```
44e10800-44e10a37 : pinctrl-single
4804c000-4804cfff : 4804c000.gpio gpio@0
```

`/dev/mem` does not consult that list, so `mmap()` succeeds anyway. Two kernel
options change that, and it is worth knowing which:

- **`CONFIG_STRICT_DEVMEM`** blocks `/dev/mem` access to system RAM but still
  permits MMIO ranges like these two — so it does not get in the way here.
- **`CONFIG_IO_STRICT_DEVMEM`** additionally refuses any range a driver has
  claimed. With it set, `mmap()` fails with `EPERM` and there is no way around
  it short of unbinding the drivers. That failure is the kernel correctly
  telling you to use libgpiod.

The consequence of going behind `pinctrl-single` is that it still believes
`conf_gpmc_be1n` holds whatever it last wrote there. Nothing here puts that
right — the two views stay out of step until something re-muxes the pin or the
board reboots.

## The clock caveat

The bare-metal version enables the GPIO1 module clock itself. This program does
not — `gpio-omap` has already done it long before a userspace process can run.

That is a dependency worth being explicit about: `gpio-omap` manages the clock
with runtime PM and drops its reference at the end of probe, so a bank with no
active users can idle. Accessing these registers with the clock gated is an
external abort on the L4LS interconnect, which reaches a process as a
**`SIGBUS`**, not a write that quietly does nothing.

On a BeagleBone Black bank 1 also drives the four on-board user LEDs, so it is
kept awake in practice and this program gets away with it. If you would rather
not depend on that, the options are, roughly in order of preference:

- Use libgpiod and let `gpio-omap` handle power (i.e. stop doing this).
- Add `ti,no-idle-on-init` to the `gpio1` device-tree node so the bank never
  autosuspends.
- Hold a libgpiod line on bank 1 open for as long as the program runs, so
  runtime PM keeps the bank resumed.

Unlike the kernel module, there is no fourth option: a process cannot poke
`CM_PER_GPIO1_CLKCTRL` in any way the clock framework would survive.

## Why this is wrong

The supported equivalent is a device-tree node that sets the mux, plus
libgpiod to drive the line — no `/dev/mem`, no root, no fighting the drivers:

```bash
gpioset gpiochip1 28=1
```

That goes through `gpio-omap`, which arbitrates between users, keeps the clock
awake while the line is held, and knows whether something else has already
claimed the pin. Everything this program does by hand, it does wrong on
purpose.

## Build

Needs an `arm-linux-gnueabi-` cross toolchain on `$PATH`:

```bash
make
```

The result is `bin/blinky`, statically linked so it runs on any AM335x rootfs
regardless of its libc or float ABI. Override `CROSS_COMPILE` to build against
a toolchain matching your target instead:

```bash
make CROSS_COMPILE=arm-poky-linux-gnueabi-
```

## Run

Copy it over and run it as root — `/dev/mem` is `0600 root:root`:

```bash
scp bin/blinky root@<target>:/tmp/
ssh root@<target> /tmp/blinky
```

It prints one line and then blinks until killed:

```
blinking GPIO1_28 at 1 Hz
```

The rate is fixed at 1 Hz (`half_period` in [main.c](main.c)) — there are no
command-line options.

Do not run this at the same time as
[../linux-mmio-kernel](../linux-mmio-kernel): both drive the same pin, and the
module will restore the mux and `OE` values it saw on load — which, if this
program went first, are this program's, not the kernel's.
