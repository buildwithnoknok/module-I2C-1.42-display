/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — REVEAL DEMO  v1.0.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * A self-running "boot to life" loop for the module's first public reveal
 * video. NOT product code: no I2C, no enumeration, no bootloader. It is a
 * standalone image linked at 0x0000 and SWD-flashed straight onto a bench
 * board — power it and it just runs, forever, on a seamless loop.
 *
 * ── The loop (≈13.5 s) ──────────────────────────────────────────────────────
 *   1. Black, backlight off                                        0.25 s
 *   2. noknok wordmark (white + purple), backlight fades up         0.98 s
 *   3. Fade down, clear, fade back up                               0.42 s
 *   4. One continuous terminal session, scrolling               ~11.5 s
 *   5. Backlight fades to black -> back to step 1                   0.65 s
 *
 * The session is the whole show. "Hello, World!" is simply its first line;
 * every line after it arrives as if Enter had been pressed, the cursor blinks
 * where it lands, and the window scrolls once the panel is full:
 *
 *     Hello, World!        white
 *     wake up,             green
 *     maker...
 *     the modules
 *     have you.
 *     do not follow
 *     the white
 *     rabbit.
 *     > follow             brand purple
 *     noknok_
 *
 * Two typing speeds on purpose: "Hello, World!" is slow and ceremonial at
 * 70 ms/char, the feed that follows is faster and more urgent at 45 ms/char.
 *
 * Step 1 leaves the panel black with the backlight already at zero, which is
 * exactly the state step 5 hands over — so the loop point is invisible and the
 * footage cuts as a seamless reel.
 *
 * ── Panel configuration ─────────────────────────────────────────────────────
 * Every panel number here is lifted verbatim from the bench-proven bring-up
 * (firmware/bringup/display_bringup.c, commit 95ab605) — the minimal 5-command
 * DCS init, INVON, GRAM offset (24,0), COLMOD 0x05, SPI at PCLK/8. The only
 * thing that changes is MADCTL: this build runs the panel LANDSCAPE (160x80)
 * so a line of text fits legibly across the screen.
 *
 *   PC3  D/C (RS)        PC5  SPI1_SCK  -> 22R -> panel SCL
 *   PC4  panel RESET     PC6  SPI1_MOSI -> 22R -> panel SDA
 *   PD2  Backlight PWM (TIM1_CH1 — needs the MOE bit)
 *   CS is tied to GND on the board.
 *
 * ── If it comes out wrong on the bench ──────────────────────────────────────
 *   Text upside down     -> flip MADCTL_LANDSCAPE between 0xA0 and 0x60.
 *   Image shifted a few  -> nudge LAND_Y_OFF (24 assumes a 128-wide GRAM with
 *   px up or down           the 80 px window centred; a 132-wide GRAM wants 26).
 *   Colours inverted     -> drop the INVON line in gpu_init().
 *   Colours garbage      -> COLMOD_VALUE 0x55 instead of 0x05.
 * Each is a one-line change; nothing else depends on them.
 */

#include "ch32fun.h"
#include <stdint.h>
#include "font8x8.h"
#include "wordmark.h"

/* ══════════════════════════════════════════════════════════════════════════
 * PANEL
 * ══════════════════════════════════════════════════════════════════════════ */

/* Landscape.
 *
 * BENCH RESULT (28 Aug 2026): the two canonical landscape rotations of the
 * working portrait value 0xC0 — 0xA0 (MV|MY) and 0x60 (MV|MX) — both come out
 * MIRRORED left-to-right on a rescued board, not merely rotated. So this panel
 * needs a NON-canonical MADCTL: 0x20 is 0xA0 with MY cleared, which un-mirrors
 * the long axis while leaving the short axis (and therefore the vertical
 * position) alone. Believed to be a side effect of the rework — but whatever
 * the cause, MADCTL is the right place to cancel it.
 *
 *   mirrored left-to-right -> 0x20 <-> 0xE0 (flip the MY bit)
 *   upside down            -> add or remove MX (0x20 <-> 0x60)
 *
 * Neither mirror bit disturbs the offsets: the 160 axis is full-range, and 24
 * centres the 80 px window in a 128-wide GRAM, so both are symmetric. */
