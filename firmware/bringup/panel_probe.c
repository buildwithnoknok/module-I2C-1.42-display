/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — PANEL READ-BACK PROBE  v0.1.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * Run under the Link-E with:   ../minichlink/minichlink -T
 *
 * Separates the two stacked unknowns on a reworked rev 1.0 board:
 *   (a) are the FPC joints on the SPI end actually connected?
 *   (b) is our NV3022B init sequence right for THIS panel?
 *
 * It resets the panel, reads its ID/status registers, then runs the init and
 * reads them AGAIN.
 *
 *   all 00 / all FF          -> no communication. Wiring, not firmware. The
 *                               init sequence is irrelevant until this is fixed.
 *   sensible ID bytes        -> panel alive and talking; wiring is good and the
 *                               init sequence becomes the suspect.
 *   RDDPM changes after init -> our WRITES land too. Both directions proven,
 *                               so it is purely init CONTENT. Get EastRising's
 *                               demo code and swap the table wholesale.
 *
 * ── BIT-BANGED READ — deliberate rule exception, flagged and approved ─────
 * noknok's standing rule is "no bit-banging, use hardware peripherals". This
 * file breaks it ON PURPOSE, for the read direction only, because:
 *   - PC7/MISO is NOT connected on this board, so full-duplex SPI cannot read.
 *     (Worth wiring MISO on a future revision purely for this.)
 *   - SPI bidirectional receive-only mode free-runs its clock while SPE=1 and
 *     is awkward to stop cleanly on the last byte.
 *   - An SPI READ has no timing-critical bitstream: any clock rate works and
 *     slower is safer. This is nothing like the WS2812 waveform that bit us on
 *     the LED Button, where the timing WAS the protocol.
 *   - No I2C ISR is running, so nothing is starved.
 * This is a bench diagnostic and must never be copied into module firmware.
 * WRITES still go through hardware SPI1, as normal.
 */

#include "ch32fun.h"
#include <stdint.h>
#include <stdio.h>

#define PANEL_ROTATED_REWORK  1
#if PANEL_ROTATED_REWORK
  #define MADCTL_VALUE   0xC0
#else
  #define MADCTL_VALUE   0x00
#endif
#define COLMOD_VALUE     0x05
#define SPI_BAUD_DIV     (2u << 3)      /* /8 = 6 MHz for writes */

#define DC_LOW()    (GPIOC->BCR  = (1u << 3))
#define DC_HIGH()   (GPIOC->BSHR = (1u << 3))
#define RES_LOW()   (GPIOC->BCR  = (1u << 4))
#define RES_HIGH()  (GPIOC->BSHR = (1u << 4))

/* ── hardware SPI: writes ────────────────────────────────────────────────── */

static void spi_hw_on(void)
{
    /* PC5 SCK + PC6 MOSI back to alternate function */
    GPIOC->CFGLR &= ~(0xFu << (4 * 5) | 0xFu << (4 * 6));
    GPIOC->CFGLR |=  (0xBu << (4 * 5)) | (0xBu << (4 * 6));
    SPI1->CTLR1 = (1u << 9) | (1u << 8) | SPI_BAUD_DIV | (1u << 2);
    SPI1->CTLR2 = 0;
    SPI1->CTLR1 |= (1u << 6);
}

static inline void spi_tx(uint8_t b)
{
    while (!(SPI1->STATR & SPI_STATR_TXE)) { }
    SPI1->DATAR = b;
}
static inline void spi_idle(void) { while (SPI1->STATR & SPI_STATR_BSY) { } }

static void lcd_cmd(uint8_t c)  { spi_idle(); DC_LOW(); spi_tx(c); spi_idle(); DC_HIGH(); }
static void lcd_data(uint8_t d) { spi_tx(d); }

/* ── bit-banged read path (see the rule-exception note above) ────────────── */

#define SCK_H()   (GPIOC->BSHR = (1u << 5))
#define SCK_L()   (GPIOC->BCR  = (1u << 5))
#define SDA_H()   (GPIOC->BSHR = (1u << 6))
#define SDA_L()   (GPIOC->BCR  = (1u << 6))
#define SDA_BIT() ((GPIOC->INDR >> 6) & 1u)

static inline void bb_tick(void) { Delay_Us(2); }   /* ~250 kHz, deliberately slow */

static void bb_mode(void)
{
    SPI1->CTLR1 &= ~(1u << 6);                       /* SPE off              */
    GPIOC->CFGLR &= ~(0xFu << (4 * 5) | 0xFu << (4 * 6));
    GPIOC->CFGLR |=  (0x1u << (4 * 5)) | (0x1u << (4 * 6));   /* both output */
    SCK_L();
}

static void sda_in(void)
{
    GPIOC->CFGLR &= ~(0xFu << (4 * 6));
    GPIOC->CFGLR |=  (0x4u << (4 * 6));              /* floating input       */
}

static void bb_write8(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        SCK_L();
        if ((b >> i) & 1) SDA_H(); else SDA_L();
        bb_tick();
        SCK_H();
        bb_tick();
    }
    SCK_L();
}

