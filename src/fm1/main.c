/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The FM-1 build: the core (src/core) on the hardware layer of felucca-bsp (MPL-2.0, bsp/).
 *
 *   audio   ALNK0 I2S, 44.1 kHz, halves of 128 frames: od_audio in the ISR, Q15 -> the codec's 24 bits
 *   input   TIMER5 at 10 kHz scans keys, buttons and encoders (BSP); the audio ISR hands the changes to the core
 *   screen  od_frame draws 240x240 RGB565 into RAM; only the rows that changed go to the panel, byte-swapped
 *   USB     MIDI in (a keyboard or a DAW) and the installer's update entry: a new package can always replace this */
#include <stdint.h>
#define FELUCCA_CDC 0
#define FELUCCA_OTA 1
#ifndef OD_PRODUCT
#define OD_PRODUCT "FM-1_910"                   /* the identity the installer sees, == the package's */
#endif
#define FELUCCA_ID OD_PRODUCT
#define FELUCCA_USB_PRODUCT "FM-1 OndaDream"           /* the USB name: "FM-1" in it, as every installer looks for */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_timer.h"
#include "fm1_input.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_flash.h"
#include "fm1_audio.h"
#include "libc.c"
#include "usb.c"
#include "ota.c"
#include "fm1_glue.c"

void *memmove(void *d, const void *s, unsigned n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    if (a < b)
        while (n--) *a++ = *b++;
    else
        while (n--) a[n] = b[n];
    return d;
}

/* the core reads the clock the tick keeps; its big buffers go to POOL (app.ld) */
#define OD_BIG __attribute__((section(".pool")))
uint32_t od_ms(void) { return fm1_ms; }
#include "od.c"

/* ---- the panel: which matrix id carries which printed label (measured on the hardware) ---- */
static const uint8_t BTN_ID[OB_N] = {
    2, 3, 4, 5, 6, 7,                           /* FX SCL ENV LFO EDIT GLO */
    8, 9, 10, 11, 12, 13,                       /* HOME SAVE ARP SEQ PLAY REC */
    0, 1,                                       /* OCT- OCT+ */
};
static const uint8_t ENC_ID[OE_N] = { 0, 1, 6, 2, 3, 4, 5 };   /* SELECT ALGORITHM PRESETS KNOB1..4 */

/* ---- the panel and MIDI to the core: in the audio ISR, before each half, so a key plays within 3 ms whatever the
 * screen is doing (the main loop draws for tens of ms) ---- */
static uint32_t notes_seen, btn_seen;
static void midi_drain(void)
{
    if (midi_in_overflow) {                     /* a broken stream: drop it, release the notes */
        mi_r = mi_w;
        midi_in_overflow = 0;
        od_midi(0xB0, 123, 0);
        return;
    }
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ];
        mi_r++;
        od_midi((uint8_t)(pkt >> 8), (uint8_t)((pkt >> 16) & 0x7Fu), (uint8_t)((pkt >> 24) & 0x7Fu));
    }
}
static void panel_take(void)
{
    uint32_t i, d, n = fm1_in.notes, b = fm1_in.buttons;
    if ((d = n ^ notes_seen) != 0u)
        for (i = 0; i < 27u; i++)
            if (d & (1u << i))
                od_key(i, (int)((n >> i) & 1u));
    notes_seen = n;
    if ((d = b ^ btn_seen) != 0u)
        for (i = 0; i < OB_N; i++)
            if (d & (1u << BTN_ID[i]))
                od_button(i, (int)((b >> BTN_ID[i]) & 1u));
    btn_seen = b;
    for (i = 0; i < OE_N; i++) {
        int32_t st = fm1_enc_take(ENC_ID[i]);
        if (st)
            od_enc(i, st);
    }
    midi_drain();
}

/* ---- audio ---- */
#define HALF_FRAMES 128u
static int32_t abuf[2u * HALF_FRAMES * 2u];
static volatile int32_t master_pot = -1;        /* the main loop reads the pot, the ISR hands it on */
static volatile uint32_t audio_ticks;           /* TIMER4 ticks spent rendering (the GLOBAL page's CPU) */
void fm1_alnk0_irq(void)
{
    uint8_t p;
    in_audio = 1;                               /* first: the tick nests from here on */
    p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        int32_t *o = &abuf[fm1_audio_free_half() * HALF_FRAMES * 2u];
        uint32_t i, t0 = fm1_ticks();
        panel_take();
        if (master_pot >= 0) {
            od_master((uint32_t)master_pot);
            master_pot = -1;
        }
        od_audio(o, HALF_FRAMES);
        for (i = 0; i < 2u * HALF_FRAMES; i++)
            o[i] *= 1 << 7;                     /* Q15 -> 24 bits, -6 dBFS ceiling */
        fm1_audio_ack_half();
        audio_ticks += fm1_ticks() - t0;
    }
    in_audio = 0;
}
extern void isr_alnk0(void);

