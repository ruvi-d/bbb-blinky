/*
 * blinky - blink the LED on GPIO1_28 (ball V18/U18, pin-mux name GPMC_BEn1)
 * of an AM335x (BeagleBone Black) from a userspace process, by mapping the
 * GPIO and control-module registers through /dev/mem.
 *
 * This is the userspace counterpart of the bare-metal blinky and of the
 * kblinky-mmio kernel module in the bbb-blinky repo
 * (https://github.com/ruvi-d/bbb-blinky): same pin, same registers, same
 * write sequence -- but from an ordinary process, on top of a booted Linux
 * kernel.
 *
 * *** This is deliberately NOT how an application should drive a GPIO. ***
 *
 * The supported way is libgpiod (or just `gpioset`), with the pin mux set once
 * in the device tree.  Mapping a peripheral that a kernel driver owns and
 * writing its registers behind its back is exactly what an application must
 * not do.  The point here is to show the bare-metal register sequence
 * unchanged, in userspace, so the three versions can be compared.
 *
 * Three things differ from the bare-metal version:
 *
 *   1. Addresses are virtual.  mmap() on /dev/mem hands back a mapping of the
 *      physical page; the MMU makes the register appear at some unrelated
 *      address in this process.  Only the offsets within the page carry over.
 *   2. No clock setup.  gpio-omap has already enabled the GPIO1 module clock
 *      (see the caveat above GPIO1_BASE below).
 *   3. Sleep instead of a busy-wait.  nanosleep() hands the CPU back to the
 *      scheduler between toggles, which a spin loop would not.
 *
 * Like the bare-metal version this never returns, and it restores nothing:
 * kill it and the mux and OE registers keep the values set here, with
 * pinctrl-single none the wiser.  The kernel module undoes its writes on
 * rmmod; there is no equivalent here.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/*-------------- Register access -------------------------------------------*/

/* One 4 KiB page per peripheral is enough to reach everything below, and both
 * physical bases are page-aligned already -- which is what mmap() requires of
 * its offset argument. */
#define MAP_SIZE		0x1000u

/* volatile because these are device registers: every access in the source has
 * to reach the peripheral, in program order, with nothing merged or hoisted
 * out of the loop.  Userspace has no readl()/writel(); opening /dev/mem with
 * O_SYNC is what keeps the mapping uncached, so a store does not sit in a
 * write-back cache line waiting for an eviction. */
#define REG32(base, off)	(*(volatile uint32_t *)((uint8_t *)(base) + (off)))

/*-------------- Register map (only what we touch) -------------------------*/

/* Control module: one 32-bit "conf" register per pin, holding its mux mode
 * and pad settings.  Physically 0x44E10000..0x44E11FFF. */
#define CTRL_BASE		0x44E10000u
#define CONF_GPMC_BE1N		0x0878u		/* ball V18/U18 -> gpio1_28 */

/* Bit layout of a conf_<pin> register (AM335x TRM 9.3.51). */
#define CONF_MMODE(x)		((x) & 0x7u)	/* mux mode; 7 = gpio */
#define CONF_PUDEN		(1u << 3)	/* 1 = pull disabled */
#define CONF_PUTYPESEL		(1u << 4)	/* 1 = pull-up, 0 = pull-down */
#define CONF_RXACTIVE		(1u << 5)	/* 1 = input buffer enabled */
#define CONF_SLEWCTRL		(1u << 6)	/* 1 = slow slew rate */

/* 0x27: gpio mode, receiver on, pull-down active.  Neither the receiver nor
 * the pull-down is needed to drive an output -- this value is kept purely so
 * it matches the bare-metal blinky byte for byte. */
#define PINMUX_GPIO1_28		(CONF_MMODE(7) | CONF_RXACTIVE)

