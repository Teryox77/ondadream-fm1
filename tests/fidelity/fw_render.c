/* the firmware's core, rendered offline with the same notes as ref.js */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "od.h"
static uint64_t samples;
static unsigned long long log_base;
#ifndef OUTFMT
#define OUTFMT "/tmp/fid/c_%d.f32"
#endif
uint32_t od_ms(void) { return (uint32_t)(samples * 1000u / 44100u); }
#include "od.c"
static int32_t lr[256];
static void run(FILE *f, double sec)
{
    uint32_t n = (uint32_t)(sec * 44100.0), i, k;
    for (i = 0; i < n; i += 128) {
        od_audio(lr, 128);
        samples += 128;
        if (f) for (k = 0; k < 256; k++) { float v = (float)lr[k] / 32768.f; fwrite(&v, 4, 1, f); }
    }
}
int main(int argc, char **argv)
{
    int i = atoi(argv[1]);
    char nm[64];
    FILE *f;
    struct { double t; int on, n; } ev[] = {{.05,1,57},{.05,1,60},{.05,1,64},{1.25,0,57},{1.25,0,60},{1.25,0,64},{2.6,1,45},{3.2,0,45}};
    double t = 0;
    unsigned e = 0;
    od_init();
    run(0, 1.4);                                /* the splash */
    od_enc(OE_PRESET, i);
    run(0, 2.0);
    sprintf(nm, OUTFMT, i);
    log_base = sclock ? sclock : 1;
    f = fopen(nm, "wb");
    while (t < 4.5) {
        while (e < 8 && ev[e].t <= t + 1e-9) { od_midi(ev[e].on ? 0x90 : 0x80, (uint8_t)ev[e].n, ev[e].on ? 108 : 0); e++; }
        run(f, 1024 / 44100.0);
        t += 1024 / 44100.0;
    }
    fclose(f);
    return 0;
}
