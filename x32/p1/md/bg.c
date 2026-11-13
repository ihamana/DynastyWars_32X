/* Dynasty Wars 32xM - Mega Drive side: stage backgrounds (scroll2 -> plane A, scroll3 -> plane B).
 * Ported from DW_RE (bg.c / cache.c): each plane shows a 41x23 cell window of a prebuilt stage map
 * (reduced colours); cells entering the window are written into the 64x32 name table and their tiles
 * pulled into a reference-counted VRAM cache; cells leaving release their tiles.
 * The camera comes from the 32X (COMM2..), sent when the 32X requests a frame-buffer flip, so the
 * scroll registers are written at the vblank where the 32X picture changes.
 * Tiles are written by the CPU: with RV=0 the VDP cannot DMA from the cartridge. */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned long u32;
typedef signed short s16; typedef signed long s32;
#define VDP_DATA  (*(volatile u16 *)0xC00000)
#define VDP_DATAL (*(volatile u32 *)0xC00000)
#define VDP_CTRL  (*(volatile u16 *)0xC00004)
#define VDP_CTRLL (*(volatile u32 *)0xC00004)
#define COMM(n)   (*(volatile u16 *)(0xA15120 + (n)))
#define VR_PLANE_A 0xC000
#define VR_PLANE_B 0xE000
#define VR_SAT     0xF000
#define VR_HSCROLL 0xF400
#define TILE_BG0   16
#define NBG        1500
#define MAXID      8191
#define WIN_W 41
#define WIN_H 23              /* 176 playfield lines + fine scroll; the HUD (32X) covers the rest */

/* data in the cartridge, seen through the 1MB bank window (0xA15104 = 3: cart 0x300000 at 0x900000) */
#define BANKWIN(ofs) ((const void *)(0x900000 + ((ofs) - 0x300000)))
/* stage background: Word RAM (Mega-CD, loaded from the CD by the sub CPU) */
static const u8 *bgbase;              /* Word RAM 0x600000: 'BGST', u16 stage, u16 0, u32 offsets (tools/mksnd.py) */
#define WRHDR(i) (*(const u32 *)(bgbase + 8 + (i) * 4))
#define WRAMW(i) ((const void *)(bgbase + WRHDR(i)))
static const u32 *bg_tiles; static const u8 *bg_tline;   /* set when a stage is loaded */
#define BG_TILES bg_tiles
#define BG_MAPA  ((const u16 *)WRAMW(1))
#define BG_MAPB  ((const u16 *)WRAMW(2))
#define BG_PAL   ((const u16 *)WRAMW(3))      /* 4 lines */
#define BG_TLINE bg_tline                     /* palette line of each tile (id 1 first) */
#define BG_ANIM  ((const u16 *)BANKWIN(0x3DF100))     /* n, {code, attr, e[4]} */

volatile u16 vdp_busy;                 /* main loop is in a VDP address/data sequence */
volatile u16 vbl_count;
volatile u16 sc_pending, sc_hsA, sc_hsB, sc_vsA, sc_vsB;   /* applied by the vblank handler */

static inline void vdp_waddr(u16 a) { VDP_CTRLL = 0x40000000UL | ((u32)(a & 0x3FFF) << 16) | (a >> 14); }
static inline void vdp_reg(u16 r, u16 v) { VDP_CTRL = 0x8000 | (r << 8) | v; }

/* ---- tile cache ---- */
static u16 id2slot[MAXID + 1], slot2id[NBG], ref[NBG], fq[2048];
static u16 fq_head, fq_tail, never_used;
u16 cache_full, cache_loads;
static void cache_reset(void)
{
    u16 i;
    for (i = 0; i <= MAXID; i++) id2slot[i] = 0;
    for (i = 0; i < NBG; i++) { slot2id[i] = 0; ref[i] = 0; }
    fq_head = fq_tail = never_used = 0;
}
static u16 alloc_slot(void)
{
    if (never_used < NBG) return never_used++;
    while (fq_head != fq_tail) {
        u16 s = fq[fq_head]; fq_head = (fq_head + 1) & 2047;
        ref[s] &= 0x7FFF;
        if (ref[s] == 0) {
            u16 old = slot2id[s];
            if (old && id2slot[old] == s + 1) id2slot[old] = 0;
            return s;
        }
    }
    cache_full++;
    return 0xFFFF;
}
static void wa_put(u16 vtile, u16 k, u16 ph);
static u16 wa_find(u16 id);
static u16 wa_ph;
static void upload_tile(u16 vtile, u16 id)
{
    if (wa_ph) { u16 k = wa_find(id); if (k != 0xFFFF) { wa_put(vtile, k, wa_ph); return; } }
    const u32 *src = BG_TILES + ((u32)(id - 1) << 3);
    vdp_busy = 1;
    vdp_waddr(vtile << 5);
    VDP_DATAL = src[0]; VDP_DATAL = src[1]; VDP_DATAL = src[2]; VDP_DATAL = src[3];
    VDP_DATAL = src[4]; VDP_DATAL = src[5]; VDP_DATAL = src[6]; VDP_DATAL = src[7];
    vdp_busy = 0;
}
static u16 cache_acquire(u16 id)
{
    if (!id || id > MAXID) return 0;
    u16 s1 = id2slot[id], s;
    if (s1) s = s1 - 1;
    else {
        s = alloc_slot();
        if (s == 0xFFFF) return 0;
        id2slot[id] = s + 1; slot2id[s] = id;
        upload_tile(TILE_BG0 + s, id);
        cache_loads++;
    }
    ref[s]++;
    return TILE_BG0 + s;
}
static void cache_release(u16 id)
{
    if (!id || id > MAXID) return;
    u16 s1 = id2slot[id];
    if (!s1) return;
    u16 s = s1 - 1, r = ref[s] & 0x7FFF;
    if (!r) return;
    r--;
    if (r == 0 && !(ref[s] & 0x8000)) { fq[fq_tail] = s; fq_tail = (fq_tail + 1) & 2047; ref[s] = 0x8000; }
    else ref[s] = (ref[s] & 0x8000) | r;
}

