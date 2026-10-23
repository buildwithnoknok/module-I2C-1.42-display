/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 noknok / Christopher Houben
 *
 * noknok 1.42" Display Module Firmware  v0.7.1
 *
 * v0.4.0: DEV-31 hardening C — independent watchdog + boot-attempt handshake
 *   with stage-1 (clears the counter once an I2C address is assigned).
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * A real noknok I2C module: enumeration, the standard system commands, drawing
 * primitives, a built-in 8x8 monospace font (ASCII, integer-scaled 1-8) and the
 * 1bpp host-blit path. Icons and a second larger font are still to come.
 *
 * TEXT SIZING — the agreed split. The module renders the NATIVE heights only
 * (8/16/24/32/40/48/56/64 = the 8x8 font at scale 1-8). Any other size, and any
 * nicer typeface, is rasterised on the Pico and sent as a 1bpp blit. noknok.py picks
 * the path automatically, so `size` is always an exact pixel height and there
 * is never a "size not supported" error.
 *
 * ── Bootloader-hosted build ───────────────────────────────────────────────
 *   Runs UNDER the shared noknok I2C bootloader (module-I2C-bootloader).
 *   Linked at the 0x1400 flash offset (app.ld) and reserves the top 16 bytes
 *   of RAM for the handoff cell at 0x200007F0. Command 0xB0 drops the module
 *   back into the bootloader so the Pico can re-flash it over I2C.
 *
 * ── Hardware ──────────────────────────────────────────────────────────────
 *   PC1  I2C SDA                PC5  SPI1_SCK  -> 22R -> panel SCL
 *   PC2  I2C SCL                PC6  SPI1_MOSI -> 22R -> panel SDA
 *   PC3/PC4  D/C + panel RESET - SWAPPED between board revisions, see
 *            BOARD_REV_11 below. rev 1.1: PC3 = RES, PC4 = D/C.
 *   PD2  Backlight PWM (TIM1_CH1, needs the MOE bit)
 *   PD1  SWIO / flash pad. NOTE: the status LED on this pin DOES NOT WORK —
 *        the debug module owns PD1. See Jira DEV-20. Not driven here.
 *   CS is tied to GND on the board.
 *
 * ── Panel (all three bench-confirmed Jul 23 2026) ─────────────────────────
 *   NV3022B, 80x160 visible window at GRAM OFFSET (24, 0).
 *   MINIMAL DCS init only — the generic vendor register table gives a dead
 *   screen on this panel. INVON (0x21) is REQUIRED or all colours invert.
 *
 * ── Enumeration ───────────────────────────────────────────────────────────
 *   Staging address : 0x7F        MODULE_TYPE : 0x05 (display)
 *   Response (10 bytes): [UID x8][0x05][CRC8]
 *
 * ── I2C protocol after enumeration ───────────────────────────────────────
 *   Master WRITE:
 *     [0x01, colHi, colLo]                        CLEAR to colour
 *     [0x02, x, y, w, h, colHi, colLo]            FILL_RECT
 *     [0x03, x, y, style, fgHi,fgLo, bgHi,bgLo, ascii...]   DRAW_TEXT
 *              style: bits0-1 font id, bits2-4 (scale-1) => scale 1-8,
 *                     bit7 transparent background
 *     [0x05, x, y, w, h, fgHi,fgLo, bgHi,bgLo, flags]       BLIT_BEGIN (1bpp)
 *     [0x06, <=64 bytes of 1bpp rows]                       BLIT_DATA (repeat)
 *     [0x10, level]                               SET_BACKLIGHT (0-255)
 *     [0x12, mode]                                DISPLAY 0=off 1=on 2=sleep
 *     [0x15]                                      GET_INFO -> next read = 5 B
 *     [0xB0]                                      ENTER_BOOTLOADER
 *     [0xB1]                                      GET_VERSION -> next read = 4 B
 *   Master READ (2 bytes, default):
 *     [0] status  bit0 = busy drawing
 *     [1] last error (0 = none). Read-to-clear since v0.7.1 (DEV-44): the
 *         next status read returns 0 unless a new error happened.
 *   Master READ (4 bytes, after 0xB1):
 *     [PROTOCOL_VER, FW_MAJOR, FW_MINOR, FW_PATCH]
 *   Master READ (5 bytes, after 0x15):
 *     [width, height, colour_depth_code, n_fonts, n_icons]
 *
 *   GET_INFO exists so ONE Pico-side driver serves every display tier — it
 *   asks the module its geometry instead of hard-coding 80x160.
 *
 *   0xB0-0xBF is the reserved ecosystem range. See Ecosystem/software §5.
 */

#include "ch32fun.h"
#include <stdint.h>
#include "font8x8.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

#define ENUM_ADDR        0x7F
#define MODULE_TYPE      0x05
#define REG_ASSIGN_ADDR  0x1D

#define PROTOCOL_VERSION 0x01
#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 7
#define FW_VERSION_PATCH 1

#define CMD_CLEAR            0x01
#define CMD_FILL_RECT        0x02
#define CMD_DRAW_TEXT        0x03
#define CMD_BLIT_BEGIN       0x05
#define CMD_BLIT_DATA        0x06
#define CMD_SET_BACKLIGHT    0x10
#define CMD_SET_ROTATION     0x11
#define CMD_DISPLAY          0x12
#define CMD_SET_ORIENT       0x14   /* bench calibration: raw MADCTL + offsets */
#define CMD_GET_INFO         0x15
#define CMD_ENTER_BOOTLOADER 0xB0
#define CMD_GET_VERSION      0xB1

#define UID_ADDR         ((volatile uint8_t*)0x1FFFF7E8)
#define UID_LEN          8

#define BL_MAGIC_CELL    (*(volatile uint32_t *)0x200007F0U)
#define BL_MAGIC_ENTER   0x6E6B4231U   /* "nkB1" */

/* ── App health handshake with stage-1 (DEV-31 hardening C) ──────────────────
 * Stage-1 counts every boot of this app in a no-init RAM cell and stops booting
 * it after three consecutive warm resets without the app ever proving itself,
 * parking the module in the bootloader at 0x7E so the host can push a good
 * image instead. That turns "a broken app with a valid CRC is booted forever"
 * into "a broken app gets three tries". Two things make it work here:
 *   - the independent watchdog, so a hang becomes a warm reset (~2 s), and
 *   - clearing the counter the moment we KNOW we are healthy: an I2C address
 *     has been assigned, i.e. enumeration completed, i.e. I2C works.
 * Address and tag must match noknok_stage1. */
