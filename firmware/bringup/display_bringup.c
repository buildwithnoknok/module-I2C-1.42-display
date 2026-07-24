/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — HARDWARE BRING-UP firmware  v0.1.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * ── What this is (and is NOT) ─────────────────────────────────────────────
 *   This is a STANDALONE bench firmware, linked at 0x0000 and SWD-flashed
 *   directly (NOT under the noknok I2C bootloader, NOT an I2C module yet).
 *   Its only job is to answer the three unknowns on a brand-new board:
 *
 *     1. Does the backlight chain work?  (MT3540 boost → 33R → 2N7002 on PD2)
 *     2. Does the SPI link to the NV3022B work at all?
 *     3. What are the COLUMN/ROW OFFSETS of the 80x160 visible window inside
 *        the controller's larger internal GRAM?  ← the real unknown
 *
 *   Once the offsets are known, they get baked into the real module firmware
 *   (enumeration + GPU command set + bootloader relink at 0x1000).
 *
 * ── Hardware (verified against the schematic netlist, Jul 2026) ───────────
 *   PC5  SPI1_SCK   → 22R → panel SCL/SCK (pin 4)
 *   PC6  SPI1_MOSI  → 22R → panel SDA/MOSI (pin 3)
 *   PC3  D/C (RS)   → panel RS (pin 5)          low = command, high = data
 *   PC4  RESET      → panel RES (pin 6)         active low
 *   ---  CS tied to GND on the board (panel always selected)
 *   PD2  Backlight PWM (TIM1_CH1) → 100R → 2N7002 gate (low-side switch)
 *   PD1  Status LED (ACTIVE LOW: 3V3 → 2.2k → LED → PD1) + SWIO/flash pad
 *   PC1/PC2  I2C — unused in this bring-up build
 *
 * ── How to use it at the bench ────────────────────────────────────────────
 *   PHASE 1 (~4 s):  panel held in reset, backlight ramps 0 → 100 % → 0.
 *                    You should see the screen glow evenly (dark, no image).
 *                    If nothing glows: boost / 33R / 2N7002 / PD2 problem.
 *                    Status LED = slow heartbeat.
 *
 *   PHASE 2 (loop):  the panel is initialised, then the firmware CYCLES
 *                    through a list of candidate window offsets, ~3 s each.
 *                    Before each candidate the status LED blinks N times
 *                    (N = candidate number, 1..OFFSET_COUNT) so you can
 *                    identify it even if the screen stays black.
 *
 *                    For each candidate it draws, into an 80x160 window:
 *                      · a 2 px WHITE border around the whole screen
 *                      · dark-blue fill
 *                      · four 12x12 corner squares:
 *                          top-left RED     top-right GREEN
 *                          bottom-left YELLOW  bottom-right CYAN
 *                      · N small white tally dots down the middle
 *
 *                    The CORRECT offset is the one where the border is
 *                    complete on ALL FOUR edges and all four corner squares
 *                    are fully visible. Note that candidate number and tell
 *                    Sam — it gets hard-coded and this file's job is done.
 *
 *                    Bonus: the corner colours also confirm MADCTL/rotation
 *                    (red must be top-left as you look at the panel with the
 *                    FPC exiting at the bottom).
 *
 * ── If the screen stays black in phase 2 on every candidate ───────────────
 *   Then it is not an offset problem. In order of likelihood:
 *     · COLMOD value — try 0x55 instead of 0x05 (see COLMOD_VALUE below)
 *     · SPI too fast — drop SPI_BAUD_DIV to SPI_DIV_8 or SPI_DIV_16
 *     · the init table is a generic NV3022B vendor sequence; the panel may
 *       need EastRising's own. Grab the ER-TFT1.42-1 demo code from
 *       buydisplay.com and replace nv3022b_init_seq[] wholesale.
 *     · D/C/RES swapped or not driven — scope PC3/PC4
 */

#include "ch32fun.h"
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * CONFIGURATION — the knobs you will actually turn at the bench
 * ═══════════════════════════════════════════════════════════════════════════ */

