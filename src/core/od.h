/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The core: the synth, the UI and the input, the same on the FM-1 (src/fm1) and in the browser (web/). It does not
 * know the hardware: the platform calls od_audio from the audio interrupt, od_frame from its main loop, and hands
 * the panel's events to od_key / od_button / od_enc / od_master. */
#pragma once
#include <stdint.h>

#define OD_VERSION "v1.1"
#ifndef OD_BIG
#define OD_BIG                          /* big buffers: the FM-1 build puts them in the RAM above .bss (src/fm1/app.ld) */
#endif
#define OD_AUTHOR "Teryox77"

/* ---- panel: the printed labels ---- */
enum { OB_FX, OB_SCL, OB_ENV, OB_LFO, OB_EDIT, OB_GLO, OB_HOME, OB_SAVE, OB_ARP, OB_SEQ, OB_PLAY, OB_REC,
       OB_OCTDN, OB_OCTUP, OB_N };
enum { OE_SELECT, OE_ALGO, OE_PRESET, OE_K1, OE_K2, OE_K3, OE_K4, OE_N };

/* ---- the platform gives ---- */
uint32_t od_ms(void);                      /* milliseconds since power-on */

/* ---- the core ---- */
void od_init(void);
void od_audio(int32_t *lr, uint32_t n);    /* n stereo frames, interleaved, Q15 in int32 (the platform scales) */
int od_frame(uint16_t *fb);                /* the 240 x 240 RGB565 screen; 1 = it changed (send it) */
void od_key(uint32_t k, int down);         /* the 27 keys, 0 = the lowest (C) */
void od_button(uint32_t b, int down);
void od_enc(uint32_t e, int32_t steps);    /* clockwise > 0 */
void od_master(uint32_t v);                /* the MASTER pot 0..1023 */
void od_stats(uint32_t fps, uint32_t cpu);
void od_selftest(void);                    /* once, at start (before the audio): GLOBAL shows the result */ /* the platform's screen rate and audio load (%), shown on GLOBAL */
void od_midi(uint8_t st, uint8_t d1, uint8_t d2);   /* a channel message (USB MIDI, TRS) */
uint32_t od_leds_keys(void);               /* bit k: key k lit (27 keys) */
uint32_t od_leds_buttons(void);            /* bit b: button b (OB_*) lit */
int od_splash(void);                       /* 1 while the power-on splash runs */
/* the presets saved with SAVE: the platform keeps the image (the FM-1 in its flash) */
int od_store_dirty(void);                  /* 1 once after a SAVE: write the image */
#define OD_STORE_MAX_BYTES 3008u
uint32_t od_store_image(uint8_t *buf, uint32_t max);   /* the image's bytes (at most OD_STORE_MAX_BYTES), 0 if max is short */
void od_store_done(int ok);                /* the platform's answer: 0 = it could not keep it (the screen says so) */
int od_store_load(const uint8_t *buf, uint32_t n);     /* at start, after od_init; -1 if not a store */