#define BOOT_ATTEMPT_CELL (*(volatile uint32_t *)0x200007F8U)

static void iwdg_init(void)
{
    /* LSI ~128 kHz / 64 = 2 kHz; reload 4095 -> ~2.05 s. Hardware starts LSI. */
    IWDG->CTLR = 0x5555;  IWDG->PSCR = 4;
    IWDG->CTLR = 0x5555;  IWDG->RLDR = 0xFFF;
    IWDG->CTLR = 0xCCCC;  /* start — cannot be stopped except by reset */
}
static inline void iwdg_kick(void) { IWDG->CTLR = 0xAAAA; }

/* ── Panel geometry ──────────────────────────────────────────────────────
 * The 80x160 visible area is a window inside the controller's larger GRAM.
 * (24, 0) was found empirically with the bring-up offset sweep. */
#define PANEL_W          80
#define PANEL_H          160
#define GRAM_X_OFF       24
#define GRAM_Y_OFF       0

/* Rev 1.0 boards were rescued by re-soldering the panel rotated 180 degrees
 * (the footprint's pad order was mirrored). MADCTL cancels that in the
 * controller. Set to 0 for corrected batch-2 boards.
 * NOTE: the GRAM offset above is only valid for THIS orientation. Flipping
 * MX/MY remaps the column/row addresses, and the controller's true RAM width
 * is unknown, so a rotated offset has to be re-measured. That is why
 * SET_ROTATION is deliberately not implemented in stage 1. */
/* 1 Oct 2026: set to 0 for rev 1.1 production boards. The panel is mounted
 * the right way round now, so no 180deg compensation is needed - confirmed
 * on the marketing demo, where MADCTL 0x20 (MV, no mirror bits) came out
 * right-side up and correctly positioned on a production unit. Portrait with
 * no mirror bits is 0x00, which is what the 0 branch selects. Set back to 1
 * only for the old reworked bench board. */
#define PANEL_ROTATED_REWORK  0
/* ── ORIENTATION ────────────────────────────────────────────────────────────
 * ALL FOUR BENCH-VERIFIED 1 Oct 2026 on rev 1.1 unit 03-8c-ab-cd-6b-95-bc-3c.
 * This replaces the old "the offsets are educated guesses" caveat - they are
 * measured now.
 *
 *   ROT_PORTRAIT           0   80x160   MADCTL 0x40  MX
 *   ROT_LANDSCAPE          1  160x80    MADCTL 0x20  MV            <- DEFAULT
 *   ROT_PORTRAIT_FLIPPED   2   80x160   MADCTL 0x80  MY
 *   ROT_LANDSCAPE_FLIPPED  3  160x80    MADCTL 0xE0  MV|MX|MY
 *
 * Portrait needs MX SET. MADCTL 0x00 renders text MIRRORED - a symmetric border
 * cannot reveal that, only text can. The reworked rev 1.0 board wanted 0xC0
 * (MX|MY); a correctly-mounted panel wants 0x40 (MX). They differ by MY alone,
 * which is exactly the 180 degrees of the rework's physical rotation - so the
 * old "un-rotated = 0x00" branch was always wrong: dropping the rotation
 * removes MY, it does not clear both bits.
 *
 * Offsets need NO per-rotation adjustment. (24,0) holds in both mirror states,
 * so 24 is symmetric and the effective RAM width is 24+80+24 = 128; landscape
 * is simply the swap, (0,24). The panel blurb quotes 132x162 - that is the
 * NV3022B maximum, NOT this panel's mapping. */
#define ROT_PORTRAIT           0
#define ROT_LANDSCAPE          1
#define ROT_PORTRAIT_FLIPPED   2
#define ROT_LANDSCAPE_FLIPPED  3

/* Boot orientation. A maker overrides it at runtime with SET_ROTATION (0x11);
 * this is only what the module comes up in before anyone says otherwise. */
#define DEFAULT_ROTATION       ROT_LANDSCAPE

#define COLMOD_VALUE     0x05

/* ── Which init runs ────────────────────────────────────────────────────────
 * 1 = EastRising's real sequence incl. the gamma table (fixes brand colours)
 * 0 = the minimal 5-command DCS init we shipped up to v0.6.0
 * Full rationale at gpu_run_vendor_init(). DEFINED HERE, EARLY, ON PURPOSE:
 * gpu_set_rotation() picks its MADCTL table with #if VENDOR_INIT, and an
 * undefined macro evaluates to 0 in #if with no warning - so defining this
 * further down silently selected the wrong table. */
#define VENDOR_INIT   1

/* Vendor COLMOD is 0x55 (full MIPI 16-bit encoding); the minimal init shipped
 * 0x05. Both work on this panel - kept separate so they stay testable. */
#define COLMOD_VENDOR 0x55

#define SPI_BAUD_DIV     (2u << 3)   /* PCLK/8 = 6 MHz — proven on a rework  */

#define BL_PWM_MAX       999
#define BL_DEFAULT       180         /* of 255, applied after enumeration    */

/* RGB565 */
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define NOKNOK  0x04BF               /* brand-ish blue for the splash        */

#define RX_BUF_SIZE  72              /* sized for stage-2 64-byte blit chunks */
#define TX_BUF_SIZE  16

/* ═══════════════════════════════════════════════════════════════════════════
 * STATE
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    DEV_BOOT_WAITING,
    DEV_ENUM_READY,
    DEV_ASSIGNING,
    DEV_ASSIGNED,
} DeviceState;

static volatile DeviceState dev_state = DEV_BOOT_WAITING;
static volatile uint32_t    ms_tick   = 0;
static volatile uint8_t     new_addr  = 0;
static volatile uint8_t     version_pending = 0;
static volatile uint8_t     info_pending    = 0;

static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_len    = 0;
static volatile uint8_t cmd_ready = 0;
/* DEV-44: the ISR snapshots a finished write into cmd_buf at STOP, so a later
 * address match or a spurious STOP can neither wipe nor re-run a command. */
static volatile uint8_t cmd_buf[RX_BUF_SIZE];
static volatile uint8_t cmd_len   = 0;
static volatile uint8_t addressed = 0;   /* 1 between an address match and its STOP */

static volatile uint8_t tx_buf[TX_BUF_SIZE];
static volatile uint8_t tx_len = 0;
static volatile uint8_t tx_idx = 0;

static volatile uint8_t gpu_busy  = 0;   /* 1 while an SPI draw is running   */
static volatile uint8_t last_err  = 0;

