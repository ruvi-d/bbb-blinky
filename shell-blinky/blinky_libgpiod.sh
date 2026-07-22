#!/bin/sh

# Blink P9_23 five times (1s on / 1s off) with a single gpioset (libgpiod v2) call.
# The line is addressed by its device-tree name, so no chip/offset needs to be
# hardcoded, and one persistent request holds the line for the whole sequence
# instead of releasing it between toggles.
#
# Periods alternate on/off for 1s each; the trailing 0 tells gpioset to exit
# once the last period elapses instead of repeating the sequence.
gpioset -t 1s,1s,1s,1s,1s,1s,1s,1s,1s,1s,0 P9_23=1
