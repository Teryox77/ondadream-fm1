/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The sequencer, as OndaDream's preview has it: 4 tracks x 16 steps on the sample clock. Track 0 is the rhythm (the
 * 808-style drum machine, its ten rhythms and their layers; shown last, as "D"), tracks 1..3 play a preset each from
 * a 24-note piano roll (two octaves from C2), four voices a track. Each track has its level, pan and reverb send.
 * The drums are computed in float, sample by sample, as the preview's DrumVoice (the FM-1 has an FPU). */

/* ---------------------------------------------------------------- math --- */
static float sin_t(float turns)          /* sin(2 pi turns), the table interpolated */
{
    float f = turns - (float)(int32_t)turns, x;
    uint32_t i;
    if (f < 0.f) f += 1.f;
    x = f * 1024.f;
    i = (uint32_t)x;
    if (i > 1023u) i = 1023u;
    return ((float)OD_SINE[i] + ((float)OD_SINE[i + 1u] - (float)OD_SINE[i]) * (x - (float)i)) * (1.f / 32767.f);
}
static float exp_f(float x) { return od_exp2(x * 1.44269504f); }
static float tanh_f(float x)             /* the table over -4..4 (64 steps a unit), interpolated; beyond, 1 - 2e^-2|x| */
{
    float a = x < 0.f ? -x : x, y, u;
    uint32_t i;
    if (a >= 4.f) y = 1.f - 2.f * exp_f(-2.f * a);
    else {
        u = (a + 4.f) * 64.f;
        i = (uint32_t)u;
        y = ((float)OD_TANH[i] + ((float)OD_TANH[i + 1u] - (float)OD_TANH[i]) * (u - (float)i)) * (1.f / 32767.f);
    }
    return x < 0.f ? -y : y;
}

