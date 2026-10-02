# noknok 1.42″ Display Module (I2C)

> **Status:** rev 1.1 boards are **working and in production** (20 pcs, Oct 2026). Firmware
> **v0.7.0** is bench-verified on a production unit: enumeration, drawing, both text paths,
> all four orientations, and brand colours rendering correctly.

> ### Rev 1.1 changed the panel control pins — firmware must match
>
> **rev 1.1 swaps D/C and RES** relative to the reworked rev 1.0 boards:
> `PC3 = RES` and `PC4 = D/C` (rev 1.0 had them the other way round). SPI is unaffected.
> Selected by `BOARD_REV_11` in each firmware.
>
> The symptom of getting it wrong is **actively misleading**: lit backlight, visibly running
> MCU, and a completely blank screen — because every command/data decision toggles the panel's
> RESET line while the reset pulse goes to D/C. It looks exactly like a dead panel or a bad
> init table.

<details>
<summary><strong>History: the rev 1.0 mirrored-FPC defect (resolved)</strong></summary>

The first assembled boards (July 2026) had the panel mounted **mirrored**: the
`noknok_ER-TFT1.42-1_13pin_solder_proto` footprint numbered its 13 pads backwards, so board
pad *n* contacted panel pin *14-n*. The panel's **RES** landed on GND (permanent reset), its
**VCC** on the SCK line (never powered), and **LEDA** on a touch-NC pin.

Fixed in the Ecosystem library (commit `cb83a33`) — pad 1 is now on the `+X` end — and
re-fabbed as rev 1.1. Bench boards were rescued by re-soldering the panel rotated 180° in the
plane of the board, which is why rev 1.0 firmware carried a `PANEL_ROTATED_REWORK` switch.

**The lesson worth keeping:** physically compare the pin-1 label printed on the panel's own FPC
against the board's `Pin1` silk marker before ordering. A netlist check cannot catch this
class of error — the schematic was correct all along.

</details>

A modular **1.42-inch colour TFT** display for the noknok ecosystem. A CH32V003 acts as a
local "GPU" — it renders text, icons and graphics to the panel and speaks the noknok I2C
protocol, so the display plugs into the standard JST SH bus like any other module.

## Overview

| | |
|---|---|
| **Panel** | EastRising **ER-TFT1.42-1** — 1.42″ IPS, **80×160**, colour, **NV3022B** controller |
| **MCU** | **CH32V003F4U6** (QFN-20) — local GPU, I2C slave |
| **Bus** | noknok I2C (JST SH 4-pin), 3.3 V |
| **Panel link** | 4-wire SPI (CH32 → NV3022B); interface is fixed, **no IM strapping** |
| **Backlight** | 3 parallel LEDs (~3 V, 45–60 mA) → **MT3540 boost to 4.5 V** + 33 Ω ballast + PWM dimming |
| **FPC** | 13-pin, **0.70 mm pitch, soldered** to the board (PCBWay-assembled) |
| **Board** | **40 × 30 mm** (noknok 4×3 cm standard); 4× M2.5 mounting holes 2.25 mm from each corner; 2× JST-SH on the W + S edges; panel bonded on front, FPC wraps to bottom-side pads |

## Architecture

