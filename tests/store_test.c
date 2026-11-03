/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * Host test of the FM-1's flash store (src/fm1/fm1_store.c) on a flash in RAM: saves taken in turn, read back at
 * power-on; a power cut at every point of a save leaves the store before it or the one after, never nothing;
 * a damaged sector is passed over. */
#include <stdio.h>
#include <string.h>
static unsigned now_ms;
#include "../src/core/od.h"
uint32_t od_ms(void) { return now_ms; }
void od_midi_out(uint8_t st, uint8_t d1, uint8_t d2) { (void)st; (void)d1; (void)d2; }
#include "../src/core/od.c"
static uint8_t fl[0x100000];
static int ops, cut_at = -1;              /* a power cut: erases and page programs after this many do nothing */
static int store_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, fl + off, n); return 0; }
static int store_erase(uint32_t off)
{
    if (off & 0xFFFu || off < 0x93000u || off >= 0x95000u) { printf("erase outside the store: %x\n", off); return -1; }
    if (cut_at >= 0 && ops++ >= cut_at) return 0;
    memset(fl + off, 0xFF, 0x1000);
    return 0;
}
static int store_prog(uint32_t off, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (off < 0x93000u || off + n > 0x95000u || ((off & 0xFFFu) + n) > 0xF00u) { printf("program outside: %x %u\n", off, n); return -1; }
    for (i = 0; i < n; i++) {
        if ((off + i) % 256u == 0u || i == 0) { if (cut_at >= 0 && ops++ >= cut_at) return 0; }
        fl[off + i] &= p[i];                  /* NOR: bits only go to 0 */
    }
    return 0;
}
static void store_quiet(void) {}
#include "../src/fm1/fm1_store.c"
static void boot(void)                    /* a power-on: the core fresh, the store read */
{
    n_user = 0;
    memset(&mset, 0, sizeof mset);
    store_at = store_seq = 0;
    store_boot();
}
int main(void)
{
    int bad = 0, k, c;
    memset(fl, 0xFF, sizeof fl);
    od_init();
    boot();
    if (n_user) { printf("an empty flash gave presets\n"); bad++; }
    for (k = 0; k < 5; k++) {                     /* five saves */
        P.cutoff = 1000.f + 100.f * (float)k;
        preset_save();
        if (!store_save()) { printf("save %d failed\n", k); bad++; }
    }
    boot();
    if (n_user != 5 || user_p[4].cutoff != 1400.f || store_seq != 5u) { printf("after 5 saves: %d presets, seq %u\n", n_user, store_seq); bad++; }
    mset.in_ch = 9; settings_dirty = 1; settings_ms = 0; now_ms = 5000;
    if (!od_store_dirty() || !store_save()) { printf("settings not saved\n"); bad++; }
    boot();
    if (mset.in_ch != 9 || n_user != 5) { printf("settings: in %d, %d presets\n", mset.in_ch, n_user); bad++; }
    for (c = 0; c < 40; c++) {                    /* a power cut at every step of the 7th save */
        static uint8_t keep[0x2000];
        memcpy(keep, fl + 0x93000, sizeof keep);
        boot();
        P.cutoff = 3000.f;
        preset_save();
        ops = 0; cut_at = c;
        store_save();
        cut_at = -1;
        boot();
        if (n_user != 5 && n_user != 6) { printf("cut at %d: %d presets\n", c, n_user); bad++; }
        if (n_user == 6 && user_p[5].cutoff != 3000.f) { printf("cut at %d: a wrong 6th\n", c); bad++; }
        memcpy(fl + 0x93000, keep, sizeof keep);  /* back to before, for the next cut */
    }
    boot();
    fl[store_at + 16 + 40] ^= 0x10u;              /* damage the sector in charge: the other one takes over */
    k = (int)store_at;
    boot();
    if (n_user != 5 && n_user != 4) { printf("damage: %d presets\n", n_user); bad++; }
    if ((int)store_at == k) { printf("damage: the damaged sector still in charge\n"); bad++; }
    if (bad) { printf("store: %d failures\n", bad); return 1; }
    printf("store ok\n");
    return 0;
}
