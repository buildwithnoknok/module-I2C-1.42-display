# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
# Exactly how a maker would write it: import the named orientation, set it once.
import board, time
from noknok import Conductor, PORTRAIT, LANDSCAPE, PORTRAIT_FLIPPED, LANDSCAPE_FLIPPED
from noknok import BLACK, WHITE, RED, GREEN

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
d = c.display[0]
d.on(); d.backlight(200)

maj, mnr, pat = d.version() if hasattr(d, "version") else (None, None, None)
print("firmware: %s.%s.%s" % (maj, mnr, pat))
print("BOOT DEFAULT: %dx%d  -> %s" % (d.width, d.height,
      "LANDSCAPE, correct" if (d.width, d.height) == (160, 80) else "NOT landscape!"))

def frame(dots, caption):
    w, h = d.width, d.height
    d.clear(BLACK)
    d.fill_rect(0, 0, w, 2, WHITE); d.fill_rect(0, h - 2, w, 2, WHITE)
    d.fill_rect(0, 0, 2, h, WHITE); d.fill_rect(w - 2, 0, 2, h, WHITE)
    d.fill_rect(4, 5, 10, 10, RED)
    for t in range(dots):
        d.fill_rect(20 + t * 8, 6, 6, 6, GREEN)
    d.text(caption, size=8,  color=WHITE, x=4, y=22)
    d.text("F",     size=16, color=GREEN, x=4, y=36)

print("PART A (15s): boot default, nothing set")
frame(2, "default")
time.sleep(15)

print("PART B: set each orientation BY NAME, 6s each")
for name, const in (("portrait", PORTRAIT), ("landscape", LANDSCAPE),
                    ("portrait_flipped", PORTRAIT_FLIPPED),
                    ("landscape_flipped", LANDSCAPE_FLIPPED)):
    d.rotation(name)                      # <-- by NAME, the maker-facing form
    frame(const + 1, name[:10])
    print("  d.rotation(%-20r) -> %dx%d  tally=%d" % (name, d.width, d.height, const + 1))
    time.sleep(6)

print("PART C: by CONSTANT, and a bad value should raise")
d.rotation(LANDSCAPE)
print("  d.rotation(LANDSCAPE) -> %dx%d" % (d.width, d.height))
try:
    d.rotation("sideways")
    print("  FAIL: bad name was accepted")
except ValueError as e:
    print("  bad name correctly rejected:", e)

d.clear(BLACK)
d.text("PASS", size=16, color=GREEN, x=40, y=30)
print("DONE - left in LANDSCAPE")