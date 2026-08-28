PROJECT = reveal_demo
SOURCES = reveal_demo.c
TARGET  = reveal_demo

TARGET_MCU ?= CH32V003

# NO LINKER_SCRIPT override on purpose: like the bring-up image this is
# STANDALONE, linked at 0x0000 and SWD-flashed directly. It does not run under
# the noknok I2C bootloader and it is not an I2C module — it is a marketing
# demo that owns the whole board.
#
# Flashing overwrites the bootloader + app. To turn the board back into a real
# noknok module afterwards, reflash the combined bootloader+app image
# (see Sam.md, "Flashing Interface V3").

include ../ch32fun/ch32fun.mk

all   : build
flash : cv_flash
clean : cv_clean
