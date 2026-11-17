# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
# With the vendor init in place, answer the three questions.
import board, time
from noknok import Conductor, LANDSCAPE, BLACK, WHITE, RED, GREEN, BLUE

PURPLE_TRUE = 0x8B00F3   # the REAL brand colour -> rgb565 0x881E
PURPLE_COMP = 0xD800E0   # the old compensated hack -> rgb565 0xD81C

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
if not c.display:
    print("FAIL: nothing enumerated - vendor init may have broken the module")
    raise SystemExit
d = c.display[0]
d.on(); d.backlight(200)
print("enumerated: 0x%02X  %s  %dx%d" % (d.address, d.info(refresh=True), d.width, d.height))

# ---- Q1: does TRUE brand purple finally look purple? ----
print("Q1 (30s): purple A/B - TOP = TRUE #8B00F3, BOTTOM = old compensated hack")
d.clear(BLACK)
W, H = d.width, d.height
half = (H - 6) // 2
d.fill_rect(0, 0, W, half, PURPLE_TRUE)
d.text("TRUE", size=8, color=BLACK, bg=PURPLE_TRUE, x=4, y=half//2 - 4)
d.fill_rect(0, half, W, 6, WHITE)
d.fill_rect(0, half + 6, W, half, PURPLE_COMP)
d.text("HACK", size=8, color=BLACK, bg=PURPLE_COMP, x=4, y=half + 6 + half//2 - 4)
time.sleep(30)

# ---- Q2: sanity on the primaries ----
print("Q2 (8s): primaries - RED GREEN BLUE WHITE, 2s each")
for nm, col in (("RED", RED), ("GREEN", GREEN), ("BLUE", BLUE), ("WHITE", WHITE)):
    d.clear(col); print("   ", nm); time.sleep(2)

# ---- Q3: with vendor 0xC3 ss/gs set, is MADCTL 0x00 now correct? ----
print("Q3: MADCTL sweep via SET_ORIENT - which gives UNMIRRORED text?")
for num, madctl, note in ((1, 0x00, "vendor's own value"), (2, 0x40, "what the minimal init needed")):
    d.orient(madctl, 0, 24, 160, 80) if hasattr(d, "orient") else d._send([0x14, madctl, 0, 24, 160, 80])
    time.sleep(0.2)
    d.clear(BLACK)
    d.fill_rect(0, 0, 160, 2, WHITE); d.fill_rect(0, 78, 160, 2, WHITE)
    d.fill_rect(0, 0, 2, 80, WHITE); d.fill_rect(158, 0, 2, 80, WHITE)
    d.fill_rect(4, 5, 10, 10, RED)
    for t in range(num):
        d.fill_rect(20 + t * 10, 6, 8, 8, GREEN)
    d.text("noknok", size=8,  color=WHITE, x=4, y=24)
    d.text("F",      size=16, color=GREEN, x=4, y=40)
    print("   %d dot(s): MADCTL 0x%02X  (%s)" % (num, madctl, note))
    time.sleep(10)

d.rotation(LANDSCAPE)
d.clear(BLACK)
d.text("noknok", size=16, color=PURPLE_TRUE, bg=BLACK, x=30, y=20)
d.text("true purple", size=8, color=WHITE, bg=BLACK, x=30, y=46)
print("DONE - wordmark left on screen in TRUE #8B00F3")