#define MADCTL_LANDSCAPE 0x20

#define PANEL_W          160         /* landscape: long axis across          */
#define PANEL_H          80

/* MADCTL's MV bit swaps the row and column counters, so the portrait offset
 * (24,0) swaps with it. 24 is the centre of a 128-wide GRAM, which makes it
 * symmetric under the MX/MY mirror bits — the value holds either way up. */
#define LAND_X_OFF       0
#define LAND_Y_OFF       24

#define COLMOD_VALUE     0x05
#define SPI_BAUD_DIV     (2u << 3)   /* PCLK/8 = 6 MHz                       */

#define BLACK            0x0000
#define WHITE            0xFFFF

/* noknok Purple, GAMMA-COMPENSATED FOR THIS PANEL — bench-picked 28 Aug 2026.
 *
 * The brand colour is #8B00F3, which encodes to RGB565 0x881E (R 17, G 0,
 * B 30). Sending that renders as BLUE here, because we run the minimal DCS
 * init with no vendor gamma table (the generic one killed the panel during
 * bring-up), so the controller sits on its power-on default curve. That curve
 * crushes MID-level channels hardest: #8B00F3 is R 54.8 % / B 96.8 %, so the
 * blue sails through untouched while the red is flattened out of the mix.
 *
 * 0xD81C (R 27, G 0, B 28) is what reads as noknok purple on the panel. Note
 * that it is nominally ~#DE00E6, a MAGENTA — we send magenta to get purple
 * back out, which is the measure of how far off the default red curve is.
 * Picked from the six-swatch comparison in SWATCH_MODE below.
 *
 * SCOPE: this is panel-specific compensation for a rework board on default
 * gamma, correct for this video and nothing else. #8B00F3 / 0x881E stays the
 * canonical brand value in the module firmware — do NOT propagate this number
 * into product code. Revisit once EastRising's real gamma table is in. */
#define NOKNOK_PURPLE    0xD81C

/* ── Logo colourway ─────────────────────────────────────────────────────────
 * The two official colourways from brand/logo/README.md. Only these two are
 * legal — the mark is always half brand-neutral, half purple.
 *
 *   DARK  (0) — black background, "nok" WHITE + "nok" PURPLE  (whitepurple)
 *   LIGHT (1) — white background, "nok" BLACK + "nok" PURPLE  (blackpurple)
 *
 * Dark is the default for the reveal: a full white field blows out the camera
 * exposure at this size, and the shot cuts straight into a black terminal
 * afterwards — staying on black keeps that transition seamless. */
#define LOGO_LIGHT_BG    0

#if LOGO_LIGHT_BG
  #define WM_BG          WHITE
  #define WM_NOK1        BLACK
#else
  #define WM_BG          BLACK
  #define WM_NOK1        WHITE
#endif
#define WM_NOK2          NOKNOK_PURPLE

/* Purple calibration screen — see the SWATCH_MODE block further down for what
 * it does and why. Declared up here because char_adv() is guarded by it. */
#define SWATCH_MODE      0

#define DC_LOW()    (GPIOC->BCR  = (1u << 3))
#define DC_HIGH()   (GPIOC->BSHR = (1u << 3))
#define RES_LOW()   (GPIOC->BCR  = (1u << 4))
#define RES_HIGH()  (GPIOC->BSHR = (1u << 4))

static inline void spi_tx(uint8_t b)
{
    while (!(SPI1->STATR & SPI_STATR_TXE)) { }
    SPI1->DATAR = b;
}
static inline void spi_idle(void) { while (SPI1->STATR & SPI_STATR_BSY) { } }

static void lcd_cmd(uint8_t c) { spi_idle(); DC_LOW(); spi_tx(c); spi_idle(); DC_HIGH(); }
static void lcd_dat(uint8_t d) { spi_tx(d); }

