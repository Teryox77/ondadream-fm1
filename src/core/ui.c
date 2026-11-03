/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The screen and the panel: OndaDream's pages as the web preview has them (a header line, the scene, the four knob
 * cards), the creatures, the FX faces, the splash; the FM-1's controls mapped onto them. */

/* ---------------------------------------------------------------- palette --- */
#define C_BG 0x0841u                    /* #0a0b0f */
static uint16_t C_INK, C_FAINT, C_BLUE, C_GREEN, C_ORANGE, C_RED, C_WHITE, C_KNOB3, C_CARD, C_CARDHOT, C_LABEL;
static uint16_t ACCENTS[8];
static uint16_t KNOB_COL[4];
static void pal_init(void)
{
    static const uint32_t AC[8] = {0x3E7BFF, 0x00C878, 0xFF7A1A, 0xFF3355, 0xB26BFF, 0x29D2E0, 0x9AE000, 0xFF6BD5};
    int i;
    C_INK = hexc(0xC3C8CF); C_FAINT = hexc(0x6D727A); C_BLUE = hexc(0x3E7BFF); C_GREEN = hexc(0x00C878);
    C_ORANGE = hexc(0xFF7A1A); C_RED = hexc(0xFF3355); C_WHITE = hexc(0xEDF0F4); C_KNOB3 = hexc(0xE8EBEF);
    C_CARD = hexc(0x121418); C_CARDHOT = hexc(0x1C1F24); C_LABEL = hexc(0x4B4F55);
    for (i = 0; i < 8; i++) ACCENTS[i] = hexc(AC[i]);
    KNOB_COL[0] = C_BLUE; KNOB_COL[1] = C_GREEN; KNOB_COL[2] = C_KNOB3; KNOB_COL[3] = C_ORANGE;
}

/* ---------------------------------------------------------------- state --- */
enum { PG_SYNTH, PG_FILTER, PG_ENV, PG_MOD, PG_ARP, PG_FX, PG_SEQ, PG_MIX, PG_GLO, PG_N };
static const char *const PG_TITLE[PG_N] = {"HOME", "FILTER", "ENVELOPE", "LFO", "ARPEGGIO", "EFFECTS", "SEQUENCER", "MIXER",
    "MIDI"};
static int page = PG_SYNTH;
static uint8_t hold;
static int oct_shift;
static int knob_touch = -1;
static uint32_t knob_touch_ms, toast_ms, boot_ms;
static char toast_txt[24];
static uint32_t keys_down;               /* the panel's keys held (bit k) */
static int32_t batt_raw = -1;            /* the platform's reading, filtered; -1 none (the browser: full) */
static int batt_lvl = 3;
static uint8_t batt_usb;
static uint8_t midi_held[128];
#define KB_BASE 53                       /* key 0 = F3 */
#define SPLASH_MS 1200u

static void toast(const char *s)
{
    int i;
    for (i = 0; s[i] && i < 23; i++) toast_txt[i] = s[i];
    toast_txt[i] = 0;
    toast_ms = od_ms() | 1u;
}

/* ---------------------------------------------------------------- formatting --- */
static char *fmt_i(char *b, int32_t v)
{
    char t[12];
    int n = 0, neg = v < 0;
    uint32_t u = neg ? (uint32_t)-v : (uint32_t)v;
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (neg) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
    return b;
}
static char *fmt_f(char *b, float v, int dec)   /* v with dec decimals */
{
    int32_t m = dec == 2 ? 100 : dec == 1 ? 10 : 1, x = (int32_t)(v * (float)m + (v < 0.f ? -.5f : .5f));
    if (x < 0) { *b++ = '-'; x = -x; }
    b = fmt_i(b, x / m);
    if (dec) {
        int32_t f = x % m;
        *b++ = '.';
        if (dec == 2 && f < 10) *b++ = '0';
        b = fmt_i(b, f);
    }
    return b;
}
static char *cat(char *b, const char *s) { while (*s) *b++ = *s++; *b = 0; return b; }
static void time_txt(char *b, float s) { if (s < 1.f) cat(fmt_i(b, (int32_t)(s * 1000.f + .5f)), " ms"); else cat(fmt_f(b, s, 2), " s"); }
static void hz_txt(char *b, float f) { if (f < 1000.f) cat(fmt_i(b, (int32_t)(f + .5f)), " Hz"); else cat(fmt_f(b, f / 1000.f, 1), " kHz"); }
static void pct_txt(char *b, float v) { cat(fmt_i(b, (int32_t)(v * 100.f + .5f)), " %"); }
static const char *const WAVE_NAMES[4] = {"SAW", "SQUARE", "TRI", "SINE"};
static const char *const ARP_NAMES[5] = {"OFF", "UP", "DOWN", "UP-DN", "RANDOM"};
static const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static void note_txt(char *b, int n) { fmt_i(cat(b, NOTE_NAMES[n % 12]), n / 12 - 1); }   /* midiName */

static float exp_map(float v, float l, float h) { return l * od_exp2(v * od_log2(h / l)); }
static float exp_unmap(float x, float l, float h) { if (x < l) x = l; return od_log2(x / l) / od_log2(h / l); }
static float clampf(float x, float l, float h) { return x < l ? l : x > h ? h : x; }

/* ---------------------------------------------------------------- the knobs of each page --- */
enum { K_WAVE, K_DETUNE, K_MIX, K_DRIVE, K_CUT, K_RESO, K_FENV, K_KTRK, K_A, K_D, K_S, K_R, K_LFO, K_VIB, K_LFOCUT,
       K_FDEC, K_APAT, K_ABPM, K_AOCT, K_AGATE, K_CHO, K_ECO, K_ETIME, K_REV, K_VOL, K_OCT, K_TEMPO, K_AI,
       K_SQBPM, K_SQLVL, K_SQSND, K_SQNOTE, K_MXVOL, K_MXPAN, K_MXFX, K_MXMUTE, K_MIN, K_MOUT, K_MSYNC, K_MCLK };
static const uint8_t PG_K[PG_N][4] = {
    {K_WAVE, K_DETUNE, K_MIX, K_DRIVE}, {K_CUT, K_RESO, K_FENV, K_KTRK}, {K_A, K_D, K_S, K_R},
    {K_LFO, K_VIB, K_LFOCUT, K_FDEC}, {K_APAT, K_ABPM, K_AOCT, K_AGATE}, {K_CHO, K_ECO, K_ETIME, K_REV},
    {K_SQBPM, K_SQLVL, K_SQSND, K_SQNOTE}, {K_MXVOL, K_MXPAN, K_MXFX, K_MXMUTE}, {K_MIN, K_MOUT, K_MSYNC, K_MCLK}};
static const char *const K_LBL[] = {"Wave", "Detune", "Mix", "Drive", "Cutoff", "Reso", "Env", "KeyTrk", "Attack",
    "Decay", "Sustain", "Release", "LFO", "Vibrato", "LFO>Cut", "F.Decay", "Pattern", "Tempo", "Octaves", "Gate",
    "Chorus", "Echo", "Time", "Reverb", "Volume", "Octave", "Tempo", "Arp AI", "Tempo", "Level", "Sound", "Note",
    "Volume", "Pan", "Reverb", "Mute", "In Ch", "Out Ch", "Sync", "Clk Out"};
