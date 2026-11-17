# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
# Three labelled bands so holding orientation cannot confuse the result.
import board, time
from noknok import Conductor, BLACK, WHITE, BLUE

PURPLE_TRUE = 0x8B00F3   # real brand colour        -> rgb565 0x881E
PURPLE_COMP = 0xD800E0   # encodes to rgb565 0xD81C -> the rework-board hack

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
d = c.display[0]
d.on(); d.backlight(200)
W, H = d.width, d.height

d.clear(BLACK)
# band 1: the true brand value
d.fill_rect(0,  0, W, 50, PURPLE_TRUE)
d.text("TRUE", size=8, color=BLACK, bg=PURPLE_TRUE, x=6, y=20)
# divider
d.fill_rect(0, 50, W,  4, WHITE)
# band 2: the compensated value
d.fill_rect(0, 54, W, 50, PURPLE_COMP)
d.text("HACK", size=8, color=BLACK, bg=PURPLE_COMP, x=6, y=74)
# divider
d.fill_rect(0, 104, W, 4, WHITE)
# band 3: pure blue reference
d.fill_rect(0, 108, W, 52, BLUE)
d.text("BLUE", size=8, color=WHITE, bg=BLUE, x=6, y=130)

print("Three bands are up, labelled TRUE / HACK / BLUE.")
print("  TRUE = #8B00F3, the real brand purple")
print("  HACK = the magenta-shifted value the reworked board needed")
print("  BLUE = pure blue, as a reference")
print("Holding for 60 s - read the LABELS, ignore which way round you hold it.")
time.sleep(60)
print("done")