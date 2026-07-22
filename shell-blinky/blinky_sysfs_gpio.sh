#!/bin/sh

# GPIO number for P9_23 (gpio1_17 = 32*1 + 17)
GPIO=49
GPIO_PATH=/sys/class/gpio/gpio$GPIO

# Export the pin if it isn't already
if [ ! -d "$GPIO_PATH" ]; then
    echo $GPIO > /sys/class/gpio/export
    sleep 1
fi

# Configure the pin as an output
echo out > "$GPIO_PATH/direction"

# Blink the pin 5 times
for i in $(seq 5); do
    echo 1 > "$GPIO_PATH/value"  # Turn on
    sleep 1
    echo 0 > "$GPIO_PATH/value"  # Turn off
    sleep 1
done

# Release the pin
echo $GPIO > /sys/class/gpio/unexport