/* ---- planes ---- */
typedef struct { const u16 *map, *bmap, *blk; u16 w, h, y0, bs, bm, nby, vram; s16 cx0, cy0; u16 valid; } BgPlane;
/* map: rows y0..y0+h-1; bs 0: flat [w][h], else blocks of 2^bs x 2^bs cells: bmap[w >> bs][h >> bs] -> blk[id][row][col] */
/* V1.10: inline, and 16x16 -> 32 multiplies (mulu.w; the 32-bit multiply was a __mulsi3 call): the harbour
   messages (4 map lookups per tile) were ~900 cycles a lookup, the MD fell behind the docking ships */
static inline __attribute__((always_inline)) u32 mulu16(u16 a, u16 b) { u32 r = a; __asm__("mulu.w %1,%0" : "+d"(r) : "dm"(b)); return r; }
static inline __attribute__((always_inline)) u16 mcell(const BgPlane *p, u16 x, u16 yy)      /* yy = y - y0 (in range) */
{
    if (!p->bs) return p->map[mulu16(x, p->h) + yy];
    u16 b = p->bmap[mulu16(x >> p->bs, p->nby) + (yy >> p->bs)];
    return p->blk[((u32)b << (p->bs << 1)) + ((yy & p->bm) << p->bs) + (x & p->bm)];
}
static BgPlane planeA, planeB;
static inline __attribute__((always_inline)) u16 acquire(u16 e)
{
    u16 id = (e & 0xFFF) | ((e >> 2) & 0x1000);         /* id bit 12 in bit 14 */
    if (!id) return 0;
    u16 t = cache_acquire(id);
    if (!t) return 0;
    /* map entry: pri 15, vflip 13, hflip 12, tile id; the palette line belongs to the tile */
    return (e & 0x8000) | ((u16)BG_TLINE[id - 1] << 13) | ((e >> 1) & 0x1800) | t;
}
static inline void release(u16 e) { cache_release((e & 0xFFF) | ((e >> 2) & 0x1000)); }

/* ---- stage 1 army dust: animated scroll2 cells (tilemap rows 52-55 = plane A rows 40-47) ----
 * The 32X compares those arcade cells and sends changes (COMM8/COMM10); each arcade 16x16 cell is
 * 2x2 map cells, looked up in the anim table (DW_RE st1bga_anim). */
#define AN_Y0 40
static u16 ovr[64][8][2];             /* MD map entry, 0xFFFF = static map */
static s16 ovr_col[64];
static u8 oidx[64][4];                /* anim entry of each arcade cell, 0xFF = static map */
#define BG_ANEXT ((const u8 *)BANKWIN(0x3DF040))
static void anim_reset(void) { u16 i; for (i = 0; i < 64; i++) ovr_col[i] = -1; }
static inline u16 ovr_get(s16 x, s16 y)
{
    u16 c = (u16)x >> 1, slot = c & 63;
    if (ovr_col[slot] != (s16)c) return 0xFFFF;
    return ovr[slot][y - AN_Y0][x & 1];
}
/* stage 2 harbour: scroll3 cells the arcade rewrote (32X render.c dyn3_scan): per arcade cell (32x32 = 4x4 map cells)
 * the map cell (column << 5 | row) of the extra cells to show instead (tools/bg4/convbg4.py EXTRA3), 0xFFFF static */
static s16 o3col[16]; static u16 o3[16][24], o3ph;
static void o3_reset(void) { u16 i, j; for (i = 0; i < 16; i++) { o3col[i] = -1; for (j = 0; j < 24; j++) o3[i][j] = 0xFFFF; } }
/* stage 2 harbour: scroll2 cells the arcade rewrote (the ships; 32X render.c dyn2_scan): per arcade cell (16x16 = 2x2
 * map cells) the map cell (column << 6 | row) to show instead, 0xFFFE nothing, 0xFFFF the static map */