#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 1
#define FW_VERSION_PATCH 0

#define PANEL_W          80          /* visible pixels, portrait (native)     */
#define PANEL_H          160

/* SPI clock = PCLK(48 MHz) / divider. The NV3022B tolerates ~15 MHz writes;
 * 12 MHz is a safe, fast default. Drop to _8 (6 MHz) if the picture is noisy
 * or absent — long-ish FPC + 22R series resistors are the limiting factor. */
#define SPI_DIV_2        (0u << 3)
#define SPI_DIV_4        (1u << 3)   /* 12 MHz  ← default                     */
#define SPI_DIV_8        (2u << 3)   /*  6 MHz                                */
#define SPI_DIV_16       (3u << 3)   /*  3 MHz                                */
#define SPI_BAUD_DIV     SPI_DIV_4

/* Pixel format for command 0x3A. MIPI-DCS style controllers want 0x05 for
 * RGB565; some vendor variants want 0x55 (RGB iface + MCU iface both 16-bit).
 * If colours come out as noise/garbage, try the other one. */
#define COLMOD_VALUE     0x05

/* ── Rework switch (rev 1.0 boards only) ─────────────────────────────────
 * Rev 1.0 shipped with a mirrored FPC footprint. A rev-1.0 board can be
 * rescued for bench use by desoldering the panel and re-soldering it rotated
 * 180° in the plane of the board — that reverses the finger order and undoes
 * the mirror. The panel then hangs off the board (no housing, bench only) and
 * its image is upside down, which MADCTL corrects in the controller.
 *
 *   1 = rev 1.0 board with the panel rotated 180° (the rework)
 *   0 = corrected board from batch 2 onwards                                */
#define PANEL_ROTATED_REWORK  1

/* MADCTL (0x36) — memory access control: row/column order + RGB/BGR.
 * 0x00 = portrait, top-left origin, RGB order.
 * 0xC0 = MY|MX = rotated 180°, which cancels the physically rotated panel.
 * If red and blue come out swapped, add bit 3 (0x08 = BGR). */
#if PANEL_ROTATED_REWORK
  #define MADCTL_VALUE   0xC0
#else
  #define MADCTL_VALUE   0x00
#endif

/* Backlight PWM: TIM1 at 48 MHz / (ATRLR+1) = 48 kHz — well above any
 * visible or camera-visible flicker. Duty is 0..BL_MAX. */
#define BL_MAX           999
#define BL_NORMAL        700         /* ~70 % for the test patterns           */

/* ── The candidate offsets we are hunting for ─────────────────────────────
 * The 80x160 visible area is a WINDOW inside the controller's larger GRAM,
 * so every drawing coordinate must be shifted by (x_off, y_off). Nobody
 * publishes this for the ER-TFT1.42-1, so we sweep the plausible values:
 *   (0,0)    no offset at all
 *   (26,1)   classic 132x162 GRAM, window centred   (ST7735-style geometry)
 *   (24,0)   the other common 132-wide variant
 *   (80,62)  240x284 GRAM, window centred
 *   (0,62) / (80,0)  one axis centred, the other not
 *   (0,24) / (24,24) small-offset variants
 * Add or reorder freely — the tally blinks/dots follow the array. */
/* ── ANSWER (bench-confirmed Jul 23 2026) ─────────────────────────────────
 * Candidate 3 aligned perfectly on real hardware: complete border on all four
 * edges, all four corner squares visible. The 80x160 visible window sits at
 * (24, 0) in the NV3022B's GRAM. Set OFFSET_CONFIRMED to 1 to skip the sweep
 * and draw a colour-verification pattern at that offset instead. */
#define OFFSET_CONFIRMED  1
#define CONFIRMED_X_OFF   24
#define CONFIRMED_Y_OFF   0

typedef struct { uint8_t x, y; } offset_t;

static const offset_t candidates[] = {
    {  0,  0 },   /* 1 */
    { 26,  1 },   /* 2 */
    { 24,  0 },   /* 3 */
    { 80, 62 },   /* 4 */
    {  0, 62 },   /* 5 */
    { 80,  0 },   /* 6 */
    {  0, 24 },   /* 7 */
    { 24, 24 },   /* 8 */
};
#define OFFSET_COUNT (sizeof(candidates) / sizeof(candidates[0]))

