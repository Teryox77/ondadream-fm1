/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (felucca-bsp examples/hwtest)
 * Copyright (C) 2026 Teryox77 (the changes for OndaDream)
 * Boot, LCD set-up, the 10 kHz tick and the installer's update hooks, adapted from felucca-bsp's hwtest.
 * This file is under the MPL-2.0 like the BSP it comes from; the rest of OndaDream is not.
 *
 * Two boots in a row that never reach the main loop's healthy point start the rescue (USB and the update entry only,
 * main.c), where the web installer can install again; two more, and the chip's own update mode (UBOOT). */

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[], _pool_start[], _pool_end[];
static uint8_t boot_rescue;                     /* two failed boots: the rescue (main.c), not OndaDream */

static struct { uint32_t magic, pending, failed; } guard __attribute__((section(".noinit")));
/* how far each boot got (main.c BOOT_STAGE), kept across resets: the rescue screen shows the last ones */
static struct { uint32_t magic; uint8_t stage, hist[4]; uint8_t p3_rst; } diag __attribute__((section(".noinit")));
#define DIAG_MAGIC 0x4F444431u
static void boot_stage(uint32_t s) { diag.stage = (uint8_t)s; }
static volatile uint8_t in_audio;               /* set by the audio ISR: the tick nests into it */
static volatile uint8_t usb_on;                 /* usb_start has run: the tick may poll it */

/* ---- the 10 kHz tick: key scan, milliseconds, USB at 2 kHz (never while nested in the audio render) ---- */
void fm1_timer5_irq(void)
{
    static uint32_t sub, last, acc, owed;
    uint32_t t = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    acc += t - last;
    last = t;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (++sub == 5u) {
        sub = 0;
        owed = 1;
    }
    if (owed && !in_audio && usb_on) {
        owed = 0;
        usb_poll();
    }
}
extern void isr_timer5(void);

/* ---- LCD: ST7789 240x240 ---- */
static void lcd_cmd(uint8_t c, const uint8_t *d, uint32_t n)
{
    static uint8_t buf[4];
    uint32_t i;
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
    if (!n)
        return;
    for (i = 0; i < n; i++)
        buf[i] = d[i];
    fm1_lcd_send_data(buf, n);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void lcd_init(void)
{
    static const uint8_t colmod = 0x55, madctl = 0x00;
    fm1_lcd_hw_init();
    fm1_delay_ms(50);
    lcd_cmd(0x01, 0, 0);                        /* SWRESET */
    fm1_delay_ms(150);
    lcd_cmd(0x11, 0, 0);                        /* SLPOUT */
    fm1_delay_ms(120);
    lcd_cmd(0x3A, &colmod, 1);                  /* RGB565 */
    lcd_cmd(0x36, &madctl, 1);                  /* top-left origin, RGB */
    lcd_cmd(0x21, 0, 0);                        /* INVON: this panel is IPS */
    lcd_cmd(0x13, 0, 0);                        /* NORON */
    lcd_cmd(0x29, 0, 0);                        /* DISPON */
    fm1_lcd_baud(4);
}

static void lcd_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint8_t a[4];
    a[0] = 0; a[1] = (uint8_t)x; a[2] = 0; a[3] = (uint8_t)(x + w - 1u);
    lcd_cmd(0x2A, a, 4);
    a[1] = (uint8_t)y; a[3] = (uint8_t)(y + h - 1u);
    lcd_cmd(0x2B, a, 4);
    lcd_cmd(0x2C, 0, 0);
}

/* ---- the update (chip/ota.c): the installer's M-UPGRADE session over USB-MIDI ---- */
static uint8_t flash_ok;                        /* the expected flash part: the update may write it */
static void ota_show(uint32_t step, int32_t code);   /* main.c: the progress on the screen */
static void ota_quiet(void);                    /* main.c: audio off before the update */
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    uint8_t *d = p;
    while (n) {                                 /* 256-byte windows with interrupts off */
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void ota_commit(const uint8_t *parm)
{
    guard.pending = 0;                          /* an intentional reset, not a failed boot */
    guard.failed = 0;
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}

/* a fault: audio off, a red screen, then reset (fm1_crash keeps the record) */
static void fm1_fault(const fm1_crash_t *c)
{
    static uint16_t red[240];
    uint32_t i;
    (void)c;
    fm1_audio_stop();
    for (i = 0; i < 240u; i++)
        red[i] = 0x00F8u;                       /* 0xF800, high byte first */
    lcd_window(0, 0, 240, 240);
    for (i = 0; i < 240u; i++) {
        fm1_lcd_send_data(red, sizeof red);
        fm1_lcd_wait();
        fm1_lcd_deselect();
    }
    fm1_delay_ms(1500);
    fm1_reboot();
}

static void boot_healthy(void)                  /* the main loop runs: the next boot is not a loop */
{
    guard.pending = 0;
    guard.failed = 0;
}

static void app_main(void);

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_cpu1_stop();                            /* the second core held first: a reset (a watchdog, a fault, the update,
                                                 * the firmware before this one) can leave it running old code, which
                                                 * would write into the RAM cleared below (as Felucca's start) */
    fm1_audio_stop();                           /* and no DMA left running into the RAM by the firmware or the loader */
    fm1_usb_reset();                            /* before this one (the USB endpoints write RAM): both start again later */
    fm1_time_init();
    fm1_reset_reason();
    fm1_wdt_arm(0x0D);
    if (diag.magic != DIAG_MAGIC) {
        diag.magic = DIAG_MAGIC;
        diag.stage = 0;
        diag.hist[0] = diag.hist[1] = diag.hist[2] = diag.hist[3] = 0;
    }
    diag.hist[3] = diag.hist[2];                /* the stages the last four boots reached, newest first */
    diag.hist[2] = diag.hist[1];
    diag.hist[1] = diag.hist[0];
    diag.hist[0] = diag.stage;
    diag.stage = 1;
    diag.p3_rst = fm1_boot.p3_rst;
    if (guard.magic != 0x4F445231u) {
        guard.magic = 0x4F445231u;
        guard.failed = 0;
        guard.pending = 0;
    }
    if (guard.pending)
        guard.failed++;
    guard.pending = 1;
    if (guard.failed >= 4u) {                   /* the rescue failed too: the chip's own update mode */
        guard.failed = 0;
        guard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    boot_rescue = guard.failed >= 2u;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)    /* the flash driver, run from RAM */
        *d = *s;
    fm1_mailbox_clear();                        /* boot info and mailbox: nothing stale (an old "usb_update_mode") */
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;)
        ;
}