- **CH32V003 = GPU.** The Pico (Conductor) sends high-level commands ("draw text", "draw
  icon", "blit bitmap"); the CH32 renders to the NV3022B's GRAM over SPI — **framebuffer-less,
  no external RAM**.
- **Assets.** A small default set (one font + common icons, 1bpp, tinted at draw time) lives in
  the CH32's ~12 KB app flash. Anything beyond that is **rendered on the Pico and blitted** as a
  1bpp bitmap (host-supplied assets), so the module is **not limited by its flash** — makers add
  fonts on the Pico side.
- **Standard noknok module behaviour:** dynamic-address enumeration, `GET_VERSION` (0xB1),
  `ENTER_BOOTLOADER` (0xB0), I2C-OTA via the shared CH32V003 bootloader.

## CH32V003F4U6 pinout

| CH32 pin | Function | → Panel |
|---|---|---|
| PC1 / PC2 | I2C SDA / SCL | (noknok bus) |
| PC5 | SPI SCK (via 22 Ω) | SCL/SCK (pin 4) |
| PC6 | SPI MOSI (via 22 Ω) | SDA/MOSI (pin 3) |
| PC3 | **rev 1.1: Reset** / rev 1.0: D/C | **RES (pin 6)** / RS (pin 5) |
| PC4 | **rev 1.1: D/C** / rev 1.0: Reset | **RS (pin 5)** / RES (pin 6) |
| (GND) | CS tied low | CS (pin 7) |
| PD2 | Backlight PWM (TIM1_CH1) | (via 2N7002 + 33 Ω) |
| PD1 | SWIO / flash / status LED | — |
| PD5 / PD6 | UART debug (USART1 TX/RX) | — |
| PD7 | NRST (RC reset) | — |

Panel power: **VCC (pin 10) → 3.3 V** (internal panel power; only VCC + GND needed). Touch
pins (1, 2) and pin 9 = NC.

## Hardware blocks

- **Power/protection:** 3.3V-IN → 10 Ω series + **3.6 V Zener** (clamps below the panel's 3.6 V
  abs-max) → 3.3V-PROT; bulk 10 µF/0.1 µF + MCU & panel decoupling. No on-module I2C pull-ups
  (host-side only).
- **Backlight boost:** MT3540-F23 → **4.5 V** (R1 33 k / R2 12 k), 33 Ω ballast, 2N7002 low-side
  PWM MOSFET on PD2 (100 Ω gate + 100 k pulldown). Boost taps 3.3V-IN (before the series R).
- **Panel:** single VCC, 4-wire SPI. **80×160 is a window in the NV3022B's 132×162 RAM → the
  init sequence needs a column/row offset** (tune at bring-up, like a small ST7735).
- **noknok front-end:** 2× JST SH, 3-pad pogo flash pads (PD1/SWIO), UART debug pads, status
  LED, reset RC. No castellated edges.

## KiCad library

Symbols & footprints live in the **Ecosystem** repo (`electrical/noknok.kicad_sym` + `noknok.pretty/`):

- `noknok_ER-TFT1.42-1` (symbol) + `noknok_ER-TFT1.42-1_13pin_solder` (footprint)
- `noknok_ER-TFT1.42-1_13pin_solder_proto` — prototype variant, **stretched 3.5 mm pads**
  (±0.9 mm FPC-fold slack) + silk alignment rulers; switch back to the production footprint once
  the fold geometry is confirmed.
- `noknok_UART_debug_1x3_pogo`, `noknok_FlashPads_I2C-module_1x3_M2.5`, `noknok_MountingHole`

## Firmware

Source in `firmware/src/`, distributable OTA image in `firmware/bin/`. Linked at `0x1400` (bootloader layout 2) to
run under the shared noknok I2C bootloader; flashed over I2C, not SWD.

### Changelog

Every release from **v0.4.0 onwards must add an entry here before the binary is committed.**
Entries below v0.4.0 were written retrospectively.

#### v0.7.0 — 1 Oct 2026
- **Runs EastRising's real init sequence, including the gamma table** (`VENDOR_INIT`), ported
  from the ER-TFT1.42-1 8051 tutorial on buydisplay.com. **This fixes brand colours**:
  `#8B00F3` renders as actual purple. Previously, with no gamma table, the controller ran its
  power-on curve, which mis-maps mid-level codes — purple (R 54.8% / B 96.8%) came out blue
  because the red was flattened while the near-rail blue passed through.
- **Why the July vendor-table attempt killed four panels, finally understood.** Vendor
  registers sit behind a locked page: EastRising unlocks with `0xFF,0xA5`, but the generic
  Espressif `esp_lcd_nv3022b` table we tried used `0xFD,0x06,0x08`. Wrong key, so every
  vendor write hit a locked page — and the gamma block is written through `0x2A/0x2B/0x2C`,
  which *outside* the unlocked page are CASET/RASET/**RAMWR**. Doing that blind sets a
  nonsense address window and dumps 64 bytes into GRAM.
- MADCTL is now **init-dependent**: the vendor sets scan direction with the vendor register
  `0xC3 = 0x11` (ss/gs). Both tables are bench-measured, not derived:

  | rot | vendor init | minimal init |
  |---|---|---|
  | 0 portrait | `0xC0` | `0x40` |
  | 1 landscape (default) | `0xA0` | `0x20` |
  | 2 portrait flipped | `0x00` | `0x80` |
  | 3 landscape flipped | `0x60` | `0xE0` |

- COLMOD is the vendor's `0x55` (was `0x05`). The minimal init survives under `#else` —
  one define reverts everything.
- Vendor code independently confirms two values found empirically in July: `LCD_SetPos()`
  uses `x0 + 24` (our GRAM offset) and the init contains `INVON 0x21`.

#### v0.6.0 — 1 Oct 2026
- **rev 1.1 pin mapping** (`BOARD_REV_11`): `PC3 = RES`, `PC4 = D/C`. Neither this
  firmware nor the bring-up could drive a rev 1.1 board before this.
- **Default orientation is now landscape** (160×80). Named `ROT_*` constants plus
  `DEFAULT_ROTATION`, and a single `gpu_set_rotation()` helper serving both the boot
  default and `SET_ROTATION` so the two cannot drift apart.
- **Fixed: `gpu_window()` ignored the runtime GRAM offsets**, using the compile-time
  constants instead. `g_xoff`/`g_yoff` were written by `SET_ROTATION` and
  `SET_ORIENT` and never read, so the rotation API changed MADCTL and geometry but
  silently dropped the offsets.
- **Fixed: portrait MADCTL was `0x00`, which renders text mirrored.** A symmetric border
  cannot reveal that — only text can, which is why it survived the original bring-up.
- **All four rotations bench-verified**, retiring the `offsets for anything other than
  rotation 0 are guesses` caveat. The effective RAM is **128** wide (offset 24 is
  symmetric); the blurb's 132×162 is the NV3022B maximum, not this panel's mapping.
- `draw_splash()` is centred on the live geometry; it previously drew at y=60..98, off the
  bottom of an 80 px-tall landscape panel.

#### v0.5.0 — 11 Sep 2026
- Relinked at the `0x1400` flash offset (bootloader **layout 2**: 1 KB stage-0 + 4 KB stage-1 below the app; was `0x1000`). Firmware behaviour unchanged. **Not compatible with the legacy monolithic bootloader or a layout-1 stage-1** — such a module gets stage-0 + stage-1 v1.1.0 over SWD first, then this app over I²C. See [module-I2C-bootloader](https://github.com/buildwithnoknok/module-I2C-bootloader).

#### v0.4.0 — 11 Sep 2026
- Runs the independent watchdog (~2 s, kicked every main-loop pass) and writes `0` to `0x200007F8` the moment its I²C address is assigned — the app-health handshake with the stage-1 bootloader. Stage-1 counts watchdog resets and parks the module (error 7, rescued by the Conductor by UID) after three in a row, instead of booting a broken app forever. Contract: [Ecosystem / software / bootloader-update.md §3](https://github.com/buildwithnoknok/Ecosystem/blob/main/software/bootloader-update.md). No protocol or command change. The watchdog is also kicked inside the backlight ramp (the one blocking loop).

#### v0.3.0 — 23 Jul 2026
- `SET_ROTATION` (0x11) and `SET_ORIENT` (0x14). Panel geometry and GRAM offsets became
  **runtime state** rather than compile-time constants, so clipping, `GET_INFO` and the blit
  bounds all follow the current orientation.
- `SET_ORIENT` is a bench calibrator taking a raw MADCTL plus offsets: the MADCTL values are
  certain but the offsets for any rotation other than 0 are unknown, because the controller's
  true GRAM width is undocumented.
- Version bumped purely so the orientation build is distinguishable from v0.2.0 over
  `GET_VERSION` — v0.2.0 shipped twice with different command sets, which was a mistake.

#### v0.2.0 — 23 Jul 2026
- Built-in **8×8 monospace font**, ASCII 0x20–0x7E (760 bytes), generated from the
  public-domain `dhepper/font8x8`. Bit order is LSB-first (bit 0 = leftmost).
- `DRAW_TEXT` (0x03) with integer scale 1–8 → native heights 8/16/24/32/40/48/56/64.
- `BLIT_BEGIN` (0x05) / `BLIT_DATA` (0x06): 1bpp host-rendered assets streamed in ≤64-byte
  chunks and expanded to RGB565 on arrival — the CH32V003 cannot buffer a bitmap in 2 KB.
- Style-byte layout aligned with `noknok.py`; transparent blit fixed (it previously streamed
  pixels without opening a window).
- `GET_INFO` corrected to report 1 built-in font.

#### v0.1.0 — 23 Jul 2026
- First real module firmware. Enumeration as **MODULE_TYPE 0x05**, `GET_VERSION` (0xB1),
  `ENTER_BOOTLOADER` (0xB0), `GET_INFO` (0x15).
- GPU: hardware SPI, minimal DCS init, `INVON`, GRAM offset (24, 0), clipped `CLEAR` and
  `FILL_RECT`, backlight PWM with a soft-start ramp, boot splash.
- Command processor snapshots `rx_buf` before drawing — a full-screen fill is ~35 ms of SPI and
  the I2C ISR can land the next command mid-draw.

### Known gaps

- **I2C slave robustness (Jira DEV-44, found 19 Sep 2026):** `last_err` is sticky (never
  cleared — power-cycle to reset); a STOP without a preceding address match re-executes the
  stale `rx_buf` (seen as error 4 after a host bus deinit); an address match can wipe a
  command the main loop has not snapshotted yet. Fix = snapshot into a command buffer in the
  ISR at STOPF, reset `rx_len`, read-to-clear `last_err`.
- The status LED is not driven — PD1/SWIO does not work (Jira DEV-20).
- Icons, images and screen layouts are designed in the browser with the
  [noknok Display Planner](https://buildwithnoknok.github.io/display-planner/) — it previews the
  panel pixel-for-pixel (including this firmware's 8×8 font and the landscape boot orientation)
  and exports ready-to-paste `noknok.py` code
  ([source](https://github.com/buildwithnoknok/Ecosystem/tree/main/software/display-planner),
  [how-to](https://buildwithnoknok.github.io/display-planner-guide/)).
- Icons and a second (larger) font are not implemented in firmware, and no longer need to be:
  since `noknok.py` v1.9 (DEV-41) icons, images, `print()` and named regions are rendered
  on the Pico and streamed over the existing 1bpp `BLIT` path. `DRAW_ICON` (0x04) is
  reserved/unused.
- The **bring-up** firmware (`firmware/bringup/`) still runs the minimal init, so brand
  colours render wrong there. That is fine for its job (liveness, offsets, wiring) but do not
  judge colour with it — use the module firmware.
- `panel_flood.c` keeps the **generic Espressif init table purely as a documented dead end**.
  It is missing the `0xFF,0xA5` unlock, so it cannot work on this panel. Do not revive it.

Resolved in v0.6.0 / v0.7.0: mirrored text, unverified rotation offsets, the rev 1.1 pin
mapping, and brand colours rendering blue.

## Status / next steps

- [x] Schematic, PCB layout, BOM, fabrication — rev 1.1 in production (20 pcs)
- [x] Bring-up: pin-1/fold/offset verified; GRAM offset (24,0), confirmed against the vendor's
      own `LCD_SetPos()`
- [x] Firmware v0.7.0 — bench-verified end to end on a production unit, vendor gamma, all four
      orientations, landscape default
- [ ] **DEV-44** I2C slave robustness hardening (the one real open firmware item)
- [ ] Re-check `BOARD_REV_11` and the MADCTL table on the next panel lot — both are
      hardware-specific and neither is guaranteed across a lot change
- [ ] Consider `-Wundef` in the ch32fun builds: a `#define` placed after the `#if` that
      tests it evaluates to 0 silently, which cost real bench time in this repo twice

## Hardware

KiCad schematic in `hardware/` (to be added). Panel: [ER-TFT1.42-1 datasheet](https://www.buydisplay.com)
· Controller: NV3022B.

## License

Follows the noknok open standard once public — firmware **MIT**, hardware **CC BY-SA 4.0** — with a
link to the central safety & liability page. (Repo is private during development; Christopher sets
visibility + adds LICENSE files at the public rollout.)