/* ── RGB565 colours (MSB first on the wire) ──────────────────────────────── */
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define NAVY    0x000C   /* dark blue background — easy on the eyes          */

/* ═══════════════════════════════════════════════════════════════════════════
 * LOW-LEVEL PINS
 * ═══════════════════════════════════════════════════════════════════════════ */

#define DC_LOW()    (GPIOC->BCR  = (1u << 3))    /* PC3 = 0 → command        */
#define DC_HIGH()   (GPIOC->BSHR = (1u << 3))    /* PC3 = 1 → data           */
#define RES_LOW()   (GPIOC->BCR  = (1u << 4))    /* PC4 = 0 → panel in reset */
#define RES_HIGH()  (GPIOC->BSHR = (1u << 4))
#define LED_ON()    (GPIOD->BCR  = (1u << 1))    /* PD1 active LOW           */
#define LED_OFF()   (GPIOD->BSHR = (1u << 1))

/* ═══════════════════════════════════════════════════════════════════════════
 * SPI — hardware peripheral, transmit-only (no bit-banging, house rule)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void spi_init(void)
{
    /* CTLR1: software NSS (SSM+SSI, because CS is hard-wired to GND on the
     * board), master mode, 8-bit frames, MSB first, SPI mode 0 (CPOL=0,
     * CPHA=0) — what the NV3022B's 4-wire serial interface expects. */
    SPI1->CTLR1 = (1u << 9)      /* SSM  — software slave management         */
                | (1u << 8)      /* SSI  — internal NSS high = stay master   */
                | SPI_BAUD_DIV
                | (1u << 2);     /* MSTR — master                            */
    SPI1->CTLR2 = 0;
    SPI1->CTLR1 |= (1u << 6);    /* SPE  — enable                            */
}

/* Push one byte and wait for the shift register to drain. We block on BSY
 * because D/C must never change while a byte is still on the wire. */
static inline void spi_tx(uint8_t b)
{
    while (!(SPI1->STATR & SPI_STATR_TXE)) { }
    SPI1->DATAR = b;
}

static inline void spi_wait_idle(void)
{
    while (SPI1->STATR & SPI_STATR_BSY) { }
}

static void lcd_cmd(uint8_t c)
{
    spi_wait_idle();
    DC_LOW();
    spi_tx(c);
    spi_wait_idle();
    DC_HIGH();
}