static const char *k_lbl(int k)          /* (SEQUENCER: the drums' knobs say what they do there) */
{
    if (!sel_track && k == K_SQSND) return "Inst";
    if (!sel_track && k == K_SQNOTE) return "Rhythm";
    return K_LBL[k];
}
static void ai_apply(void);
static void midi_out_release(void);
static void midi_out_key(int n, int down);
static void activity(void);
static float k_get(int k)
{
    switch (k) {
    case K_WAVE: return P.wave / 3.f;
    case K_DETUNE: return (P.detune + 12.f) / 24.f;
    case K_MIX: return P.oscMix;
    case K_DRIVE: return (P.drive - 1.f) / 7.f;
    case K_CUT: return exp_unmap(P.cutoff, 40.f, 16000.f);
    case K_RESO: return P.reso;
    case K_FENV: return P.fEnvAmt;
    case K_KTRK: return P.keyTrk;
    case K_A: return exp_unmap(P.a, .002f, 3.f);
    case K_D: return exp_unmap(P.d, .01f, 4.f);
    case K_S: return P.s;
    case K_R: return exp_unmap(P.r, .01f, 5.f);
    case K_LFO: return exp_unmap(P.lfoRate, .05f, 20.f);
    case K_VIB: return P.lfoPitch / 50.f;
    case K_LFOCUT: return P.lfoCut;
    case K_FDEC: return exp_unmap(P.fDecay, .02f, 3.f);
    case K_APAT: return P.arpPattern / 4.f;
    case K_ABPM: case K_TEMPO: return (P.arpBpm - 40.f) / 200.f;
    case K_AOCT: return ((float)(int32_t)(P.arpOct + .5f) - 1.f) / 2.f;
    case K_AGATE: return P.arpGate;
    case K_CHO: return P.cho;
    case K_ECO: return P.eco;
    case K_ETIME: return exp_unmap(P.etime, .06f, 1.2f);
    case K_REV: return P.rev;
    case K_VOL: return P.vol;
    case K_OCT: return (float)(oct_shift + 3) / 6.f;
    case K_AI: return ai_on ? 1.f : 0.f;
    case K_SQBPM: return (float)(sq.bpm - 40) / 200.f;
    case K_SQLVL: return tr[sel_track].vol;
    case K_SQSND: return sel_track ? (float)tr[sel_track].preset / (float)(n_presets() - 1) : (float)sel_drum / 7.f;
    case K_SQNOTE: return sel_track ? (float)tr[sel_track].sel_note / (float)(PIANO_ROWS - 1) : (float)rhythm_idx / (float)(NRHY - 1);
    case K_MXVOL: return tr[sel_track].vol;
    case K_MXPAN: return (tr[sel_track].pan + 1.f) / 2.f;
    case K_MXFX: return tr[sel_track].fx;
    case K_MXMUTE: return tr[sel_track].mute ? 1.f : 0.f;
    case K_MIN: return (float)mset.in_ch / 16.f;
    case K_MOUT: return (float)mset.out_ch / 16.f;
    case K_MSYNC: return mset.sync ? 1.f : 0.f;
    case K_MCLK: return mset.clk_out ? 1.f : 0.f;
    }
    return 0.f;
}
static void k_set(int k, float v)
{
    v = clampf(v, 0.f, 1.f);
    switch (k) {
    case K_WAVE: P.wave = (float)(int32_t)(v * 3.f + .5f); break;
    case K_DETUNE: P.detune = v * 24.f - 12.f; break;
    case K_MIX: P.oscMix = v; break;
    case K_DRIVE: P.drive = 1.f + v * 7.f; break;
    case K_CUT: P.cutoff = exp_map(v, 40.f, 16000.f); break;
    case K_RESO: P.reso = v; break;
    case K_FENV: P.fEnvAmt = v; break;
    case K_KTRK: P.keyTrk = v; break;
    case K_A: P.a = exp_map(v, .002f, 3.f); break;
    case K_D: P.d = exp_map(v, .01f, 4.f); break;
    case K_S: P.s = v; break;
    case K_R: P.r = exp_map(v, .01f, 5.f); break;
    case K_LFO: P.lfoRate = exp_map(v, .05f, 20.f); break;
    case K_VIB: P.lfoPitch = v * 50.f; break;
    case K_LFOCUT: P.lfoCut = v; break;
    case K_FDEC: P.fDecay = exp_map(v, .02f, 3.f); break;
    case K_APAT: set_arp_pattern((float)(int32_t)(v * 4.f + .5f)); break;
    case K_ABPM: case K_TEMPO: P.arpBpm = (float)(int32_t)(40.f + v * 200.f + .5f); break;
    case K_AOCT: P.arpOct = (float)(int32_t)(1.f + v * 2.f + .5f); break;
    case K_AGATE: P.arpGate = v; break;
    case K_CHO: P.cho = v; break;
    case K_ECO: P.eco = v; break;
    case K_ETIME: P.etime = exp_map(v, .06f, 1.2f); break;
    case K_REV: P.rev = v; break;
    case K_VOL: P.vol = v; break;
    case K_OCT: oct_shift = (int)(v * 6.f + .5f) - 3; break;
    case K_AI: { int on = v > .5f; if (on != ai_on) { ai_on = (uint8_t)on; if (on) ai_apply(); } } break;
    case K_SQBPM: sq.bpm = (int)(40.f + v * 200.f + .5f); break;
    case K_SQLVL: case K_MXVOL: tr[sel_track].vol = v; break;
    case K_SQSND:
        if (sel_track) tr[sel_track].preset = (int)(v * (float)(n_presets() - 1) + .5f);
        else sel_drum = (int)(v * 7.f + .5f);
        break;
    case K_SQNOTE:
        if (sel_track) tr[sel_track].sel_note = (int)(v * (float)(PIANO_ROWS - 1) + .5f);
        else { int i = (int)(v * (float)(NRHY - 1) + .5f); if (i != rhythm_idx) set_rhythm(i); }
        break;
    case K_MXPAN: tr[sel_track].pan = v * 2.f - 1.f; break;
    case K_MXFX: tr[sel_track].fx = v; break;
    case K_MXMUTE: seq_mute(sel_track, v > .5f); break;
    case K_MIN: case K_MOUT: case K_MSYNC: case K_MCLK: {   /* the MIDI settings: kept in the flash */
        uint8_t *f = k == K_MIN ? &mset.in_ch : k == K_MOUT ? &mset.out_ch : k == K_MSYNC ? &mset.sync : &mset.clk_out;
        uint8_t nv = (uint8_t)(k == K_MIN || k == K_MOUT ? (int)(v * 16.f + .5f) : v > .5f);
        if (k == K_MCLK && nv != mset.clk_out && sq.playing && !ext_active()) od_midi_out(nv ? 0xFA : 0xFC, 0, 0);
        if (k == K_MOUT && nv != mset.out_ch) midi_out_release();
        if (nv != *f) { *f = nv; settings_dirty = 1; settings_ms = od_ms(); }
        break;
    }
    }
}
static void k_txt(int k, char *b)
{
    switch (k) {
    case K_WAVE: cat(b, WAVE_NAMES[(int32_t)(P.wave + .5f) & 3]); break;
    case K_DETUNE: { char *e = b; if (P.detune >= 0.f) *e++ = '+'; cat(fmt_f(e, P.detune, 2), " st"); } break;
    case K_MIX: pct_txt(b, P.oscMix); break;
    case K_DRIVE: cat(fmt_f(b, P.drive, 1), "x"); break;
    case K_CUT: hz_txt(b, P.cutoff); break;
    case K_RESO: pct_txt(b, P.reso); break;
    case K_FENV: pct_txt(b, P.fEnvAmt); break;
    case K_KTRK: pct_txt(b, P.keyTrk); break;
    case K_A: time_txt(b, P.a); break;
    case K_D: time_txt(b, P.d); break;
    case K_S: pct_txt(b, P.s); break;
    case K_R: time_txt(b, P.r); break;
    case K_LFO: cat(fmt_f(b, P.lfoRate, 2), " Hz"); break;
    case K_VIB: cat(fmt_i(b, (int32_t)(P.lfoPitch + .5f)), " ct"); break;
    case K_LFOCUT: pct_txt(b, P.lfoCut); break;
    case K_FDEC: time_txt(b, P.fDecay); break;
    case K_APAT: cat(b, ARP_NAMES[(int32_t)(P.arpPattern + .5f) % 5]); break;
    case K_ABPM: case K_TEMPO: fmt_i(b, (int32_t)(P.arpBpm + .5f)); break;
    case K_AOCT: cat(fmt_i(b, (int32_t)(P.arpOct + .5f)), " oct"); break;
    case K_AGATE: pct_txt(b, P.arpGate); break;
    case K_CHO: pct_txt(b, P.cho); break;
    case K_ECO: pct_txt(b, P.eco); break;
    case K_ETIME: time_txt(b, P.etime); break;
    case K_REV: pct_txt(b, P.rev); break;
    case K_VOL: pct_txt(b, P.vol); break;
    case K_OCT: { char *e = b; if (oct_shift > 0) *e++ = '+'; fmt_i(e, oct_shift); } break;
    case K_AI: cat(b, ai_on ? "ON" : "OFF"); break;
    case K_SQBPM: fmt_i(b, sq.bpm); break;
    case K_SQLVL: fmt_i(b, (int32_t)(tr[sel_track].vol * 100.f + .5f)); break;
    case K_SQSND:
        if (!sel_track) cat(b, DRUM_ABBR[sel_drum]);
        else { char *e = b; if (tr[sel_track].preset + 1 < 10) *e++ = '0'; fmt_i(e, tr[sel_track].preset + 1); }
        break;
    case K_SQNOTE:
        if (!sel_track) fmt_i(b, rhythm_idx + 1);
        else note_txt(b, SEQ_LOW + tr[sel_track].sel_note);
        break;
    case K_MXVOL: pct_txt(b, tr[sel_track].vol); break;
    case K_MXPAN: {
        float f = tr[sel_track].pan * 50.f + .5f;
        int32_t q = (int32_t)f;                                   /* Math.round: floor(x + .5) */
        if ((float)q > f) q--;
        if (!q) cat(b, "C");
        else { b[0] = q < 0 ? 'L' : 'R'; fmt_i(b + 1, q < 0 ? -q : q); }
        break;
    }
    case K_MXFX: pct_txt(b, tr[sel_track].fx); break;
    case K_MXMUTE: cat(b, tr[sel_track].mute ? "ON" : "OFF"); break;
    case K_MIN: if (mset.in_ch) fmt_i(b, mset.in_ch); else cat(b, "ALL"); break;
    case K_MOUT: if (mset.out_ch) fmt_i(b, mset.out_ch); else cat(b, "OFF"); break;
    case K_MSYNC: cat(b, mset.sync ? "EXT" : "INT"); break;
    case K_MCLK: cat(b, mset.clk_out ? "ON" : "OFF"); break;
    default: b[0] = 0;
    }
}

/* ---------------------------------------------------------------- the creature of each preset --- */
static uint32_t mb_s;
static float mb(void)                    /* mulberry32, as the web synth */
{
    uint32_t t;
    mb_s += 0x6D2B79F5u;
    t = mb_s;
    t = (t ^ (t >> 15)) * (1u | t);
    t ^= t + (t ^ (t >> 7)) * (61u | t);
    return (float)((t ^ (t >> 14)) >> 8) * (1.f / 16777216.f);
}
enum { CT_UFO, CT_ALIEN, CT_ROCKET, CT_SAT, CT_JALIEN, CT_ROBOT, CT_JELLY, CT_PROBE };
enum { CM_HOVER, CM_ORBIT, CM_DRIFT, CM_PULSE };
enum { CX_HOP, CX_BEAM, CX_SPIN, CX_FLASH, CX_TRAIL, CX_SHAKE };
typedef struct {
    uint8_t type, eyes, ant, motion, react, nstars;
    uint16_t accent, a2;
    float speed, ph, rot;
    uint8_t sx[44], sy[44], ss[44];
    float stw[44];
    int8_t planet;
    float plx, ply, plr;
    uint8_t ring;
    float trx[12], try_[12];
    uint8_t trn;
} viz_t;
static viz_t viz;
static int viz_for = -1;
static void viz_make(int i)
{
    int k, a1, a2;
    uint32_t seed = i < OD_NPRESETS ? (uint32_t)i : (uint32_t)i * 7919u + 31u;   /* the saved ones: the preview's idx*7919+31 */
    mb_s = seed * 2654435761u + 99u;
    viz.accent = ACCENTS[a1 = (int)(mb() * 8.f)];
    a2 = (int)(mb() * 8.f);
    if (a2 == a1) a2 = (a1 + 3) % 8;
    viz.a2 = ACCENTS[a2];
    viz.type = (uint8_t)(mb() * 8.f);
    viz.eyes = (uint8_t)(1 + (int)(mb() * 3.f));
    viz.ant = (uint8_t)(mb() * 3.f);
    viz.motion = (uint8_t)(mb() * 4.f);
    viz.react = (uint8_t)(mb() * 6.f);
    viz.speed = .5f + mb() * 1.4f;
    viz.ph = mb() * 6.28f;
    viz.rot = 0.f;
    viz.nstars = (uint8_t)(20 + (int)(mb() * 24.f));
    for (k = 0; k < viz.nstars; k++) {
        viz.sx[k] = (uint8_t)(mb() * 240.f);
        viz.sy[k] = (uint8_t)(mb() * .85f * 157.f);
        viz.stw[k] = mb() * 6.28f;
        viz.ss[k] = mb() < .2f ? 2 : 1;
    }
    viz.planet = mb() < .55f;
    if (viz.planet) {
        viz.plx = .12f + mb() * .76f;
        viz.ply = .15f + mb() * .3f;
        viz.plr = 8.f + mb() * 13.f;
        viz.ring = mb() < .5f;
    }
    viz.trn = 0;
    viz_for = i;
}

/* the screen's own starfield */
static uint8_t gst_s[170], gst_c0[170], gst_c1[170];
static float gst_x[170], gst_y[170];   /* from the sky's middle (120, 78 in the scene) */
static float gst_tw[170], gst_ph[170], gst_rt[170];
static const uint32_t STAR_TINT[6] = {0xE6ECF5, 0x7FB2FF, 0xFFD27A, 0xFF8FC8, 0x7FFFD4, 0xC49BFF};
static void stars_init(void)
{
    int i;
    for (i = 0; i < 170; i++) {
        gst_x[i] = (float)(rnd() % 240u) - 120.f;
        gst_y[i] = (float)(rnd() % 157u) - 78.f;
        gst_tw[i] = rndf() * 6.28f;
        gst_s[i] = rndf() < .12f ? 2 : 1;
        gst_c0[i] = (uint8_t)(rnd() % 6u);       /* each star drifts between two tints, at its own pace */
        gst_c1[i] = (uint8_t)((gst_c0[i] + 1u + rnd() % 5u) % 6u);
        gst_ph[i] = rndf() * 6.28f;
        gst_rt[i] = .0008f + rndf() * .0016f;
    }
}
/* the background: the stars twinkle and change colour; now and then a comet crosses the sky */
static struct { uint8_t on; uint32_t t0, next; float x0, y0, vx, vy, len; uint16_t c; } comet = {0, 0, 4000u};
static uint32_t comet_rng = 0x6B43A9B5u;
static float comet_rf(void) { comet_rng ^= comet_rng << 13; comet_rng ^= comet_rng >> 17; comet_rng ^= comet_rng << 5; return (float)(comet_rng >> 8) * (1.f / 16777216.f); }
/* the warp: each note played pushes the stars outwards from the middle, as through hyperspace; it fades in ~0.6 s
 * (a slow drift always) */
