#include <stdint.h>

// C port of ../bare-meta-asm/blinky.S -- see that file for register-level
// comments. Toggles GPIO1_28 (ball V18/U18, pin-mux name GPMC_BEn1) forever.

#define MMIO32(addr)  (*(volatile uint32_t *)(addr))

// PRCM clock module (CM_PER)
#define CM_PER_BASE           0x44E00000u
#define CM_PER_L4LS_CLKCTRL   MMIO32(CM_PER_BASE + 0x000)
#define CM_PER_GPIO1_CLKCTRL  MMIO32(CM_PER_BASE + 0x0AC)
#define MODULEMODE_ENABLE     0x2u
#define IDLEST_MASK           (3u << 16)

// Control module (pin mux)
#define CTRL_BASE             0x44E10000u
#define CONF_GPMC_BE1N        MMIO32(CTRL_BASE + 0x0878)
#define PINMUX_GPIO1_28       0x27u

// GPIO1
#define GPIO1_BASE            0x4804C000u
#define GPIO_OE               MMIO32(GPIO1_BASE + 0x134)
#define GPIO_CLEARDATAOUT     MMIO32(GPIO1_BASE + 0x190)
#define GPIO_SETDATAOUT       MMIO32(GPIO1_BASE + 0x194)

#define PIN28                 (1u << 28)
#define BLINK_DELAY           0x00300000u

static void delay(void)
{
	for (volatile uint32_t i = BLINK_DELAY; i != 0; i--)
		;
}

// `used`: only referenced from inline asm below, invisible to the optimizer
__attribute__((used))
static void blink(void)
{
	// 1. enable GPIO1 module clock
	CM_PER_L4LS_CLKCTRL = MODULEMODE_ENABLE;
	CM_PER_GPIO1_CLKCTRL = MODULEMODE_ENABLE;
	while (CM_PER_GPIO1_CLKCTRL & IDLEST_MASK)
		;

	// 2. pin mux: conf_gpmc_be1n = 0x27
	CONF_GPMC_BE1N = PINMUX_GPIO1_28;

	// 3. GPIO1_28 direction = output (read-modify-write clears bit)
	GPIO_OE &= ~PIN28;

	// 4. blink forever
	for (;;) {
		GPIO_SETDATAOUT = PIN28;
		delay();
		GPIO_CLEARDATAOUT = PIN28;
		delay();
	}
}

// Entrypoint: ROM jumps here in ARM mode. Sets sp (blink()/delay() need a
// valid stack even though they hold no state of their own) then never
// returns.
__attribute__((naked, noreturn, section(".text._start")))
void _start(void)
{
	__asm__ volatile (
		"ldr	sp, =__stack_top \n"
		"bl	blink            \n"
	);
}
