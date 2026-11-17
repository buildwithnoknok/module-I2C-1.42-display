# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
# "noknok" in brand purple - true value vs the compensated one, side by side.
import board, time
from noknok import Conductor, LANDSCAPE, BLACK, WHITE

PURPLE_TRUE = 0x8B00F3   # the real brand colour        -> rgb565 0x881E
PURPLE_COMP = 0xD800E0   # encodes to rgb565 0xD81C     -> the compensated one

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
d = c.display[0]
d.on(); d.backlight(200)
d.rotation(LANDSCAPE)
print("landscape %dx%d" % (d.width, d.height))

d.clear(BLACK)
d.text("noknok", size=16, color=PURPLE_TRUE, bg=BLACK, x=4, y=6)
d.text("true #8B00F3", size=8, color=WHITE, bg=BLACK, x=4, y=26)
d.text("noknok", size=16, color=PURPLE_COMP, bg=BLACK, x=4, y=44)
d.text("compensated", size=8, color=WHITE, bg=BLACK, x=4, y=64)

print("TOP    = noknok in the TRUE brand purple #8B00F3")
print("BOTTOM = noknok in the compensated purple (rgb565 0xD81C)")
print("Holding 90 s.")
time.sleep(90)
print("done")