static void ota_quiet(void) { fm1_audio_stop(); }

/* ---- the screen: rows that changed, swapped to high byte first, two row buffers so the swap overlaps the DMA ---- */
static uint16_t fbuf[240u * 240u] OD_BIG;      /* (drawn here, sent through rowbuf: the DMA reads .bss) */
static uint32_t row_sum[240];
static uint16_t rowbuf[2][240];

static uint32_t row_hash(const uint16_t *r)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < 240u; i += 2u)
        h = (h ^ ((uint32_t)r[i] | (uint32_t)r[i + 1u] << 16)) * 16777619u;
    return h;
}

static void row_swap(uint16_t *d, const uint16_t *s)
{
    uint32_t i;
    for (i = 0; i < 240u; i++)
        d[i] = (uint16_t)((s[i] >> 8) | (s[i] << 8));
}

static void lcd_rows(uint32_t y0, uint32_t y1)  /* rows y0 .. y1 - 1 */
{
    uint32_t y, b = 0;
    lcd_window(0, y0, 240, y1 - y0);
    row_swap(rowbuf[0], &fbuf[y0 * 240u]);
    for (y = y0; y < y1; y++) {
        fm1_lcd_send_data(rowbuf[b], 480);
        if (y + 1u < y1)
            row_swap(rowbuf[b ^ 1u], &fbuf[(y + 1u) * 240u]);
        fm1_lcd_wait();
        fm1_lcd_deselect();
        fm1_wdt_feed();
        b ^= 1u;
    }
}

static void lcd_flush(int all)
{
    uint32_t y = 0;
    while (y < 240u) {
        uint32_t h = row_hash(&fbuf[y * 240u]), y0;
        if (!all && h == row_sum[y]) {
            y++;
            continue;
        }
        y0 = y;
        do {                                    /* a run of changed rows: one window */
            row_sum[y] = h;
            y++;
            if (y < 240u)
                h = row_hash(&fbuf[y * 240u]);
        } while (y < 240u && (all || h != row_sum[y]));
        lcd_rows(y0, y);
    }
}

static void ota_show(uint32_t step, int32_t code)   /* the update's steps as a bar across the bottom */
{
    uint32_t x, y;
    uint16_t c = step < 9u ? 0xFD20u : code == 1 ? 0x07E0u : 0xF800u;   /* orange; green done; red failed */
    uint32_t w = step < 9u ? step * 240u / 9u : 240u;
    for (y = 214u; y < 226u; y++)
        for (x = 0; x < 240u; x++)
            fbuf[y * 240u + x] = x < w ? c : 0x2104u;
    lcd_rows(214, 226);
}

/* ---- the rescue: USB and the installer's update entry only, nothing else; after two boots that never reached the main
 * loop (fm1_glue.c), or with OCT- held at power-on. The web installer finds the FM-1 here and installs again ---- */
static char *hex8(char *e, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        *e++ = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    *e = 0;
    return e;
}
static void rescue_main(void)
{
    char b[48], *e;
    uint32_t i;
    flog(0xC0);
    fb = fbuf;                                  /* the core's drawing (gfx.c), on the frame */
    g_reset();
    fill_rect(0, 0, 240, 240, 0xFD20u);         /* orange: the rescue */
    text(&F_INT11, 120, 40, "ONDADREAM RESCUE", 0x0000u, TA_C);
    text(&F_SILK8, 120, 62, "INSTALL AGAIN WITH THE WEB INSTALLER", 0x0000u, TA_C);
    /* what the last boots reached (1 start .. 9 running) and why the chip last reset */
    e = cat(b, "BOOTS ");
    for (i = 0; i < 4u; i++) { *e++ = (char)('0' + diag.hist[i] % 10u); *e++ = ' '; }
    *e = 0;
    text(&F_SILK8, 20, 100, b, 0x0000u, TA_L);
    e = cat(b, "RESET ");
    e = hex8(e, diag.p3_rst);
    e = cat(e, " FAIL ");
    *e++ = (char)('0' + guard.failed % 10u);
    *e = 0;
    text(&F_SILK8, 20, 116, b, 0x0000u, TA_L);
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        e = cat(b, "CRASH ");
        e = hex8(e, fm1_crash.vec);
        e = cat(e, " PC ");
        hex8(e, fm1_crash.pc);
        text(&F_SILK8, 20, 132, b, 0x0000u, TA_L);
        e = cat(b, "EMU ");
        e = hex8(e, fm1_crash.emu);
        e = cat(e, " DBG ");
        hex8(e, fm1_crash.dbg);
        text(&F_SILK8, 20, 148, b, 0x0000u, TA_L);
    } else
        text(&F_SILK8, 20, 132, "NO CRASH ON RECORD", 0x0000u, TA_L);
    text(&F_SILK8, 120, 200, OD_VERSION, 0x0000u, TA_C);
    lcd_flush(1);
    usb_start();
    usb_on = 1;
    fm1_guard_lock_top();
    for (;;) {
        uint32_t now = fm1_ms;
        fm1_wdt_feed();
        usb_retry(now);
        if (now > 2000u)
            boot_healthy();                     /* the next power-on tries OndaDream again */
        if (usb.uboot_req) {
            usb_detach();
            fm1_delay_ms(30);
            fm1_enter_uboot();
        }
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();
        }
    }
}

