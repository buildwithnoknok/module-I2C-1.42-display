/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — BACKLIGHT STATIC TEST  v0.1.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * The simplest possible backlight test. PD2 is driven as a PLAIN GPIO —
 * 3 s fully HIGH (backlight should be at 100 %), 3 s LOW, forever.
 *
 * This removes every firmware variable from the question: no TIM1, no PWM,
 * no MOE bit, no alternate-function mapping, no duty cycle. If the backlight
 * does not light with PD2 held hard high and 4.5 V present on C6, the fault
 * is downstream of the pin — 2N7002, the 100R gate resistor, the 33R ballast,
 * or the LEDA/LEDK joints on the FPC.
 *
 * Measure while it runs (a slow 3 s cadence is easy to follow on a meter):
 *   Q1 gate  (R10's Q1-side pad)  → should swing 0 V ↔ 3.3 V every 3 s
 *   Q1 drain (SOT-23 pin 3)       → should drop near 0 V while the gate is high
 *   LEDA (C6 / panel pin 12)      → ~4.5 V throughout
 *
 * Gate swinging but drain not pulling down = the 2N7002.
 * Drain pulling down but no light = the 33R, the LEDs, or the FPC joints.
 */

#include "ch32fun.h"
#include <stdint.h>

#define BL_HIGH()  (GPIOD->BSHR = (1u << 2))    /* PD2 = 1 → MOSFET on       */
#define BL_LOW()   (GPIOD->BCR  = (1u << 2))

int main(void)
{
    SystemInit();

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD;

    /* PD2 as plain push-pull output, 10 MHz. Deliberately NOT alternate
     * function — TIM1 is not involved in this test at all. */
    GPIOD->CFGLR &= ~(0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0x1u << (4 * 2));

    for (;;) {
        BL_HIGH(); Delay_Ms(3000);
        BL_LOW();  Delay_Ms(3000);
    }
}