/* ── Live orientation state ──────────────────────────────────────────────
 * Geometry is RUNTIME state, not compile-time constants, because the mapping
 * between MADCTL and the GRAM offset cannot be calculated: flipping MX/MY
 * remaps column/row addresses against a controller RAM width we do not know.
 * It has to be measured. SET_ROTATION applies a best-guess table; SET_ORIENT
 * lets the bench set MADCTL and the offsets directly so the right combination
 * can be found live, the same way the original (24,0) offset was found. */
/* Seeded to the DEFAULT_ROTATION geometry (landscape) so these are never
 * momentarily inconsistent; gpu_init() re-applies it authoritatively. */
static uint8_t g_w    = PANEL_H;        /* 160 - landscape */
static uint8_t g_h    = PANEL_W;        /*  80             */
static uint8_t g_xoff = GRAM_Y_OFF;     /*   0             */
static uint8_t g_yoff = GRAM_X_OFF;     /*  24             */

/* ═══════════════════════════════════════════════════════════════════════════
 * CRC8  (poly 0x07, init 0x00) — identical to every other noknok module
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BACKOFF — FNV-1a over the 8-byte hardware UID
 *
 * CRITICAL: start h at the FNV offset basis (2166136261), do NOT XOR a seed
 * into it first — with seed == basis that yields h = 0 and every chip from the
 * same batch collapses to the same backoff. Cost the buzzer a v3.1 fix.
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint32_t backoff_ms;
static uint32_t enum_ready_start_ms = 0;

static uint32_t fnv_hash(uint32_t h)
{
    volatile uint8_t *uid = UID_ADDR;
    for (uint8_t i = 0; i < UID_LEN; i++) {
        h ^= uid[i];
        h *= 16777619UL;
    }
    return h;
}

static void calc_backoff(void)      { backoff_ms = (fnv_hash(2166136261UL) % 2500) + 300; }
static uint32_t calc_rebackoff_ms(void) { return ms_tick + (fnv_hash(ms_tick) % 500) + 50; }

/* ═══════════════════════════════════════════════════════════════════════════
 * I2C RESPONSES
 * ═══════════════════════════════════════════════════════════════════════════ */

static void build_uid_response(void)
{
    volatile uint8_t *uid = UID_ADDR;
    for (uint8_t i = 0; i < UID_LEN; i++) tx_buf[i] = uid[i];
    tx_buf[8] = MODULE_TYPE;
    tx_buf[9] = crc8((const uint8_t*)tx_buf, 9);
    tx_len = 10;
    tx_idx = 0;
}

static void build_version_response(void)
{
    tx_buf[0] = PROTOCOL_VERSION;
    tx_buf[1] = FW_VERSION_MAJOR;
    tx_buf[2] = FW_VERSION_MINOR;
    tx_buf[3] = FW_VERSION_PATCH;
    tx_len = 4;
    tx_idx = 0;
}

/* [width, height, colour_depth_code, n_fonts, n_icons]
 * depth_code 0x10 = RGB565. Width/height are the CURRENT orientation, so the
 * host sees 160x80 after a landscape SET_ROTATION without hard-coding anything. */
