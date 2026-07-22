import Adafruit_BBIO.GPIO as GPIO
import time

# Set up the GPIO pin
LED_PIN = "P9_14"
GPIO.setup(LED_PIN, GPIO.OUT)

# Blink the LED 5 times
for i in range(5):
    GPIO.output(LED_PIN, GPIO.HIGH) # Turn on
    time.sleep(1)
    GPIO.output(LED_PIN, GPIO.LOW)  # Turn off
    time.sleep(1)

GPIO.cleanup()