/* ---------------------------------------------------------------- the drums --- */
enum { DR_KICK, DR_SNARE, DR_CLHAT, DR_OPHAT, DR_CLAP, DR_TOMLO, DR_TOMHI, DR_RIM, DR_N };
static const char *const DRUM_ABBR[DR_N] = {"KICK", "SNAR", "CHAT", "OHAT", "CLAP", "TOML", "TOMH", "RIM"};
static const float HAT_F[6] = {205.3f, 304.4f, 369.6f, 522.7f, 800.f, 900.f};   /* the 808's six square oscillators */
typedef struct {
    uint8_t active, type;
    uint32_t rs;                         /* its noise (each hit its own: the tests compare them with the preview's) */
    float tm;                            /* tune */
    uint32_t t;                          /* samples since the hit */
    float ph, ph2, hp, hp2, pn, pn2;
    float hph[6];
} drum_t;
#define DRUM_POLY 4u
static drum_t dv[DRUM_POLY];
static uint32_t drum_hits;
static float drnd(drum_t *d)             /* Math.random() */
{
    d->rs ^= d->rs << 13; d->rs ^= d->rs >> 17; d->rs ^= d->rs << 5;
    return (float)(d->rs >> 8) * (1.f / 16777216.f);
}
/* sat(x, amt) = tanh(x amt) / tanh(amt) */
#define SAT(x, amt, inv) (tanh_f((x) * (amt)) * (inv))
static void drum_trigger(drum_t *d, uint8_t type, float tm)
{
    uint32_t k;
    d->active = 1;
    d->type = type;
    d->tm = tm;
    d->t = 0;
    d->rs = ++drum_hits * 0x9E3779B9u | 1u;
    d->ph = d->ph2 = d->hp = d->hp2 = d->pn = d->pn2 = 0.f;
    for (k = 0; k < 6u; k++) d->hph[k] = 0.f;
}
static void drum_render(drum_t *d, int32_t *out, uint32_t n, float gain)
{
    const float isr = 1.f / (float)OD_FS, tm = d->tm, g14 = .9f * gain * 16384.f;
    uint32_t i, k;
    if (!d->active) return;
    for (i = 0; i < n; i++) {
        float t = (float)d->t * isr, s = 0.f, dur = .3f;
        switch (d->type) {
        case DR_KICK: {
            float f = (42.f + 165.f * exp_f(-t / .038f)) * tm, body, sub, amp, click;
            d->ph += f * isr; if (d->ph >= 1.f) d->ph -= 1.f;
            body = sin_t(d->ph);
            sub = sin_t(d->ph * 2.f) * .18f;                       /* the second harmonic: the body */
            amp = exp_f(-t / .42f);
            click = t < .002f ? (1.f - t / .002f) * (drnd(d) * 2.f - 1.f) * .7f : 0.f;   /* the attack */
            s = SAT((body + sub) * amp, 1.8f, 1.05618256f) + click;
            dur = .55f;
            break;
        }
        case DR_TOMLO: case DR_TOMHI: {
            float base = d->type == DR_TOMLO ? 125.f : 225.f, f = (base * .55f + base * exp_f(-t / .075f)) * tm, click;
            d->ph += f * isr; if (d->ph >= 1.f) d->ph -= 1.f;
            click = t < .0015f ? (1.f - t / .0015f) * (drnd(d) * 2.f - 1.f) * .3f : 0.f;
            s = SAT(sin_t(d->ph) * exp_f(-t / .3f), 1.4f, 1.12949471f) + click;
            dur = .4f;
            break;
        }
        case DR_SNARE: {
            float tone, raw, noise;
            d->ph += 180.f * tm * isr; if (d->ph >= 1.f) d->ph -= 1.f;
            d->ph2 += 330.f * tm * isr; if (d->ph2 >= 1.f) d->ph2 -= 1.f;
            tone = (sin_t(d->ph) * .6f + sin_t(d->ph2) * .4f) * exp_f(-t / .085f);
            raw = drnd(d) * 2.f - 1.f;
            d->hp = raw - d->pn + .96f * d->hp; d->pn = raw;
            d->hp2 = d->hp - d->pn2 + .6f * d->hp2; d->pn2 = d->hp;   /* a second stage: drier noise */
            noise = d->hp2 * exp_f(-t / .19f);
            s = SAT(tone * .55f + noise * .95f, 1.6f, 1.08498874f);
            dur = .34f;
            break;
        }
        case DR_CLHAT: case DR_OPHAT: {
            float hs = 0.f;
            for (k = 0; k < 6u; k++) {
                d->hph[k] += HAT_F[k] * tm * isr; if (d->hph[k] >= 1.f) d->hph[k] -= 1.f;
                hs += d->hph[k] < .5f ? 1.f : -1.f;
            }
            hs /= 6.f;
            d->hp = hs - d->pn + .92f * d->hp; d->pn = hs;
            d->hp2 = d->hp - d->pn2 + .85f * d->hp2; d->pn2 = d->hp;   /* two high-pass stages: the metal */
            dur = d->type == DR_CLHAT ? .09f : .42f;
            s = d->hp2 * exp_f(-t / (dur * (d->type == DR_CLHAT ? .22f : .32f))) * .55f;
            break;
        }
        case DR_CLAP: {
            static const float B[4] = {0.f, .011f, .022f, .034f};
            float raw = drnd(d) * 2.f - 1.f, env = 0.f;
            d->hp = raw - d->pn + .92f * d->hp; d->pn = raw;
            for (k = 0; k < 4u; k++)
                if (t >= B[k] && t < B[k] + .009f) { float e = 1.f - (t - B[k]) / .009f; if (e > env) env = e; }
            if (t >= .045f) { float e = exp_f(-(t - .045f) / .13f) * .75f; if (e > env) env = e; }
            s = SAT(d->hp * env, 1.3f, 1.16046550f) * .8f;
            dur = .3f;
            break;
        }
        default: {                                                  /* RIM */
            float sq1, sq2;
            d->ph += 587.f * tm * isr; if (d->ph >= 1.f) d->ph -= 1.f;
            d->ph2 += 845.f * tm * isr; if (d->ph2 >= 1.f) d->ph2 -= 1.f;
            sq1 = d->ph < .5f ? 1.f : -1.f;
            sq2 = d->ph2 < .5f ? 1.f : -1.f;
            s = SAT((sq1 * .5f + sq2 * .5f) * exp_f(-t / .028f), 1.5f, 1.10479139f) * .42f;
            dur = .08f;
        }
        }
        out[i] += (int32_t)(s * g14);
        d->t++;
        if (t > dur) { d->active = 0; break; }
    }
}

