/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — MINIMAL ALIVE TEST  v0.1.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * Deliberately the smallest useful firmware: NO SPI, NO panel, NO I2C.
 * It only proves two things, so a dead display can be bisected:
 *
 *   1. Is the MCU running at all?      → status LED on PD1 blinks 2 Hz
 *   2. Does the backlight chain work?  → PD2 PWM held at 50 %, screen glows
 *
 * Reading the result:
 *   LED blinks + screen glows  → MCU and backlight are fine. The fault is in
 *                                SPI or the NV3022B init. Go back to
 *                                display_bringup and tune COLMOD / SPI speed /
 *                                the init table.
 *   LED blinks, NO glow        → backlight chain. Boost output is known good
 *                                (4.6 V on C6), so suspect the 2N7002, its
 *                                100R gate resistor, the 33R ballast, or the
 *                                LEDK/LEDA FPC joints. Scope PD2 for a 48 kHz
 *                                square wave — if PD2 is dead it is TIM1/MOE.
 *   Nothing at all             → the board is not powered, or the MCU is not
 *                                running. Check 3.3V-PROT at the MCU VDD pin.
 */

#include "ch32fun.h"
#include <stdint.h>

#define LED_ON()   (GPIOD->BCR  = (1u << 1))    /* PD1 active LOW */
#define LED_OFF()  (GPIOD->BSHR = (1u << 1))

int main(void)
{
    SystemInit();

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_TIM1
                    | RCC_APB2Periph_AFIO;

    /* PD1 = status LED (output push-pull), PD2 = TIM1_CH1 (AF push-pull). */
    GPIOD->CFGLR &= ~(0xFu << (4 * 1) | 0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0x1u << (4 * 1)) | (0xBu << (4 * 2));
    LED_OFF();

    /* Backlight PWM: 48 MHz / 1000 = 48 kHz, duty 50 %. */
    TIM1->PSC     = 0;
    TIM1->ATRLR   = 999;
    TIM1->CH1CVR  = 500;
    TIM1->CHCTLR1 = (6u << 4) | (1u << 3);   /* PWM mode 1 + preload         */
    TIM1->CCER    = (1u << 0);               /* CC1E, active high            */
    TIM1->BDTR   |= (1u << 15);              /* MOE — advanced timer, MUST   */
    TIM1->SWEVGR  = (1u << 0);               /* UG — latch the preloads      */
    TIM1->CTLR1  |= (1u << 0);               /* CEN — run                    */

    for (;;) {
        LED_ON();  Delay_Ms(250);
        LED_OFF(); Delay_Ms(250);
    }
}