static s16 o2col[32]; static u16 o2[32][48];
static void o2_reset(void) { u16 i, j; for (i = 0; i < 32; i++) { o2col[i] = -1; for (j = 0; j < 48; j++) o2[i][j] = 0xFFFF; } }
static inline __attribute__((always_inline)) u16 cellv(const BgPlane *p, s16 x, s16 y, u16 m)
{
    if (p == &planeA) {
        u16 ac = (u16)x >> 1, ay = (u16)y >> 1, sl = ac & 31;
        if (o2col[sl] == (s16)ac && ay < 48) {
            u16 o = o2[sl][ay];
            if (o != 0xFFFF) {
                if (o == 0xFFFE) return 0;
                return mcell(p, ((o >> 6) << 1) | (x & 1), (((o & 63) << 1) | (y & 1)) - p->y0);
            }
        }
        if ((u16)(y - AN_Y0) < 8) { u16 o = ovr_get(x, y); if (o != 0xFFFF) return o; }
    }
    if (p == &planeB) {
        u16 ac = (u16)x >> 2, ay = (u16)y >> 2, sl = ac & 15;
        if (o3col[sl] == (s16)ac && ay < 24) {
            u16 o = o3[sl][ay];
            if (o != 0xFFFF) {
                u16 r = o & 31;
                return mcell(p, (((o & 0x7FFF) >> 5) << 2) | (x & 3), ((r << 2) | (y & 3)) - p->y0);
            }
        }
    }
    return m;
}
static void write_run(u16 addr, const u16 *buf, u16 n, u16 inc)
{
    vdp_busy = 1;
    vdp_reg(15, inc);
    vdp_waddr(addr);
    while (n--) VDP_DATA = *buf++;
    vdp_reg(15, 2);
    vdp_busy = 0;
}
/* V1.10: the map value (before the cache) of each shown cell, as the VRAM name table (64 x 32): a cell leaving
   the window or rewritten is released from it instead of looking the map up again */
static u16 shad[2][32][64];
#define SHD(p, x, y) shad[(p) == &planeB][(y) & 31][(x) & 63]
static void put_column(BgPlane *p, s16 x, s16 y0, u16 n, u16 *buf)
{
    u16 i;
    if (x < 0 || x >= (s16)p->w) { for (i = 0; i < n; i++) buf[i] = 0; }
    else {
        s16 y = y0; u16 h = p->h, b = p->y0;
        for (i = 0; i < n; i++, y++) { u16 e = ((u16)(y - b) < h) ? cellv(p, x, y, mcell(p, x, y - b)) : 0; SHD(p, x, y) = e; buf[i] = acquire(e); }
    }
    u16 px = x & 63, py = y0 & 31;
    u16 first = 32 - py; if (first > n) first = n;
    write_run(p->vram + (py << 7) + (px << 1), buf, first, 128);
    if (first < n) write_run(p->vram + (px << 1), buf + first, n - first, 128);
}
static void drop_column(BgPlane *p, s16 x, s16 y0, u16 n)
{
    if (x < 0 || x >= (s16)p->w) return;
    s16 y = y0; u16 h = p->h, b = p->y0, i;
    for (i = 0; i < n; i++, y++) if ((u16)(y - b) < h) { release(SHD(p, x, y)); SHD(p, x, y) = 0; }
}
static void put_row(BgPlane *p, s16 y, s16 x0, u16 n, u16 *buf)
{
    u16 i;
    if ((u16)(y - p->y0) >= p->h) { for (i = 0; i < n; i++) buf[i] = 0; }
    else {
        u16 h = p->h, w = p->w, yy = y - p->y0;
        s16 x = x0;
        for (i = 0; i < n; i++, x++) { u16 e = ((u16)x < w) ? cellv(p, x, y, mcell(p, x, yy)) : 0; SHD(p, x, y) = e; buf[i] = acquire(e); }
    }
    u16 px = x0 & 63, py = y & 31;
    u16 first = 64 - px; if (first > n) first = n;
    write_run(p->vram + (py << 7) + (px << 1), buf, first, 2);
    if (first < n) write_run(p->vram + (py << 7), buf + first, n - first, 2);
}
static void drop_row(BgPlane *p, s16 y, s16 x0, u16 n)
{
    if ((u16)(y - p->y0) >= p->h) return;
    u16 h = p->h, w = p->w, yy = y - p->y0, i;
    s16 x = x0;
    for (i = 0; i < n; i++, x++) if ((u16)x < w) { release(SHD(p, x, y)); SHD(p, x, y) = 0; }
}
static void plane_init(BgPlane *p, const u16 *md, u16 vram)
{
    p->w = md[0]; p->h = md[1]; p->y0 = md[2]; p->bs = md[3]; p->bm = (1 << p->bs) - 1; p->nby = md[5];
    p->map = md + 6; p->bmap = md + 6; p->blk = md + 6 + (u32)md[4] * md[5];
    p->vram = vram; p->valid = 0;
}
static void plane_drop(BgPlane *p)
{
    s16 i;
    if (!p->valid) return;
    for (i = 0; i < WIN_W; i++) drop_column(p, p->cx0 + i, p->cy0, WIN_H);
    p->valid = 0;
}
static void set_camera(BgPlane *p, s16 x, s16 y)
{
    s16 cx = x >> 3, cy = y >> 3;
    u16 tmp[64];
    if (!p->valid || cx - p->cx0 > WIN_W || p->cx0 - cx > WIN_W || cy - p->cy0 > WIN_H || p->cy0 - cy > WIN_H) {
        s16 i;
        plane_drop(p);
        p->cx0 = cx; p->cy0 = cy;
        for (i = 0; i < WIN_W; i++) put_column(p, cx + i, cy, WIN_H, tmp);
        p->valid = 1;
        return;
    }
    while (p->cx0 < cx) { drop_column(p, p->cx0, p->cy0, WIN_H); p->cx0++; put_column(p, p->cx0 + WIN_W - 1, p->cy0, WIN_H, tmp); }
    while (p->cx0 > cx) { drop_column(p, p->cx0 + WIN_W - 1, p->cy0, WIN_H); p->cx0--; put_column(p, p->cx0, p->cy0, WIN_H, tmp); }
    while (p->cy0 < cy) { drop_row(p, p->cy0, p->cx0, WIN_W); p->cy0++; put_row(p, p->cy0 + WIN_H - 1, p->cx0, WIN_W, tmp); }
    while (p->cy0 > cy) { drop_row(p, p->cy0 + WIN_H - 1, p->cx0, WIN_W); p->cy0--; put_row(p, p->cy0, p->cx0, WIN_W, tmp); }
}
static void clear_planes(void)
{
    u16 i;
    vdp_busy = 1;
    vdp_waddr(VR_PLANE_A);
    for (i = 0; i < 0x2000 / 4; i++) VDP_DATAL = 0;     /* plane A + plane B (0xC000-0xFFFF minus SAT/hscroll follow) */
    vdp_waddr(VR_PLANE_B);
    for (i = 0; i < 0x1000 / 4; i++) VDP_DATAL = 0;
    vdp_busy = 0;
}