/* ---------------------------------------------------------------- the rhythms --- */
typedef struct { uint8_t type; float tune; uint16_t steps; } layer_t;   /* steps: bit c = step c */
static const struct { const char *name; layer_t l[4]; } RHYTHMS[10] = {
    {"BOSSA NOVA", {{DR_KICK, .95f, 0x0441}, {DR_RIM, 1.f, 0x1010}, {DR_CLHAT, 1.f, 0x5555}, {DR_RIM, 1.2f, 0x0808}}},
    {"NEW AGE", {{DR_KICK, .8f, 0x0101}, {DR_TOMLO, 1.f, 0x0400}, {DR_OPHAT, 1.f, 0x4444}, {DR_RIM, .9f, 0x1010}}},
    {"DOWNTEMPO", {{DR_KICK, 1.f, 0x0401}, {DR_SNARE, .9f, 0x1010}, {DR_CLHAT, 1.f, 0x5555}, {DR_CLAP, 1.f, 0x8080}}},
    {"AMBIENT MIST", {{DR_KICK, .75f, 0x0001}, {DR_TOMLO, .9f, 0x0200}, {DR_OPHAT, 1.f, 0x1010}, {DR_RIM, .85f, 0x4000}}},
    {"PSYCH GROOVE", {{DR_KICK, 1.f, 0x0909}, {DR_SNARE, 1.f, 0x1010}, {DR_CLHAT, 1.f, 0x5555}, {DR_RIM, 1.f, 0x4040}}},
    {"DUB SPACE", {{DR_KICK, .85f, 0x0401}, {DR_SNARE, .9f, 0x0010}, {DR_OPHAT, 1.f, 0x4444}, {DR_CLAP, 1.f, 0x2000}}},
    {"KRAUT PULSE", {{DR_KICK, 1.f, 0x1111}, {DR_SNARE, 1.f, 0x4444}, {DR_CLHAT, 1.f, 0x5555}, {DR_RIM, 1.f, 0x0000}}},
    {"CHILLWAVE", {{DR_KICK, .9f, 0x0481}, {DR_SNARE, .85f, 0x1010}, {DR_OPHAT, 1.f, 0x5555}, {DR_CLAP, .9f, 0x0808}}},
    {"TRIBAL AMBIENT", {{DR_TOMLO, 1.f, 0x2449}, {DR_TOMHI, 1.f, 0x0100}, {DR_RIM, 1.f, 0x1224}, {DR_CLHAT, .9f, 0x4000}}},
    {"COSMIC BREATH", {{DR_KICK, .8f, 0x0001}, {DR_SNARE, .85f, 0x0100}, {DR_OPHAT, 1.f, 0x1010}, {DR_RIM, 1.1f, 0x4444}}},
};
#define NRHY 10

/* ---------------------------------------------------------------- the tracks --- */
#define PIANO_ROWS 24
#define SEQ_LOW 36                       /* the piano roll's lowest note: the preview's kbBase 48 - 12 */
#define NONE 0xFFFFFFFFu
static struct {
    float vol, fx, pan;
    uint8_t mute;
    int preset;                          /* tracks 1..3 */
    uint16_t grid[PIANO_ROWS];           /* tracks 1..3: grid[p] bit c: note SEQ_LOW + p on step c */
    int sel_note;
} tr[NTR];
static int rhythm_idx;                   /* track 0 */
static uint8_t nlay;
static layer_t lay[DR_N + 4];            /* the rhythm's layers, and one for each instrument the steps add */
static voice_t tv[NTR][4];               /* the synth tracks' voices (tv[0] unused) */
static patch_t tp[NTR];
static int tp_for[NTR] = {-1, -1, -1, -1};
static struct {
    uint8_t playing, step;
    int8_t vis;                          /* the step shown, -1 none */
    int bpm;
    uint32_t next, gate[NTR];
    voice_t *rv[NTR][4];                 /* the voices a track sounds now (chords) */
    uint8_t nrv[NTR];
} sq = {0, 0, -1, 120, NONE, {NONE, NONE, NONE, NONE}};
static int sel_track = 1, sel_drum;
static uint8_t rec_on;