static void gpu_init(void)
{
    /* PC3 D/C + PC4 RES = push-pull out; PC5 SCK + PC6 MOSI = AF push-pull */
    GPIOC->CFGLR &= ~(0xFu << (4 * 3) | 0xFu << (4 * 4)
                    | 0xFu << (4 * 5) | 0xFu << (4 * 6));
    GPIOC->CFGLR |=  (0x1u << (4 * 3)) | (0x1u << (4 * 4))
                  |  (0xBu << (4 * 5)) | (0xBu << (4 * 6));

    SPI1->CTLR1 = (1u << 9)      /* SSM — CS is hard-wired to GND on the board */
                | (1u << 8)      /* SSI                                        */
                | SPI_BAUD_DIV
                | (1u << 2);     /* MSTR                                       */
    SPI1->CTLR2 = 0;
    SPI1->CTLR1 |= (1u << 6);    /* SPE                                        */

    RES_LOW();  Delay_Ms(50);
    RES_HIGH(); Delay_Ms(150);

    lcd_cmd(0x01);                          /* SWRESET */
    Delay_Ms(150);
    lcd_cmd(0x11);                          /* SLPOUT  */
    Delay_Ms(200);
    lcd_cmd(0x3A); lcd_dat(COLMOD_VALUE);
    lcd_cmd(0x36); lcd_dat(MADCTL_LANDSCAPE);
    spi_idle();
    lcd_cmd(0x21);                          /* INVON — mandatory on this cell */
    lcd_cmd(0x29);                          /* DISPON */
    Delay_Ms(20);
}

/* Open a GRAM window and leave the controller expecting pixels. */
static void gpu_window(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    uint16_t x0 = x + LAND_X_OFF, x1 = x0 + w - 1;
    uint16_t y0 = y + LAND_Y_OFF, y1 = y0 + h - 1;

    lcd_cmd(0x2A);
    lcd_dat(x0 >> 8); lcd_dat(x0 & 0xFF);
    lcd_dat(x1 >> 8); lcd_dat(x1 & 0xFF);

    lcd_cmd(0x2B);
    lcd_dat(y0 >> 8); lcd_dat(y0 & 0xFF);
    lcd_dat(y1 >> 8); lcd_dat(y1 & 0xFF);

    lcd_cmd(0x2C);
}

static void gpu_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c)
{
    uint32_t n;
    uint8_t  hi = c >> 8, lo = c & 0xFF;

    if (x >= PANEL_W || y >= PANEL_H || w == 0 || h == 0) return;
    if (x + w > PANEL_W) w = (uint8_t)(PANEL_W - x);
    if (y + h > PANEL_H) h = (uint8_t)(PANEL_H - y);

    gpu_window(x, y, w, h);
    n = (uint32_t)w * h;
    while (n--) { spi_tx(hi); spi_tx(lo); }
    spi_idle();
}

static void gpu_clear(uint16_t c) { gpu_fill_rect(0, 0, PANEL_W, PANEL_H, c); }

/* ══════════════════════════════════════════════════════════════════════════
 * PROPORTIONAL TEXT
 *
 * The module firmware draws the 8x8 font monospaced, which is right for a UI
 * but too wide here: "Hello, World!" monospaced at 16 px tall is 208 px and
 * the panel is 160. Trimming each glyph to its actual ink columns brings the
 * same string down to 144 px, so it fits at full size with the cursor after
 * it. Purely a cosmetic win for the camera; nothing else uses this path.
 *
 * The font is LSB-first — bit 0 of each row byte is the LEFTMOST pixel.
 *
 * "Bold" smears each row one pixel to the right (bits | bits<<1). At scale 3
 * that is what turns the wordmark from thin terminal text into something that
 * reads as a logotype.
 * ══════════════════════════════════════════════════════════════════════════ */

