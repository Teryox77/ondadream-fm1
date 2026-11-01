/* SPDX-License-Identifier: GPL-3.0-only
 * OndaDream (a Felucca fork, Teryox77): the HOME creature. In place of the oscilloscope, a little line-art space
 * creature for the selected track's sound (UFO, alien, rocket, satellite, jelly-alien, robot, jelly, probe), on a
 * starfield, with a short trace of the output under it. The creature, its motion and its reaction are drawn from
 * the sound (engine + preset): every sound has its own. It reacts to the notes (the output's level): it hops,
 * shakes, spins, flashes or beams. Colours: the palette's (THEME the body, ACCENT the details, MID the stars), so
 * every palette keeps its look (GREY and MONO stay grey). Included by ui_graph.c, drawn on the graph canvas
 * (240 x H_GRAPH, the panel from x 3 to 237). */

enum { CR_UFO, CR_ALIEN, CR_ROCKET, CR_SAT, CR_JALIEN, CR_ROBOT, CR_JELLY, CR_PROBE, CR_N };
enum { CM_HOVER, CM_ORBIT, CM_DRIFT, CM_PULSE };
enum { CX_HOP, CX_BEAM, CX_SPIN, CX_FLASH, CX_SHAKE };

static uint32_t cr_hash(uint32_t a)
{
    a ^= a >> 16;
    a *= 0x7FEB352Du;
    a ^= a >> 15;
    a *= 0x846CA68Bu;
    a ^= a >> 16;
    return a;
}

/* sin / cos of a phase 0..1023 (a full turn), Q15 */
static inline int32_t cr_sin(uint32_t ph) { return SINE[ph & 1023u]; }
static inline int32_t cr_cos(uint32_t ph) { return SINE[(ph + 256u) & 1023u]; }

/* a 2 px outline of an ellipse, rotated by rot (0..1023) about (cx, cy) */
static void cr_ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t rot, uint16_t c)
{
    int32_t i, px = 0, py = 0;
    int32_t cr = cr_cos(rot), sr = cr_sin(rot);
    for (i = 0; i <= 48; i++) {
        uint32_t a = (uint32_t)i * 1024u / 48u;
        int32_t ex = rx * cr_cos(a) >> 15, ey = ry * cr_sin(a) >> 15;
        int32_t x = cx + ((ex * cr - ey * sr) >> 15), y = cy + ((ex * sr + ey * cr) >> 15);
        if (i)
            cv_line_t(px, py, x, y, c, 2);
        px = x;
        py = y;
    }
}

/* a half ellipse (the top: a dome), 2 px */
static void cr_dome(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)
{
    int32_t i, px = 0, py = 0;
    for (i = 0; i <= 24; i++) {
        uint32_t a = 512u + (uint32_t)i * 512u / 24u;          /* 180 .. 360 degrees: the upper half */
        int32_t x = cx + (rx * cr_cos(a) >> 15), y = cy + (ry * cr_sin(a) >> 15);
        if (i)
            cv_line_t(px, py, x, y, c, 2);
        px = x;
        py = y;
    }
}

static void cr_dot(int32_t x, int32_t y, uint16_t c)
{
    cv_rect(x - 1, y - 1, 3, 3, c);
}

/* a point (dx, dy) of the creature turned by rot about its centre */
static void cr_rot(int32_t cx, int32_t cy, uint32_t rot, int32_t dx, int32_t dy, int32_t *x, int32_t *y)
{
    int32_t cr = cr_cos(rot), sr = cr_sin(rot);
    *x = cx + ((dx * cr - dy * sr) >> 15);
    *y = cy + ((dx * sr + dy * cr) >> 15);
}

static void cr_seg(int32_t cx, int32_t cy, uint32_t rot, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t a, b, d, e;
    cr_rot(cx, cy, rot, x0, y0, &a, &b);
    cr_rot(cx, cy, rot, x1, y1, &d, &e);
    cv_line_t(a, b, d, e, c, 2);
}

