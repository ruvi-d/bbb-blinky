// SPDX-License-Identifier: GPL-2.0-only
/*
 * kblinky-mmio - blink the LED on GPIO1_28 (ball V18/U18, pin-mux name
 * GPMC_BEn1) of an AM335x (BeagleBone Black) by writing the GPIO and
 * control-module registers directly, through ioremap().
 *
 * This is the kernel-module counterpart of the bare-metal blinky in the
 * bbb-blinky repo (https://github.com/ruvi-d/bbb-blinky): same pin, same
 * registers, same write sequence -- but running on top of a booted Linux
 * kernel instead of on bare metal.
 *
 * *** This is deliberately NOT how a driver should do this. ***
 *
 * The correct way is a device-tree node plus the gpiod API (gpiod_get(),
 * gpiod_set_value()).  Reaching past the subsystem that owns a register is
 * exactly what a real driver must not do.  The point here is to show the
 * bare-metal register sequence unchanged, in kernel context, so the two can
 * be compared.
 *
 * Three things differ from the bare-metal version:
 *
 *   1. No clock setup.  gpio-omap has already enabled the GPIO1 module clock
 *      (see the caveat above GPIO1_BASE below).
 *   2. No busy-wait loop.  A module_init() function must return; spinning
 *      forever would wedge the kernel.  A delayed work item does the toggling
 *      instead, so the blinking outlives init and runs in process context.
 *   3. An unload path.  Bare metal never stops, but a module can be rmmod'd,
 *      so everything touched here is saved on entry and restored on exit.
 */

/* Prefixes every pr_*() in this file with "kblinky_mmio: ". Must precede the
 * includes, since it is what linux/printk.h expands pr_*() against. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bits.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/workqueue.h>

/*-------------- Register map (only what we touch) -------------------------*/

/* Control module: one 32-bit "conf" register per pin, holding its mux mode
 * and pad settings.  Physically 0x44E10000..0x44E11FFF; a single 4 KiB page
 * is enough to reach the register we want. */
#define CTRL_BASE		0x44E10000u
#define CTRL_SIZE		0x1000u
#define CONF_GPMC_BE1N		0x0878u		/* ball V18/U18 -> gpio1_28 */

/* Bit layout of a conf_<pin> register (AM335x TRM 9.3.51). */
#define CONF_MMODE(x)		((x) & 0x7)	/* mux mode; 7 = gpio */
#define CONF_PUDEN		BIT(3)		/* 1 = pull disabled */
#define CONF_PUTYPESEL		BIT(4)		/* 1 = pull-up, 0 = pull-down */
#define CONF_RXACTIVE		BIT(5)		/* 1 = input buffer enabled */
#define CONF_SLEWCTRL		BIT(6)		/* 1 = slow slew rate */

/* 0x27: gpio mode, receiver on, pull-down active.  Neither the receiver nor
 * the pull-down is needed to drive an output -- this value is kept purely so
 * it matches the bare-metal blinky byte for byte. */
#define PINMUX_GPIO1_28		(CONF_MMODE(7) | CONF_RXACTIVE)

/* GPIO1.  Its module clock is *not* enabled here, unlike in the bare-metal
 * version: gpio-omap owns the bank and has already turned the clock on.  That
 * driver manages the clock with runtime PM though, so a bank with no users can
 * idle -- and touching these registers with the clock gated is an external
 * abort on the L4LS interconnect, not a silently ignored write.  On a
 * BeagleBone Black bank 1 also drives the four on-board user LEDs, so it stays
 * powered in practice.  Ways to stop relying on that, best first: use the
 * gpiod API instead; add ti,no-idle-on-init to the gpio1 DT node; or take a
 * runtime PM reference on the gpio1 platform device before touching it. */
#define GPIO1_BASE		0x4804C000u
#define GPIO1_SIZE		0x1000u
#define GPIO_OE			0x134u		/* output enable, 0 = output */
#define GPIO_CLEARDATAOUT	0x190u		/* self-masking: 1 -> pin low */
#define GPIO_SETDATAOUT		0x194u		/* self-masking: 1 -> pin high */

#define PIN28			BIT(28)

/*-------------- Blink rate ------------------------------------------------*/

/* Half-period: the LED is on for this long, then off for this long, so a full
 * on/off cycle takes 1 s -- a 1 Hz blink. */
#define BLINK_HALF_PERIOD_MS	500u

/*-------------- State -----------------------------------------------------*/