static void build_info_response(void)
{
    tx_buf[0] = g_w;
    tx_buf[1] = g_h;
    tx_buf[2] = 0x10;
    tx_buf[3] = 1;   /* one built-in font: 8x8 monospace */
    tx_buf[4] = 0;   /* icons are a later stage          */
    tx_len = 5;
    tx_idx = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SPI + NV3022B  — the "GPU". Framebuffer-less: everything goes straight into
 * the panel's GRAM. Hardware SPI only, never bit-banged.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ---- Board revision: D/C and RES are SWAPPED between revisions -----------
 * Bench-found 1 Oct 2026 on the first small-batch production unit.
 *
 *   rev 1.0 (reworked bench board)            PC3 = D/C,  PC4 = RES
 *   rev 1.1 (production, corrected footprint) PC3 = RES,  PC4 = D/C
 *
 * Verified against hardware/kicad/module-I2C-Display_1_42inch.kicad_pcb:
 * U1 pad 10 (PC3) -> Net-(DS1-RES), U1 pad 11 (PC4) -> Net-(DS1-RS/DC).
 * SPI is unaffected: PC5 -> R6 -> SCL/SCK, PC6 -> R7 -> SDA/MOSI, which is
 * what SPI1's fixed alternate function requires. CS is still tied to GND.
 *
 * SYMPTOM IF WRONG: lit backlight, MCU visibly alive, screen totally blank -
 * every command/data decision toggles the panel's RESET line while the reset
 * pulse goes to D/C. Looks exactly like a dead panel or a bad init. */
#define BOARD_REV_11  1

#if BOARD_REV_11
  #define PIN_RES   3
  #define PIN_DC    4
#else
  #define PIN_RES   4
  #define PIN_DC    3
#endif
#define DC_LOW()    (GPIOC->BCR  = (1u << PIN_DC))
#define DC_HIGH()   (GPIOC->BSHR = (1u << PIN_DC))
#define RES_LOW()   (GPIOC->BCR  = (1u << PIN_RES))
#define RES_HIGH()  (GPIOC->BSHR = (1u << PIN_RES))

static inline void spi_tx(uint8_t b)
{
    while (!(SPI1->STATR & SPI_STATR_TXE)) { }
    SPI1->DATAR = b;
}
static inline void spi_idle(void) { while (SPI1->STATR & SPI_STATR_BSY) { } }

static void lcd_cmd(uint8_t c)  { spi_idle(); DC_LOW(); spi_tx(c); spi_idle(); DC_HIGH(); }
static void lcd_dat(uint8_t d)  { spi_tx(d); }

/* The ONE place orientation is decided. Used by gpu_init() for the boot default
 * and by CMD_SET_ROTATION at runtime, so the two can never drift apart. */
static void gpu_set_rotation(uint8_t rot)
{
    /* The MADCTL set depends on WHICH INIT RAN, because the vendor sequence sets
     * scan direction itself with the vendor register 0xC3 = 0x11 (ss/gs). That
     * mirrors the column axis in hardware - exactly what MX does in software - so
     * with the vendor init every value is the minimal-init one XOR 0x40, which
     * lands on the canonical rotation set. Mixing them up mirrors everything. */
#if VENDOR_INIT
    /* BENCH-MEASURED 1 Oct 2026, not derived. My derivation (minimal-init value
     * XOR MX) produced unmirrored text but every entry 180 degrees from its name:
     * 0xC3 = 0x11 sets SS *and* GS, reversing source AND gate scan, so it is a
     * 180 degree flip on top of the mirror, not just a mirror. Measured mapping:
     *   0x00 -> portrait flipped   0x60 -> landscape flipped
     *   0xC0 -> portrait           0xA0 -> landscape
     * Parity is preserved, so the rot & 1 geometry test below still holds. */
    static const uint8_t rot_madctl[4] = {
        0xC0,        /* 0 portrait          MX | MY */
        0xA0,        /* 1 landscape         MV | MY */
        0x00,        /* 2 portrait flipped  -       */
        0x60         /* 3 landscape flipped MV | MX */
    };
#else
    /* Minimal init (no 0xC3), so MX has to supply the mirror in software. */
    static const uint8_t rot_madctl[4] = {
        0x40,        /* 0 portrait          MX           */
        0x20,        /* 1 landscape         MV           */
        0x80,        /* 2 portrait flipped  MY           */
        0xE0         /* 3 landscape flipped MV | MX | MY */
    };
#endif

    rot &= 0x03;
#if PANEL_ROTATED_REWORK
    /* Rescued rev 1.0 boards carry a physically 180-rotated panel, so add 180
     * to whatever was asked for. */
    rot = (uint8_t)((rot + 2) & 0x03);
#endif

    lcd_cmd(0x36); lcd_dat(rot_madctl[rot]);
    spi_idle();

    if (rot & 1u) { g_w = PANEL_H; g_h = PANEL_W;          /* landscape */
                    g_xoff = GRAM_Y_OFF; g_yoff = GRAM_X_OFF; }
    else          { g_w = PANEL_W; g_h = PANEL_H;          /* portrait  */
                    g_xoff = GRAM_X_OFF; g_yoff = GRAM_Y_OFF; }
}
/* ── VENDOR INIT (EastRising ER-TFT1.42-1 8051 tutorial) ────────────────────
 * 1 = run EastRising's real init, including the gamma table.
 * 0 = the minimal 5-command DCS init we shipped up to v0.6.0.
 *
 * WHY THIS EXISTS. Up to v0.6.0 we ran a minimal init with NO gamma table,
 * because a generic NV3022B table (Espressif's esp_lcd_nv3022b) gave a dead
 * screen on four boards during bring-up. Consequence: the controller ran its
 * power-on gamma, which maps mid-level codes to the wrong voltages, so brand
 * purple #8B00F3 (R 54.8%, B 96.8%) rendered BLUE - the red was flattened out
 * while the near-rail blue sailed through.
 *
 * WHY THE GENERIC TABLE KILLED THE PANEL. The vendor registers sit behind a
 * locked page. EastRising unlocks with 0xFF,0xA5 and closes with 0xFF,0x00;
 * the Espressif table used 0xFD,0x06,0x08. Wrong command, wrong key - so every
 * vendor write hit a locked page. Worse, the gamma block is written through
 * 0x2A/0x2B/0x2C, which OUTSIDE the unlocked page are CASET/RASET/RAMWR: doing
 * that blind sets a nonsense address window and dumps 64 bytes of gamma data
 * into GRAM. A dead screen is exactly what you would predict.
 *
 * WHAT THE GAMMA BLOCK IS. Not a file - a sequence of SPI register writes done
 * once at power-up. It programs the voltage-vs-grey-level curve: VRP/VRN end
 * references, VPMIDP/VPMIDN mid-points, and KP0..KP7 / KN0..KN7 knee points.
 * Positive and negative sets exist because LCD pixels are driven with
 * alternating polarity to avoid DC stress, and each polarity needs its own
 * curve. 0x87/0x88/0x89 set the GVDD (4.7 V) and VCOMH/VCOML rails it rides on.
 *
 * MACHINE-EXTRACTED from NV3022.h : NV3022_Initial(), not retyped - the gamma
 * block alone is 64 bytes and a transcription slip there is unfindable by eye.
 *
 * The table stops after SLPOUT (0x11). COLMOD, MADCTL and DISPON are then done
 * in code so gpu_set_rotation() stays the single authority on orientation.
 * NOTE the vendor sets scan direction with the VENDOR register 0xC3 = 0x11
 * (ss/gs) and leaves MADCTL at 0x00 - so with this init the right MADCTL may
 * be 0x00 rather than the 0x40 the minimal init needs. Test, do not assume. */
/* VENDOR_INIT and COLMOD_VENDOR are defined in the config block near the top
 * of the file - they MUST precede gpu_set_rotation(), whose MADCTL table is
 * selected by #if VENDOR_INIT. An undefined macro silently evaluates to 0 in
 * #if, with no warning, so a late define quietly selects the wrong table. */


#if VENDOR_INIT
static const uint8_t vendor_init_seq[] = {
    0xFF, 1, 0xA5,
    0x3A, 1, 0x65,
    0x51, 1, 0x14,
    0x53, 1, 0x11,
    0x62, 1, 0x10,
    0x86, 1, 0x01,
    0x87, 1, 0x1E,
    0x88, 1, 0x3E,
    0x89, 1, 0x3C,
    0x61, 1, 0x10,
    0x93, 1, 0x12,
    0x94, 1, 0x10,
    0x95, 1, 0x10,
    0x96, 1, 0x0E,
    0xB2, 1, 0x0F,
    0xB4, 1, 0x60,
    0x91, 1, 0x10,
    0xC1, 1, 0xF1,
    0xC5, 1, 0xF8,
    0xB5, 1, 0x00,
    0xC3, 1, 0x11,
    0x83, 1, 0x13,
    0x2A, 4, 0x00, 0x84, 0x00, 0x84,
    0x2B, 4, 0x00, 0x00, 0x00, 0x1F,
    0x2C, 64,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x1C, 0x00, 0x16, 0x00, 0x1E,
        0x00, 0x02, 0x00, 0x08, 0x00, 0x09, 0x00, 0x10,
        0x00, 0x1F, 0x00, 0x19, 0x00, 0x1C, 0x00, 0x1E,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x08,
        0x00, 0x01, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00,
        0x00, 0x0F, 0x00, 0x10, 0x00, 0x16, 0x00, 0x1D,
    0xFF, 1, 0x00,
    0x21, 0,
    0x11, 0,
    0x00                           /* terminator (0x00 = NOP, unused) */
};

/* Walk {cmd, count, data...} until the 0x00 terminator. */
static void gpu_run_vendor_init(void)
{
    const uint8_t *p = vendor_init_seq;

    while (*p) {
        uint8_t cmd = *p++;
        uint8_t n   = *p++;
        lcd_cmd(cmd);
        while (n--) lcd_dat(*p++);
        spi_idle();
    }
}
#endif
static void gpu_init(void)
{
    /* PC3 + PC4 = push-pull out (which is D/C and which is RES depends on
     * BOARD_REV_11); PC5 SCK + PC6 MOSI = AF push-pull */
    GPIOC->CFGLR &= ~(0xFu << (4 * 3) | 0xFu << (4 * 4)
                    | 0xFu << (4 * 5) | 0xFu << (4 * 6));
    GPIOC->CFGLR |=  (0x1u << (4 * 3)) | (0x1u << (4 * 4))
                  |  (0xBu << (4 * 5)) | (0xBu << (4 * 6));

    SPI1->CTLR1 = (1u << 9)     /* SSM  — CS is hard-wired to GND on the board */
                | (1u << 8)     /* SSI                                          */
                | SPI_BAUD_DIV
                | (1u << 2);    /* MSTR                                         */
    SPI1->CTLR2 = 0;
    SPI1->CTLR1 |= (1u << 6);   /* SPE                                          */

    RES_LOW();  Delay_Ms(50);
    RES_HIGH(); Delay_Ms(150);

    #if VENDOR_INIT
    /* EastRising's real sequence: unlock, power/VCOM, gamma, lock, INVON,
     * SLPOUT. No SWRESET - the vendor relies on the hardware reset above. */
    gpu_run_vendor_init();
    Delay_Ms(120);                              /* vendor waits 120 ms here */
    lcd_cmd(0x3A); lcd_dat(COLMOD_VENDOR);
    gpu_set_rotation(DEFAULT_ROTATION);         /* MADCTL + geometry + offsets */
    lcd_cmd(0x29);                              /* DISPON */
    Delay_Ms(10);
#else
    /* Minimal DCS init - what shipped up to v0.6.0. No gamma table, so brand
     * colours render wrong (purple comes out blue). Kept as the fallback. */
    lcd_cmd(0x01);              /* SWRESET */
    Delay_Ms(150);
    lcd_cmd(0x11);              /* SLPOUT  */
    Delay_Ms(200);
    lcd_cmd(0x3A); lcd_dat(COLMOD_VALUE);
    gpu_set_rotation(DEFAULT_ROTATION);   /* MADCTL + geometry + offsets */
    lcd_cmd(0x21);              /* INVON — mandatory on this IPS cell */
    lcd_cmd(0x29);              /* DISPON */
    Delay_Ms(20);
#endif
}

/* Open a GRAM window and leave the controller expecting pixels. */
static void gpu_window(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    /* RUNTIME offsets, not the compile-time constants. Using the constants here
     * was a real bug: SET_ROTATION and SET_ORIENT both write g_xoff/g_yoff, so
     * with the constants hard-wired they changed MADCTL and the geometry but
     * silently ignored the offsets - the rotation API was half-broken. */
    uint16_t x0 = x + g_xoff, x1 = x0 + w - 1;
    uint16_t y0 = y + g_yoff, y1 = y0 + h - 1;

    lcd_cmd(0x2A);
    lcd_dat(x0 >> 8); lcd_dat(x0 & 0xFF);
    lcd_dat(x1 >> 8); lcd_dat(x1 & 0xFF);

    lcd_cmd(0x2B);
    lcd_dat(y0 >> 8); lcd_dat(y0 & 0xFF);
    lcd_dat(y1 >> 8); lcd_dat(y1 & 0xFF);

    lcd_cmd(0x2C);
}

/* Clip to the panel, then fill. Clipping matters: a bad x/y/w/h from the host
 * must never walk the GRAM pointer outside the window. */
static void gpu_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c)
{
    uint32_t n;
    uint8_t  hi = c >> 8, lo = c & 0xFF;

    if (x >= g_w || y >= g_h || w == 0 || h == 0) { last_err = 3; return; }
    if (x + w > g_w) w = (uint8_t)(g_w - x);
    if (y + h > g_h) h = (uint8_t)(g_h - y);

    gpu_busy = 1;
    gpu_window(x, y, w, h);
    n = (uint32_t)w * h;
    while (n--) { spi_tx(hi); spi_tx(lo); }
    spi_idle();
    gpu_busy = 0;
}

