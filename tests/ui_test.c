/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * Host test of the UI: every page rendered to a PPM, with notes playing; UBSan / ASan on. */
#include <stdio.h>
#include <string.h>
static unsigned now_ms;
#include "../src/core/od.h"
uint32_t od_ms(void) { return now_ms; }
#include "../src/core/od.c"
static uint16_t frame[240 * 240];
static int32_t abuf[2 * 256];
static void tick(int ms) { int i; for (i = 0; i < ms / 5; i++) { od_audio(abuf, 220); now_ms += 5; } }
static void save(const char *name)
{
    char path[128];
    FILE *f;
    int i;
    snprintf(path, sizeof path, "/tmp/odui/%s.ppm", name);
    f = fopen(path, "wb");
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t v = frame[i];
        unsigned char c[3] = {(unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)((v & 31) << 3)};
        fwrite(c, 3, 1, f);
    }
    fclose(f);
}
int main(void)
{
    static const struct { int b; const char *n; } PAGES[] = {{OB_HOME, "home"}, {OB_EDIT, "filter"}, {OB_ENV, "env"},
        {OB_LFO, "lfo"}, {OB_ARP, "arp"}, {OB_FX, "fx"}, {OB_GLO, "glo"}};
    int i, p;
    char n[32];
    od_init();
    tick(400); od_frame(frame); save("splash_400");
    tick(1000); od_frame(frame); save("home_idle");
    od_key(7, 1); od_key(11, 1); od_key(14, 1); tick(300);
    for (i = 0; i < 7; i++) { od_button(PAGES[i].b, 1); od_frame(frame); save(PAGES[i].n); }
    od_button(OB_HOME, 1);
    for (p = 1; p < 12; p++) { od_enc(OE_PRESET, 1); tick(120); od_frame(frame); snprintf(n, sizeof n, "home_p%02d", p); save(n); }
    od_enc(OE_K1, 3); od_frame(frame); save("home_knob");
    od_button(OB_FX, 1); od_enc(OE_K1, 40); od_enc(OE_K2, 50); od_enc(OE_K4, 60); tick(200); od_frame(frame); save("fx_full");
    od_button(OB_ARP, 1); od_enc(OE_K1, 1); tick(500); od_frame(frame); save("arp_on");
    printf("ok, %u ms\n", now_ms);
    return 0;
}
