#include "asm-defs.h"

// Minimal baremetal blinky for the BeagleBone Black (AM335x, Cortex-A8).
//
// Toggles GPIO1_28 forever.  GPIO1_28 is brought out on ball V18/U18
// (pin-mux name GPMC_BEn1).  Nothing else is configured.
//
//   1. enable the GPIO1 module clock            (prerequisite for step 2-4)
//   2. mux the ball to its gpio1_28 function
//   3. set GPIO1_28 as an output
//   4. drive it high / low in an endless loop


//-------------- Register map (only what we touch) ---------------------------//

// PRCM clock module (CM_PER)
#define CM_PER_BASE		0x44E00000
#define CM_PER_L4LS_CLKCTRL	0x000	// L4LS interconnect clock
#define CM_PER_GPIO1_CLKCTRL	0x0AC	// GPIO1 module functional clock
#define MODULEMODE_ENABLE	0x2	// MODULEMODE = ENABLE
#define IDLEST_MASK		(3 << 16)	// 0b00 in these bits == Func

// Control module (pin mux)
#define CTRL_BASE		0x44E10000
#define CONF_GPMC_BE1N		0x0878	// ball V18/U18 -> gpio1_28
#define PINMUX_GPIO1_28		0x27	// mmode=7 (gpio), rxactive=1

// GPIO1
#define GPIO1_BASE		0x4804C000
#define GPIO_OE			0x134	// output-enable (0 = output)
#define GPIO_CLEARDATAOUT	0x190	// self-masking: write 1 -> pin low
#define GPIO_SETDATAOUT		0x194	// self-masking: write 1 -> pin high

#define PIN28			(1 << 28)

// busy-wait iterations between toggles (visible blink, no timer needed)
#define BLINK_DELAY		0x00300000


//-------------- Code --------------------------------------------------------//

// .text._start is placed first in the image by the linker script; ROM enters
// here in ARM mode at the load address.  Only the entrypoint belongs in it --
// everything below switches back to plain .text.
.section .text._start
.arm

.fun _start
	// --- 1. enable GPIO1 module clock ------------------------------------
	ldr	r0, = CM_PER_BASE
	movs	r1, MODULEMODE_ENABLE
	str	r1, [ r0, CM_PER_L4LS_CLKCTRL ]
	str	r1, [ r0, CM_PER_GPIO1_CLKCTRL ]
1:	ldr	r1, [ r0, CM_PER_GPIO1_CLKCTRL ]	// wait until IDLEST == Func
	tst	r1, IDLEST_MASK
	bne	1b

	// --- 2. pin mux: conf_gpmc_be1n = 0x27 -------------------------------
	ldr	r0, = CTRL_BASE
	movs	r1, PINMUX_GPIO1_28
	str	r1, [ r0, CONF_GPMC_BE1N ]

	// --- 3. GPIO1_28 direction = output (read-modify-write clears bit) ---
	ldr	r0, = GPIO1_BASE
	ldr	r1, [ r0, GPIO_OE ]
	bic	r1, r1, PIN28
	str	r1, [ r0, GPIO_OE ]

	// --- 4. blink forever (r0 = GPIO1_BASE) ------------------------------
	mov	r2, PIN28
blink:
	str	r2, [ r0, GPIO_SETDATAOUT ]	// drive HIGH
	bl	delay
	str	r2, [ r0, GPIO_CLEARDATAOUT ]	// drive LOW
	bl	delay
	b	blink
.done

	.ltorg				// the three base addresses loaded above


.section .text

// crude busy-wait; clobbers r3 only
.fun delay
	ldr	r3, = BLINK_DELAY
1:	subs	r3, r3, 1
	bne	1b
	ret
.done
