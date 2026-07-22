import time

import Adafruit_BBIO.GPIO as GPIO

# Pin the LED is wired to
LED_PIN = "P9_23"

# Configure the pin as an output
GPIO.setup(LED_PIN, GPIO.OUT)

# Blink the LED 5 times
for _ in range(5):
    GPIO.output(LED_PIN, GPIO.HIGH)  # Turn on
    time.sleep(1)
    GPIO.output(LED_PIN, GPIO.LOW)  # Turn off
    time.sleep(1)

# Release the pin
GPIO.cleanup()