static void gpu_clear(uint16_t c) { gpu_fill_rect(0, 0, g_w, g_h, c); }

/* ── Text ────────────────────────────────────────────────────────────────
 * Monospace 8x8 base font scaled by an integer 1-4, giving native heights
 * 8/16/24/32. Anything else (arbitrary sizes, nicer typefaces) is rendered on
 * the Pico and sent through the blit path instead — that is the agreed split.
 *
 * The font is LSB-first: bit 0 of each row byte is the LEFTMOST pixel.
 *
 * Opaque draw streams a whole scaled glyph cell in one window, which is fast.
 * Transparent draw has to place each set pixel individually, because with no
 * local framebuffer we cannot read back what is underneath — so it is much
 * slower and is meant for small accents, not paragraphs.
 */
static void gpu_draw_glyph(uint8_t x, uint8_t y, uint8_t ch, uint8_t scale,
                           uint16_t fg, uint16_t bg, uint8_t transparent)
{
    const uint8_t *rows;
    uint8_t cell = (uint8_t)(FONT8X8_H * scale);
    uint8_t fh = fg >> 8, fl = fg & 0xFF;
    uint8_t bh = bg >> 8, bl = bg & 0xFF;

    if (ch < FONT8X8_FIRST || ch > FONT8X8_LAST) ch = '?';
    rows = font8x8[ch - FONT8X8_FIRST];

    if (x >= g_w || y >= g_h) return;

    if (transparent) {
        for (uint8_t r = 0; r < FONT8X8_H; r++) {
            uint8_t bits = rows[r];
            for (uint8_t c = 0; c < FONT8X8_W; c++) {
                if (bits & (1u << c))          /* LSB = leftmost */
                    gpu_fill_rect((uint8_t)(x + c * scale),
                                  (uint8_t)(y + r * scale), scale, scale, fg);
            }
        }
        return;
    }

    /* Opaque: one window for the whole cell, pixels streamed row by row. */
    if (x + cell > g_w || y + cell > g_h) {
        /* Would overflow the panel — fall back to the clipped per-pixel path
         * so a glyph at the edge degrades instead of corrupting the window. */
        for (uint8_t r = 0; r < FONT8X8_H; r++) {
            uint8_t bits = rows[r];
            for (uint8_t c = 0; c < FONT8X8_W; c++)
                gpu_fill_rect((uint8_t)(x + c * scale), (uint8_t)(y + r * scale),
                              scale, scale, (bits & (1u << c)) ? fg : bg);
        }
        return;
    }

    gpu_busy = 1;
    gpu_window(x, y, cell, cell);
    for (uint8_t r = 0; r < FONT8X8_H; r++) {
        uint8_t bits = rows[r];
        for (uint8_t sy = 0; sy < scale; sy++) {
            for (uint8_t c = 0; c < FONT8X8_W; c++) {
                uint8_t on = (bits >> c) & 1u;
                for (uint8_t sx = 0; sx < scale; sx++) {
                    spi_tx(on ? fh : bh);
                    spi_tx(on ? fl : bl);
                }
            }
        }
    }
    spi_idle();
    gpu_busy = 0;
}