static void set_rhythm(int i)
{
    uint32_t k;
    rhythm_idx = (i % NRHY + NRHY) % NRHY;
    nlay = 0;
    for (k = 0; k < 4u; k++) lay[k] = RHYTHMS[rhythm_idx].l[k];
    nlay = 4;
    sel_drum = lay[0].type;
}
static void seq_init(void)
{
    static const int PRE[NTR] = {0, 4, 9, 16};
    static const float PAN[NTR] = {0.f, -.25f, .25f, 0.f};
    static const uint16_t G[NTR] = {0, 0x0101, 0x1010, 0x0404};
    uint32_t r;
    for (r = 0; r < NTR; r++) {
        tr[r].vol = .85f;
        tr[r].fx = r ? .3f : .25f;
        tr[r].pan = PAN[r];
        tr[r].preset = PRE[r];
        tr[r].sel_note = 12;
        tr[r].grid[12] = G[r];           /* a few notes to start with, not silent */
    }
    set_rhythm(4);
}
static int drum_has(int type, int col)
{
    uint32_t k;
    for (k = 0; k < nlay; k++)
        if (lay[k].type == type && (lay[k].steps >> col & 1u)) return 1;
    return 0;
}
static int drum_toggle(int type, int col)   /* 1: now on */
{
    uint32_t k;
    if (drum_has(type, col)) {
        for (k = 0; k < nlay; k++)
            if (lay[k].type == type) lay[k].steps &= (uint16_t)~(1u << col);
        return 0;
    }
    for (k = 0; k < nlay; k++)
        if (lay[k].type == type) break;
    if (k == nlay) {
        if (nlay >= sizeof lay / sizeof lay[0]) return 0;
        lay[k].type = (uint8_t)type;
        lay[k].tune = 1.f;
        lay[k].steps = 0;
        nlay++;
    }
    lay[k].steps |= (uint16_t)(1u << col);
    return 1;
}
static int track_on_at(int t, int col)
{
    int p;
    if (!t) {
        uint32_t k;
        for (k = 0; k < nlay; k++)
            if (lay[k].steps >> col & 1u) return 1;
        return 0;
    }
    for (p = 0; p < PIANO_ROWS; p++)
        if (tr[t].grid[p] >> col & 1u) return 1;
    return 0;
}
static int sel_step_on(int t, int col)
{
    return t ? (int)(tr[t].grid[tr[t].sel_note] >> col & 1u) : drum_has(sel_drum, col);
}

static uint32_t seq_step_samples(void)
{
    uint32_t s = (uint32_t)((float)OD_FS * 60.f / (float)sq.bpm / 4.f + .5f);
    return s ? s : 1u;
}
static void seq_release_row(int r)
{
    uint32_t k;
    for (k = 0; k < sq.nrv[r]; k++) v_off(sq.rv[r][k]);
    sq.nrv[r] = 0;
    sq.gate[r] = NONE;
}
static void seq_release_all(void)
{
    uint32_t k;
    int r;
    for (r = 1; r < (int)NTR; r++) seq_release_row(r);
    for (k = 0; k < DRUM_POLY; k++) dv[k].active = 0;
}
static drum_t *drum_free(void)
{
    uint32_t k, b = 0;
    for (k = 0; k < DRUM_POLY; k++)
        if (!dv[k].active) return &dv[k];
    for (k = 1; k < DRUM_POLY; k++)              /* steal the oldest hit (the later one on a tie, as the preview) */
        if (!(dv[b].t > dv[k].t)) b = k;
    return &dv[b];
}
static void seq_fire(void)
{
    int col = sq.step % 16, r, p;
    uint32_t k;
    for (r = 0; r < (int)NTR; r++) {
        if (tr[r].mute) continue;
        if (!r) {
            for (k = 0; k < nlay; k++)
                if (lay[k].steps >> col & 1u) drum_trigger(drum_free(), lay[k].type, lay[k].tune);
        } else {
            voice_t *fresh[4];
            uint32_t nf = 0, j;
            seq_release_row(r);                  /* the track's notes before (chords too) */
            for (p = 0; p < PIANO_ROWS; p++) {
                voice_t *v = 0;
                if (!(tr[r].grid[p] >> col & 1u)) continue;
                for (k = 0; k < 4u && !v; k++) {
                    int used = 0;
                    for (j = 0; j < nf; j++) used |= fresh[j] == &tv[r][k];
                    if (!tv[r][k].active && !used) v = &tv[r][k];
                }
                for (k = 0; k < 4u && !v; k++) {
                    int used = 0;
                    for (j = 0; j < nf; j++) used |= fresh[j] == &tv[r][k];
                    if (!used) v = &tv[r][k];
                }
                if (!v) v = &tv[r][0];
                v_on(v, SEQ_LOW + p, .8f);
                if (nf < 4u) fresh[nf++] = v;
            }
            if (nf) {
                for (j = 0; j < nf; j++) sq.rv[r][j] = fresh[j];
                sq.nrv[r] = (uint8_t)nf;
                sq.gate[r] = sclock + (uint32_t)((float)seq_step_samples() * .62f + .5f);
            }
        }
    }
    sq.vis = (int8_t)col;
    sq.step = (uint8_t)((sq.step + 1u) % 16u);
}
static void seq_play(void)
{
    if (sq.playing) return;
    sq.playing = 1;
    sq.step = 0;
    sq.next = sclock;
}
static void seq_stop(void)
{
    sq.playing = 0;
    sq.next = NONE;
    sq.vis = -1;
    seq_release_all();
}
static void seq_mute(int t, int m)
{
    if (m == tr[t].mute) return;
    tr[t].mute = (uint8_t)m;
    if (m && t > 0) seq_release_row(t);
}

