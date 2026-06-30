# noknok 1.42″ Display Module (I2C)

> **Status:** 🚧 In development — schematic complete, pre-prototype. Not yet built or flashed.

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
| **Board** | small (~24 × 42 mm); panel bonded on front, FPC wraps to bottom-side pads |

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

## Firmware (TODO)

- NV3022B init sequence + the **80×160 column/row offset**
- GPU command set: clear / draw text / draw icon / **blit (host bitmap)** / contrast / backlight PWM
- 1bpp asset rendering + colour tint at draw time; host-blit protocol for the long tail of assets
- Enumeration (**MODULE_TYPE = 0x05**), GET_VERSION, bootloader-relink @ 0x1000 + 0xB0 handler

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
