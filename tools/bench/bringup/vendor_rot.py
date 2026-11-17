# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
# Vendor init + canonical MADCTL table. Check text is unmirrored in all four.
import board, time
from noknok import Conductor, LANDSCAPE, BLACK, WHITE, RED, GREEN

PURPLE = 0x8B00F3        # the REAL brand colour - no compensation any more
NAMES = ("portrait", "landscape (default)", "portrait flipped", "landscape flipped")

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
d = c.display[0]
d.on(); d.backlight(200)
print("boot: %s  %dx%d" % (d.info(refresh=True), d.width, d.height))

def frame(dots):
    w, h = d.width, d.height
    d.clear(BLACK)
    d.fill_rect(0, 0, w, 2, WHITE); d.fill_rect(0, h - 2, w, 2, WHITE)
    d.fill_rect(0, 0, 2, h, WHITE); d.fill_rect(w - 2, 0, 2, h, WHITE)
    d.fill_rect(4, 5, 10, 10, RED)
    for t in range(dots):
        d.fill_rect(20 + t * 9, 6, 7, 7, GREEN)
    d.text("noknok", size=8,  color=WHITE, x=4, y=22)
    d.text("F",      size=16, color=GREEN, x=4, y=36)

print("PART A (18s): boot default, untouched")
frame(2)
time.sleep(18)

print("PART B: all four, 8s each - F must read correctly, border complete")
for rot in (0, 1, 2, 3):
    d.rotation(rot)
    frame(rot + 1)
    print("  %d dots = %-20s %dx%d" % (rot + 1, NAMES[rot], d.width, d.height))
    time.sleep(8)

d.rotation(LANDSCAPE)
d.clear(BLACK)
d.text("noknok", size=16, color=PURPLE, bg=BLACK, x=28, y=22)
d.text("real #8B00F3", size=8, color=WHITE, bg=BLACK, x=28, y=48)
print("DONE - wordmark in the REAL brand purple, no compensation")