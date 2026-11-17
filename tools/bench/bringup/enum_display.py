# SPDX-FileCopyrightText: 2026 noknok (Christopher Houben)
# SPDX-License-Identifier: MIT
#
import board
from noknok import Conductor

c = Conductor(sda=board.GP20, scl=board.GP21)
c.enumerate_all()
print("--- result ---")
print("displays found:", len(c.display))
for d in c.display:
    print("  addr=0x%02X uid=%s" % (d.address, d._uid_hex))
    try:
        print("  version:", d.get_version())
    except Exception as e:
        print("  get_version failed:", repr(e))
    try:
        print("  info:", d.get_info())
    except Exception as e:
        print("  get_info failed:", repr(e))