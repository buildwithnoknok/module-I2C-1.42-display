# noknok 1.42″ Display Module (I2C)

> **Status:** 🚧 In development — **rev 1.0 boards are non-functional, respin required.**

> ### ⚠️ Rev 1.0 defect — mirrored FPC footprint
>
> The first assembled boards (July 2026) have the panel mounted **mirrored**: the
> `noknok_ER-TFT1.42-1_13pin_solder_proto` footprint numbered its 13 pads backwards, so board
> pad *n* contacts panel pin *14−n*. The panel's **RES** lands on GND (permanent reset), its
> **VCC** lands on the SCK line (never powered), and **LEDA** lands on a touch-NC pin — so the
> backlight rail sits at a healthy 4.6 V driving 0.18 mA into nothing, and the panel is dead.
>
> The footprint is **fixed** in the Ecosystem library (commit `cb83a33`) — pad 1 is now on the
> `+X` end. The KiCad layout needs re-importing and the board re-fabbing.
>
> Everything else on rev 1.0 is **bench-proven good** and does not need re-debugging: the
> CH32V003 runs on bus power, flash verifies byte-for-byte, GPIO outputs work, the MT3540 boost
> delivers 4.6 V, the 2N7002 backlight switch works, and the reset RC releases correctly.
>
> **Before ordering the respin:** physically compare the pin-1 label printed on the panel's own
> FPC against the board's `Pin1` silk marker. A netlist check cannot catch this class of error —
> the schematic was correct all along.

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
| PC3 | D/C | RS (pin 5) |
| PC4 | Reset | RES (pin 6) |
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

Source in `firmware/src/`, distributable OTA image in `firmware/bin/`. Linked at `0x1000` to
run under the shared noknok I2C bootloader; flashed over I2C, not SWD.

### Changelog

Every release from **v0.4.0 onwards must add an entry here before the binary is committed.**
Entries below v0.4.0 were written retrospectively.

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

- **Text renders mirrored** on the rescued rev 1.0 boards — under investigation; `MX` appears
  not to be honoured by this controller, so the physical 180° panel rotation is only half
  cancelled. Use `SET_ORIENT` to find the right MADCTL.
- Rotation offsets for anything other than rotation 0 are guesses.
- Icons and a second (larger) font are not implemented.
- The status LED is not driven — PD1/SWIO does not work (Jira DEV-20).

## Status / next steps

- [ ] Confirm schematic nets (boost EN→3.3V-IN, backlight gate→PD2, status-LED cathode→PD1)
- [ ] PCB layout — FPC keep-out strip (no components under the flex), glass keep-out (top),
      2× M2.5 mounting holes; use the `_proto` footprint for the first run
- [ ] BOM with verified LCSC parts
- [ ] Firmware
- [ ] Prototype + bring-up (verify pin-1/fold/offset, dial in the production pad position)

## Hardware

KiCad schematic in `hardware/` (to be added). Panel: [ER-TFT1.42-1 datasheet](https://www.buydisplay.com)
· Controller: NV3022B.

## License

Follows the noknok open standard once public — firmware **MIT**, hardware **CC BY-SA 4.0** — with a
link to the central safety & liability page. (Repo is private during development; Christopher sets
visibility + adds LICENSE files at the public rollout.)