static float warp;
static uint32_t warp_seen, sky_ms;
static void sky_line(float x0, float y0, float x1, float y1, uint16_t c, float a)   /* a streak: bright at the head */
{
    float dx = x1 - x0, dy = y1 - y0, l = (dx < 0.f ? -dx : dx) > (dy < 0.f ? -dy : dy) ? (dx < 0.f ? -dx : dx) : (dy < 0.f ? -dy : dy);
    int n = (int)l, i;
    if (n > 40) n = 40;
    for (i = 0; i <= n; i++) {
        float t = n ? (float)i / (float)n : 1.f;
        int32_t x = (int32_t)(x0 + dx * t + .5f), y = (int32_t)(y0 + dy * t + .5f);
        if (y >= 19 && y < 176) blend(x, y, c, (uint32_t)(a * t * 256.f));
    }
}
static void dr_sky(uint32_t now)
{
    int k;
    float dt = sky_ms ? (float)(now - sky_ms) * .001f : 0.f, grow;
    if (dt > .1f) dt = .1f;
    sky_ms = now;
    if (note_flash_ms != warp_seen) { warp_seen = note_flash_ms; warp = warp + 1.6f > 4.f ? 4.f : warp + 1.6f; }
    warp *= od_exp2(-dt * 2.4f);                 /* (~0.6 s) */
    grow = (warp + .05f) * dt;                   /* the outward step: r grows by r x grow */
    for (k = 0; k < 170; k++) {
        float tw = .5f + .5f * g_sin((float)now * .0015f + gst_tw[k]), m = .5f + .5f * g_sin((float)now * gst_rt[k] + gst_ph[k]);
        float a = gst_s[k] == 2 ? .35f + .6f * tw : .18f + .45f * tw, x = gst_x[k], y = gst_y[k], r2, depth;
        int32_t px, py;
        uint16_t c = mix565(hexc(STAR_TINT[gst_c0[k]]), hexc(STAR_TINT[gst_c1[k]]), (uint32_t)(m * 256.f));
        gst_x[k] = x + x * grow + (x < 0.f ? -1.f : 1.f) * grow * 6.f;
        gst_y[k] = y + y * grow + (y < 0.f ? -1.f : 1.f) * grow * 4.f;
        if (gst_x[k] < -121.f || gst_x[k] > 121.f || gst_y[k] < -79.f || gst_y[k] > 79.f) {   /* gone: born again near the middle */
            float ang = rndf() * 6.2832f, rad = 4.f + rndf() * 30.f;
            gst_x[k] = g_cos(ang) * rad;
            gst_y[k] = g_sin(ang) * rad * .7f;
            continue;
        }
        r2 = gst_x[k] * gst_x[k] + gst_y[k] * gst_y[k];
        depth = r2 > 1600.f ? 1.f : r2 / 1600.f;  /* near the middle: far, faint */
        a *= .25f + .75f * depth;
        px = (int32_t)(120.f + gst_x[k] + .5f);
        py = (int32_t)(97.f + gst_y[k] + .5f);
        if (warp > .15f) {                        /* streaks, longer the faster */
            float sl = warp * .09f;
            sky_line(120.f + gst_x[k] * (1.f - sl), 97.f + gst_y[k] * (1.f - sl), 120.f + gst_x[k], 97.f + gst_y[k], c, a);
            continue;
        }
        blend(px, py, c, (uint32_t)(a * 256.f));
        if (gst_s[k] == 2 && tw > .7f && py > 19 && py < 175) {   /* the bright ones flare a little */
            uint32_t f = (uint32_t)((tw - .7f) * 1.2f * a * 256.f);
            blend(px - 1, py, c, f); blend(px + 1, py, c, f);
            blend(px, py - 1, c, f); blend(px, py + 1, c, f);
        }
    }
    if (!comet.on && (int32_t)(now - comet.next) >= 0) {   /* a new comet: from the top or a side, downwards */
        static const uint32_t CC[4] = {0xBFE6FF, 0xFFFFFF, 0xFFE3A8, 0xA8FFE8};
        float sp = 110.f + 90.f * comet_rf(), ang = .35f + .5f * comet_rf();   /* px/s; radians below the horizon */
        int left = comet_rf() < .5f;
        comet.on = 1;
        comet.t0 = now;
        comet.x0 = left ? -10.f : 250.f - 120.f * comet_rf();
        comet.y0 = left ? 19.f + 70.f * comet_rf() : 12.f;
        if (!left && comet_rf() < .5f) comet.x0 = 20.f + 120.f * comet_rf();
        comet.vx = sp * g_cos(ang) * (left ? 1.f : (comet.x0 > 120.f ? -1.f : 1.f));
        comet.vy = sp * g_sin(ang);
        comet.len = 26.f + 22.f * comet_rf();
        comet.c = hexc(CC[(int)(comet_rf() * 4.f) & 3]);
    }
    if (comet.on) {
        float t = (float)(now - comet.t0) * .001f, hx = comet.x0 + comet.vx * t, hy = comet.y0 + comet.vy * t;
        float sp = g_sqrt(comet.vx * comet.vx + comet.vy * comet.vy), ux = comet.vx / sp, uy = comet.vy / sp, fade;
        int i;
        float tx = hx - ux * comet.len, ty = hy - uy * comet.len;   /* the tail's end: gone once it has left the sky */
        if (ty > 176.f || (comet.vx > 0.f && tx > 250.f) || (comet.vx < 0.f && tx < -10.f) || t > 6.f) {
            comet.on = 0;                        /* gone: the next in 6 to 14 s */
            comet.next = now + 6000u + (uint32_t)(8000.f * comet_rf());
        } else {
            fade = t < .25f ? t * 4.f : 1.f;
            for (i = 0; i < 12; i++) {           /* the tail: thinner and fainter behind */
                float a0 = (float)i / 12.f, a1 = (float)(i + 1) / 12.f;
                float x0 = hx - ux * comet.len * a0, y0 = hy - uy * comet.len * a0, x1 = hx - ux * comet.len * a1, y1 = hy - uy * comet.len * a1;
                if (y0 < 19.f || y1 < 19.f || y0 > 176.f || y1 > 176.f) continue;
                seg(x0, y0, x1, y1, 1.6f - a0, comet.c, (1.f - a0) * (1.f - a0) * .8f * fade);
            }
            if (hy >= 20.f && hy <= 175.f) dot(hx, hy, 1.f, hexc(0xFFFFFF));
        }
    }
}

static float wave_y(int sh, float t)
{
    t = t - (float)(int32_t)t;
    if (t < 0.f) t += 1.f;
    switch (sh) {
    case 0: return 2.f * t - 1.f;
    case 1: return t < .5f ? 1.f : -1.f;
    case 2: return t < .5f ? 4.f * t - 1.f : 3.f - 4.f * t;
    default: return g_sin(6.2832f * t);
    }
}
static void label(const char *s, float x, float y, uint16_t c) { no_glow(); text(gs.a < .9f ? &F_PX7M : &F_PX10M, x, y, s, c, TA_L); }   /* (ENVELOPE: drawn at .75) */

/* ---- the creatures (the web synth's drawUFO, drawAlien, ...) ---- */
static void dr_ufo(int flash, uint32_t now)
{
    int i;
    led(viz.accent, 2.4f);
    g_begin(); g_ellipse(0, 0, 26, 9, 0, 0, 6.2832f); g_stroke();
    g_begin(); g_arc(0, -6, 12, 3.1416f, 6.2832f); g_stroke();
    for (i = 0; i < 4; i++) {
        int on = flash || (int)((now / 140u) % 4u) == i;
        if (on) dot(-15.f + (float)i * 10.f, 4, 2.2f, viz.a2);
        else dot(-15.f + (float)i * 10.f, 4, 1.4f, C_FAINT);
    }
    for (i = 0; i < viz.eyes; i++) dot(((float)i - (float)(viz.eyes - 1) / 2.f) * 8.f, -8, 1.8f, viz.a2);
}
static void dr_alien(int flash)
{
    int a, e;
    led(viz.accent, 2.4f);
    g_begin(); g_ellipse(0, -14, 11, 15, 0, 0, 6.2832f); g_stroke();
    g_begin(); g_move(-5, 1); g_line(-6, 18); g_move(5, 1); g_line(6, 18);
    g_move(-6, 8); g_line(-14, 14); g_move(6, 8); g_line(14, 14); g_stroke();
    for (a = 0; a < viz.ant; a++) {
        float x = ((float)a - (float)(viz.ant - 1) / 2.f) * 8.f;
        g_begin(); g_move(x, -27); g_line(x * 1.4f, -35); g_stroke();
        dot(x * 1.4f, -36, 1.8f, viz.a2);
    }
    led(viz.a2, flash ? 3.f : 2.f);
    g_begin();
    for (e = 0; e < viz.eyes; e++) {
        float x = ((float)e - (float)(viz.eyes - 1) / 2.f) * 9.f;
        g_move(x - 4, -16); g_line(x + 4, -13);
    }
    g_stroke();
}
static void dr_rocket(int flash, uint32_t now, int thrust)
{
    int e;
    led(viz.accent, 2.4f);
    g_begin(); g_rrect(-8, -22, 16, 34, 7.5f); g_stroke();
    g_begin(); g_move(-8, -16); g_line(0, -30); g_line(8, -16); g_stroke();
    g_begin(); g_move(-8, 6); g_line(-15, 16); g_move(8, 6); g_line(15, 16); g_stroke();
    for (e = 0; e < viz.eyes; e++) { g_begin(); g_arc(0, -12.f + (float)e * 9.f, 3.4f, 0, 6.2832f); g_stroke(); }
    if (thrust || flash) {
        float t = (float)now;
        led(viz.a2, 2.2f);
        g_begin(); g_move(-4, 13); g_line(-2, 20.f + g_sin(t * .05f) * 3.f);
        g_move(0, 13); g_line(0, 24.f + g_sin(t * .07f) * 3.f);
        g_move(4, 13); g_line(2, 20.f + g_cos(t * .05f) * 3.f); g_stroke();
    }
}
static void dr_sat(int flash)
{
    int e;
    led(viz.accent, 2.4f);
    g_begin(); g_rrect(-8, -8, 16, 16, 3); g_stroke();
    g_begin(); g_move(-8, 0); g_line(-14, 0); g_move(8, 0); g_line(14, 0); g_stroke();
    g_begin(); g_rrect(-30, -6, 16, 12, 2); g_stroke();
    g_begin(); g_rrect(14, -6, 16, 12, 2); g_stroke();
    g_begin(); g_move(-25, -6); g_line(-25, 6); g_move(-20, -6); g_line(-20, 6);
    g_move(20, -6); g_line(20, 6); g_move(25, -6); g_line(25, 6); g_stroke();
    g_begin(); g_arc(0, -14, 6, 3.1416f * .9f, 3.1416f * 2.1f); g_stroke();
    dot(0, -14, 1.8f, flash ? viz.a2 : viz.accent);
    for (e = 0; e < viz.eyes; e++) dot(-4.f + (float)e * 4.f, 2, 1.6f, viz.a2);
}
static void dr_jalien(int flash, uint32_t now)
{
    uint16_t P1 = hexc(0xC13DFF), P2 = hexc(0xE9B8FF);
    float dt = (float)(now - note_flash_ms), lift = dt < 260.f ? g_sin(clampf(dt / 260.f, 0, 1) * 3.1416f) : 0.f;
    int ne = viz.eyes < 2 ? 2 : viz.eyes, e, t;
    float lvl = (float)lvl_peak / 32767.f;
    led(P1, 2.4f);
    g_begin(); g_arc(0, -4, 15, 3.1416f, 6.2832f); g_stroke();
    g_begin(); g_move(-15, -4); g_line(15, -4); g_stroke();
    led(P2, flash || lift > .3f ? 2.8f : 2.f);
    g_begin();
    for (e = 0; e < ne; e++) { float x = ((float)e - (float)(ne - 1) / 2.f) * 8.f; g_move(x - 4, -9); g_line(x + 4, -7); }
    g_stroke();
    for (t = 0; t < 6; t++) {
        float bx = -13.f + (float)t * 5.2f, len = (15.f + (float)(t % 3) * 5.f) * (1.f - lift * .65f);
        float sw = g_sin((float)now * .0028f * viz.speed + (float)t * 1.1f) * (7.f + lvl * 7.f) * (1.f - lift * .5f);
        uint16_t c = t % 2 ? P1 : P2;
        led(c, 2.f);
        g_begin(); g_move(bx, -4); g_quad(bx + sw * .6f, 4.f + len * .45f - lift * 10.f, bx + sw, 4.f + len - lift * 14.f); g_stroke();
        dot(bx + sw, 4.f + len - lift * 14.f, 1.4f, c);
    }
}
static void dr_robot(int flash, uint32_t now)
{
    float arm = 1.f - (float)(now - note_flash_ms) / 250.f;
    int e;
    if (arm < 0.f) arm = 0.f;
    led(viz.accent, 2.4f);
    g_begin(); g_rrect(-9, -24, 18, 14, 3); g_stroke();
    g_begin(); g_rrect(-11, -7, 22, 20, 3); g_stroke();
    g_begin(); g_move(-6, 13); g_line(-6, 20); g_move(6, 13); g_line(6, 20);
    g_move(-11, -2); g_line(-18, 4.f - arm * 14.f); g_move(11, -2); g_line(18, 4);
    g_move(0, -24); g_line(0, -30); g_stroke();
    dot(0, -31, 1.8f, viz.a2);
    for (e = 0; e < viz.eyes; e++) dot(((float)e - (float)(viz.eyes - 1) / 2.f) * 7.f, -18, flash ? 2.6f : 1.8f, viz.a2);
    no_glow(); gs.col = viz.accent; gs.lw = 1.6f;
    g_begin(); g_move(-4, 2); g_line(4, 2); g_stroke();
}
static void dr_jelly(int flash)
{
    int t, e;
    float lp = lfo_view * 6.2832f;
    led(viz.accent, 2.4f);
    g_begin(); g_arc(0, -6, 15, 3.1416f, 6.2832f); g_stroke();
    g_begin(); g_move(-15, -6); g_line(15, -6); g_stroke();
    for (t = 0; t < 4; t++) {
        float x = -10.f + (float)t * 6.6f;
        g_begin(); g_move(x, -5); g_quad(x + g_sin(lp + (float)t) * 6.f, 4, x + g_sin(lp + (float)t + 1.f) * 7.f, 14); g_stroke();
    }
    for (e = 0; e < viz.eyes; e++) dot(((float)e - (float)(viz.eyes - 1) / 2.f) * 8.f, -11, flash ? 2.6f : 1.8f, viz.a2);
}
static void dr_probe(int flash, uint32_t now)
{
    float a = (float)now * .003f * viz.speed;
    int e;
    led(viz.accent, 2.4f);
    g_begin(); g_arc(0, 0, 9, 0, 6.2832f); g_stroke();
    g_begin(); g_ellipse(0, 0, 24, 8, viz.rot, 0, 6.2832f); g_stroke();
    dot(g_cos(a) * 24.f, g_sin(a) * 8.f, 2.4f, viz.a2);
    for (e = 0; e < viz.eyes; e++) dot(((float)e - (float)(viz.eyes - 1) / 2.f) * 5.f, 0, 1.6f, flash ? viz.a2 : C_FAINT);
}