#define GAP_PX      1    /* letter spacing, in real pixels (not scaled)      */
#define SPACE_COLS  2    /* width of a space, in font columns                */

/* Ink bounds of a glyph. Returns 0 for a blank glyph (space). */
static uint8_t glyph_ink(const uint8_t *rows, uint8_t bold,
                         uint8_t *lo, uint8_t *hi)
{
    uint8_t all = 0, l = 0, h = 7;

    for (uint8_t r = 0; r < FONT8X8_H; r++) {
        uint8_t b = rows[r];
        if (bold) b |= (uint8_t)(b << 1);
        all |= b;
    }
    if (all == 0) return 0;

    while (!(all & (1u << l))) l++;
    while (!(all & (1u << h))) h--;
    *lo = l; *hi = h;
    return 1;
}

static const uint8_t *glyph_rows(char ch)
{
    uint8_t c = (uint8_t)ch;
    if (c < FONT8X8_FIRST || c > FONT8X8_LAST) c = '?';
    return font8x8[c - FONT8X8_FIRST];
}

/* How far the pen moves after drawing this character. Only the swatch screen
 * needs it: draw_char computes its own advance, and the terminal is left
 * aligned, so nothing has to measure a string up front. */
#if SWATCH_MODE
static uint8_t char_adv(char ch, uint8_t scale, uint8_t bold)
{
    uint8_t lo, hi;
    if (!glyph_ink(glyph_rows(ch), bold, &lo, &hi))
        return (uint8_t)(SPACE_COLS * scale + GAP_PX);
    return (uint8_t)((hi - lo + 1) * scale + GAP_PX);
}
#endif

/* Draw one character opaquely and return its advance. The whole cell —
 * ink columns plus the trailing gap — streams through a single GRAM window,
 * which is both fast and flicker-free: a redrawn character never shows a
 * half-erased frame on camera. */
static uint8_t draw_char(uint8_t x, uint8_t y, char ch, uint8_t scale,
                         uint8_t bold, uint16_t fg, uint16_t bg)
{
    const uint8_t *rows = glyph_rows(ch);
    uint8_t lo = 0, hi = 0, cols, w, h;
    uint8_t fh = fg >> 8, fl = fg & 0xFF;
    uint8_t bh = bg >> 8, bl = bg & 0xFF;

    if (!glyph_ink(rows, bold, &lo, &hi)) {
        w = (uint8_t)(SPACE_COLS * scale + GAP_PX);
        gpu_fill_rect(x, y, w, (uint8_t)(FONT8X8_H * scale), bg);
        return w;
    }

    cols = (uint8_t)(hi - lo + 1);
    w    = (uint8_t)(cols * scale + GAP_PX);
    h    = (uint8_t)(FONT8X8_H * scale);

    /* Never open a window that runs off the panel — the controller misbehaves
     * on an out-of-range address window rather than clipping politely. */
    if (x >= PANEL_W || y >= PANEL_H || x + w > PANEL_W || y + h > PANEL_H)
        return w;

    gpu_window(x, y, w, h);
    for (uint8_t r = 0; r < FONT8X8_H; r++) {
        uint8_t bits = rows[r];
        if (bold) bits |= (uint8_t)(bits << 1);

        for (uint8_t sy = 0; sy < scale; sy++) {
            for (uint8_t c = lo; c <= hi; c++) {
                uint8_t on = (bits >> c) & 1u;
                for (uint8_t sx = 0; sx < scale; sx++) {
                    spi_tx(on ? fh : bh);
                    spi_tx(on ? fl : bl);
                }
            }
            for (uint8_t g = 0; g < GAP_PX; g++) { spi_tx(bh); spi_tx(bl); }
        }
    }
    spi_idle();
    return w;
}

/* ══════════════════════════════════════════════════════════════════════════
 * THE WORDMARK — the real logo, not the built-in font
 *
 * wordmark.h holds ONE "nok" rasterised from Lilita One, the typeface the
 * official logo outlines are built from. The logo is that half drawn twice
 * side by side in two colours, which is exactly how the brand SVGs are put
 * together (one <path id="nok">, two <use> elements with different fills), so
 * we store one half, draw it twice, and get the two-tone for free.
 * ══════════════════════════════════════════════════════════════════════════ */

