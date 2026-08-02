#!/bin/sh
#
# blinky.sh - blink the LED on GPIO1_28 (ball V18/U18, pin-mux name GPMC_BEn1)
# of an AM335x (BeagleBone Black), driven from a shell script via devmem2
# instead of a compiled mmap() program.
#
# This is the shell-script counterpart of ../linux-mmio-c and of the
# kblinky-mmio kernel module in the bbb-blinky repo
# (https://github.com/ruvi-d/bbb-blinky): same pin, same registers, same
# write sequence -- just driven one devmem2 invocation at a time instead of
# through a single long-lived mapping.
#
# *** This is deliberately NOT how an application should drive a GPIO. ***
# See ../linux-mmio-c/README.md ("Why this is wrong") for what to do
# instead; everything said there about /dev/mem, pinctrl-single and
# gpio-omap applies here unchanged.
#
# Unlike the C version, this script does not hold a single mapping for its
# whole run: devmem2 opens /dev/mem, mmaps one page, does one access, and
# exits -- every read or write below pays that cost again. That also means
# there is no self-masking shortcut for the direction register: devmem2 has
# no read-modify-write primitive, so GPIO1_OE has to be read back into the
# shell, modified, and written back separately, with a real (if narrow) race
# against gpio-omap between the two steps.
#
# Like the other two versions this never returns, and it restores nothing:
# Ctrl-C it and the mux and OE registers keep the values set here, with
# pinctrl-single none the wiser. The kernel module undoes its writes on
# rmmod; there is no equivalent here.
#
# Needs devmem2 on $PATH on the target -- it is not part of a stock BBB
# rootfs. Needs root: /dev/mem is 0600 root:root.

set -euo pipefail

if ! command -v devmem2 >/dev/null 2>&1; then
	echo "blinky.sh: devmem2 not found on \$PATH" >&2
	exit 1
fi

#-------------- Register map (only what we touch) ----------------------------
# Same offsets and values as ../linux-mmio-c/main.c; see that file for the
# TRM references behind them.

CONF_GPMC_BE1N=0x44E10878	# control module: pin-mux conf reg for gpio1_28
PINMUX_GPIO1_28=0x27		# mmode=7 (gpio) | rxactive=1

GPIO1_OE=0x4804C134		# GPIO1 output enable, 0 = output
GPIO1_CLEARDATAOUT=0x4804C190	# self-masking: 1 -> pin low
GPIO1_SETDATAOUT=0x4804C194	# self-masking: 1 -> pin high

PIN28=0x10000000

HALF_PERIOD=0.5			# seconds; 1 Hz on/off cycle, matching the
				# other two blinky variants. A fractional
				# sleep is not POSIX, but both busybox and
				# coreutils accept one.

#-------------- devmem2 helpers ----------------------------------------------

# Read a 32-bit word and print it as a "0x"-prefixed hex value, by picking
# the value token out of devmem2's three lines of read output, e.g.:
#   /dev/mem opened.
#   Memory mapped at address 0xb6fc3000.
#   Read at address  0x44E10878 (0xb6fc3878): 0x00000037
#
#   /^Read/     only the third line starts with "Read" -- this skips the
#               other two lines (they're not data, just devmem2 narrating
#               what it did) without needing to match their content.
#   {print $NF} awk splits the matched line on whitespace and prints the
#               last field ($NF). On the "Read at address ..." line that
#               last field is the "0x00000037" value itself -- the two
#               addresses earlier on the line don't matter, we only ever
#               want whatever comes after the final space.
#
# The value comes out already "0x"-prefixed, so the caller can hand it
# straight to shell arithmetic ($(( )) accepts 0x-prefixed hex literals)
# instead of having to splice a bare hex string onto a literal "0x" itself.
#
# The empty check matters: without it a failed read would yield "", and
# $(( "" & ~PIN28 )) would be a shell arithmetic error instead of a clean
# message pointing at devmem2.
devmem_read() {
	value=$(devmem2 "$1" w | awk '/^Read/{print $NF}')

	if [ -z "$value" ]; then
		echo "blinky.sh: could not read $1 via devmem2" >&2
		exit 1
	fi
	echo "$value"
}

devmem_write() {
	devmem2 "$1" w "$2" >/dev/null
}

#-------------- Implementation -----------------------------------------------

# 1. Pin mux: point ball V18/U18 at its gpio1_28 function.
devmem_write "$CONF_GPMC_BE1N" "$PINMUX_GPIO1_28"

# 2. Direction: GPIO1_28 = output. OE is not self-masking, so this is a
# read-modify-write spread across two separate devmem2 invocations -- and it
# races with gpio-omap by construction, same as the C version, just with a
# wider window: nothing stops the kernel from writing OE between the read
# below and the write that follows it.
oe=$(devmem_read "$GPIO1_OE")
oe=$(( (oe & ~PIN28) & 0xFFFFFFFF ))
devmem_write "$GPIO1_OE" "$(printf '0x%08X' "$oe")"

echo "blinking GPIO1_28 at 1 Hz"

# 3. Blink forever. SETDATAOUT and CLEARDATAOUT are self-masking: a 1 bit
# acts on that pin, a 0 bit leaves it alone. So each toggle is one write,
# same as the C version -- no read-modify-write, and no risk to the other 31
# pins of the bank (four of which are the on-board user LEDs, GPIO1_21..24).
while true; do
	devmem_write "$GPIO1_SETDATAOUT" "$PIN28"
	sleep "$HALF_PERIOD"
	devmem_write "$GPIO1_CLEARDATAOUT" "$PIN28"
	sleep "$HALF_PERIOD"
done
