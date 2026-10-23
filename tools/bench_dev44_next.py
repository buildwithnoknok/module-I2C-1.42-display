# SPDX-License-Identifier: MIT
# bench_dev44_next.py - DEV-44: first thing in the next script - read the Display status before anything else
import board, busio
i2c = busio.I2C(board.GP21, board.GP20, frequency=100000)
r = bytearray(2)
while not i2c.try_lock(): pass
i2c.readfrom_into(0x08, r)
i2c.unlock()
print("next-script first status: busy %d err %d -> %s" % (r[0], r[1], "PASS" if r[1] == 0 else "FAIL"))
i2c.deinit()