/* ---- the scenes ---- */
static void dr_scene(float W, float H, uint32_t now)
{
    float lvl = (float)lvl_peak / 32767.f, t, cx = W / 2.f, cy = H * .42f, sc = 1.f, baseY = H - 14.f, amp, dtN;
    int k, sh = (int32_t)(P.wave + .5f) & 3, flash, beam;
    char b[8];
    if (viz_for != preset_idx) viz_make(preset_idx);
    no_glow();
    for (k = 0; k < viz.nstars; k++) {
        float a = .22f + .3f * (.5f + .5f * g_sin((float)now * .002f + viz.stw[k]));
        fill_rect_a(viz.sx[k], (int32_t)(viz.sy[k] + gs.f), viz.ss[k], viz.ss[k], hexc(0xC8D2DE), (uint32_t)(a * 256.f));
    }
    if (viz.planet) {
        float px = viz.plx * W, py = viz.ply * H;
        led(viz.a2, 1.6f); gs.alpha = .55f;
        g_begin(); g_arc(px, py, viz.plr, 0, 6.2832f); g_stroke();
        if (viz.ring) { g_begin(); g_ellipse(px, py, viz.plr * 1.7f, viz.plr * .5f, .35f, 0, 6.2832f); g_stroke(); }
        gs.alpha = 1.f;
    }
    amp = 5.f + lvl * 24.f;
    led(viz.a2, 2.f);
    g_begin();
    for (k = 8; k <= (int)W - 8; k += 3) {
        float y = baseY - (wave_y(sh, ((float)k - 8.f) / (W - 16.f) * 2.f) * .5f + .5f) * amp;
        if (k == 8) g_move((float)k, y); else g_line((float)k, y);
    }
    g_stroke();
    t = (float)now * .001f * viz.speed + viz.ph;
    if (viz.motion == CM_HOVER) cy += g_sin(t * 1.6f) * 8.f;
    else if (viz.motion == CM_ORBIT) { cx = W / 2.f + g_cos(t) * W * .24f; cy = H * .4f + g_sin(t) * 12.f; }
    else if (viz.motion == CM_DRIFT) { float d = t * 36.f; cx = d - (float)(int32_t)(d / (W + 90.f)) * (W + 90.f) - 45.f; }
    else sc = 1.f + g_sin(t * 2.f) * .05f + lvl * .18f;
    dtN = (float)(now - note_flash_ms);
    if (viz.react == CX_HOP && dtN < 170.f) cy -= g_sin(dtN / 170.f * 3.1416f) * 11.f;
    if (viz.react == CX_SHAKE && dtN < 150.f) cx += g_sin((float)now * .7f) * 3.f;
    if (viz.react == CX_SPIN) viz.rot += .012f + lvl * .1f;
    if (viz.react == CX_TRAIL) {
        if (viz.trn == 12) { for (k = 0; k < 11; k++) { viz.trx[k] = viz.trx[k + 1]; viz.try_[k] = viz.try_[k + 1]; } viz.trn = 11; }
        viz.trx[viz.trn] = cx; viz.try_[viz.trn] = cy; viz.trn++;
        for (k = 0; k < viz.trn; k++) { gs.alpha = (float)k / 12.f * .45f; dot(viz.trx[k], viz.try_[k], 1.6f, viz.a2); }
        gs.alpha = 1.f;
    }
    flash = viz.react == CX_FLASH && dtN < 160.f;
    beam = viz.react == CX_BEAM && (dtN < 220.f || lvl > .35f);
    g_save(); g_translate(cx, cy); g_scale(sc, sc);
    if (viz.type == CT_SAT || viz.type == CT_PROBE) g_rotate(viz.rot);
    switch (viz.type) {
    case CT_UFO: dr_ufo(flash, now); break;
    case CT_ALIEN: dr_alien(flash); break;
    case CT_ROCKET: dr_rocket(flash, now, beam); break;
    case CT_SAT: dr_sat(flash); break;
    case CT_JALIEN: dr_jalien(flash, now); break;
    case CT_ROBOT: dr_robot(flash, now); break;
    case CT_JELLY: dr_jelly(flash); break;
    default: dr_probe(flash, now);
    }
    g_restore();
    if (beam && viz.type != CT_ROCKET) {
        gs.alpha = .5f + .3f * g_sin((float)now * .02f);
        led(viz.a2, 1.8f);
        g_begin(); g_move(cx - 6.f * sc, cy + 12.f * sc); g_line(cx - 16.f, baseY - 4.f);
        g_move(cx + 6.f * sc, cy + 12.f * sc); g_line(cx + 16.f, baseY - 4.f); g_stroke();
        gs.alpha = 1.f;
    }
    label(preset_at(preset_idx)->name, 10, 16, viz.accent);
    cat(b, WAVE_NAMES[sh]);
    label(b, W - 56.f, 16, C_FAINT);
}
static void dr_filter(float W, float H)
{
    float eff = clampf(P.cutoff * od_exp2(tmod * 3.f), 40.f, 16000.f), x0 = 14, x1 = W - 14, base = H * .44f;
    float fx = x0 + exp_unmap(eff, 40.f, 16000.f) * (x1 - x0), bump = P.reso * 32.f;
    float fx2 = x0 + clampf(exp_unmap(eff * od_exp2(P.fEnvAmt * 5.f), 40.f, 16000.f), 0.f, 1.f) * (x1 - x0);
    char b[20];
    if (viz_for != preset_idx) viz_make(preset_idx);
    led(C_FAINT, 2); g_begin(); g_move(x0, H - 16); g_line(x1, H - 16); g_stroke();
    led(viz.accent, 1.8f); gs.alpha = .35f;
    g_begin(); g_move(x0, base); g_line(fx2 - 30.f > x0 ? fx2 - 30.f : x0, base);
    g_quad(fx2, base - bump, fx2 + 40.f < x1 ? fx2 + 40.f : x1, H - 18.f); g_stroke();
    gs.alpha = 1.f;
    led(viz.accent, 3.f);
    g_begin(); g_move(x0, base); g_line(fx - 34.f > x0 ? fx - 34.f : x0, base);
    g_quad(fx - 12.f, base, fx, base - bump);
    g_quad(fx + 14.f, base - bump * .2f, fx + 30.f < x1 ? fx + 30.f : x1, base + 28.f);
    g_line(fx + 52.f < x1 ? fx + 52.f : x1, H - 18.f); g_stroke();
    dot(fx, base - bump, 5, viz.a2);
    hz_txt(b, eff);
    label(b, 10, 16, C_INK);
    cat(cat(b, "Q "), ""); pct_txt(b + 2, P.reso);
    label(b, W - 66, 16, viz.a2);
}
static void dr_env(float W, float H)
{
    float x0 = 18, y0 = H - 24, peak = 24;
    float aW = 12.f + exp_unmap(P.a, .002f, 3.f) * 68.f, dW = 12.f + exp_unmap(P.d, .01f, 4.f) * 68.f, sW = 50.f;
    float rW = 12.f + exp_unmap(P.r, .01f, 5.f) * 68.f, susY = y0 - P.s * (y0 - peak - 10.f);
    float px[5] = {x0, x0 + aW, x0 + aW + dW, x0 + aW + dW + sW, x0 + aW + dW + sW + rW};
    float py[5] = {y0, peak, susY, susY, y0};
    float durs[4] = {P.a, P.d, .4f, P.r}, total = P.a + P.d + .4f + P.r, t = (float)(od_ms() - note_flash_ms) / 1000.f;
    char b[48], *e;
    int i;
    led(C_FAINT, 2); g_begin(); g_move(x0 - 6, y0 + 6); g_line(x0 + 16, y0 + 6); g_stroke();
    led(C_INK, 2.4f); g_begin();
    for (i = 0; i < 5; i++) { if (i) g_line(px[i], py[i]); else g_move(px[i], py[i]); }
    g_stroke();
    for (i = 1; i < 5; i++) dot(px[i], py[i], 5, KNOB_COL[i - 1]);
    if (t < total) {
        float acc = 0, f, x, y, ang;
        int sg = 0;
        while (sg < 3 && t > acc + durs[sg]) { acc += durs[sg]; sg++; }
        f = clampf((t - acc) / (durs[sg] > 1e-4f ? durs[sg] : 1e-4f), 0, 1);
        x = px[sg] + (px[sg + 1] - px[sg]) * f;
        y = py[sg] + (py[sg + 1] - py[sg]) * f;
        ang = 0.f;
        {   /* atan2 by a short search: the arrow only needs a rough angle */
            float dx = px[sg + 1] - px[sg], dy = py[sg + 1] - py[sg], best = 1e9f, a;
            for (a = -3.1f; a < 3.15f; a += .1f) { float c = g_cos(a) * dy - g_sin(a) * dx; c = c < 0 ? -c : c; if (c < best && g_cos(a) * dx + g_sin(a) * dy > 0) { best = c; ang = a; } }
        }
        g_save(); g_translate(x, y); g_rotate(ang);
        led(C_RED, 2.2f);
        g_begin(); g_move(8, 0); g_line(-5, -4); g_line(-5, 4); g_line(8, 0); g_move(-5, 0); g_line(-10, 0); g_stroke();
        g_restore();
    }
    e = cat(b, "A "); time_txt(e, P.a); e = cat(b + od_strlen(b), "  D "); time_txt(e, P.d);
    e = cat(b + od_strlen(b), "  S "); pct_txt(e, P.s); e = cat(b + od_strlen(b), "  R "); time_txt(e, P.r);
    label(b, 10, 16, C_INK);
}
static void face_eyes(float x, float y, float s, uint16_t c) { dot(x - 5.f * s, y - 4.f * s, 2.f * s, c); dot(x + 5.f * s, y - 4.f * s, 2.f * s, c); }
static void dr_fx(float W, float H, uint32_t now)
{
    float dtN = (float)(now - note_flash_ms), hop = dtN < 160.f ? -g_sin(dtN / 160.f * 3.1416f) * 4.f : 0.f;
    float fy = H * .52f + hop, X[3] = {W * .18f, W * .5f, W * .82f};
    char b[20];
    int k;
    {   /* CHORUS (blue): the face doubles, the mouth waves */
        float cx = X[0], v = P.cho, off = g_sin((float)now * .005f) * (2.f + 9.f * v);
        int twin;
        for (twin = v > .02f ? 1 : 0; twin >= 0; twin--) {
            float dx = twin ? off : 0.f, mx;
            gs.alpha = twin ? .25f + .45f * v : 1.f;
            led(C_BLUE, 2.2f);
            g_begin(); g_ellipse(cx + dx, fy, 14, 17, 0, 0, 6.2832f); g_stroke();
            g_begin(); g_move(cx + dx, fy - 17); g_line(cx + dx + off * .5f, fy - 25); g_stroke();
            dot(cx + dx + off * .5f, fy - 26, 1.8f, C_BLUE);
            face_eyes(cx + dx, fy, 1.f, C_WHITE);
            led(C_BLUE, 2.f); g_begin();
            for (mx = -7.f; mx <= 7.f; mx += 1.5f) {
                float my = fy + 7.f + g_sin(mx * (.4f + v * .8f) + (float)now * .01f) * (.5f + 4.5f * v);
                if (mx == -7.f) g_move(cx + dx + mx, my); else g_line(cx + dx + mx, my);
            }
            g_stroke();
        }
        gs.alpha = 1.f;
        cat(b, "CHO "); pct_txt(b + 4, v);
        label(b, cx - 26.f, H - 8.f, C_BLUE);
    }
    {   /* ECHO (green): copies that fade, spaced by the time */
        float cx = X[1], v = P.eco, sp = 10.f + P.etime * 34.f, mouthR = 1.5f + 5.f * v;
        float per = P.etime > .12f ? P.etime : .12f, tt = (float)now / 1000.f;
        int blink = v > .03f && (tt - (float)(int32_t)(tt / per) * per) < .1f;
        no_glow();
        for (k = 1; k <= 3; k++) fill_rect((int32_t)(cx + (float)k * sp - sp * .5f), (int32_t)(fy + 22.f + gs.f), 3, 3, hexc(0x9BA0A7));
        for (k = 3; k >= 0; k--) {
            float sc = k ? (k == 1 ? .9f : k == 2 ? .81f : .73f) : 1.f, al = k ? (k == 1 ? .55f : k == 2 ? .3f : .17f) * v * 2.f : 1.f;
            float dx = (float)k * sp;
            if (al > .85f) al = .85f;
            if (k && al < .03f) continue;
            gs.alpha = k ? al : 1.f;
            led(C_GREEN, 2.2f * sc);
            g_begin(); g_ellipse(cx + dx, fy, 14.f * sc, 17.f * sc, 0, 0, 6.2832f); g_stroke();
            if (blink && !k) {
                led(C_WHITE, 2); g_begin(); g_move(cx - 8, fy - 4); g_line(cx - 2, fy - 4); g_move(cx + 2, fy - 4); g_line(cx + 8, fy - 4); g_stroke();
            } else face_eyes(cx + dx, fy, sc, C_WHITE);
            led(C_GREEN, 2.f * sc);
            g_begin(); g_arc(cx + dx, fy + 7.f * sc, mouthR * sc, 0, 6.2832f); g_stroke();
        }
        gs.alpha = 1.f;
        cat(b, "ECHO "); pct_txt(b + 5, v);
        label(b, cx - 24.f, H - 8.f, C_GREEN);
    }
    {   /* REVERB (orange): an aura that spreads, an ever happier face */
        float cx = X[2], v = P.rev;
        if (v > .01f) {
            for (k = 0; k < 3; k++) {
                float rt = (float)now * .0004f + (float)k * .33f;
                rt -= (float)(int32_t)rt;
                gs.alpha = (1.f - rt) * v * .8f;
                led(C_ORANGE, 1.6f);
                g_begin(); g_ellipse(cx, fy, 15.f + rt * 26.f, 18.f + rt * 26.f, 0, 0, 6.2832f); g_stroke();
            }
            gs.alpha = 1.f;
        }
        led(C_ORANGE, 2.2f);
        g_begin(); g_ellipse(cx, fy, 14, 17, 0, 0, 6.2832f); g_stroke();
        g_begin(); g_move(cx, fy - 17); g_line(cx, fy - 25); g_stroke();
        dot(cx, fy - 26, 1.8f, C_ORANGE);
        if (v < .35f) face_eyes(cx, fy, 1.f, C_WHITE);
        else {
            led(C_WHITE, 2);
            g_begin(); g_arc(cx - 5, fy - 3, 3.2f, 3.1416f, 6.2832f); g_stroke();
            g_begin(); g_arc(cx + 5, fy - 3, 3.2f, 3.1416f, 6.2832f); g_stroke();
        }
        led(C_ORANGE, 2);
        g_begin(); g_arc(cx, fy + 5, 4.f + 4.f * v, .15f * 3.1416f, .85f * 3.1416f); g_stroke();
        if (v > .6f) {
            dot(cx - 14, fy - 20.f + g_sin((float)now * .005f) * 2.f, 1.6f, C_WHITE);
            dot(cx + 15, fy - 16.f + g_cos((float)now * .005f) * 2.f, 1.6f, C_WHITE);
        }
        cat(b, "REV "); pct_txt(b + 4, v);
        label(b, cx - 24.f, H - 8.f, C_ORANGE);
    }
    label("FX", 10, 16, C_FAINT);
}
static void dr_arp(float W, float H, uint32_t now)
{
    float tw = (W - 40.f) / 8.f, ty = H - 24.f, px, uy;
    char b[32], *e;
    int i;
    if (viz_for != preset_idx) viz_make(preset_idx);
    for (i = 0; i < 8; i++) {
        if (i == (int)arp_tile && arp_on()) fill_rrect(20.f + (float)i * tw + 2.f, ty + gs.f, tw - 4.f, 10, 4, viz.accent, 1.f);
        else { led(hexc(0x565B62), 2); g_begin(); g_rrect(20.f + (float)i * tw + 2.f, ty, tw - 4.f, 10, 4); g_stroke(); }
    }
    no_glow();
    px = arp_on() ? 20.f + (float)arp_tile * tw + tw / 2.f : W / 2.f;
    uy = ty - 44.f + g_sin((float)now * .003f) * 4.f;
    g_save(); g_translate(px, uy); dr_ufo(0, now); g_restore();
    if (arp_on() && now - arp_jump_ms < 130u) {
        gs.alpha = .75f; led(viz.a2, 2);
        g_begin(); g_move(px - 8, uy + 10); g_line(px - tw * .32f, ty); g_move(px + 8, uy + 10); g_line(px + tw * .32f, ty); g_stroke();
        gs.alpha = 1.f;
    }
    if (!arp_on()) label("ARP OFF: TURN KNOB1", W / 2.f - 52.f, ty - 58.f, C_FAINT);
    e = cat(b, "ARP "); e = cat(e, ARP_NAMES[(int32_t)(P.arpPattern + .5f) % 5]); if (hold) cat(e, " HOLD");
    label(b, 10, 16, C_INK);
    if (ai_on) label("AI GROOVE", 10, 30, hexc(0xB26BFF));
    e = fmt_i(b, (int32_t)(P.arpBpm + .5f)); e = cat(e, " BPM  "); e = fmt_i(e, (int32_t)(P.arpOct + .5f)); cat(e, " OCT");
    label(b, W - 112.f, 16, viz.accent);
}
static void dr_mod(float W, float H, uint32_t now)
{
    float lfo = g_sin(6.2832f * lfo_view), cx = W / 2.f, cy = 34.f, bw, sy;
    char b[24], *e;
    int i;
    if (viz_for != preset_idx) viz_make(preset_idx);
    g_save(); g_translate(cx, cy); g_rotate(lfo * .12f);
    led(viz.accent, 2.6f);
    g_begin(); g_ellipse(0, 0, 44, 12, 0, 0, 6.2832f); g_stroke();
    g_begin(); g_arc(0, -8, 17, 3.1416f, 6.2832f); g_stroke();
    for (i = 0; i < 6; i++) {
        int on = (int)((float)now * .001f * P.lfoRate * 6.f) % 6 == i;
        if (on) dot(-27.f + (float)i * 11.f, 5, 2.2f, viz.a2); else dot(-27.f + (float)i * 11.f, 5, 1.4f, C_FAINT);
    }
    g_restore();
    bw = 12.f + (P.lfoPitch / 50.f) * 36.f;
    gs.alpha = .35f + .3f * (.5f + .5f * lfo);
    led(viz.a2, 2);
    g_begin(); g_move(cx - 14, cy + 12); g_line(cx - bw, H - 16); g_move(cx + 14, cy + 12); g_line(cx + bw, H - 16); g_stroke();
    gs.alpha = 1.f;
    sy = H - 34.f - (.5f + .5f * lfo) * 26.f;
    g_save(); g_translate(cx + g_sin((float)now * .002f) * 8.f, sy); g_rotate(lfo * .4f);
    led(C_INK, 1.8f);
    g_begin(); g_rrect(-5, -5, 10, 10, 2); g_stroke();
    g_begin(); g_move(-5, 0); g_line(-10, 0); g_move(5, 0); g_line(10, 0); g_stroke();
    g_restore();
    led(C_FAINT, 2); g_begin(); g_move(26, H - 14); g_line(W - 26, H - 14); g_stroke();
    e = cat(b, "LFO "); e = fmt_f(e, P.lfoRate, 1); cat(e, " Hz");
    label(b, 10, 16, C_INK);
    e = cat(b, "VIB "); e = fmt_i(e, (int32_t)(P.lfoPitch + .5f)); cat(e, " ct");
    label(b, W - 76, 16, viz.a2);
}
static void ui_selftest(char **e)             /* the cutoff knob's mapping, there and back (synth.c od_selftest) */
{
    static volatile float c = 2200.f, v = .5f;
    float u = exp_unmap(c, 40.f, 16000.f);
    st_num(e, (int32_t)(u * 1000.f));                             /* 668 */
    st_num(e, (int32_t)exp_map(v, 40.f, 16000.f));               /* 800 */
    st_num(e, (int32_t)exp_map(u + 1.f / 128.f, 40.f, 16000.f)); /* one step up from 2200 */
}
static uint32_t st_fps, st_cpu;
void od_stats(uint32_t fps, uint32_t cpu) { st_fps = fps; st_cpu = cpu; }
/* MIDI (the GLO button): the four settings on the knobs, what comes in, and the FM-1's own state below */
static uint32_t min_ms;                  /* the last message in */
static char min_txt[24];
static void dr_glo(float W, float H)
{
    static const char *const K[10] = {"MIDI IN", "MIDI OUT", "CLOCK SYNC", "CLOCK OUT", "USB", "LAST IN", "FIRMWARE",
        "BATTERY", "SCREEN", "TEST"};
    char v[10][64];
    int i;
    uint32_t now = od_ms();
    (void)W; (void)H;
    if (mset.in_ch) fmt_i(cat(v[0], "CHANNEL "), mset.in_ch); else cat(v[0], "ALL CHANNELS");
    if (mset.out_ch) fmt_i(cat(v[1], "CHANNEL "), mset.out_ch); else cat(v[1], "OFF");
    if (!mset.sync) cat(v[2], "INTERNAL");
    else if (ext_active()) cat(fmt_i(cat(v[2], "EXTERNAL "), seq_bpm_shown()), " BPM");
    else cat(v[2], "EXTERNAL: NO CLOCK");
    if (!mset.clk_out) cat(v[3], "OFF");
    else if (ext_active()) cat(v[3], "ON (FOLLOWING)");
    else cat(fmt_i(cat(v[3], "ON "), sq.bpm), " BPM");
    cat(v[4], batt_raw < 0 ? "-" : batt_usb ? "CONNECTED" : "NOT CONNECTED");
    cat(v[5], min_ms ? min_txt : "-");
    cat(cat(v[6], "ONDADREAM "), OD_VERSION);
    if (batt_raw < 0) cat(v[7], "-");
    else { char *e = cat(v[7], batt_usb ? "USB" : ""); if (!batt_usb) { e = fmt_i(e, batt_lvl); e = cat(e, "/3"); } e = cat(e, "  ADC "); fmt_i(e, batt_raw); }
    { char *e = fmt_i(v[8], (int)st_fps); e = cat(e, " FPS  CPU "); e = fmt_i(e, (int)st_cpu); cat(e, " %"); }
    cat(v[9], od_test_txt);
    for (i = 0; i < 10; i++) {
        text(&F_SILK8, 10, 14.f + (float)i * 15.f, K[i], C_FAINT, TA_L);
        text(&F_PX10, 230, 14.f + (float)i * 15.f, v[i], hexc(0xE9EBEE), TA_R);
    }
    if (min_ms && now - min_ms < 150u) dot(4, 11.f + 5.f * 15.f, 2, C_GREEN);   /* a message now */
    fill_rect(8, 19 + 93, 224, 1, hexc(0x1E2024));   /* the FM-1's own, below */
}