#define WM_W  (NOK_W * 2 + NOK_GAP)

/* Opaque 1bpp blit. Bits are MSB-first with each row padded to a byte
 * boundary; the padding bits are skipped rather than drawn. */
static void draw_nok(uint8_t x, uint8_t y, uint16_t fg, uint16_t bg)
{
    uint8_t fh = fg >> 8, fl = fg & 0xFF;
    uint8_t bh = bg >> 8, bl = bg & 0xFF;

    if (x >= PANEL_W || y >= PANEL_H
        || x + NOK_W > PANEL_W || y + NOK_H > PANEL_H) return;

    gpu_window(x, y, NOK_W, NOK_H);
    for (uint8_t r = 0; r < NOK_H; r++) {
        const uint8_t *row = nok_bitmap[r];
        for (uint8_t c = 0; c < NOK_W; c++) {
            uint8_t on = (row[c >> 3] >> (7 - (c & 7))) & 1u;
            spi_tx(on ? fh : bh);
            spi_tx(on ? fl : bl);
        }
    }
    spi_idle();
}

static void draw_wordmark(uint8_t x, uint8_t y)
{
    draw_nok(x, y, WM_NOK1, WM_BG);
    draw_nok((uint8_t)(x + NOK_W + NOK_GAP), y, WM_NOK2, WM_BG);
}

/* ══════════════════════════════════════════════════════════════════════════
 * BACKLIGHT — TIM1_CH1 on PD2. The fades are the whole reason this is here:
 * a PWM ramp reads as a real device waking up, where a hard cut just reads as
 * a slideshow.
 * ══════════════════════════════════════════════════════════════════════════ */

#define BL_PWM_MAX  999              /* 48 kHz — no beat against the camera  */

/* Peak brightness for the shoot. This is a DARK-ROOM setting: at 100 % the
 * panel blows out the exposure and the purple clips toward white.
 *
 * Perceived brightness goes roughly as luminance^0.45, so duty and apparent
 * brightness are not the same thing — 25 % duty still looks a bit over half
 * as bright, which is why this is not set lower.
 *
 *   150 = 15 %   very dim, for a pitch-black room
 *   250 = 25 %   dark room                          <- current
 *   400 = 40 %   dim room
 *   999 = 100 %  daylight / desk (the original)
 *
 * The fades all ramp to this value, so it dims the whole sequence coherently
 * and the 40-step ramp stays smooth (6 counts per step at 250). */
#define BL_FULL     250

static uint16_t bl_current = 0;

static void backlight_init(void)
{
    GPIOD->CFGLR &= ~(0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0xBu << (4 * 2));

    TIM1->PSC     = 0;
    TIM1->ATRLR   = BL_PWM_MAX;
    TIM1->CH1CVR  = 0;
    TIM1->CHCTLR1 = (6u << 4) | (1u << 3);   /* PWM mode 1 + preload         */
    TIM1->CCER    = (1u << 0);
    TIM1->BDTR   |= (1u << 15);              /* MOE — advanced timer, needed */
    TIM1->SWEVGR  = (1u << 0);
    TIM1->CTLR1  |= (1u << 0);
}

static void backlight_set(uint16_t duty)
{
    bl_current   = (duty > BL_PWM_MAX) ? BL_PWM_MAX : duty;
    TIM1->CH1CVR = bl_current;
}

/* 40 steps is enough to look continuous at these durations and coarse enough
 * that the step delay stays an integer number of milliseconds. */
static void backlight_ramp_to(uint16_t target, uint16_t ms)
{
    int32_t from = bl_current, delta = (int32_t)target - from;
    uint16_t step_ms = ms / 40;
    if (step_ms == 0) step_ms = 1;

    for (int32_t i = 0; i <= 40; i++) {
        backlight_set((uint16_t)(from + delta * i / 40));
        Delay_Ms(step_ms);
    }
    backlight_set(target);
}

