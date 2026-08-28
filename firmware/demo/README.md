# Reveal demo firmware — 1.42″ Display Module

Self-running "boot to life" loop for the module's **first public reveal video**.
Not product code: no I2C, no enumeration, no bootloader. A standalone image
linked at `0x0000` and SWD-flashed straight onto a bench board — power it and it
runs forever on a seamless loop.

Runs the panel **landscape (160×80)**.

## The loop (≈6 s)

| | | |
|---|---|---|
| 1 | Black, backlight off | 0.3 s |
| 2 | Purple field + white **noknok** wordmark, backlight fades up | 1.0 s hold |
| 3 | Fade down, swap to black, fade back up | 0.4 s |
| 4 | `Hello, World!` types out character by character | ~1.2 s |
| 5 | Block cursor blinks after it | ~3.0 s |
| 6 | Backlight fades out → back to step 1 | 0.35 s |

Step 1 leaves the panel black with the backlight already at zero — exactly the
state step 6 hands over — so the loop point is invisible and the footage cuts as
a seamless reel.

## Build and flash

On the RPi4, as a sibling of `ch32fun/`:

```bash
cd /home/noknok/dev/ch32fun/noknok_display_demo && make build && make flash
```

Contact the 3-pad pogo footprint (GND / SWIO / VCC). If `make flash` reports
*"nothing connected to linker"* or `marchid ffffffff`, that is a **contact or
power** problem, not a Link-E problem — work the contact and retry.

> **Flashing this overwrites the bootloader and the module app.** To turn the
> board back into a real noknok module afterwards, reflash the combined
> bootloader+app image (Sam.md, *Flashing Interface V3*).

## If it comes out wrong on camera

Each is a one-line change at the top of `reveal_demo.c`; nothing else depends
on them.

| Symptom | Fix |
|---|---|
| Image mirrored left-to-right | flip the MY bit: `MADCTL_LANDSCAPE` `0x20` <-> `0xE0` |
| Image upside down | flip the MX bit: `MADCTL_LANDSCAPE` `0x20` <-> `0x60` |
| Image shifted a few px up/down | nudge `LAND_Y_OFF` (24 assumes a 128-wide GRAM; a 132-wide one wants 26) |
| Colours inverted | drop the `INVON` line in `gpu_init()` |
| Purple looks blue | gamma, not encoding — see **Purple calibration** below; set `SWATCH_MODE 1` to re-pick |
| Colours garbage | `COLMOD_VALUE` `0x55` instead of `0x05` |
| Too bright (dark room) | `BL_FULL` — 250 = 25% duty; 150 very dim, 400 dim room, 999 full |
| Typing too fast/slow | `TYPE_MS` (85 ms per character) |
| Cursor blinks too fast | `BLINK_MS` / `BLINK_COUNT` |
| Boot log too fast/slow | `TERM_MS` (22 ms/char), `TERM_PAUSE` (beat between lines) |
| Boot log text or wrap | `term_lines[]` — hard-wrapped, keep each row under 156 px |


## Purple calibration — why the brand colour is not the brand value

`NOKNOK_PURPLE` is **`0xD81C`**, not the `0x881E` that `#8B00F3` actually
encodes to. Sending the correct value renders **blue** on this panel.

The cause is gamma. We run the minimal DCS init with **no vendor gamma table**
(the generic one killed the panel during bring-up), so the controller sits on
its power-on default curve — and that curve crushes **mid-level** channels
hardest. `#8B00F3` is R 54.8 % / B 96.8 %: the blue sails through untouched
while the red is flattened out of the mix, and violet collapses to blue.

`0xD81C` is R 27 / B 28, nominally about `#DE00E6` — a **magenta**. We send
magenta to get purple back out, which is the measure of how far off the
default red curve is.

To re-pick (different panel, different lighting, different camera): set
`SWATCH_MODE` to `1`, flash, and six candidates appear edge to edge,
numbered 1-6. Judge them **through the camera**, not by eye. Copy the winner
into `NOKNOK_PURPLE` and set `SWATCH_MODE` back to `0`.

> **Do not propagate `0xD81C` into product code.** It is panel-specific
> compensation for a rework board on default gamma. `#8B00F3` / `0x881E`
> stays the canonical brand value in the module firmware. Revisit once
> EastRising's real gamma table is in — at which point the correct value
> should simply render correctly.

## Panel configuration

Every panel number is lifted verbatim from the bench-proven bring-up
(`../bringup/display_bringup.c`, commit `95ab605`): minimal 5-command DCS init,
`INVON`, GRAM offset (24,0), `COLMOD 0x05`, SPI at PCLK/8. The only change is
`MADCTL`, which rotates the panel into landscape. The portrait offset (24,0)
swaps with the `MV` bit to (0,24); 24 is the centre of a 128-wide GRAM, which
makes it symmetric under the `MX`/`MY` mirror bits, so the value holds either
way up.

**Bench result, 28 Aug 2026 — this panel needs a non-canonical MADCTL.** Both
canonical landscape rotations of the working portrait value `0xC0` — `0xA0`
(MV|MY) and `0x60` (MV|MX) — come out **mirrored** left-to-right on a rescued
board, not merely rotated. `0x20` (`0xA0` with MY cleared) is what renders
correctly. Probably a rework artefact, so a correctly-fabbed board may well
want a canonical value — recheck this on the respin.

## Why proportional text

The module firmware draws the 8×8 font monospaced, which is right for a UI but
too wide here: `Hello, World!` monospaced at 16 px tall is 208 px and the panel
is 160. Trimming each glyph to its actual ink columns brings the same string
down to **144 px**, so it fits at full size with the cursor after it. The
wordmark uses the same path with a one-pixel bold smear, which turns thin
terminal text into something that reads as a logotype.
