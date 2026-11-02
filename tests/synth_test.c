/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * Host test of the synth: every preset plays a chord and an arpeggio; UBSan catches any integer overflow; the
 * output's peak and RMS per preset; a WAV of the whole run. */
#include <stdio.h>
#include <math.h>
#include <string.h>
static unsigned now_ms;
#include "../src/core/od.h"
uint32_t od_ms(void) { return now_ms; }
#include "../src/core/synth.c"
#include "../src/core/seq.c"
static void key_off(int n) { (void)n; }
static int32_t buf[2 * 512];
static FILE *wav;
static long frames;
static void run(int ms, double *peak, double *rms)
{
    int n = ms * 441 / 10, k;
    while (n > 0) {
        int m = n > 512 ? 512 : n;
        od_audio(buf, (uint32_t)m);
        for (k = 0; k < 2 * m; k++) {
            short s = (short)buf[k];
            double v = buf[k] / 32768.0;
            if (fabs(v) > *peak) *peak = fabs(v);
            *rms += v * v;
            fwrite(&s, 2, 1, wav);
        }
        frames += m;
        n -= m;
        now_ms += (unsigned)(m * 1000 / 44100);
    }
}
int main(void)
{
    int p;
    unsigned char hdr[44] = {0};
    wav = fopen("/tmp/od_test.wav", "wb");
    fwrite(hdr, 44, 1, wav);
    synth_init();
    seq_init();
    for (p = 0; p < OD_NPRESETS; p++) {
        double peak = 0, rms = 0;
        preset_load(p);
        held_add(48, .9f); if (!arp_on()) direct_on(48, .9f);
        held_add(55, .8f); if (!arp_on()) direct_on(55, .8f);
        held_add(64, .7f); if (!arp_on()) direct_on(64, .7f);
        run(900, &peak, &rms);
        held_remove(48); held_remove(55); held_remove(64);
        if (!arp_on()) { direct_off(48); direct_off(55); direct_off(64); }
        run(500, &peak, &rms);
        printf("%2d %-12s peak %.2f rms %.3f voices %d\n", p, OD_PRESETS[p].name, peak,
               sqrt(rms / (1400 * 44.1 * 2)), voices_active());
        synth_panic();
        run(300, &peak, &rms);
    }
    {   /* the WAV header */
        long data = frames * 4;
        unsigned int v;
        fseek(wav, 0, SEEK_SET);
        fwrite("RIFF", 4, 1, wav); v = (unsigned)(36 + data); fwrite(&v, 4, 1, wav);
        fwrite("WAVEfmt ", 8, 1, wav); v = 16; fwrite(&v, 4, 1, wav);
        { short f[2] = {1, 2}; fwrite(f, 4, 1, wav); }
        v = 44100; fwrite(&v, 4, 1, wav); v = 44100 * 4; fwrite(&v, 4, 1, wav);
        { short f[2] = {4, 16}; fwrite(f, 4, 1, wav); }
        fwrite("data", 4, 1, wav); v = (unsigned)data; fwrite(&v, 4, 1, wav);
    }
    fclose(wav);
    return 0;
}
