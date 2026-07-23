# Bring-up firmware — 1.42″ Display Module

Standalone bench firmware for a **brand-new, never-powered** display board.
Linked at `0x0000`, SWD-flashed directly — it does **not** run under the shared
noknok I2C bootloader and is **not** an I2C module. It answers three questions
and then its job is done.

## Before you power the board

Meter these with the board **unpowered** (all three were verified in the
schematic + PCB netlist, this is just the physical check):

| Check | Expect |
|---|---|
| 3.3V-IN ↔ GND | no short |
| +4.5V (boost output, C6) ↔ GND | no short |
| Panel VCC (DS1 pin 10) ↔ +3.3V-PROT | continuity |

Then power from the Qwiic bus and check **+4.5 V ±0.2 V** on C6 before flashing.

## Flashing

Factory-fresh CH32V003 ships **read-protected** — unlock once, then flash:

```bash
minichlink -p
make flash
```

Contact the 3-pad pogo footprint (GND / SWIO / VCC). Castellated-style
hand-pressed contact is unreliable; if `make flash` reports *"nothing connected
to linker"* or `marchid ffffffff`, that is a **contact/power** problem, not a
Link-E problem — work the contact and retry in a loop.

## What you will see

**Phase 1 (~4 s) — backlight only.** The panel is held in reset; the backlight
ramps 0 → 100 % → 0. The screen should glow evenly with no image.

* No glow → boost / 33 Ω / 2N7002 / PD2 chain. Scope PD2 for a 48 kHz PWM.
* Glow → the MT3540 boost, the ballast and the PWM MOSFET are all good.

**Phase 2 (loop) — offset hunt.** The panel is initialised, then the firmware
cycles through candidate window offsets, ~3 s each. Before each one the status
LED blinks **N** times (N = candidate number) so you can identify it even if the
screen stays black. Each candidate draws:

* a 2 px **white border** around the full 80×160 area
* four **12×12 corner squares** — red (top-left), green (top-right),
  yellow (bottom-left), cyan (bottom-right)
* **N white tally dots** down the middle

**The correct offset is the candidate where the border is complete on all four
edges and all four corner squares are fully visible.** Note the number and tell
Sam — it gets hard-coded into the real firmware and this file is retired.

Red must appear **top-left** with the FPC exiting at the bottom. If it doesn't,
`MADCTL_VALUE` needs adjusting. If red and blue are swapped, set `MADCTL_VALUE`
to `0x08` (BGR).

## If every candidate is black

Not an offset problem. In order of likelihood:

1. `COLMOD_VALUE` — try `0x55` instead of `0x05`.
2. `SPI_BAUD_DIV` — drop to `SPI_DIV_8` (6 MHz) or `SPI_DIV_16` (3 MHz).
3. The init table is a **generic NV3022B vendor sequence** (from Espressif's
   `esp_lcd_nv3022b`), not EastRising's panel-specific one. If EastRising's
   ER-TFT1.42-1 demo code is available, replace `nv3022b_init_seq[]` wholesale.
4. Scope PC3 (D/C) and PC4 (RES) — D/C must be low only during command bytes.

## Build

On the RPi4, as a sibling of `ch32fun/`:

```bash
cd /home/noknok/dev/ch32fun/noknok_display && make build
```
