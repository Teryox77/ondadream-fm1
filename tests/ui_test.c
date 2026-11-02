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
    {   /* the sequencer, through the panel: its pages, keys, knobs, PLAY / REC, every rhythm and preset, all steps on */
        int bad = 0, t, k, r;
        int32_t pk = 0;
        od_button(OB_SEQ, 1); if (page != PG_SEQ) { printf("SEQ: no page\n"); bad++; }
        od_frame(frame); save("seq_stop");
        od_button(OB_PLAY, 1); tick(1000);
        if (!sq.playing || sq.vis < 0) { printf("PLAY: not playing\n"); bad++; }
        for (i = 0; i < 2 * 256; i++) { int32_t v = abuf[i] < 0 ? -abuf[i] : abuf[i]; if (v > pk) pk = v; }
        if (pk < 300) { printf("PLAY: silent (%d)\n", (int)pk); bad++; }
        od_frame(frame); save("seq_play");
        od_key(0, 1); od_key(0, 0);                              /* F: the first white key, step 1 of track 1 */
        if (tr[1].grid[12] & 1u) { printf("step key: not toggled\n"); bad++; }
        od_key(0, 1); od_key(0, 0);
        od_enc(OE_ALGO, 5); if (sel_track != 0) { printf("ALGORITHM: not the drums (%d)\n", sel_track); bad++; }
        od_enc(OE_ALGO, -1); if (sel_track != 3) { printf("ALGORITHM: not track 3\n"); bad++; }
        od_key(1, 1); od_key(1, 0); if (sel_track != 1) { printf("black key 1: not track 1\n"); bad++; }
        od_key(8, 1); od_key(8, 0); if (sel_track != 0) { printf("black key 4: not the drums\n"); bad++; }
        od_key(13, 1); od_key(13, 0); if (sel_drum != 1) { printf("black key 6: not the snare (%d)\n", sel_drum); bad++; }
        for (r = 0; r < 10; r++) { od_enc(OE_K4, 1); tick(250); }      /* every rhythm */
        od_frame(frame); save("seq_drums");
        for (t = 1; t < 4; t++) {                                  /* every preset, every note, each synth track */
            od_key(t == 1 ? 1 : t == 2 ? 3 : 5, 1); od_key(t == 1 ? 1 : t == 2 ? 3 : 5, 0);
            for (k = 0; k < 45; k++) { od_enc(OE_K3, 1); tick(20); }
            for (k = 0; k < 25; k++) { od_enc(OE_K4, k < 12 ? 1 : -1); tick(10); }
            }
        for (t = 1; t < 4; t++) for (k = 0; k < 24; k++) tr[t].grid[k] = 0xFFFF;   /* the worst case: chords on every step */
        for (k = 0; k < 8; k++) lay[k % 4].steps = 0xFFFF;
        od_enc(OE_K1, 400); tick(2000);                               /* 240 BPM */
        if (sq.bpm != 240) { printf("tempo: %d\n", sq.bpm); bad++; }
        od_button(OB_SEQ, 1); if (page != PG_MIX) { printf("SEQ again: no MIXER\n"); bad++; }
        od_key(0, 1); od_key(0, 0); if (!tr[1].mute) { printf("MIXER: white key 1 did not mute track 1\n"); bad++; }
        od_enc(OE_K2, -100); od_enc(OE_K3, 100); od_enc(OE_K1, 100); tick(500);
        od_frame(frame); save("mix");
        od_button(OB_HOME, 1); od_button(OB_REC, 1);                  /* REC: a key on HOME lands on track 1 */
        od_key(0, 1); od_key(0, 0); od_key(0, 1); od_key(0, 0);
        sel_track = 2; for (k = 0; k < 24; k++) tr[2].grid[k] = 0;
        od_key(2, 1); tick(150); od_key(2, 0);                         /* G3 = 55: row 19 of the roll */
        if (!tr[2].grid[19]) { printf("REC: G3 not recorded on track 2\n"); bad++; }
        od_button(OB_PLAY, 1); tick(300);
        if (sq.playing || sq.vis != -1) { printf("STOP: still playing\n"); bad++; }
        for (t = 1; t < 4; t++) for (k = 0; k < 4; k++) if (tv[t][k].active && !tv[t][k].rel) { printf("STOP: a voice held\n"); bad++; }
        od_frame(frame); save("home_after_seq");
        if (bad) { printf("sequencer: %d failures\n", bad); return 1; }
        printf("sequencer ok\n");
    }
    printf("ok, %u ms\n", now_ms);
    return 0;
}
