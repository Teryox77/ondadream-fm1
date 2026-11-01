/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The synth: OndaDream's voice (two band-limited oscillators SAW / SQUARE / TRI / SINE, detune and mix, a
 * resonant state-variable low-pass with its own envelope and key tracking, an ADSR, a vibrato / filter LFO), eight
 * voices, the arpeggiator, the master drive, then chorus, echo and reverb.
 * Audio rate: integers only (Q15 signals, Q13 filter coefficients; no 64-bit products: see the BSP's docs/CPU.md).
 * Control rate (every OD_BLK samples): float for the coefficients. */
#include "od.h"
#include "od_tables.h"

#define OD_BLK 16u                      /* control block */
#define NV 8u                           /* voices */
#define CLAMP(x, l, h) ((x) < (l) ? (l) : (x) > (h) ? (h) : (x))

/* ---------------------------------------------------------------- the patch --- */
typedef struct {
    float wave, detune, oscMix, drive, cutoff, reso, fEnvAmt, keyTrk, a, d, s, r;
    float lfoRate, lfoPitch, lfoCut, fDecay, vol, arpPattern, arpBpm, arpOct, arpGate, cho, eco, etime, rev;
} patch_t;
static patch_t P;
static float tmod;                      /* the screen's filter touch (not on the FM-1: 0) */
static int preset_idx;

/* ---------------------------------------------------------------- fast math --- */
/* 2^x for |x| < 30, float: the integer part in the exponent, the fraction by OD_EXP2F (linear between steps) */
static float od_exp2(float x)
{
    union { float f; uint32_t u; } r;
    int32_t i;
    float fr, t;
    uint32_t k;
    if (x < -30.f) x = -30.f;
    if (x > 30.f) x = 30.f;
    i = (int32_t)x;
    if ((float)i > x) i--;
    fr = (x - (float)i) * 256.f;
    k = (uint32_t)fr;
    if (k > 255u) k = 255u;
    t = ((float)OD_EXP2F[k] + ((float)OD_EXP2F[k + 1u] - (float)OD_EXP2F[k]) * (fr - (float)k)) * (1.f / 32768.f) + 1.f;
    r.f = t;
    r.u += (uint32_t)i << 23;
    return r.f;
}
static float od_log2(float x)            /* x > 0: the exponent, then a polynomial on [1, 2) */
{
    union { float f; uint32_t u; } r;
    float m;
    int32_t e;
    r.f = x;
    e = (int32_t)((r.u >> 23) & 255u) - 127;
    r.u = (r.u & 0x007FFFFFu) | 0x3F800000u;
    m = r.f;
    return (float)e + (-1.7417939f + (2.8212026f + (-1.4699568f + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}
static float od_tan(float x)             /* 0 <= x <= 1.35: a Pade fit */
{
    float x2 = x * x;
    return x * (945.f - 105.f * x2 + x2 * x2) / (945.f - 420.f * x2 + 15.f * x2 * x2);
}
static float od_sinf(float ph)           /* ph in turns */
{
    float f = ph - (float)(int32_t)ph;
    if (f < 0.f) f += 1.f;
    return (float)OD_SINE[(uint32_t)(f * 1024.f) & 1023u] * (1.f / 32767.f);
}
static float midi_hz(float n) { return 440.f * od_exp2((n - 69.f) * (1.f / 12.f)); }
static uint32_t rnd_s = 0x12345678u;
static uint32_t rnd(void) { rnd_s ^= rnd_s << 13; rnd_s ^= rnd_s >> 17; rnd_s ^= rnd_s << 5; return rnd_s; }
static float rndf(void) { return (float)(rnd() >> 8) * (1.f / 16777216.f); }

/* ---------------------------------------------------------------- envelopes --- */
/* OndaDream's ADSR: one-pole segments (attack aims at 1.3 and stops at 1), the time is the segment's time constant */
typedef struct { uint8_t st; float v; } env_t;
static float env_step(env_t *e, float a, float d, float s, float r)
{
    float n = (float)OD_BLK, c;
    switch (e->st) {
    case 1:
        c = 1.f - od_exp2(-1.4427f * n / ((a < .002f ? .002f : a) * OD_FS));
        e->v += c * (1.3f - e->v);
        if (e->v >= 1.f) { e->v = 1.f; e->st = 2; }
        break;
    case 2:
        c = 1.f - od_exp2(-1.4427f * n / ((d < .002f ? .002f : d) * OD_FS));
        e->v += c * (s - e->v);
        if (e->v - s < .001f && s - e->v < .001f) e->st = 3;
        break;
    case 3:
        e->v = s;
        break;
    case 4:
        c = 1.f - od_exp2(-1.4427f * n / ((r < .002f ? .002f : r) * OD_FS));
        e->v -= c * e->v;
        if (e->v < .0005f) { e->v = 0.f; e->st = 0; }
        break;
    default:
        e->v = 0.f;
    }
    return e->v;
}

/* ---------------------------------------------------------------- voices --- */
typedef struct {
    int16_t note;
    uint8_t active, rel;
    uint32_t ts;
    int32_t vel_q15;
    uint32_t ph1, ph2;
    int32_t ic1, ic2;                    /* the filter's state */
    env_t amp, fe;
    float lfo;                           /* its LFO's phase, turns */
    int32_t g0;                          /* the amplitude at the block's start, Q15 */
} voice_t;
static voice_t vc[NV];
static uint32_t note_ctr, sclock;       /* note ages; the sample clock */
static int32_t lvl_peak;                /* the output's level for the screen, Q15 */
static float lfo_view;                  /* the LFO's phase for the screen */
static uint32_t note_flash_ms;

static void v_on(voice_t *v, int n, float vel)
{
    v->note = (int16_t)n;
    v->vel_q15 = (int32_t)(CLAMP(vel, .05f, 1.f) * 32767.f);
    v->ts = ++note_ctr;
    v->ph1 = rnd();
    v->ph2 = rnd();
    v->ic1 = v->ic2 = 0;
    v->amp.st = 1;
    v->fe.st = 1;
    v->lfo = rndf();
    v->active = 1;
    v->rel = 0;
    note_flash_ms = od_ms();
}
static void v_off(voice_t *v)
{
    if (v->amp.st) v->amp.st = 4;
    if (v->fe.st) v->fe.st = 4;
    v->rel = 1;
}
static void direct_on(int n, float vel)
{
    uint32_t i, best = 0xFFFFFFFFu;
    voice_t *t = &vc[0];
    int rel = 0;
    for (i = 0; i < NV; i++)
        if (vc[i].active && vc[i].note == n) { v_on(&vc[i], n, vel); return; }
    for (i = 0; i < NV; i++)
        if (!vc[i].active) { v_on(&vc[i], n, vel); return; }
    for (i = 0; i < NV; i++) {                   /* steal: the oldest releasing, else the oldest */
        if (vc[i].rel) { if (!rel || vc[i].ts < best) { rel = 1; best = vc[i].ts; t = &vc[i]; } }
        else if (!rel && vc[i].ts < best) { best = vc[i].ts; t = &vc[i]; }
    }
    v_on(t, n, vel);
}
static void direct_off(int n)
{
    uint32_t i;
    for (i = 0; i < NV; i++)
        if (vc[i].active && vc[i].note == n && !vc[i].rel) v_off(&vc[i]);
}

/* band-limited steps (polyBLEP) at a wrap: x = how far into the step, Q15 */
static inline int32_t blep(uint32_t ph, uint32_t inc)
{
    int32_t x;
    if (ph < inc) {                              /* just after the wrap */
        x = (int32_t)((ph >> 16) * 32768u / ((inc >> 16) | 1u));
        return 2 * x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {                /* just before it */
        x = (int32_t)(((0xFFFFFFFFu - ph) >> 16) * 32768u / ((inc >> 16) | 1u));
        return -(2 * x - ((x * x) >> 15) - 32768);
    }
    return 0;
}
static inline int32_t osc(uint32_t ph, uint32_t inc, uint32_t wave)
{
    int32_t s;
    switch (wave) {
    case 0:                                      /* SAW */
        return (int32_t)(ph >> 16) - 32768 - blep(ph, inc);
    case 1:                                      /* SQUARE */
        s = ph < 0x80000000u ? 32767 : -32767;
        return s + blep(ph, inc) - blep(ph + 0x80000000u, inc);
    case 2:                                      /* TRI */
        s = (int32_t)(ph >> 16);
        return s < 32768 ? 2 * s - 32768 : 98303 - 2 * s;
    default:                                     /* SINE */
        return OD_SINE[ph >> 22];
    }
}

/* one voice, one block: the coefficients in float, the samples in integers, added to out (Q15) */
static void v_render(voice_t *v, int32_t *out, uint32_t n)
{
    uint32_t wave = (uint32_t)CLAMP((int32_t)(P.wave + .5f), 0, 3), i;
    float amp, fe, l, vib, f1, f2, kmul, fc, g, k, a1f, vel, vamp;
    uint32_t inc1, inc2;
    int32_t a1, a2, a3, m2, m1, g0, g1, ic1 = v->ic1, ic2 = v->ic2;
    uint32_t ph1 = v->ph1, ph2 = v->ph2;
    amp = env_step(&v->amp, P.a, P.d, P.s, P.r);
    fe = env_step(&v->fe, .003f, P.fDecay, 0.f, .08f);
    if (!v->amp.st) {
        v->active = 0;
        return;
    }
    v->lfo += P.lfoRate * (float)n / (float)OD_FS;
    if (v->lfo >= 1.f) v->lfo -= 1.f;
    l = od_sinf(v->lfo);
    vib = l * P.lfoPitch * .01f;
    f1 = midi_hz((float)v->note + vib);
    f2 = midi_hz((float)v->note + P.detune + vib);
    inc1 = (uint32_t)(CLAMP(f1, 1.f, 19000.f) * (4294967296.f / (float)OD_FS));
    inc2 = (uint32_t)(CLAMP(f2, 1.f, 19000.f) * (4294967296.f / (float)OD_FS));
    kmul = od_exp2(((float)v->note - 48.f) * (1.f / 12.f) * P.keyTrk);
    fc = P.cutoff * kmul * od_exp2(fe * P.fEnvAmt * 5.f + l * P.lfoCut * 2.f + tmod * 3.f);
    fc = CLAMP(fc, 20.f, .42f * OD_FS);
    g = od_tan(3.14159265f * fc / (float)OD_FS);
    k = 1.f / (.5f + P.reso * P.reso * 17.5f);
    a1f = 1.f / (1.f + g * (g + k));
    a1 = (int32_t)(a1f * 8192.f);
    a2 = (int32_t)(g * a1f * 8192.f);
    a3 = (int32_t)(g * g * a1f * 8192.f);
    m2 = (int32_t)(CLAMP(P.oscMix, 0.f, 1.f) * 32767.f);
    m1 = 32767 - m2;
    vel = (float)v->vel_q15 * (1.f / 32767.f);
    vamp = amp * (.25f + .75f * vel * vel);
    g1 = (int32_t)(CLAMP(vamp, 0.f, 1.f) * 32767.f);
    g0 = v->g0;
    for (i = 0; i < n; i++) {
        int32_t s = (((osc(ph1, inc1, wave) * m1) >> 15) + ((osc(ph2, inc2, wave) * m2) >> 15)) >> 1;   /* Q14 */
        int32_t v3 = s - ic2;
        int32_t vv1 = (a1 * ic1 + a2 * v3) >> 13;
        int32_t vv2 = ic2 + ((a2 * ic1 + a3 * v3) >> 13);
        int32_t gg = g0 + (((g1 - g0) * (int32_t)i) >> 4);
        ic1 = CLAMP(2 * vv1 - ic1, -100000, 100000);
        ic2 = CLAMP(2 * vv2 - ic2, -100000, 100000);
        out[i] += (CLAMP(vv2, -65535, 65535) * gg) >> 15;
        ph1 += inc1;
        ph2 += inc2;
    }
    v->g0 = g1;
    v->ic1 = ic1;
    v->ic2 = ic2;
    v->ph1 = ph1;
    v->ph2 = ph2;
}

/* ---------------------------------------------------------------- the arpeggiator --- */
static struct {
    int8_t held[16];
    float vels[16];
    uint8_t nheld;
    int idx, dir, cur;
    uint32_t next, gate_off;             /* sample clock; 0xFFFFFFFF none */
} arp = {.cur = -1, .dir = 1, .next = 0xFFFFFFFFu, .gate_off = 0xFFFFFFFFu};
static uint8_t ai_on;
static uint32_t ai_rng = 1;
static uint32_t arp_tile, arp_jump_ms;
static int arp_on(void) { return P.arpPattern > .5f; }
static uint32_t step_samples(void) { float b = P.arpBpm < 20.f ? 20.f : P.arpBpm; return (uint32_t)((float)OD_FS * 60.f / b / 4.f); }

static int arp_seq(int8_t *out)
{
    int8_t b[16];
    int n = arp.nheld, i, j, o, c = 0;
    for (i = 0; i < n; i++) b[i] = arp.held[i];
    for (i = 1; i < n; i++)                      /* sorted */
        for (j = i; j > 0 && b[j - 1] > b[j]; j--) { int8_t t = b[j]; b[j] = b[j - 1]; b[j - 1] = t; }
    for (o = 0; o < (int)(P.arpOct + .5f) && c < 48; o++)
        for (i = 0; i < n && c < 48; i++) out[c++] = (int8_t)(b[i] + 12 * o);
    return c;
}
static void fire_step(void)
{
    int8_t seq[48];
    int n = arp_seq(seq), pat = (int)(P.arpPattern + .5f), i, note;
    float vel = .8f;
    if (!n) return;
    if (pat == 4) i = (int)(rnd() % (uint32_t)n);
    else if (pat == 2) i = (n - 1) - (arp.idx % n);
    else if (pat == 3) {
        i = CLAMP(arp.idx, 0, n - 1);
        if (n > 1) {
            arp.idx += arp.dir;
            if (arp.idx >= n - 1) arp.dir = -1;
            if (arp.idx <= 0) arp.dir = 1;
        }
    } else i = arp.idx % n;
    if (pat != 3) arp.idx++;
    note = seq[i];
    if (arp.cur >= 0) direct_off(arp.cur);
    if (ai_on) {
        ai_rng ^= ai_rng << 13; ai_rng ^= ai_rng >> 17; ai_rng ^= ai_rng << 5;
        if ((ai_rng & 1023u) < 123u) {           /* a rest: the groove breathes */
            arp.cur = -1;
            arp.gate_off = 0xFFFFFFFFu;
            arp_tile = (arp_tile + 1u) % 8u;
            arp_jump_ms = od_ms();
            return;
        }
        if ((ai_rng & 1023u) > 870u) note += 12;   /* an octave leap */
    }
    for (i = 0; i < arp.nheld; i++)
        if (((note - arp.held[i]) % 12) == 0) { vel = arp.vels[i]; break; }
    direct_on(note, vel);
    arp.cur = note;
    arp.gate_off = sclock + (uint32_t)((float)step_samples() * CLAMP(P.arpGate, .05f, .95f));
    arp_tile = (arp_tile + 1u) % 8u;
    arp_jump_ms = od_ms();
}
static void held_add(int n, float vel)
{
    int i;
    for (i = 0; i < arp.nheld; i++)
        if (arp.held[i] == n) { arp.vels[i] = vel; return; }
    if (arp.nheld < 16) { arp.held[arp.nheld] = (int8_t)n; arp.vels[arp.nheld] = vel; arp.nheld++; }
    if (arp_on() && arp.next == 0xFFFFFFFFu) arp.next = sclock;
}
static void held_remove(int n)
{
    int i, j = 0;
    for (i = 0; i < arp.nheld; i++)
        if (arp.held[i] != n) { arp.held[j] = arp.held[i]; arp.vels[j] = arp.vels[i]; j++; }
    arp.nheld = (uint8_t)j;
    if (!arp.nheld) {
        if (arp.cur >= 0) direct_off(arp.cur);
        arp.cur = -1;
        arp.next = arp.gate_off = 0xFFFFFFFFu;
        arp.idx = 0;
        arp.dir = 1;
    }
}
static void set_arp_pattern(float v)
{
    int was = arp_on(), is, i;
    P.arpPattern = v;
    is = arp_on();
    if (was && !is) {
        if (arp.cur >= 0) direct_off(arp.cur);
        arp.cur = -1;
        arp.next = arp.gate_off = 0xFFFFFFFFu;
        for (i = 0; i < arp.nheld; i++) direct_on(arp.held[i], arp.vels[i]);
    }
    if (!was && is) {
        for (i = 0; i < arp.nheld; i++) direct_off(arp.held[i]);
        arp.idx = 0;
        arp.dir = 1;
        arp.next = arp.nheld ? sclock : 0xFFFFFFFFu;
    }
}

/* ---------------------------------------------------------------- effects --- */
#define CH_N 2656u                       /* chorus: 60 ms */
#define DL_N 26624u                      /* echo: 1.2 s at half rate (one sample of two stored) */
static int16_t ch_buf[CH_N];
static int16_t dl_l[DL_N], dl_r[DL_N];
static uint32_t ch_w, dl_w;
static uint32_t ch_ph;                   /* chorus LFO, turns as 2^32 */
static int32_t dl_lp_l, dl_lp_r, dl_hold_l, dl_hold_r;
static float dl_cur = 15000.f;           /* the echo's time in samples (glides like a tape) */
static const uint16_t COMB_T[4] = {1116, 1188, 1277, 1356}, AP_T[2] = {556, 441};
static int16_t cb_l0[1116], cb_l1[1188], cb_l2[1277], cb_l3[1356], cb_r0[1139], cb_r1[1211], cb_r2[1300], cb_r3[1379];
static int16_t ap_l0[556], ap_l1[441], ap_r0[579], ap_r1[464];
typedef struct { int16_t *b; uint16_t n, i; int32_t f; } comb_t;
typedef struct { int16_t *b; uint16_t n, i; } ap_t;
static comb_t cl[4] = {{cb_l0, 1116}, {cb_l1, 1188}, {cb_l2, 1277}, {cb_l3, 1356}};
static comb_t cr[4] = {{cb_r0, 1139}, {cb_r1, 1211}, {cb_r2, 1300}, {cb_r3, 1379}};
static ap_t al[2] = {{ap_l0, 556}, {ap_l1, 441}}, ar[2] = {{ap_r0, 579}, {ap_r1, 464}};
static inline int16_t sat16(int32_t x) { return (int16_t)CLAMP(x, -32767, 32767); }
static inline int32_t comb(comb_t *c, int32_t x)   /* feedback .84, damping .4 */
{
    int32_t y = c->b[c->i];
    c->f = (y * 19661 + c->f * 13107) >> 15;
    c->b[c->i] = sat16(x + ((c->f * 27525) >> 15));
    if (++c->i >= c->n) c->i = 0;
    return y;
}
static inline int32_t allp(ap_t *a, int32_t x)
{
    int32_t bo = a->b[a->i];
    a->b[a->i] = sat16(x + (bo >> 1));
    if (++a->i >= a->n) a->i = 0;
    return bo - x;
}
static inline int32_t frd(const int16_t *b, uint32_t w, uint32_t n, uint32_t del_q8)   /* a delay line, fractional */
{
    uint32_t d = del_q8 >> 8, fr = del_q8 & 255u;
    uint32_t i0 = (w + n - d) % n, i1 = (i0 + n - 1u) % n;
    return ((int32_t)b[i0] * (int32_t)(256u - fr) + (int32_t)b[i1] * (int32_t)fr) >> 8;
}

/* ---------------------------------------------------------------- the engine --- */
static float vol_s = .8f;               /* the volume, smoothed */
static float master_v = 1.f;            /* the MASTER pot */

void od_audio(int32_t *lr, uint32_t n)
{
    static int32_t mix[OD_BLK];
    uint32_t b, i, j;
    float drive = P.drive < 1.f ? 1.f : P.drive, pre, post, th;
    int32_t pre_q10, post_q14, cho, eco, rev, volq, peak = 0;
    pre = .28f * drive;
    th = drive > 3.f ? .995f : (float)OD_TANH[(uint32_t)CLAMP((drive + 4.f) * 64.f, 0.f, 512.f)] * (1.f / 32767.f);
    post = .85f / (th < .4f ? .4f : th);
    pre_q10 = (int32_t)(pre * 1024.f);
    post_q14 = (int32_t)(post * 16384.f);       /* (post can reach 2.1) */
    cho = (int32_t)(CLAMP(P.cho, 0.f, 1.f) * 32767.f);
    eco = (int32_t)(CLAMP(P.eco, 0.f, 1.f) * 32767.f);
    rev = (int32_t)(CLAMP(P.rev, 0.f, 1.f) * 32767.f);
    for (b = 0; b < n; b += OD_BLK) {
        uint32_t m = n - b < OD_BLK ? n - b : OD_BLK;
        float et = CLAMP(P.etime, .02f, 1.19f) * (float)OD_FS;
        vol_s += (P.vol * master_v - vol_s) * .05f;
        volq = (int32_t)(CLAMP(vol_s, 0.f, 1.f) * 32767.f);
        dl_cur += (et - dl_cur) * .012f;
        for (i = 0; i < m; i++) mix[i] = 0;
        /* the arpeggiator, at block resolution (16 samples: 0.36 ms) */
        if (arp_on()) {
            if (sclock >= arp.gate_off) { if (arp.cur >= 0) direct_off(arp.cur); arp.cur = -1; arp.gate_off = 0xFFFFFFFFu; }
            if (sclock >= arp.next) { fire_step(); arp.next = sclock + step_samples(); }
        }
        for (j = 0; j < NV; j++)
            if (vc[j].active) v_render(&vc[j], mix, m);
        for (i = 0; i < m; i++) {
            int32_t x, l, r, d1, d2, rl, rr;
            uint32_t ti, dq;
            /* the master's soft clip: tanh(x * pre) * post */
            x = (CLAMP(mix[i], -1000000, 1000000) * pre_q10) >> 9;   /* the voices' Q14 sum x pre: Q15 */
            ti = (uint32_t)CLAMP((x >> 9) + 256, 0, 512);       /* tanh over -4..4: 64 steps a unit */
            x = ((int32_t)OD_TANH[ti] * post_q14) >> 14;
            /* chorus: two taps 12 +- 6 ms in quadrature */
            ch_buf[ch_w] = sat16(x);
            ch_ph += (uint32_t)(.6f * 4294967296.f / (float)OD_FS);
            d1 = (12 * 441 / 10) * 256 + ((6 * 441 / 10) * OD_SINE[ch_ph >> 22] >> 7);
            d2 = (12 * 441 / 10) * 256 + ((6 * 441 / 10) * OD_SINE[(ch_ph + 0x40000000u) >> 22] >> 7);
            l = ((x * (32767 - (cho * 13107 >> 15))) >> 15) + ((frd(ch_buf, ch_w, CH_N, (uint32_t)d1) * cho) >> 15);
            r = ((x * (32767 - (cho * 13107 >> 15))) >> 15) + ((frd(ch_buf, ch_w, CH_N, (uint32_t)d2) * cho) >> 15);
            ch_w = (ch_w + 1u) % CH_N;
            /* echo: stereo, filtered feedback with a little cross, at half rate */
            dq = (uint32_t)(dl_cur * 128.f);                    /* (half rate: samples / 2, Q8) */
            if (dq > (DL_N - 2u) << 8) dq = (DL_N - 2u) << 8;
            rl = frd(dl_l, dl_w, DL_N, dq);
            rr = frd(dl_r, dl_w, DL_N, dq);
            dl_lp_l += (rl - dl_lp_l) >> 2;
            dl_lp_r += (rr - dl_lp_r) >> 2;
            if (sclock & 1u) {
                dl_l[dl_w] = sat16(((l + dl_hold_l) >> 1) + ((dl_lp_l * 14746) >> 15) + ((dl_lp_r * 3932) >> 15));
                dl_r[dl_w] = sat16(((r + dl_hold_r) >> 1) + ((dl_lp_r * 14746) >> 15) + ((dl_lp_l * 3932) >> 15));
                dl_w = (dl_w + 1u) % DL_N;
            } else {
                dl_hold_l = l;
                dl_hold_r = r;
            }
            l += (rl * eco) >> 15;
            r += (rr * eco) >> 15;
            /* reverb: four combs and two all-passes a side */
            if (rev > 32) {
                int32_t in = (x * 11469) >> 15, wl = 0, wr = 0, k;
                for (k = 0; k < 4; k++) { wl += comb(&cl[k], in); wr += comb(&cr[k], in); }
                wl >>= 2;
                wr >>= 2;
                wl = allp(&al[1], allp(&al[0], wl));
                wr = allp(&ar[1], allp(&ar[0], wr));
                l += ((wl * rev) >> 15) * 5 / 4;
                r += ((wr * rev) >> 15) * 5 / 4;
            }
            l = CLAMP((CLAMP(l, -65535, 65535) * volq) >> 15, -32767, 32767);
            r = CLAMP((CLAMP(r, -65535, 65535) * volq) >> 15, -32767, 32767);
            lr[2u * (b + i)] = l;
            lr[2u * (b + i) + 1u] = r;
            if (l > peak) peak = l; else if (-l > peak) peak = -l;
            sclock++;
        }
    }
    lvl_peak = (lvl_peak * 6 + peak * 4) / 10;
    lfo_view += P.lfoRate * (float)n / (float)OD_FS;
    if (lfo_view >= 1.f) lfo_view -= (float)(int32_t)lfo_view;
}

/* ---------------------------------------------------------------- the patch --- */
static void preset_load(int i)
{
    const od_preset_t *p;
    float keep_vol = P.vol;
    preset_idx = (i % OD_NPRESETS + OD_NPRESETS) % OD_NPRESETS;
    p = &OD_PRESETS[preset_idx];
    P.wave = p->wave; P.detune = p->detune; P.oscMix = p->oscMix; P.drive = p->drive; P.cutoff = p->cutoff;
    P.reso = p->reso; P.fEnvAmt = p->fEnvAmt; P.keyTrk = p->keyTrk; P.a = p->a; P.d = p->d; P.s = p->s; P.r = p->r;
    P.lfoRate = p->lfoRate; P.lfoPitch = p->lfoPitch; P.lfoCut = p->lfoCut; P.fDecay = p->fDecay;
    P.arpBpm = p->arpBpm; P.arpOct = p->arpOct; P.arpGate = p->arpGate;
    P.cho = p->cho; P.eco = p->eco; P.etime = p->etime; P.rev = p->rev;
    P.vol = keep_vol > 0.f ? keep_vol : p->vol;
    set_arp_pattern(p->arpPattern);
}
static void synth_init(void)
{
    P.vol = .8f;
    preset_load(0);
}
static void synth_panic(void)
{
    uint32_t i;
    arp.nheld = 0;
    arp.cur = -1;
    arp.next = arp.gate_off = 0xFFFFFFFFu;
    for (i = 0; i < NV; i++) v_off(&vc[i]);
}
static int voices_active(void)
{
    uint32_t i;
    int c = 0;
    for (i = 0; i < NV; i++) c += vc[i].active;
    return c;
}
