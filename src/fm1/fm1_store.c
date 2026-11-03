/* OndaDream firmware. Copyright (C) 2026 Teryox77. All rights reserved.
 * The FM-1's saved presets and settings in its flash. The platform gives store_fread / store_erase (4 KiB) /
 * store_prog / store_quiet; tests/store_test.c runs this against a flash in RAM (power cuts, damage). */
/* ---- the saved presets: two 4 KiB sectors taken in turn (the board's own-data flash below the stores, which no
 * installer writes: bsp/chip/fm1_flash.h FL_XDATA0), each a header (magic, CRC-32, sequence, length; the CRC over
 * the sequence, the length and the image) and the core's
 * image; the header goes in last, so a save cut by a power-off leaves the other sector, the one before, in charge ---- */
#define STORE_A 0x93000u
#define STORE_B 0x94000u
#define STORE_MAGIC 0x3153444Fu                 /* "ODS1" */
#define STORE_HDR 16u
static uint8_t store_buf[STORE_HDR + OD_STORE_MAX_BYTES];
static uint32_t store_at, store_seq;            /* the sector in charge (0 none) and its sequence */
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static uint32_t rd32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void wr32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static int store_read(uint32_t at, uint32_t *seq)   /* the sector's image into store_buf; its length, or 0 */
{
    uint32_t len;
    if (store_fread(at, store_buf, STORE_HDR) || rd32(store_buf) != STORE_MAGIC)
        return 0;
    len = rd32(store_buf + 12);
    if (len < 8u || len > OD_STORE_MAX_BYTES || store_fread(at + STORE_HDR, store_buf + STORE_HDR, len))
        return 0;
    if (crc32(store_buf + 8, 8u + len) != rd32(store_buf + 4))
        return 0;
    *seq = rd32(store_buf + 8);
    return (int)len;
}
static void store_boot(void)
{
    uint32_t sa = 0, sb = 0;
    int la, lb;
    la = store_read(STORE_A, &sa);
    lb = store_read(STORE_B, &sb);
    if (lb && (!la || (int32_t)(sb - sa) > 0)) {
        store_at = STORE_B; store_seq = sb;
    } else if (la) {
        store_at = STORE_A; store_seq = sa;
    } else
        return;
    if (store_read(store_at, &sa) && od_store_load(store_buf + STORE_HDR, rd32(store_buf + 12)) == 0)
        return;
    store_at = 0;                               /* (not a store the core takes: the next save starts over) */
}
static int store_save(void)
{
    uint32_t len = od_store_image(store_buf + STORE_HDR, OD_STORE_MAX_BYTES), at = store_at == STORE_A ? STORE_B : STORE_A;
    uint32_t i;
    uint8_t chk[64];
    if (!len)
        return 0;
    wr32(store_buf, STORE_MAGIC);
    wr32(store_buf + 8, store_seq + 1u);
    wr32(store_buf + 12, len);
    wr32(store_buf + 4, crc32(store_buf + 8, 8u + len));
    store_quiet();
    if (store_erase(at) || store_prog(at + STORE_HDR, store_buf + STORE_HDR, len) || store_prog(at, store_buf, STORE_HDR))
        return 0;
    for (i = 0; i < STORE_HDR + len; i += sizeof chk) {   /* read back */
        uint32_t k = STORE_HDR + len - i < sizeof chk ? STORE_HDR + len - i : sizeof chk, j;
        if (store_fread(at + i, chk, k))
            return 0;
        for (j = 0; j < k; j++)
            if (chk[j] != store_buf[i + j])
                return 0;
    }
    store_at = at;
    store_seq++;
    return 1;
}
