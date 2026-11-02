/* the firmware's core with the sequencer playing, offline: the scenarios of ref_seq.js */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "od.h"
static uint64_t samples;
uint32_t od_ms(void) { return (uint32_t)(samples * 1000u / 44100u); }
#include "od.c"
static int32_t lr[256];
static void run(FILE *f, uint32_t blocks1024)
{
    uint32_t i, k;
    for (i = 0; i < blocks1024 * 8u; i++) {
        od_audio(lr, 128);
        samples += 128;
        if (f) for (k = 0; k < 256; k++) { float v = (float)lr[k] / 32768.f; fwrite(&v, 4, 1, f); }
    }
}
int main(int argc, char **argv)
{
    int sc = atoi(argv[1]);
    char nm[64];
    FILE *f;
    od_init();
    P.vol = .8f;
    if (sc > 0 && sc < 11) {
        int i = sc - 1, r;
        preset_load(i * 4 % 43); PS = P; P.vol = .8f;
        set_rhythm(i);
        sq.bpm = 90 + i * 12;
        for (r = 1; r < 4; r++) {
            tr[r].preset = (i * 5 + r * 7) % 43;
            tr[r].pan = (float)((i + r) % 5 - 2) * .4f;
            tr[r].fx = (float)((i * 3 + r) % 4) * .3f;
            tr[r].vol = .5f + (float)((i + 2 * r) % 4) * .15f;
            for (int p = 0; p < 24; p++) tr[r].grid[p] = 0;
        }
        tr[0].pan = (float)(i % 3 - 1) * .5f; tr[0].fx = (float)(i % 4) * .25f; tr[0].vol = .6f + (float)(i % 3) * .15f;
        tr[1].grid[12] = 0x0101; tr[1].grid[16] = 0x0101; tr[1].grid[19] = 0x0101;   /* a chord */
        tr[2].grid[i % 24] = 0x1248; tr[2].grid[(i * 7) % 24] |= 0x8421;
        tr[3].grid[23 - i] = 0x5555;
        tr[i % 4].mute = (uint8_t)(i % 3 == 2);
    }
    if (sc >= 13) { set_rhythm(sc - 13); sc = 11; }
    if (sc == 11) { tr[1].mute = tr[2].mute = tr[3].mute = 1; }
    if (sc == 12) tr[0].mute = 1;
    if (sc >= 11) { P.cho = P.eco = P.rev = 0.f; PS = P; }
    run(0, 86);                                     /* 2 s: the splash, the smoothers settle */
    seq_play();
    sprintf(nm, "/tmp/sfid/c_%d.f32", atoi(argv[1]));
    f = fopen(nm, "wb");
    run(f, sc && sc < 11 ? 216 : 345);                         /* 5 s (8 s) */
    seq_stop();
    run(f, 43);                                     /* stop: the tails */
    fclose(f);
    return 0;
}