static void anim_apply(u16 c, u16 r, u16 idx)
{
    BgPlane *p = &planeA;
    u16 slot = c & 63, j, i;
    if (ovr_col[slot] != (s16)c) {
        /* reusing the slot for another column: put the old column's cells back to the static map */
        s16 oc = ovr_col[slot];
        for (j = 0; j < 8; j++) for (i = 0; i < 2; i++) {
            u16 o = ovr[slot][j][i];
            if (oc >= 0 && o != 0xFFFF && p->valid) {
                s16 x = (oc << 1) + i, y = AN_Y0 + j;
                ovr_col[slot] = oc;
                if (x >= p->cx0 && x < p->cx0 + WIN_W && y >= p->cy0 && y < p->cy0 + WIN_H) {
                    u16 m = mcell(p, x, y - p->y0);
                    release(o); SHD(p, x, y) = m;
                    u16 v = acquire(m);
                    vdp_busy = 1; vdp_waddr(p->vram + ((y & 31) << 7) + ((x & 63) << 1)); VDP_DATA = v; vdp_busy = 0;
                }
            }
            ovr[slot][j][i] = 0xFFFF;
        }
        ovr_col[slot] = c;
        for (j = 0; j < 4; j++) oidx[slot][j] = 0xFF;
    }
    oidx[slot][r] = idx;
    const u16 *e = 0;
    if (idx != 0xFF) e = BG_ANIM + 1 + idx * 6 + 2;
    for (j = 0; j < 2; j++) {
        s16 y = AN_Y0 + (r << 1) + j;
        for (i = 0; i < 2; i++) {
            s16 x = (c << 1) + i;
            u16 m = ((u16)x < p->w) ? mcell(p, x, y - p->y0) : 0;
            u16 old = ovr[slot][(r << 1) + j][i] != 0xFFFF ? ovr[slot][(r << 1) + j][i] : m;
            u16 nv = e ? e[(j << 1) + i] : 0xFFFF;
            ovr[slot][(r << 1) + j][i] = nv;
            u16 neu = nv != 0xFFFF ? nv : m;
            if (neu == old || !p->valid) continue;
            if (y < p->cy0 || y >= p->cy0 + WIN_H || x < p->cx0 || x >= p->cx0 + WIN_W) continue;
            release(old); SHD(p, x, y) = neu;
            u16 v = acquire(neu);
            vdp_busy = 1; vdp_waddr(p->vram + ((y & 31) << 7) + ((x & 63) << 1)); VDP_DATA = v; vdp_busy = 0;
        }
    }
}

