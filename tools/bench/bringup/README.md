<!-- SPDX-FileCopyrightText: 2026 noknok (Christopher Houben) -->
<!-- SPDX-License-Identifier: MIT -->
# tools/bench/bringup - Display bring-up and vendor-init reproduction scripts

The scripts that produced the orientation, GRAM-offset, vendor-init and brand-purple findings documented
in the module README (rev 1.0/1.1 bring-up, Sep/Oct 2026). Kept as the reproduction path (DEV-77). All run
on a Pico with `noknok.py` (brain-Pico `tools/pico.py run <file>`); most assume the Display on GP20/GP21
(`Conductor(sda=board.GP20, scl=board.GP21)`) - change the pins for your wiring. Visual checks: look at the panel.

| File | Checks |
|---|---|
| `enum_display.py` | the Display enumerates; prints address, firmware, info |
| `test_display.py` | basic drawing in 24-bit colours (0xRRGGBB, not RGB565) |
| `orient_sweep.py` | MADCTL + GRAM offset candidates live over I2C (`CMD_SET_ORIENT 0x14`), numbered squares |
| `verify_default.py`, `verify_rot.py`, `maker_orient.py` | boot default is LANDSCAPE; all four named orientations unmirrored, written the way a maker would |
| `vendor_test.py`, `vendor_rot.py` | the vendor init: colours, inversion, MADCTL table in all four orientations |
| `purple_ab.py`, `purple_wordmark.py` | brand purple: true value vs the old compensated one, side by side |