/* ---- SEQUENCER (the whole screen) and MIXER ---- */
static const uint8_t DISP[NTR] = {1, 2, 3, 0};   /* the tracks as shown: 1, 2, 3, D */
static int slot_of(int t) { int d; for (d = 0; d < (int)NTR; d++) if (DISP[d] == t) return d; return 0; }
static const uint32_t TRACK_DARK[NTR] = {0x123063, 0x0B4A33, 0x5A3A10, 0x5C1626};
static uint16_t shade(uint16_t c, float f)       /* the preview's shade(): each channel x f, floored (in 8 bits) */
{
    uint32_t r = (uint32_t)(c >> 11) * 255u / 31u, g = (uint32_t)((c >> 5) & 63u) * 255u / 63u, b = (uint32_t)(c & 31u) * 255u / 31u;
    return RGB((uint32_t)((float)r * f), (uint32_t)((float)g * f), (uint32_t)((float)b * f));
}
static void lower(char *d, const char *s) { while (*s) { *d++ = (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s); s++; } *d = 0; }
static const char *track_name(int t) { return t ? preset_at(tr[t].preset)->name : RHYTHMS[rhythm_idx].name; }
static void cut(char *d, const char *s, int n) { int i; for (i = 0; i < n && s[i]; i++) d[i] = s[i]; d[i] = 0; }
static void dr_seq_full(uint32_t now)
{
    static const int ROW_Y[NTR] = {41, 80, 119, 158};
    char b[24], c2[24];
    int st = sq.vis, d, i, k;
    float bw;
    fill_rect(0, 0, 240, 240, 0);
    /* the top: the tempo, the position, rec */
    fmt_i(b, seq_bpm_shown());
    text(&F_PX30, 6, 30, b, hexc(0xE8EAED), TA_L);
    bw = (float)text_w16(&F_PX30, b) / 16.f;
    text(&F_PX10, 10.f + bw, 30, "bpm", hexc(0x9AA0A8), TA_L);
    if (sq.playing) fill_tri(116, 9, 128, 16, 116, 23, hexc(0xFFFFFF), 1.f);
    else fill_rect(116, 10, 11, 11, hexc(0x9AA0A8));
    if (st >= 0) { char *e = fmt_i(b, st / 4 + 1); *e++ = '.'; fmt_i(e, st % 4 + 1); }
    else cat(b, "1.1");
    text(&F_PX13, 133, 22, b, hexc(0xE8EAED), TA_L);
    for (i = 0; i < 4; i++) fill_rect(116 + i * 9, 28, 6, 6, st >= 0 && st / 4 == i ? hexc(0xFF3355) : hexc(0x3A1F26));
    gs.alpha = rec_on ? 1.f : .35f;
    text(&F_PX11, 185, 20, "rec", hexc(0xFF3355), TA_L);
    fill_circle(226, 16, 8, hexc(0xFF3355), gs.alpha);
    if (rec_on && (now / 450u) % 2u) fill_circle(226, 16, 3, hexc(0xFFB3BF), 1.f);
    gs.alpha = 1.f;
    /* the tracks */
    for (d = 0; d < (int)NTR; d++) {
        int t = DISP[d], y = ROW_Y[d], sel = t == sel_track, muted = tr[t].mute, c;
        uint16_t tc = ACCENTS[t], box = sel ? tc : hexc(TRACK_DARK[t]);
        float nmw;
        fill_rect(6, y + 1, 22, 27, box);
        fill_rect(6, y + 1, 22, 1, tc); fill_rect(6, y + 27, 22, 1, tc);
        fill_rect(6, y + 1, 1, 27, tc); fill_rect(27, y + 1, 1, 27, tc);
        b[0] = t ? (char)('0' + d + 1) : 'D'; b[1] = 0;
        text(&F_PX14, 17, (float)y + 20.f, b, sel ? hexc(0xFFFFFF) : tc, TA_C);
        cut(b, track_name(t), 11);
        text(&F_PX11, 34, (float)y + 11.f, b, muted ? hexc(0x5D6168) : hexc(0xE8EAED), TA_L);
        nmw = (float)text_w16(&F_PX11, b) / 16.f;
        if (muted) cat(c2, "mute");
        else if (!t) { char u[8]; lower(u, DRUM_ABBR[sel_drum]); cat(cat(c2, "808 "), u); }
        else { note_txt(b, SEQ_LOW + tr[t].sel_note); lower(c2, b); }
        text(&F_PX9, 38.f + nmw, (float)y + 11.f, c2, muted ? hexc(0xFF3355) : tc, TA_L);
        if (sel && rec_on) {
            fill_rect(203, y + 1, 32, 12, hexc(0xFF3355));
            text(&F_PX9, 219, (float)y + 10.f, "rec", hexc(0xFFFFFF), TA_C);
        }
        for (c = 0; c < 16; c++) {               /* the steps: x0 34, 10.2 px each */
            int on = track_on_at(t, c), hi = sel && sel_step_on(t, c), x = (int)(34.f + (float)c * 10.2f + .5f);
            float a = muted ? .35f : 1.f;
            uint16_t col = hi ? tc : on ? (sel ? shade(tc, .55f) : tc) : hexc(c % 4 == 0 ? 0x262A31 : 0x1A1D22);
            if (on && !sel) a *= .85f;
            if (a >= 1.f) fill_rect(x, y + 15, 8, 7, col);
            else fill_rect_a(x, y + 15, 8, 7, col, (uint32_t)(a * 256.f));
        }
        if (st >= 0) fill_rect((int)(34.f + (float)st * 10.2f + .5f), y + 24, 8, 2, hexc(0xFFFFFF));
        fill_rect(203, y + 17, 32, 4, hexc(0x2A2D33));     /* the level */
        fill_rect(203, y + 17, (int)(32.f * tr[t].vol + .5f), 4, sel ? tc : hexc(0x8A8F97));
    }
    /* the knobs */
    for (k = 0; k < 4; k++) {
        int id = PG_K[PG_SEQ][k];
        float cx = 30.f + (float)k * 60.f, cy = 208.f, v = clampf(k_get(id), 0.f, 1.f), a = 3.14159265f * (.75f + 1.5f * v);
        arc_butt(cx, cy, 10, 4, 3.14159265f * .75f, 3.14159265f * 2.25f, hexc(0x2C2F34));
        arc_butt(cx, cy, 10, 4, 3.14159265f * .75f, a, KNOB_COL[k]);
        line_butt(cx, cy, cx + g_cos(a) * 7.f, cy + g_sin(a) * 7.f, 2, hexc(0xFFFFFF));
        lower(b, k_lbl(id));
        text(&F_PX10, cx, cy + 20.f, b, C_INK, TA_C);
        k_txt(id, b);
        text(&F_PX10, cx, cy + 31.f, b, hexc(0xE8EAED), TA_C);
    }
}
static void dr_mix(void)                         /* (in the scene: from y 19) */
{
    int d;
    char b[16];
    for (d = 0; d < (int)NTR; d++) {
        int t = DISP[d], x = d * 60, muted = tr[t].mute, hit, vh, bx = x + 22, by = 34 + 19, bh = 84;
        uint16_t tc = ACCENTS[t];
        if (t == sel_track) { fill_rect(x + 8, 19, 44, 142, hexc(0x1B1E24)); fill_rect(x + 8, 19, 44, 2, tc); }
        if (t) { b[0] = 'T'; b[1] = 'R'; b[2] = (char)('0' + d + 1); b[3] = 0; } else cat(b, "DRUM");
        text(&F_SILK8, (float)x + 30.f, 14.f + 19.f, b, tc, TA_C);
        cut(b, track_name(t), 7);
        text(&F_SILK6, (float)x + 30.f, 26.f + 19.f, b, hexc(0x9AA0A8), TA_C);
        hit = sq.vis >= 0 && track_on_at(t, sq.vis) && !muted;
        fill_rect(bx, by, 16, bh, hexc(0x1D2026));
        vh = (int)((float)bh * tr[t].vol + .5f);
        fill_rect_a(bx, by + bh - vh, 16, vh, muted ? hexc(0x3A3D42) : tc, hit ? 256u : 192u);
        if (hit) fill_rect(bx, by + bh - vh, 16, 2, hexc(0xFFFFFF));
        text(&F_SILK8, (float)x + 30.f, 134.f + 19.f, muted ? "MUTE" : "ON", muted ? C_RED : C_INK, TA_C);
    }
}