static void lcd_data(uint8_t d)
{
    spi_tx(d);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NV3022B INIT
 *
 * Generic NV3022B vendor power/gamma sequence (as published in Espressif's
 * esp_lcd_nv3022b component). It is NOT panel-specific to the ER-TFT1.42-1,
 * so gamma may be slightly off — but it is a known-good starting point that
 * gets a picture on the glass. Replace wholesale if EastRising's own demo
 * code becomes available.
 *
 * Table format:  cmd, nargs (bit7 set = a 1-byte delay follows the args),
 *                args..., [delay_ms]
 * ═══════════════════════════════════════════════════════════════════════════ */

#define DLY 0x80

static const uint8_t nv3022b_init_seq[] = {
    0xFD, 2, 0x06, 0x08,
    0x61, 2, 0x07, 0x07,
    0x73, 1, 0x70,
    0x73, 1, 0x00,                       /* yes, 0x73 really is sent twice   */
    0x62, 3, 0x00, 0x44, 0x40,
    0x65, 3, 0x08, 0x10, 0x21,
    0x66, 3, 0x08, 0x10, 0x21,
    0x67, 2, 0x21, 0x40,
    0x68, 4, 0x9F, 0x30, 0x27, 0x21,
    0xB1, 3, 0x0F, 0x02, 0x01,
    0xB4, 1, 0x01,
    0xB5, 4, 0x02, 0x02, 0x0A, 0x14,
    0xB6, 5, 0x44, 0x01, 0x9F, 0x00, 0x02,
    0xE6, 2, 0x00, 0xFF,
    0xE7, 6, 0x01, 0x04, 0x03, 0x03, 0x00, 0x12,
    0xE8, 3, 0x00, 0x70, 0x00,
    0xEC, 1, 0x52,
    0xDF, 1, 0x11,
    0xE0, 8, 0x06, 0x05, 0x0B, 0x12, 0x10, 0x10, 0x10, 0x15,   /* gamma+     */
    0xE3, 8, 0x15, 0x10, 0x11, 0x0E, 0x12, 0x0D, 0x06, 0x06,   /* gamma-     */
    0xE1, 2, 0x35, 0x75,
    0xE4, 2, 0x74, 0x35,
    0xE2, 6, 0x22, 0x22, 0x21, 0x35, 0x36, 0x3F,
    0xE5, 6, 0x3F, 0x35, 0x34, 0x21, 0x22, 0x22,
    0xF1, 3, 0x01, 0x01, 0x02,
    0xF6, 4, 0x09, 0x30, 0x00, 0x00,
    0xFD, 2, 0xFA, 0xFC,
    0x3A, 1, COLMOD_VALUE,               /* pixel format = RGB565            */
    0x36, 1, MADCTL_VALUE,               /* memory access control            */
    0x11, DLY | 0, 200,                  /* SLPOUT  + 200 ms                 */
    0x29, DLY | 0, 20,                   /* DISPON  + 20 ms                  */
    0x00                                 /* terminator                        */
};

/* ── Init selection ───────────────────────────────────────────────────────
 * BENCH RESULT (Jul 23 2026): the generic NV3022B vendor table below produced
 * a completely dead screen on every board, while a MINIMAL five-command init
 * produced a picture immediately. Those ~30 undocumented vendor registers are
 * for a different panel and evidently leave this controller unusable.
 * So the minimal init is the default until EastRising supply the real one.
 *   1 = minimal (SWRESET/SLPOUT/COLMOD/MADCTL/DISPON)  ← proven to work
 *   0 = the generic vendor table                        ← proven NOT to work */
#define USE_MINIMAL_INIT  1

static void lcd_init_minimal(void)
{
    lcd_cmd(0x01);                     /* SWRESET */
    Delay_Ms(150);
    lcd_cmd(0x11);                     /* SLPOUT  */
    Delay_Ms(200);
    lcd_cmd(0x3A); lcd_data(COLMOD_VALUE);
    lcd_cmd(0x36); lcd_data(MADCTL_VALUE);
    spi_wait_idle();
    /* INVON — display inversion ON. BENCH RESULT: without this the panel
     * renders every colour inverted (navy background came out bright yellow,
     * white dots came out black). Normal for an IPS cell of this type; it is
     * NOT a wiring or byte-order problem. */
    lcd_cmd(0x21);
    lcd_cmd(0x29);                     /* DISPON  */
    Delay_Ms(20);
}

static void lcd_run_init_table(const uint8_t *p)
{
    while (*p) {
        uint8_t cmd  = *p++;
        uint8_t n    = *p++;
        uint8_t wait = n & DLY;
        n &= 0x7F;

        lcd_cmd(cmd);
        while (n--) lcd_data(*p++);
        spi_wait_idle();

        if (wait) Delay_Ms(*p++);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DRAWING — no framebuffer; everything goes straight into the panel's GRAM
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint8_t x_off = 0, y_off = 0;     /* current candidate offset          */

/* Open a rectangle in GRAM and leave the controller in "expect pixels" mode. */
static void lcd_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x0 = x + x_off, x1 = x0 + w - 1;
    uint16_t y0 = y + y_off, y1 = y0 + h - 1;

    lcd_cmd(0x2A);                        /* CASET — column address           */
    lcd_data(x0 >> 8); lcd_data(x0 & 0xFF);
    lcd_data(x1 >> 8); lcd_data(x1 & 0xFF);

    lcd_cmd(0x2B);                        /* RASET — row address              */
    lcd_data(y0 >> 8); lcd_data(y0 & 0xFF);
    lcd_data(y1 >> 8); lcd_data(y1 & 0xFF);

    lcd_cmd(0x2C);                        /* RAMWR — pixels follow            */
}

static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    uint32_t n  = (uint32_t)w * h;
    uint8_t  hi = c >> 8, lo = c & 0xFF;

    lcd_window(x, y, w, h);
    while (n--) { spi_tx(hi); spi_tx(lo); }
    spi_wait_idle();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BACKLIGHT — TIM1_CH1 on PD2, hardware PWM
 * ═══════════════════════════════════════════════════════════════════════════ */

static void backlight_init(void)
{
    TIM1->PSC   = 0;                      /* 48 MHz counter clock             */
    TIM1->ATRLR = BL_MAX;                 /* → 48 kHz PWM                     */
    TIM1->CH1CVR = 0;                     /* start dark                       */

    TIM1->CHCTLR1 = (6u << 4)             /* OC1M = PWM mode 1                */
                  | (1u << 3);            /* OC1PE — preload enable           */
    TIM1->CCER  = (1u << 0);              /* CC1E — output enable, active hi  */
    TIM1->BDTR |= (1u << 15);             /* MOE — main output enable.
                                           * Advanced timer: without this the
                                           * pin stays dead. Classic CH32V003
                                           * gotcha, same as on the buzzer.   */
    TIM1->SWEVGR = (1u << 0);             /* UG — load the preloaded regs     */
    TIM1->CTLR1 |= (1u << 0);             /* CEN — run                        */
}

static inline void backlight_set(uint16_t duty)
{
    TIM1->CH1CVR = (duty > BL_MAX) ? BL_MAX : duty;
}

/* Ramp instead of snapping to full. The MT3540 boost hangs off the SHARED
 * 3.3 V bus rail (ahead of the module's 10R), so a cold inrush here is felt
 * by every other module on the daisy chain. ~200 ms of ramp costs nothing
 * and keeps the bus clean. */
static void backlight_ramp(uint16_t from, uint16_t to, uint16_t ms)
{
    int32_t steps = 50, i;
    int32_t delta = (int32_t)to - (int32_t)from;
    for (i = 0; i <= steps; i++) {
        backlight_set((uint16_t)(from + delta * i / steps));
        Delay_Ms(ms / steps);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * GPIO
 * ═══════════════════════════════════════════════════════════════════════════ */

static void gpio_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD
                    | RCC_APB2Periph_SPI1  | RCC_APB2Periph_TIM1
                    | RCC_APB2Periph_AFIO;

    /* GPIOC — 4 config bits per pin.
     *   0x1 = output push-pull, 10 MHz
     *   0xB = alternate-function push-pull, 50 MHz  (SPI pins) */
    GPIOC->CFGLR &= ~(0xFu << (4 * 3) | 0xFu << (4 * 4)
                    | 0xFu << (4 * 5) | 0xFu << (4 * 6));
    GPIOC->CFGLR |=  (0x1u << (4 * 3))    /* PC3 D/C   output PP              */
                  |  (0x1u << (4 * 4))    /* PC4 RES   output PP              */
                  |  (0xBu << (4 * 5))    /* PC5 SCK   AF PP                  */
                  |  (0xBu << (4 * 6));   /* PC6 MOSI  AF PP                  */

    /* GPIOD — PD1 status LED (output PP), PD2 backlight (AF PP for TIM1_CH1,
     * default mapping, no remap needed). */
    GPIOD->CFGLR &= ~(0xFu << (4 * 1) | 0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0x1u << (4 * 1))
                  |  (0xBu << (4 * 2));

    LED_OFF();
    RES_LOW();                            /* hold the panel in reset for now  */
    DC_HIGH();
}

static void blink(uint8_t n)
{
    while (n--) {
        LED_ON();  Delay_Ms(120);
        LED_OFF(); Delay_Ms(180);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TEST PATTERN
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_test_pattern(uint8_t tally)
{
    uint8_t i;

    /* Background + a 2 px white border. If any edge of the border is missing
     * or doubled, the offset for that axis is wrong. */
    fill_rect(0, 0, PANEL_W, PANEL_H, NAVY);
    fill_rect(0,            0,            PANEL_W, 2, WHITE);
    fill_rect(0,            PANEL_H - 2,  PANEL_W, 2, WHITE);
    fill_rect(0,            0,            2, PANEL_H, WHITE);
    fill_rect(PANEL_W - 2,  0,            2, PANEL_H, WHITE);

    /* Corner squares — also prove orientation (red must be top-left). */
    fill_rect(4,           4,            12, 12, RED);
    fill_rect(PANEL_W - 16, 4,           12, 12, GREEN);
    fill_rect(4,           PANEL_H - 16, 12, 12, YELLOW);
    fill_rect(PANEL_W - 16, PANEL_H - 16, 12, 12, CYAN);

    /* Tally dots: which candidate offset you are looking at. */
    for (i = 0; i < tally; i++)
        fill_rect(PANEL_W / 2 - 3, 40 + i * 10, 6, 6, WHITE);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    uint8_t idx = 0;

    SystemInit();
    gpio_init();
    spi_init();
    backlight_init();

    /* ── PHASE 1: backlight only, panel still held in reset ──────────────── */
    blink(1);
    backlight_ramp(0, BL_MAX, 1500);      /* screen should glow evenly        */
    Delay_Ms(800);
    backlight_ramp(BL_MAX, 0, 800);
    Delay_Ms(400);

    /* ── Panel reset + init ──────────────────────────────────────────────── */
    RES_LOW();
    Delay_Ms(20);
    RES_HIGH();
    Delay_Ms(150);                        /* NV3022B needs >120 ms after RES  */

#if USE_MINIMAL_INIT
    lcd_init_minimal();
#else
    lcd_run_init_table(nv3022b_init_seq);
#endif

    backlight_ramp(0, BL_NORMAL, 200);

#if OFFSET_CONFIRMED
    /* ── Offset is known — draw a colour-verification pattern instead ─────
     * BENCH RESULT Jul 23 2026: candidate 3 aligned perfectly on hardware,
     * so the 80x160 window sits at (24, 0) in the controller's GRAM. */
    x_off = CONFIRMED_X_OFF;
    y_off = CONFIRMED_Y_OFF;

    fill_rect(0, 0, PANEL_W, PANEL_H, NAVY);

    /* 2 px white border — should be WHITE, and complete on all four edges */
    fill_rect(0,           0,           PANEL_W, 2, WHITE);
    fill_rect(0,           PANEL_H - 2, PANEL_W, 2, WHITE);
    fill_rect(0,           0,           2, PANEL_H, WHITE);
    fill_rect(PANEL_W - 2, 0,           2, PANEL_H, WHITE);

    /* Corner squares: red TL, green TR, yellow BL, cyan BR */
    fill_rect(4,            4,            12, 12, RED);
    fill_rect(PANEL_W - 16, 4,            12, 12, GREEN);
    fill_rect(4,            PANEL_H - 16, 12, 12, YELLOW);
    fill_rect(PANEL_W - 16, PANEL_H - 16, 12, 12, CYAN);

    /* Colour bars down the middle — the definitive colour check. Top to
     * bottom: red, green, blue, white, black, yellow, cyan, magenta. */
    {
        static const uint16_t bars[8] = {
            RED, GREEN, BLUE, WHITE, BLACK, YELLOW, CYAN, 0xF81F /* magenta */
        };
        for (idx = 0; idx < 8; idx++)
            fill_rect(20, 30 + idx * 12, PANEL_W - 40, 10, bars[idx]);
    }

    for (;;) { Delay_Ms(1000); }
#else
    /* ── PHASE 2: sweep the candidate offsets forever ────────────────────── */
    for (;;) {
        idx = (idx + 1);
        if (idx > OFFSET_COUNT) idx = 1;

        x_off = candidates[idx - 1].x;
        y_off = candidates[idx - 1].y;

        blink(idx);                       /* count these to know where you are */
        draw_test_pattern(idx);
        Delay_Ms(3000);
    }
#endif
}