/* ══════════════════════════════════════════════════════════════════════════
 * THE SEQUENCE
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── Purple calibration mode ────────────────────────────────────────────────
 * Set to 1 to replace the whole sequence with six purple swatches side by
 * side, numbered 1-6, and pick the one that reads as #8B00F3 on the actual
 * panel under the actual lights. Then copy that value into NOKNOK_PURPLE and
 * set this back to 0.
 *
 * WHY this is needed: we run the minimal DCS init with NO vendor gamma table
 * (the generic one killed the panel during bring-up), so the controller is on
 * its power-on default gamma. That curve crushes MID-level channels hardest.
 * #8B00F3 is R 54.8 % / B 96.8 % — the blue sails through untouched while the
 * red gets flattened, so a numerically correct violet renders as blue. The
 * cure is to pre-compensate the red, which is a thing you judge by eye, not
 * by arithmetic. Swatches are drawn edge to edge on purpose: adjacent
 * comparison is far more reliable than looking at one colour at a time. */

#if SWATCH_MODE
static const uint16_t swatches[6] = {
    0x881E,   /* 1  R17 B30 — exact #8B00F3; renders BLUE on this panel */
    0xA01E,   /* 2  R20 B30 */
    0xB81E,   /* 3  R23 B30 */
    0xC81D,   /* 4  R25 B29 */
    0xD81C,   /* 5  R27 B28 — PICKED, now NOKNOK_PURPLE */
    0xB81A,   /* 6  R23 B26 — red boosted AND blue pulled back */
};

static void run_swatch(void)
{
    gpu_clear(BLACK);
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t x  = (uint8_t)(2 + i * 26);
        uint8_t cw = char_adv((char)('1' + i), 2, 0);
        gpu_fill_rect(x, 0, 26, 56, swatches[i]);
        draw_char((uint8_t)(x + (26 - cw) / 2), 60, (char)('1' + i),
                  2, 0, WHITE, BLACK);
    }
    backlight_ramp_to(BL_FULL, 300);
    for (;;) { }
}
#endif

#define MSG_SCALE   2                /* 16 px tall — one size for everything  */
#define GLYPH_H     (FONT8X8_H * MSG_SCALE)

/* Cursor: a solid block, 4 font columns wide, the full height of the text.
 * A 3 px underscore is the more literal reading of "Hello, World!_" but it
 * disappears at this size once the camera is on it; the block is what sells
 * a live terminal. */
#define CURSOR_COLS 4
#define CURSOR_W    (CURSOR_COLS * MSG_SCALE)
#define CURSOR_GAP  2

/* Pass BLACK as the colour to erase. */
static void draw_cursor(uint8_t x, uint8_t y, uint16_t c)
{
    gpu_fill_rect(x, y, CURSOR_W, GLYPH_H, c);
}

/* ── A SCROLLING TERMINAL SESSION ──────────────────────────────────────────────────────────────────────
 * The whole piece is ONE terminal session: "Hello, World!" is just the first
 * line, and every line after it arrives as if Enter had been pressed.
 *
 * At 16 px text the panel holds FOUR rows (4 x 18 px pitch = 72 of 80), and
 * every line of copy is wider than the 160 px panel:
 *
 *     "Hello, World!"                   144 px   fits
 *     "wake up, maker..."               192 px   wraps
 *     "the modules have you."           248 px   wraps
 *     "do not follow the white rabbit."  348 px   wraps
 *     "> follow noknok"                 174 px   wraps
 *
 * Wrapped, that is TEN rows through a four-row window, so this version really
 * does scroll — six times. Scrolling is a full software redraw: the panel is
 * framebuffer-less, and the controller's hardware vertical scroll runs along
 * the panel's native axis, which the MV bit has turned into the screen's
 * HORIZONTAL axis, so it is useless to us here.
 *
 * The redraw is deliberately row-by-row rather than clear-then-redraw. Each
 * row band is fully overwritten in place (glyphs, then black to the right
 * edge, then the leading strip), so there is never an all-black frame. A
 * clear-first redraw costs the same ~50 ms but flashes the whole panel black
 * for about one video frame, which the camera would catch.
 *
 * The wrap is hard-coded, not computed. It is fixed marketing copy, and hand
 * breaking it keeps "rabbit." and "noknok" on rows of their own rather than
 * wherever a greedy wrapper would land. Giving "noknok" its own row with the
 * blinking purple cursor under it is the better ending anyway.
 */