/* ---- the battery (top right, where the preview has its level meter): the FM-1's own thresholds on the battery's
 * ADC (531 / 561 / 591: 0..3 bars, as the stock firmware), a bolt on USB power ---- */
void od_battery(int32_t raw, int usb)
{
    static const int32_t T[3] = {531, 561, 591};
    int l = 0;
    batt_raw = raw;
    batt_usb = (uint8_t)(usb != 0);
    while (l < 3 && raw >= T[l] + (l >= batt_lvl ? 3 : -3)) l++;   /* (3 counts of hysteresis: no flicker on a step) */
    batt_lvl = l;
}
static void dr_battery(void)
{
    static const uint8_t BOLT[7][2] = {{222, 2}, {221, 2}, {220, 2}, {218, 6}, {220, 2}, {219, 2}, {218, 2}};   /* x, width */
    uint16_t ink = batt_lvl || batt_usb ? C_INK : C_RED, bar = batt_lvl >= 2 ? C_GREEN : batt_lvl == 1 ? C_ORANGE : C_RED;
    int i;
    fill_rect(210, 4, 22, 1, ink); fill_rect(210, 12, 22, 1, ink);   /* the body, 22 x 9, and its tip */
    fill_rect(210, 4, 1, 9, ink); fill_rect(231, 4, 1, 9, ink);
    fill_rect(232, 6, 2, 5, ink);
    if (batt_usb) {                              /* USB power: a bolt */
        for (i = 0; i < 7; i++) fill_rect(BOLT[i][0], 5 + i, BOLT[i][1], 1, C_GREEN);
        return;
    }
    for (i = 0; i < batt_lvl; i++) fill_rect(212 + i * 6, 6, 5, 5, bar);
}

