# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
# Colours here are 24-bit 0xRRGGBB - that is what noknok.py expects.
# Passing RGB565 ints makes rgb565() read them as 24-bit and mangles everything.
import board, time
from noknok import Conductor, BLACK, WHITE, RED, GREEN, BLUE

PURPLE_TRUE = 0x8B00F3   # the real brand colour      -> rgb565 0x881E
PURPLE_COMP = 0xD800E0   # encodes to rgb565 0xD81C   -> the rework-board hack

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
if not c.display:
    print("FAIL: no display enumerated")
    raise SystemExit
d = c.display[0]
d.on(); d.backlight(200)
W, H = d.width, d.height
print("addr=0x%02X  %dx%d  %s" % (d.address, W, H, d.info(refresh=True)))

print(">>> GET READY - 3 WHITE flashes (they must look WHITE, not cyan)")
for _ in range(3):
    d.clear(WHITE); time.sleep(0.25)
    d.clear(BLACK); time.sleep(0.25)
time.sleep(1.5)

print("PHASE 1 (6s): full screen RED, GREEN, BLUE, WHITE")
for nm, col in (("RED", RED), ("GREEN", GREEN), ("BLUE", BLUE), ("WHITE", WHITE)):
    d.clear(col); print("   ", nm); time.sleep(1.5)

print("PHASE 2 (4s): bars top->bottom RED GREEN BLUE WHITE PURPLE")
d.clear(BLACK)
bh = H // 5
for i, col in enumerate((RED, GREEN, BLUE, WHITE, PURPLE_TRUE)):
    d.fill_rect(0, i * bh, W, bh, col)
time.sleep(4)

print("PHASE 3 (6s): white border + corners RED(TL) GREEN(TR) BLUE(BL) PURPLE(BR)")
d.clear(BLACK)
d.fill_rect(0, 0, W, 2, WHITE); d.fill_rect(0, H - 2, W, 2, WHITE)
d.fill_rect(0, 0, 2, H, WHITE); d.fill_rect(W - 2, 0, 2, H, WHITE)
d.fill_rect(3, 3, 12, 12, RED)
d.fill_rect(W - 15, 3, 12, 12, GREEN)
d.fill_rect(3, H - 15, 12, 12, BLUE)
d.fill_rect(W - 15, H - 15, 12, 12, PURPLE_TRUE)
time.sleep(6)

print("PHASE 4 (8s): PURPLE A/B - TOP = true #8B00F3, BOTTOM = rework-board hack")
d.clear(BLACK)
d.fill_rect(0, 0, W, 76, PURPLE_TRUE)
d.fill_rect(0, 78, W, 4, WHITE)
d.fill_rect(0, 84, W, 76, PURPLE_COMP)
time.sleep(8)

print("PHASE 5 (5s): native font, sizes 16 and 8 - text must be WHITE/GREEN")
d.clear(BLACK)
d.text("nok", size=16, color=WHITE, x=2, y=6)
d.text("noknok", size=8, color=GREEN, x=2, y=32)
d.text("80x160", size=8, color=WHITE, x=2, y=46)
time.sleep(5)

print("PHASE 6 (5s): size 20 = non-native -> 1bpp host blit path, WHITE")
d.clear(BLACK)
d.text("blit", size=20, color=WHITE, x=2, y=14)
time.sleep(5)

print("PHASE 7 (2s): backlight ramp")
for lv in (160, 110, 60, 110, 200):
    d.backlight(lv); time.sleep(0.35)

print("status:", d.status(), " ready:", d.wait_ready(timeout=1.0))
d.clear(BLACK)
d.text("PASS", size=16, color=GREEN, x=6, y=64)
print("DONE - screen should read PASS in GREEN")