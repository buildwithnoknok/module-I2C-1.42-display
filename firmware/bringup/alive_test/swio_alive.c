/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — SWIO EXECUTION PROOF  v0.1.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * Answers exactly one question: IS THE CPU EXECUTING OUR CODE?
 *
 * It prints a counter over the debug (SWIO) link once a second. Watch it with
 *     ../minichlink/minichlink -T
 * with the Link-E attached. Text scrolling = the chip is running our firmware,
 * full stop — no LED, no panel, no board component involved in that answer.
 *
 * It ALSO drives the backlight at 50 % (PD2 / TIM1_CH1), so one session tells
 * you both things at once:
 *
 *   text + backlight     → chip runs, backlight chain works. The dead status
 *                          LED is then the known PD1/SWIO ownership problem
 *                          (see below), NOT a fault.
 *   text, no backlight   → chip runs, backlight chain is broken. Suspect the
 *                          2N7002, the 100R gate resistor, the 33R ballast or
 *                          the LEDA/LEDK FPC joints.
 *   no text              → the CPU is not executing. Power, clock or reset.
 *
 * NOTE — there is deliberately NO status LED here. The LED sits on PD1, which
 * IS the SWIO debug pin, and this build uses that pin for printf. The two
 * cannot coexist. That shared-pin conflict is also the most likely reason the
 * status LED looked dead during bring-up: on CH32V003 the debug module keeps
 * ownership of PD1, so firmware driving it as a GPIO has no effect until SWD
 * is explicitly disabled. The noknok "status LED on SWIO" standard has never
 * been bench-validated on a real module — this is the first evidence on it.
 */

#include "ch32fun.h"
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint32_t n = 0;

    SystemInit();

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_TIM1
                    | RCC_APB2Periph_AFIO;

    /* PD2 = TIM1_CH1 alternate function, push-pull. PD1 is left alone so the
     * debug module can keep using it for printf. */
    GPIOD->CFGLR &= ~(0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0xBu << (4 * 2));

    TIM1->PSC     = 0;
    TIM1->ATRLR   = 999;                     /* 48 kHz                       */
    TIM1->CH1CVR  = 500;                     /* 50 %                         */
    TIM1->CHCTLR1 = (6u << 4) | (1u << 3);   /* PWM mode 1 + preload         */
    TIM1->CCER    = (1u << 0);               /* CC1E, active high            */
    TIM1->BDTR   |= (1u << 15);              /* MOE — advanced timer, MUST   */
    TIM1->SWEVGR  = (1u << 0);               /* UG — latch preloads          */
    TIM1->CTLR1  |= (1u << 0);               /* CEN — run                    */

    for (;;) {
        printf("noknok display alive %lu  (backlight PWM 50%%)\n",
               (unsigned long)n++);
        Delay_Ms(1000);
    }
}