/* ---- the frame's parts ---- */
static void dr_header(void)
{
    char b[24], *e;
    if (viz_for != preset_idx) viz_make(preset_idx);
    e = cat(b, "PRESET ");
    if (preset_idx + 1 < 10) *e++ = '0';
    e = fmt_i(e, preset_idx + 1);
    e = cat(e, "/");
    fmt_i(e, n_presets());
    text(&F_SILK8, 5, 12, b, C_INK, TA_L);
    text(&F_SILK8, 150, 12, PG_TITLE[page], viz.accent, TA_C);
    dr_battery();
    fill_rect(0, 17, 240, 1, hexc(0x1E2024));
}
static void dr_cards(uint32_t now)
{
    static const char *const KL[4] = {"KNOB1", "KNOB2", "KNOB3", "KNOB4"};
    int k;
    char b[24], u[12];
    for (k = 0; k < 4; k++) {
        int x = k * 60, y = 184, id = PG_K[page][k], i, on = knob_touch == k && now - knob_touch_ms < 1200u;
        uint16_t col = KNOB_COL[k];
        float v = k_get(id), cx = (float)x + 30.f, cy = (float)y + 46.f, a;
        text(&F_SILK8, (float)x + 30.f, 180, KL[k], C_LABEL, TA_C);
        fill_rect(x + 1, y, 58, 56, on ? C_CARDHOT : C_CARD);
        fill_rect(x + 1, y, 58, 2, col);
        for (i = 0; k_lbl(id)[i] && i < 7; i++) u[i] = (char)(k_lbl(id)[i] >= 'a' && k_lbl(id)[i] <= 'z' ? k_lbl(id)[i] - 32 : k_lbl(id)[i]);
        u[i] = 0;
        text(&F_SILK8, (float)x + 30.f, (float)y + 14.f, u, col, TA_C);
        k_txt(id, b);
        if (text_w(&F_PX11, b) > 56) {          /* too wide: without its unit */
            char *sp = b; while (*sp && *sp != ' ') sp++;
            *sp = 0;
        }
        text(&F_PX11, (float)x + 30.f, (float)y + 32.f, b, C_WHITE, TA_C);
        no_glow();
        gs.col = hexc(0x2C2F34); gs.lw = 2.f;
        g_begin(); g_arc(cx, cy, 7, 3.1416f * .75f, 3.1416f * 2.25f); g_stroke();
        a = 3.1416f * (.75f + 1.5f * v);
        if (v > .005f) { gs.col = col; g_begin(); g_arc(cx, cy, 7, 3.1416f * .75f, a); g_stroke(); }
    }
}

/* ---- the splash (design B, "Onde") ---- */
static const uint8_t SPL_ST[20][3] = {{224, 58, 0}, {25, 27, 2}, {68, 42, 0}, {152, 47, 3}, {167, 29, 2}, {234, 6, 2},
    {123, 12, 3}, {10, 15, 1}, {38, 10, 2}, {232, 32, 2}, {206, 12, 2}, {187, 9, 1}, {93, 8, 3}, {19, 104, 2},
    {58, 96, 1}, {212, 98, 2}, {226, 214, 1}, {16, 222, 1}, {200, 112, 1}, {44, 118, 1}};
static void dr_splash(uint32_t t)
{
    static const uint32_t WC[4] = {0x3E7BFF, 0x00C878, 0xE8EBEF, 0xFF7A1A};
    static const float WA[4] = {10, 8.5f, 7, 5.5f}, WP[4] = {0, .9f, 1.8f, 2.7f};
    float mv = (float)t / 2400.f * 6.2832f, jx = 89.f, jy;
    int i, k;
    fill_rect(0, 0, 240, 240, C_BG);
    for (i = 0; i < 20; i++) {
        static const uint8_t L[4] = {70, 110, 160, 210};
        uint32_t lv = (SPL_ST[i][2] + (t / 300u + (uint32_t)i) % 4u) % 4u;
        fill_rect_a(SPL_ST[i][0], SPL_ST[i][1], 1, 1, RGB(L[lv], L[lv], L[lv]), 256);
    }
    text(&F_INT36, 120, 56, "Onda", C_WHITE, TA_C);
    text(&F_INT36, 120, 88, "Dream", C_WHITE, TA_C);
    no_glow(); gs.col = C_ORANGE; gs.lw = 2.5f;
    g_begin(); g_move(78, 98); g_line(162, 98); g_stroke();
    for (k = 0; k < 4; k++) {
        gs.col = hexc(WC[k]); gs.lw = 2.5f;
        g_begin();
        for (i = 0; i <= 240; i += 4) {
            float y = 138.f + (float)k * 7.f + WA[k] * g_sin(6.2832f * (float)i / 120.f + WP[k] + mv);
            if (!i) g_move((float)i, y); else g_line((float)i, y);
        }
        g_stroke();
    }
    jy = 118.f + 5.f * g_sin(6.2832f * jx / 120.f + mv);
    {   /* the jelly riding the waves */
        uint16_t P1 = hexc(0xB26BFF);
        float sw = g_sin((float)t * .007f) * 2.f;
        gs.col = P1; gs.lw = 2.2f;
        g_begin(); g_arc(jx, jy, 13, 3.1416f, 6.2832f); g_line(jx - 13, jy); g_stroke();
        for (k = 0; k < 4; k++) {
            float x = jx - 9.f + (float)k * 6.f, s = k & 1 ? sw : -sw;
            g_begin(); g_move(x, jy + 1); g_quad(x + s * 2.f, jy + 6, x - s, jy + 11); g_stroke();
        }
        dot(jx - 4, jy - 6, 1.4f, C_WHITE);
        dot(jx + 4, jy - 6, 1.4f, C_WHITE);
    }
    no_glow();
    gs.col = C_ORANGE; gs.lw = 1.4f;
    g_begin(); g_rrect(18, 196, 34, 17, 8.5f); g_stroke();
    text(&F_INT10B, 35, 208, OD_VERSION, C_ORANGE, TA_C);
    text(&F_INT11, 222, 204, OD_AUTHOR, C_WHITE, TA_R);
    text(&F_INT7, 222, 216, "FIRMWARE FOR M-VAVE FM-1", hexc(0x8A9099), TA_R);
}

/* ---------------------------------------------------------------- the frame --- */
int od_splash(void) { return od_ms() - boot_ms < SPLASH_MS; }
static void dr_toast(uint32_t now)
{
    if (toast_ms && now - toast_ms < 900u) {
        fill_rect_a(20, 150, 200, 22, hexc(0x08090C), 235);
        text(&F_SILK8, 120, 165, toast_txt, C_ORANGE, TA_C);
    }
}
int od_frame(uint16_t *frame)
{
    uint32_t now = od_ms();
    fb = frame;
    g_reset();
    if (od_splash()) {
        dr_splash(now - boot_ms);
        return 1;
    }
    if (page == PG_SEQ) {                       /* the whole screen */
        dr_seq_full(now);
        dr_toast(now);
        return 1;
    }
    fill_rect(0, 0, 240, 240, C_BG);
    dr_header();
    /* the scene: 240 x 157 from y 19 */
    dr_sky(now);
    g_save();
    g_translate(0, 19);
    switch (page) {
    case PG_SYNTH: dr_scene(240, 157, now); break;
    case PG_FILTER: dr_filter(240, 157); break;
    case PG_ENV: g_scale(.75f, .75f); dr_env(320, 209); break;
    case PG_MOD: dr_mod(240, 157, now); break;
    case PG_ARP: dr_arp(240, 157, now); break;
    case PG_FX: dr_fx(240, 157, now); break;
    case PG_MIX: break;
    default: dr_glo(240, 157);
    }
    g_restore();
    if (page == PG_MIX) dr_mix();
    fill_rect(0, 176, 240, 64, C_BG);          /* (the scene stays above the cards) */
    dr_cards(now);
    if (sq.playing && page != PG_MIX && sq.vis >= 0) fill_rect(sq.vis * 15, 176, 14, 2, C_GREEN);   /* the step */
    dr_toast(now);
    return 1;
}