/* ---- the engine's side (od_audio) ---- */
static struct { int8_t note; uint32_t ms; } aud[4];   /* the steps' preview notes: off after 180 ms */
static void key_off(int n);
static void seq_control(void)
{
    uint32_t r, k, now = od_ms();
    for (r = 1; r < NTR; r++)
        if (tp_for[r] != tr[r].preset) {
            patch_voice(&tp[r], &OD_PRESETS[tr[r].preset]);
            tp_for[r] = tr[r].preset;
        }
    for (k = 0; k < 4u; k++)
        if (aud[k].note && now - aud[k].ms >= 180u) { key_off(aud[k].note); aud[k].note = 0; }
}
static void seq_render(int32_t (*trk)[OD_BLK], uint32_t at, uint32_t seg)
{
    uint32_t r, k;
    for (k = 0; k < DRUM_POLY; k++)
        if (dv[k].active) drum_render(&dv[k], trk[0] + at, seg, tr[0].vol);
    for (r = 1; r < NTR; r++)
        for (k = 0; k < 4u; k++)
            if (tv[r][k].active) v_render(&tv[r][k], &tp[r], tr[r].vol, trk[r] + at, seg);
}
static uint32_t seq_seg(uint32_t now, uint32_t seg)
{
    uint32_t r;
    if (sq.playing && sq.next != NONE) seg = seg_lim(seg, sq.next, now);
    for (r = 1; r < NTR; r++)
        if (sq.gate[r] != NONE) seg = seg_lim(seg, sq.gate[r], now);
    return seg;
}
static void seq_events(uint32_t now)
{
    uint32_t r;
    if (sq.playing && sq.next != NONE && (int32_t)(now - sq.next) >= 0) {
        seq_fire();
        sq.next = now + seq_step_samples();
    }
    for (r = 1; r < NTR; r++)
        if (sq.gate[r] != NONE && (int32_t)(now - sq.gate[r]) >= 0) seq_release_row((int)r);
}
static void seq_gains(int32_t *pl, int32_t *pr, int32_t *fx)   /* Q14: cos / sin of (pan + 1) pi / 4, the send */
{
    uint32_t r;
    for (r = 0; r < NTR; r++) {
        float a = (CLAMP(tr[r].pan, -1.f, 1.f) + 1.f) * .125f;
        pl[r] = (int32_t)(sin_t(a + .25f) * 16384.f + .5f);
        pr[r] = (int32_t)(sin_t(a) * 16384.f + .5f);
        fx[r] = (int32_t)(CLAMP(tr[r].fx, 0.f, 1.f) * 16384.f + .5f);
    }
}