/* ---- the main loop ---- */
static void app_main(void)
{
    uint32_t i, leds_k = ~0u, leds_b = ~0u, last_adc = 0, last_frame = 0, last_stat = 0, frames = 0;
    int32_t master = -100;
    uint8_t healthy = 0;
    {
        uint32_t f = irq_save();
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the 1 MiB part every FM-1 has */
        irq_restore(f);
        if (flash_ok)
            ota_boot_cleanup();                 /* what an interrupted update left behind */
    }
    boot_stage(2);
    lcd_init();
    boot_stage(3);
    fm1_input_init();
    fm1_timer5_start(isr_timer5, 4);            /* the key scan and the ms clock from here (the rescue needs them) */
    {
        uint32_t f = irq_save();
        fm1_lcd_backlight(1);                   /* port A is the scan's too: IRQs off around it */
        irq_restore(f);
    }
    fm1_irq_enable_all();
    fm1_delay_ms(60);                           /* a few scans: OCT- held at power-on? */
    if (boot_rescue || (fm1_in.buttons & (1u << BTN_ID[OB_OCTDN])))
        rescue_main();
    boot_stage(4);
    od_init();
    od_selftest();
    od_frame(fbuf);                             /* the splash */
    boot_stage(5);
    lcd_flush(1);
    boot_stage(6);
    fm1_adc_init();
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
    {
        uint32_t f = irq_save();                /* (a vector is attached with IRQs off) */
        fm1_audio_init(abuf, HALF_FRAMES * 2u, isr_alnk0, 3);   /* below TIMER5 (4): the scan nests into a render */
        irq_restore(f);
    }
    boot_stage(7);
    usb_start();
    usb_on = 1;
    fm1_guard_lock_top();
    boot_stage(8);
    for (;;) {
        uint32_t now = fm1_ms, b, n;
        fm1_wdt_feed();
        usb_retry(now);
        if (usb.uboot_req) {                    /* the SysEx soft key */
            fm1_audio_stop();
            usb_detach();
            fm1_delay_ms(30);
            fm1_enter_uboot();
        }
        ota_service();                          /* the installer's handshake */
        if (usb.ota_req) {                      /* and its update: commits and resets, or comes back */
            usb.ota_req = 0;
            if (flash_ok) {
                ota_quiet();
                ota_session();
                {
                    uint32_t f = irq_save();
                    fm1_audio_init(abuf, HALF_FRAMES * 2u, isr_alnk0, 3);
                    irq_restore(f);
                }
            }
            lcd_flush(1);
        }
        if (!healthy && now > 2000u) {
            healthy = 1;
            boot_stage(9);
            boot_healthy();
        }
        if (now - last_adc >= 30u) {            /* MASTER: polled, a few hundred us */
            int32_t v;
            last_adc = now;
            v = fm1_adc_read(FM1_ADC_MASTER);
            if (v >= 0 && (v - master > 3 || master - v > 3 || (v != master && (v == 0 || v == 1023)))) {
                master = v;
                master_pot = v;
            }
        }
        /* the LEDs */
        n = od_leds_keys();
        b = od_leds_buttons();
        if (n != leds_k) {
            for (i = 0; i < 27u; i++)
                if ((n ^ leds_k) & (1u << i))
                    fm1_led_key(14u + i, (int)((n >> i) & 1u));
            leds_k = n;
        }
        if (b != leds_b) {
            for (i = 0; i < OB_N; i++)
                if ((b ^ leds_b) & (1u << i))
                    fm1_led_key(BTN_ID[i], (int)((b >> i) & 1u));
            leds_b = b;
        }
        /* the screen, at most ~30 times a second */
        if (now - last_frame >= 33u) {
            last_frame = now;
            od_frame(fbuf);
            lcd_flush(0);
            frames++;
        }
        if (now - last_stat >= 1000u) {         /* GLOBAL: frames a second, the audio's share of the CPU */
            uint32_t t = audio_ticks;
            audio_ticks = 0;
            od_stats(frames * 1000u / (now - last_stat), t / ((now - last_stat) * (FM1_TICKS_PER_US * 10u)));
            frames = 0;
            last_stat = now;
        }
    }
}