static uint8_t bb_read8(void)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        SCK_L();
        bb_tick();
        SCK_H();
        v = (uint8_t)((v << 1) | SDA_BIT());
        bb_tick();
    }
    SCK_L();
    return v;
}

/* Read `n` bytes back from register `reg`. Reads one extra leading byte,
 * because most MIPI-DCS controllers emit a dummy clock byte first — printing
 * it too means we can see the alignment instead of guessing at it. */
static void panel_read(const char *name, uint8_t reg, int n)
{
    uint8_t buf[6];

    bb_mode();
    DC_LOW();
    bb_write8(reg);
    DC_HIGH();
    sda_in();
    for (int i = 0; i < n + 1 && i < 6; i++) buf[i] = bb_read8();

    /* ch32fun's mini-printf does not handle width specifiers like %-8s, and
     * mixing several conversions in one call misbehaves — keep it dead simple,
     * one conversion per call. */
    printf("  ");
    printf(name);
    printf(":");
    for (int i = 0; i < n + 1 && i < 6; i++) { printf(" "); printf("%02x", buf[i]); }
    printf("\n");

    spi_hw_on();
}

static void read_all(const char *when)
{
    printf("\n--- panel registers %s ---\n", when);
    panel_read("RDDID",   0x04, 3);   /* manufacturer / version / driver id  */
    panel_read("RDDST",   0x09, 4);   /* display status                      */
    panel_read("RDDPM",   0x0A, 1);   /* power mode: sleep / display on-off  */
    panel_read("RDDMADCTL", 0x0B, 1); /* memory access control readback      */
    panel_read("RDDCOLMOD", 0x0C, 1); /* pixel format readback               */
}

/* ── init table (generic NV3022B vendor sequence — UNVERIFIED for this panel) */

#define DLY 0x80
static const uint8_t nv3022b_init_seq[] = {
    0xFD, 2, 0x06, 0x08,
    0x61, 2, 0x07, 0x07,
    0x73, 1, 0x70,
    0x73, 1, 0x00,
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
    0xE0, 8, 0x06, 0x05, 0x0B, 0x12, 0x10, 0x10, 0x10, 0x15,
    0xE3, 8, 0x15, 0x10, 0x11, 0x0E, 0x12, 0x0D, 0x06, 0x06,
    0xE1, 2, 0x35, 0x75,
    0xE4, 2, 0x74, 0x35,
    0xE2, 6, 0x22, 0x22, 0x21, 0x35, 0x36, 0x3F,
    0xE5, 6, 0x3F, 0x35, 0x34, 0x21, 0x22, 0x22,
    0xF1, 3, 0x01, 0x01, 0x02,
    0xF6, 4, 0x09, 0x30, 0x00, 0x00,
    0xFD, 2, 0xFA, 0xFC,
    0x3A, 1, COLMOD_VALUE,
    0x36, 1, MADCTL_VALUE,
    0x11, DLY | 0, 200,
    0x29, DLY | 0, 20,
    0x00
};

static void run_init(void)
{
    const uint8_t *p = nv3022b_init_seq;
    while (*p) {
        uint8_t cmd = *p++, n = *p++, wait = n & DLY;
        n &= 0x7F;
        lcd_cmd(cmd);
        while (n--) lcd_data(*p++);
        spi_idle();
        if (wait) Delay_Ms(*p++);
    }
}

int main(void)
{
    SystemInit();

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD
                    | RCC_APB2Periph_SPI1  | RCC_APB2Periph_TIM1
                    | RCC_APB2Periph_AFIO;

    GPIOC->CFGLR &= ~(0xFu << (4 * 3) | 0xFu << (4 * 4));
    GPIOC->CFGLR |=  (0x1u << (4 * 3)) | (0x1u << (4 * 4));   /* D/C, RES */
    GPIOD->CFGLR &= ~(0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0xBu << (4 * 2));                       /* backlight */

    spi_hw_on();

    /* backlight on so you can see whatever happens */
    TIM1->PSC = 0;  TIM1->ATRLR = 999;  TIM1->CH1CVR = 700;
    TIM1->CHCTLR1 = (6u << 4) | (1u << 3);
    TIM1->CCER = (1u << 0);
    TIM1->BDTR |= (1u << 15);
    TIM1->SWEVGR = (1u << 0);
    TIM1->CTLR1 |= (1u << 0);

    printf("\n\n=== noknok display panel probe ===\n");
    printf("Reading the NV3022B back over SDA (bit-banged read, hw SPI writes).\n");

    RES_LOW();  Delay_Ms(50);
    RES_HIGH(); Delay_Ms(150);

    read_all("AFTER RESET (before init)");

    printf("\nrunning init sequence...\n");
    run_init();

    read_all("AFTER INIT");

    printf("\nIf every byte above is 00 or FF -> no communication, it is the\n");
    printf("FPC joints on the SPI end (pads 3-6), not the init sequence.\n");
    printf("If RDDPM changed between the two dumps -> our writes ARE landing.\n");

    for (;;) {
        Delay_Ms(5000);
        read_all("periodic re-read");
    }
}
