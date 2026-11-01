/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The browser build: the same core as the FM-1 (src/core), compiled to WebAssembly. The clock follows the audio
 * the page asks for (44.1 kHz), so the screen, the arpeggiator and the splash run in the device's time. */
#include <stdint.h>
#include "../src/core/od.h"
#define EXPORT __attribute__((used, visibility("default")))
static uint64_t samples;
uint32_t od_ms(void) { return (uint32_t)(samples * 1000u / 44100u); }
#include "../src/core/od.c"

static float out_l[1024], out_r[1024];
static int32_t lr[2048];
static uint16_t web_fb[240 * 240];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT void web_init(void) { od_init(); od_selftest(); }
EXPORT void web_render(uint32_t n)
{
    uint32_t i;
    if (n > 1024u) n = 1024u;
    od_audio(lr, n);
    for (i = 0; i < n; i++) {
        out_l[i] = (float)lr[2u * i] * (1.f / 32768.f);
        out_r[i] = (float)lr[2u * i + 1u] * (1.f / 32768.f);
    }
    samples += n;
}
EXPORT void web_idle(uint32_t ms) { samples += (uint64_t)ms * 441u / 10u; }   /* time without audio (before Start) */
EXPORT int web_frame(void) { return od_frame(web_fb); }
EXPORT void web_key(uint32_t k, int down) { od_key(k, down); }
EXPORT void web_button(uint32_t b, int down) { od_button(b, down); }
EXPORT void web_enc(uint32_t e, int32_t s) { od_enc(e, s); }
EXPORT void web_master(uint32_t v) { od_master(v); }
EXPORT void web_midi(uint32_t st, uint32_t d1, uint32_t d2) { od_midi((uint8_t)st, (uint8_t)d1, (uint8_t)d2); }
EXPORT uint32_t web_leds_keys(void) { return od_leds_keys(); }
EXPORT uint32_t web_leds_buttons(void) { return od_leds_buttons(); }
EXPORT void web_stats(uint32_t fps, uint32_t cpu) { od_stats(fps, cpu); }
