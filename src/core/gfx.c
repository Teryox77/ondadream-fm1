/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The screen's drawing: a 240 x 240 RGB565 frame and a small canvas-like API (a transform, paths of lines, arcs,
 * ellipses and curves, stroked with anti-aliased edges and an optional glow; filled dots and boxes; text in the
 * generated fonts), so that the OndaDream web synth's drawings come over one for one. Float, at frame rate. */
#include "od_fonts.h"

#define GW 240
#define GH 240
static uint16_t *fb;

static inline uint16_t RGB(uint32_t r, uint32_t g, uint32_t b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
static uint16_t hexc(uint32_t h) { return RGB(h >> 16, (h >> 8) & 255u, h & 255u); }
static inline uint16_t mix565(uint16_t a, uint16_t b, uint32_t t)   /* t 0..256: a -> b */
{
    uint32_t r = ((a >> 11) * (256u - t) + (b >> 11) * t) >> 8;
    uint32_t g = (((a >> 5) & 63u) * (256u - t) + ((b >> 5) & 63u) * t) >> 8;
    uint32_t bl = ((a & 31u) * (256u - t) + (b & 31u) * t) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static inline void blend(int32_t x, int32_t y, uint16_t c, uint32_t a)   /* a 0..256 */
{
    uint16_t *p;
    if ((uint32_t)x >= GW || (uint32_t)y >= GH || !a)
        return;
    p = &fb[y * GW + x];
    *p = a >= 256u ? c : mix565(*p, c, a);
}
static void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GW) w = GW - x;
    if (y + h > GH) h = GH - y;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            fb[(y + j) * GW + x + i] = c;
}
static void fill_rect_a(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t a)
{
    int32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            blend(x + i, y + j, c, a);
}

/* ---------------------------------------------------------------- the canvas --- */
static struct {
    float a, b, c, d, e, f;              /* x' = a x + c y + e, y' = b x + d y + f */
    uint16_t col;
    float lw;
    uint8_t glow;
    float alpha;                         /* the global alpha, 0..1 */
} gs;
#define GS_STACK 6
static float gstk[GS_STACK][6];
static int gsp;
static void g_reset(void) { gs.a = gs.d = 1.f; gs.b = gs.c = gs.e = gs.f = 0.f; gs.alpha = 1.f; gs.glow = 0; gs.lw = 2.f; gsp = 0; }
static void g_save(void) { if (gsp < GS_STACK) { float *s = gstk[gsp++]; s[0] = gs.a; s[1] = gs.b; s[2] = gs.c; s[3] = gs.d; s[4] = gs.e; s[5] = gs.f; } }
static void g_restore(void) { if (gsp > 0) { float *s = gstk[--gsp]; gs.a = s[0]; gs.b = s[1]; gs.c = s[2]; gs.d = s[3]; gs.e = s[4]; gs.f = s[5]; } }
static void g_translate(float x, float y) { gs.e += gs.a * x + gs.c * y; gs.f += gs.b * x + gs.d * y; }
static void g_scale(float sx, float sy) { gs.a *= sx; gs.b *= sx; gs.c *= sy; gs.d *= sy; }
static float g_sin(float x);
static float g_cos(float x) { return g_sin(x + 1.5707963f); }
static void g_rotate(float r)
{
    float cs = g_cos(r), sn = g_sin(r), a = gs.a, b = gs.b, c = gs.c, d = gs.d;
    gs.a = a * cs + c * sn; gs.b = b * cs + d * sn;
    gs.c = -a * sn + c * cs; gs.d = -b * sn + d * cs;
}
static void tx(float x, float y, float *X, float *Y) { *X = gs.a * x + gs.c * y + gs.e; *Y = gs.b * x + gs.d * y + gs.f; }
static float g_scale_of(void) { float s = gs.a * gs.a + gs.b * gs.b; return s > 0.f ? (s < 1.f ? (1.f + s) * .5f : s) : 1.f; }

/* sin: the core's sine table, linear between steps */
static float g_sin(float x)
{
    float t = x * (1024.f / 6.2831853f), fr;
    int32_t i = (int32_t)t;
    if ((float)i > t) i--;
    fr = t - (float)i;
    {
        uint32_t k = (uint32_t)i & 1023u;
        return ((float)OD_SINE[k] + ((float)OD_SINE[k + 1u] - (float)OD_SINE[k]) * fr) * (1.f / 32767.f);
    }
}
static float g_sqrt(float x)
{
    union { float f; uint32_t u; } r;
    float y;
    if (x <= 0.f) return 0.f;
    r.f = x;
    r.u = 0x1FBD1DF5u + (r.u >> 1);
    y = r.f;
    y = .5f * (y + x / y);
    return .5f * (y + x / y);
}

