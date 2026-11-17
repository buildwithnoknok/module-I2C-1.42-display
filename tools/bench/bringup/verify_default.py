# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
# Verify the boot default is LANDSCAPE, then that all four named orientations work.
import board, time
from noknok import Conductor, BLACK, WHITE, RED, GREEN

NAMES = ("portrait", "landscape (default)", "portrait flipped", "landscape flipped")

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
d = c.display[0]
d.on(); d.backlight(200)

nfo = d.info(refresh=True)
print("BOOT DEFAULT: %s  width=%d height=%d" % (nfo, d.width, d.height))
if d.width == 160 and d.height == 80:
    print("  -> LANDSCAPE. correct.")
else:
    print("  -> NOT landscape! expected 160x80")

def frame(label_dots):
    w, h = d.width, d.height
    d.clear(BLACK)
    d.fill_rect(0, 0, w, 2, WHITE); d.fill_rect(0, h - 2, w, 2, WHITE)
    d.fill_rect(0, 0, 2, h, WHITE); d.fill_rect(w - 2, 0, 2, h, WHITE)
    d.fill_rect(4, 5, 10, 10, RED)                      # top-left marker
    for t in range(label_dots):
        d.fill_rect(20 + t * 8, 6, 6, 6, GREEN)
    d.text("noknok", size=8,  color=WHITE, x=4, y=22)
    d.text("F",      size=16, color=GREEN, x=4, y=38)

print("PART A (20s): the boot default, untouched - should be LANDSCAPE")
frame(2)
time.sleep(20)

print("PART B: each named orientation, 7s each")
for rot in (0, 1, 2, 3):
    d._send([0x11, rot])
    time.sleep(0.2)
    d.info(refresh=True)
    frame(rot + 1)
    print("  rot %d = %-20s %dx%d  tally=%d" % (rot, NAMES[rot], d.width, d.height, rot + 1))
    time.sleep(7)

d._send([0x11, 1])
time.sleep(0.2)
d.info(refresh=True)
d.clear(BLACK)
d.text("PASS", size=16, color=GREEN, x=40, y=30)
print("DONE - left in landscape, screen reads PASS")