static void cr_draw(uint32_t type, int32_t cx, int32_t cy, uint32_t rot, uint32_t eyes, uint32_t ant, uint32_t fr,
                    int flash, uint16_t body, uint16_t det)
{
    uint16_t b = flash ? T_TEXT : body;
    uint32_t i;
    switch (type) {
    case CR_UFO:
        cr_ellipse(cx, cy, 26, 8, 0, b);
        cr_dome(cx, cy - 3, 12, 12, b);
        for (i = 0; i < 5; i++)                                  /* the running lights */
            cr_dot(cx - 16 + (int32_t)i * 8, cy + 1, (fr / 4u + i) % 5u == 0 ? T_TEXT : det);
        break;
    case CR_ALIEN:
        cr_ellipse(cx, cy, 14, 17, 0, b);
        for (i = 0; i < eyes; i++)
            cr_dot(cx - (int32_t)(eyes - 1u) * 5 + (int32_t)i * 10, cy - 4, det);
        cv_line_t(cx - 5, cy + 8, cx + 5, cy + 8, det, 2);
        for (i = 0; i < ant; i++) {                              /* antennas that sway */
            int32_t sx = cx - 6 + (int32_t)i * 12, sw = cr_sin(fr * 24u + i * 300u) * 3 >> 15;
            cv_line_t(sx, cy - 15, sx + sw, cy - 25, b, 1);
            cr_dot(sx + sw, cy - 26, det);
        }
        break;
    case CR_ROCKET:
        cr_seg(cx, cy, rot, 0, -24, 9, -8, b);
        cr_seg(cx, cy, rot, 0, -24, -9, -8, b);
        cr_seg(cx, cy, rot, 9, -8, 9, 14, b);
        cr_seg(cx, cy, rot, -9, -8, -9, 14, b);
        cr_seg(cx, cy, rot, -9, 14, 9, 14, b);
        cr_seg(cx, cy, rot, -9, 6, -16, 18, b);
        cr_seg(cx, cy, rot, 9, 6, 16, 18, b);
        cr_ellipse(cx, cy - 4, 4, 4, 0, det);                   /* the porthole */
        if (fr & 2u) {                                           /* the flame flickers */
            cr_seg(cx, cy, rot, -5, 16, 0, 28, det);
            cr_seg(cx, cy, rot, 5, 16, 0, 28, det);
        }
        break;
    case CR_SAT:
        cr_ellipse(cx, cy, 8, 8, rot, b);
        cr_seg(cx, cy, rot, -8, 0, -14, 0, b);
        cr_seg(cx, cy, rot, 8, 0, 14, 0, b);
        for (i = 0; i < 2; i++) {                                /* the solar panels */
            int32_t s = i ? 1 : -1;
            cr_seg(cx, cy, rot, s * 14, -7, s * 30, -7, det);
            cr_seg(cx, cy, rot, s * 14, 7, s * 30, 7, det);
            cr_seg(cx, cy, rot, s * 30, -7, s * 30, 7, det);
            cr_seg(cx, cy, rot, s * 22, -7, s * 22, 7, det);
        }
        cr_seg(cx, cy, rot, 0, -8, 0, -16, b);
        break;
    case CR_JALIEN:
    case CR_JELLY:
        cr_dome(cx, cy, 16, 14, b);
        cv_line_t(cx - 16, cy, cx + 16, cy, b, 2);
        for (i = 0; i < 4; i++) {                                /* the tentacles wave */
            int32_t tx = cx - 12 + (int32_t)i * 8, w = cr_sin(fr * 20u + i * 256u) * 4 >> 15;
            cv_line_t(tx, cy + 1, tx + w, cy + 12, det, 2);
            cv_line_t(tx + w, cy + 12, tx - w, cy + 22, det, 2);
        }
        if (type == CR_JALIEN) {
            cr_dot(cx - 5, cy - 6, T_TEXT);
            cr_dot(cx + 5, cy - 6, T_TEXT);
        }
        break;
    case CR_ROBOT:
        cv_frame(cx - 14, cy - 14, 28, 26, b);
        cv_frame(cx - 13, cy - 13, 26, 24, b);
        cv_rect(cx - 8, cy - 7, 5, 5, det);
        cv_rect(cx + 3, cy - 7, 5, 5, det);
        for (i = 0; i < 4; i++)                                  /* the mouth: a little level meter */
            cv_rect(cx - 8 + (int32_t)i * 4, cy + 4, 3, 3, (fr / 3u + i) & 1u ? det : T_DIM);
        cv_line_t(cx, cy - 14, cx, cy - 22, b, 2);
        cr_dot(cx, cy - 23, (fr & 8u) ? det : T_TEXT);
        break;
    default:                                                     /* CR_PROBE */
        cr_ellipse(cx, cy, 10, 10, rot, b);
        for (i = 0; i < 3; i++) {
            int32_t ax, ay;
            cr_rot(cx, cy, rot + i * 341u, 0, -10, &ax, &ay);
            cr_seg(cx, cy, rot + i * 341u, 0, -10, 0, -20, b);
            cr_dot(ax + (ax - cx), ay + (ay - cy), det);
        }
        cr_dot(cx, cy, det);
        break;
    }
}

