/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The FM-1 build: the core (src/core) on the hardware layer of felucca-bsp (MPL-2.0, bsp/).
 *
 *   audio   ALNK0 I2S, 44.1 kHz, halves of 128 frames: od_audio in the ISR, Q15 -> the codec's 24 bits
 *   input   TIMER5 at 10 kHz scans keys, buttons and encoders (BSP); the main loop hands the changes to the core
 *   screen  od_frame draws 240x240 RGB565 into RAM; only the rows that changed go to the panel, byte-swapped
 *   USB     MIDI in (a keyboard or a DAW) and the installer's update entry: a new package can always replace this */
#include <stdint.h>
#define FELUCCA_CDC 0
#define FELUCCA_OTA 1
#ifndef OD_PRODUCT
#define OD_PRODUCT "FM-1_910"                   /* the identity the installer sees, == the package's */
#endif
#define FELUCCA_ID OD_PRODUCT
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

/* the core reads the clock the tick keeps */
uint32_t od_ms(void) { return fm1_ms; }
#include "od.c"

/* ---- the panel: which matrix id carries which printed label (measured on the hardware) ---- */
static const uint8_t BTN_ID[OB_N] = {
    2, 3, 4, 5, 6, 7,                           /* FX SCL ENV LFO EDIT GLO */
    8, 9, 10, 11, 12, 13,                       /* HOME SAVE ARP SEQ PLAY REC */
    0, 1,                                       /* OCT- OCT+ */
};
static const uint8_t ENC_ID[OE_N] = { 0, 1, 6, 2, 3, 4, 5 };   /* SELECT ALGORITHM PRESETS KNOB1..4 */

/* ---- audio ---- */
#define HALF_FRAMES 128u
static int32_t abuf[2u * HALF_FRAMES * 2u];
void fm1_alnk0_irq(void)
{
    uint8_t p;
    in_audio = 1;                               /* first: the tick nests from here on */
    p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        int32_t *o = &abuf[fm1_audio_free_half() * HALF_FRAMES * 2u];
        uint32_t i;
        od_audio(o, HALF_FRAMES);
        for (i = 0; i < 2u * HALF_FRAMES; i++)
            o[i] *= 1 << 7;                     /* Q15 -> 24 bits, -6 dBFS ceiling */
        fm1_audio_ack_half();
    }
    in_audio = 0;
}
extern void isr_alnk0(void);

static void ota_quiet(void) { fm1_audio_stop(); }

/* ---- the screen: rows that changed, swapped to high byte first, two row buffers so the swap overlaps the DMA ---- */
static uint16_t fbuf[240u * 240u];
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

/* ---- the main loop ---- */
static void midi_drain(void)
{
    if (midi_in_overflow) {                     /* a broken stream: drop it, release the notes */
        mi_r = mi_w;
        midi_in_overflow = 0;
        od_midi(0xB0, 123, 0);
        return;
    }
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], f;
        mi_r++;
        f = irq_save();
        od_midi((uint8_t)(pkt >> 8), (uint8_t)((pkt >> 16) & 0x7Fu), (uint8_t)((pkt >> 24) & 0x7Fu));
        irq_restore(f);
    }
}

static void app_main(void)
{
    uint32_t i, notes_seen = 0, btn_seen = 0, leds_k = ~0u, leds_b = ~0u, last_adc = 0, last_frame = 0;
    int32_t master = -100;
    uint8_t healthy = 0;
    {
        uint32_t f = irq_save();
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the 1 MiB part every FM-1 has */
        irq_restore(f);
        if (flash_ok)
            ota_boot_cleanup();                 /* what an interrupted update left behind */
    }
    lcd_init();
    od_init();
    od_frame(fbuf);                             /* the splash's first frame before the light comes on */
    lcd_flush(1);
    fm1_input_init();
    fm1_adc_init();
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_FRAMES * 2u, isr_alnk0, 3);
    usb_start();
    fm1_timer5_start(isr_timer5, 4);            /* above the audio (3): the scan keeps its pace during a render */
    fm1_guard_lock_top();
    {
        uint32_t f = irq_save();
        fm1_lcd_backlight(1);                   /* port A is the scan's too: IRQs off around it */
        irq_restore(f);
    }
    fm1_irq_enable_all();
    for (;;) {
        uint32_t now = fm1_ms, b, n, d, f;
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
                fm1_audio_init(abuf, HALF_FRAMES * 2u, isr_alnk0, 3);
            }
            lcd_flush(1);
        }
        if (!healthy && now > 2000u) {
            healthy = 1;
            boot_healthy();
        }
        midi_drain();
        /* the keys, the buttons, the encoders: changes to the core, the audio held off meanwhile */
        n = fm1_in.notes;
        b = fm1_in.buttons;
        f = irq_save();
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
        irq_restore(f);
        for (i = 0; i < OE_N; i++) {
            int32_t s = fm1_enc_take(ENC_ID[i]);
            if (s) {
                f = irq_save();
                od_enc(i, s);
                irq_restore(f);
            }
        }
        if (now - last_adc >= 30u) {            /* MASTER: polled, a few hundred us */
            int32_t v;
            last_adc = now;
            v = fm1_adc_read(FM1_ADC_MASTER);
            if (v >= 0 && (v - master > 3 || master - v > 3 || (v != master && (v == 0 || v == 1023)))) {
                master = v;
                od_master((uint32_t)v);
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
        }
    }
}
