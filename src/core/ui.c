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
enum { PG_SYNTH, PG_FILTER, PG_ENV, PG_MOD, PG_ARP, PG_FX, PG_GLO, PG_N };
static const char *const PG_TITLE[PG_N] = {"HOME", "FILTER", "ENVELOPE", "LFO", "ARPEGGIO", "EFFECTS", "GLOBAL"};
static int page = PG_SYNTH;
static uint8_t hold;
static int oct_shift;
static int knob_touch = -1;
static uint32_t knob_touch_ms, toast_ms, boot_ms;
static char toast_txt[24];
static uint32_t keys_down;               /* the panel's keys held (bit k) */
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

static float exp_map(float v, float l, float h) { return l * od_exp2(v * od_log2(h / l)); }
static float exp_unmap(float x, float l, float h) { if (x < l) x = l; return od_log2(x / l) / od_log2(h / l); }
static float clampf(float x, float l, float h) { return x < l ? l : x > h ? h : x; }

/* ---------------------------------------------------------------- the knobs of each page --- */
enum { K_WAVE, K_DETUNE, K_MIX, K_DRIVE, K_CUT, K_RESO, K_FENV, K_KTRK, K_A, K_D, K_S, K_R, K_LFO, K_VIB, K_LFOCUT,
       K_FDEC, K_APAT, K_ABPM, K_AOCT, K_AGATE, K_CHO, K_ECO, K_ETIME, K_REV, K_VOL, K_OCT, K_TEMPO, K_AI };
static const uint8_t PG_K[PG_N][4] = {
    {K_WAVE, K_DETUNE, K_MIX, K_DRIVE}, {K_CUT, K_RESO, K_FENV, K_KTRK}, {K_A, K_D, K_S, K_R},
    {K_LFO, K_VIB, K_LFOCUT, K_FDEC}, {K_APAT, K_ABPM, K_AOCT, K_AGATE}, {K_CHO, K_ECO, K_ETIME, K_REV},
    {K_VOL, K_OCT, K_TEMPO, K_AI}};
static const char *const K_LBL[] = {"Wave", "Detune", "Mix", "Drive", "Cutoff", "Reso", "Env", "KeyTrk", "Attack",
    "Decay", "Sustain", "Release", "LFO", "Vibrato", "LFO>Cut", "F.Decay", "Pattern", "Tempo", "Octaves", "Gate",
    "Chorus", "Echo", "Time", "Reverb", "Volume", "Octave", "Tempo", "Arp AI"};