/* one harbour cell: show source o (0xFFFF static); visible map cells are rewritten */
static void o3_set(u16 c, u16 r, u16 o)
{
    BgPlane *p = &planeB;
    u16 sl = c & 15, i, j;
    s16 x0 = c << 2, y0 = r << 2;
    if (o3[sl][r] == o) return;
    if (!p->valid) { o3[sl][r] = o; return; }         /* not shown (or Word RAM away): only remembered */
    o3[sl][r] = o;
    for (j = 0; j < 4; j++) for (i = 0; i < 4; i++) {
        s16 x = x0 + i, y = y0 + j;
        if (x < p->cx0 || x >= p->cx0 + WIN_W || y < p->cy0 || y >= p->cy0 + WIN_H) continue;
        if ((u16)x >= p->w || (u16)(y - p->y0) >= p->h) continue;
        u16 neu = cellv(p, x, y, mcell(p, x, y - p->y0)), ov = SHD(p, x, y);
        if (neu == ov) continue;
        release(ov); SHD(p, x, y) = neu;
        u16 v = acquire(neu);
        vdp_busy = 1; vdp_waddr(p->vram + ((y & 31) << 7) + ((x & 63) << 1)); VDP_DATA = v; vdp_busy = 0;
    }
}
/* one harbour scroll2 cell: show source o; visible map cells are rewritten */
static void o2_set(u16 sl, u16 r, u16 o)
{
    BgPlane *p = &planeA;
    s16 c = o2col[sl];
    u16 i, j;
    s16 x0 = c << 1, y0 = r << 1;
    if (c < 0 || o2[sl][r] == o) return;
    if (!p->valid) { o2[sl][r] = o; return; }
    o2[sl][r] = o;
    for (j = 0; j < 2; j++) for (i = 0; i < 2; i++) {
        s16 x = x0 + i, y = y0 + j;
        if (x < p->cx0 || x >= p->cx0 + WIN_W || y < p->cy0 || y >= p->cy0 + WIN_H) continue;
        if ((u16)x >= p->w || (u16)(y - p->y0) >= p->h) continue;
        u16 neu = cellv(p, x, y, mcell(p, x, y - p->y0)), ov = SHD(p, x, y);
        if (neu == ov) continue;
        release(ov); SHD(p, x, y) = neu;
        u16 v = acquire(neu);
        vdp_busy = 1; vdp_waddr(p->vram + ((y & 31) << 7) + ((x & 63) << 1)); VDP_DATA = v; vdp_busy = 0;
    }
}
static void o2_msg(u16 am, u16 v)
{
    u16 k, sl;
    (*(volatile u16 *)(0xFF0F30 + ((am >> 9) & 6)))++; *(volatile u16 *)0xFF0F38 = am;
    if ((am & 0x0C00) == 0x0C00) {                 /* slot am & 31 now holds column v */
        sl = am & 31;
        for (k = 0; k < 48; k++) if (o2[sl][k] != 0xFFFF) o2_set(sl, k, 0xFFFF);
        o2col[sl] = v;
    } else if (am & 0x0800) {                      /* row back to the static map */
        k = am & 63;
        if (k >= 48) { *(volatile u16 *)(0xFF0FA0 + ((k - 48) << 1)) = v; return; }   /* debug */
        if (k < 48) for (sl = 0; sl < 32; sl++) if (o2[sl][k] != 0xFFFF) o2_set(sl, k, 0xFFFF);
    } else {
        k = am & 63;
        if (k < 48) o2_set((am >> 6) & 31, k, v);
    }
}
/* harbour water: the arcade steps code bits 0-1 of all its water cells together (4 phases).  The MD map always
 * shows phase 0; a phase change rewrites the patterns of the phase-0 tiles that are in VRAM, from a cart table
 * (tools/bg4/wanim.py: u16 n, u16 id[n] sorted, then n x 4 phases x 32 bytes, pre-flipped). */
