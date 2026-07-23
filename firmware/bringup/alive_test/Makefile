PROJECT = alive_test
SOURCES = alive_test.c
TARGET  = alive_test

TARGET_MCU ?= CH32V003

# Standalone, linked at 0x0000, SWD-flashed. Minimal bisection image:
# status LED + backlight PWM only, no SPI / panel / I2C.

include ../ch32fun/ch32fun.mk

all   : build
flash : cv_flash
clean : cv_clean
