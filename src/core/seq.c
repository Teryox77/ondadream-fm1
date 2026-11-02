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
    float e1, e2;                        /* its two decays, e^(-t / tau), a multiply a sample */
} drum_t;
/* e^(-1 / (tau fs)) for each decay: KICK pitch .038 s, level .42; TOM .075, .3; SNARE tone .085, noise .19;
 * CLHAT .09 x .22; OPHAT .42 x .32; CLAP tail .13; RIM .028 */
#define KD_KF .9994034767150879f
#define KD_KA .9999459981918335f
#define KD_TF .9996976852416992f
#define KD_TA .9999244213104248f
#define KD_ST .9997332692146301f
#define KD_SN .9998806715011597f
#define KD_CH .9988554120063782f
#define KD_OH .9998313188552856f
#define KD_CL .9998255968093872f
#define KD_RM .9991905093193054f
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
    d->e1 = d->e2 = 1.f;
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
            float f = (42.f + 165.f * d->e1) * tm, body, sub, amp, click;
            d->ph += f * isr; if (d->ph >= 1.f) d->ph -= 1.f;
            body = sin_t(d->ph);
            sub = sin_t(d->ph * 2.f) * .18f;                       /* the second harmonic: the body */
            amp = d->e2;
            d->e1 *= KD_KF;
            d->e2 *= KD_KA;
            click = t < .002f ? (1.f - t / .002f) * (drnd(d) * 2.f - 1.f) * .7f : 0.f;   /* the attack */
            s = SAT((body + sub) * amp, 1.8f, 1.05618256f) + click;
            dur = .55f;
            break;
        }
        case DR_TOMLO: case DR_TOMHI: {
            float base = d->type == DR_TOMLO ? 125.f : 225.f, f = (base * .55f + base * d->e1) * tm, click;
            d->ph += f * isr; if (d->ph >= 1.f) d->ph -= 1.f;
            click = t < .0015f ? (1.f - t / .0015f) * (drnd(d) * 2.f - 1.f) * .3f : 0.f;
            s = SAT(sin_t(d->ph) * d->e2, 1.4f, 1.12949471f) + click;
            d->e1 *= KD_TF;
            d->e2 *= KD_TA;
            dur = .4f;
            break;
        }
        case DR_SNARE: {
            float tone, raw, noise;
            d->ph += 180.f * tm * isr; if (d->ph >= 1.f) d->ph -= 1.f;
            d->ph2 += 330.f * tm * isr; if (d->ph2 >= 1.f) d->ph2 -= 1.f;
            tone = (sin_t(d->ph) * .6f + sin_t(d->ph2) * .4f) * d->e1;
            raw = drnd(d) * 2.f - 1.f;
            d->hp = raw - d->pn + .96f * d->hp; d->pn = raw;
            d->hp2 = d->hp - d->pn2 + .6f * d->hp2; d->pn2 = d->hp;   /* a second stage: drier noise */
            noise = d->hp2 * d->e2;
            d->e1 *= KD_ST;
            d->e2 *= KD_SN;
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
            s = d->hp2 * d->e1 * .55f;
            d->e1 *= d->type == DR_CLHAT ? KD_CH : KD_OH;
            break;
        }
        case DR_CLAP: {
            static const float B[4] = {0.f, .011f, .022f, .034f};
            float raw = drnd(d) * 2.f - 1.f, env = 0.f;
            d->hp = raw - d->pn + .92f * d->hp; d->pn = raw;
            for (k = 0; k < 4u; k++)
                if (t >= B[k] && t < B[k] + .009f) { float e = 1.f - (t - B[k]) / .009f; if (e > env) env = e; }
            if (t >= .045f) {
                float e;
                if (d->e2 == 1.f) d->e2 = exp_f(-(t - .045f) * (1.f / .13f));   /* the tail's first sample */
                e = d->e2 * .75f;
                d->e2 *= KD_CL;
                if (e > env) env = e;
            }
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
            s = SAT((sq1 * .5f + sq2 * .5f) * d->e1, 1.5f, 1.10479139f) * .42f;
            d->e1 *= KD_RM;
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

/* ---------------------------------------------------------------- MIDI: the settings, the clock --- */
static struct { uint8_t in_ch, out_ch, sync, clk_out; } mset = {0, 1, 0, 0};   /* in 0 = all; out 0 = off */
static uint8_t settings_dirty;
static uint32_t settings_ms;
/* the clock that comes in (sync EXT): 24 a beat, its time on the sample clock, a sixteenth every 6 */
static struct { uint32_t last, cnt; float tick; } xclk;   /* tick: samples per clock, averaged; 0 none yet */
static float clk_acc;                    /* the clock that goes out: samples to its next tick */
static int ext_active(void)              /* following a clock that is there now (else the own tempo, as INT) */
{
    return mset.sync && xclk.tick > 0.f && sclock - xclk.last < (uint32_t)OD_FS / 2u;
}
static float ext_step(void) { return xclk.tick * 6.f; }
static uint32_t seq_step_samples(void)
{
    uint32_t s = ext_active() ? (uint32_t)(ext_step() + .5f) : (uint32_t)((float)OD_FS * 60.f / (float)sq.bpm / 4.f + .5f);
    return s ? s : 1u;
}
static int seq_bpm_shown(void) { return ext_active() ? (int)((float)OD_FS * 60.f / (xclk.tick * 24.f) + .5f) : sq.bpm; }
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
static void clk_send(uint8_t st) { if (mset.clk_out && !ext_active()) od_midi_out(st, 0, 0); }
static void seq_play(void)
{
    if (sq.playing) return;
    sq.playing = 1;
    sq.step = 0;
    sq.next = ext_active() ? NONE : sclock;      /* (following a clock: the next sixteenth of it) */
    xclk.cnt = 0;
    clk_send(0xFA);
    clk_acc = 0.f;                               /* the clock out: a tick with the first step */
}
static void seq_stop(void)
{
    if (sq.playing) clk_send(0xFC);
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

/* the clock and the transport that come in (from od_midi, before the audio): with sync EXT the steps of the
 * sequencer and of the arpeggiator fall on every sixth clock */
static void seq_realtime(uint8_t st)
{
    if (!mset.sync) return;
    if (st == 0xF8u) {
        uint32_t d = sclock - xclk.last;
        if (xclk.last && d > 0u && d < (uint32_t)OD_FS / 2u)
            xclk.tick = xclk.tick > 0.f ? xclk.tick + ((float)d - xclk.tick) * .125f : (float)d;
        xclk.last = sclock ? sclock : 1u;
        if (xclk.cnt % 6u == 0u && xclk.tick > 0.f) {   /* a sixteenth: the steps on it */
            int32_t half = (int32_t)(ext_step() * .5f);
            if (sq.playing && (sq.next == NONE || (int32_t)(sq.next - sclock) <= half)) sq.next = sclock;
            if (arp_on() && arp.next != 0xFFFFFFFFu && (int32_t)(arp.next - sclock) <= half) arp.next = sclock;
        }
        xclk.cnt++;
    } else if (st == 0xFAu) {                    /* START: from the first step, on the next clock */
        seq_stop();
        sq.playing = 1;
        sq.step = 0;
        sq.next = NONE;
        xclk.cnt = 0;
    } else if (st == 0xFBu) {                    /* CONTINUE */
        if (!sq.playing) { sq.playing = 1; sq.next = NONE; xclk.cnt = 0; }
    } else if (st == 0xFCu) {
        seq_stop();
    }
}

/* ---- the engine's side (od_audio) ---- */
static struct { int8_t note; uint32_t ms; } aud[4];   /* the steps' preview notes: off after 180 ms */
static void key_off(int n);
static void seq_control(void)
{
    uint32_t r, k, now = od_ms();
    if (sq.playing && sq.next == NONE && !ext_active()) sq.next = sclock;   /* the clock went away: on our own */
    for (r = 1; r < NTR; r++)
        if (tp_for[r] != tr[r].preset) {
            patch_voice(&tp[r], preset_at(tr[r].preset));
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
        sq.next = now + seq_step_samples();      /* (following a clock: its next sixteenth moves this onto it) */
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

/* the clock that goes out (CLOCK OUT on, not following one): 24 a beat at the sequencer's tempo, each control block */
static void clk_tick(uint32_t m)
{
    if (!mset.clk_out || ext_active()) return;
    clk_acc -= (float)m;
    while (clk_acc <= 0.f) {
        od_midi_out(0xF8, 0, 0);
        clk_acc += (float)OD_FS * 60.f / ((float)sq.bpm * 24.f);
    }
}

/* the store's image: "ODU2", the count, the MIDI settings (4 bytes), then each preset's 25 numbers (the order of
 * od_preset_t), little-endian ("ODU1": without the settings) */
static void put32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
int od_store_dirty(void)                  /* a SAVE at once; the settings once they stay put for 2 s */
{
    if (settings_dirty && od_ms() - settings_ms >= 2000u) { settings_dirty = 0; store_dirty = 1; }
    if (!store_dirty) return 0;
    store_dirty = 0;
    return 1;
}
uint32_t od_store_image(uint8_t *b, uint32_t max)
{
    int k, j;
    if (max < 12u + (uint32_t)n_user * 100u) return 0;
    put32(b, 0x3255444Fu);
    put32(b + 4, (uint32_t)n_user);
    b[8] = mset.in_ch; b[9] = mset.out_ch; b[10] = mset.sync; b[11] = mset.clk_out;
    for (k = 0; k < n_user; k++)
        for (j = 0; j < 25; j++) {
            union { float f; uint32_t u; } c;
            c.f = (&user_p[k].wave)[j];
            put32(b + 12 + k * 100 + j * 4, c.u);
        }
    return 12u + (uint32_t)n_user * 100u;
}
int od_store_load(const uint8_t *b, uint32_t n)   /* 0: taken; -1: not a store (the presets stay as they are) */
{
    /* each number in a sane range (the knobs' and the presets' with room): a damaged store must not reach the voices */
    static const float LO[25] = {0, -48, 0, 0, 20, 0, 0, 0, .0005f, .001f, 0, .001f, .01f, 0, 0, .005f, 0, 0, 20, 1, 0, 0, 0, .02f, 0};
    static const float HI[25] = {3, 48, 1, 16, 20000, 1, 1, 1, 10, 10, 1, 10, 40, 100, 1, 10, 1, 4, 300, 3, 1, 1, 1, 1.5f, 1};
    uint32_t cnt, k, j, h;
    if (n < 8u || (get32(b) != 0x3155444Fu && get32(b) != 0x3255444Fu)) return -1;
    h = get32(b) == 0x3255444Fu ? 12u : 8u;
    cnt = get32(b + 4);
    if (cnt > OD_MAXUSER || n < h + cnt * 100u) return -1;
    if (h == 12u && (b[8] > 16u || b[9] > 16u || b[10] > 1u || b[11] > 1u)) return -1;
    for (k = 0; k < cnt; k++)
        for (j = 0; j < 25u; j++) {
            union { float f; uint32_t u; } c;
            c.u = get32(b + h + k * 100u + j * 4u);
            if (!(c.f >= LO[j] - .0001f && c.f <= HI[j] + .0001f)) return -1;   /* (NaN fails too) */
        }
    for (k = 0; k < cnt; k++) {
        for (j = 0; j < 25u; j++) {
            union { float f; uint32_t u; } c;
            c.u = get32(b + h + k * 100u + j * 4u);
            (&user_p[k].wave)[j] = c.f;
        }
        user_name_set((int)k);
    }
    n_user = (int)cnt;
    if (h == 12u) { mset.in_ch = b[8]; mset.out_ch = b[9]; mset.sync = b[10]; mset.clk_out = b[11]; }
    return 0;
}
