PROJECT = display_bringup
SOURCES = display_bringup.c
TARGET  = display_bringup

TARGET_MCU ?= CH32V003

# NO LINKER_SCRIPT override on purpose: this bring-up image is STANDALONE,
# linked at 0x0000 and SWD-flashed directly. It does not run under the shared
# noknok I2C bootloader. The real module firmware will relink at 0x1000.
#
# Factory-fresh CH32V003 ships read-protected — run `minichlink -p` once on a
# new board before the first `make flash`.

include ../ch32fun/ch32fun.mk

all   : build
flash : cv_flash
clean : cv_clean
