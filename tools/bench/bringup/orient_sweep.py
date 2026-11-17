# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
# Sweep MADCTL + GRAM offset candidates over I2C using CMD_SET_ORIENT (0x14).
# [0x14, madctl, xoff, yoff, w, h] sets them live - no reflash needed.
#
# Tally squares along the TOP edge = candidate number (orientation-proof).
# The word "noknok" is the mirror detector. RED square marks the TRUE top-left.
import board, time
from noknok import Conductor, BLACK, WHITE, RED, GREEN

CANDIDATES = (
    (1, 0x00, 24, 0, "current - expected MIRRORED (reference)"),
    (2, 0x40, 28, 0, "MX, offset 28 (132-wide RAM mirrored)"),
    (3, 0x40, 24, 0, "MX, offset 24 (if RAM were 128 wide)"),
    (4, 0x80, 24, 2, "MY, yoff 2 (162-tall RAM mirrored)"),
    (5, 0x80, 24, 0, "MY, yoff 0"),
)
W, H = 80, 160

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
d = c.display[0]
d.on(); d.backlight(200)

for num, madctl, xoff, yoff, note in CANDIDATES:
    d._send([0x14, madctl, xoff, yoff, W, H])
    time.sleep(0.15)
    d.clear(BLACK)
    # 2px border - proves every edge is reachable at this offset
    d.fill_rect(0, 0, W, 2, WHITE); d.fill_rect(0, H - 2, W, 2, WHITE)
    d.fill_rect(0, 0, 2, H, WHITE); d.fill_rect(W - 2, 0, 2, H, WHITE)
    # tally squares along the top, 8px each, starting 6px in
    for t in range(num):
        d.fill_rect(6 + t * 10, 6, 8, 8, GREEN)
    # RED block marks where the firmware thinks top-left is
    d.fill_rect(4, 20, 14, 14, RED)
    # mirror detector
    d.text("noknok", size=8, color=WHITE, x=4, y=44)
    d.text("abc", size=16, color=WHITE, x=4, y=60)
    print("candidate %d: MADCTL 0x%02X off=(%d,%d)  %s" % (num, madctl, xoff, yoff, note))
    time.sleep(7)

# leave it on the current/reference setting
d._send([0x14, 0x00, 24, 0, W, H])
d.clear(BLACK)
d.text("done", size=16, color=GREEN, x=4, y=70)
print("sweep done - which tally count showed UNMIRRORED text AND a complete border?")