/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — PANEL FLOOD TEST  v0.1.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * Answers ONE question: does the panel respond to SPI at all?
 *
 * The offset sweep assumes the panel already works and only the window
 * position is unknown. This test removes that assumption: it floods the
 * controller's ENTIRE addressable GRAM (0..239 x 0..319) with a solid colour,
 * so whatever the real offset turns out to be, the visible 80x160 window is
 * guaranteed to sit inside the painted area.
 *
 * Sequence, ~2 s each, forever:  RED -> GREEN -> BLUE -> WHITE -> BLACK
 *
 *   Colours cycle          → the panel is alive and the SPI link works.
 *                            Only the window offset is left to find; go back
 *                            to display_bringup and run the sweep.
 *   Uniform milky/white    → backlight on, panel NOT powered or NOT driven.
 *                            The LC is passing light unmodulated. Suspect the
 *                            panel VCC joint (pad 10) or SCK/MOSI/DC/RES.
 *   Stays dark/black       → panel may be powered but never initialised.
 *                            Suspect RES (PC4) or D/C (PC3).
 *
 * Note the backlight already works, which proves the LEDA/LEDK joints at one
 * END of the FPC are good. The SPI signals live at the OTHER end (pads 3-6),
 * so a partially-successful rework can easily give exactly this symptom.
 */

#include "ch32fun.h"
#include <stdint.h>

#define PANEL_ROTATED_REWORK  1

#if PANEL_ROTATED_REWORK
  #define MADCTL_VALUE   0xC0
#else
  #define MADCTL_VALUE   0x00
#endif

#define COLMOD_VALUE     0x05        /* try 0x55 if colours are garbage      */
#define SPI_BAUD_DIV     (2u << 3)   /* /8 = 6 MHz — conservative for a rework */

/* Flood the whole controller RAM, not just the 80x160 window. */
#define FLOOD_W          240
#define FLOOD_H          320

#define BL_LEVEL         700
#define BL_MAX           999

#define DC_LOW()    (GPIOC->BCR  = (1u << 3))
#define DC_HIGH()   (GPIOC->BSHR = (1u << 3))
#define RES_LOW()   (GPIOC->BCR  = (1u << 4))
#define RES_HIGH()  (GPIOC->BSHR = (1u << 4))

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

static void lcd_data(uint8_t d) { spi_tx(d); }

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

static void lcd_run_init_table(const uint8_t *p)
{
    while (*p) {
        uint8_t cmd = *p++, n = *p++, wait = n & DLY;
        n &= 0x7F;
        lcd_cmd(cmd);
        while (n--) lcd_data(*p++);
        spi_wait_idle();
        if (wait) Delay_Ms(*p++);
    }
}

static void flood(uint16_t c)
{
    uint32_t n = (uint32_t)FLOOD_W * FLOOD_H;
    uint8_t hi = c >> 8, lo = c & 0xFF;

    lcd_cmd(0x2A);                                  /* CASET 0..FLOOD_W-1 */
    lcd_data(0); lcd_data(0);
    lcd_data((FLOOD_W - 1) >> 8); lcd_data((FLOOD_W - 1) & 0xFF);

    lcd_cmd(0x2B);                                  /* RASET 0..FLOOD_H-1 */
    lcd_data(0); lcd_data(0);
    lcd_data((FLOOD_H - 1) >> 8); lcd_data((FLOOD_H - 1) & 0xFF);

    lcd_cmd(0x2C);                                  /* RAMWR              */
    while (n--) { spi_tx(hi); spi_tx(lo); }
    spi_wait_idle();
}

int main(void)
{
    SystemInit();

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD
                    | RCC_APB2Periph_SPI1  | RCC_APB2Periph_TIM1
                    | RCC_APB2Periph_AFIO;

    GPIOC->CFGLR &= ~(0xFu << (4 * 3) | 0xFu << (4 * 4)
                    | 0xFu << (4 * 5) | 0xFu << (4 * 6));
    GPIOC->CFGLR |=  (0x1u << (4 * 3))    /* PC3 D/C  */
                  |  (0x1u << (4 * 4))    /* PC4 RES  */
                  |  (0xBu << (4 * 5))    /* PC5 SCK  */
                  |  (0xBu << (4 * 6));   /* PC6 MOSI */

    GPIOD->CFGLR &= ~(0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0xBu << (4 * 2));   /* PD2 backlight PWM */

    SPI1->CTLR1 = (1u << 9) | (1u << 8) | SPI_BAUD_DIV | (1u << 2);
    SPI1->CTLR2 = 0;
    SPI1->CTLR1 |= (1u << 6);

    TIM1->PSC = 0;  TIM1->ATRLR = BL_MAX;  TIM1->CH1CVR = BL_LEVEL;
    TIM1->CHCTLR1 = (6u << 4) | (1u << 3);
    TIM1->CCER = (1u << 0);
    TIM1->BDTR |= (1u << 15);
    TIM1->SWEVGR = (1u << 0);
    TIM1->CTLR1 |= (1u << 0);

    RES_LOW();  Delay_Ms(50);
    RES_HIGH(); Delay_Ms(150);

    lcd_run_init_table(nv3022b_init_seq);

    for (;;) {
        flood(0xF800); Delay_Ms(2000);   /* RED   */
        flood(0x07E0); Delay_Ms(2000);   /* GREEN */
        flood(0x001F); Delay_Ms(2000);   /* BLUE  */
        flood(0xFFFF); Delay_Ms(2000);   /* WHITE */
        flood(0x0000); Delay_Ms(2000);   /* BLACK */
    }
}