/* ---------------------------------------------------------------- the panel --- */
static void ai_apply(void)
{
    float pat, bpm, oct, gate, r;
    if (viz_for != preset_idx) viz_make(preset_idx);
    mb_s = (uint32_t)preset_idx * 97561u + 7u;
    r = mb();
    bpm = 70.f + (float)(int32_t)(r * 130.f);
    if (P.a > .3f) bpm = bpm - 30.f < 55.f ? 55.f : bpm - 30.f;
    if (P.s < .3f && P.d < .2f) bpm = bpm + 25.f > 220.f ? 220.f : bpm + 25.f;
    oct = viz.type == CT_ROCKET || viz.type == CT_PROBE ? 3.f : viz.type == CT_UFO || viz.type == CT_SAT ? 2.f : 1.f + (float)(int32_t)(mb() * 2.f);
    gate = P.a > .3f ? .85f : P.s == 0.f ? .35f : .55f + mb() * .2f;
    pat = viz.motion == CM_HOVER ? 1.f : viz.motion == CM_ORBIT ? 3.f : viz.motion == CM_DRIFT ? 2.f : 4.f;
    P.arpBpm = bpm; P.arpOct = oct; P.arpGate = gate;
    set_arp_pattern(pat);
    ai_rng = (uint32_t)preset_idx * 3u + 11u;
}
static void key_on(int n, float vel) { held_add(n, vel); if (!arp_on()) direct_on(n, vel); }
static void key_off(int n)
{
    int was = arp_on();
    if (hold) return;
    held_remove(n);
    if (!was) direct_off(n);
}
static void preset_go(int i)
{
    preset_load(i);
    if (ai_on) ai_apply();
}
void od_init(void)
{
    ui_selftest_fn = ui_selftest;
    pal_init();
    stars_init();
    synth_init();
    seq_init();
    boot_ms = od_ms();
}
/* REC: with the sequencer playing, the notes played go to the chosen track, on the step now */
static void key_on(int n, float vel);
static void record_note(int n)
{
    int col = sq.vis, p;
    if (!rec_on || !sq.playing || col < 0) return;
    if (!sel_track) { if (!drum_has(sel_drum, col)) drum_toggle(sel_drum, col); return; }
    p = n - SEQ_LOW;
    if (p >= 0 && p < PIANO_ROWS) { tr[sel_track].grid[p] |= (uint16_t)(1u << col); tr[sel_track].sel_note = p; }
}
static void audition_drum(int type)
{
    uint32_t k;
    drum_t *d = &dv[0];
    for (k = 0; k < DRUM_POLY; k++)
        if (!dv[k].active) { d = &dv[k]; break; }
    drum_trigger(d, (uint8_t)type, 1.f);
}
static void step_key(int col)                    /* SEQUENCER: a white key, a step of the track */
{
    if (!sel_track) {
        if (drum_toggle(sel_drum, col) && !sq.playing) audition_drum(sel_drum);
    } else {
        uint16_t *g = &tr[sel_track].grid[tr[sel_track].sel_note];
        *g ^= (uint16_t)(1u << col);
        if ((*g >> col & 1u) && !sq.playing) {   /* hear it: the keyboard's sound, 180 ms */
            int n = SEQ_LOW + tr[sel_track].sel_note, k;
            key_on(n, .8f);
            for (k = 0; k < 4; k++)
                if (!aud[k].note) { aud[k].note = (int8_t)n; aud[k].ms = od_ms(); break; }
        }
    }
}
/* the keys' places: white or black, counted from the left (the FM-1's keyboard starts on F) */
static const int8_t KEY_WHITE[27] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1, 12, -1, 13, 14, -1, 15};
static const int8_t KEY_BLACK[27] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1, -1, 5, -1, 6, -1, 7, -1, -1, 8, -1, 9, -1, -1, 10, -1};
static int step_mode(void) { return page == PG_SEQ || page == PG_MIX; }
static void step_press(uint32_t k)
{
    int wi = KEY_WHITE[k], bi = KEY_BLACK[k];
    if (page == PG_SEQ) {
        if (wi >= 0) step_key(wi);
        else if (bi < 4) sel_track = DISP[bi];
        else if (!sel_track) { sel_drum = (bi - 4) % 8; audition_drum(sel_drum); }
    } else {
        if (wi >= 0 && wi < 4) { int t = DISP[wi]; seq_mute(t, !tr[t].mute); }
        else if (bi >= 0 && bi < 4) sel_track = DISP[bi];
    }
}
void od_key(uint32_t k, int down)
{
    int n;
    if (k >= 27u) return;
    activity();
    n = KB_BASE + 12 * oct_shift + (int)k;
    if (down && step_mode() && !od_splash()) { step_press(k); return; }   /* SEQUENCER / MIXER: the keys are steps */
    if (down) { keys_down |= 1u << k; key_on(n, .85f); record_note(n); midi_out_key(n, 1); }
    else if (keys_down >> k & 1u) { keys_down &= ~(1u << k); key_off(n); midi_out_key(n, 0); }
}
static void midi_seen(uint8_t st, uint8_t d1, uint8_t d2)   /* MIDI: the last message in, for the page */
{
    uint8_t c = st & 0xF0u;
    char *e = min_txt;
    if (st >= 0xF8u) { if (st != 0xF8u) cat(e, st == 0xFAu ? "START" : st == 0xFBu ? "CONTINUE" : "STOP"); else if (!min_ms || od_ms() - min_ms > 300u) cat(e, "CLOCK"); else return; }
    else {
        if (c == 0x90u && d2) { e = cat(e, "NOTE "); note_txt(e, d1); e += od_strlen(e); }
        else if (c == 0x80u || c == 0x90u) { e = cat(e, "OFF "); note_txt(e, d1); e += od_strlen(e); }
        else if (c == 0xB0u) { e = cat(e, "CC "); e = fmt_i(e, d1); *e++ = '='; e = fmt_i(e, d2); }
        else if (c == 0xE0u) e = cat(e, "PITCH BEND");
        else e = cat(e, "MESSAGE");
        e = cat(e, "  CH "); fmt_i(e, (st & 15) + 1);
    }
    min_ms = od_ms() | 1u;
}
/* MIDI OUT: the panel's keys, as played (OUT channel; 0 off) */
static uint8_t mout_on[128];             /* the notes sent on, with their channel + 1 */
static void midi_out_release(void)       /* the notes on, off (the channel changes, or OFF) */
{
    int n;
    for (n = 0; n < 128; n++)
        if (mout_on[n]) { od_midi_out((uint8_t)(0x80u | (mout_on[n] - 1u)), (uint8_t)n, 0); mout_on[n] = 0; }
}
static void midi_out_key(int n, int down)
{
    if (n < 0 || n > 127) return;
    if (down && mset.out_ch) {
        if (mout_on[n]) od_midi_out((uint8_t)(0x80u | (mout_on[n] - 1u)), (uint8_t)n, 0);
        od_midi_out((uint8_t)(0x90u | (mset.out_ch - 1u)), (uint8_t)n, 108);   /* (the keys' velocity: .85) */
        mout_on[n] = mset.out_ch;
    } else if (!down && mout_on[n]) {
        od_midi_out((uint8_t)(0x80u | (mout_on[n] - 1u)), (uint8_t)n, 0);
        mout_on[n] = 0;
    }
}
void od_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t c = st & 0xF0u;
    if (st < 0xF0u) activity();
    if (st >= 0xF8u) { midi_seen(st, 0, 0); seq_realtime(st); return; }   /* the clock and the transport */
    if (c >= 0x80u && c < 0xF0u && mset.in_ch && (st & 15u) + 1u != mset.in_ch) return;   /* not our channel */
    if (c >= 0x80u && c < 0xF0u) midi_seen(st, d1, d2);
    if (c == 0x90u && d2) { midi_held[d1 & 127u] = 1; key_on(d1 & 127u, (float)d2 / 127.f); record_note(d1 & 127u); }
    else if (c == 0x80u || (c == 0x90u && !d2)) { midi_held[d1 & 127u] = 0; key_off(d1 & 127u); }
}
void od_store_done(int ok) { if (!ok) toast("SAVE FAILED"); }
static void set_page(int p) { page = p; }
void od_button(uint32_t b, int down)
{
    if (down) activity();
    if (!down || od_splash()) return;
    switch (b) {
    case OB_HOME: set_page(PG_SYNTH); break;
    case OB_EDIT: set_page(PG_FILTER); break;
    case OB_ENV: set_page(PG_ENV); break;
    case OB_LFO: set_page(PG_MOD); break;
    case OB_ARP:                                 /* ARP on ARPEGGIO: ARP AI on / off */
        if (page != PG_ARP) { set_page(PG_ARP); break; }
        ai_on = (uint8_t)!ai_on;
        if (ai_on) ai_apply();
        toast(ai_on ? "ARP AI ON" : "ARP AI OFF");
        break;
    case OB_FX: set_page(PG_FX); break;
    case OB_GLO: set_page(PG_GLO); break;
    case OB_SCL:
        hold = (uint8_t)!hold;
        if (!hold) { int i; for (i = arp.nheld - 1; i >= 0; i--) { int n = arp.held[i]; if (!(n >= KB_BASE + 12 * oct_shift && n < KB_BASE + 12 * oct_shift + 27 && (keys_down >> (n - KB_BASE - 12 * oct_shift) & 1u)) && !midi_held[n & 127]) { held_remove(n); direct_off(n); } } }
        toast(hold ? "HOLD ON" : "HOLD OFF");
        break;
    case OB_SEQ: set_page(page == PG_SEQ ? PG_MIX : PG_SEQ); break;
    case OB_PLAY:
        if (sq.playing) seq_stop(); else seq_play();
        toast(sq.playing ? "PLAY" : "STOP");
        break;
    case OB_REC:
        rec_on = (uint8_t)!rec_on;
        toast(rec_on ? "REC: PLAY THE KEYS" : "REC OFF");
        break;
    case OB_SAVE:                                /* the sound as it is, as the next "MIO n" (kept through power-off) */
        if (preset_save() < 0) toast("MEMORY FULL (30/30)");
        else {
            char t[24];
            cat(cat(t, "SAVED: "), preset_at(preset_idx)->name);
            toast(t);
        }
        break;
    case OB_OCTDN: case OB_OCTUP:
        oct_shift = oct_shift + (b == OB_OCTUP ? 1 : -1);
        oct_shift = oct_shift < -3 ? -3 : oct_shift > 3 ? 3 : oct_shift;
        {
            char t[16], *e = cat(t, "OCTAVE ");
            if (oct_shift > 0) *e++ = '+';
            fmt_i(e, oct_shift);
            toast(t);
        }
        break;
    }
}
void od_enc(uint32_t e, int32_t s)
{
    if (s) activity();
    if (!s || od_splash()) return;
    switch (e) {
    case OE_PRESET:
        if (step_mode()) {                       /* SEQUENCER / MIXER: the chosen track's preset, or the drums' rhythm */
            if (sel_track) { int n = n_presets(); tr[sel_track].preset = ((tr[sel_track].preset + s) % n + n) % n; }
            else set_rhythm(rhythm_idx + s);
        } else preset_go(preset_idx + s);
        break;
    case OE_SELECT: { int p = page + (s > 0 ? 1 : -1); set_page((p + PG_N) % PG_N); } break;
    case OE_ALGO:
        if (step_mode()) sel_track = DISP[CLAMP(slot_of(sel_track) + s, 0, (int)NTR - 1)];   /* the track */
        else P.wave = (float)CLAMP((int32_t)(P.wave + .5f) + s, 0, 3);
        break;
    default: {
        int k = (int)e - OE_K1, id = PG_K[page][k];
        float step = id == K_WAVE ? 1.f / 3.f : id == K_APAT ? .25f : id == K_AOCT ? .5f : id == K_OCT ? 1.f / 6.f
                   : id == K_AI || id == K_MXMUTE || id == K_MSYNC || id == K_MCLK ? 1.f : id == K_MIN || id == K_MOUT ? 1.f / 16.f
                   : id == K_SQBPM ? 1.f / 200.f
                   : id == K_SQLVL || id == K_MXVOL || id == K_MXPAN || id == K_MXFX ? 1.f / 100.f
                   : id == K_SQSND ? (sel_track ? 1.f / (float)(n_presets() - 1) : 1.f / 7.f)
                   : id == K_SQNOTE ? (sel_track ? 1.f / (float)(PIANO_ROWS - 1) : 1.f / (float)(NRHY - 1)) : 1.f / 128.f;
        int32_t a = s < 0 ? -s : s;
        if (step < .01f && a > 2) step *= (float)(a > 6 ? 4 : 2);   /* acceleration */
        k_set(id, k_get(id) + step * (float)s);
        knob_touch = k;
        knob_touch_ms = od_ms();
    }
    }
}
void od_master(uint32_t v) { master_v = (float)v / 1023.f; }
uint32_t od_leds_keys(void)              /* bit k: key k lit */
{
    uint32_t m = keys_down, base = (uint32_t)(KB_BASE + 12 * oct_shift), i;
    if (step_mode()) {                           /* the steps (the step playing: the other way round), the tracks */
        m = 0;
        for (i = 0; i < 27u; i++) {
            int wi = KEY_WHITE[i], bi = KEY_BLACK[i], on = 0, play = 0;
            if (page == PG_SEQ) {
                if (wi >= 0) { on = sel_step_on(sel_track, wi); play = sq.vis == wi; }
                else if (bi < 4) on = DISP[bi] == sel_track;
                else on = !sel_track && bi - 4 == sel_drum;
            } else {
                if (wi >= 0 && wi < 4) on = !tr[DISP[wi]].mute;
                else if (bi >= 0 && bi < 4) on = DISP[bi] == sel_track;
                if (wi >= 0) play = sq.vis == wi;
            }
            if (on ^ play) m |= 1u << i;
        }
        return m;
    }
    for (i = 0; i < NV; i++)
        if (vc[i].active && !vc[i].rel && (uint32_t)vc[i].note >= base && (uint32_t)vc[i].note < base + 27u)
            m |= 1u << ((uint32_t)vc[i].note - base);
    return m;
}
/* the keys' LEDs with a trail: a key going dark fades out, through the mid level and the glow */
#define TRAIL_MID_MS 160u
#define TRAIL_END_MS 450u
static uint32_t led_prev, led_trail, led_off_ms[27];
/* idle (nothing touched for 8 s, nothing sounding, the sequencer stopped, not on its pages): keys light up at
 * random and fade with their trail, as the stars twinkle */
static uint32_t act_ms, tw_next, tw_rng = 0x51ED270Bu, tw_on, tw_ms[27];
static void activity(void) { act_ms = od_ms(); tw_on = 0; }
static uint32_t idle_twinkle(uint32_t now)
{
    uint32_t i, m = 0;
    if (now - act_ms < 8000u || sq.playing || voices_active() || step_mode() || od_splash()) { tw_on = 0; return 0; }
    if ((int32_t)(now - tw_next) >= 0) {        /* a new one every 0.15 to 0.6 s */
        tw_rng ^= tw_rng << 13; tw_rng ^= tw_rng >> 17; tw_rng ^= tw_rng << 5;
        i = tw_rng % 27u;
        tw_on |= 1u << i;
        tw_ms[i] = now;
        tw_next = now + 150u + (tw_rng >> 8) % 450u;
    }
    for (i = 0; i < 27u; i++)
        if (tw_on >> i & 1u) {
            if (now - tw_ms[i] < 110u) m |= 1u << i;   /* lit a moment, then the trail */
            else tw_on &= ~(1u << i);
        }
    return m;
}
void od_leds_key_levels(uint8_t *lv)
{
    uint32_t now = od_ms(), m = od_leds_keys() | idle_twinkle(now), i;
    for (i = 0; i < 27u; i++) {
        uint32_t b = 1u << i;
        if (m & b) { lv[i] = 3; led_trail &= ~b; continue; }
        if (led_prev & b) { led_trail |= b; led_off_ms[i] = now; }
        lv[i] = 0;
        if (led_trail & b) {
            uint32_t d = now - led_off_ms[i];
            if (d < TRAIL_MID_MS) lv[i] = 2;
            else if (d < TRAIL_END_MS) lv[i] = 1;
            else led_trail &= ~b;
        }
    }
    led_prev = m;
}
uint32_t od_leds_buttons(void)           /* bit b: button b (OB_*) lit */
{
    static const uint8_t PB[PG_N] = {OB_HOME, OB_EDIT, OB_ENV, OB_LFO, OB_ARP, OB_FX, OB_SEQ, OB_SEQ, OB_GLO};
    uint32_t m = 1u << PB[page];
    if (hold) m |= 1u << OB_SCL;
    if (sq.playing) m |= 1u << OB_PLAY;
    if (rec_on) m |= 1u << OB_REC;
    return m;
}