#define TROWS       4                /* visible rows                          */
#define TROW_H      18               /* line pitch: 16 px text + 2 px leading */
#define TERM_X0     3
#define TERM_Y0     4                /* 4 rows -> last ends at 74 of 80       */

#define TERM_GREEN  0x07E0           /* phosphor green — a full channel, so   */
                                     /* the default gamma leaves it alone     */

#define HEAD_MS     70               /* "Hello, World!" — slow, ceremonial    */
#define TERM_MS     45               /* the feed — faster, more urgent        */
#define BLINK_MS    350
#define ENTER_MS    220              /* beat on the Enter press               */
#define WRAP_MS     80               /* beat on a wrap continuation           */

typedef struct {
    const char *text;
    uint8_t     enter;               /* 1 = Enter precedes it (new line)      */
                                     /* 0 = wrap continuation of the previous */
    uint8_t     blinks;              /* cursor blink cycles after this row    */
    uint16_t    fg;
} ScriptRow;

static const ScriptRow script[] = {
    { "Hello, World!", 1, 2, WHITE         },   /* 144 px */
    { "wake up,",      1, 0, TERM_GREEN    },   /*  99 px */
    { "maker...",      0, 1, TERM_GREEN    },   /*  87 px */
    { "the modules",   1, 0, TERM_GREEN    },   /* 136 px */
    { "have you.",     0, 1, TERM_GREEN    },   /* 106 px */
    { "do not follow", 1, 0, TERM_GREEN    },   /* 146 px */
    { "the white",     0, 0, TERM_GREEN    },   /* 106 px */
    { "rabbit.",       0, 1, TERM_GREEN    },   /*  84 px */
    { "> follow",      1, 0, NOKNOK_PURPLE },   /*  87 px */
    { "noknok",        0, 3, NOKNOK_PURPLE },   /*  81 px */
};
#define SCRIPT_LEN  (sizeof(script) / sizeof(script[0]))

typedef struct {
    const char *text;
    uint8_t     shown;               /* chars typed so far — a row mid-type   */
                                     /* must not redraw its unwritten tail    */
    uint16_t    fg;
} Row;

static Row    rows[TROWS];
static uint8_t row_count;
static uint8_t cur_x, cur_y;         /* where the cursor sits right now       */

/* Repaint one row band in place: glyphs, then black out to the right edge,
 * then the leading strip below. Never leaves the panel blank. */
static void term_draw_row(uint8_t i)
{
    uint8_t y = (uint8_t)(TERM_Y0 + i * TROW_H);
    uint8_t x = TERM_X0;

    gpu_fill_rect(0, y, TERM_X0, GLYPH_H, BLACK);
    if (i < row_count) {
        const char *t = rows[i].text;
        for (uint8_t j = 0; j < rows[i].shown; j++)
            x = (uint8_t)(x + draw_char(x, y, t[j], MSG_SCALE, 0,
                                        rows[i].fg, BLACK));
    }
    if (x < PANEL_W) gpu_fill_rect(x, y, (uint8_t)(PANEL_W - x), GLYPH_H, BLACK);
    gpu_fill_rect(0, (uint8_t)(y + GLYPH_H), PANEL_W,
                  (uint8_t)(TROW_H - GLYPH_H), BLACK);
}

static void term_redraw(void)
{
    for (uint8_t i = 0; i < TROWS; i++) term_draw_row(i);
}

