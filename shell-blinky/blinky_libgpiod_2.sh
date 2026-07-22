#!/bin/sh

# Blink P9_23 five times (1s on / 1s off) using chip+offset addressing
# instead of a device-tree line name. Use this variant when no
# gpio-line-names overlay is loaded, so gpioset can't resolve "P9_23"
# by name (run `gpioinfo` to check for named lines).
#
# P9_23 = GPIO1_17, i.e. offset 17 on the gpio1 bank. On a mainline BBB
# kernel that bank is exposed as /dev/gpiochip1; if this doesn't match
# your kernel, run `gpioinfo` and adjust CHIP/OFFSET below.
CHIP=gpiochip1
OFFSET=17

gpioset -c "$CHIP" -t 1s,1s,1s,1s,1s,1s,1s,1s,1s,1s,0 $OFFSET=1
