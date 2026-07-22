import glob
import time

import gpiod
from gpiod.line import Direction, Value

# Pin the LED is wired to
LED_PIN = "P9_23"


def find_line(pin_name):
    # Find which gpiochip and line offset a named pin belongs to
    for chip_path in sorted(glob.glob("/dev/gpiochip*")):
        with gpiod.Chip(chip_path) as chip:
            for offset in range(chip.get_info().num_lines):
                if chip.get_line_info(offset).name == pin_name:
                    return chip_path, offset
    raise RuntimeError(f"no gpio line named {pin_name!r} found")


chip_path, line_offset = find_line(LED_PIN)

# Configure the pin as an output
settings = gpiod.LineSettings(direction=Direction.OUTPUT, output_value=Value.INACTIVE)
request = gpiod.request_lines(chip_path, consumer="blinky", config={line_offset: settings})

# Blink the LED 5 times
for _ in range(5):
    request.set_value(line_offset, Value.ACTIVE)  # Turn on
    time.sleep(1)
    request.set_value(line_offset, Value.INACTIVE)  # Turn off
    time.sleep(1)

# Release the pin
request.release()