/* Open a new row, scrolling the window up if it is already full. */
static void term_push(const char *text, uint16_t fg)
{
    uint8_t i;

    if (row_count == TROWS) {
        for (i = 1; i < TROWS; i++) rows[i - 1] = rows[i];
        i = TROWS - 1;
    } else {
        i = row_count++;
    }
    rows[i].text  = text;
    rows[i].shown = 0;
    rows[i].fg    = fg;

    /* Scrolling means every row moved, so the whole window is repainted.
     * On a non-scrolling push only the new (empty) row needs clearing. */
    if (row_count == TROWS && i == TROWS - 1) term_redraw();
    else                                      term_draw_row(i);

    cur_y = (uint8_t)(TERM_Y0 + i * TROW_H);
    cur_x = TERM_X0;
    draw_cursor(cur_x, cur_y, fg);
}

/* Type the current row out, carrying the cursor along with it. */
static void term_type(uint8_t ms)
{
    uint8_t     i = (uint8_t)(row_count - 1);
    const char *t = rows[i].text;
    uint8_t     x = TERM_X0;

    for (uint8_t j = 0; t[j]; j++) {
        draw_cursor(cur_x, cur_y, BLACK);        /* lift the cursor */
        x = (uint8_t)(x + draw_char(x, cur_y, t[j], MSG_SCALE, 0,
                                    rows[i].fg, BLACK));
        rows[i].shown = (uint8_t)(j + 1);
        cur_x = (uint8_t)(x + CURSOR_GAP);
        draw_cursor(cur_x, cur_y, rows[i].fg);
        Delay_Ms(ms);
    }
}

static void term_blink(uint8_t cycles, uint16_t fg)
{
    for (uint8_t b = 0; b < cycles; b++) {
        Delay_Ms(BLINK_MS);
        draw_cursor(cur_x, cur_y, BLACK);
        Delay_Ms(BLINK_MS);
        draw_cursor(cur_x, cur_y, fg);
    }
}

int main(void)
{
    SystemInit();

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD
                    | RCC_APB2Periph_SPI1  | RCC_APB2Periph_TIM1
                    | RCC_APB2Periph_AFIO;

    backlight_init();
    backlight_set(0);                /* panel init happens in the dark        */
    gpu_init();

#if SWATCH_MODE
    run_swatch();                    /* never returns */
#endif

    const uint8_t wm_x = (uint8_t)((PANEL_W - WM_W) / 2);    /* 146 px -> x=7 */
    const uint8_t wm_y = (uint8_t)((PANEL_H - NOK_H) / 2);

    for (;;) {
        /* ── 1. black ──────────────────────────────────────────────────────────────────────────────────────────── */
        backlight_set(0);
        gpu_clear(BLACK);
        Delay_Ms(250);

        /* ── 2. wordmark ──────────────────────────────────────────────────────────────────────────────────── */
        gpu_clear(WM_BG);
        draw_wordmark(wm_x, wm_y);
        backlight_ramp_to(BL_FULL, 280);
        Delay_Ms(700);

        /* ── 3. cut to the terminal ──────────────────────────────────────────────────────────────── */
        backlight_ramp_to(0, 150);
        gpu_clear(BLACK);
        row_count = 0;
        backlight_ramp_to(BL_FULL, 120);
        Delay_Ms(150);

        /* ── 4. the session ────────────────────────────────────────────────────────────────────────────── */
        for (uint8_t i = 0; i < SCRIPT_LEN; i++) {
            Delay_Ms(script[i].enter ? ENTER_MS : WRAP_MS);
            term_push(script[i].text, script[i].fg);
            term_type(i == 0 ? HEAD_MS : TERM_MS);
            if (script[i].blinks) term_blink(script[i].blinks, script[i].fg);
        }

        /* ── 5. fade out, hand back to step 1 ──────────────────────────────────────────────── */
        Delay_Ms(300);
        backlight_ramp_to(0, 350);
    }
}