/* ── 1bpp blit (host-rendered assets) ────────────────────────────────────
 * BLIT_BEGIN opens a window and latches the colours; BLIT_DATA then streams
 * 1bpp rows in chunks of up to 64 bytes. Kept as separate transactions because
 * the CH32V003 has 2 KB of RAM and cannot buffer a whole bitmap — each chunk is
 * expanded to RGB565 and pushed to the panel as it arrives.
 *
 * We do NOT need a flow-control protocol: at 100 kHz an I2C byte takes ~90 us
 * while expanding it to 8 pixels over SPI at 6 MHz takes ~21 us, so the module
 * always drains faster than the host delivers. */
static uint16_t blit_fg = 0xFFFF, blit_bg = 0x0000;
static uint8_t  blit_transparent = 0;
static uint32_t blit_pixels_left = 0;
static uint8_t  blit_row_bytes = 0, blit_row_pos = 0;
static uint8_t  blit_w = 0;
static uint8_t  blit_x0 = 0, blit_y0 = 0, blit_row = 0;

static void gpu_blit_begin(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                           uint16_t fg, uint16_t bg, uint8_t flags)
{
    if (x >= g_w || y >= g_h || w == 0 || h == 0
        || x + w > g_w || y + h > g_h) { last_err = 3; blit_pixels_left = 0; return; }

    blit_fg = fg; blit_bg = bg;
    blit_transparent = flags & 1u;
    blit_w = w;
    blit_row_bytes = (uint8_t)((w + 7) / 8);
    blit_row_pos = 0;
    blit_row = 0;
    blit_x0 = x; blit_y0 = y;
    blit_pixels_left = (uint32_t)w * h;

    /* Opaque blits stream into one window. A transparent blit cannot: with no
     * local framebuffer we cannot read back the background, so each set pixel
     * has to be placed individually (see gpu_blit_data). */
    if (!blit_transparent) gpu_window(x, y, w, h);
}

