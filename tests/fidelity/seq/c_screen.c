/* the firmware's SEQUENCER and MIXER screens in fixed states (the same as shots.js sets in the preview) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "od.h"
static uint32_t ms;
uint32_t od_ms(void) { return ms; }
#include "od.c"
static uint16_t fbx[240 * 240];
static void dump(const char *nm)
{
    FILE *f = fopen(nm, "wb");
    int i;
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t c = fbx[i];
        unsigned char p[3] = {(unsigned char)((c >> 11) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
        fwrite(p, 3, 1, f);
    }
    fclose(f);
}
int main(void)
{
    od_init();
    ms = 10350;
    page = PG_SEQ; od_frame(fbx); dump("c_A.ppm");                      /* A: stopped */
    sq.playing = 1; sq.vis = 5; rec_on = 1; sel_track = 0;                /* B: playing, rec, the drums */
    od_frame(fbx); dump("c_B.ppm");
    sel_track = 2; tr[3].mute = 1; tr[2].grid[16] |= 0x0011; tr[2].sel_note = 16; sq.bpm = 97; rec_on = 0; sq.vis = 13;
    tr[2].preset = 20; tr[1].vol = .4f; sel_drum = 3;
    od_frame(fbx); dump("c_D.ppm");                                       /* D: a synth track, a mute */
    page = PG_MIX; sq.vis = 4; tr[1].mute = 1;
    od_frame(fbx); dump("c_C.ppm");                                       /* C: the mixer */
    tr[2].pan = -.5f; tr[2].fx = .6f;
    od_frame(fbx); dump("c_E.ppm");
    return 0;
}