static void graph_creature(const track_t *t)
{
    uint32_t id = (uint32_t)t->eng_req * 131u + t->preset + (t->user ? 977u * t->user : 0u);
    uint32_t h = cr_hash(id * 2654435761u + 99u), h2 = cr_hash(h);
    uint32_t type = h % CR_N, motion = (h >> 4) % 4u, react = (h >> 7) % 5u;
    uint32_t eyes = 1u + (h >> 10) % 3u, ant = (h >> 12) % 3u;
    uint32_t fr = ui.frame, i, rot = 0;
    uint32_t speed = 8u + (h >> 14) % 12u;                      /* phase steps a frame */
    int32_t W = 240, H = (int32_t)cv_h, cx = W / 2, cy = H * 2 / 5, x, py = 0;
    int32_t peak = 1, lvl;
    static uint32_t last_peak_frame;
    uint16_t body = T_THEME, det = T_ACCENT;
    if (ux.onda) {                                              /* OndaDream: the sound's own two colours */
        static const uint16_t CR_ACC[8] = {RGB(62, 123, 255), RGB(0, 200, 120), RGB(255, 122, 26), RGB(255, 51, 85),
                                        RGB(178, 107, 255), RGB(41, 210, 224), RGB(154, 224, 0), RGB(255, 107, 213)};
        uint32_t a1 = h2 % 8u, a2 = (h2 >> 5) % 8u;
        if (a2 == a1)
            a2 = (a1 + 3u) % 8u;
        body = CR_ACC[a1];
        det = CR_ACC[a2];
    }
    /* the output's level (the scope's buffer): the creature reacts to it */
    for (i = 0; i < SCOPE_N; i += 4u) {
        int32_t s = scope_buf[i];
        if (s < 0)
            s = -s;
        if (s > peak)
            peak = s;
    }
    lvl = peak > 1200 ? (peak > 20000 ? 100 : peak / 200) : 0;  /* 0..100 */
    if (lvl > 25)
        last_peak_frame = fr;
    /* stars: fixed for the sound, twinkling */
    for (i = 0; i < 26u; i++) {
        uint32_t s = cr_hash(h2 + i * 7919u);
        int32_t sx = 6 + (int32_t)(s % 228u), sy = 3 + (int32_t)((s >> 8) % (uint32_t)(H - 30));
        if (((s >> 16) + fr / 6u) % 7u)
            cv_pset(sx, sy, ((s >> 20) & 3u) ? T_DIM : T_MID);
    }
    if ((h2 >> 3) & 1u) {                                       /* sometimes a planet, with a ring */
        int32_t px0 = 30 + (int32_t)((h2 >> 5) % 180u), py0 = 16 + (int32_t)((h2 >> 13) % 20u);
        int32_t pr = 7 + (int32_t)((h2 >> 18) % 8u);
        cr_ellipse(px0, py0, pr, pr, 0, T_DIM);
        if ((h2 >> 23) & 1u)
            cr_ellipse(px0, py0, pr * 17 / 10, pr / 2, 90u, T_DIM);
    }
    /* motion */
    {
        uint32_t ph = fr * speed;
        if (motion == CM_HOVER)
            cy += cr_sin(ph) * 6 >> 15;
        else if (motion == CM_ORBIT) {
            cx += cr_cos(ph / 2u) * 52 >> 15;
            cy += cr_sin(ph / 2u) * 8 >> 15;
        } else if (motion == CM_DRIFT)
            cx = (int32_t)((ph / 8u) % (uint32_t)(W + 80)) - 40;
        else
            cy += cr_sin(ph * 2u) * 2 >> 15;
    }
    /* reaction to the notes */
    {
        uint32_t since = fr - last_peak_frame;
        int flash = 0;
        if (since < 6u) {
            if (react == CX_HOP)
                cy -= (int32_t)(6u - since) * 2;
            else if (react == CX_SHAKE)
                cx += (fr & 1u) ? 3 : -3;
            else if (react == CX_FLASH)
                flash = 1;
        }
        if (react == CX_SPIN || type == CR_SAT || type == CR_PROBE)
            rot = fr * (4u + (uint32_t)lvl / 4u);
        if (react == CX_BEAM && since < 10u && type != CR_ROCKET) {   /* a tractor beam down to the trace */
            cv_line(cx - 6, cy + 12, cx - 16, H - 22, det);
            cv_line(cx + 6, cy + 12, cx + 16, H - 22, det);
        }
        cr_draw(type, cx, cy, rot, eyes, ant, fr, flash, body, det);
    }
    /* the output's trace along the bottom, small */
    {
        int32_t base = H - 12, a = 7 + lvl / 12;
        uint32_t trig = 0;
        for (i = 1; i < SCOPE_N - 240u; i++)
            if (scope_buf[i - 1] < 0 && scope_buf[i] >= 0) {
                trig = i;
                break;
            }
        for (x = 0; x < PANEL_W; x++) {
            int32_t y = base - scope_buf[(trig + (uint32_t)x) & (SCOPE_N - 1u)] * a / (peak > 1500 ? peak : 1500);
            if (x)
                cv_line(PANEL_X0 - 1 + x, py, PANEL_X0 + x, y, det);
            py = y;
        }
    }
}