/* one segment, anti-aliased, width w (screen px), colour c, alpha a (0..1) */
static void seg(float x0, float y0, float x1, float y1, float w, uint16_t c, float a)
{
    float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy, r = w * .5f;
    int32_t xa = (int32_t)((x0 < x1 ? x0 : x1) - r - 1.f), xb = (int32_t)((x0 > x1 ? x0 : x1) + r + 2.f);
    int32_t ya = (int32_t)((y0 < y1 ? y0 : y1) - r - 1.f), yb = (int32_t)((y0 > y1 ? y0 : y1) + r + 2.f);
    int32_t x, y;
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb > GW) xb = GW;
    if (yb > GH) yb = GH;
    for (y = ya; y < yb; y++)
        for (x = xa; x < xb; x++) {
            float px = (float)x + .5f - x0, py = (float)y + .5f - y0, t = l2 > 0.f ? (px * dx + py * dy) / l2 : 0.f, qx, qy, d, cov;
            if (t < 0.f) t = 0.f;
            if (t > 1.f) t = 1.f;
            qx = px - t * dx;
            qy = py - t * dy;
            d = qx * qx + qy * qy;
            if (d > (r + 1.f) * (r + 1.f))
                continue;
            d = g_sqrt(d);
            cov = r + .5f - d;
            if (cov <= 0.f)
                continue;
            if (cov > 1.f) cov = 1.f;
            blend(x, y, c, (uint32_t)(cov * a * 256.f));
        }
}

/* paths: points in user space, flattened as they come */
#define PMAX 160
static float pX[PMAX], pY[PMAX];
static uint8_t pM[PMAX];                 /* 1: a subpath starts here */
static int pn;
static void g_begin(void) { pn = 0; }
static void g_move(float x, float y) { if (pn < PMAX) { tx(x, y, &pX[pn], &pY[pn]); pM[pn++] = 1; } }
static void g_line(float x, float y) { if (!pn) { g_move(x, y); return; } if (pn < PMAX) { tx(x, y, &pX[pn], &pY[pn]); pM[pn] = 0; pn++; } }
static void g_ellipse(float cx, float cy, float rx, float ry, float rot, float a0, float a1)
{
    int i, n = 28;
    float cr = g_cos(rot), sr = g_sin(rot);
    for (i = 0; i <= n; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)n;
        float ex = rx * g_cos(a), ey = ry * g_sin(a);
        float x = cx + ex * cr - ey * sr, y = cy + ex * sr + ey * cr;
        if (i == 0) g_move(x, y); else g_line(x, y);
    }
}
static void g_arc(float cx, float cy, float r, float a0, float a1) { g_ellipse(cx, cy, r, r, 0.f, a0, a1); }
static void g_quad(float cx, float cy, float x, float y)   /* from the last point */
{
    float x0, y0, ia = 1.f, det;
    int i;
    if (!pn) return;
    /* the last point back in user space */
    det = gs.a * gs.d - gs.b * gs.c;
    if (det != 0.f) ia = 1.f / det;
    x0 = ((pX[pn - 1] - gs.e) * gs.d - (pY[pn - 1] - gs.f) * gs.c) * ia;
    y0 = ((pY[pn - 1] - gs.f) * gs.a - (pX[pn - 1] - gs.e) * gs.b) * ia;
    for (i = 1; i <= 10; i++) {
        float t = (float)i / 10.f, u = 1.f - t;
        g_line(u * u * x0 + 2.f * u * t * cx + t * t * x, u * u * y0 + 2.f * u * t * cy + t * t * y);
    }
}
static void g_rrect(float x, float y, float w, float h, float r)
{
    g_move(x + r, y);
    g_line(x + w - r, y);
    g_ellipse(x + w - r, y + r, r, r, 0.f, -1.5708f, 0.f);
    g_line(x + w, y + h - r);
    g_ellipse(x + w - r, y + h - r, r, r, 0.f, 0.f, 1.5708f);
    g_line(x + r, y + h);
    g_ellipse(x + r, y + h - r, r, r, 0.f, 1.5708f, 3.1416f);
    g_line(x, y + r);
    g_ellipse(x + r, y + r, r, r, 0.f, 3.1416f, 4.7124f);
}
/* a LED line: the colour and width (the web synth's led()): with its glow */
static void led(uint16_t c, float w) { gs.col = c; gs.lw = w; gs.glow = 1; }
static void no_glow(void) { gs.glow = 0; }
static void g_stroke(void)
{
    int i, pass;
    float w = gs.lw * g_scale_of();
    if (w > 3.2f) w = 3.2f;                      /* (the 240 px screen: the web synth's 2.4 px at 2x is ~2 px) */
    if (w < 1.f) w = 1.f;
    for (pass = gs.glow ? 0 : 1; pass < 2; pass++)
        for (i = 1; i < pn; i++)
            if (!pM[i])
                seg(pX[i - 1], pY[i - 1], pX[i], pY[i], pass ? w * .8f : w + 3.f, gs.col, gs.alpha * (pass ? 1.f : .16f));
}
/* a filled dot with a glow (the web synth's dot()) */
static void dot(float x, float y, float r, uint16_t c)
{
    float X, Y, R = r * g_scale_of(), gr = R + 2.5f;
    int32_t i, j;
    tx(x, y, &X, &Y);
    for (j = (int32_t)(Y - gr - 1.f); j <= (int32_t)(Y + gr + 1.f); j++)
        for (i = (int32_t)(X - gr - 1.f); i <= (int32_t)(X + gr + 1.f); i++) {
            float dx = (float)i + .5f - X, dy = (float)j + .5f - Y, d = g_sqrt(dx * dx + dy * dy);
            float cov = R + .5f - d, glow = (gr - d) / 2.5f;
            if (cov > 1.f) cov = 1.f;
            if (cov > 0.f) blend(i, j, c, (uint32_t)(cov * gs.alpha * 256.f));
            else if (glow > 0.f) blend(i, j, c, (uint32_t)(glow * .22f * gs.alpha * 256.f));
        }
}
/* a filled rounded box, anti-aliased (screen space) */
static void fill_rrect(float x, float y, float w, float h, float r, uint16_t c, float a)
{
    int32_t i, j;
    for (j = (int32_t)y; j < (int32_t)(y + h + .999f); j++)
        for (i = (int32_t)x; i < (int32_t)(x + w + .999f); i++) {
            float px = (float)i + .5f, py = (float)j + .5f, qx = 0.f, qy = 0.f, cov;
            if (px < x + r) qx = x + r - px; else if (px > x + w - r) qx = px - (x + w - r);
            if (py < y + r) qy = y + r - py; else if (py > y + h - r) qy = py - (y + h - r);
            cov = r + .5f - g_sqrt(qx * qx + qy * qy);
            if (cov > 1.f) cov = 1.f;
            if (cov > 0.f) blend(i, j, c, (uint32_t)(cov * a * 256.f));
        }
}