static void gpu_blit_data(const uint8_t *data, uint8_t n)
{
    if (blit_pixels_left == 0) { last_err = 4; return; }   /* no blit in progress */

    gpu_busy = 1;
    for (uint8_t i = 0; i < n && blit_pixels_left; i++) {
        uint8_t bits = data[i];
        /* Bits are MSB-first within each byte, rows padded to a byte boundary —
         * the natural output of a host-side renderer. */
        for (int8_t b = 7; b >= 0 && blit_pixels_left; b--) {
            /* Skip the padding bits at the end of each row. */
            if (blit_row_pos >= blit_w) break;
            uint8_t on = (bits >> b) & 1u;
            if (blit_transparent) {
                if (on) gpu_fill_rect((uint8_t)(blit_x0 + blit_row_pos),
                                      (uint8_t)(blit_y0 + blit_row), 1, 1, blit_fg);
            } else {
                uint16_t c = on ? blit_fg : blit_bg;
                spi_tx(c >> 8); spi_tx(c & 0xFF);
            }
            blit_row_pos++;
            blit_pixels_left--;
        }
        if (blit_row_pos >= blit_w) { blit_row_pos = 0; blit_row++; }   /* next row */
    }
    spi_idle();
    gpu_busy = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BACKLIGHT — TIM1_CH1 on PD2. Ramped, never snapped: the MT3540 boost taps
 * the SHARED 3.3V bus rail ahead of the module's 10R, so a cold inrush here is
 * felt by every other module on the daisy chain.
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint16_t bl_current = 0;

static void backlight_init(void)
{
    GPIOD->CFGLR &= ~(0xFu << (4 * 2));
    GPIOD->CFGLR |=  (0xBu << (4 * 2));

    TIM1->PSC     = 0;
    TIM1->ATRLR   = BL_PWM_MAX;          /* 48 kHz — no visible flicker */
    TIM1->CH1CVR  = 0;
    TIM1->CHCTLR1 = (6u << 4) | (1u << 3);   /* PWM mode 1 + preload */
    TIM1->CCER    = (1u << 0);
    TIM1->BDTR   |= (1u << 15);          /* MOE — advanced timer, mandatory */
    TIM1->SWEVGR  = (1u << 0);
    TIM1->CTLR1  |= (1u << 0);
}

static void backlight_set_raw(uint16_t duty)
{
    bl_current = (duty > BL_PWM_MAX) ? BL_PWM_MAX : duty;
    TIM1->CH1CVR = bl_current;
}

/* level 0-255 from the host, scaled to the PWM range */
static void backlight_set(uint8_t level)
{
    backlight_set_raw(((uint32_t)level * BL_PWM_MAX) / 255u);
}

static void backlight_ramp_to(uint16_t target, uint16_t ms)
{
    int32_t from = bl_current, delta = (int32_t)target - from;
    for (int32_t i = 0; i <= 20; i++) {
        backlight_set_raw((uint16_t)(from + delta * i / 20));
        iwdg_kick();      /* a ramp may legitimately run for a while */
        Delay_Ms(ms / 20);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SPLASH — shown while the enumeration backoff runs, so a powered module
 * visibly proves itself alive before the Conductor has even reached it.
 * Placeholder geometry; the real 1bpp noknok mark lands with stage 2 assets.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_splash(void)
{
    /* Centred on g_w/g_h, NOT hard-coded portrait coordinates. The old version
     * drew at y=60..98, which falls off the bottom of an 80px-tall landscape
     * panel - it only ever worked because portrait used to be the default. */
    uint8_t bw = (uint8_t)(g_w * 2 / 3);         /* bar width  */
    uint8_t x  = (uint8_t)((g_w - bw) / 2);
    uint8_t y  = (uint8_t)(g_h / 2 - 13);

    gpu_clear(BLACK);
    gpu_fill_rect(x, y,                 bw,              12, NOKNOK);
    gpu_fill_rect(x, (uint8_t)(y + 15), bw,               5, WHITE);
    gpu_fill_rect(x, (uint8_t)(y + 23), (uint8_t)(bw/2),  5, NOKNOK);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TIM2 — 1 ms tick
 * ═══════════════════════════════════════════════════════════════════════════ */

static void tim2_init(void)
{
    RCC->APB1PCENR  |= RCC_APB1Periph_TIM2;
    TIM2->PSC        = 47;
    TIM2->ATRLR      = 999;
    TIM2->DMAINTENR  = TIM_IT_Update;
    TIM2->SWEVGR     = TIM_UG;
    TIM2->CTLR1     |= TIM_CEN;
    NVIC_EnableIRQ(TIM2_IRQn);
}

void TIM2_IRQHandler(void) __attribute__((interrupt));
void TIM2_IRQHandler(void) { TIM2->INTFR = 0; ms_tick++; }

/* ═══════════════════════════════════════════════════════════════════════════
 * I2C SLAVE
 * ═══════════════════════════════════════════════════════════════════════════ */

static void i2c_slave_init(uint8_t addr)
{
    RCC->APB1PCENR |= RCC_APB1Periph_I2C1;

    /* PC1 = SDA, PC2 = SCL: open-drain alternate function */
    GPIOC->CFGLR &= ~(0xF << (1 * 4)); GPIOC->CFGLR |= (0xF << (1 * 4));
    GPIOC->CFGLR &= ~(0xF << (2 * 4)); GPIOC->CFGLR |= (0xF << (2 * 4));

    I2C1->CTLR1 |=  I2C_CTLR1_SWRST;
    I2C1->CTLR1 &= ~I2C_CTLR1_SWRST;

    I2C1->CTLR2  = 48;     /* APB1 = 48 MHz  */
    I2C1->CKCFGR = 240;    /* 100 kHz        */
    I2C1->OADDR1 = ((uint16_t)addr << 1);

    I2C1->CTLR2 |= I2C_CTLR2_ITEVTEN | I2C_CTLR2_ITBUFEN | I2C_CTLR2_ITERREN;
    I2C1->CTLR1 |= I2C_CTLR1_ACK | I2C_CTLR1_PE;

    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);
}

static void i2c_switch_addr(uint8_t addr)
{
    I2C1->CTLR1 &= ~I2C_CTLR1_PE;
    I2C1->OADDR1 = ((uint16_t)addr << 1);
    I2C1->CTLR1 |= I2C_CTLR1_ACK | I2C_CTLR1_PE;
}

void I2C1_EV_IRQHandler(void) __attribute__((interrupt));
void I2C1_EV_IRQHandler(void)
{
    uint32_t star1 = I2C1->STAR1;
    uint32_t star2 = I2C1->STAR2;   /* reading STAR2 clears ADDR */

    if (star1 & I2C_STAR1_ADDR) {
        rx_len = 0;
        addressed = 1;
        I2C1->CTLR1 |= I2C_CTLR1_ACK;

        if (star2 & I2C_STAR2_TRA) {
            if (dev_state == DEV_ENUM_READY) {
                build_uid_response();
            } else if (version_pending) {
                version_pending = 0;
                build_version_response();
            } else if (info_pending) {
                info_pending = 0;
                build_info_response();
            } else {
                tx_buf[0] = gpu_busy ? 0x01 : 0x00;
                tx_buf[1] = last_err;
                last_err = 0;            /* DEV-44: read-to-clear */
                tx_len = 2; tx_idx = 0;
            }
            I2C1->DATAR = tx_buf[tx_idx++];
        }
        return;
    }

    if (star1 & I2C_STAR1_RXNE) {
        uint8_t b = (uint8_t)I2C1->DATAR;
        if (rx_len < RX_BUF_SIZE) rx_buf[rx_len++] = b;
        return;
    }

    if (star1 & I2C_STAR1_TXE) {
        I2C1->DATAR = (tx_idx < tx_len) ? tx_buf[tx_idx++] : 0x00;
        return;
    }

    if (star1 & I2C_STAR1_STOPF) {
        I2C1->CTLR1 |= I2C_CTLR1_PE;   /* clears STOPF */

        if (dev_state == DEV_ENUM_READY) {
            if (rx_len == 2 && rx_buf[0] == REG_ASSIGN_ADDR) {
                new_addr  = rx_buf[1];
                dev_state = DEV_ASSIGNING;
            }
        } else if (dev_state == DEV_ASSIGNED) {
            /* Latch the zero-argument query commands here in the ISR, exactly
             * where the 0x1D assign is handled — routing them via the main loop
             * lets the master's very next read race ahead of the response. */
            if (rx_len == 1 && rx_buf[0] == CMD_GET_VERSION)
                version_pending = 1;
            else if (rx_len == 1 && rx_buf[0] == CMD_GET_INFO)
                info_pending = 1;
            else if (addressed && rx_len > 0) {
                /* DEV-44: snapshot here; a STOP with no address match (bus
                 * glitch, host i2c.deinit()) must not re-run the old command. */
                for (uint8_t i = 0; i < rx_len; i++) cmd_buf[i] = rx_buf[i];
                cmd_len   = rx_len;
                cmd_ready = 1;
            }
        }
        rx_len    = 0;
        addressed = 0;

        I2C1->CTLR1 |= I2C_CTLR1_ACK;
        return;
    }
}

void I2C1_ER_IRQHandler(void) __attribute__((interrupt));
void I2C1_ER_IRQHandler(void)
{
    I2C1->STAR1 &= ~(I2C_STAR1_BERR | I2C_STAR1_ARLO |
                     I2C_STAR1_AF   | I2C_STAR1_OVR);
    I2C1->CTLR1 |= I2C_CTLR1_ACK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * COMMAND PROCESSOR
 * ═══════════════════════════════════════════════════════════════════════════ */

static void process_command(void)
{
    /* Snapshot first. A full-screen fill is ~35 ms of SPI, and the I2C ISR can
     * land the next command in rx_buf while we are still drawing from it. The
     * other noknok modules do not need this because their commands are instant. */
    uint8_t cmd[RX_BUF_SIZE];
    uint8_t n;
    __disable_irq();   /* the ISR must not rewrite cmd_buf mid-copy */
    n = cmd_len;
    if (n > RX_BUF_SIZE) n = RX_BUF_SIZE;
    for (uint8_t i = 0; i < n; i++) cmd[i] = cmd_buf[i];
    cmd_len = 0;
    __enable_irq();
    if (n == 0) return;

    switch (cmd[0]) {
    case CMD_CLEAR:
        if (n >= 3) gpu_clear(((uint16_t)cmd[1] << 8) | cmd[2]);
        else last_err = 1;
        break;

    case CMD_FILL_RECT:
        if (n >= 7) gpu_fill_rect(cmd[1], cmd[2], cmd[3], cmd[4],
                                  ((uint16_t)cmd[5] << 8) | cmd[6]);
        else last_err = 1;
        break;

    case CMD_DRAW_TEXT:
        /* [0x03, x, y, style, fgHi, fgLo, bgHi, bgLo, ascii...]
         * style: bits 0-2 = scale (1-4), bit 3 = transparent background,
         *        bits 4-5 = font index (only 0 = 8x8 exists today). */
        if (n >= 9) {
            uint8_t  x     = cmd[1], y = cmd[2];
            uint8_t  scale = (uint8_t)(((cmd[3] >> 2) & 0x07) + 1);
            uint8_t  trans = (cmd[3] >> 7) & 1u;
            uint16_t fg    = ((uint16_t)cmd[4] << 8) | cmd[5];
            uint16_t bg    = ((uint16_t)cmd[6] << 8) | cmd[7];
            /* font index = cmd[3] bits 0-1; only 0 (8x8) exists today */

            if (scale > 8) scale = 8;   /* scale 1-8 -> heights 8..64 */

            for (uint8_t i = 8; i < n; i++) {
                uint8_t cx = (uint8_t)(x + (i - 8) * FONT8X8_W * scale);
                if (cx >= g_w) break;              /* ran off the edge */
                gpu_draw_glyph(cx, y, cmd[i], scale, fg, bg, trans);
            }
        } else last_err = 1;
        break;

    case CMD_BLIT_BEGIN:
        /* [0x05, x, y, w, h, fgHi, fgLo, bgHi, bgLo, flags] */
        if (n >= 10)
            gpu_blit_begin(cmd[1], cmd[2], cmd[3], cmd[4],
                           ((uint16_t)cmd[5] << 8) | cmd[6],
                           ((uint16_t)cmd[7] << 8) | cmd[8], cmd[9]);
        else last_err = 1;
        break;

    case CMD_BLIT_DATA:
        /* [0x06, <=64 bytes of 1bpp rows] */
        if (n >= 2) gpu_blit_data((const uint8_t *)&cmd[1], (uint8_t)(n - 1));
        else last_err = 1;
        break;

    case CMD_SET_ROTATION:
        /* [0x11, rot 0-3]. All four orientations are bench-verified (1 Oct
         * 2026) - see the ORIENTATION block near the top for the MADCTL values
         * and why the offsets need no per-rotation adjustment. The work is done
         * by gpu_set_rotation(), the same helper gpu_init() uses for the boot
         * default, so runtime and boot can never disagree.
         *
         *   0 portrait   1 landscape (default)   2 portrait flipped
         *   3 landscape flipped
         *
         * SET_ORIENT (0x14) still overrides MADCTL and offsets directly for
         * bench calibration. */
        if (n >= 2) gpu_set_rotation(cmd[1]);
        else        last_err = 1;
        break;

    case CMD_SET_ORIENT:
        /* [0x14, madctl, xoff, yoff, w, h] — raw bench calibration.
         * Sets MADCTL and the geometry directly so the right combination can be
         * found live on the bench, exactly how the original (24,0) offset was
         * found. Not meant for product code; SET_ROTATION is the real API. */
        if (n >= 6) {
            lcd_cmd(0x36); lcd_dat(cmd[1]);
            spi_idle();
            g_xoff = cmd[2];
            g_yoff = cmd[3];
            g_w    = cmd[4] ? cmd[4] : 1;
            g_h    = cmd[5] ? cmd[5] : 1;
        } else last_err = 1;
        break;

    case CMD_SET_BACKLIGHT:
        if (n >= 2) backlight_set(cmd[1]);
        else last_err = 1;
        break;

    case CMD_DISPLAY:
        if (n >= 2) {
            if (cmd[1] == 0)      { lcd_cmd(0x28); backlight_set_raw(0); }
            else if (cmd[1] == 1) { lcd_cmd(0x29); backlight_set(BL_DEFAULT); }
            else                  { lcd_cmd(0x10); backlight_set_raw(0); }  /* sleep */
        } else last_err = 1;
        break;

    case CMD_ENTER_BOOTLOADER:
        /* Arm the handoff magic in no-init RAM and warm-reset. SRAM survives a
         * warm reset, so the bootloader sees the magic and stays at 0x7E. */
        BL_MAGIC_CELL = BL_MAGIC_ENTER;
        NVIC_SystemReset();
        break;   /* not reached */

    default:
        last_err = 2;   /* unknown command */
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    SystemInit();
    iwdg_init();          /* hardening C: hang -> warm reset -> stage-1 counts it */

    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD
                    | RCC_APB2Periph_SPI1  | RCC_APB2Periph_TIM1
                    | RCC_APB2Periph_AFIO;

    backlight_init();
    gpu_init();
    tim2_init();
    calc_backoff();

    __enable_irq();

    /* Alive before the Conductor has even scanned the bus. */
    draw_splash();
    backlight_ramp_to(((uint32_t)BL_DEFAULT * BL_PWM_MAX) / 255u, 200);

    while (1)
    {
        uint32_t now = ms_tick;
        iwdg_kick();          /* every loop iteration; nothing here blocks > 2 s */

        if (dev_state == DEV_BOOT_WAITING && now >= backoff_ms) {
            enum_ready_start_ms = now;
            i2c_slave_init(ENUM_ADDR);
            dev_state = DEV_ENUM_READY;
        }

        /* Safety net: re-backoff if the Conductor has not assigned us in 200 ms.
         * The short re-backoff range keeps us inside its 3000 ms timeout. */
        if (dev_state == DEV_ENUM_READY && (now - enum_ready_start_ms) > 200) {
            I2C1->CTLR1 &= ~I2C_CTLR1_PE;
            backoff_ms  = calc_rebackoff_ms();
            dev_state   = DEV_BOOT_WAITING;
        }

        if (dev_state == DEV_ASSIGNING) {
            i2c_switch_addr(new_addr);
            dev_state = DEV_ASSIGNED;
            BOOT_ATTEMPT_CELL = 0;   /* healthy: I2C works, enumeration done */
            /* Clear the splash — the module now belongs to a product. */
            gpu_clear(BLACK);
        }

        if (dev_state == DEV_ASSIGNED && cmd_ready) {
            cmd_ready = 0;
            process_command();
        }
    }
}