#define WA_ROM 0x330800
static u16 wa_n;
static const u16 *wa_id; static const u32 *wa_pat;
static u8 wa_bit[(MAXID + 8) >> 3];
static u16 wa_find(u16 id)
{
    if (!wa_n || !(wa_bit[id >> 3] & (1 << (id & 7)))) return 0xFFFF;
    u16 lo = 0, hi = wa_n;
    while (lo < hi) { u16 m = (lo + hi) >> 1; if (wa_id[m] < id) lo = m + 1; else hi = m; }
    return lo < wa_n && wa_id[lo] == id ? lo : 0xFFFF;
}
static void wa_put(u16 vtile, u16 k, u16 ph)
{
    const u32 *src = wa_pat + ((u32)k << 5) + (ph << 3);
    vdp_busy = 1;
    vdp_waddr(vtile << 5);
    VDP_DATAL = src[0]; VDP_DATAL = src[1]; VDP_DATAL = src[2]; VDP_DATAL = src[3];
    VDP_DATAL = src[4]; VDP_DATAL = src[5]; VDP_DATAL = src[6]; VDP_DATAL = src[7];
    vdp_busy = 0;
}
static void wa_setup(void)
{
    const u16 *t = (const u16 *)BANKWIN(WA_ROM);
    u16 i, n = t[0];
    for (i = 0; i < sizeof wa_bit; i++) wa_bit[i] = 0;
    wa_id = t + 1; wa_pat = (const u32 *)((u32)(t + 1 + n + 1) & ~3UL);
    for (i = 0; i < n; i++) { u16 id = wa_id[i]; if (id <= MAXID) wa_bit[id >> 3] |= 1 << (id & 7); }
    wa_ph = 0; wa_n = n;
}
static u16 wa_last;
static void o3_phase(u16 np)
{
    u16 k;
    np &= 3;
    o3ph = np;
    if (np == wa_ph) return;
    if ((u16)(vbl_count - wa_last) < 6) return;          /* at most every 6 frames (the next message brings it up to date) */
    wa_last = vbl_count;
    wa_ph = np;
    for (k = 0; k < wa_n; k++) {
        u16 id = wa_id[k], s1 = id2slot[id];
        if (!s1) continue;
        if (ref[s1 - 1] & 0x7FFF) wa_put(TILE_BG0 + s1 - 1, k, np);
        else id2slot[id] = 0;                   /* not shown: loaded again (in the phase of the time) when needed */
    }
}
/* message from the 32X: row 31 = the column (re)enters its window: back to the static map first; row 30 of
   column 127: animation phase */
static void o3_msg(u16 c, u16 r, u16 o)
{
    u16 sl = c & 15, k;
    if (c == 127 && r == 30) { (*(volatile u16 *)0xFF0F3E)++; o3_phase(o & 3); return; }
    (*(volatile u16 *)0xFF0F3A)++; if (o != 0xFFFF) (*(volatile u16 *)0xFF0F3C)++;
    if (r == 31 || o3col[sl] != (s16)c) {
        if (o3col[sl] >= 0) { u16 oc = o3col[sl]; for (k = 0; k < 24; k++) if (o3[sl][k] != 0xFFFF) o3_set(oc, k, 0xFFFF); }
        o3col[sl] = c;
        for (k = 0; k < 24; k++) o3[sl][k] = 0xFFFF;
        if (r == 31) return;
    }
    if (r < 24) o3_set(c, r, o);
}
/* the arcade stepped all its dust cells: same here with the next-step table */
static void anim_tick(void)
{
    u16 s, r;
    for (s = 0; s < 64; s++) {
        if (ovr_col[s] < 0) continue;
        for (r = 0; r < 4; r++) {
            u16 m = oidx[s][r];
            if (m == 0xFF) continue;
            u16 n = BG_ANEXT[m];
            if (n != m) anim_apply(ovr_col[s], r, n);
        }
    }
}

/* ---- main loop ---- */
static inline void msg_ack(void) { COMM(8) = 0; *(volatile u8 *)0xA15103 = 1; }   /* taken: CMD interrupt to the master SH-2 (sends the next) */
extern void snd_start(void), snd_poll(void), snd_cmd(unsigned char c), snd_vol(u16 k, u16 v);
extern int snd_bg_ready(void);
static u16 cur;                       /* stage whose background is in Word RAM (0xFF none) */
/* ---- V2.1: pictures of the scaled scenes shown on plane A (tools/mdpic.py, after the stage background in Word RAM,
 * header +28).  The 32X sets COMM2 bit 15 with COMM12 = picture id, COMM14 = brightness 0-16 (its reference colour
 * against the live arcade palette: the fades).  Tiles and map go to VRAM, so the picture stays while Word RAM is away. */