/* Kernel virtual addresses returned by ioremap().  __iomem marks them as
 * device memory: they must be reached with readl()/writel() rather than
 * dereferenced, both to keep the compiler from reordering or merging the
 * accesses and to get the barriers an MMIO write to a peripheral needs. */
static void __iomem *ctrl_base;
static void __iomem *gpio1_base;

/* Register contents from before we started, put back on unload. */
static u32 saved_pinmux;
static u32 saved_oe;

/* Current LED state.  Only ever touched by kblinky_init(), the work item, and
 * kblinky_exit(), and those never run concurrently (init finishes before the
 * first work item can run, and exit does cancel_delayed_work_sync() first),
 * so no lock is needed. */
static bool led_on;

static void kblinky_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(kblinky_work, kblinky_work_fn);

/*-------------- Implementation --------------------------------------------*/

/* SETDATAOUT and CLEARDATAOUT are self-masking: a 1 bit acts on that pin, a 0
 * bit leaves it alone.  So a single write to the right register is enough --
 * no read-modify-write, and no risk of clobbering the other 31 pins of the
 * bank (four of which are the on-board user LEDs, GPIO1_21..24). */
static void kblinky_set(bool on)
{
	writel(PIN28, gpio1_base + (on ? GPIO_SETDATAOUT : GPIO_CLEARDATAOUT));
	led_on = on;
}

/* Runs in process context on a system workqueue.  Toggle, then re-arm: the
 * work item schedules itself, which is what keeps the blinking going. */
static void kblinky_work_fn(struct work_struct *work)
{
	kblinky_set(!led_on);
	schedule_delayed_work(&kblinky_work,
			      msecs_to_jiffies(BLINK_HALF_PERIOD_MS));
}

static int __init kblinky_init(void)
{
	/* ioremap() maps physical MMIO into the kernel's address space.  Note
	 * there is no request_mem_region()/devm_ioremap_resource() here: those
	 * would claim the region and correctly fail with -EBUSY, because
	 * gpio-omap and pinctrl-single already hold it (both ranges show up in
	 * /proc/iomem).  Bare ioremap() does no such arbitration -- which is
	 * the whole trick this module relies on, and precisely why it is not a
	 * pattern to copy. */
	ctrl_base = ioremap(CTRL_BASE, CTRL_SIZE);
	if (!ctrl_base) {
		pr_err("failed to map control module\n");
		return -ENOMEM;
	}

	gpio1_base = ioremap(GPIO1_BASE, GPIO1_SIZE);
	if (!gpio1_base) {
		pr_err("failed to map GPIO1\n");
		iounmap(ctrl_base);
		return -ENOMEM;
	}

	/* 1. Pin mux: point ball V18/U18 at its gpio1_28 function.
	 *
	 * pinctrl-single caches what it thinks this register holds and will not
	 * notice the change, so the old value is saved and restored on unload
	 * to keep the two views consistent. */
	saved_pinmux = readl(ctrl_base + CONF_GPMC_BE1N);
	writel(PINMUX_GPIO1_28, ctrl_base + CONF_GPMC_BE1N);

	/* 2. Direction: GPIO1_28 = output.  OE is not self-masking, so this is
	 * a read-modify-write, and a *cleared* bit means output. */
	saved_oe = readl(gpio1_base + GPIO_OE);
	writel(saved_oe & ~PIN28, gpio1_base + GPIO_OE);

	/* 3. Start blinking.  Unlike the bare-metal version this returns
	 * immediately; the work item carries on from here. */
	kblinky_set(true);
	schedule_delayed_work(&kblinky_work,
			      msecs_to_jiffies(BLINK_HALF_PERIOD_MS));

	pr_info("blinking GPIO1_28 at 1 Hz\n");
	return 0;
}

static void __exit kblinky_exit(void)
{
	/* Wait for a running work item to finish and make sure it is not
	 * re-armed -- it must not touch the registers after the iounmap()
	 * below. */
	cancel_delayed_work_sync(&kblinky_work);

	/* Undo in reverse: LED off, then direction, then mux. */
	kblinky_set(false);
	writel(saved_oe, gpio1_base + GPIO_OE);
	writel(saved_pinmux, ctrl_base + CONF_GPMC_BE1N);

	iounmap(gpio1_base);
	iounmap(ctrl_base);

	pr_info("stopped\n");
}

module_init(kblinky_init);
module_exit(kblinky_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ruvi-d <contact@ruvi.tech>");
MODULE_DESCRIPTION("Blink an AM335x LED via direct GPIO/pinmux MMIO access");
MODULE_VERSION("0.1");
