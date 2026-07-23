/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — "IS THE MCU RUNNING?"  v0.1.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * The smallest firmware that can exist and still tell you something.
 *
 *   1. PD1 is pulled LOW immediately at boot  → status LED lights AT ONCE,
 *      before any delay, clock work or peripheral setup. If the LED comes on
 *      the instant you apply power, the CPU fetched and executed instructions.
 *   2. Then it blinks at 1 Hz forever         → proves the code LOOPS, not
 *      just that a pin happened to be configured once.
 *   3. PD2 is held statically HIGH            → backlight full on, as a second
 *      independent indicator that does not involve PD1.
 *
 * Reading it, on JST/bus power with the Link-E DISCONNECTED:
 *
 *   LED on immediately, then blinking  → the MCU runs on bus power.
 *   LED dark                           → the MCU is not executing. Power is
 *                                        known good (3.25 V) and reset is
 *                                        known released (PD7 3.23 V), so the
 *                                        next suspect is the power-on ramp:
 *                                        try the manual-reset trick below.
 *
 * MANUAL RESET TRICK (no equipment): with the board powered from JST and
 * apparently dead, briefly short PD7 to GND, then release. That forces a clean
 * reset pulse the power-on ramp may have failed to produce. If the LED springs
 * to life afterwards, the fault is the power-up sequence — not the firmware,
 * not the MCU, and not the panel.
 */

#include "ch32fun.h"
#include <stdint.h>

#define LED_ON()   (GPIOD->BCR  = (1u << 1))    /* PD1 active LOW  */
#define LED_OFF()  (GPIOD->BSHR = (1u << 1))

int main(void)
{
    /* Light the LED as the very first thing, before SystemInit(), so that even
     * a clock-configuration failure still shows up as "LED on". */
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD;

    GPIOD->CFGLR &= ~(0xFu << (4 * 1) | 0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0x1u << (4 * 1))      /* PD1 output push-pull  */
                  |  (0x1u << (4 * 2));     /* PD2 output push-pull  */

    LED_ON();
    GPIOD->BSHR = (1u << 2);                /* PD2 high → backlight full on */

    SystemInit();

    for (;;) {
        LED_ON();  Delay_Ms(500);
        LED_OFF(); Delay_Ms(500);
    }
}