static s16 pic_shown; static u16 pic_lvl, pic_pal[64], pic_bgpal;   /* (no initialised data on the MD side: md_main sets them) */
static void pic_level(u16 lv)
{
    u16 i;
    if (lv > 16) lv = 16;
    vdp_busy = 1;
    VDP_CTRLL = 0xC0000000UL;
    for (i = 0; i < 64; i++) {
        u16 c = pic_pal[i], r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7;
        r = (r * lv + 8) >> 4; g = (g * lv + 8) >> 4; b = (b * lv + 8) >> 4;
        VDP_DATA = (b << 9) | (g << 5) | (r << 1);
    }
    vdp_busy = 0;
    pic_lvl = lv; pic_bgpal = 1;
}
static void clear_planes(void);
static int pic_show(u16 id, u16 lv)
{
    u32 po = WRHDR(5);
    if (!po) return 0;
    const u8 *t = bgbase + po;
    u16 n = *(const u16 *)t, i, k, y;
    for (i = 0; i < n; i++) {
        const u8 *e = t + 4 + i * 16;
        if (*(const u16 *)e != id) continue;
        u16 nt = *(const u16 *)(e + 2);
        const u32 *tl = (const u32 *)(t + *(const u32 *)(e + 4));
        const u16 *map = (const u16 *)(t + *(const u32 *)(e + 8)), *pal = (const u16 *)(t + *(const u32 *)(e + 12));
        clear_planes(); cache_reset();
        for (k = 0; k < 64; k++) pic_pal[k] = pal[k];
        pic_level(0);                                   /* dark while the tiles go in */
        vdp_busy = 1;
        vdp_waddr(TILE_BG0 << 5);
        for (k = 0; k < nt * 8; k++) VDP_DATAL = tl[k];
        for (y = 0; y < 28; y++) { vdp_waddr(VR_PLANE_A + (y << 7)); for (k = 0; k < 40; k++) VDP_DATA = map[y * 40 + k]; }
        vdp_busy = 0;
        sc_hsA = sc_hsB = sc_vsA = sc_vsB = 0; sc_pending = 1;
        pic_level(lv);
        return 1;
    }
    return 0;
}
static void bg_setup(u16 st)
{
    u16 i;
    bgbase = (const u8 *)0x600000;
    bg_tiles = (const u32 *)WRAMW(0); bg_tline = (const u8 *)WRAMW(4);
    vdp_busy = 1;
    VDP_CTRLL = 0xC0000000UL;                    /* CRAM 0..63 = stage palettes (4 lines) */
    for (i = 0; i < 64; i++) VDP_DATA = BG_PAL[i];
    vdp_busy = 0;
    plane_init(&planeA, BG_MAPA, VR_PLANE_A);
    plane_init(&planeB, BG_MAPB, VR_PLANE_B);
    if (st == 1) wa_setup();
    cache_reset();
    if (st != 1) { o2_reset(); o3_reset(); wa_n = 0; wa_ph = 0; }   /* harbour overrides only in the harbour (the 32X resets its own columns) */
    cur = st;
    if (pic_shown >= 0) pic_level(pic_lvl); else pic_bgpal = 0;      /* a picture shown from VRAM keeps its colours */
}
/* V3.2: background palette cycles (tools/bg4/palanim.py, after the 64 colours: 'PV' 0xA55A, u8 ngroups, 0, per group
   u8 nslot, u8 nver, u8 CRAM index[nslot] (even), u16 colour[nver][nslot]); the 32X sends group << 4 | version */
