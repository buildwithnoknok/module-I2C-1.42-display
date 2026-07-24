/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — MINIMAL-INIT FLOOD TEST  v0.1.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * Counter-test to panel_flood.c. Identical in every way EXCEPT the init:
 * this one sends only the four standard MIPI-DCS commands that every
 * controller of this family implements, and NONE of the vendor registers.
 *
 *     0x01 SWRESET   -> 150 ms
 *     0x11 SLPOUT    -> 200 ms
 *     0x3A COLMOD    = 0x05 (RGB565)
 *     0x36 MADCTL    = 0xC0 (180deg, cancels the physically rotated panel)
 *     0x29 DISPON    -> 20 ms
 *
 * WHY: panel_flood.c uses a generic NV3022B vendor table taken from
 * Espressif's driver, written for a DIFFERENT panel. Those ~30 undocumented
 * registers (0xFD, 0x61, 0x62, 0x65-0x68, 0xE0-0xE8 ...) configure power
 * rails, gamma and panel timing. Wrong values there do not merely look bad —
 * they can leave the driver in a state where nothing is displayed at all,
 * even though the SPI interface is working perfectly.
 *
 * Most controllers of this family come up usable on their power-on defaults,
 * so this minimal sequence is a genuine second shot on goal.
 *
 *   Colours appear here but not with the full table  -> the vendor table is
 *       the problem. Wait for EastRising's real sequence and swap it in.
 *   Nothing here either                              -> the init is NOT the
 *       variable. Look at the interface itself: joints, or CS tied to GND.
 */

#include "ch32fun.h"
#include <stdint.h>

#define MADCTL_VALUE     0xC0        /* panel physically rotated 180 deg     */
/* RGB565. 0x05 is the ST-style encoding (low nibble only); 0x55 is the full
 * MIPI-DCS encoding (RGB interface AND MCU interface both 16-bit).
 * BENCH RESULT: 0x05 did NOT take effect on this panel — the controller stayed
 * in 18-bit (3 bytes/pixel), so our 2-byte stream filled only 2/3 of the GRAM
 * (rest left as power-on noise) and the bytes landed misaligned across pixel
 * boundaries, turning green/blue into teal. 0x55 is the correct value here. */
#define COLMOD_VALUE     0x55
#define SPI_BAUD_DIV     (2u << 3)   /* /8 = 6 MHz                           */

#define FLOOD_W          240
#define FLOOD_H          320

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

static void lcd_cmd(uint8_t c)  { spi_idle(); DC_LOW(); spi_tx(c); spi_idle(); DC_HIGH(); }
static void lcd_data(uint8_t d) { spi_tx(d); }

static void flood(uint16_t c)
{
    uint32_t n = (uint32_t)FLOOD_W * FLOOD_H;
    uint8_t hi = c >> 8, lo = c & 0xFF;

    lcd_cmd(0x2A);
    lcd_data(0); lcd_data(0);
    lcd_data((FLOOD_W - 1) >> 8); lcd_data((FLOOD_W - 1) & 0xFF);

    lcd_cmd(0x2B);
    lcd_data(0); lcd_data(0);
    lcd_data((FLOOD_H - 1) >> 8); lcd_data((FLOOD_H - 1) & 0xFF);

    lcd_cmd(0x2C);
    while (n--) { spi_tx(hi); spi_tx(lo); }
    spi_idle();
}

int main(void)
{
    SystemInit();

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD
                    | RCC_APB2Periph_SPI1  | RCC_APB2Periph_TIM1
                    | RCC_APB2Periph_AFIO;

    GPIOC->CFGLR &= ~(0xFu << (4 * 3) | 0xFu << (4 * 4)
                    | 0xFu << (4 * 5) | 0xFu << (4 * 6));
    GPIOC->CFGLR |=  (0x1u << (4 * 3)) | (0x1u << (4 * 4))
                  |  (0xBu << (4 * 5)) | (0xBu << (4 * 6));

    GPIOD->CFGLR &= ~(0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0xBu << (4 * 2));

    SPI1->CTLR1 = (1u << 9) | (1u << 8) | SPI_BAUD_DIV | (1u << 2);
    SPI1->CTLR2 = 0;
    SPI1->CTLR1 |= (1u << 6);

    TIM1->PSC = 0;  TIM1->ATRLR = 999;  TIM1->CH1CVR = 700;
    TIM1->CHCTLR1 = (6u << 4) | (1u << 3);
    TIM1->CCER = (1u << 0);
    TIM1->BDTR |= (1u << 15);
    TIM1->SWEVGR = (1u << 0);
    TIM1->CTLR1 |= (1u << 0);

    /* hardware reset */
    RES_LOW();  Delay_Ms(50);
    RES_HIGH(); Delay_Ms(150);

    /* ── the whole init, and nothing else ─────────────────────────────── */
    lcd_cmd(0x01);                       /* SWRESET */
    Delay_Ms(150);
    lcd_cmd(0x11);                       /* SLPOUT  */
    Delay_Ms(200);
    lcd_cmd(0x3A); lcd_data(COLMOD_VALUE);
    lcd_cmd(0x36); lcd_data(MADCTL_VALUE);
    lcd_cmd(0x29);                       /* DISPON  */
    Delay_Ms(20);

    for (;;) {
        flood(0xF800); Delay_Ms(2000);   /* RED   */
        flood(0x07E0); Delay_Ms(2000);   /* GREEN */
        flood(0x001F); Delay_Ms(2000);   /* BLUE  */
        flood(0xFFFF); Delay_Ms(2000);   /* WHITE */
        flood(0x0000); Delay_Ms(2000);   /* BLACK */
    }
}
