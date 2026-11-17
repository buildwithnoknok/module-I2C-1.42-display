# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
import board, time
from noknok import Conductor, BLACK, WHITE, RED, GREEN, BLUE

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
if not c.display:
    print("FAIL: no display enumerated")
    raise SystemExit
d = c.display[0]
d.on(); d.backlight(200)
print("addr=0x%02X  info=%s  width=%s height=%s"
      % (d.address, d.info(refresh=True), d.width, d.height))

# ---- PART A: default portrait, MADCTL 0x40 ----
W, H = d.width, d.height
d.clear(BLACK)
d.fill_rect(0, 0, W, 2, WHITE); d.fill_rect(0, H - 2, W, 2, WHITE)
d.fill_rect(0, 0, 2, H, WHITE); d.fill_rect(W - 2, 0, 2, H, WHITE)
d.fill_rect(4, 6, 12, 12, RED)              # marks top-left
d.fill_rect(W - 16, 6, 12, 12, GREEN)       # marks top-right
d.fill_rect(4, H - 18, 12, 12, BLUE)        # marks bottom-left
d.text("noknok", size=8,  color=WHITE, x=4, y=30)
d.text("abc",    size=16, color=WHITE, x=4, y=46)
d.text("F",      size=24, color=GREEN, x=4, y=70)
print("PART A (25s): default portrait. Text must read the RIGHT WAY ROUND now.")
print("  RED=top-left  GREEN=top-right  BLUE=bottom-left, all hard against the border")
time.sleep(25)

# ---- PART B: exercise SET_ROTATION 0..3 ----
for rot in (0, 1, 2, 3):
    d._send([0x11, rot])
    time.sleep(0.2)
    nfo = d.info(refresh=True)
    w, h = d.width, d.height
    d.clear(BLACK)
    d.fill_rect(0, 0, w, 2, WHITE); d.fill_rect(0, h - 2, w, 2, WHITE)
    d.fill_rect(0, 0, 2, h, WHITE); d.fill_rect(w - 2, 0, 2, h, WHITE)
    d.fill_rect(4, 6, 10, 10, RED)
    for t in range(rot + 1):
        d.fill_rect(20 + t * 8, 6, 6, 6, GREEN)
    d.text("noknok", size=8, color=WHITE, x=4, y=24)
    d.text("F", size=16, color=GREEN, x=4, y=40)
    print("  rot %d -> %s  (%dx%d)  tally=%d green dots, RED=top-left"
          % (rot, nfo, w, h, rot + 1))
    time.sleep(7)

d._send([0x11, 0])
time.sleep(0.2)
d.info(refresh=True)
d.clear(BLACK)
d.text("PASS", size=16, color=GREEN, x=6, y=64)
print("DONE - back to rotation 0, screen reads PASS in green")