/* GPIO1.  Its module clock is *not* enabled here, unlike in the bare-metal
 * version: gpio-omap owns the bank and has already turned the clock on.  That
 * driver manages the clock with runtime PM though, so a bank with no users can
 * idle -- and touching these registers with the clock gated is an external
 * abort on the L4LS interconnect, which from userspace arrives as a SIGBUS.
 * On a BeagleBone Black bank 1 also drives the four on-board user LEDs, so it
 * stays powered in practice.  Ways to stop relying on that, best first: use
 * libgpiod instead; add ti,no-idle-on-init to the gpio1 DT node; or keep a
 * gpiod line on the bank open for as long as this program runs. */
#define GPIO1_BASE		0x4804C000u
#define GPIO_OE			0x134u		/* output enable, 0 = output */
#define GPIO_CLEARDATAOUT	0x190u		/* self-masking: 1 -> pin low */
#define GPIO_SETDATAOUT		0x194u		/* self-masking: 1 -> pin high */

#define PIN28			(1u << 28)

/*-------------- Blink rate ------------------------------------------------*/

/* The LED is on for this long, then off for this long, so a full on/off cycle
 * takes 1 s -- a 1 Hz blink, matching the kernel module. */
static const struct timespec half_period = { .tv_nsec = 500 * 1000 * 1000 };

/*-------------- Implementation --------------------------------------------*/

/* Map a single page of physical MMIO out of /dev/mem.  MAP_SHARED is what
 * makes the writes go to the device rather than to a private copy. */
static void *map_page(int fd, off_t phys, const char *what)
{
	void *p = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		       phys);

	if (p == MAP_FAILED) {
		fprintf(stderr, "mmap %s @ 0x%08lx: %s\n", what,
			(unsigned long)phys, strerror(errno));
		return NULL;
	}
	return p;
}

int main(void)
{
	void *ctrl_base, *gpio1_base;
	int fd;

	/* O_SYNC asks for an uncached mapping, which is mandatory for
	 * registers.  Needs root: /dev/mem is 0600 root:root.
	 *
	 * Two kernel options can refuse this outright, and neither leaves much
	 * room to work around it:
	 *   - CONFIG_STRICT_DEVMEM blocks /dev/mem access to system RAM but
	 *     still permits MMIO ranges like these two, so it is not a problem
	 *     here.
	 *   - CONFIG_IO_STRICT_DEVMEM additionally refuses any range a driver
	 *     has claimed, and both of ours are claimed (pinctrl-single and
	 *     gpio@0 in /proc/iomem).  With that set, mmap() fails with EPERM,
	 *     as it should -- the kernel is telling us to use libgpiod. */
	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "open /dev/mem: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	ctrl_base = map_page(fd, CTRL_BASE, "control module");
	gpio1_base = map_page(fd, GPIO1_BASE, "GPIO1");
	close(fd);		/* the mappings outlive the descriptor */
	if (!ctrl_base || !gpio1_base)
		return EXIT_FAILURE;

	/* 1. Pin mux: point ball V18/U18 at its gpio1_28 function. */
	REG32(ctrl_base, CONF_GPMC_BE1N) = PINMUX_GPIO1_28;

	/* 2. Direction: GPIO1_28 = output.  OE is not self-masking, so this is
	 * a read-modify-write, and a *cleared* bit means output.  It also races
	 * with gpio-omap by construction: nothing stops the kernel from writing
	 * OE between the read and the write. */
	REG32(gpio1_base, GPIO_OE) &= ~PIN28;

	printf("blinking GPIO1_28 at 1 Hz\n");

	/* 3. Blink forever.  SETDATAOUT and CLEARDATAOUT are self-masking: a 1
	 * bit acts on that pin, a 0 bit leaves it alone.  So a single write per
	 * toggle -- no read-modify-write, no risk of clobbering the other 31
	 * pins of the bank (four of which are the on-board user LEDs,
	 * GPIO1_21..24), and no race with gpio-omap's own accesses. */
	for (;;) {
		REG32(gpio1_base, GPIO_SETDATAOUT) = PIN28;
		nanosleep(&half_period, NULL);
		REG32(gpio1_base, GPIO_CLEARDATAOUT) = PIN28;
		nanosleep(&half_period, NULL);
	}
}