static void pv_apply(u16 g, u16 v)
{
    const u16 *w = BG_PAL + 64;
    if (w[0] != 0x5056 || w[1] != 0xA55A) return;
    const u8 *b = (const u8 *)(w + 2); u16 ng = b[0], i; b += 2;
    if (g >= ng) return;
    for (;;) {
        u16 ns = b[0], nv = b[1];
        const u8 *sl = b + 2; const u8 *cv = sl + ((ns + 1) & ~1);
        if (!g) {
            if (v >= nv) return;
            const u16 *c = (const u16 *)cv + v * ns;
            vdp_busy = 1;
            for (i = 0; i < ns; i++) { VDP_CTRLL = 0xC0000000UL | ((u32)(sl[i] << 1) << 16); VDP_DATA = c[i]; }
            vdp_busy = 0;
            return;
        }
        b = cv + nv * ns * 2; g--;
    }
}
extern int snd_bg_load(u8 st), snd_bg_done(u8 st);
u16 dbg_bgframes, dbg_anmsg, dbg_loops, dbg_idle, dbg_ticks, dbg_bad[2];
void md_main(void)
{
    u16 last = 0xFFFF, on = 0;
    u16 loading = 0, failed = 0xFF; int k;
    *(volatile u8 *)0xA15105 = 3;                        /* bank window: cart 0x300000-0x3FFFFF */
    cur = 0xFF; pic_shown = -1; pic_lvl = 0xFFFF; pic_bgpal = 0;
    anim_reset(); o3_reset(); o2_reset();
    snd_start();
    for (;;) {
        u16 am = COMM(8);
        dbg_loops++; if (!am) dbg_idle++;
        if ((am & 0xA000) == 0xA000) {                   /* dust: the arcade stepped (n times) */
            u16 k = am & 0xFF;
            dbg_ticks += k;
            if (on && cur == 0) while (k--) anim_tick();
            msg_ack();
        } else if (am & 0x8000) {                        /* dust cell change from the 32X */
            u16 idx = COMM(10), pr = (am >> 7) & 2;     /* row pair: 2 cells per message */
            dbg_anmsg++;
            if (on && cur == 0) { anim_apply(am & 0xFF, pr, idx >> 8); anim_apply(am & 0xFF, pr + 1, idx & 0xFF); }
            msg_ack();
        } else if (am & 0x4000) {                        /* arcade sound command */
            snd_cmd(am & 0xFF);
            msg_ack();
        } else if ((am & 0x3000) == 0x3000) {            /* harbour scroll2 cell / row / column (o2_msg) */
            o2_msg(am, COMM(10));
            msg_ack();
        } else if (am & 0x1000 && !(am & 0x2000)) {      /* harbour scroll3 cell: col << 5 | row, COMM10 source */
            u16 o = COMM(10);
            o3_msg((am >> 5) & 127, am & 31, o);
            msg_ack();
        } else if ((am & 0x3C00) == 0x2400) {            /* V3.2: background palette cycle: group << 4 | version */
            if (on && cur != 0xFF && pic_shown < 0 && !loading) pv_apply((am >> 4) & 15, am & 15);
            msg_ack();
        } else if (am & 0x2000) {                        /* volume (options): which << 8 | level 0-10 */
            snd_vol((am >> 8) & 1, am & 0xFF);
            msg_ack();
        }
        snd_poll();
        if (cur == 0xFF && (k = snd_bg_ready()) > 0) bg_setup(0);   /* stage 0 background read at start-up (no Mega-CD: none) */
        if (loading) {
            int r = snd_bg_done(loading - 1);
            if (r > 0) bg_setup(loading - 1);
            if (r) { if (r < 0) failed = loading - 1; loading = 0; }
        }
        if (COMM(6) == 0xBAD0) { dbg_bad[0] = 0xBAD0; dbg_bad[1] = COMM(4); }
        u16 c = COMM(2);
        if (!c || (c & 0xFF) == last) continue;
        s16 x2 = COMM(4), y2 = COMM(6), x3 = COMM(12), y3 = COMM(14);
        if (COMM(2) != c) continue;                     /* changed while reading */
        last = c & 0xFF;
        u16 want = (c >> 9) & 31;                        /* the game's stage (when bit 14) */
        if ((c & 0x4000) && want != cur && want != failed && !loading && cur != 0xFF && !(c & 0x100)) {
            if (on) {                                    /* planes off while Word RAM is away */
                plane_drop(&planeA); plane_drop(&planeB);
                clear_planes(); cache_reset(); anim_reset();
                sc_hsA = sc_hsB = sc_vsA = sc_vsB = 0; sc_pending = 1;
                on = 0;
            }
            if (snd_bg_load(want)) { loading = want + 1; cur = 0xFE; }
        }
        if (c & 0x8000) {                                /* V2.1: a picture (x3 = id, y3 = brightness) */
            if (on) {
                plane_drop(&planeA); plane_drop(&planeB);
                clear_planes(); cache_reset(); anim_reset();
                on = 0;
            }
            if (pic_shown != x3 && cur == want && !loading && pic_show(x3, y3)) pic_shown = x3;
            if (pic_shown == x3 && (u16)y3 != pic_lvl) pic_level(y3);
            continue;
        }
        if (pic_shown >= 0) { clear_planes(); cache_reset(); pic_shown = -1; planeA.valid = planeB.valid = 0; }
        if ((c & 0x100) && cur == want && !loading) {
            s16 ax = x2 + 96, ay = 272 - y2, bx = x3 + 96, by = 272 - y3;
            if (!on) {
                clear_planes(); cache_reset(); anim_reset(); planeA.valid = planeB.valid = 0; on = 1;
                if (pic_bgpal) { u16 i; vdp_busy = 1; VDP_CTRLL = 0xC0000000UL; for (i = 0; i < 64; i++) VDP_DATA = BG_PAL[i]; vdp_busy = 0; pic_bgpal = 0; }   /* the stage colours back */
            }
            set_camera(&planeA, ax, ay);
            set_camera(&planeB, bx, by);
            sc_hsA = -ax; sc_hsB = -bx; sc_vsA = ay; sc_vsB = by;
            sc_pending = 1;
            dbg_bgframes++;
        } else if (on) {
            plane_drop(&planeA); plane_drop(&planeB);
            clear_planes(); cache_reset(); anim_reset();
            sc_hsA = sc_hsB = sc_vsA = sc_vsB = 0; sc_pending = 1;
            on = 0;
        }
    }
}
/* called from the vblank handler (asm) when the main loop is not inside a VDP sequence */
/* vblank: count 32X frame flips (the camera sequence number changes once per flip) */
void md_vblank_count(void)
{
    static u16 lastc, cnt, vb;
    vbl_count++;
    u16 c = COMM(2) & 0xFF;
    if (c && c != lastc) { lastc = c; cnt++; }
    if (++vb >= 60) { *(volatile u16 *)0xFF0F20 = cnt; cnt = 0; vb = 0; }
    for (int i = 0; i < 3; i++) *(volatile u8 *)(0xFF0F24 + i) = *(volatile u8 *)(0xA12029 + i);
    *(volatile u8 *)0xFF0F27 = *(volatile u8 *)0xA12023;   /* sub sound stats (pcm.c) */
}
void md_vblank_scroll(void)
{
    if (!sc_pending) return;
    sc_pending = 0;
    VDP_CTRLL = 0x40000000UL | ((u32)(VR_HSCROLL & 0x3FFF) << 16) | (VR_HSCROLL >> 14);
    VDP_DATA = sc_hsA; VDP_DATA = sc_hsB;
    VDP_CTRLL = 0x40000010UL;                            /* VSRAM 0 */
    VDP_DATA = sc_vsA; VDP_DATA = sc_vsB;
}
