#!/bin/sh
set -eu

# Blink P9_23 five times (1s on / 1s off) with libgpiod v2, locating the
# line at runtime so a kernel upgrade can't break it.
#
# blinky_libgpiod_2.sh hardcoded /dev/gpiochip1, but the kernel doesn't
# guarantee stable gpiochip numbering -- after an upgrade the bank that
# used to be gpiochip1 can appear as a different chip, so the offset lands
# on the wrong pin (or none). Instead of trusting the number we find the
# line two ways, both of which survive renumbering.

LINE_NAME=P9_23
BANK_ADDR=4804c000   # AM335x GPIO1 bank base address (fixed in silicon)
OFFSET=17            # P9_23 = GPIO1_17 = global gpio49

# Preferred path: if a gpio-line-names overlay is loaded, let gpioset
# resolve the name itself -- with no -c it searches every chip, so the
# chip number is irrelevant.
if gpioinfo 2>/dev/null | grep -q "\"$LINE_NAME\""; then
    exec gpioset -t 1s,1s,1s,1s,1s,1s,1s,1s,1s,1s,0 "$LINE_NAME=1"
fi

# Fallback: no named lines. Find the gpiochip backing the GPIO1 bank by
# matching its platform address in sysfs (the /sys path embeds the fixed
# "4804c000.gpio" device node regardless of the /dev number), then drive
# it by offset.
CHIP=
for d in /sys/bus/gpio/devices/gpiochip*; do
    [ -e "$d" ] || continue   # no glob match -> literal, skip
    case "$(readlink -f "$d")" in
        *"$BANK_ADDR"*) CHIP="/dev/$(basename "$d")"; break ;;
    esac
done

if [ -z "$CHIP" ]; then
    echo "Could not find the GPIO1 bank ($BANK_ADDR.gpio) -- run 'gpiodetect'." >&2
    exit 1
fi

exec gpioset -c "$CHIP" -t 1s,1s,1s,1s,1s,1s,1s,1s,1s,1s,0 "$OFFSET=1"
