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
| Text upside down | flip `MADCTL_LANDSCAPE` between `0xA0` and `0x60` |
| Image shifted a few px up/down | nudge `LAND_Y_OFF` (24 assumes a 128-wide GRAM; a 132-wide one wants 26) |
| Colours inverted | drop the `INVON` line in `gpu_init()` |
| Colours garbage | `COLMOD_VALUE` `0x55` instead of `0x05` |
| Typing too fast/slow | `TYPE_MS` (85 ms per character) |
| Cursor blinks too fast | `BLINK_MS` / `BLINK_COUNT` |

## Panel configuration

Every panel number is lifted verbatim from the bench-proven bring-up
(`../bringup/display_bringup.c`, commit `95ab605`): minimal 5-command DCS init,
`INVON`, GRAM offset (24,0), `COLMOD 0x05`, SPI at PCLK/8. The only change is
`MADCTL`, which rotates the panel into landscape. The portrait offset (24,0)
swaps with the `MV` bit to (0,24); 24 is the centre of a 128-wide GRAM, which
makes it symmetric under the `MX`/`MY` mirror bits, so the value holds either
way up.

## Why proportional text

The module firmware draws the 8×8 font monospaced, which is right for a UI but
too wide here: `Hello, World!` monospaced at 16 px tall is 208 px and the panel
is 160. Trimming each glyph to its actual ink columns brings the same string
down to **144 px**, so it fits at full size with the cursor after it. The
wordmark uses the same path with a one-pixel bold smear, which turns thin
terminal text into something that reads as a logotype.