static void ai_apply(void);
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
    mb_s = (uint32_t)i * 2654435761u + 99u;
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
static uint8_t gst_x[170], gst_y[170], gst_s[170];
static float gst_tw[170];
static void stars_init(void)
{
    int i;
    for (i = 0; i < 170; i++) {
        gst_x[i] = (uint8_t)(rnd() % 240u);
        gst_y[i] = (uint8_t)(rnd() % 157u);
        gst_tw[i] = rndf() * 6.28f;
        gst_s[i] = rndf() < .12f ? 2 : 1;
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
    label(OD_PRESETS[preset_idx].name, 10, 16, viz.accent);
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
static void dr_glo(float W, float H)
{
    static const char *const K[7] = {"FIRMWARE", "ENGINE", "VOICES", "VOLUME", "OCTAVE", "HOLD", "ARP AI"};
    char v[7][24];
    int i;
    (void)W; (void)H;
    cat(cat(v[0], "ONDADREAM "), OD_VERSION);
    cat(v[1], "2 OSC + FILTER");
    cat(fmt_i(v[2], voices_active()), " / 8");
    pct_txt(v[3], P.vol);
    { char *e = v[4]; if (oct_shift > 0) *e++ = '+'; fmt_i(e, oct_shift); }
    cat(v[5], hold ? "ON" : "OFF");
    cat(v[6], ai_on ? "ON" : "OFF");
    for (i = 0; i < 7; i++) {
        text(&F_SILK8, 10, 14.f + (float)i * 15.f, K[i], C_FAINT, TA_L);
        text(&F_PX10, 230, 14.f + (float)i * 15.f, v[i], hexc(0xE9EBEE), TA_R);
    }
}

/* ---- the frame's parts ---- */
static void dr_header(void)
{
    char b[24], *e;
    int i, lit;
    if (viz_for != preset_idx) viz_make(preset_idx);
    e = cat(b, "PRESET ");
    if (preset_idx + 1 < 10) *e++ = '0';
    e = fmt_i(e, preset_idx + 1);
    e = cat(e, "/");
    fmt_i(e, OD_NPRESETS);
    text(&F_SILK8, 5, 12, b, C_INK, TA_L);
    text(&F_SILK8, 150, 12, PG_TITLE[page], viz.accent, TA_C);
    lit = (int)((float)lvl_peak / 32767.f * 6.f + .5f);
    for (i = 0; i < 6; i++) fill_rect(203 + i * 6, 5, 4, 7, i < lit ? (i < 4 ? C_GREEN : C_RED) : hexc(0x2C2F34));
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
        for (i = 0; K_LBL[id][i] && i < 7; i++) u[i] = (char)(K_LBL[id][i] >= 'a' && K_LBL[id][i] <= 'z' ? K_LBL[id][i] - 32 : K_LBL[id][i]);
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
int od_frame(uint16_t *frame)
{
    uint32_t now = od_ms();
    int k;
    fb = frame;
    g_reset();
    if (od_splash()) {
        dr_splash(now - boot_ms);
        return 1;
    }
    fill_rect(0, 0, 240, 240, C_BG);
    dr_header();
    /* the scene: 240 x 157 from y 19 */
    for (k = 0; k < 170; k++) {
        float a = .12f + .22f * (.5f + .5f * g_sin((float)now * .0015f + gst_tw[k]));
        blend(gst_x[k], 19 + gst_y[k], hexc(0xC3CDDC), (uint32_t)(a * 256.f));
    }
    g_save();
    g_translate(0, 19);
    switch (page) {
    case PG_SYNTH: dr_scene(240, 157, now); break;
    case PG_FILTER: dr_filter(240, 157); break;
    case PG_ENV: g_scale(.75f, .75f); dr_env(320, 209); break;
    case PG_MOD: dr_mod(240, 157, now); break;
    case PG_ARP: dr_arp(240, 157, now); break;
    case PG_FX: dr_fx(240, 157, now); break;
    default: dr_glo(240, 157);
    }
    g_restore();
    fill_rect(0, 176, 240, 64, C_BG);          /* (the scene stays above the cards) */
    dr_cards(now);
    if (toast_ms && now - toast_ms < 900u) {
        fill_rect_a(20, 150, 200, 22, hexc(0x08090C), 235);
        text(&F_SILK8, 120, 165, toast_txt, C_ORANGE, TA_C);
    }
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
    pal_init();
    stars_init();
    synth_init();
    boot_ms = od_ms();
}
void od_key(uint32_t k, int down)
{
    int n;
    if (k >= 27u) return;
    n = KB_BASE + 12 * oct_shift + (int)k;
    if (down) { keys_down |= 1u << k; key_on(n, .85f); }
    else { keys_down &= ~(1u << k); key_off(n); }
}
void od_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t c = st & 0xF0u;
    if (c == 0x90u && d2) { midi_held[d1 & 127u] = 1; key_on(d1 & 127u, (float)d2 / 127.f); }
    else if (c == 0x80u || (c == 0x90u && !d2)) { midi_held[d1 & 127u] = 0; key_off(d1 & 127u); }
}
static void set_page(int p) { page = p; }
void od_button(uint32_t b, int down)
{
    if (!down || od_splash()) return;
    switch (b) {
    case OB_HOME: set_page(PG_SYNTH); break;
    case OB_EDIT: set_page(PG_FILTER); break;
    case OB_ENV: set_page(PG_ENV); break;
    case OB_LFO: set_page(PG_MOD); break;
    case OB_ARP: set_page(PG_ARP); break;
    case OB_FX: set_page(PG_FX); break;
    case OB_GLO: set_page(PG_GLO); break;
    case OB_SCL:
        hold = (uint8_t)!hold;
        if (!hold) { int i; for (i = arp.nheld - 1; i >= 0; i--) { int n = arp.held[i]; if (!(n >= KB_BASE + 12 * oct_shift && n < KB_BASE + 12 * oct_shift + 27 && (keys_down >> (n - KB_BASE - 12 * oct_shift) & 1u)) && !midi_held[n & 127]) { held_remove(n); direct_off(n); } } }
        toast(hold ? "HOLD ON" : "HOLD OFF");
        break;
    case OB_REC:
        ai_on = (uint8_t)!ai_on;
        if (ai_on) ai_apply();
        toast(ai_on ? "ARP AI ON" : "ARP AI OFF");
        break;
    case OB_SEQ: case OB_PLAY: toast("SEQUENCER: NEXT VERSION"); break;
    case OB_SAVE: toast("SAVE: NEXT VERSION"); break;
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
    if (!s || od_splash()) return;
    switch (e) {
    case OE_PRESET: preset_go(preset_idx + s); break;
    case OE_SELECT: { int p = page + (s > 0 ? 1 : -1); set_page((p + PG_N) % PG_N); } break;
    case OE_ALGO: P.wave = (float)CLAMP((int32_t)(P.wave + .5f) + s, 0, 3); break;
    default: {
        int k = (int)e - OE_K1, id = PG_K[page][k];
        float step = (id == K_WAVE ? 1.f / 3.f : id == K_APAT ? .25f : id == K_AOCT ? .5f : id == K_OCT ? 1.f / 6.f : id == K_AI ? 1.f : 1.f / 128.f);
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
    for (i = 0; i < NV; i++)
        if (vc[i].active && !vc[i].rel && (uint32_t)vc[i].note >= base && (uint32_t)vc[i].note < base + 27u)
            m |= 1u << ((uint32_t)vc[i].note - base);
    return m;
}
uint32_t od_leds_buttons(void)           /* bit b: button b (OB_*) lit */
{
    static const uint8_t PB[PG_N] = {OB_HOME, OB_EDIT, OB_ENV, OB_LFO, OB_ARP, OB_FX, OB_GLO};
    uint32_t m = 1u << PB[page];
    if (hold) m |= 1u << OB_SCL;
    if (ai_on) m |= 1u << OB_REC;
    return m;
}