/* ---------------------------------------------------------------- text --- */
static const od_glyph_t *glyph_of(const od_font_t *f, char ch)
{
    int i;
    for (i = 0; i < f->n; i++)
        if (f->chars[i] == ch) return &f->g[i];
    return 0;
}
static int32_t text_w16(const od_font_t *f, const char *s)   /* 1/16 px */
{
    int32_t w = 0;
    for (; *s; s++) { const od_glyph_t *g = glyph_of(f, *s); if (g) w += g->adv16; }
    return w;
}
static int32_t text_w(const od_font_t *f, const char *s) { return (text_w16(f, s) + 8) >> 4; }
enum { TA_L, TA_C, TA_R };
/* text with its baseline at y; x by align, as the canvas does it (the pen in 1/16 px, each glyph on the nearest
 * pixel); the anchor goes through the transform, the glyphs keep their size (a scaled page picks a smaller font) */
static int32_t text(const od_font_t *f, float fx, float fy, const char *s, uint16_t c, int al)
{
    float px = gs.a * fx + gs.c * fy + gs.e, py = gs.b * fx + gs.d * fy + gs.f;   /* the anchor, transformed */
    int32_t x16 = (int32_t)(px * 16.f + .5f), y = (int32_t)(py + .5f), w = text_w16(f, s), x0;
    if (al == TA_C) x16 -= w / 2;
    else if (al == TA_R) x16 -= w;
    x0 = x16;
    for (; *s; s++) {
        const od_glyph_t *g = glyph_of(f, *s);
        uint32_t k, i, j;
        int32_t x = (x16 + 8) >> 4;
        if (!g) continue;
        for (j = 0, k = g->off * 2u; j < g->h; j++)
            for (i = 0; i < g->w; i++, k++) {
                uint32_t v = (f->px[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
                if (v) blend(x + g->x + (int32_t)i, y - g->y + (int32_t)j, c, (uint32_t)((float)(v * 17u + 1u) * gs.alpha));
            }
        x16 += g->adv16;
    }
    return (x16 - x0 + 8) >> 4;
}

/* ---------------------------------------------------------------- shapes of the SEQUENCER page --- */
/* (screen space, as the canvas fills them: coverage-weighted edges) */
static float g_atan2(float y, float x)
{
    float ax = x < 0.f ? -x : x, ay = y < 0.f ? -y : y, a, s, r;
    if (ax == 0.f && ay == 0.f) return 0.f;
    a = (ax < ay ? ax : ay) / (ax > ay ? ax : ay);
    s = a * a;
    r = ((-.0464964749f * s + .15931422f) * s - .327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0.f) r = 3.14159274f - r;
    return y < 0.f ? -r : r;
}
static float cov01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
static void fill_circle(float cx, float cy, float r, uint16_t c, float a)
{
    int32_t i, j;
    for (j = (int32_t)(cy - r - 1.f); j <= (int32_t)(cy + r + 1.f); j++)
        for (i = (int32_t)(cx - r - 1.f); i <= (int32_t)(cx + r + 1.f); i++) {
            float dx = (float)i + .5f - cx, dy = (float)j + .5f - cy, cov = cov01(r + .5f - g_sqrt(dx * dx + dy * dy));
            if (cov > 0.f) blend(i, j, c, (uint32_t)(cov * a * 256.f));
        }
}
static void fill_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c, float a)
{
    float xa = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2), xb = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    float ya = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), yb = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    float ar = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    int32_t i, j, u, v;
    for (j = (int32_t)ya; j <= (int32_t)yb; j++)
        for (i = (int32_t)xa; i <= (int32_t)xb; i++) {
            int n = 0;
            for (v = 0; v < 4; v++)
                for (u = 0; u < 4; u++) {
                    float px = (float)i + ((float)u + .5f) * .25f, py = (float)j + ((float)v + .5f) * .25f;
                    float e0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0), e1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
                    float e2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
                    if (ar < 0.f) { e0 = -e0; e1 = -e1; e2 = -e2; }
                    n += e0 >= 0.f && e1 >= 0.f && e2 >= 0.f;
                }
            if (n) blend(i, j, c, (uint32_t)((float)n * (1.f / 16.f) * a * 256.f));
        }
}
/* a ring's arc, width w, flat ends (lineCap "butt"), angles a0 < a1 (radians, clockwise on the screen) */
static void arc_butt(float cx, float cy, float r, float w, float a0, float a1, uint16_t c)
{
    float hw = w * .5f;
    int32_t i, j;
    if (a1 - a0 <= 0.f) return;
    for (j = (int32_t)(cy - r - hw - 1.f); j <= (int32_t)(cy + r + hw + 1.f); j++)
        for (i = (int32_t)(cx - r - hw - 1.f); i <= (int32_t)(cx + r + hw + 1.f); i++) {
            float dx = (float)i + .5f - cx, dy = (float)j + .5f - cy, d = g_sqrt(dx * dx + dy * dy), dr = d - r, cr, ca, th;
            cr = cov01(hw + .5f - (dr < 0.f ? -dr : dr));
            if (cr <= 0.f) continue;
            th = g_atan2(dy, dx);
            while (th < a0) th += 6.2831853f;
            while (th >= a0 + 6.2831853f) th -= 6.2831853f;
            if (th <= a1) ca = cov01(.5f + (th - a0 < a1 - th ? th - a0 : a1 - th) * d);
            else { float g0 = th - a1, g1 = a0 + 6.2831853f - th; ca = cov01(.5f - (g0 < g1 ? g0 : g1) * d); }
            if (cr * ca > 0.f) blend(i, j, c, (uint32_t)(cr * ca * 256.f));
        }
}
static void line_butt(float x0, float y0, float x1, float y1, float w, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0, len = g_sqrt(dx * dx + dy * dy), hw = w * .5f;
    int32_t i, j;
    if (len <= 0.f) return;
    dx /= len;
    dy /= len;
    for (j = (int32_t)((y0 < y1 ? y0 : y1) - hw - 1.f); j <= (int32_t)((y0 > y1 ? y0 : y1) + hw + 1.f); j++)
        for (i = (int32_t)((x0 < x1 ? x0 : x1) - hw - 1.f); i <= (int32_t)((x0 > x1 ? x0 : x1) + hw + 1.f); i++) {
            float px = (float)i + .5f - x0, py = (float)j + .5f - y0, t = px * dx + py * dy, q = px * dy - py * dx;
            float cov = cov01(hw + .5f - (q < 0.f ? -q : q)) * cov01((t < len - t ? t : len - t) + .5f);
            if (cov > 0.f) blend(i, j, c, (uint32_t)(cov * 256.f));
        }
}
