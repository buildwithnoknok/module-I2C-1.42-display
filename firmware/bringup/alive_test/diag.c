/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module — GPIO DIAGNOSTIC  v0.1.0
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * Run under the Link-E with:   ../minichlink/minichlink -T
 *
 * printf over SWIO is the one firmware behaviour already PROVEN on this board,
 * so it is used here as the trusted channel to report on everything else.
 *
 * Every 2 seconds this toggles PD2 (plain GPIO, no TIM1) and prints the actual
 * register contents read back from the chip. Measure Q1's gate (SOT-23 pin 1,
 * or R10's Q1-side pad) while watching the log — the terminal tells you what
 * the pin is *supposed* to be doing, the meter tells you what it is *actually*
 * doing.
 *
 *   terminal says PD2=1 and gate measures ~3.3 V  → the pin works. A dark
 *       backlight then means the fault is Q1, R8 (33R), or the LEDA/LEDK
 *       joints on the FPC — downstream of the MCU entirely.
 *   terminal says PD2=1 but gate stays at 0 V     → the pin is not driving.
 *       Compare OUTDR/CFGLR in the log against the expected values printed
 *       alongside them.
 *
 * PD1 is deliberately NOT touched: it is the SWIO pin this log travels over.
 */

#include "ch32fun.h"
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint32_t n = 0;

    SystemInit();

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD;

    /* PD2 = plain push-pull output. Nibble 2 of CFGLR should read 0x1. */
    GPIOD->CFGLR &= ~(0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0x1u << (4 * 2));

    printf("\n=== noknok display GPIO diagnostic ===\n");
    printf("APB2PCENR = %08lx  (GPIOD enable = bit 5)\n",
           (unsigned long)RCC->APB2PCENR);
    printf("GPIOD CFGLR = %08lx  (nibble 2 should be 1 = output PP 10MHz)\n",
           (unsigned long)GPIOD->CFGLR);

    for (;;) {
        GPIOD->BSHR = (1u << 2);            /* PD2 high */
        printf("%lu  PD2=1  OUTDR=%08lx  INDR=%08lx  <- measure gate now\n",
               (unsigned long)n, (unsigned long)GPIOD->OUTDR,
               (unsigned long)GPIOD->INDR);
        Delay_Ms(2000);

        GPIOD->BCR = (1u << 2);             /* PD2 low */
        printf("%lu  PD2=0  OUTDR=%08lx  INDR=%08lx\n",
               (unsigned long)n++, (unsigned long)GPIOD->OUTDR,
               (unsigned long)GPIOD->INDR);
        Delay_Ms(2000);
    }
}
