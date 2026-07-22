#!/bin/sh

# LED brightness file
LED=/sys/class/leds/beaglebone:green:usr3/brightness

# Blink the LED 5 times
for i in $(seq 5); do
    echo 1 > "$LED"  # Turn on
    sleep 1
    echo 0 > "$LED"  # Turn off
    sleep 1
done
