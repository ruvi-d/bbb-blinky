# linux-mmio-kernel

A loadable kernel module that blinks **GPIO1_28** on a BeagleBone Black by
writing the GPIO and pin-mux registers directly through `ioremap()` — the
same registers, in the same order, as
[../../bare-metal-blinky](../../bare-metal-blinky) and
[../linux-mmio-c](../linux-mmio-c), but from kernel context on top of a booted
Linux kernel.

GPIO1_28 is brought out on ball **V18/U18**, pin-mux name **GPMC_BEn1**.

> **This is not how a driver should drive a GPIO.** It reaches past the
> subsystems that own these registers. It is here to make the bare-metal
> register sequence comparable in kernel context — see
> [Why this is wrong](#why-this-is-wrong) for what to do instead.

## What it does

1. **Pin mux** — `conf_gpmc_be1n` @ `0x44E10878` ← `0x27` (mmode=7 → gpio1_28,
   rxactive=1).
2. **Direction** — `GPIO1_OE` @ `0x4804C134`, clear bit 28 to make it an
   output.
3. **Blink** — a self-rescheduling delayed work item alternates
   `GPIO1_SETDATAOUT` @ `0x4804C194` and `GPIO1_CLEARDATAOUT` @ `0x4804C190`
   ← `0x10000000` every 500 ms, i.e. a 1 Hz on/off cycle. Both registers are
   self-masking, so a single write per toggle, no read-modify-write, and no
   chance of disturbing the other 31 pins of the bank (four of which are the
   on-board user LEDs, GPIO1_21..24).

On unload it stops the work item, drives the LED low, and restores the
original `OE` and mux register values.

## Differences from the bare-metal version

| | bare metal | this module |
|---|---|---|
| Addresses | physical, no MMU | kernel virtual, `ioremap()` of the physical pages |
| GPIO1 module clock | enabled explicitly via `CM_PER_GPIO1_CLKCTRL` | left to `gpio-omap` (see [The clock caveat](#the-clock-caveat)) |
| Blink timing | busy-wait loop | delayed work item, 500 ms half-period (1 Hz) |
| Termination | never returns | `module_exit()` restores every register it touched |

`module_init()` must return, so the toggling cannot live in an endless loop the
way it does in
[bare-metal-asm/blinky.S](../../bare-metal-blinky/bare-metal-asm/blinky.S); the
work item is what keeps it going after init returns. It runs in process context
on a system workqueue, so its timing is scheduler-dependent — fine for an LED,
not for anything with real timing requirements.

## Why `ioremap()` and not `devm_ioremap_resource()`

Both register ranges are already claimed, as `/proc/iomem` on the target
shows:

```
44e10800-44e10a37 : pinctrl-single
4804c000-4804cfff : 4804c000.gpio gpio@0
```

`request_mem_region()` and `devm_ioremap_resource()` would consult that list
and correctly fail with `-EBUSY`. Bare `ioremap()` does no such arbitration —
it just builds a page-table mapping. That is the only reason this module can
work at all, and it is the part to take as a cautionary example rather than a
pattern.

The consequence is that `pinctrl-single` still believes `conf_gpmc_be1n` holds
whatever it last wrote there. The module saves and restores that register so
the two views agree again after `rmmod`, but while it is loaded they do not.

## The clock caveat

The bare-metal version enables the GPIO1 module clock itself. This module does
not — `gpio-omap` has already done it by the time a module can load.

That is a dependency worth being explicit about: `gpio-omap` manages the clock
with runtime PM and drops its reference at the end of probe, so a bank with no
active users can idle. Accessing these registers with the clock gated is an
**external abort on the L4LS interconnect**, not a write that quietly does
nothing.

On a BeagleBone Black bank 1 also drives the four on-board user LEDs, so it is
kept awake in practice and this module gets away with it. If you would rather
not depend on that, the options are, roughly in order of preference:

- Use the gpiod API and let `gpio-omap` handle power (i.e. write a real
  driver).
- Add `ti,no-idle-on-init` to the `gpio1` device-tree node so the bank never
  autosuspends.
- Take a runtime PM reference on the `gpio1` platform device before touching
  the registers (`of_find_device_by_node()` + `pm_runtime_get_sync()`).
- Poke `CM_PER_GPIO1_CLKCTRL` the way the bare-metal code does — which fights
  the clock framework and is the worst of the four.

## Why this is wrong

The supported equivalent is a device-tree node that sets the mux, plus the
gpiod API to drive the line — no `ioremap()`, no addresses in the source, and
no fighting the subsystems:

```c
struct gpio_desc *led = gpiod_get(dev, "blink", GPIOD_OUT_LOW);

gpiod_set_value(led, 1);
```

That goes through `gpio-omap`, which arbitrates between users, keeps the clock
awake while the line is held, and knows whether something else has already
claimed the pin. A real driver would also bind to a device rather than doing
its work in `module_init()`, so the mapping's lifetime follows the hardware's.
Everything this module does by hand, it does wrong on purpose.

If the LED only needs to blink, no C is required at all: describe it as a
`gpio-leds` node in the device tree and let the `timer` LED trigger do the
toggling.

## Layout

This is a BitBake recipe, not a layer — there is no `conf/layer.conf` here.
It builds out of tree against a **6.16** kernel. Nothing in the module is
version-specific, so it should build against neighbouring versions too.

```
recipes-kernel/kblinky-mmio/
├── kblinky-mmio_0.1.bb
└── files/
    ├── Makefile
    └── kblinky-mmio.c
```

- **[kblinky-mmio_0.1.bb](recipes-kernel/kblinky-mmio/kblinky-mmio_0.1.bb)** —
  `inherit module` does the work: it unpacks the two `file://` entries into
  `${S}`, runs `make` there with `KERNEL_SRC` pointing at the kernel's build
  tree, and installs the result under `/lib/modules/<version>/extra`.
- **[files/Makefile](recipes-kernel/kblinky-mmio/files/Makefile)** — a Kbuild
  stub. `obj-m` names the module; each target just re-enters the kernel build
  system with `M=` pointing back here, which is what supplies the compiler
  flags, the includes and the module linking.

## Build

Copy the recipe into a layer that already carries an AM335x machine and a
kernel — [meta-kiss](https://github.com/ruvi-d/simplest-yocto-setup) is the one
this was developed against:

```bash
cp -r recipes-kernel/kblinky-mmio /path/to/meta-kiss/recipes-kernel/
```

`COMPATIBLE_MACHINE` in the recipe is set to `dogbonedark|dogbonedarker` —
change it if your machine is named differently, or BitBake will skip the recipe
as incompatible. Then:

```bash
bitbake kblinky-mmio
```

To ship it in an image, add the module to `IMAGE_INSTALL`, per-machine since
the recipe only builds for AM335x:

```bitbake
IMAGE_INSTALL:append:dogbonedark = " kernel-module-kblinky-mmio"
```

## Run

Nothing loads the module automatically — blinking starts on load and stops on
unload:

```bash
modprobe kblinky-mmio
rmmod kblinky_mmio      # note: underscore
dmesg | tail
```

Add `KERNEL_MODULE_AUTOLOAD += "kblinky-mmio"` to the recipe to have it load at
boot instead.

The rate is fixed at 1 Hz (`BLINK_HALF_PERIOD_MS` in
[kblinky-mmio.c](recipes-kernel/kblinky-mmio/files/kblinky-mmio.c)) — there are
no module parameters.

Do not load this while [../linux-mmio-c](../linux-mmio-c) or
[../linux-mmio-bash](../linux-mmio-bash) is running: all three drive the same
pin, and this module restores the mux and `OE` values it saw on load — which,
if one of the others went first, are that program's values, not the kernel's.
