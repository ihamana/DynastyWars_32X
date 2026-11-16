/* Dynasty Wars 32xM - CPS1 sprites + scroll1 on the 32X (ported from the DW 32X renderer).
 * Reads the arcade's own video RAM (gfx RAM page 0x90 in SDRAM) and CPS-A/B registers.
 * Both CPUs draw part of the screen (master 0..split-1, slave split..223) into the back
 * frame buffer; colour 0 is "through" (the MD planes show there). */
#include <stdint.h>
#include "mars.h"
typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16; typedef uint32_t u32; typedef int16_t s16; typedef int32_t s32;
#include "render.h"

volatile u32 bench_acc, dbg_want;
static inline u32 wdt(void) { return *(volatile u8 *)0xFFFFFE81; }
#ifdef TRACE
extern volatile u32 vcount;
#define TRC(c) { volatile u32 *Q = (volatile u32 *)0x26007200; u32 i = Q[0]; Q[1 + (i & 127)] = ((u32)(c) << 28) | ((vcount & 0xFFFFF) << 8) | (*(volatile u8 *)0xFFFFFE81); Q[0] = i + 1; }
#else
#define TRC(c)
#endif
/* the SH-2 has no barrel shifter for variable counts (gcc calls a library routine): tables */
static const u8 bit8[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
static const u32 bit32[32] = { 1u<<0,1u<<1,1u<<2,1u<<3,1u<<4,1u<<5,1u<<6,1u<<7,1u<<8,1u<<9,1u<<10,1u<<11,1u<<12,1u<<13,1u<<14,1u<<15,
    1u<<16,1u<<17,1u<<18,1u<<19,1u<<20,1u<<21,1u<<22,1u<<23,1u<<24,1u<<25,1u<<26,1u<<27,1u<<28,1u<<29,1u<<30,1u<<31 };
static u8 bgpri_slot0 = 224;                  /* V3.18: 224 (2 lines, slots 224-254) or 208 (3 lines, 208-254) */
#define BGPRI_SLOT0 bgpri_slot0
static void scan23(u8 *used);
volatile u32 s23_writes, lut23ver;
static u16 bgpri_cram[47];
/* stage 1 army dust: changed scroll2 cells of tilemap rows 52-55 -> MD (COMM8/COMM10) */
#define BG_ANIM16 ((const u16 *)0x023DF100)
static u32 an_raw[64][4] __attribute__((section(".rd2"))); static volatile s16 an_col[64];
/* The arcade steps all its dust cells together every 4 frames; the MD does the same on a 'tick'
 * message with a next-step table, so only the other changes are sent.  an_idx = what the MD will
 * show once it has every message (ticks included); a version per row pair is bumped by the scan
 * (vblank interrupt) and compared by the sender (main loop): the newest state wins. */
#define BG_ANEXT ((const u8 *)0x023DF040)
static u8 an_act[64][4];
static volatile u8 an_idx[64][4], an_ver[64][2]; static u8 an_sent[64][2]; static u32 an_rr;
static volatile u8 an_dirty;                    /* an_ver changed since msg_send last found nothing to send */
static volatile u8 an_tick; static u8 an_tick_sent;
static u16 an_delay;
#define WRAM_PTR ((const u8 *)0x06000000)
#ifndef HLINE
#define HLINE 160
#endif
#define UI __attribute__((section(".romui"), noinline, noclone))   /* rarely run: cartridge, not SDRAM */
#ifndef NOPH
#define NOPH 0
#endif
#define UI2 __attribute__((section(".rom2ui"), noinline, noclone))  /* the same, in the second cart code area */
#define CT(p) ((volatile __typeof__(*(p)) *)((uintptr_t)(p) | 0x20000000))
#define SCR_W 320
#if defined(SHOWFPS) || defined(DBGLAY)
#define FPSON (fps_game >= 0)
#else
#define FPSON 0
#endif
#define SCR_H 224
#define FB_LINE(y) (0x200 + (y) * SCR_W)
#define VOFF 0                                 /* scaled scenes: 5/6 across only (keeps the shape) */
#define SCH 224
#define SPR_TAB ((const u32 *)0x02028000)     /* 12288 sprite tiles -> address (0 = blank) */
#define S1_TAB  ((const u32 *)0x02034000)     /* 8192 codes x 2 halves -> address (0 = blank) */
#include "../data/zh/zhcodes.h"
#include "palanim.h"                    /* V3.2: background palette cycles (tools/bg4/palanim.py) */
#define S1X_TAB ((const u32 *)0x02370000)     /* Chinese glyphs (tools/zh/mkzh.py): codes 0x8000-0x9FFF x 2 halves */
#define RD1 __attribute__((section(".rd1")))
#define RD2 __attribute__((section(".rd2")))
#define HB __attribute__((section(".hb")))
static u16 mxp[SCR_W] HB, mxa[SCR_W] HB, oinvx[385] HB;
static u8 myp[SCH] HB, mya[SCH] HB, oinvy[225] HB;
extern void cache_purge(void);
extern u32 cpu_id(void);

/* ---------------- frame state ----------------
 * The game logic runs in the vblank interrupt (render_logic_done() is called after each arcade
 * frame) and fills the packet that is not being drawn; the main loops of both CPUs draw the
 * newest packet.  Scroll1 is read live from the arcade's video RAM. */
typedef struct { u16 spr[1024]; u16 pal[2048]; u16 nspr, s1x, s1y, layerctl, flags, bgon; s16 cam[4]; u16 sc23[4], vc, wantv; u32 s1base, b2, b3, ep23, seq; u8 ui_mode, ui_sel, ui_opt[7], ui_six, layout, stage, pic, piclv; u16 ui_pad, ui_hold; u32 lid; u8 bgtail, ui_fsa; u16 flrow; u32 palser; } pk_t;
static pk_t pk0 RD1, pk1 HB;
static pk_t *const PK[2] = { &pk0, &pk1 };
static volatile int pk_r;                     /* packet being drawn */
static volatile u32 pk_seq;                   /* last packet written */
static pk_t *P;                               /* = PK[pk_r] while drawing */
static pk_t *Q;                               /* V3.1: the packet being filled (snapshot, master, vblank interrupt).  Was P
                                                 itself, switched for the time of the snapshot: the slave, reading P again
                                                 from memory (a cache miss) while drawing, drew from the half-filled packet
                                                 (sprite pieces left on the screen: the flames' "embers") */
#define CP P                                  /* the packet the macros below use (Q in the snapshot code) */
#define nspr (CP->nspr)
#define spr (CP->spr)
#define s1x (CP->s1x)
#define s1y (CP->s1y)
#define layerctl (CP->layerctl)
#define flags (CP->flags)
#define s1base (CP->s1base)
#define cpspal (CP->pal)
static u16 objlatch[1024] HB;
static u8 s1blank[2048] HB;
/* scroll1 palettes in use: pen-pair tables (byte of 2 pens -> 2 CRAM slots), built by the master */
static u16 s1pt[16][256] HB;
static s8 s1slot[32]; static u8 s1pt_pal[16]; static u16 s1pt_ver[16];
static u16 nlatch;
static u8 lut[128][16];                       /* 0-31 sprites, 32-63 scroll1, 64-95 scroll2, 96-127 scroll3 */
static u32 rowsig[29];                        /* per screen 8-line row: hash of its scroll1 cells */
static volatile u32 skipmask, hudsk;   /* V3.15 hudsk: HUD rows 22-27 drawn on one side only (bit r-22: 1P side kept; bit 8+r-22: 2P side kept) */                 /* rows not redrawn this frame (unchanged, no sprites) */
static u32 s1epoch;
static volatile u32 rowhas;                  /* scroll1 rows (from the top visible cell row) with something to draw */                           /* bumped when any scroll1 colour mapping changes */                        /* palette, pen -> CRAM slot */
static u16 pver[32];                          /* scroll1 palette versions (lut changes) */
static u16 sp2[32][256] HB;                  /* sprite palettes: pen pair byte -> 2 CRAM bytes */
static u8 spdirty[32];
static u16 cram_next[256], cram_build[256];
static u32 pal_seen = 0xFFFFFFFF;
static u8 pal_exact;                          /* V3.13: CRAM slots ran out: next build maps only the palettes in use */
static volatile int frame_go, slave_done, master_done, split_y, slave_wait;
/* V2.7: the bands of WSB lines are taken as the CPUs get free (the master from the top, the slave from the bottom):
   the game logic stops the master at any time, a split guessed from the last frame left one CPU waiting for the
   other (the slave 14-22% of its time).  Peterson's lock around taking one (both cache-through). */
#ifndef NOWS
#ifndef WSB
#define WSB 32                     /* (tried 16-56: 32 best) */
#endif
#ifndef NOWSROWS
#define WSROWS 1                   /* V3.16: the pieces get smaller near the end (ws_claim) */
#endif
#ifndef WSR_A
#define WSR_A 6                    /* (rows left) above: whole WSB bands; below: WSR_MIN rows (tried 4-10, min 1-3: 6 / 2 best) */
#endif
#ifndef WSR_MIN
#define WSR_MIN 2
#endif
static volatile int ws_flag[2], ws_turn, ws_top, ws_bot;
#endif
static volatile u32 boot_done;
static u16 flip_from;
static volatile int flip_pending;
volatile int fps_game = -1, fps_logic, logic_busy; static u32 tmx[4], tshow[4], tmn; static u32 ptm[8] RD1, ptshow[8] RD1, ptn; static u32 r0dbg, skacc; volatile u32 dbg_rowch, dbg_keych, dbg_sprrows; volatile u32 dbg_spm; volatile u32 logic_ticks; static volatile u32 ntiles; volatile u32 frames_drawn;
#ifdef PALDBG
volatile u32 dbg_palchg; volatile unsigned long long dbg_palwhich;
#endif
volatile u32 bench[4], dbg_wt; volatile u32 dbg_cells, dbg_palbuild, dbg_s1miss, dbg_colors, dbg_split, dbg_sprmiss;

static void pal_upload(void)
{
    volatile u16 *c = &MARS_CRAM; volatile u16 *src = CT(&cram_next[0]);
    for (int i = 0; i < 256; i++) c[i] = src[i];
    flip_pending = 0;
}
#ifdef TSTAT
UI2
#endif
void render_vint(void)
{
    if (flip_pending) {
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) == flip_from && (MARS_VDP_FBCTL & 0x8000)) ;
        if ((MARS_VDP_FBCTL & MARS_VDP_FS) != flip_from) { pal_upload(); TRC(7) }
    }
}

/* ---------------- state from the arcade RAM ---------------- */
static inline const u8 *gptr(u32 off) { off &= 0x3FFFF; return m68_page[0x90 + (off >> 16)] + (off & 0xFFFF); }
/* scroll1 map summary per map row (64) and 8-column group (8): palettes used (bit per s1
 * palette), hash of the cells, something drawable.  Rows are recomputed when the translated
 * program writes into them (s1_note, from the memory write helpers). */
u32 s1_lo = 0xFFFFFFFEu; volatile u32 dbg_basech, dbg_notes;
static u32 grp_used[64][8] HB, grp_sig[64][8] HB;
static u8 grp_has[64] HB;
static volatile u8 s1dirty[64];
static volatile u32 s1all = 1;
/* V2.5: the scroll2/3 cells written since dyn2_scan / dyn3_scan last looked: per tilemap column, a bit per row & 31
   (the scans skip the clean ones); and the palette (pal_latch skips an unchanged one) */
/* scroll2: per column slot (c & 31) a bit per row & 15 (dyn2_scan keeps a ring of 16 rows); scroll3: per column slot
   (c & 15) a bit per row & 31 */
#ifdef DCHK
extern u16 cpspal_live[2048]; extern u8 *m68_page[256];
UI2 void pal_chk(u32 b, u32 ctrl)
{
    const u16 *s = (const u16 *)(m68_page[0x90] + b); int src = 0; ((volatile u32 *)0x26007000)[3]++;
    for (int page = 0; page < 4; page++) { if (ctrl & (1 << page)) { for (int o = 0; o < 0x200; o++) if (cpspal_live[page * 0x200 + o] != s[src + o]) { ((volatile u32 *)0x26007000)[2]++; break; } src += 0x200; } else if (src) src += 0x200; }
}
#endif
u16 d2dm[32] RD1; u32 d3dm[16] RD1; volatile u8 pal_dirty = 1;
void s23_note(u32 a)
{
    u32 o = a & 0x3FFFF, k = o >> 14, d = (o >> 2) & 0xFFF;   /* the layer bases are 16 KB aligned */
    if (k == ((io_cpsa[2] >> 6) & 15)) { s23_writes++; d2dm[(d >> 4) & 31] |= bit32[d & 15]; }
    else if (k == ((io_cpsa[3] >> 6) & 15)) { s23_writes++; d3dm[(d >> 3) & 15] |= bit32[(d & 7) | ((d >> 6) & 0x18)]; }
    else pal_dirty = 1;                         /* (anything else in page 0x90: a palette write, or as good as one) */
}
void s1_note(u32 a)
{
    u32 idx = ((a & 0xFFFF) - s1_lo) >> 2;
    u32 row = (idx & 0x1f) | ((idx >> 6) & 0x20), col = (idx >> 5) & 63;
    s1dirty[row] |= bit8[col >> 3];
    dbg_notes++;
}
UI2 void s1_base_changed(void)
{
    u32 b = ((u32)io_cpsa[1] * 256) & 0x3c000;
    u32 lo = b < 0x10000 ? b : 0xFFFFFFFFu;
    if (lo != s1_lo) { s1_lo = lo; s1all = 1; dbg_basech++; }
}
#ifdef SKIPCHK
static u8 skchk[2];
#endif
static u8 s1chg[2][64] HB;                    /* V2.8 */
static u32 s1dm[22][2] HB, s1cm[22][2] HB, s1cmon; static u8 s1xg[2][22] HB;   /* V2.8: scroll1 cells drawn / cleared (cell mode) */
static void s1_refresh(void)
{
    const u8 *base = (const u8 *)gptr(((u32)io_cpsa[1] * 256) & 0x3c000);
    int all = s1all || s1_lo == 0xFFFFFFFFu;
    s1all = 0;
    for (u32 row = 0; row < 64; row++) {
        u32 m;
        __asm__ volatile("stc sr,r0; or #0xF0,r0; ldc r0,sr" ::: "r0");
        m = s1dirty[row]; s1dirty[row] = 0;
        __asm__ volatile("stc sr,r0; and #0x0F,r0; ldc r0,sr" ::: "r0");
        if (all) m = 0xFF;
        if (!m) continue;
        s1chg[0][row] |= m; s1chg[1][row] |= m;    /* V2.8: per buffer, the groups changed since it was drawn */
        const u32 *rp = (const u32 *)(base + (((row & 0x1f) + ((row & 0x20) << 6)) << 2));
        u32 has = grp_has[row];
        for (u32 g = 0; g < 8; g++) {
            if (!(m & bit8[g])) continue;
            u32 used = 0, sg = 0, hs = 0;
            for (u32 c = g * 8; c < g * 8 + 8; c++) {
                u32 v = rp[c << 5];
                sg = (sg ^ v) * 0x01000193u;
                u32 k = (v >> 16) - 0x6000;
                if (k < 0x2000) {
                    used |= bit32[v & 31];
                    k = (k << 1) | (c & 1);
                    if (s1blank[k >> 3] & bit8[k & 7]) hs = 1;
                } else if (k - 0x2000 < 0x2000) { used |= bit32[v & 31]; hs = 1; }   /* Chinese glyphs (0x8000-0x9FFF; V3.4: 0x9000- 24x24 blocks) */
            }
            grp_used[row][g] = used; grp_sig[row][g] = sg;
            if (hs) has |= bit8[g]; else has &= ~bit8[g];
        }
        grp_has[row] = has;
    }
}
static inline int spr_unused(const u16 *e) { u16 a = e[3]; return (a & 0xff00) == 0xff00 || (e[1] == 0x180 && !a && !e[2]); }
#undef CP
#define CP Q
static inline u16 s1cell(int sc, int sr)       /* code at screen cell (from the arcade's left edge) */
{
    u16 col = (((u16)(64 + s1x) >> 3) + sc) & 63, row = (((u16)(16 + s1y) >> 3) + sr) & 63;
    u32 idx = (row & 0x1f) + (col << 5) + ((row & 0x20) << 6);
    return *(const u16 *)(s1base + idx * 4);
}
UI2 static int hud_on(void) { return (s1cell(8, 26) == 0x61ff && s1cell(9, 26) == 0x61f7) || (s1cell(25, 26) == 0x61ff && s1cell(26, 26) == 0x61f7); }
#undef CP
#define CP P
static int anim_find(u32 code, u32 attr)
{
    const u16 *t = BG_ANIM16; int lo = 0, hi = t[0] - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1; const u16 *e = t + 1 + mid * 6;
        if (e[0] < code || (e[0] == code && e[1] < attr)) lo = mid + 1;
        else if (e[0] == code && e[1] == attr) return mid;
        else hi = mid - 1;
    }
    return 0xFF;
}
static void anim_scan(pk_t *q)
{
    if (!q->bgon || q->stage) { for (int i = 0; i < 64; i++) an_col[i] = -1; an_delay = 3; return; }   /* stage 1 only */
    if (an_delay) { an_delay--; return; }
    int ax = q->cam[0] + 96;
    if (ax > 1200 + 96) return;                     /* the dust only exists at the start */
    u32 base = ((u32)io_cpsa[2] * 256) & 0x3FFFF;
    const u32 *gb = (const u32 *)gptr(base);
    int cx0 = ax >> 3, c0 = cx0 >> 1, c1 = (cx0 + 40) >> 1, hits = 0;
    if (c0 < 0) c0 = 0;
    for (int c = c0; c <= c1; c++) {
        int slot = c & 63;
        if (an_col[slot] != c) {                    /* new column in this slot: the MD shows the static map */
            for (int r = 0; r < 4; r++) { an_raw[slot][r] = 0xFFFFFFFF; an_idx[slot][r] = 0xFF; }
            an_col[slot] = c; an_ver[slot][0]++; an_ver[slot][1]++; an_dirty = 1;
        }
        const u32 *src = gb + ((slot << 4) + 4 + 0xC00);
        for (int r = 0; r < 4; r++) {
            u32 raw = src[r];
            if (raw != an_raw[slot][r]) { an_raw[slot][r] = raw; an_act[slot][r] = anim_find(raw >> 16, raw & 0x7F); }
            u8 m = an_idx[slot][r], a = an_act[slot][r];
            if (a != m && m != 0xFF && BG_ANEXT[m] == a && a != m) hits++;
        }
    }
    if (hits >= 4) {                                /* the arcade stepped: so does the MD (all its cells) */
        for (int s = 0; s < 64; s++) if (an_col[s] >= 0)
            for (int r = 0; r < 4; r++) { u8 m = an_idx[s][r]; if (m != 0xFF) an_idx[s][r] = BG_ANEXT[m]; }
        an_tick++;
    }
    for (int c = c0; c <= c1; c++) {
        int slot = c & 63;
        for (int r = 0; r < 4; r++)
            if (an_act[slot][r] != an_idx[slot][r]) { an_idx[slot][r] = an_act[slot][r]; an_ver[slot][r >> 1]++; an_dirty = 1; }
    }
}
/* stage 2 harbour: the arcade rewrites scroll3 cells (ships moving in, animated water).  Visible cells
   are compared with what the MD shows; a changed cell is sent as the MD map cell to show instead
   (table from tools/bg4/dyn3.py at BGPRI_TAB[16 + stage]; not in the table: the static map) */
#define DY_TAB ((const u32 *)0x023C0000)
static u32 dy_val[16][24] RD1; static u16 dy_src[16][24] RD1; static s16 dy_tag[16] RD1;
static volatile u32 dy_q[128] RD1; static volatile u32 dy_w, dy_r; static u8 dy_on, dy_ph;   /* queue: COMM8 << 16 | COMM10 */
static inline void dy_push(u32 c8, u32 c10) { dy_q[dy_w & 127] = (c8 << 16) | c10; dy_w++; }
/* V2.1: scroll3 (dyn3) messages in their own queue, sent only when the scroll2 one (the ships) is empty: the MD takes
   ~3x longer for a 32x32 scroll3 cell than for a 16x16 scroll2 one, and the docking ships are what shows */
static volatile u32 dz_q[64] RD1; static volatile u32 dz_w, dz_r;
static inline void dz_push(u32 c8, u32 c10) { dz_q[dz_w & 63] = (c8 << 16) | c10; dz_w++; }
/* V3.21: scroll2 cells going back to the static map wait here until the additions (scroll2 and scroll3) are out: when a
   ship changes layer (the arcade rewrites ~220 scroll2 and ~100 scroll3 cells in one frame, the MD needs ~25 frames) the
   old picture stays until the new one is there, instead of the ship being gone in between */
static volatile u16 dyr_q[128]; static volatile u32 dyr_w, dyr_r;   /* COMM8 (COMM10 = 0xFFFF) */
UI static u32 dy_find(const u32 *t, u32 key)
{
    int lo = 0, hi = (int)t[0] - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1; u32 k = t[1 + mid * 2];
        if (k < key) lo = mid + 1; else if (k > key) hi = mid - 1; else return t[2 + mid * 2];
    }
    return 0xFFFF;
}
UI static void dyn3_scan(pk_t *q)
{
    const u32 *t = 0;
    if (q->bgon && q->stage < 16) { u32 a = DY_TAB[16 + q->stage]; if (a) t = (const u32 *)(0x02000000 + a); }
    if (!t || WRAM_PTR[0x81D8] != q->stage) { if (dy_on) { for (int i = 0; i < 16; i++) dy_tag[i] = -1; dy_on = 0; } return; }
    if (!dy_on) { for (int i = 0; i < 16; i++) dy_tag[i] = -1; for (int i = 0; i < 16; i++) d3dm[i] = ~0u; dy_on = 1; dy_ph = 0; dz_push(0x1000 | (127 << 5) | 30, 0); }
    const u32 *gb = (const u32 *)gptr(((u32)io_cpsa[3] * 256) & 0x3c000);
    { static u32 lb = ~0u; u32 b = io_cpsa[3]; if (b != lb) { lb = b; for (int i = 0; i < 16; i++) d3dm[i] = ~0u; } }
    int c0 = (q->cam[2] + 64) >> 5, r0 = ((1792 - q->cam[3] + 16) >> 5) - 1;   /* game camera = scroll3 x, 1792 - y; visible columns only */
    if (c0 < 0) c0 = 0;
    /* animated cells (water): the arcade adds 1 to code bits 0-1 of all of them at once; one phase message
       instead of one per cell (the sources sent are for phase 0, the MD adds the phase) */
    int hits = 0;
    u32 wm = 0; for (int r = r0; r < r0 + 9; r++) if ((u32)(r - 48) < 24) wm |= bit32[r & 31];   /* V3.17: rows in view */
    for (int c = c0; c < c0 + 13 && c < 128; c++) {
        int sl = c & 15; u32 dm = d3dm[c & 15] & wm;
        if (dy_tag[sl] != c || !dm) continue;
        for (int r = r0; dm && r < r0 + 9; r++) {
            int y = r - 48; u32 bit = bit32[r & 31];
            if (!(dm & bit)) continue;
            dm &= ~bit;
            if (!(dy_src[sl][y] & 0x8000) || dy_src[sl][y] == 0xFFFF) continue;
            u32 v = gb[(r & 7) + ((c & 63) << 3) + ((r & 0x38) << 6)], o = dy_val[sl][y];
            if (v != o && (v & 0x3FFC007F) == (o & 0x3FFC007F) && (((v >> 16) - (o >> 16)) & 3) == 1) hits++;
        }
    }
    if (!NOPH && hits >= 3 && dy_w == dy_r && dyr_w == dyr_r && dz_w - dz_r < 8) {   /* V3.21: not in a burst of cells (the water waits: a phase costs the MD most of a frame) */ dy_ph = (dy_ph + 1) & 3; dz_push(0x1000 | (127 << 5) | 30, dy_ph); }
    static u8 d3_rr;                            /* round robin start (as dyn2_scan) */
    if (d3_rr >= 13) d3_rr = 0;
    for (int k = 0; k < 13; k++) {
        int c = c0 + (d3_rr + k) % 13;
        if (c >= 128) continue;
        int sl = c & 15;
        if (dz_w - dz_r > 64 - 10) { d3_rr = (d3_rr + k) % 13; break; }   /* queue full: the rest next frame */
        if (dy_tag[sl] != c) {                      /* column enters the window: the MD goes back to the static map for it */
            dy_tag[sl] = c; for (int y = 0; y < 24; y++) { dy_val[sl][y] = 0xFFFFFFFF; dy_src[sl][y] = 0xFFFF; }
            dz_push(0x1000 | (c << 5) | 31, 0xFFFF);
            d3dm[sl] = ~0u;
        }
        u32 dm = d3dm[c & 15];
        if (!dm) continue;                          /* V2.5: nothing written in this column since it was last looked at */
        u32 todo = dm & wm;
        for (int r = r0; todo && r < r0 + 9; r++) {
            int y = r - 48;
            u32 bit = bit32[r & 31];
            if (!(todo & bit)) continue;
            todo &= ~bit; dm &= ~bit;
            u32 v = gb[(r & 7) + ((c & 63) << 3) + ((r & 0x38) << 6)];
            if (v == dy_val[sl][y]) continue;
            dy_val[sl][y] = v;
            u32 src = (v & 0x3FFF0000) ? dy_find(t, (v & 0x3FFF0000) | (v & 0x7F)) : 0xFFFF;   /* code 0: not written yet */
            if (src != 0xFFFF && (src & 0x8000)) src &= ~3u;   /* water: the group (phase 0 row); the MD animates the tiles */
            if (src == dy_src[sl][y]) continue;
            dy_src[sl][y] = src;
            dz_push(0x1000 | (c << 5) | y, src);
        }
        d3dm[sl] = dm;                              /* the rows looked at are clean (the rest keep their bits) */
    }
#ifdef DCHK   /* test: a clean cell must hold what was last looked at */
    for (int c = c0; c < c0 + 13 && c < 128; c++) { int sl = c & 15; if (dy_tag[sl] != c) continue;
        for (int r = r0; r < r0 + 9; r++) { int y = r - 48; if ((u32)y >= 24 || (d3dm[sl] >> (r & 31) & 1)) continue;
            ((volatile u32 *)0x26007000)[4]++;
            if (gb[(r & 7) + ((c & 63) << 3) + ((r & 0x38) << 6)] != dy_val[sl][y]) ((volatile u32 *)0x26007000)[1]++; } }
#endif
}
/* stage 2 harbour scroll2 (the ships): the arcade rewrites these cells.  Table (tools/bg4/dyn2.py) at
   BGPRI_TAB[32 + stage]: u32 W, n, n x {key, src} sorted, u32 static key [W][48]; key = code << 16 | attr & 0x1FF,
   src = MD map column << 6 | row of the same cell (0xFFFE nothing, 0xFFFF the static map).  Messages to the MD:
   COMM8 0x3000 | slot << 6 | row (COMM10 src), 0x3800 | row (row back to static, all slots),
   0x3C00 | slot (COMM10 column: the slot now holds that column, all static).  32 column slots x a ring of 16 rows. */
static u16 d2_bl[32];   /* V3.21: the cell's last message was 0xFFFE (nothing): see dyr_q */
static u32 d2_val[32][16] RD2; static u16 d2_ov[32] RD2; static s16 d2_tag[32] RD2;   /* d2_val: the arcade cell last scanned (V1.9; was the source sent) */ static s8 d2_ry[16] RD2; static u8 d2_on, d2_on2;
UI2 static u32 d2_find(const u32 *t, u32 n, u32 key)
{
    int lo = 0, hi = (int)n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1; u32 k = t[mid * 2];
        if (k < key) lo = mid + 1; else if (k > key) hi = mid - 1; else return t[mid * 2 + 1];
    }
    return 0xFFFF;
}
/* V3.2: background palette cycles (stage 6's burning city): the version of each cycled arcade palette (found by its
   probe colour) to the MD, which rewrites the CRAM slots those colours were quantized to (md/bg.c pv_apply);
   sent again every 32 frames (a stage load or a picture puts the base colours back) */
UI2 static void pv_scan(pk_t *q)
{
    static u8 pv_last[4], pv_st = 0xFF, pv_fc;
    if (!q->bgon || q->stage >= 8 || WRAM_PTR[0x81D8] != q->stage) { pv_st = 0xFF; return; }
    const u8 *t = pv_tab[q->stage];
    if (!t) return;
    if (pv_st != q->stage) { pv_st = q->stage; for (int i = 0; i < 4; i++) pv_last[i] = 0xFF; }
    int resend = !(++pv_fc & 31);
    const u8 *pal = gptr(((u32)io_cpsa[5] * 256) & 0x3ffff);
    u32 ng = *t++;
    for (u32 g = 0; g < ng && g < 4; g++) {
        u32 p = t[0], e = t[1], nv = t[2], v;
        u32 val = *(const u16 *)(pal + p * 32 + e * 2);
        for (v = 0; v < nv; v++) if ((u32)((t[3 + v * 2] << 8) | t[4 + v * 2]) == val) break;
        if (v < nv && (v != pv_last[g] || resend) && dy_w - dy_r < 120) { dy_push(0x2400 | (g << 4) | v, 0); pv_last[g] = v; }
        t += 3 + nv * 2;
    }
}
UI2 static void dyn2_scan(pk_t *q)
{
    const u32 *t = 0;
    if (q->bgon && q->stage < 16) { u32 a = DY_TAB[32 + q->stage]; if (a) t = (const u32 *)(0x02000000 + a); }
    if (!t || WRAM_PTR[0x81D8] != q->stage) { d2_on = 0; d2_on2 = 0; return; }
    if (!d2_on) { for (int i = 0; i < 32; i++) d2_tag[i] = -1; for (int i = 0; i < 16; i++) d2_ry[i] = -1; d2_on = 1; }
    u32 X0 = t[0] >> 16, W = t[0] & 0xFFFF, n = t[1]; const u32 *tab = t + 2, *st = t + 2 + n * 2 - X0 * 48;   /* V3.2: static keys from column X0 (W high half) */
    const u32 *gb = (const u32 *)gptr(((u32)io_cpsa[2] * 256) & 0x3c000);
    { static u32 lb = ~0u; u32 b = io_cpsa[2]; if (b != lb || !d2_on2) { lb = b; d2_on2 = 1; for (int i = 0; i < 32; i++) d2dm[i] = 0xFFFF; } }
    int cx0 = ((q->cam[0] + 96) >> 4) - 1, ry0 = ((272 - q->cam[1]) >> 4) - 1;   /* scroll2 x = cam, y = 768 - cam */
    for (int y = ry0; y < ry0 + 14; y++) {
        if ((u32)y >= 48 || d2_ry[y & 15] == y) continue;
        if (dy_w - dy_r > 128 - 4) return;
        d2_ry[y & 15] = y; for (int s = 0; s < 32; s++) { d2_val[s][y & 15] = 0xFFFFFFFF; d2_ov[s] &= ~(1u << (y & 15)); d2_bl[s] &= ~(1u << (y & 15)); }
        for (int i = 0; i < 32; i++) d2dm[i] |= bit32[y & 15];   /* V2.5: the row is looked at again in every column */
        dy_push(0x3800 | y, 0);
    }
#ifdef D2DBG
    int nd = 0, q0 = dy_w - dy_r, cl = -1;
#endif
    /* round robin: start where the last full queue stopped (cells changing every frame on the left kept
       the columns on the right from ever being sent: V1.8 ships docking) */
    static u8 d2_rr;
    if (d2_rr >= 24) d2_rr = 0;
    u32 wm = 0; for (int y = ry0; y < ry0 + 14; y++) if ((u32)y < 48) wm |= bit32[y & 15];
    for (int k = 0; k < 24; k++) {
        int c = cx0 + (d2_rr + k) % 24;
        if ((u32)c >= W || c < (int)X0) continue;
        int sl = c & 31;
        if (dy_w - dy_r > 128 - 16) { d2_rr = (d2_rr + k) % 24; break; }   /* queue full: the rest next frame */
#ifdef D2DBG
        cl = c;
#endif
        if (d2_tag[sl] != c) { d2_tag[sl] = c; for (int r = 0; r < 16; r++) d2_val[sl][r] = 0xFFFFFFFF; d2_ov[sl] = 0; d2_bl[sl] = 0; dy_push(0x3C00 | sl, c); d2dm[sl] = 0xFFFF; }
        u32 dm = d2dm[sl];
        if (!dm) continue;                          /* V2.5: nothing written in this column since it was last looked at */
        u32 todo = dm & wm;                         /* V3.17: only the marked rows in view, stop when none is left */
        for (int y = ry0; todo && y < ry0 + 14; y++) {
            u32 db = bit32[y & 15];                /* (r & 15: r = y + 32) */
            if (!(todo & db)) continue;
            todo &= ~db; dm &= ~db;
            int r = y + 32;
            u32 v = gb[(r & 15) + ((c & 63) << 4) + ((r & 0x30) << 6)];
            u32 *pv = &d2_val[sl][y & 15];
            if (v == *pv) continue;                 /* unchanged since the last scan (V1.9: no table search) */
            *pv = v;
            u32 key = (v & 0xFFFF0000) | (v & 0x1FF), sk = st[c * 48 + y], src;
            if (key == sk) src = 0xFFFF;
            else { src = d2_find(tab, n, key); if (src == 0xFFFE && !sk) src = 0xFFFF; }
#ifdef D2DBG
            if (src != 0xFFFF) nd++;
#endif
            u32 bit = bit32[y & 15];
            if (src == 0xFFFF) { if (!(d2_ov[sl] & bit)) continue; d2_ov[sl] &= ~bit; }   /* static already */
            else d2_ov[sl] |= bit;
            if (src == 0xFFFE) d2_bl[sl] |= bit; else d2_bl[sl] &= ~bit;
            dy_push(0x3000 | (sl << 6) | y, src);
        }
        d2dm[sl] = dm;
    }
#ifdef DCHK
    for (int k = 0; k < 24; k++) { int c = cx0 + k, sl = c & 31; if ((u32)c >= W || d2_tag[sl] != c) continue;
        for (int y = ry0; y < ry0 + 14; y++) { if ((u32)y >= 48 || d2_ry[y & 15] != y || (d2dm[sl] >> (y & 15) & 1)) continue;
            int r = y + 32; ((volatile u32 *)0x26007000)[5]++;
            if (gb[(r & 15) + ((c & 63) << 4) + ((r & 0x30) << 6)] != d2_val[sl][y & 15]) ((volatile u32 *)0x26007000)[0]++; } }
#endif
#ifdef D2DBG
    { static u32 fc; if (!(++fc & 7) && dy_w - dy_r < 124) { { static u16 fr; fr++; int r = 74, c = 63, y = 42; u32 v = gb[(r & 15) + ((c & 63) << 4) + ((r & 0x30) << 6)]; dy_push(0x3830, fr); dy_push(0x3831, cx0); dy_push(0x3832, ry0); dy_push(0x3833, v >> 16); dy_push(0x3834, v); dy_push(0x3835, st[c * 48 + y] >> 16); dy_push(0x3836, d2_find(tab, n, (v & 0xFFFF0000) | (v & 0x1FF))); dy_push(0x3837, d2_val[c & 31][y & 15]); dy_push(0x3838, d2_tag[c & 31]); dy_push(0x3839, d2_ry[y & 15]); dy_push(0x383A, q0); dy_push(0x383B, fr); } } }
#endif
}
/* the 32X priority cells of an overridden harbour scroll2 cell are not the static map's */
static inline int d2_over(int ac, int ay)
{
    if (!d2_on || (u32)ay >= 48) return 0;
    int sl = ac & 31;
    return d2_tag[sl] == ac && d2_ry[ay & 15] == ay && (d2_ov[sl] & bit32[ay & 15]);
}
extern volatile u8 snd_q[64]; extern volatile u32 snd_w;
static u32 snd_r;
#ifdef D2DBG
u32 an_calls, an_busy, an_dy;
#endif
/* one message to the MD if it took the last one (interrupts masked).  The MD raises the CMD interrupt when it
   has taken a message (md/bg.c msg_ack), so the queue drains at the MD's pace without polling here. */
static volatile u8 msg_out; static volatile u8 msg_age;  /* a message is with the MD (cleared by its CMD interrupt) */
static void msg_send(int acked)
{
    /* COMM8 clear = the MD took the last message (the CMD interrupt only makes the next one go out sooner) */
    if (MARS_SYS_COMM8) return;
#ifdef D2DBG
    an_calls++;
#endif
    msg_age = 0;
    { extern volatile u8 ui_volq, ui_opt[8];      /* volume change (options): 0x2000 | which << 8 | level 0-10 */
      if (ui_volq) { int k = ui_volq & 1 ? 0 : 1; ui_volq &= ~(1 << k); msg_out = 1; MARS_SYS_COMM8 = 0x2000 | (k << 8) | ui_opt[3 + k]; return; } }
    if (snd_r != snd_w) { msg_out = 1; MARS_SYS_COMM8 = 0x4000 | snd_q[snd_r & 63]; snd_r++; return; }   /* sound command */
    { extern volatile u8 in_logic; extern volatile int logic_msgs;
      if (acked && in_logic && (dy_r != dy_w || dz_r != dz_w || dyr_r != dyr_w) && --logic_msgs < 0) return; }   /* the rest after the logic (main.c) */
    if (dy_r != dy_w || dz_r != dz_w || dyr_r != dyr_w) {   /* harbour cell messages: scroll2 additions, scroll3, scroll2 removals */
        u32 m = 0;
        while (dy_r != dy_w) {
            u32 x = dy_q[dy_r & 127]; dy_r++;
            if ((x >> 28) == 3 && !(x & 0x08000000) && (x & 0xFFFF) >= 0xFFFE && dyr_w - dyr_r < 128) { dyr_q[dyr_w & 127] = (x >> 16) | ((x & 1) ? 0 : 0x8000); dyr_w++; continue; }   /* bit 15: 0xFFFE */
            m = x; break;
        }
        if (!m && dz_r != dz_w) { m = dz_q[dz_r & 63]; dz_r++; }
        while (!m && dyr_r != dyr_w) {
            u32 e = dyr_q[dyr_r & 127], bl = e >> 15, x = ((0x3000 | (e & 0x0FFF)) << 16) | (bl ? 0xFFFE : 0xFFFF); dyr_r++;
            u32 sl = (x >> 22) & 31, y = (x >> 16) & 63, bit = 1u << (y & 15);
            if (d2_ry[y & 15] == (int)y && (bl ? !(d2_bl[sl] & bit) : (d2_ov[sl] & bit))) continue;   /* rewritten since: that message went out already */
            m = x;
        }
        if (m) {
            msg_out = 1; MARS_SYS_COMM10 = m & 0xFFFF; MARS_SYS_COMM8 = m >> 16;
#ifdef D2DBG
            an_dy++;
#endif
            return;
        }
    }
    u8 t = an_tick;
    if (t != an_tick_sent) {                    /* ticks first: the pair values below already include them */
        msg_out = 1; MARS_SYS_COMM8 = 0xA000 | ((t - an_tick_sent) & 0xFF); an_tick_sent = t;
    } else if (an_dirty) {                      /* (the full scan only when something changed: msg_send runs on every CMD) */
      an_dirty = 0;
      for (int k = 0; k < 128; k++) {           /* next changed row pair (round robin) */
        u32 i = (an_rr + k) & 127, slot = i >> 1, pr = i & 1;
        u8 v = an_ver[slot][pr];
        if (v == an_sent[slot][pr]) continue;
        s16 c = an_col[slot];
        an_sent[slot][pr] = v;
        if (c < 0) continue;
        an_rr = i + 1;
        msg_out = 1;
        MARS_SYS_COMM10 = (an_idx[slot][pr * 2] << 8) | an_idx[slot][pr * 2 + 1];
        MARS_SYS_COMM8 = 0x8000 | (pr << 8) | c;
        an_dirty = 1;                           /* maybe more */
        break;
      }
    }
}
static void anim_send(void)
{
    { extern void pads_sample(void); pads_sample(); }
    u32 sr;
    __asm__ volatile("stc sr,%0" : "=r"(sr));
    __asm__ volatile("ldc %0,sr" :: "r"(sr | 0xF0));    /* not with the scan (vblank) or the CMD interrupt in between */
    msg_send(0);
    __asm__ volatile("ldc %0,sr" :: "r"(sr));
}
#ifdef D2DBG
u32 an_cmd;
void master_cmd(void) { an_cmd++; msg_out = 0; msg_send(1); }
#else
void master_cmd(void) { msg_out = 0; msg_send(1); }   /* the MD took a message */
#endif
/* V2.1: pictures the cart has no room for (ROUND maps 2-8, the generals after part 1) are shown by the MD
   (md/bg.c pic_show, tools/mdpic.py); here: which one, and its brightness (the reference colour's level in the live
   palette against full brightness: the fades) */
#include "../data/mdpic.h"
static u8 t5[16][16];
#undef CP
#define CP Q
UI2 static void mdpic_pick(pk_t *q)
{
    q->pic = 0xFF;
    if (!(flags & 0x10)) return;
    int st = WRAM_PTR[0x81D7];
    for (u32 i = 0; i < sizeof mdpic_tab / sizeof mdpic_tab[0]; i++) {
        if (mdpic_tab[i].stage != st) continue;
        if (mdpic_tab[i].kind == 0) { if (q->layout != 3) continue; }
        else {
            if (!((layerctl & 8) && (q->vc & 8)) || q->sc23[2] != mdpic_tab[i].sx || q->sc23[3] != mdpic_tab[i].sy) continue;
            const u16 *b3 = (const u16 *)q->b3; int ok = 1;
            for (int k = 0; k < 4; k++) {
                u32 c = mdpic_tab[i].sig[k][0], r = mdpic_tab[i].sig[k][1], code = mdpic_tab[i].sig[k][2];
                if (code && b3[((r & 7) + ((c & 63) << 3) + ((r & 0x38) << 6)) * 2] != code) ok = 0;
            }
            if (!ok) continue;
        }
        u16 raw = cpspal[mdpic_tab[i].ref]; const u8 *t = t5[raw >> 12];
        u32 l = t[(raw >> 8) & 15] + t[(raw >> 4) & 15] + t[raw & 15], ref = mdpic_tab[i].lvl;
        u32 lv = ref ? (l * 16 + ref / 2) / ref : 16;
        q->pic = mdpic_tab[i].id; q->piclv = lv > 16 ? 16 : lv; q->wantv = 1;
        return;
    }
}
/* V2.3/V3.11: the long flame rows (stage 2's burning ships; code 0x24c0-0x253f, palette 0x11) are put up in turns,
   the upper row one frame, the lower the next (a 60 Hz flicker on the arcade).  The 32X shows every other logic frame
   when it draws below 60 fps: always the same row.  V3.11: every drawn frame shows the other row than the last drawn
   one (this frame's own row, or the one kept from an earlier frame), so the rows flicker in turns as on the arcade
   (V3.1 drew both with half their pixels). */
static u16 fk_e[24 * 4] HB, fk_n, fk_front, fk_last;
volatile u16 fl_drawn;
static inline int flame(const u16 *e) { return (e[3] & 0x1f) == 0x11 && e[2] >= 0x24c0 && e[2] < 0x2540; }
UI2 static void flame_merge(void)   /* on P */
{
    u16 *sp = spr; u32 n = nspr, cy = 0xffff, first = 0xffff;
    for (u32 i = 0; i < n; i++) if (flame(&sp[i * 4])) { first = i; cy = sp[i * 4 + 1]; break; }
    if (first == 0xffff) { fk_last = 0; return; }
    u16 own[24 * 4]; u32 nn = 0;
    for (u32 i = first; i < n && nn < 24; i++) if (flame(&sp[i * 4])) { for (int j = 0; j < 4; j++) own[nn * 4 + j] = sp[i * 4 + j]; nn++; }
    u32 ofront = first == 0;
    if (cy == fl_drawn && fk_n && fk_e[1] != cy) {      /* on screen now: show the other row (the previous frame's) */
        u32 m = 0;
        for (u32 i = 0; i < n; i++) if (!flame(&sp[i * 4])) { if (m != i) for (int j = 0; j < 4; j++) sp[m * 4 + j] = sp[i * 4 + j]; m++; }
        u32 add = m + fk_n <= 256 ? fk_n : 0, k = m;
        if (fk_front) { for (int i = (int)m * 4 - 1; i >= 0; i--) sp[i + add * 4] = sp[i]; k = 0; }
        for (u32 i = 0; i < add * 4; i++) sp[k * 4 + i] = fk_e[i];
        nspr = m + add;
        fk_last = fk_e[1];
    } else fk_last = cy;
    for (u32 i = 0; i < nn * 4; i++) fk_e[i] = own[i];
    fk_n = nn; fk_front = ofront;
}
UI static void snapshot(pk_t *q)   /* V3.15: once a frame: in the cart (room in SDRAM) */
{
    Q = q;
    /* sprites: CPS1 shows the table latched at the previous vblank */
    for (int i = 0; i < (int)nlatch * 4; i++) spr[i] = objlatch[i];
    nspr = nlatch;
    flame_merge();
    q->flrow = fk_last;
    const u16 *obj = (const u16 *)gptr(((u32)io_cpsa[0] * 256) & 0x3f800);
    int lo = 0, hi = 256;
    while (lo < hi) { int mid = (lo + hi) >> 1; if (spr_unused(obj + mid * 4)) hi = mid; else lo = mid + 1; }
    nlatch = lo;
    for (int i = 0; i < lo * 4; i++) objlatch[i] = obj[i];
    s1x = io_cpsa[6]; s1y = io_cpsa[7]; layerctl = io_cpsb[0x2c / 2];
    s1base = (u32)gptr(((u32)io_cpsa[1] * 256) & 0x3c000);
    { extern volatile u32 pal_serial;   /* V3.17: only when the game latched colours since this packet's copy (2 packets) */
      if (q->palser != pal_serial + 1) { q->palser = pal_serial + 1;
        const u32 *a = (const u32 *)cpspal_live; u32 *b = (u32 *)cpspal; for (int i = 0; i < 1024; i += 4) { b[i] = a[i]; b[i + 1] = a[i + 1]; b[i + 2] = a[i + 2]; b[i + 3] = a[i + 3]; } } }
    /* scroll2/scroll3 (drawn by the 32X outside the stage scenes) */
    q->sc23[0] = io_cpsa[8]; q->sc23[1] = io_cpsa[9]; q->sc23[2] = io_cpsa[10]; q->sc23[3] = io_cpsa[11];
    q->vc = io_cpsa[0x22 / 2];
    q->b2 = (u32)gptr(((u32)io_cpsa[2] * 256) & 0x3c000); q->b3 = (u32)gptr(((u32)io_cpsa[3] * 256) & 0x3c000);
    q->ep23 = s23_writes;
    int s1on = layerctl & 2;
    /* character select (all in scroll1): the 384-pixel width is shrunk to 320 */
    int sel = s1on && ((s1cell(13, 21) == 0x60fd && s1cell(25, 21) == 0x60fd && s1cell(37, 23) == 0x60d1 && s1cell(13, 25) == 0x60ed)
                       || (s1cell(1, 2) == 0x7a10 && s1cell(5, 1) == 0x7a04));   /* first card: from the first frame on */
    /* in game the HUD rows (22-27) are shown from the arcade's left edge ("パワー" of the 1P panel) */
    int hud = s1on && !sel && hud_on();       /* 1P or (V1.9) 2P panel */
    flags = (s1on ? 2 : 0) | (sel ? 4 : 0) | (hud ? 8 : 0);
    if (hud) for (int c = 8; c < 40; c++) { u16 v = s1cell(c, 22); if ((v >= 0x6800 && v < 0x7800) || (v >= 0x8000 && v < 0x9000)) { flags |= 0x40; break; } }   /* talk in the HUD */
    /* scaled scene: menus/story with scroll2/3 pictures (latched for the rest of the scene) */
    { static u16 lastsc = 0xffff; static u8 lat, laylat, kill;
      const u8 *w = WRAM_PTR; u16 sc = (w[0x81D7] << 8) | w[0x81D8];
      if (sc != lastsc) {   /* V2.2: scene 8 straight from the stage scene = the boss kill picture (its text in place, unmoved) */
          kill = w[0x81D8] >= 8 && w[0x81D8] <= 11 && (lastsc >> 8) < 32 && (lastsc & 0xFF) == (lastsc >> 8);   /* V3.21: 8 + character (0x7EE4), not only 張飛 */
          lastsc = sc; lat = 0; laylat = 0; }
      int on23 = ((layerctl & 4) && (q->vc & 4)) || ((layerctl & 8) && (q->vc & 8));
      if (!sel && !hud && w[0x81D8] >= 8 && on23) lat = 1;
      if (lat && !sel && !hud) flags |= 0x10;
      /* text layout in the scaled scenes (scroll1 and sprites are not scaled, only moved) */
      int lay = 0;
      if (flags & 0x10) {
          u8 scn = w[0x81D8];
          int hs = 0;
          if (s1on) for (int r = 0; r < 28; r++) if (s1cell(9, r) == 0x6be0 || (s1cell(38, r) == 0x6404 && s1cell(39, r) == 0x6405)) { hs = 1; break; }   /* 大 (the Japanese) or "CA" of CAP */
          if (s1on && ((s1cell(22, 2) == 0x6e60 && s1cell(23, 2) == 0x6e61) || (s1cell(23, 2) == ZH_RES_DENG && s1cell(24, 2) == ZH_RES_DENG + 1))) lay = 5;   /* stage clear results (scene 8 too) */
          else if (scn >= 8 && scn <= 11 && !kill) lay = 1;         /* character introductions (V2.2: not the boss kill, scene 8 too) */
          else if (s1on && s1cell(2, 8) == 0x7728) lay = 3;         /* ROUND map (its frame is scaled with the map); scene 48 too */
          else if (hs) lay = 2;                                     /* high score table (scrolls up into place) */
          else if (scn >= 44 && scn <= 47) lay = 4;                 /* continue */
          /* latched for the scene: the game redraws scroll1 (the ROUND map alternates frames) */
          if (lay) laylat = lay; else lay = laylat;
      }
      q->layout = lay;
#ifdef DBGLAY
      { extern u16 dbg_cc[8]; static const u8 cc[8][2] = {{22,2},{23,2},{24,2},{2,4},{3,4},{2,6},{3,6},{4,1}}; for (int i = 0; i < 8; i++) dbg_cc[i] = s1cell(cc[i][0], cc[i][1]); }
#endif
    }
    /* stage background camera (the game's own scroll2/scroll3 positions), for the MD planes */
    { const u8 *w = WRAM_PTR;
      q->cam[0] = *(const s16 *)(w + 0x8170); q->cam[1] = *(const s16 *)(w + 0x8172);
      q->cam[2] = *(const s16 *)(w + 0x8178); q->cam[3] = *(const s16 *)(w + 0x817A);
      static u8 bgl; int st1 = w[0x81D8] == w[0x81D7] && w[0x81D7] < 32;   /* in a stage (scene = stage number) */
      static u8 away; if (!st1) { if (away < 60) away++; else bgl = 0; } else { away = 0; if (hud) bgl = 1; }
      q->bgon = st1 && bgl;   /* in game (from the HUD on; kept when the HUD goes at the stage end) */
      q->bgtail = !st1 && bgl && away <= 10;   /* V3.2: just left the stage: the MD planes may still show for a few frames */
      q->stage = w[0x81D7] & 31;
      q->wantv = st1 || w[0x81D8] == 48;     /* the MD may load this stage's background (ROUND screen or in the stage) */
      if (q->layout == 5) { q->stage = (q->stage + 1) & 31; q->wantv = 1; }   /* stage clear results: the next stage */
      { extern int ui_stage_pending(void); if (!st1 && ui_stage_pending()) q->wantv = 1; }
      /* V3.22: attract mode (no players, 0xFF8155): stage 1's background is read in already for the demo (after a game
         the MD has the last stage's: the demo showed no background for the ~3 s the CD read takes) */
      if (!st1 && !w[0x8155] && !ui_stage_pending()) { q->stage = 0; q->wantv = 1; } }   /* V2.1: the stage chosen in the options, from the character select on */
    mdpic_pick(q);
    { extern volatile u8 ui_mode, ui_sel, ui_opt[8], ui_six; extern volatile u16 ui_pad, ui_hold;
      q->ui_mode = ui_mode; q->ui_sel = ui_sel; for (int i = 0; i < 7; i++) q->ui_opt[i] = ui_opt[i]; q->ui_fsa = ui_opt[7];
      q->ui_six = ui_six; q->ui_pad = ui_pad; q->ui_hold = ui_hold; }
    { u32 sr; __asm__ volatile("stc sr,%0" : "=r"(sr)); __asm__ volatile("ldc %0,sr" :: "r"(sr | 0xF0));
      anim_scan(q);                         /* (not with a message being sent: master_cmd) */
      __asm__ volatile("ldc %0,sr" :: "r"(sr)); }
#ifndef DYNLATE
    dyn3_scan(q);
#ifndef NOD2
    dyn2_scan(q);
#endif
#endif
    pv_scan(q);
}
#undef CP
#define CP P

/* ui.c (2P): HUD panel k (0 1P, 1 2P) shows the press start / insert coin prompt (its second line starts with kana:
   map row 29, column 16 / 33) and the game is in a stage (the HUD is on) */
UI2 int ui_panel_prompt(int k)
{
    if (!PK[pk_r]->bgon && !(flags & 8)) return 0;
    u32 base = (u32)gptr(((u32)io_cpsa[1] * 256) & 0x3c000);
    u16 v = *(const u16 *)(base + ((29 & 0x1f) + (((k ? 33 : 16) & 63) << 5)) * 4);
    return v >= 0x61A0 && v <= 0x61FF;
}
/* front end (ui.c): the title screen (CAPCOM logo in scroll1 rows 20-21) and its CREDIT line */
static inline u16 s1cell_live(int sc, int sr)
{
    u32 base = (u32)gptr(((u32)io_cpsa[1] * 256) & 0x3c000);
    u16 col = (((u16)(64 + io_cpsa[6]) >> 3) + sc) & 63, row = (((u16)(16 + io_cpsa[7]) >> 3) + sr) & 63;
    return *(const u16 *)(base + ((row & 0x1f) + (col << 5) + ((row & 0x20) << 6)) * 4);
}
UI int ui_title_visible(void) { return (io_cpsb[0x2c / 2] & 2) && s1cell_live(16, 20) == 0x620e && s1cell_live(20, 20) == 0x6202; }
/* V3.9/V3.11: "CD32X Port by Mana" in the arcade's own 8x8 font (codes 0x6000 + ASCII, palette 0 like the copyright line)
   on the title screen, one row below the copyright (row 25); both lines centred under the 英 of 集英社 (screen column
   24 = x 192 of 384 = the 32X screen's centre): the copyright line (screen columns 11-32) moved 2 columns right.
   Taken away again when the title goes. */
static inline u32 s1off_live(int sc, int sr)
{
    u16 col = (((u16)(64 + io_cpsa[6]) >> 3) + sc) & 63, row = (((u16)(16 + io_cpsa[7]) >> 3) + sr) & 63;
    return (((u32)io_cpsa[1] * 256) & 0x3c000) + ((row & 0x1f) + (col << 5) + ((row & 0x20) << 6)) * 4;
}
UI void ui_title_port(void)
{
    static const char txt[] = "CD32X Port by Mana";
    static u8 shown;
    static u16 cw[22][2];                       /* the copyright line as the arcade wrote it (screen columns 11-32) */
    extern void slow_ww(u32 a, u32 v);
    int vis = ui_title_visible();
    if (!vis && !shown) return;
    for (int i = 0; txt[i]; i++) {
        u32 off = s1off_live(15 + i, 26);
        if (off >= 0x10000) continue;               /* (scroll1 is in the first page: slow_ww marks its rows) */
        u16 want = txt[i] == ' ' ? 0x6020 : 0x6000 + txt[i];
        const u16 *p = (const u16 *)gptr(off);
        if (vis) { if (p[0] != want || (p[1] & 0x1F)) { slow_ww(0x900000 + off, want); slow_ww(0x900000 + off + 2, p[1] & ~0x1F); } }
        else if (p[0] == want && txt[i] != ' ') slow_ww(0x900000 + off, 0x6020);
    }
    if (vis) {
        const u16 *c11 = (const u16 *)gptr(s1off_live(11, 25));
        if (c11[0] == 0x6023 && s1off_live(34, 25) < 0x10000 && s1off_live(11, 25) < 0x10000) {   /* as the arcade wrote it: move it */
            for (int i = 0; i < 22; i++) { const u16 *p = (const u16 *)gptr(s1off_live(11 + i, 25)); cw[i][0] = p[0]; cw[i][1] = p[1]; }
            for (int i = 21; i >= 0; i--) { u32 o = s1off_live(13 + i, 25); slow_ww(0x900000 + o, cw[i][0]); slow_ww(0x900000 + o + 2, cw[i][1]); }
            for (int i = 0; i < 2; i++) { u32 o = s1off_live(11 + i, 25); slow_ww(0x900000 + o, 0x6020); }
        }
    } else if (shown) {                             /* the arcade clears its own columns 11-32: the 2 moved past them go */
        for (int i = 20; i < 22; i++) {
            u32 o = s1off_live(13 + i, 25); const u16 *p = (const u16 *)gptr(o);
            if (o < 0x10000 && p[0] == cw[i][0] && p[0] != 0x6020) slow_ww(0x900000 + o, 0x6020);
        }
    }
    shown = vis;
}
/* V3.20: the ending's last screen (吞食天地 第2彈 ~赤壁之戰~ … 敬請期待……終, message 0x34 drawn by 0x4520C straight into
   scroll1 map cells: 終 at column 50 row 26, 吞 at column 18 row 11) gets the port's staff line in the arcade's 8x8
   font, two centred lines on screen rows 17 and 19 (map rows 19, 21; the 32X shows map columns 12-51), palette as 終 */
UI void ui_end_credit(void)
{
    static const char l1[] = "32X CD Port & Chinese Translation", l2[] = "by Mana";
    static u8 shown;
    extern void slow_ww(u32 a, u32 v);
    const u16 *e = (const u16 *)gptr(50 * 0x80 + 26 * 4), *t = (const u16 *)gptr(18 * 0x80 + 11 * 4);
    int vis = e[0] == 0x817a && t[0] == 0x88f8;
    if (!vis && !shown) return;
    for (int k = 0; k < 2; k++) {
        const char *s = k ? l2 : l1; int c0 = k ? 28 : 15, row = k ? 21 : 19;
        for (int i = 0; s[i]; i++) {
            u32 off = (c0 + i) * 0x80 + row * 4;
            u16 want = s[i] == ' ' ? 0x6020 : 0x6000 + s[i];
            const u16 *p = (const u16 *)gptr(off);
            if (vis) { if (p[0] != want || p[1] != e[1]) { slow_ww(0x900000 + off, want); slow_ww(0x900000 + off + 2, e[1]); } }
            else if (p[0] == want && s[i] != ' ') slow_ww(0x900000 + off, 0x6020);
        }
    }
    shown = vis;
}
UI int ui_title_credit(void) { u16 c = s1cell_live(38, 27); return c != 0x6020 && c != 0x6120 && c != 0x6000 && c != 0x6100; }
/* vblank interrupt, after the arcade frame: fill the packet that is not being drawn */
void render_logic_done(void)
{
    pk_t *q = PK[pk_r ^ 1];
    snapshot(q);
    q->seq = ++pk_seq;
#ifdef FBID
    { extern volatile u32 lframes; q->lid = lframes; }
#endif
}

/* ---------------- palette ---------------- */
/* t5 (brightness, 4-bit level -> 5-bit): declared before mdpic_pick */
static inline u16 cps2mars(u16 c)
{
    const u8 *t = t5[c >> 12];
    return (u16)((t[c & 15] << 10) | (t[(c >> 4) & 15] << 5) | t[(c >> 8) & 15]);
}
static inline int sprite_code(int code)
{
    int c = code << 1;
    if (c <= 0x3fff) return c >> 1;
    if (c <= 0x5fff) return (0x4000 + (c & 0x3fff)) >> 1;
    return -1;
}
static u32 pt[3], tx[3]; static volatile u32 ws[4]; static volatile int slave_time;
static void build_palette(void)
{
    u8 used[128]; u32 tA = wdt();
    for (int i = 0; i < 128; i++) used[i] = 0;
    if (!P->bgon) scan23(used);
    for (u32 i = 0; i < nspr; i++) used[spr[i * 4 + 3] & 31] = 1;
    if (layerctl & 2) {
        u32 sel = flags & 4, scl = flags & 0x10, wide = sel || scl || (flags & 8);
        u32 c0 = ((wide ? 64 : 96) + s1x) >> 3 & 63, r0 = ((16 + s1y) >> 3);
        u32 nc = (sel || scl) ? 49u : wide ? 45u : 41u;
        u32 g0 = c0 >> 3, g1 = (c0 + nc - 1) >> 3;          /* groups touched (mod 8) */
        u32 su = 0; r0dbg = r0;
        u32 tB = wdt(); ptm[0] += (tB - tA) & 255;
        s1_refresh();
        ptm[1] += (wdt() - tB) & 255;
        for (u32 r = 0; r < 29; r++) {
            u32 row = (r0 + r) & 63, sg = 0, has = 0;
            for (u32 g = g0; g <= g1; g++) {
                u32 gg = g & 7;
                su |= grp_used[row][gg];
                sg = (sg ^ grp_sig[row][gg]) * 0x01000193u;
                has |= grp_has[row] & bit8[gg];
            }
            rowsig[r] = sg; if (has) rowhas |= bit32[r];
        }
        for (int i = 0; i < 32; i++) if (su & bit32[i]) used[32 + i] = 1;
    }
    if (P->ui_mode) { used[32 + 2] = used[32 + 31] = used[32 + 0] = 1; }
    u32 tb0 = wdt(); ptm[2] += (tb0 - tA) & 255;
    /* nothing changed (same palettes in use, same colours): keep the last mapping.
     * V3.13: palettes stay mapped ("sticky") after they leave the screen while their colours do not change, so a
     * palette coming back needs no rebuild (stage 3's rain: the scroll1 palettes in view changed nearly every frame
     * and the whole mapping was rebuilt each time).  A colour change or running out of CRAM slots drops them. */
    { static u8 stk[128]; static u32 sig_last = 1; u32 sig = 0; int same = 1;
      if (pal_exact) { pal_exact = 0; same = 0; for (int i = 0; i < 128; i++) stk[i] = 0; }
      for (int i = 0; i < 128; i++) {
          if (used[i] && !stk[i]) { same = 0; stk[i] = 1; }
          if (stk[i]) { const u32 *c = (const u32 *)&cpspal[i * 16]; for (int k = 0; k < 8; k++) sig = (sig ^ c[k]) * 0x01000193u; } }
      ptm[3] += (wdt() - tb0) & 255;
      if (same && sig == sig_last) return;
      if (same) {   /* colours changed: map only what is in use, recompute the signature */
          sig = 0;
          for (int i = 0; i < 128; i++) { stk[i] = used[i];
              if (used[i]) { const u32 *c = (const u32 *)&cpspal[i * 16]; for (int k = 0; k < 8; k++) sig = (sig ^ c[k]) * 0x01000193u; } }
      }
      for (int i = 0; i < 128; i++) used[i] = stk[i];
      sig_last = sig; dbg_palbuild++; }
    /* colour -> CRAM slot, stable across frames: a colour keeps the slot it had last time
     * (so sprite/scroll1 lookup tables only change for palettes whose colours changed) */
    static u16 hk2[2][512]; static u8 hv2[2][512]; static int init, cur;
    if (!init) { init = 1; for (int i = 0; i < 512; i++) hk2[0][i] = hk2[1][i] = 0xffff; }
    u16 *okey = hk2[cur], *hkey = hk2[cur ^ 1]; u8 *oval = hv2[cur], *hval = hv2[cur ^ 1]; cur ^= 1;
    for (int i = 0; i < 512; i++) hkey[i] = 0xffff;
    u8 taken[256]; for (int i = 0; i < 256; i++) taken[i] = 0;
    taken[0] = 1; taken[255] = 1;   /* 255: fps digits (debug) or opaque black */
    for (int q = BGPRI_SLOT0; q < 255; q++) taken[q] = 1;
    int ncol = 1, pend_n = 0;
    static u16 pend_m[1024]; static u16 pend_ci[1024];
    cram_build[0] = 0x8000;
    for (int pi = 0; pi < 128; pi++) {
        int p = pi < 64 ? pi ^ 32 : pi;
        if (!used[p]) continue;
        for (int pen = 0; pen < 15; pen++) {
            u32 ci = p * 16 + pen; u16 raw = cpspal[ci];
            /* in game: the HUD's dark green band and gauge (scroll1 colour 0x054) becomes black */
            if ((flags & 8) && p >= 32 && p < 64 && (raw & 0xfff) == 0x054) raw = (raw & 0xf000) | 0x111;
            u16 m = cps2mars(raw);
            u32 h = (m * 0x9E37u) >> 7 & 511;
            int slot = -1;
            while (hkey[h] != 0xffff) { if (hkey[h] == m) { slot = hval[h]; break; } h = (h + 1) & 511; }
            if (slot < 0) {                     /* first time this frame: last frame's slot? */
                u32 g = (m * 0x9E37u) >> 7 & 511;
                while (okey[g] != 0xffff) { if (okey[g] == m) { slot = oval[g]; break; } g = (g + 1) & 511; }
                if (slot >= 0 && !taken[slot]) { taken[slot] = 1; hkey[h] = m; hval[h] = slot; cram_build[slot] = m; ncol++; }
                else { slot = -1; pend_m[pend_n] = m; pend_ci[pend_n++] = ci; continue; }
            }
            if (lut[p][pen] != slot) { lut[p][pen] = slot; if (p >= 64) lut23ver++; else if (p >= 32) { pver[p - 32]++; s1epoch++; } else spdirty[p] = 1; }
        }
        lut[p][15] = 0;
    }
    u32 tb1 = wdt();
    /* new colours: free slots */
    int fs = 1;
    for (int k = 0; k < pend_n; k++) {
        u16 m = pend_m[k]; u32 ci = pend_ci[k]; int p = ci >> 4, pen = ci & 15;
        u32 h = (m * 0x9E37u) >> 7 & 511;
        int slot = -1;
        while (hkey[h] != 0xffff) { if (hkey[h] == m) { slot = hval[h]; break; } h = (h + 1) & 511; }
        if (slot < 0) {   /* V3.13: the pen's own last slot when nothing else took it: its scroll1 cells / sprite tables
                             stay valid (stage 3's rain: a palette whose colours change every frame missed the cell cache) */
            int os = lut[p][pen];
            if (os > 0 && !taken[os]) { slot = os; taken[os] = 1; cram_build[slot] = m; hkey[h] = m; hval[h] = slot; ncol++; goto have; }
            while (fs < 256 && taken[fs]) fs++;
            if (fs < 256) { slot = fs; taken[fs] = 1; cram_build[slot] = m; hkey[h] = m; hval[h] = slot; ncol++; }
            else { pal_exact = 1;
                int best = 1, bd = 1 << 30;
                for (int q = 1; q < 256; q++) {
                    int u = cram_build[q];
                    int dr = (int)(u & 31) - (m & 31), dg = (int)((u >> 5) & 31) - ((m >> 5) & 31), db = (int)((u >> 10) & 31) - ((m >> 10) & 31);
                    int d = dr * dr + dg * dg + db * db; if (d < bd) { bd = d; best = q; }
                }
                slot = best;
            }
        }
      have:
        if (lut[p][pen] != slot) { lut[p][pen] = slot; if (p >= 64) lut23ver++; else if (p >= 32) { pver[p - 32]++; s1epoch++; } else spdirty[p] = 1; }   /* V3.13: + s1epoch / lut23ver like the loop above (the row skip key) */
    }
    int next = ncol;
    u32 tb2 = wdt();
    for (int q = 1; q < 256; q++) if (!taken[q]) cram_build[q] = 0;
    for (int q = BGPRI_SLOT0; q < 255; q++) cram_build[q] = bgpri_cram[q - BGPRI_SLOT0];
    cram_build[255] = FPSON ? 0x7fff : 0x0001;
    for (int p = 0; p < 32; p++) {
        if (!spdirty[p]) continue;
        spdirty[p] = 0;
        const u8 *pl = lut[p];
        u16 *t = sp2[p];
        for (int h = 0; h < 16; h++) {
            u32 ch = pl[h];
            for (int l = 0; l < 16; l++) t[(h * 16 + l) ^ 0x80] = (ch << 8) | pl[l];   /* V2.8: rotated (t16.s: signed byte index from the middle) */
        }
    }
    dbg_colors = next;
    {   /* scroll1 pair tables */
        u32 want = 0;
        for (int i = 0; i < 32; i++) if (used[32 + i]) want |= bit32[i];
        u32 keep = 0; dbg_want = __builtin_popcount(want);
        for (int sl = 0; sl < 16; sl++) { int p = s1pt_pal[sl]; if (p < 32 && (want & bit32[p]) && s1slot[p] == sl) keep |= bit32[sl]; }
        for (int i = 0; i < 32; i++) if (!(want & bit32[i])) s1slot[i] = -1;
        for (int p = 0; p < 32; p++) {
            if (!(want & bit32[p])) continue;
            int sl = s1slot[p];
            if (sl < 0 || !(keep & bit32[sl])) {
                sl = -1;
                for (int q = 0; q < 16; q++) if (!(keep & bit32[q])) { sl = q; break; }
                if (sl < 0) { s1slot[p] = -1; continue; }
                keep |= bit32[sl]; s1slot[p] = sl; s1pt_pal[sl] = p; s1pt_ver[sl] = pver[p] - 1;
            }
            if (s1pt_ver[sl] != pver[p]) {
                s1pt_ver[sl] = pver[p];
                const u8 *pl = lut[32 + p]; u16 *t = s1pt[sl];
                for (int h = 0; h < 16; h++) { u32 ch = pl[h] << 8; for (int l = 0; l < 16; l++) t[h * 16 + l] = ch | pl[l]; }
            }
        }
    }
    u32 tb3 = wdt(); ptm[4] += (tb3 - tb0) & 255; ptm[5]++; pt[0] += (tb1 - tb0) & 255; pt[1] += (tb2 - tb1) & 255; pt[2] += (tb3 - tb2) & 255;
}

/* ---------------- background priority cells ----------------
 * The MD planes are behind the whole 32X picture, so stage map cells with the priority bit (the parts
 * of the scenery in front of the characters: tree trunks, logs, ...) are drawn again by the 32X, over
 * the sprites, wherever a sprite is.  The MD's data is on the CD (Word RAM, which the SH-2 cannot
 * read): the cart has a copy of just these cells (tools/mkbgpri.py, 0x3A0000), colours in fixed CRAM
 * slots 224..254 (MD palette lines 0 and 1). */
#define BGPRI_TAB ((const u32 *)0x023C0000)      /* u32 cart address[32] per stage (0: none) */
static const u8 *bgpri_p;                          /* u16 w, nrows; u8 rowidx[128]; u16 pal[32]; u16 map[w][nrows]; tiles */
static int bgpri_stage = -1;
#define BGPRI bgpri_p
#define BGP_W     (((const u16 *)BGPRI)[0])
#define BGP_ROW   (BGPRI + 4)                      /* u8 rowflag[128] */
#define BG_PAL16  ((const u16 *)(BGPRI + 132))
#define BGP_COL   ((const u16 *)(BGPRI + 200))     /* u16 colidx[w + 1] */
#define BGP_CELLS ((const u16 *)(BGPRI + ((200 + (BGP_W + 1) * 2 + 3) & ~3)))   /* {u16 row, u16 entry} */
#define BG_TILES32 ((const u32 *)(BGPRI + (*(const u32 *)(BGPRI + 196) & 0xFFFFFF)))
static u16 bgpt[256];                          /* pen pair -> 2 slots of line 0 (V3.18: line L = +16*L on the nonzero ones) */
static u32 cov[28][2]; static u32 covprev[28][2] RD1, s1prev, pclr;
static u8 rowpri[128];                         /* map A rows that have priority cells */                         /* screen 8x8 cells covered by sprites */
UI static void bgpri_init(int st)
{
    bgpri_stage = st;
    u32 o = BGPRI_TAB[st & 31];
    bgpri_p = o ? (const u8 *)(0x02000000 + o) : 0;
    if (!bgpri_p) { for (int y = 0; y < 128; y++) rowpri[y] = 0; return; }
    for (int y = 0; y < 128; y++) rowpri[y] = BGP_ROW[y] != 0;
    /* V3.18: 2 or 3 palette lines (stages 5-8: the colours of their cells did not fit 2 lines of 15: ~200-350 cells
       were drawn with substitutes, the "colour change" when something passes behind them) */
    int nl = BGPRI[196] == 3 ? 3 : 2; u32 s0 = nl == 3 ? 208 : 224;
    if (s0 != bgpri_slot0) { bgpri_slot0 = s0; pal_exact = 1; }   /* slots taken from / given back to the sprites: remap */
    const u16 *pl2 = (const u16 *)((const u8 *)BG_TILES32 - 32);
    static const u8 l5[8] = { 0, 6, 10, 14, 17, 21, 25, 29 };   /* V3.13: the MD levels as PicoDrive shows them (its non-linear MD output, like the real DAC; V3.11 had 0,4,8,12): the same colour as the MD plane under it */
    for (int q = 0; q < 47; q++) bgpri_cram[q] = 0;
    for (int line = 0; line < nl; line++)
        for (int pen = 1; pen < 16; pen++) {
            u16 c = line < 2 ? BG_PAL16[line * 16 + pen] : pl2[pen];
            u16 m = (l5[(c >> 9) & 7] << 10) | (l5[(c >> 5) & 7] << 5) | l5[(c >> 1) & 7];
            if (s0 + line * 16 + pen - 1 < 255) bgpri_cram[line * 16 + pen - 1] = m;
        }
#ifdef BGPRI_COLOUR
    for (int h = 0; h < 16; h++) for (int l = 0; l < 16; l++) {   /* (slots >= 208: bit 7 marks a pen, see draw_bgpri) */
        u32 sh = h ? s0 + h - 1 : 0, sl = l ? s0 + l - 1 : 0;
        bgpt[h * 16 + l] = (sh << 8) | sl;
    }
#else
    for (int h = 0; h < 16; h++) for (int l = 0; l < 16; l++) bgpt[h * 16 + l] = (h ? 0 : 0xFF00) | (l ? 0 : 0xFF);   /* V3.22: pen pair -> keep mask */
#endif
}
static void sprite_cover(void)
{
    for (int r = 0; r < 28; r++) cov[r][0] = cov[r][1] = 0;
    if (!P->bgon) return;
    for (int i = (int)nspr - 1; i >= 0; i--) {
        const u16 *e = &spr[i * 4];
        int x = e[0], y = e[1]; u32 colour = e[3];
        if (sprite_code(e[2]) < 0) continue;
        int nx = 1, ny = 1;
        if (colour & 0xff00) { nx = ((colour >> 8) & 15) + 1; ny = ((colour >> 12) & 15) + 1; }
        for (int a = 0; a < ny; a++) {
            int sy = ((y + a * 16) & 0x1ff) - 16;
            if (sy + 16 <= 0 || sy >= 176) continue;
            int r0 = sy < 0 ? 0 : sy >> 3, r1 = (sy + 15) >> 3;
            if (r1 > 27) r1 = 27;
            for (int b = 0; b < nx; b++) {
                int sx = ((x + b * 16) & 0x1ff) - 96;
                if (sx + 16 <= 0 || sx >= SCR_W) continue;
                int c0 = sx < 0 ? 0 : sx >> 3, c1 = (sx + 15) >> 3;
                if (c1 > 39) c1 = 39;
                /* V2.7: the 2-3 columns as one mask per word, then OR-ed into each row */
                u32 m0 = 0, m1 = 0;
                for (int c = c0; c <= c1; c++) { if (c < 32) m0 |= bit32[c]; else m1 |= bit32[c & 31]; }
                for (int r = r0; r <= r1; r++) { cov[r][0] |= m0; cov[r][1] |= m1; }
            }
        }
    }
}
static inline int covered(int sx, int sy)          /* any sprite in the 8x8 area at (sx, sy)? */
{
    int c0 = sx < 0 ? 0 : sx >> 3, c1 = (sx + 7) >> 3, r0 = sy < 0 ? 0 : sy >> 3, r1 = (sy + 7) >> 3;
    if (c1 > 39) c1 = 39;
    if (r1 > 27) r1 = 27;
    for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) if (cov[r][c >> 5] & bit32[c & 31]) return 1;
    return 0;
}
#ifdef PADTEST
#define PADS_(x) #x
#define PADS(x) PADS_(x)
void pad_test(volatile u32 *p) { __asm__ volatile(".rept " PADS(PADTEST) "\n nop\n .endr"); p[0] = 1; }
#endif
static void draw_bgpri(u8 *fb, int y0, int y1)
{
    if (!P->bgon || !bgpri_p || bgpri_stage != P->stage) return;
    if (y1 > 176) y1 = 176;
    if (y0 >= y1) return;
    u32 w = BGP_W; const u16 *ci = BGP_COL; const u16 *cells = BGP_CELLS;
    int ax = P->cam[0] + 96, ay = 272 - P->cam[1];
    int mx0 = ax >> 3, my0 = (ay + y0) >> 3, my1 = (ay + y1 - 1) >> 3;
    u8 *ov = (u8 *)&MARS_OVERWRITE_IMG;
    u32 rowsok = 0;                                 /* rows my0.. with a sprite in them (bit per row) */
    for (int my = my0; my <= my1 && my - my0 < 32; my++) {
        if ((u32)my >= 128 || !rowpri[my]) continue;
        int sy = my * 8 - ay;
#ifndef PRIALL
        if (!(cov[(sy < 0 ? 0 : sy) >> 3][0] | cov[(sy < 0 ? 0 : sy) >> 3][1] | (sy + 7 < 224 ? cov[((sy + 7) >> 3) > 27 ? 27 : (sy + 7) >> 3][0] | cov[((sy + 7) >> 3) > 27 ? 27 : (sy + 7) >> 3][1] : 0))) continue;
#endif
        rowsok |= bit32[my - my0];
    }
    if (!rowsok) return;
#ifdef DBGPRI
    { volatile u32 *D = (volatile u32 *)0x26007200; D[0] = ax; D[1] = ay; D[2] = mx0; D[3] = my0; D[4] = my1; D[5] = w; D[6] = rowsok; D[7] = P->stage; D[8] = (u32)bgpri_p; D[9] = y0; D[10] = y1; }
#endif
    /* V3.22: the screen columns with a sprite in this band's rows: map columns without one are not looked at, and a
       column's cells (cart, sorted by row) are entered by binary search: the stage 8 bamboo forest (cells top to bottom)
       took ~20 of the 47 ticks a frame, most of it walking the lists from the top once per band */
    u32 cm0 = 0, cm1 = 0;
    { int r0 = y0 >> 3, r1 = (y1 - 1) >> 3; if (r1 > 27) r1 = 27; for (int r = r0; r <= r1; r++) { cm0 |= cov[r][0]; cm1 |= cov[r][1]; } }
#ifdef PRIALL
    cm0 = cm1 = 0xFFFFFFFFu;
#endif
    for (int mx = mx0; mx <= mx0 + 40; mx++) {
        if ((u32)mx >= w) continue;
        { int sx = mx * 8 - ax, c = sx < 0 ? 0 : sx >> 3, c2 = (sx + 7) >> 3; if (c2 > 39) c2 = 39;
#ifndef NOCOLMASK
          if (!((c < 32 ? cm0 & bit32[c] : cm1 & bit32[c & 31]) | (c2 < 32 ? cm0 & bit32[c2] : cm1 & bit32[c2 & 31]))) continue;
#endif
        }
        u32 k = ci[mx], ke = ci[mx + 1];
#ifndef BSMIN
#define BSMIN 12
#endif
        if (ke - k > BSMIN) { u32 lo = k, hi = ke; while (lo < hi) { u32 md = (lo + hi) >> 1; if ((int)cells[md * 2] < my0) lo = md + 1; else hi = md; } k = lo; }   /* (long lists) */
        for (; k < ke; k++) {
            int my = cells[k * 2];
            if (my > my1) break;
            if (!(rowsok & bit32[(my - my0) & 31])) continue;
            u16 e = cells[k * 2 + 1];
            int sy = my * 8 - ay;
            int sx = mx * 8 - ax;
            if (sx <= -8 || sx >= SCR_W) continue;
            if (d2_over(mx >> 1, my >> 1)) continue;
#ifndef PRIALL
            if (!covered(sx, sy)) continue;
#endif
            const u32 *t = BG_TILES32 + (((e & 0xFFF) - 1) << 3);
            const u16 *T = bgpt; u32 lad = (((e >> 14) & 1) | ((e >> 12) & 2)) * 16;   /* V3.18: line bit 1 in bit 13 (the cells are stored flipped) */
            const int hf = 0, vf = 0;
            int ra = sy < y0 ? y0 - sy : 0, rb = sy + 8 > y1 ? y1 - sy : 8;
            for (int r = ra; r < rb; r++) {
                u32 a = t[vf ? 7 - r : r];
                if (!a) continue;
                if (hf) { a = ((a & 0x0f0f0f0f) << 4) | ((a >> 4) & 0x0f0f0f0f); a = (a << 24) | ((a & 0xff00) << 8) | ((a >> 8) & 0xff00) | (a >> 24); }
#ifdef BGPRI_COLOUR
                u32 o0 = ((u32)T[a >> 24] << 16) | T[(a >> 16) & 255], o1 = ((u32)T[(a >> 8) & 255] << 16) | T[a & 255];
                o0 += ((o0 >> 7) & 0x01010101u) * lad; o1 += ((o1 >> 7) & 0x01010101u) * lad;   /* the line's slots (pens: slot >= 208) */
                u8 *d = ov + FB_LINE(sy + r) + sx;
                if (sx >= 0 && sx + 8 <= SCR_W && !(sx & 1)) {
                    volatile u16 *q = (volatile u16 *)d;
                    if (o0 >> 16) q[0] = o0 >> 16;
                    if (o0 & 0xffff) q[1] = o0;
                    if (o1 >> 16) q[2] = o1 >> 16;
                    if (o1 & 0xffff) q[3] = o1;
                } else {
                    u8 px[8] = { o0 >> 24, o0 >> 16, o0 >> 8, o0, o1 >> 24, o1 >> 16, o1 >> 8, o1 };
                    u8 *f = fb + FB_LINE(sy + r) + sx;
                    for (int k = 0; k < 8; k++) if (px[k] && sx + k >= 0 && sx + k < SCR_W) f[k] = px[k];
                }
#else
                /* V3.21: the occluding pixels are not painted: they are cleared (pixel 0 = the MD plane shows through, and
                   the MD plane has the very same picture), so the colour is the MD's own on any machine (a painted 32X pixel
                   could only be near: 5 bits per gun, and PicoDrive adds the priority bit to green) */
                u32 m0 = ((u32)T[a >> 24] << 16) | T[(a >> 16) & 255], m1 = ((u32)T[(a >> 8) & 255] << 16) | T[a & 255];
                (void)lad;
                u8 *f = fb + FB_LINE(sy + r) + sx;
                /* (16-bit read-modify-write: the frame buffer drops byte writes of 0) */
#ifdef NORMW
                { static volatile u32 sink; if (m0 != 0x12345678) { sink = m0 ^ m1; continue; } }
#endif
                if (sx >= 0 && sx + 8 <= SCR_W && !(sx & 1)) {
                    volatile u16 *q = (volatile u16 *)f;
                    /* (V3.22: a halfword the cell covers whole is just written: frame buffer reads are slow) */
                    if (!(m0 | m1)) { q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 0; continue; }
                    { u16 h = m0 >> 16; if (!h) q[0] = 0; else if (h != 0xffff) q[0] = q[0] & h; }
                    { u16 h = m0; if (!h) q[1] = 0; else if (h != 0xffff) q[1] = q[1] & h; }
                    { u16 h = m1 >> 16; if (!h) q[2] = 0; else if (h != 0xffff) q[2] = q[2] & h; }
                    { u16 h = m1; if (!h) q[3] = 0; else if (h != 0xffff) q[3] = q[3] & h; }
                } else {
                    u8 px[8] = { m0 >> 24, m0 >> 16, m0 >> 8, m0, m1 >> 24, m1 >> 16, m1 >> 8, m1 };
                    for (int x = sx & ~1; x < sx + 8; x += 2) {
                        if (x < 0 || x >= SCR_W) continue;
                        int k = x - sx; u32 m = ((k >= 0 ? px[k] : 0xff) << 8) | (k + 1 < 8 ? px[k + 1] : 0xff);
                        if (m != 0xffff) { volatile u16 *q = (volatile u16 *)(fb + FB_LINE(sy + r) + x); *q = *q & m; }
                    }
                }
#endif
            }
        }
    }
}

/* ---------------- scroll2 / scroll3 on the 32X (title, story, menus, continue, game over) ----------------
 * Outside the stage the backgrounds are pictures that the MD planes do not have; the 32X draws them
 * like the arcade (layer order from the CPS-B layer control), from the tiles in the cart (S2/S3 tables). */
#define S2_TAB ((const u32 *)0x0201D000)
#define S3_TAB ((const u32 *)0x02025000)
static inline int layer23_on(int L) { return L == 2 ? ((layerctl & 4) && (P->vc & 4)) : ((layerctl & 8) && (P->vc & 8)); }
static inline u32 tm_index(int L, u32 row, u32 col)
{
    return L == 2 ? (row & 0x0f) + ((col & 0x3f) << 4) + ((row & 0x30) << 6) : (row & 0x07) + ((col & 0x3f) << 3) + ((row & 0x38) << 6);
}
static inline const u8 *tile23(int L, u32 code)
{
#ifndef RAW23
    return 0;
#endif
    if (L == 2) { u32 c2 = code << 1; if (c2 < 0x4000 || c2 > 0x7fff) return 0; return (const u8 *)S2_TAB[code & 0x1fff]; }
    code &= 0x3fff; if (code > 0x7ff) return 0; return (const u8 *)S3_TAB[code];
}
static void scan23(u8 *used)
{
    for (int L = 2; L <= 3; L++) {
        if (!layer23_on(L)) continue;
        int ts = L == 2 ? 16 : 32, sh = L == 2 ? 4 : 5;
        const u8 *base = (const u8 *)(L == 2 ? P->b2 : P->b3);
        u32 scx = P->sc23[(L - 2) * 2], scy = P->sc23[(L - 2) * 2 + 1];
        u32 c0 = ((64 + scx) >> sh) & 63, r0 = ((16 + scy) >> sh) & 63;
        u32 nc = 384 / ts + 2, nr = 224 / ts + 2;
        for (u32 r = 0; r < nr; r++) for (u32 c = 0; c < nc; c++) {
            u32 idx = tm_index(L, (r0 + r) & 63, (c0 + c) & 63);
            const u16 *e = (const u16 *)(base + idx * 4);
            u32 code = e[0], ok = L == 2 ? ((code << 1) >= 0x4000 && (code << 1) <= 0x7fff) : ((code & 0x3fff) <= 0x7ff);
            if (ok) used[(L == 2 ? 64 : 96) + (e[1] & 31)] = 1;
        }
    }
}
volatile u32 dbg23[4];
#ifndef DBGW
#define DBGW 0
#endif
static void draw_tilemap23(u8 *fb, int L, int y0, int y1)
{
    dbg23[0]++;
    if (!layer23_on(L)) return;
    dbg23[1]++;
    int ts = L == 2 ? 16 : 32, sh = L == 2 ? 4 : 5, rb = ts / 2;      /* row bytes */
    const u8 *base = (const u8 *)(L == 2 ? P->b2 : P->b3);
    u32 scx = P->sc23[(L - 2) * 2], scy = P->sc23[(L - 2) * 2 + 1];
    int pb = L == 2 ? 64 : 96;
    u32 msk = ts * 64 - 1;                                  /* 64 tiles: 1024 / 2048 pixels */
    for (int y = y0; y < y1; y++) {
        u32 ty = (y + 16 + scy) & msk, row = ty >> sh, py = ty & (ts - 1);
        u8 *d = fb + FB_LINE(y);
        int x = 0;
        u32 tx = (96 + scx) & msk;
        while (x < SCR_W) {
            u32 col = tx >> sh, px = tx & (ts - 1);
            int run = ts - px; if (run > SCR_W - x) run = SCR_W - x;
            u32 idx = tm_index(L, row, col);
            const u16 *e = (const u16 *)(base + idx * 4);
            const u8 *t = tile23(L, e[0]);
            if (t) {
                dbg23[2]++;
                u32 attr = e[1];
                const u8 *pl = lut[pb + (attr & 31)];
                const u8 *src = t + ((attr & 0x40) ? ts - 1 - py : py) * rb;
                if (!(attr & 0x20)) {
                    for (int k = 0; k < run; k++) {
                        u32 q = px + k, n = (q & 1) ? src[q >> 1] & 15 : src[q >> 1] >> 4;
                        if (n != 15) d[x + k] = DBGW ? 255 : pl[n];
                    }
                } else {
                    for (int k = 0; k < run; k++) {
                        u32 q = ts - 1 - (px + k), n = (q & 1) ? src[q >> 1] & 15 : src[q >> 1] >> 4;
                        if (n != 15) d[x + k] = pl[n];
                    }
                }
            }
            x += run; tx = (tx + run) & msk;
        }
    }
}

/* ---------------- drawing ---------------- */
/* sprite table entry: r0 << 28 | r1 << 24 | address: only source rows r0..r1 are stored (tools/mkgfx.py) */
#define SPR_R0(a) ((a) >> 28)
#define SPR_R1(a) (((a) >> 24) & 15)
#define SPR_PTR(a) ((const u8 *)(0x02000000 | ((a) & 0xFFFFFF)))
typedef void t16_t(u8 *base, const u8 *src, int n, const u16 *T, int sstep);
extern t16_t t16_0, t16_1, t16_2, t16_3, t16_4, t16_5, t16_6, t16_7;
static t16_t *const t16_fn[8] = { t16_0, t16_1, t16_2, t16_3, t16_4, t16_5, t16_6, t16_7 };
#ifdef T16CHK   /* test: one row by t16.s, checked against the pixels it should give */
UI2 static void t16_chk(u8 *ov, u8 *d, const u8 *s, int fx, int sx, const u8 *pl, const u16 *T)
{
    u8 old[24], want[24]; volatile u32 *K = (volatile u32 *)0x26007000;
    for (int i = 0; i < 22; i++) old[i] = want[i] = d[i - 3];
    for (int x = 0; x < 16; x++) { int q = fx ? 15 - x : x; u32 v = s[q >> 1], n = q & 1 ? v & 15 : v >> 4; if (n != 15 && pl[n]) want[x + 3] = pl[n]; }
    if ((((const u32 *)s)[0] & ((const u32 *)s)[1]) == 0xffffffffu) { /* skipped row */ }
    t16_fn[(fx << 2) | (sx & 3)](ov - (sx & 3), s, 1, T, 8);
    K[16]++; for (int i = 0; i < 22; i++) if (d[i - 3] != want[i]) { K[17]++; K[18] = (fx << 8) | (sx & 3); K[19] = (i << 16) | (d[i - 3] << 8) | want[i]; break; }
}
#endif
typedef void t16d_t(u8 *base, const u8 *src, int n, const u16 *T, int sstep, u32 mask);
extern t16d_t t16d_0, t16d_1, t16d_2, t16d_3, t16d_4, t16d_5, t16d_6, t16d_7;
static t16d_t *const t16d_fn[8] = { t16d_0, t16d_1, t16d_2, t16d_3, t16d_4, t16d_5, t16d_6, t16d_7 };
static void draw_tile16(u8 *fb, u32 ent, const u8 *pl, int pal, int fx, int fy, int sx, int sy, int y0, int y1, int dith)
{
    int ya = sy < y0 ? y0 - sy : 0, yb = sy + 16 > y1 ? y1 - sy : 16;
    int r0 = SPR_R0(ent), r1 = SPR_R1(ent);
    { int lo = fy ? 15 - r1 : r0, hi = fy ? 16 - r0 : r1 + 1; if (ya < lo) ya = lo; if (yb > hi) yb = hi; }
    if (ya >= yb) return;
    const u8 *src = SPR_PTR(ent) - r0 * 8;           /* row r of the full tile at src + r * 8 */
    int xa = sx < 0 ? -sx : 0, xb = sx + 16 > SCR_W ? SCR_W - sx : 16;
    if (xa >= xb) return;
    if (xa == 0 && xb == 16) {                      /* V2.8: hand-written SH-2 (t16.s, tools/mkt16.py) */
        u8 *ov = (u8 *)&MARS_OVERWRITE_IMG + FB_LINE(sy + ya) + sx;
#ifdef T16CHK
        for (int r = ya; r < yb; r++) t16_chk(ov + (r - ya) * SCR_W, (u8 *)fb + FB_LINE(sy + r) + sx, src + (fy ? 15 - r : r) * 8, fx, sx, pl, sp2[pal] + 128);
#else
        if (dith) t16d_fn[(fx << 2) | (sx & 3)](ov - (sx & 3), src + (fy ? 15 - ya : ya) * 8, yb - ya, sp2[pal] + 128, fy ? -8 : 8,
                                                ((sy + ya + dith) & 1) ? 0x00FF00FFu : 0xFF00FF00u);   /* V3.1: half the flame pixels */
        else t16_fn[(fx << 2) | (sx & 3)](ov - (sx & 3), src + (fy ? 15 - ya : ya) * 8, yb - ya, sp2[pal] + 128, fy ? -8 : 8);
#endif
        return;
    }
    for (int r = ya; r < yb; r++) {
        const u32 *s = (const u32 *)(src + (fy ? 15 - r : r) * 8);
        u32 a = s[0], b = s[1];
        if ((a & b) == 0xffffffffu) continue;
        u8 *d = fb + FB_LINE(sy + r) + sx;
        const u8 *p8 = (const u8 *)s;                /* V2.7: pixel q = nibble q (high first), no variable shift */
        if (!dith) {        /* V3.13: the common case without the dither / flip tests per pixel */
            if (!fx) for (int x = xa; x < xb; x++) { u32 v = p8[x >> 1], n = x & 1 ? v & 15 : v >> 4; if (n != 15) d[x] = pl[n]; }
            else for (int x = xa; x < xb; x++) { int q = 15 - x; u32 v = p8[q >> 1], n = q & 1 ? v & 15 : v >> 4; if (n != 15) d[x] = pl[n]; }
            continue;
        }
        int xs = dith ? ((sx + sy + r + dith) & 1) : 0;   /* (dith: skip the pixels with x + y + phase odd) */
        for (int x = xa; x < xb; x++) {
            if (dith && ((x + xs) & 1)) continue;
            int q = fx ? 15 - x : x;
            u32 v = p8[q >> 1], n = q & 1 ? v & 15 : v >> 4;
            if (n != 15) d[x] = pl[n];
        }
    }
}
static int spr_dx;                              /* extra x offset (scaled scenes' layouts) */
static void draw_sprites(u8 *fb, int y0, int y1)
{
#ifdef NOSPR
    return;
#endif
    for (int i = (int)nspr - 1; i >= 0; i--) {
        const u16 *e = &spr[i * 4];
        int x = e[0], y = e[1]; u32 colour = e[3];
        {   /* V2.7: off this band in y: skipped before decoding (the master draws in 16-line pieces) */
            int ny0 = colour & 0xff00 ? ((colour >> 12) & 15) + 1 : 1, sy0 = (y & 0x1ff) - 16, h = ny0 * 16;
            if (sy0 + h <= 496 && (sy0 >= y1 || sy0 + h <= y0)) continue;
        }
        int code = sprite_code(e[2]);
        if (code < 0) continue;
        int fx = (colour >> 5) & 1, fy = (colour >> 6) & 1;
        const u8 *pl = lut[colour & 31];
#ifdef FLAMEDITH
        int dith = flame(e) ? 1 + (P->seq & 1) : 0;    /* V3.1: the flames (the arcade shows each flame row every other frame) */
#else
        int dith = 0;
#endif
        int nx = 1, ny = 1;
        if (colour & 0xff00) { nx = ((colour >> 8) & 15) + 1; ny = ((colour >> 12) & 15) + 1; }
        for (int nys = 0; nys < ny; nys++) {
            int sy = ((y + nys * 16) & 0x1ff) - 16;
            if (sy + 16 <= y0 || sy >= y1) continue;
            for (int nxs = 0; nxs < nx; nxs++) {
                int sx = ((x + nxs * 16) & 0x1ff) - 96 + spr_dx;
                if (sx + 16 <= 0 || sx >= SCR_W) continue;
                int t;
                if (colour & 0xff00) {
                    int cx = fx ? ((code + (nx - 1) - nxs) & 0xf) : ((code + nxs) & 0xf);
                    int cy = fy ? 0x10 * (ny - 1 - nys) : 0x10 * nys;
                    t = (code & ~0xf) + cx + cy;
                } else t = code;
                if (t >= 12288) continue;
                u32 a = SPR_TAB[t];
                if (!a) continue;
                CT(&ntiles)[0]++;
                draw_tile16(fb, a, pl, colour & 31, fx, fy, sx, sy, y0, y1, dith);
            }
        }
    }
}
/* scroll1 cell cache (one per CPU): rows already mapped to CRAM slots (0 = transparent) */
#ifndef S1C_N
#define S1C_N 148
#endif
typedef struct { u32 key, ver; u32 px[16]; } s1c_t;
static s1c_t s1c_m[S1C_N] RD2, s1c_s[S1C_N] RD2;
static inline const u32 *s1c_get(s1c_t *tab, u32 k, u32 code, u32 gs, u32 attr)
{
    u32 pal = attr & 31, fl = (attr >> 5) & 3;
    u32 key = (code << 8) | (gs << 7) | (fl << 5) | pal;
    u32 hh = (key * 0x9E3779B1u) >> 16;
    s1c_t *e = &tab[(hh * S1C_N) >> 16];
    u32 ver = pver[pal];
    if (e->key == key && e->ver == ver) return e->px;
    CT(&dbg_s1miss)[0]++;
    e->key = key; e->ver = ver;
    const u8 *pl = lut[32 + pal];
    const u8 *src = (const u8 *)(k < 0x4000 ? S1_TAB[k] : S1X_TAB[k - 0x4000]);
    int sl = s1slot[pal];
    if (sl >= 0) {
        const u16 *T = s1pt[sl];
        for (int r = 0; r < 8; r++) {
            u32 a = *(const u32 *)(src + ((fl & 2) ? 7 - r : r) * 4);
            u32 o0 = ((u32)T[a >> 24] << 16) | T[(a >> 16) & 255], o1 = ((u32)T[(a >> 8) & 255] << 16) | T[a & 255];
            if (fl & 1) {   /* mirror 8 bytes */
                u32 t0 = o0, t1 = o1;
                o0 = (t1 << 24) | ((t1 & 0xff00) << 8) | ((t1 >> 8) & 0xff00) | (t1 >> 24);
                o1 = (t0 << 24) | ((t0 & 0xff00) << 8) | ((t0 >> 8) & 0xff00) | (t0 >> 24);
            }
            e->px[r * 2] = o0; e->px[r * 2 + 1] = o1;
        }
        return e->px;
    }
    for (int r = 0; r < 8; r++) {
        u32 a = *(const u32 *)(src + ((fl & 2) ? 7 - r : r) * 4);
        u32 p0 = pl[a >> 28], p1 = pl[(a >> 24) & 15], p2 = pl[(a >> 20) & 15], p3 = pl[(a >> 16) & 15];
        u32 p4 = pl[(a >> 12) & 15], p5 = pl[(a >> 8) & 15], p6 = pl[(a >> 4) & 15], p7 = pl[a & 15];
        u32 o0, o1;
        if (fl & 1) { o0 = (p7 << 24) | (p6 << 16) | (p5 << 8) | p4; o1 = (p3 << 24) | (p2 << 16) | (p1 << 8) | p0; }
        else        { o0 = (p0 << 24) | (p1 << 16) | (p2 << 8) | p3; o1 = (p4 << 24) | (p5 << 16) | (p6 << 8) | p7; }
        e->px[r * 2] = o0; e->px[r * 2 + 1] = o1;
    }
    return e->px;
}
static u32 s1over[2];                          /* HUD, per CPU: code drawn instead of the map's (0 none, 0xFFFF nothing) */
static inline const u32 *s1lookup(s1c_t *tab, u32 row, u32 col)
{
    u32 idx = (row & 0x1f) + ((col & 63) << 5) + ((row & 0x20) << 6);
    u32 v = *(const u32 *)(s1base + idx * 4); u32 code = v >> 16;
    { u32 o = s1over[tab != s1c_m]; if (__builtin_expect(o != 0, 0)) { if (o == 0xFFFF) return 0; code = o & 0xFFFF; if (o >> 31) v = (v & ~31u) | ((o >> 16) & 31); } }
    u32 k = ((code - 0x6000) << 1) | ((idx >> 5) & 1);
    if (k >= 0x4000) { if (k >= 0x8000 || !S1X_TAB[k - 0x4000]) return 0; }   /* Chinese glyphs */
    else if (!(s1blank[k >> 3] & bit8[k & 7])) return 0;
    return s1c_get(tab, k, code, k & 1, v & 0xffff);
}
/* (V3.13) one 8-pixel row r of a scroll1 cell at d0 (+ r lines): x & 3 = 0 / 2 / odd */
#define R0(r) { u32 a = px[2 * r], b = px[2 * r + 1]; if (a | b) { volatile u32 *d = (volatile u32 *)(d0 + r * SCR_W); d[0] = a; d[1] = b; } }
#define R2(r) { u32 a = px[2 * r], b = px[2 * r + 1]; if (a | b) { volatile u16 *d = (volatile u16 *)(d0 + r * SCR_W); if (a) { d[0] = a >> 16; d[1] = a; } if (b) { d[2] = b >> 16; d[3] = b; } } }
#define R1(r) { u32 a = px[2 * r], b = px[2 * r + 1]; if (a | b) { volatile u16 *d = (volatile u16 *)(d0 + r * SCR_W - 1); d[0] = a >> 24; d[1] = a >> 8; d[2] = (a << 8) | (b >> 24); d[3] = b >> 8; d[4] = b << 8; } }
static void s1draw(s1c_t *tab, u8 *fb, u32 row, u32 col, int xs, int ys, int y0, int y1)
{
    int ra = ys < y0 ? y0 - ys : 0, rb = ys + 8 > y1 ? y1 - ys : 8;
    int xa = xs < 0 ? -xs : 0, xb = xs + 8 > SCR_W ? SCR_W - xs : 8;
    if (xa >= xb) return;
    const u32 *px = s1lookup(tab, row, col);
    if (!px) return;
    if (xa == 0 && xb == 8) {
        u8 *ov = (u8 *)&MARS_OVERWRITE_IMG;
        if (ra == 0 && rb == 8) {   /* V3.15: whole cell unrolled (the HUD, the edges, cell mode) */
            u8 *d0 = ov + FB_LINE(ys) + xs;
            if (!(xs & 3)) { R0(0) R0(1) R0(2) R0(3) R0(4) R0(5) R0(6) R0(7) }
            else if (!(xs & 1)) { R2(0) R2(1) R2(2) R2(3) R2(4) R2(5) R2(6) R2(7) }
            else { R1(0) R1(1) R1(2) R1(3) R1(4) R1(5) R1(6) R1(7) }
            return;
        }
        if (!(xs & 3)) {
            for (int r = ra; r < rb; r++) {
                u32 a = px[r * 2], b = px[r * 2 + 1];
                if (!(a | b)) continue;
                volatile u32 *d = (volatile u32 *)(ov + FB_LINE(ys + r) + xs);
                d[0] = a; d[1] = b;
            }
        } else if (!(xs & 1)) {
            for (int r = ra; r < rb; r++) {
                u32 a = px[r * 2], b = px[r * 2 + 1];
                if (!(a | b)) continue;
                volatile u16 *d = (volatile u16 *)(ov + FB_LINE(ys + r) + xs);
                if (a) { d[0] = a >> 16; d[1] = a; }
                if (b) { d[2] = b >> 16; d[3] = b; }
            }
        } else {
            for (int r = ra; r < rb; r++) {
                u32 a = px[r * 2], b = px[r * 2 + 1];
                if (!(a | b)) continue;
                volatile u16 *d = (volatile u16 *)(ov + FB_LINE(ys + r) + xs - 1);
                d[0] = a >> 24; d[1] = a >> 8; d[2] = (a << 8) | (b >> 24); d[3] = b >> 8; d[4] = b << 8;
            }
        }
        return;
    }
    for (int r = ra; r < rb; r++) {
        const u8 *p8 = (const u8 *)&px[r * 2];      /* (big-endian: pixel x is byte x; V2.7: no shift by a variable,
                                                       a libgcc loop on the SH-2) */
        u8 *d = fb + FB_LINE(ys + r) + xs;
        for (int x = xa; x < xb; x++) {
            u32 c = p8[x];
            if (c) d[x] = c;
        }
    }
}
/* V3.13: a whole scroll1 row (the stage's normal path): one call per row instead of per cell, the alignment path
   chosen once (every cell of the row has the same x & 3), blank cells skipped without a call (stage 3's rain is
   scroll1 over the whole field: the 32X frame rate fell to ~32/s) */
static void s1row(s1c_t *tab, u8 *fb, u32 row, u32 c0, int ox, int ys, int y0, int y1)
{
    if (s1over[tab != s1c_m]) { for (int c = 0; c < 41; c++) s1draw(tab, fb, row, c0 + c, c * 8 - ox, ys, y0, y1); return; }
    int ra = ys < y0 ? y0 - ys : 0, rb = ys + 8 > y1 ? y1 - ys : 8;
    if (ra >= rb) return;
    int cs = 0, ce = 40;
    if (ox) { s1draw(tab, fb, row, c0, -ox, ys, y0, y1); s1draw(tab, fb, row, c0 + 40, 320 - ox, ys, y0, y1); cs = 1; }
    u32 rbase = (row & 0x1f) + ((row & 0x20) << 6);
    u8 *ov = (u8 *)&MARS_OVERWRITE_IMG + FB_LINE(ys + ra) - ox;
    const u32 *mw = (const u32 *)s1base;
    int al = (-ox) & 3, n = rb - ra;
    for (int c = cs; c < ce; c++) {
        u32 col = (c0 + c) & 63, idx = rbase + (col << 5);
        u32 v = mw[idx]; u32 code = v >> 16;
        u32 k = ((code - 0x6000) << 1) | (col & 1);
        if (k >= 0x4000) { if (k >= 0x8000 || !S1X_TAB[k - 0x4000]) continue; }
        else if (!(s1blank[k >> 3] & bit8[k & 7])) continue;
        const u32 *px;
        {   /* the cache hit inline (s1c_get on a miss) */
            u32 pal = v & 31, key = (code << 8) | ((k & 1) << 7) | (v & 0x7f);
            s1c_t *e = &tab[((((key * 0x9E3779B1u) >> 16) * S1C_N) >> 16)];
            px = (e->key == key && e->ver == pver[pal]) ? e->px : s1c_get(tab, k, code, k & 1, v & 0xffff);
            px += ra * 2;
        }
        u8 *d0 = ov + c * 8;
        if (n == 8) {       /* whole cell: unrolled (most cells; stage 3's rain rows are mostly sparse) */
            if (al == 0) { R0(0) R0(1) R0(2) R0(3) R0(4) R0(5) R0(6) R0(7) }
            else if (al == 2) { R2(0) R2(1) R2(2) R2(3) R2(4) R2(5) R2(6) R2(7) }
            else { R1(0) R1(1) R1(2) R1(3) R1(4) R1(5) R1(6) R1(7) }
            continue;
        }
        if (al == 0) {
            for (int r = 0; r < n; r++, px += 2, d0 += SCR_W) {
                u32 a = px[0], b = px[1];
                if (!(a | b)) continue;
                volatile u32 *d = (volatile u32 *)d0; d[0] = a; d[1] = b;
            }
        } else if (al == 2) {
            for (int r = 0; r < n; r++, px += 2, d0 += SCR_W) {
                u32 a = px[0], b = px[1];
                if (!(a | b)) continue;
                volatile u16 *d = (volatile u16 *)d0;
                if (a) { d[0] = a >> 16; d[1] = a; }
                if (b) { d[2] = b >> 16; d[3] = b; }
            }
        } else {
            for (int r = 0; r < n; r++, px += 2, d0 += SCR_W) {
                u32 a = px[0], b = px[1];
                if (!(a | b)) continue;
                volatile u16 *d = (volatile u16 *)(d0 - 1);
                d[0] = a >> 24; d[1] = a >> 8; d[2] = (a << 8) | (b >> 24); d[3] = b >> 8; d[4] = b << 8;
            }
        }
    }
}
/* clipped to [cx0, cx1) (HUD columns) */
static void s1draw_clip(s1c_t *tab, u8 *fb, u32 row, u32 col, int xs, int ys, int y0, int y1, int cx0, int cx1)
{
    if (xs >= cx0 && xs + 8 <= cx1) { s1draw(tab, fb, row, col, xs, ys, y0, y1); return; }
    int ra = ys < y0 ? y0 - ys : 0, rb = ys + 8 > y1 ? y1 - ys : 8;
    int xa = xs < cx0 ? cx0 - xs : 0, xb = xs + 8 > cx1 ? cx1 - xs : 8;
    if (xa >= xb) return;
    const u32 *px = s1lookup(tab, row, col);
    if (!px) return;
    for (int r = ra; r < rb; r++) {
        const u8 *p8 = (const u8 *)&px[r * 2];      /* (big-endian: pixel x is byte x; V2.7: no shift by a variable,
                                                       a libgcc loop on the SH-2) */
        u8 *d = fb + FB_LINE(ys + r) + xs;
        for (int x = xa; x < xb; x++) {
            u32 c = p8[x];
            if (c) d[x] = c;
        }
    }
}
/* HUD (rows 22-27) is 384 pixels wide on the arcade: [8 black][1P portrait 8-55][gap][1P panel 64-191]
 * [gap][2P panel 200-327][2P portrait 328-375][8 black].  Shown as 320 (user layout): black edges and
 * gaps dropped, 1P portrait cut 7 left / 8 right, 2P portrait 8 left / 9 right. */
/* 1P panel text: "パワー" -> "POW", "サクリャク" -> "TAC", the name's kana reading removed (font: ASCII at 0x6100 + c) */
/* bottom HUD panels (V1.9): the half-width kana labels -> 8-px Chinese (fusion-pixel, tools/zh/mkzh.py hud8):
 * レベル -> 等, サクリャク -> 策 + the strategy name (落石 / 伏兵 / 火計 / 爆裂計), パワー: -> 力;
 * the 2P panel (17 cells right) the same, and its "press start" / "insert coin" prompt is dropped */
#define S1CODE(r, c) (*(const u16 *)(s1base + ((((r) & 0x1f) + (((c) & 63) << 5) + (((r) & 0x20) << 6)) * 4)))
#ifndef BALLROW
#define BALLROW 3
#endif
#define HUD8 0x8024                             /* 等 策 力 落 石 伏, then 兵 火 計 爆 裂 at 0x8040 */
UI static u32 hud_sub(int R, int C, u32 row, u32 col)
{
#ifdef NOHUDSUB
    return 0;
#endif
    if (R < 23 || R > 27) return 0;
    u32 code = S1CODE(row, col);
    {                                           /* a panel's press start / insert coin prompt (its 2nd line starts with kana) */
        int p0 = C >= 25 ? 25 : 8;
        u32 r27 = (row + 27 - R) & 63, k27 = S1CODE(r27, col - C + p0);
        if (R >= 26 && k27 >= 0x61A0 && k27 <= 0x61FF) return 0x6120;
    }
    if (C >= 25) C -= 17;                       /* 2P panel: the same cells as 1P */
    if (R == 27 || C < 8) return 0;
    static const u16 name[4][3] = { { ZH_H_LUO, ZH_H_SHI, 0 }, { ZH_H_FU, ZH_H_BING, 0 }, { ZH_H_HUO, ZH_H_JI, 0 }, { ZH_H_BAO, ZH_H_LIE, ZH_H_JI } };
    /* letters in the palette of the level digit (row 24, column 13: the HUD's blue digits) */
    u32 r24 = (row + 24 - R) & 63, i24 = (r24 & 0x1f) + (((col - C + 13) & 63) << 5) + ((r24 & 0x20) << 6);
    u32 pal = 0x80000000u | ((u32)(*(const u16 *)(s1base + i24 * 4 + 2) & 31) << 16);
    int kana = code >= 0x61A0 && code <= 0x61FF;
    /* V1.10: two characters each: 等級 / 策略 / 力量 */
    if (R == 26 && C >= 8 && C <= 11 && (kana || code == 0x613A)) return C == 8 ? pal | ZH_H_LI : C == 9 ? pal | ZH_H_LIANG : 0x6120;
    if (R == 24 && C >= 8 && C <= 10 && kana) return C == 8 ? pal | ZH_H_DENG : C == 9 ? pal | ZH_H_JI2 : 0x6120;
    if (R == 25 && C >= 8 && C <= 12 && kana) return C == 8 ? pal | ZH_H_CE : C == 9 ? pal | ZH_H_LUE : 0x6120;
    if (R == 25 && C >= 13 && C <= 21) {        /* the strategy name (8 cells from column 14, table 0x37D0) */
        u32 f = S1CODE(row, col - C + 14) & 0xFF;
        int n = f == 0xEA ? 0 : f == 0xDC ? 1 : f == 0xC5 ? 2 : f == 0xDA ? 3 : -1;
        if (n < 0) return 0;
        int i = C - 13;
        return i < 3 && name[n][i] ? pal | name[n][i] : 0x6120;
    }
    if (R == 23 && C >= 12 && C <= 24) return 0x6120;     /* HUD space (keeps the panel colour) */
    return 0;
}
/* select screen card text (V1.8): "ブリョク" / "チリョク" -> 16x16 武 / 智 (centred on their bar), "レベル" -> "LV";
 * the stat bars and the type label (平均型...) 4 px left, clear of the selection frame */
#define SEL_WU ZH_WU                            /* tools/zh/mkzh.py (data/zh/zhcodes.h) */
#define SEL_ZHI ZH_ZHI
#define SEL_LV 0x8004                           /* "LV", 16x8 */
UI2 static u32 sel_code(u32 row, u32 col) { u32 idx = (row & 0x1f) + ((col & 63) << 5) + ((row & 0x20) << 6); return *(const u16 *)(s1base + idx * 4); }
UI2 static int sel_kind(u32 row, u32 cb)        /* 1 ブリョク, 2 チリョク, 3 レベル (+ the type label), 0 other */
{
    u32 a = sel_code(row, cb), b = sel_code(row, cb + 1);
    if (b == 0x60EB && a == 0x60FD) return 1;
    if (b == 0x60EB && a == 0x60D1) return 2;
    if (a == 0x60ED && b == 0x60FE) return 3;
    return 0;
}
UI2 static void sel_card_row(s1c_t *tab, u8 *fb, u32 row, u32 cb, int x0, int ys, int y0, int y1)
{
    static const u16 tyg[4][3] = {               /* type label (code 0x6C00 + 3 n): 安定 平均 安定 上昇 型 */
        { ZH_AN, ZH_DING, ZH_XING }, { ZH_PING, ZH_JUN, ZH_XING }, { ZH_AN, ZH_DING, ZH_XING }, { ZH_SHANG, ZH_SHENG, ZH_XING } };
    int sa = ys > y0 ? ys : y0, sb = ys + 8 < y1 ? ys + 8 : y1;   /* this row's strip */
    int kind = sel_kind(row, cb), up = sel_kind((row - 1) & 63, cb), dn = sel_kind((row + 1) & 63, cb);
    u32 *ov = &s1over[tab != s1c_m];
    /* V2.1: the selection frame's "1P" / "2P" label: the digit sits in the dropped margin column (cb - 1);
       digit at the card's left edge, P 7 px to its right (drawn last, over the frame's top line) */
    u32 lab = sel_code(row, cb - 1); lab = lab == 0x798c || lab == 0x79cc;
    if (kind != 3 && up != 3)
        for (int j = lab ? 1 : 0; j < 10; j++) {
            int x = x0 + 8 * j;
            if (kind == 1 || kind == 2) { if (j < 4) continue; x -= 4; }
            s1draw(tab, fb, row, cb + j, x, ys, sa, sb);
        }
    if (lab) { s1draw(tab, fb, row, cb - 1, x0, ys, sa, sb); s1draw(tab, fb, row, cb, x0 + 7, ys, sa, sb); }
    for (int d = -1; d <= 1; d++) {
        int kd = d < 0 ? up : d > 0 ? dn : kind;
        u32 lr = (row + d) & 63;
        if (kd == 1 || kd == 2) {               /* 武 / 智: 16x16 from 4 px above the label row */
            u32 g = kd == 1 ? SEL_WU : SEL_ZHI;
            int gy = ys + 8 * d - 4;
            for (int q = 0; q < 4; q++) {
                *ov = g + (q >> 1) * 0x10 + (q & 1);
                s1draw(tab, fb, lr, cb + (q & 1), x0 + 6 + 8 * (q & 1), gy + 8 * (q >> 1), sa, sb);
            }
        } else if (kd == 3) {                   /* the "レベル" row: no label; the type as 3 glyphs centred on the card */
            u32 t = (sel_code(lr, cb + 3) - 0x6C00) / 3 & 3;
            int gy = ys + 8 * d;
            for (int n = 0; n < 3; n++)
                for (int q = 0; q < 4; q++) {
                    *ov = tyg[t][n] + (q >> 1) * 0x10 + (q & 1);
                    s1draw(tab, fb, lr, cb + 3 + (q & 1), x0 + 10 + 22 * n + 8 * (q & 1), gy + 8 * (q >> 1), sa, sb);
                }
        }
    }
    *ov = 0;
}
static u32 hmk[2][2];                         /* V2.8, per CPU: the cells to draw in this top HUD row (cell mode), else all */
static inline int hm_in(s1c_t *tab, int x)                  /* an 8-px cell drawn at x touches a cell to draw */
{
    int a = x >> 3, b = (x + 7) >> 3; const u32 *hm = hmk[tab != s1c_m]; u32 hm0 = hm[0], hm1 = hm[1];
    if (a >= 0 && a < 41 && ((a < 32 ? hm0 : hm1) & bit32[a & 31])) return 1;
    return b >= 0 && b < 41 && ((b < 32 ? hm0 : hm1) & bit32[b & 31]);
}
UI2 static void hud_balls(s1c_t *tab, u8 *fb, int hud1, int hud2, u32 c0, int ox, int ys, int y0, int y1)
{
    for (int i = 0; i < 3; i++) {
        if (hud1 && hm_in(tab, 4 + 8 * i)) s1draw(tab, fb, BALLROW, 9 + i, 4 + 8 * i, ys, y0, y1);
        int x = (41 - (int)c0) * 8 - ox - 24 + 4 + 8 * i;
        if (hud2 && hm_in(tab, x)) s1draw(tab, fb, BALLROW, 38 + i, x, ys, y0, y1);
    }
}
UI2 static void hud_top2(s1c_t *tab, u8 *fb, u32 row, u32 c0, int ox, int ys, int y0, int y1)
{
    const u32 last = 54;
    for (int c = 0; c < 41; c++) { u32 cc = (c0 + c) & 63; if ((cc < 38 || cc > last) && hm_in(tab, c * 8 - ox)) s1draw(tab, fb, row, cc, c * 8 - ox, ys, y0, y1); }
    for (u32 cc = 41; cc <= last; cc++) { int x = ((int)cc - (int)c0) * 8 - ox - 24; if (hm_in(tab, x)) s1draw(tab, fb, row, cc, x, ys, y0, y1); }
}
#if defined(SHOWFPS) || defined(DBGLAY)
UI2 static void draw_num(u8 *fb, int x, int v);
UI2 static void fps_draw(u8 *fb) {
#ifdef STKDBG
    { u32 a = 0x26006380; while (a < 0x26006900 && *(volatile u32 *)a == 0x5AA55AA5) a += 4; draw_num(fb, 100, (0x26006900 - a) / 32 > 99 ? 99 : (0x26006900 - a) / 32);
      a = 0x26006900; while (a < 0x26007000 && *(volatile u32 *)a == 0x5AA55AA5) a += 4; draw_num(fb, 122, (0x26007000 - a) / 32 > 99 ? 99 : (0x26007000 - a) / 32); }
#elif defined(QDBG)
    { u32 qd = dy_w - dy_r; draw_num(fb, 100, qd > 99 ? 99 : qd); }
#else
    { extern volatile u32 hint_ph; draw_num(fb, 100, hint_ph > 99 ? 99 : hint_ph); }
#endif
    for (int i = 1; i < 6; i++) { u32 v = CT(&ptshow[i])[0] / (i == 5 ? 1 : 6); draw_num(fb, 100 + i * 22, v > 99 ? 99 : v); } draw_num(fb, 296, fps_game); draw_num(fb, 272, fps_logic); draw_num(fb, 248, logic_busy > 99 ? 99 : logic_busy); }
#else
#define fps_draw(fb)
#endif
static const s16 hudseg[4][3] = { { 12, 51, 0 }, { 64, 192, 40 }, { 200, 328, 161 }, { 333, 370, 283 } };   /* talk rows (V2.2: the V2.1 portraits, the text 7 px right) */
/* V2.1 (outside talk): portraits 3 px wider on each side (1P arcade x 12-51 at 0, 2P 333-370 at 283); the 1P name and
   label column 7 px right, the 2P values (all but the name and the labels) 7 px left.  Pieces drawn in order, the
   moved ones last (their blank margin falls on the other piece's blank margin) */
/* one 8-line strip of the bottom HUD (cart).  V1.10: the 力量 row and the gauge (rows 26-27) 2 lines lower */
static u8 hudside[2];                          /* V3.15: per CPU (tab): 1 = 1P side kept, 2 = 2P side kept */
UI2 static void hud_row(s1c_t *tab, u8 *fb, u32 row, int rr, int dlg, int yy, int a, int b, int hox, u32 hc0, int mode)
{   /* mode: 0 all, 1 the text panels (1P, 2P), 2 the portraits, 3 the text panels blank (panel colour).
       No local tables: the slave's stack is 1 KB */
    int w1 = rr <= 23 ? 32 : 16, w2 = rr <= 23 ? 32 : 24;
    for (int k = 0; k < (dlg ? 4 : 6); k++) {
        int s0, s1e, d0, pan;
        if (dlg) { s0 = hudseg[k][0]; s1e = hudseg[k][1]; d0 = hudseg[k][2]; pan = k == 1 || k == 2; }
        else switch (rr <= 23 && (k == 3 || k == 4) ? 7 - k : k) {   /* name rows: the 2P values first, the name over them */
        case 0: s0 = 12; s1e = 51; d0 = 0; pan = 0; break;                       /* 1P portrait */
        case 1: s0 = 64 + w1; s1e = 192; d0 = 33 + w1; pan = 1; break;          /* 1P values */
        case 2: s0 = 64; s1e = 64 + w1; d0 = 40; pan = 1; break;                /* 1P name / labels +7 */
        case 3: s0 = 200; s1e = 200 + w2; d0 = 161; pan = 1; break;             /* 2P name / labels */
        case 4: s0 = 200 + w2; s1e = 328; d0 = 161 + w2 - 7; pan = 1; break;   /* 2P values -7 */
        default: s0 = 333; s1e = 370; d0 = 283; pan = 0; break;                 /* 2P portrait */
        }
        if ((mode == 1 || mode == 3) && !pan) continue;
        { u8 hsd = hudside[tab != s1c_m]; if (hsd && !dlg && (hsd == 1) == (d0 < 161)) continue; }   /* V3.15: that side kept */
        if (mode == 2 && pan) continue;
        if (dlg && k == 2) continue;
        if (dlg && k == 1) s1e = 64 + 283 - 40;     /* talk: the text (arcade x 64-306, 15 glyphs) across both panels, up to the 2P portrait */
        int dx0 = d0, dx1 = d0 + s1e - s0;
        for (int ax = (s0 + hox) & ~7; ax < s1e + hox; ax += 8) {        /* ax: cell start in arcade x + fine */
            if (mode == 3) s1over[tab != s1c_m] = 0x6120;
            else if (!dlg && pan) s1over[tab != s1c_m] = hud_sub(rr, ax >> 3, row, hc0 + (ax >> 3));
            s1draw_clip(tab, fb, row, hc0 + (ax >> 3), d0 + ax - hox - s0, yy, a, b, dx0, dx1);
            s1over[tab != s1c_m] = 0;
        }
    }
}
UI2 static void hud_strip(s1c_t *tab, u8 *fb, int ys, int y0, int y1, int hox, u32 hc0)
{
    { int R = ys >> 3; u32 k = R >= 22 && R < 28 ? CT(&hudsk)[0] : 0; hudside[tab != s1c_m] = (k >> (R - 22)) & 1 ? 1 : (k >> (R - 14)) & 1 ? 2 : 0; }
    int rr = (ys + ((16 + s1y) & 7)) >> 3;
    u32 row = ((16 + s1y + ys) >> 3) & 63;
    int a = ys > y0 ? ys : y0, b = ys + 8 < y1 ? ys + 8 : y1;
    if (a >= b) return;
    int dlg = (flags & 0x40) && rr >= 22 && rr <= 25;
    if (rr == 26 || rr == 27) {                 /* V1.10: the 力量 row and the gauge 2 lines lower (text panels only) */
        hud_row(tab, fb, row, rr, 0, ys, a, b, hox, hc0, 2);
        if (rr == 26) hud_row(tab, fb, row, rr, 0, ys, a, ys + 2 < b ? ys + 2 : b, hox, hc0, 3);
        hud_row(tab, fb, row, rr, 0, ys + 2, a, b, hox, hc0, 1);
    } else if (rr <= 28) hud_row(tab, fb, row, rr, dlg, ys, a, b, hox, hc0, 0);
    if (rr == 27 || rr == 28) hud_row(tab, fb, (row - 1) & 63, rr - 1, 0, ys - 6, a, b, hox, hc0, 1);   /* the shifted row above */
}
#ifdef CMCHK   /* test: a cell not drawn must already hold its scroll1 pixels (0 where transparent) */
UI2 static void cm_chk(s1c_t *tab, u8 *fb, u32 row, u32 c0, int rr, int ys, int y0, int y1)
{
    u32 m0 = CT(&s1dm[rr][0])[0], m1 = CT(&s1dm[rr][1])[0]; volatile u32 *K = (volatile u32 *)0x26007000;
    for (int c = 0; c < 40; c++) { if ((c < 32 ? m0 : m1) & bit32[c & 31]) continue;
        const u32 *px = s1lookup(tab, row, c0 + c); K[1]++; int bad = 0;
        for (int r = 0; r < 8; r++) { int y = ys + r; if (y < y0 || y >= y1) continue;
            const u32 *d = (const u32 *)(fb + FB_LINE(y) + c * 8);
            if (d[0] != (px ? px[r * 2] : 0) || d[1] != (px ? px[r * 2 + 1] : 0)) bad = 1; }
        if (bad) { K[2]++; K[3] = (rr << 8) | c; } }
}
#endif
#ifdef CMCHK2
UI2
#endif
static void draw_scroll1(u8 *fb, int y0, int y1)
{
    s1c_t *tab = cpu_id() ? s1c_s : s1c_m;
    if (!(layerctl & 2)) return;
    int oy = (16 + s1y + y0) & 7;
    if (flags & 4) {
        /* select screen: the arcade's 384 pixels = 8-px margin, 4 cards of 80 px, 16-px gaps.
         * The margins and gaps are dropped (4 x 80 = 320, nothing scaled); the 4-px selection
         * frame edges that sat in them (columns 12k px 0-3, 12k+11 px 4-7) are drawn 2 px wide
         * over the card edges. */
        u32 c0 = ((64 + s1x) >> 3) & 63;
        for (int ys = y0 - oy; ys < y1; ys += 8) {
            u32 row = ((16 + s1y + ys) >> 3) & 63;
            int ra = ys < y0 ? y0 - ys : 0, rb = ys + 8 > y1 ? y1 - ys : 8;
            for (int k = 0; k < 4; k++) {
                sel_card_row(tab, fb, row, c0 + 12 * k + 1, 80 * k, ys, y0, y1);
                u32 L8[8], R8[8]; const u32 *pl = s1lookup(tab, row, c0 + 12 * k);
                if (pl) for (int r = 0; r < 8; r++) L8[r] = pl[r * 2];
                const u32 *pr = s1lookup(tab, row, c0 + 12 * k + 11);
                if (pr) for (int r = 0; r < 8; r++) R8[r] = pr[r * 2 + 1];
                for (int r = ra; r < rb; r++) {
                    u8 *d = fb + FB_LINE(ys + r) + 80 * k;
                    if (pl) { u32 a = L8[r]; if (a >> 24) d[0] = a >> 24; if ((a >> 8) & 255) d[1] = a >> 8; }
                    if (pr) { u32 b = R8[r]; if (b >> 24) d[78] = b >> 24; if ((b >> 8) & 255) d[79] = b >> 8; }
                }
            }
        }
        return;
    }
    int ox = (96 + s1x) & 7;
    u32 c0 = ((96 + s1x) >> 3) & 63;
    int hudy = (flags & 8) ? 22 * 8 : SCR_H;
    int hox = (64 + s1x) & 7;
    u32 hc0 = ((64 + s1x) >> 3) & 63;
    u32 rh = CT(&rowhas)[0];
    /* top HUD (V1.9): VITAL labels (row 2, columns 9 / 38: 0x6220) not shown; the VITAL balls under them
     * (row 3, columns 9-11 / 38-40) drawn in row 4 under the life numbers; the 2P numbers (columns 41-54) 24 px left */
    int hud1 = S1CODE(2, 9) == 0x6220, hud2 = S1CODE(2, 38) == 0x6220;
    int boss = 0;   /* boss life bars (rows 5, 6; columns 41-54, codes 0x6C6x): their right end is past x 320 */
    if (flags & 8) { for (u32 r = 5; r <= 6; r++) for (u32 c = 54; c >= 41; c--) if ((S1CODE(r, c) & 0xFFF0) == 0x6C60) { boss |= 1 << r; break; }
        for (u32 r = 7; r <= 10; r++) if ((S1CODE(r, 54) & 0xFFF0) == 0x6C60) boss |= 1 << r; }   /* (rows 7-10: the bar's right end, column 54, is the last to go) */   /* V3.19: rows 5-10 (two bosses: rows 5 and 7; the 2nd bar was not moved, its end cut) */
    int cm = CT(&s1cmon)[0];
#ifdef SKIPCHK
    if (skchk[cpu_id()]) cm = 0;
#endif
    for (int ys = y0 - oy; ys < y1; ys += 8) {
        u32 row = ((16 + s1y + ys) >> 3) & 63;
        int cmr = cm && ys >= 0 && ys < 176;
#ifdef CMCHK2   /* test: drawing the whole row after the cell mode one changes nothing */
        for (int pass = 0; pass < 1 + cmr; pass++) {
        if (pass) { u8 *sc = fb + 0x12000 + (tab != s1c_m) * 0x1000;
            for (int r = 0; r < 8; r++) { int y = ys + r; if (y >= y0 && y < y1) for (int x = 0; x < SCR_W; x += 4) *(u32 *)(sc + r * SCR_W + x) = *(u32 *)(fb + FB_LINE(y) + x); }
            cmr = 0; }
#endif
        { u32 *hm = hmk[tab != s1c_m]; if (cmr && !s1xg[tab != s1c_m][ys >> 3]) { hm[0] = CT(&s1dm[ys >> 3][0])[0]; hm[1] = CT(&s1dm[ys >> 3][1])[0]; } else hm[0] = hm[1] = 0xFFFFFFFFu; }
        if (row == 4 && (hud1 | hud2)) hud_balls(tab, fb, hud1, hud2, c0, ox, ys, y0, y1);
        { int rr = (ys + ((16 + s1y) & 7)) >> 3; if (rr < 0 || rr > 28 || (!(rh & bit32[rr]) && !(ys >= hudy && rr >= 27))) goto next; }
        if (ys >= hudy) hud_strip(tab, fb, ys, y0, y1, hox, hc0);
        else if ((hud2 && (row == 2 || row == 3)) || (row >= 5 && row <= 10 && (boss >> row & 1))) hud_top2(tab, fb, row, c0, ox, ys, y0, y1);   /* V3.2: the boss bars (V3.19: rows 5-10, columns 41-54) too */
        else if (cmr) {     /* V2.8: only the cells changed / under sprites (cell mode: aligned) */
            int rr = ys >> 3;
            u32 w0 = CT(&s1dm[rr][0])[0], w1 = CT(&s1dm[rr][1])[0], g = s1xg[tab != s1c_m][rr];
            if (g) for (int c = 0; c < 41; c++) if (g & bit8[((c0 + c) >> 3) & 7]) { if (c < 32) w0 |= bit32[c]; else w1 |= bit32[c & 31]; }
            for (int c = 0; w0; c++, w0 >>= 1) { while (!(w0 & 255)) { w0 >>= 8; c += 8; } if (w0 & 1) s1draw(tab, fb, row, c0 + c, c * 8, ys, y0, y1); }
            for (int c = 32; w1; c++, w1 >>= 1) { while (!(w1 & 255)) { w1 >>= 8; c += 8; } if (w1 & 1) s1draw(tab, fb, row, c0 + c, c * 8, ys, y0, y1); }
#ifdef CMCHK
            if (!(row == 4 && (hud1 | hud2))) cm_chk(tab, fb, row, c0, rr, ys, y0, y1);
#endif
        }
        else
#ifndef NOS1ROW
            s1row(tab, fb, row, c0, ox, ys, y0, y1);
#else
            for (int c = 0; c < 41; c++) s1draw(tab, fb, row, c0 + c, c * 8 - ox, ys, y0, y1);
#endif
      next:;
#ifdef CMCHK2
        if (pass) { u8 *sc = fb + 0x12000 + (tab != s1c_m) * 0x1000; volatile u32 *K = (volatile u32 *)0x26007000; int bad = 0; K[12]++;
            for (int r = 0; r < 8; r++) { int y = ys + r; if (y >= y0 && y < y1) for (int x = 0; x < SCR_W; x += 4) if (*(u32 *)(sc + r * SCR_W + x) != *(u32 *)(fb + FB_LINE(y) + x)) { bad = 1; K[14] = (ys << 16) | x; } }
            if (bad) K[13]++; }
        }
#endif
    }
}

/* ---------------- scaled scenes (menus / story pictures): 384x224 shown at 320x224 (5/6 across) ----------------
 * Output pixel o covers source [1.2o, 1.2o+1.2): primary source = the pixel under its centre, alt =
 * the other one it touches (used when the primary is transparent, so thin strokes are kept).
 * scroll2/scroll3 come pre-scaled per tile (tools/mkpic.py), s1 and sprites are scaled here. */
#define PIC_TAB ((const u32 *)0x0201D000)       /* 4096 x (key, address) */
static void scale_init(void)
{
    for (int o = 0; o < SCR_W; o++) { int p = (6 * o + 3) / 5, lo = (6 * o) / 5, hi = (6 * o + 5) / 5; mxp[o] = p; mxa[o] = p == lo ? hi : lo; }
    for (int o = 0; o < SCH; o++) { myp[o] = o; mya[o] = o; }
    for (int X = 0, o = 0; X <= 384; X++) { while (o < SCR_W && mxp[o] < X) o++; oinvx[X] = o; }
    for (int Y = 0, o = 0; Y <= 224; Y++) { while (o < SCH && myp[o] < Y) o++; oinvy[Y] = o; }
}
static inline int ox_of(int X) { return X <= 0 ? 0 : X >= 384 ? SCR_W : oinvx[X]; }
static inline int oy_of(int Y) { return Y <= 0 ? 0 : Y >= 224 ? SCH : oinvy[Y]; }
static const u8 st23[2][4] = { { 0, 13, 27, 40 }, { 0, 27, 53, 80 } };
static inline const u8 *pic_find(u32 key)
{
    u32 h = (key * 0x9E3779B1u) >> 20;
    for (;;) {
        const u32 *e = &PIC_TAB[h * 2];
        if (e[0] == key) return (const u8 *)e[1];
        if (!e[0]) return 0;
        h = (h + 1) & 4095;
    }
}
static void draw_tilemap23_scaled(u8 *fb, int L, int y0, int y1)
{
    if (!layer23_on(L)) return;
    int l3 = L == 3, ts = l3 ? 32 : 16, sh = l3 ? 5 : 4, per = l3 ? 80 : 40;
    const u8 *st = st23[l3];
    const u8 *base = (const u8 *)(l3 ? P->b3 : P->b2);
    u32 scx = P->sc23[(L - 2) * 2], scy = P->sc23[(L - 2) * 2 + 1];
    int pb = l3 ? 96 : 64;
    int Kx = ((64 + scx) * 5 + 3) / 6, Ky = 16 + scy;
    int ya = y0 > VOFF ? y0 : VOFF, yb = y1 < VOFF + SCH ? y1 : VOFF + SCH;
    if (ya >= yb) return;
    int c0 = (64 + scx) >> sh, c1 = (64 + scx + 383) >> sh, r0 = (16 + scy) >> sh, r1 = (16 + scy + 223) >> sh;
    for (int r = r0; r <= r1 && r < 64; r++) {
        int oy = VOFF + r * ts - Ky, h = ts;
        int ra = ya - oy, rb = yb - oy;
        if (ra < 0) ra = 0;
        if (rb > h) rb = h;
        if (ra >= rb) continue;
        for (int c = c0; c <= c1 && c < 64; c++) {
            int ox = (c / 3) * per + st[c % 3] - Kx, w = st[c % 3 + 1] - st[c % 3];
            int xa = ox < 0 ? -ox : 0, xb = ox + w > SCR_W ? SCR_W - ox : w;
            if (xa >= xb) continue;
            const u16 *e = (const u16 *)(base + tm_index(L, r, c) * 4);
            u32 code = e[0], attr = e[1];
            if (l3) { code &= 0x3fff; if (code > 0x7ff) continue; }
            else { u32 c2 = code << 1; if (c2 < 0x4000 || c2 > 0x7fff) continue; }
            u32 key = 0x80000000u | (l3 << 22) | ((c % 3) << 18) | (((attr >> 5) & 3) << 16) | code;
            const u8 *t = pic_find(key);
            if (!t) continue;
            const u8 *pl = lut[pb + (attr & 31)];
            if ((u32)t < 32) {                      /* one opaque pen (0x10 + pen) */
                u32 v = pl[(u32)t & 15]; v |= v << 8; v |= v << 16;
                for (int y = ra; y < rb; y++) {
                    u8 *d = fb + FB_LINE(oy + y) + ox;
                    int x = xa;
                    for (; x < xb && ((u32)(d + x) & 3); x++) d[x] = v;
                    for (; x + 4 <= xb; x += 4) *(u32 *)(d + x) = v;
                    for (; x < xb; x++) d[x] = v;
                }
                continue;
            }
            int rbytes = (w + 1) >> 1;
            for (int y = ra; y < rb; y++) {
                const u8 *sr = t + y * rbytes;
                u8 *d = fb + FB_LINE(oy + y) + ox;
                int x = xa;
                if (x & 1) { u32 n = sr[x >> 1] & 15; if (n != 15) d[x] = pl[n]; x++; }
                for (; x + 2 <= xb; x += 2) {
                    u32 bb = sr[x >> 1];
                    if (bb == 0xff) continue;
                    u32 n0 = bb >> 4, n1 = bb & 15;
                    if (n0 != 15) d[x] = pl[n0];
                    if (n1 != 15) d[x + 1] = pl[n1];
                }
                if (x < xb) { u32 n = sr[x >> 1] >> 4; if (n != 15) d[x] = pl[n]; }
            }
        }
    }
}
UI2 static void draw_scroll1_scaled(u8 *fb, int y0, int y1, u32 rowmask)
{
    if (!(layerctl & 2)) return;
    s1c_t *tab = cpu_id() ? s1c_s : s1c_m;
    int ya = y0 > VOFF ? y0 - VOFF : 0, yb = y1 < VOFF + SCH ? y1 - VOFF : SCH;    /* content rows */
    if (ya >= yb) return;
    int fx = (64 + s1x) & 7, fy = (16 + s1y) & 7;
    u32 c0 = ((64 + s1x) >> 3) & 63, r0 = ((16 + s1y) >> 3) & 63;
    for (int R = 0; R < 29; R++) {
        int Y0 = R * 8 - fy;                       /* source rows of this cell row: [Y0, Y0+8) */
        int oa = oy_of(Y0), ob = oy_of(Y0 + 8);
        if (oa < ya) oa = ya;
        if (ob > yb) ob = yb;
        if (oa >= ob) continue;
        u32 row = (r0 + R) & 63;
        if (!(rowmask & bit32[R])) continue;
        for (int C = 0; C < 49; C++) {
            int X0 = C * 8 - fx;
            int xa = ox_of(X0), xb = ox_of(X0 + 8);
            if (xa >= xb) continue;
            const u32 *px = s1lookup(tab, row, c0 + C);
            if (!px) continue;
            const u8 *cb = (const u8 *)px;
            for (int o = oa; o < ob; o++) {
                int py = myp[o] - Y0, pya = mya[o] - Y0;
                const u8 *ln = cb + py * 8, *la = (pya >= 0 && pya < 8) ? cb + pya * 8 : 0;
                u8 *d = fb + FB_LINE(VOFF + o);
                for (int x = xa; x < xb; x++) {
                    int qx = mxp[x] - X0, qa = mxa[x] - X0;
                    u32 v = ln[qx];
                    if (!v && qa >= 0 && qa < 8) v = ln[qa];
                    if (!v && la) v = la[qx];
                    if (v) d[x] = v;
                }
            }
        }
    }
}
/* where an arcade scroll1 cell (screen row R, column C) goes in a scaled scene: x/y offset, or scaled (returns 1) */
static inline int s1rule(int lay, int R, int C, int *dx, int *dy)
{
    *dy = 0;
    switch (lay) {
    case 1: if (R <= 5) { if (C < 10) { *dx = 216; *dy = 184; } else { *dx = -64; *dy = (R >> 1) * 4; } } else { *dx = -16; *dy = 16; } return 0;   /* name: bottom right; V1.10: text lines 4 px apart, the weapons 16 px lower */
    case 2: *dx = R <= 7 ? -32 : C < 16 ? -12 : C < 37 ? -56 : -60; return 0;   /* R: tilemap row (the table scrolls) */
    case 3: *dx = -32; return R >= 8 && R <= 19;
    case 4: *dx = R <= 3 ? 0 : -32; return 0;          /* top rows from the arcade's left edge: the high score "NAME" starts there */
    case 5: *dx = -24; return 0;
    }
    *dx = -32; return 0;
}
UI2 static void draw_scroll1_layout(u8 *fb, int y0, int y1)
{
    if (!(layerctl & 2)) return;
    s1c_t *tab = cpu_id() ? s1c_s : s1c_m;
    int lay = P->layout;
    int fx = (64 + s1x) & 7, fy = (16 + s1y) & 7;
    u32 c0 = ((64 + s1x) >> 3) & 63, r0 = ((16 + s1y) >> 3) & 63;
    u32 scaled = 0;
    for (int R = 0; R < 29; R++) {
        if (P->ui_mode == 1 && (R == 16 || R == 17 || R == 27)) continue;   /* coin prompt / CREDIT: menu instead */
        int only24 = lay == 4 && R >= 26;            /* continue: the 1P/2P "press START" boxes at the bottom are not shown
                                                        (V3.13: but the translated line's 24x24 blocks reach row 26: those are) */
        int dx, dy;
        u32 row = (r0 + R) & 63;
        int RR = lay == 2 ? (int)row : R;
        if (s1rule(lay, RR, 0, &dx, &dy)) { scaled |= bit32[R]; continue; }
        int ys = R * 8 - fy;
        for (int C = 0; C < 49; C++) {
            s1rule(lay, RR, C, &dx, &dy);
            int xs = C * 8 - fx + dx, yy = ys + dy;
            if (xs <= -8 || xs >= SCR_W || yy + 8 <= y0 || yy >= y1) continue;
            if (only24) { u32 col = (c0 + C) & 63; if ((*(const u16 *)(s1base + ((row & 0x1f) + (col << 5) + ((row & 0x20) << 6)) * 4) & 0xF000) != 0x9000) continue; }
            s1draw(tab, fb, row, c0 + C, xs, yy, y0, y1);
        }
    }
    if (scaled) draw_scroll1_scaled(fb, y0, y1, scaled);
}
static void draw_sprites_scaled(u8 *fb, int y0, int y1)
{
    int ya = y0 > VOFF ? y0 - VOFF : 0, yb = y1 < VOFF + SCH ? y1 - VOFF : SCH;
    if (ya >= yb) return;
    for (int i = (int)nspr - 1; i >= 0; i--) {
        const u16 *e = &spr[i * 4];
        int x = e[0], y = e[1]; u32 colour = e[3];
        int code = sprite_code(e[2]);
        if (code < 0) continue;
        int fx = (colour >> 5) & 1, fy = (colour >> 6) & 1;
        const u8 *pl = lut[colour & 31];
#ifdef FLAMEDITH
        int dith = flame(e) ? 1 + (P->seq & 1) : 0;    /* V3.1: the flames (the arcade shows each flame row every other frame) */
#else
        int dith = 0;
#endif
        int nx = 1, ny = 1;
        if (colour & 0xff00) { nx = ((colour >> 8) & 15) + 1; ny = ((colour >> 12) & 15) + 1; }
        for (int nys = 0; nys < ny; nys++) {
            int sy = ((y + nys * 16) & 0x1ff) - 16;           /* arcade screen y */
            int oa = oy_of(sy), ob = oy_of(sy + 16);
            if (oa < ya) oa = ya;
            if (ob > yb) ob = yb;
            if (oa >= ob) continue;
            for (int nxs = 0; nxs < nx; nxs++) {
                int sx = ((x + nxs * 16) & 0x1ff) - 64;       /* arcade screen x */
                int xa = ox_of(sx), xb = ox_of(sx + 16);
                if (xa >= xb) continue;
                int t;
                if (colour & 0xff00) {
                    int cx = fx ? ((code + (nx - 1) - nxs) & 0xf) : ((code + nxs) & 0xf);
                    int cy = fy ? 0x10 * (ny - 1 - nys) : 0x10 * nys;
                    t = (code & ~0xf) + cx + cy;
                } else t = code;
                if (t >= 12288) continue;
                u32 ent = SPR_TAB[t];
                if (!ent) continue;
                int r0 = SPR_R0(ent), r1 = SPR_R1(ent);
                const u8 *src = SPR_PTR(ent) - r0 * 8;
                for (int o = oa; o < ob; o++) {
                    int py = myp[o] - sy, pya = mya[o] - sy;
                    if (fy) { py = 15 - py; pya = 15 - pya; }
                    const u8 *la = (pya >= r0 && pya <= r1) ? src + pya * 8 : 0;
                    if (py < r0 || py > r1) { if (!la) continue; py = pya; la = 0; }    /* stored rows only */
                    const u8 *ln = src + py * 8;
                    u8 *d = fb + FB_LINE(VOFF + o);
                    for (int xo = xa; xo < xb; xo++) {
                        int qx = mxp[xo] - sx, qa = mxa[xo] - sx;
                        if (fx) { qx = 15 - qx; qa = 15 - qa; }
                        u32 n = (qx & 1) ? ln[qx >> 1] & 15 : ln[qx >> 1] >> 4;
                        if (n == 15 && qa >= 0 && qa < 16) n = (qa & 1) ? ln[qa >> 1] & 15 : ln[qa >> 1] >> 4;
                        if (n == 15 && la) n = (qx & 1) ? la[qx >> 1] & 15 : la[qx >> 1] >> 4;
                        if (n != 15) d[xo] = pl[n];
                    }
                }
            }
        }
    }
}

/* ---------------- front-end screens (cart code) ---------------- */
UI static void ui_str(u8 *fb, int y0, int y1, int x, int y, const char *t, int pal)
{
    if (y + 8 <= y0 || y >= y1) return;
    s1c_t *tab = cpu_id() ? s1c_s : s1c_m;
    for (; *t; t++, x += 8) {
        u32 code = 0x6000 + (u8)*t, g = (x >> 3) & 1, k = ((code - 0x6000) << 1) | g;
        if (!(s1blank[k >> 3] & bit8[k & 7])) continue;
        const u8 *px = (const u8 *)s1c_get(tab, k, code, g, pal);
        for (int r = 0; r < 8; r++) {
            int yy = y + r;
            if (yy < y0 || yy >= y1) continue;
            u8 *d = fb + FB_LINE(yy) + x;
            for (int c = 0; c < 8; c++) if (px[r * 8 + c]) d[c] = px[r * 8 + c];
        }
    }
}
/* V3.23: the infinity sign, 13x7 with a 2-pixel stroke, coloured like the arcade font: the stroke takes the '0' glyph's
   diagonal shading (its colour at the same row + column), the dark edge goes below / right of it as on the digits */
UI static void ui_inf(u8 *fb, int y0, int y1, int x, int y, int pal)
{
    static const char *const m[7] = { "..XXX...XXX..", ".XX.XX.XX.XX.", "XX...XXX...XX", "XX....X....XX", "XX...XXX...XX", ".XX.XX.XX.XX.", "..XXX...XXX.." };
    if (y + 8 <= y0 || y >= y1) return;
    s1c_t *tab = cpu_id() ? s1c_s : s1c_m;
    u32 code = 0x6000 + '0', g = (x >> 3) & 1, k = ((code - 0x6000) << 1) | g;
    const u8 *px = (const u8 *)s1c_get(tab, k, code, g, pal);
    u8 edge = px[7 * 8 + 2], grad[16] = { 0 };
    for (int rr = 0; rr < 7; rr++) for (int c = 0; c < 8; c++) { u8 p = px[rr * 8 + c]; if (p && p != edge && !grad[rr + c]) grad[rr + c] = p; }
    for (int i = 1; i < 16; i++) if (!grad[i]) grad[i] = grad[i - 1];
    for (int i = 14; i >= 0; i--) if (!grad[i]) grad[i] = grad[i + 1];
    #define INF_ON(rr, cc) ((rr) >= 0 && (rr) < 7 && (cc) >= 0 && (cc) < 13 && m[rr][cc] == 'X')
    for (int rr = 0; rr < 8; rr++) {
        int yy = y + rr;
        if (yy < y0 || yy >= y1) continue;
        u8 *d = fb + FB_LINE(yy) + x;
        for (int c = 0; c < 14; c++) {
            if (INF_ON(rr, c)) d[c] = grad[rr + (c * 7 + 6) / 13];
            else if (INF_ON(rr - 1, c) || INF_ON(rr, c - 1) || INF_ON(rr - 1, c - 1)) d[c] = edge;
        }
    }
    #undef INF_ON
}
UI static void ui_draw(u8 *fb, int y0, int y1)
{
    static const char *const names[10] = { "Charge speed", "Exp. rate", "Lives", "SFX volume", "Music volume", "Stage", "Invincible", "Full-screen attack", "Button test", "Exit" };
    static const char *const bn[12] = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "C", "START", "X", "Y", "Z", "MODE" };
    int m = P->ui_mode, sel = P->ui_sel;
    if (m == 1) {
        ui_str(fb, y0, y1, 140, 128, "Start", sel ? 2 : 31);
        ui_str(fb, y0, y1, 132, 144, "Options", sel ? 31 : 2);
        ui_str(fb, y0, y1, sel ? 118 : 126, sel ? 144 : 128, ">", 31);
        return;
    }
    if (m == 2) {
        ui_str(fb, y0, y1, 132, 40, "OPTIONS", 31);
        for (int i = 0; i < 10; i++) {
            int y = 58 + i * 13, c = i == sel ? 31 : 2;
            ui_str(fb, y0, y1, 64, y, names[i], c);
            if (i == 6 || i == 7) ui_str(fb, y0, y1, 224, y, (i == 7 ? P->ui_fsa : P->ui_opt[6]) ? "ON" : "OFF", c);
            else if (i < 6) {
                char v[5]; int k = 0, n = P->ui_opt[i];
                if (i == 2 && n > 9) { ui_inf(fb, y0, y1, 224, y, c); continue; }   /* V3.23: infinite lives */
                if (i < 2) { v[k++] = 'x'; }
                if (n >= 10) { v[k++] = '1'; n -= 10; }
                v[k++] = '0' + n; v[k] = 0;
                ui_str(fb, y0, y1, 224, y, v, c);
            }
        }
        ui_str(fb, y0, y1, 52, 58 + sel * 13, ">", 31);
        ui_str(fb, y0, y1, 28, 186, "Up/Down select  Left/Right change", 0);
        ui_str(fb, y0, y1, 48, 198, "B enter  A/C back  START exit", 0);
        return;
    }
    /* button test */
    u16 pad = P->ui_pad;
    ui_str(fb, y0, y1, 116, 40, "BUTTON TEST", 31);
    ui_str(fb, y0, y1, 108, 58, P->ui_six ? "6-button pad" : "3-button pad", 2);
    int nb = P->ui_six ? 12 : 8;
    for (int i = 0; i < nb; i++) {
        int x = 64 + (i & 1) * 112, y = 82 + (i >> 1) * 16;
        ui_str(fb, y0, y1, x, y, bn[i], (pad >> i) & 1 ? 31 : 0);
    }
    ui_str(fb, y0, y1, 36, 186, "Hold any button 1 second to exit", 0);
}
static void clear_band(u8 *fb, int y0, int y1)
{
    for (int y = y0; y < y1; y++) {
        u32 *d = (u32 *)(fb + FB_LINE(y));
        for (int i = 0; i < SCR_W / 4; i += 4) { d[i] = 0; d[i + 1] = 0; d[i + 2] = 0; d[i + 3] = 0; }
    }
}
/* top-right: game frames during the last 60 vblanks */
static const u16 font3x5[16] = { 0x7b6f, 0x2492, 0x73e7, 0x73cf, 0x5bc9, 0x79cf, 0x79ef, 0x7249, 0x7bef, 0x7bcf,
                                 0x2bed, 0x6bae, 0x3923, 0x6b6e, 0x79e7, 0x79e4 };
u16 dbg_cc[8]; static int num_hex;
UI2 static void draw_num(u8 *fb, int x0, int v)
{
    int d[2] = { num_hex ? (v >> 4) & 15 : (v / 10) % 10, num_hex ? v & 15 : v % 10 };
    for (int y = 0; y < 12; y++) {
        u8 *l = fb + FB_LINE(y + 2) + x0;
        for (int x = 0; x < 20; x++) {
            int on = 0;
            if (y >= 1 && y < 11 && x >= 1 && x < 19) {
                int k = (x - 1) / 9, cx = ((x - 1) % 9) / 2, cy = (y - 1) / 2;
                if (cx < 3) on = (font3x5[d[k]] >> (14 - (cy * 3 + cx))) & 1;
            }
            if (on) l[x] = 255;
        }
    }
}
/* untranslated arcade code reached: show the PC (hex, both buffers) so it can be reported */
UI static void bad_draw(u8 *fb, u32 pc)
{
    for (int y = 0; y < 16; y++) for (int x = 0; x < 6 * 8 + 4; x++) fb[FB_LINE(y + 4) + 4 + x] = 254;
    for (int i = 0; i < 6; i++) {
        u16 g = font3x5[(pc >> (20 - 4 * i)) & 15];
        for (int cy = 0; cy < 5; cy++) for (int cx = 0; cx < 3; cx++)
            if ((g >> (14 - (cy * 3 + cx))) & 1)
                for (int k = 0; k < 4; k++) fb[FB_LINE(6 + cy * 2 + (k >> 1)) + 6 + i * 8 + cx * 2 + (k & 1)] = 255;
    }
}
UI void render_bad(u32 pc)
{
    (&MARS_CRAM)[254] = 0x0000; (&MARS_CRAM)[255] = 0x7FFF;
    for (int n = 0; n < 2; n++) {
        bad_draw((u8 *)&MARS_FRAMEBUFFER, pc);
        u16 cur = MARS_VDP_FBCTL & MARS_VDP_FS;
        MARS_VDP_FBCTL = cur ^ 1;
        for (u32 t = 0; t < 2000000 && (MARS_VDP_FBCTL & MARS_VDP_FS) == cur; t++) ;
    }
}
static u32 tm_acc[4]; static volatile u32 tms[3];
static void clear_cells(u8 *fb, int y0, int y1)
{
    u32 s1r = CT(&s1prev)[0];
    for (int r = y0 >> 3; r <= (y1 - 1) >> 3 && r < 22; r++) {   /* rows 22+: filled (HUD) */
        int ya = r * 8 < y0 ? y0 : r * 8, yb = r * 8 + 8 > y1 ? y1 : r * 8 + 8;
        u32 c0, c1;
        if (CT(&s1cmon)[0]) {
            c0 = CT(&s1cm[r][0])[0]; c1 = CT(&s1cm[r][1])[0];
            /* scroll1 cells written by the game logic since this frame's s1_refresh (it runs on the master meanwhile) */
            u32 row = ((16 + s1y) >> 3) + r & 63, g = CT(&s1dirty[row])[0];
            if (row >= 2 && row <= 4 && (CT(&s1dirty[2])[0] | CT(&s1dirty[3])[0] | CT(&s1dirty[4])[0])) g = 0xFF, c0 = 0xFFFFFFFFu;
            s1xg[cpu_id()][r] = g;
            if (g && ~c0) { u32 cs = ((96 + s1x) >> 3) & 63; for (int c = 0; c < 41; c++) if (g & bit8[((cs + c) >> 3) & 7]) { if (c < 32) c0 |= bit32[c]; else c1 |= bit32[c & 31]; } }
            if (!~c0) { clear_band(fb, ya, yb); continue; }
        }
        else {
        if (s1r & bit32[r]) { clear_band(fb, ya, yb); continue; }
        c0 = CT(&covprev[r][0])[0]; c1 = CT(&covprev[r][1])[0];
        }
        /* V3.15: runs of cells, one loop per run and line (the harbour: ~13K instructions a frame cell by cell) */
        c1 &= 0xFF;                                  /* (columns 40+ are off the screen) */
        for (int b = 0; c0 | c1; ) {
            if (!(c0 & 1)) { if (!(c0 & 255)) { c0 = (c0 >> 8) | (c1 << 24); c1 >>= 8; b += 8; } else { c0 = (c0 >> 1) | (c1 << 31); c1 >>= 1; b++; } continue; }
            int e = b;
            while (c0 & 1) { c0 = (c0 >> 1) | (c1 << 31); c1 >>= 1; e++; }
            int x = b * 8, n = (e - b) * 2;
            if (x + n * 4 > SCR_W) n = (SCR_W - x) >> 2;
            for (int y = ya; y < yb; y++) { u32 *d = (u32 *)(fb + FB_LINE(y) + x); int k = 0; for (; k + 2 <= n; k += 2) { d[k] = 0; d[k + 1] = 0; } if (k < n) d[k] = 0; }
            b = e;
        }
    }
}
/* V3.13: the back buffer cleared by the 32X VDP's fill (one CPU, before both start drawing): the rows that will be
   drawn (not skipped), 0 = transparent, the bottom HUD rows opaque black (colour 255) where band_run filled them.
   A fill stays inside its 256-word block (the hardware wraps there): one per block. */
#if defined(HWCLRON) && !defined(SKIPCHK)
#define HWCLR 1
static void hw_fill(u32 w, u32 we, u16 v)        /* framebuffer words [w, we) */
{
    while (w < we) {
        u32 n = 256 - (w & 255); if (n > we - w) n = we - w;
        while (MARS_VDP_FBCTL & MARS_VDP_FEN) ;
        MARS_VDP_FILLEN = n - 1; MARS_VDP_FILADR = w; MARS_VDP_FILDAT = v;
        w += n;
    }
}
static void hw_preclear(void)
{
    u32 m = CT(&skipmask)[0];
    int blk = P->ui_mode < 2 && ((P->bgon && !(flags & 0x10)) || (!P->bgon && P->bgtail));
    int r = 0;
    while (r < 28) {
        if (m & bit32[r]) { r++; continue; }
        u16 v = blk && r >= 22 ? 0xFFFF : 0;
        int e = r + 1;
        while (e < 28 && !(m & bit32[e]) && ((blk && e >= 22) ? 0xFFFF : 0) == v) e++;
        hw_fill((0x200 + r * 8 * SCR_W) >> 1, (0x200 + e * 8 * SCR_W) >> 1, v);
        r = e;
    }
    while (MARS_VDP_FBCTL & MARS_VDP_FEN) ;
}
#else
#define HWCLR 0
#endif
static void band_run(u8 *fb, int y0, int y1)
{
    u32 t0 = wdt();
#ifdef SKIPCHK
    if (skchk[cpu_id()]) clear_band(fb, y0, y1); else
#endif
    if (HWCLR) ;
    else if (P->bgon && CT(&pclr)[0] && P->ui_mode < 2 && !(flags & 0x10)) clear_cells(fb, y0, y1);
    else clear_band(fb, y0, y1);
    if (!(HWCLR) && !P->bgon && P->bgtail && y1 > 176 && P->ui_mode < 2)   /* V3.2: under the field opaque black (the MD planes' rows there are not loaded) */
        for (int y = y0 > 176 ? y0 : 176; y < y1; y++) { u32 *d = (u32 *)(fb + FB_LINE(y)); for (int i = 0; i < SCR_W / 4; i += 4) { d[i] = 0xFFFFFFFFu; d[i + 1] = 0xFFFFFFFFu; d[i + 2] = 0xFFFFFFFFu; d[i + 3] = 0xFFFFFFFFu; } }
    u32 t1 = wdt(), t2, t3;
    if (P->ui_mode >= 2) {
        ui_draw(fb, y0, y1);
        t2 = wdt();
    } else if (flags & 0x10) {
        for (int k = 0; k < 4; k++) {
            int L = (layerctl >> (6 + 2 * k)) & 3;
            if (L == 0) { spr_dx = P->layout == 1 ? 16 : P->layout == 5 ? 8 : 0; draw_sprites(fb, y0, y1); spr_dx = 0; }
            else if (L == 1) { if (P->layout != 4) draw_scroll1_layout(fb, y0, y1); }
            else if (!(L == 3 && P->pic >= 9 && P->pic != 0xFF)) draw_tilemap23_scaled(fb, L, y0, y1);   /* V3.0: the stage-clear pictures: scroll3 by the MD */
        }
        if (P->layout == 4) draw_scroll1_layout(fb, y0, y1);   /* V3.13: continue screen: the text over the picture (the translated
                                                                  line's 24x24 blocks reach above the arcade's 16 px line, under its edge) */
        if (P->ui_mode == 1) ui_draw(fb, y0, y1);
        t2 = wdt();
    } else if (P->bgon) {
        if (y1 > 176 && !(HWCLR)) {     /* the MD planes have nothing below the field: opaque black under the HUD (it goes at the stage end) */
            u32 hs = CT(&hudsk)[0];
            for (int y = y0 > 176 ? y0 : 176; y < y1; y++) {
                u32 *d = (u32 *)(fb + FB_LINE(y)); int R = y >> 3, i0 = 0, i1 = SCR_W / 4;
                if (R < 28 && ((hs >> (R - 22)) & 1)) { i0 = 41; fb[FB_LINE(y) + 161] = fb[FB_LINE(y) + 162] = fb[FB_LINE(y) + 163] = 0xFF; }   /* V3.15: 1P side kept */
                else if (R < 28 && ((hs >> (R - 14)) & 1)) { i1 = 40; fb[FB_LINE(y) + 160] = 0xFF; }                                         /* 2P side kept */
                for (int i = i0; i < i1; i++) d[i] = 0xFFFFFFFFu; }
        }
        draw_sprites(fb, y0, y1);
#ifndef NOPRI
        draw_bgpri(fb, y0, y1);
#endif
        t2 = wdt();
        draw_scroll1(fb, y0, y1);
    } else {
        /* arcade layer order, back to front (CPS-B layer control bits 6-13) */
        for (int k = 0; k < 4; k++) {
            int L = (layerctl >> (6 + 2 * k)) & 3;
            if (L == 0) draw_sprites(fb, y0, y1);
            else if (L == 1) draw_scroll1(fb, y0, y1);
            else draw_tilemap23(fb, L, y0, y1);
        }
        t2 = wdt();
    }
    t3 = wdt();
    if (!cpu_id()) { tm_acc[0] += (t1 - t0) & 255; tm_acc[1] += (t2 - t1) & 255; tm_acc[2] += (t3 - t2) & 255; }
    else { volatile u32 *q = CT(&tms[0]); q[0] += (t1 - t0) & 255; q[1] += (t2 - t1) & 255; q[2] += (t3 - t2) & 255; }
}
#ifdef SKIPCHK   /* test: the drawn rows hold what a full clear + draw gives */
UI2 static void chk_rows(u8 *fb, int y, int e)
{
        for (int ya = y & ~7; ya < e; ya += 8) {
            u8 *sc = fb + 0x12000 + cpu_id() * 0x1000; volatile u32 *K = (volatile u32 *)0x26007000; int bad = 0, yb = ya + 8 > e ? e : ya + 8, yy = ya < y ? y : ya;
            for (int q = yy; q < yb; q++) for (int x = 0; x < SCR_W; x += 4) *(u32 *)(sc + (q - ya) * SCR_W + x) = *(u32 *)(fb + FB_LINE(q) + x);
            skchk[cpu_id()] = 1; band_run(fb, yy, yb); skchk[cpu_id()] = 0; K[24]++;
            for (int q = yy; q < yb; q++) for (int x = 0; x < SCR_W; x += 4) if (*(u32 *)(sc + (q - ya) * SCR_W + x) != *(u32 *)(fb + FB_LINE(q) + x)) { if (!bad) { K[29] = *(u32 *)(sc + (q - ya) * SCR_W + x); K[30] = *(u32 *)(fb + FB_LINE(q) + x); K[26] = (q << 16) | x; } bad = 1; }
            if (bad) { K[25]++; if (yy < 176 && P->bgon && P->seq > 18700) { u32 n = K[28]; if (n < 80) { K[32 + n * 3] = P->seq; K[33 + n * 3] = K[26]; K[34 + n * 3] = K[29]; K[300 + n] = K[30]; K[28] = n + 1; } } } }
}
#endif
UI2 static void render_band(int y0, int y1)   /* (V2.5: from the cart, a small loop: room in SDRAM) */
{
    u8 *fb = (u8 *)&MARS_FRAMEBUFFER;
    u32 m = CT(&skipmask)[0];
    int y = y0;
    while (y < y1) {
        int r = y >> 3;
        if (r < 28 && (m & bit32[r])) {
#ifdef SKIPCHK   /* test: a skipped row holds what drawing it would give */
            { u8 *sc = fb + 0x12000 + cpu_id() * 0x1000; volatile u32 *K = (volatile u32 *)0x26007000; int ya = r * 8, bad = 0;
              for (int q = 0; q < 8; q++) for (int x = 0; x < SCR_W; x += 4) *(u32 *)(sc + q * SCR_W + x) = *(u32 *)(fb + FB_LINE(ya + q) + x);
              skchk[cpu_id()] = 1; band_run(fb, ya, ya + 8); skchk[cpu_id()] = 0; K[20]++;
              for (int q = 0; q < 8; q++) for (int x = 0; x < SCR_W; x += 4) if (*(u32 *)(sc + q * SCR_W + x) != *(u32 *)(fb + FB_LINE(ya + q) + x)) { if (!bad && r < 22 && P->bgon) { u32 n = K[99]; if (n < 60) { K[100 + n * 4] = P->seq; K[101 + n * 4] = (r << 16) | (q << 12) | x; K[102 + n * 4] = *(u32 *)(sc + q * SCR_W + x); K[103 + n * 4] = *(u32 *)(fb + FB_LINE(ya + q) + x); K[99] = n + 1; } } bad = 1; K[22] = (r << 16) | x; }
              if (bad) { K[21]++; K[23] = r; K[64 + r]++; if (r < 22 && P->bgon && P->seq > 18700) { u32 n = K[28]; if (n < 120) { K[32 + n * 2] = P->seq | 0x80000000u; K[33 + n * 2] = K[22]; K[28] = n + 1; } } } }
#endif
            y = (r + 1) * 8; continue; }
        int e = y;
        while (e < y1) { int q = e >> 3; if (q < 28 && (m & bit32[q])) break; e = (q + 1) * 8; }
        if (e > y1) e = y1;
        if (P->bgon && !cpu_id()) {                 /* master in game: keep the MD messages going */
            while (y < e) { int f = y + 16 > e ? e : y + 16; band_run(fb, y, f);
#ifdef SKIPCHK
                chk_rows(fb, y, f);
#endif
                anim_send(); y = f; }
            continue;
        }
        band_run(fb, y, e);
#ifdef SKIPCHK
        chk_rows(fb, y, e);
#endif
        y = e;
    }
    if (FPSON && y0 == 0) fps_draw(fb);
#ifdef DBGLAY
    if (y0 == 0) { const u8 *w = WRAM_PTR; draw_num(fb, 150, w[0x81D7]); draw_num(fb, 200, w[0x81D8]); draw_num(fb, 250, P->layout);
      }
#endif
}
#ifndef NOWS
static int ws_claim(int me, int *nr)
{
    volatile int *F = CT(&ws_flag[0]), *T = CT(&ws_turn), *top = CT(&ws_top), *bot = CT(&ws_bot);
    u32 sr; __asm__ volatile("stc sr,%0" : "=r"(sr)); __asm__ volatile("ldc %0,sr" :: "r"(sr | 0xF0));   /* (the logic must not run in here) */
    F[me] = 1; *T = 1 - me;
    while (F[1 - me] && *T == 1 - me) ;
    int b = -1, t = *top, u = *bot;
#ifdef WSROWS
    /* V3.16: in 8-line rows: whole bands (WSB lines) while plenty is left, then smaller pieces, the last ones a row
       each: both CPUs finish together (the master waited ~9 ticks a frame for the slave's last 32-line band) */
    if (t < u) { int n = u - t, k = n > WSR_A ? WSB / 8 : n > 1 ? WSR_MIN : 1;
        if (me) { b = u - k; *bot = b; } else { b = t; *top = t + k; }
        *nr = k; }
#else
    if (t < u) { if (me) { b = u - 1; *bot = b; } else { b = t; *top = t + 1; } }
#endif
    F[me] = 0;
    __asm__ volatile("ldc %0,sr" :: "r"(sr));
    return b;
}
#ifndef WSLATE
#define WSLATE 44                 /* V3.16: 40 -> 44 (the pieces near the end are small now: tried 36-45) */
#endif
static void ws_render(int me)
{
    extern volatile u32 vint_t;
    for (;;) {
        /* the master stops taking bands late in the frame: the next vblank's game logic would hold its band (and
           the slave waiting for it) for most of a frame (WDT: 47 ticks a frame) */
        if (!me && ((wdt() - vint_t) & 255) >= WSLATE) return;
        int n = 1, b = ws_claim(me, &n);
        if (b < 0) return;
#ifdef WSROWS
        render_band(b * 8, (b + n) * 8);
#else
        render_band(b * WSB, b * WSB + WSB > SCR_H ? SCR_H : b * WSB + WSB);
#endif
    }
}
#endif
UI static void init_linetable(void)
{
    volatile u16 *fb = (volatile u16 *)&MARS_FRAMEBUFFER;
    for (int y = 0; y < 256; y++) fb[y] = FB_LINE(y < SCR_H ? y : SCR_H - 1) / 2;
    for (int i = 0x100; i < 0x10000; i++) fb[i] = 0;
}
static void bg_publish(void)
{
    static u16 seq;
    seq = (seq + 1) & 0xFF; if (!seq) seq = 1;
    MARS_SYS_COMM2 = 0;
    MARS_SYS_COMM4 = P->cam[0]; MARS_SYS_COMM6 = P->cam[1];
    if (P->pic != 0xFF) { MARS_SYS_COMM12 = P->pic; MARS_SYS_COMM14 = P->piclv; }     /* V2.1: a picture shown by the MD */
    else { MARS_SYS_COMM12 = P->cam[2]; MARS_SYS_COMM14 = P->cam[3]; }
    MARS_SYS_COMM2 = seq | (P->bgon ? 0x100 : 0) | (P->stage << 9) | (P->wantv ? 0x4000 : 0) | (P->pic != 0xFF ? 0x8000 : 0);
}
static void flip_request(void)
{
    bg_publish();
    flip_from = MARS_VDP_FBCTL & MARS_VDP_FS;
#ifdef FBID
    ((volatile u32 *)0x260076F0)[flip_from ^ 1] = P->lid;
#endif
    flip_pending = 1;
    MARS_VDP_FBCTL = flip_from ^ 1;
    if ((MARS_VDP_FBCTL & MARS_VDP_FS) != flip_from && flip_pending) pal_upload();
}
UI void render_init(void)
{
    bgpri_init(0);
    scale_init();
    for (int i = 0; i < 32; i++) s1slot[i] = -1;
    for (int i = 0; i < 16; i++) s1pt_pal[i] = 0xFF;
    for (int b = 0; b < 16; b++) for (int v = 0; v < 16; v++) t5[b][v] = (v * 0x11 * (0x0f + b * 2) / 0x2d) >> 3;
    for (int k = 0; k < 0x4000; k++) if (S1_TAB[k]) s1blank[k >> 3] |= 1 << (k & 7);
    *(volatile u16 *)0xFFFFFE80 = 0xA51F; *(volatile u16 *)0xFFFFFE80 = 0x5A00; *(volatile u16 *)0xFFFFFE80 = 0xA53F;   /* WDT free-running, /8192? */
#ifndef HINTLOGIC
    *(volatile u8 *)0x20004001 = 0x8A;          /* VINT, CMD */
#else
    *(volatile u8 *)0x20004005 = HLINE;         /* H interrupt every HLINE+1 lines (once a frame) */
    *(volatile u8 *)0x20004001 = 0x0E;          /* VINT, HINT (not in the vblank), CMD */
#endif
    MARS_VDP_DISPMODE = MARS_VDP_MODE_256 | MARS_VDP_PRIO_32X;
    init_linetable();
    { u16 cur = MARS_VDP_FBCTL & MARS_VDP_FS;
      MARS_VDP_FBCTL = cur ^ 1;
      while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) ; }
    init_linetable();
    { volatile u32 *ov = (volatile u32 *)&MARS_OVERWRITE_IMG, *fb = (volatile u32 *)&MARS_FRAMEBUFFER, *sd = (volatile u32 *)0x26021000;
      u32 t0 = wdt(); for (int i = 0; i < 20000; i++) ov[0x100 + (i & 4095)] = 0; u32 t1 = wdt();
      for (int i = 0; i < 20000; i++) fb[0x100 + (i & 4095)] = 0; u32 t2 = wdt();
      for (int i = 0; i < 20000; i++) sd[i & 4095] = 0; u32 t3 = wdt();
      { volatile const u32 *rom = (volatile const u32 *)0x02400000 - 0x100000; u32 acc = 0; for (int i = 0; i < 20000; i++) acc += rom[(i * 37) & 0x3FFFF]; bench_acc = acc; } u32 t4 = wdt();
      bench[0] = (t1 - t0) & 255; bench[1] = (t2 - t1) & 255; bench[2] = (t3 - t2) & 255; bench[3] = (t4 - t3) & 255; }
    CT(&boot_done)[0] = 0x600DB007;
}
/* Rows (8 lines) that are unchanged since this back buffer was last drawn and have no sprite
 * (now or then) are left as they are. */
static void compute_skip(void)
{
    static u32 lastsig[2][29], lastspr[2], lastkey[2], valid[2], lastsph[2], lastpv[2], pok[2], lasthk[2];
    u32 sph = nspr * 0x9E3779B1u;
    if (flags & 0x10) { const u32 *q = (const u32 *)spr; for (u32 i = 0; i < nspr * 2; i++) sph = (sph ^ q[i]) * 0x01000193u; }
    u32 buf = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;   /* the back buffer after the pending flip */
    u8 chg[64];                                     /* V2.8: scroll1 groups changed since this buffer was drawn */
    for (int r = 0; r < 64; r++) { chg[r] = s1chg[buf][r]; s1chg[buf][r] = 0; }
    u32 spm = 0;
    for (u32 i = 0; i < nspr; i++) {
        const u16 *e = &spr[i * 4];
        int y = e[1]; u32 colour = e[3];
        int ny = (colour & 0xff00) ? ((colour >> 12) & 15) + 1 : 1;
        for (int k = 0; k < ny; k++) {
            int sy = ((y + k * 16) & 0x1ff) - 16;
            if (sy + 16 <= 0 || sy >= SCR_H) continue;
            int ra = sy < 0 ? 0 : sy >> 3, rb = (sy + 15) >> 3;
            if (rb > 28) rb = 28;
            for (int r = ra; r <= rb; r++) spm |= bit32[r];
        }
    }
    /* priority cells are drawn over sprites from map cells that are not on the screen's 8-line rows: they reach
       one row above / below the sprite rows (left there when that row was skipped later) */
#ifndef NODIL
#ifdef OLDDIL
    if (P->bgon) spm |= (spm << 1) | (spm >> 1);
#else
    if (P->bgon) spm |= ((spm << 1) & ((flags & 8) ? 0x003FFFFFu : 0xFFFFFFFFu)) | (spm >> 1);
#endif   /* V2.8: not into the bottom HUD (no priority cells there) */
#endif
    u32 key = (s1x << 16) ^ s1y ^ (flags << 24) ^ (layerctl << 8) ^ (s1epoch * 0x9E3779B1u) ^ (P->bgon << 30) ^ ((u32)P->layout << 21);
    key ^= ((u32)P->ui_mode << 3) ^ ((u32)P->ui_sel << 7) ^ ((u32)P->ui_opt[0] << 11) ^ ((u32)P->ui_opt[1] << 14) ^ ((u32)P->ui_opt[2] << 17) ^ ((u32)P->ui_opt[3] << 21) ^ ((u32)P->ui_opt[4] << 24) ^ ((u32)P->ui_opt[5] << 5) ^ ((u32)P->ui_opt[6] << 30) ^ ((u32)P->ui_fsa << 31) ^ ((u32)P->ui_pad * 0x9E3779B1u) ^ ((u32)(P->ui_hold / 60) << 27);
    if (P->ui_mode >= 2) { CT(&hudsk)[0] = 0; CT(&skipmask)[0] = 0; CT(&s1cmon)[0] = 0; valid[buf] = 0; pok[buf] = 0; return; }
    key ^= (u32)P->pic * 0x2545F491u;
    if (!P->bgon) key ^= ((u32)P->sc23[0] * 0x10001u) ^ ((u32)P->sc23[1] << 7) ^ ((u32)P->sc23[2] << 13) ^ ((u32)P->sc23[3] << 19) ^ (P->vc << 3) ^ P->b2 ^ (P->b3 << 1) ^ (P->ep23 * 0x85EBCA6Bu) ^ (lut23ver * 0xC2B2AE35u);
#ifdef KEYDBG
    { static u32 ls[8]; u32 cur[8] = { s1x, s1y, flags, layerctl, s1epoch, P->bgon, P->layout, P->pic }; volatile u32 *K = (volatile u32 *)0x26007004;
      for (int i = 0; i < 8; i++) { if (cur[i] != ls[i]) K[12 + i]++; ls[i] = cur[i]; } }
#endif
    { static u8 ltail; if (P->bgtail != ltail) { ltail = P->bgtail; valid[0] = valid[1] = 0; } }
#ifdef DBGTAIL
    if (P->bgtail) (*(volatile u32 *)0x26007300)++;
#endif   /* V3.18: the black under the field (bgtail) on / off:
                                     both buffers drawn again (rows kept in one buffer kept the black: the stage-clear pictures' subtitle band flickered) */
    u32 m = 0;
    u32 pv = 0; for (int i = 0; i < 32; i++) pv = (pv ^ pver[i]) * 0x01000193u;
    int same = valid[buf] && key == lastkey[buf] && pv == lastpv[buf] && !((16 + s1y) & 7) && !((96 + s1x) & 7) && !(flags & 0x14);
    lastpv[buf] = pv;
#ifdef CMCHK
    { volatile u32 *K = (volatile u32 *)0x26007000; K[4]++; if (!valid[buf]) K[5]++; else if (key != lastkey[buf]) K[6]++; else if (pv != lastpv[buf]) K[7]++; if ((16 + s1y) & 7) K[8]++; if ((96 + s1x) & 7) K[9]++; if (flags & 0x14) K[10]++; if (!P->bgon) K[11]++; }
#endif
    { int ch = 0; for (int r = 0; r < 28; r++) if (rowsig[r] != lastsig[buf][r]) ch++; dbg_rowch = ch; dbg_keych += key != lastkey[buf]; dbg_sprrows = __builtin_popcount(spm); }
    if (valid[buf] && key == lastkey[buf] && !((16 + s1y) & 7))
        for (int r = 0; r < 28; r++)
            if (rowsig[r] == lastsig[buf][r] && !((spm | lastspr[buf]) & bit32[r])) m |= bit32[r];
    /* V3.13/V3.15: the bottom HUD rows (rowsig is per screen row) with their own key: not s1y's whole cells (stage 3's
       rain scrolls scroll1 down 8 lines at a time) and, instead of s1epoch (any scroll1 palette's slots changed: the
       harbour flames every frame), the versions of the palettes the HUD rows use */
    {   u32 r0 = (16 + s1y) >> 3, pm = 0, hk;
        for (int r = 22; r < 28; r++) { u32 row = (r0 + r) & 63; for (int g = 0; g < 8; g++) pm |= grp_used[row][g]; }
        hk = (key ^ s1y ^ (s1epoch * 0x9E3779B1u)) + pm;
        for (int i = 0; i < 32; i++) if (pm & bit32[i]) hk = (hk ^ pver[i]) * 0x01000193u;
        int hsame = 1;   /* V3.15: the HUD rows together (a row's substitutions read other HUD rows: the digit palette in row 24, the prompt in row 27) */
        for (int r = 22; r < 29; r++) if (rowsig[r] != lastsig[buf][r]) hsame = 0;
        if (valid[buf] && (flags & 8) && hk == lasthk[buf] && !((16 + s1y) & 7) && hsame)
            for (int r = 22; r < 28; r++)
                if (!((spm | lastspr[buf]) & bit32[r])) m |= bit32[r];
        {   /* V3.15: the HUD's halves (1P panels x 0-160, 2P x 161-319): a HUD row whose cells changed on one side only
               is redrawn on that side (the 1P score, the 2P prompt) */
            static u32 lastL[2], lastR[2]; u32 hl = 0x811C9DC5u, hr = 0x811C9DC5u, sl = 0, sr = 0, hc = (64 + s1x) >> 3;
            if (flags & 8) for (int r = 22; r < 29; r++) {
                u32 row = (r0 + r) & 63; const u32 *rp = (const u32 *)(s1base + (((row & 0x1f) + ((row & 0x20) << 6)) << 2));
                for (int c = 0; c < 25; c++) hl = (hl ^ rp[((hc + c) & 63) << 5]) * 0x01000193u;
                for (int c = 25; c < 48; c++) hr = (hr ^ rp[((hc + c) & 63) << 5]) * 0x01000193u; }
            if (valid[buf] && (flags & 8) && !(flags & 0x40) && hk == lasthk[buf] && !((16 + s1y) & 7) && (hl == lastL[buf]) != (hr == lastR[buf]))
                for (int r = 22; r < 28; r++) if (!(m & bit32[r]) && !((spm | lastspr[buf]) & bit32[r])) { if (hl == lastL[buf]) sl |= bit32[r - 22]; else sr |= bit32[r - 22]; }
            lastL[buf] = hl; lastR[buf] = hr; CT(&hudsk)[0] = sl | (sr << 8); }
        lasthk[buf] = hk; }
    if (flags & 0x10) {
        /* scaled scene: an output row (8 lines) is kept when the layers are unchanged and the
         * scroll1 rows / sprite rows (source rows) that map into it are unchanged and empty of sprites */
        m = 0;
        if (valid[buf] && key == lastkey[buf]) {
            u32 ch = 0, sp = sph == lastsph[buf] ? 0 : spm | lastspr[buf];
            for (int r = 0; r < 29; r++) if (rowsig[r] != lastsig[buf][r]) ch |= bit32[r];
            /* scroll1 and sprites are drawn unscaled (moved by the layout): a changed source row
             * dirties the output rows it lands on (the name of the character pages moves down) */
            int fy = (16 + s1y) & 7;
            u32 dirty = sp | (sp << 1);
            for (int r = 0; r < 29; r++) if (ch & bit32[r]) {
                int y = r * 8 - fy;
                if (P->layout == 3 && r >= 8 && r <= 19) { dirty = 0xffffffffu; break; }
                for (int k = 0; k < 2; k++) {
                    int yy = y + (k ? 184 : P->layout != 1 ? 0 : r <= 5 ? (r >> 1) * 4 : 16);   /* V2.2: where s1rule moves it */
                    if (k && P->layout != 1) break;
                    if (yy + 8 > 0 && yy < SCR_H) { int a = yy < 0 ? 0 : yy >> 3, b = (yy + 7) >> 3; if (b > 27) b = 27; for (int q = a; q <= b; q++) dirty |= bit32[q]; }
                }
            }
            m = ~dirty & 0x0fffffffu;
        }
    }
    if (FPSON) m &= ~3u;                 /* the fps digits */
#ifdef SKIPHIST
    { volatile u32 *T = (volatile u32 *)0x26007004; for (int r = 0; r < 28; r++) { if (m & bit32[r]) T[30 + r]++; if ((spm | lastspr[buf]) & bit32[r]) T[60 + r]++; if (rowsig[r] != lastsig[buf][r]) T[90 + r]++; } T[29]++; }
#endif
    for (int r = 0; r < 29; r++) lastsig[buf][r] = rowsig[r];
    lastspr[buf] = spm; lastkey[buf] = key; valid[buf] = 1; dbg_spm = spm; lastsph[buf] = sph;
#ifdef NOSKIP
    m = 0;
#endif
    CT(&skipmask)[0] = m;
    /* in game with the MD background: only the 8x8 cells drawn last time in this buffer are cleared */
    { static u32 covlast[2][28][2] RD1, s1last[2];
      int part = P->bgon && P->ui_mode < 2 && !(flags & 0x10);
      CT(&pclr)[0] = part && pok[buf];
      same = same && part && pok[buf];
      pok[buf] = part;
      u32 s1r = s1last[buf]; s1last[buf] = rowhas;
      CT(&s1prev)[0] = s1r | (s1r << 1);
      u32 h[28][2];                               /* cells drawn: the sprites' cells and their neighbours (priority cells overlap) */
      for (int r = 0; r < 28; r++) { u32 a = cov[r][0], b = cov[r][1];
          h[r][0] = a | (a << 1) | (a >> 1) | (b << 31); h[r][1] = b | (b << 1) | (b >> 1) | (a >> 31); }
      for (int r = 0; r < 28; r++) { CT(&covprev[r][0])[0] = covlast[buf][r][0]; CT(&covprev[r][1])[0] = covlast[buf][r][1];
          covlast[buf][r][0] = h[r][0] | (r ? h[r - 1][0] : 0) | (r < 27 ? h[r + 1][0] : 0);
          covlast[buf][r][1] = h[r][1] | (r ? h[r - 1][1] : 0) | (r < 27 ? h[r + 1][1] : 0); }
#ifdef CELLMODE   /* V2.9: off (stale cells in rows the cell mode trusted; little gain) */
      /* V2.8 cell mode (scroll1 on the 8x8 grid, the same scroll / layers / palettes as this buffer's last frame):
       * a row's scroll1 cells are cleared / drawn only where they changed (by 8-cell group) or sprites were / are */
      if (same) {
          u32 c0 = ((96 + s1x) >> 3) & 63, r0 = (16 + s1y) >> 3;
          u32 gm[8][2];
          for (int g = 0; g < 8; g++) gm[g][0] = gm[g][1] = 0;
          for (int c = 0; c < 41; c++) gm[((c0 + c) >> 3) & 7][c >> 5] |= bit32[c & 31];
          for (int r = 0; r < 22; r++) {
              u32 row = (r0 + r) & 63, g = chg[row], a = 0, b = 0;
              if (row >= 2 && row <= 4 && (chg[2] | chg[3] | chg[4])) a = b = 0xFFFFFFFFu;   /* the top HUD (cells moved between these rows) */
              else for (int k = 0; k < 8; k++) if (g & bit8[k]) { a |= gm[k][0]; b |= gm[k][1]; }
              u32 pa = CT(&covprev[r][0])[0], pb = CT(&covprev[r][1])[0];
              u32 inrow = (s1r | (s1r << 1)) & bit32[r];
              CT(&s1cm[r][0])[0] = inrow ? ((a == 0xFFFFFFFFu) ? a : a | pa) : pa;
              CT(&s1cm[r][1])[0] = inrow ? b | pb : pb;
              CT(&s1dm[r][0])[0] = a | pa | covlast[buf][r][0];
              CT(&s1dm[r][1])[0] = (b | pb | covlast[buf][r][1]) & 0x1FFu;
          }
      }
      CT(&s1cmon)[0] = same;
#endif
    }
}
/* Frame pipeline: the master picks the newest packet and builds its palette while the last
 * frame waits for its flip; at the vblank the master runs the game logic (interrupt) while the
 * slave already starts drawing (after the flip).  So a packet is shown one frame later than
 * when both waited for the logic, but the slave is never idle during the logic. */
static volatile int go2; static volatile u32 slave_rt, slave_pt;
void render_loop(void)
{
    u32 seen = 0, n = 0;
    int sy = SCR_H / 2;
    for (;;) {
        while (pk_seq == seen) anim_send();
        __asm__ volatile("stc sr,r0; or #0xF0,r0; ldc r0,sr" ::: "r0");
        pk_r ^= 1; seen = pk_seq;
        __asm__ volatile("stc sr,r0; and #0x0F,r0; ldc r0,sr" ::: "r0");
        P = PK[pk_r]; TRC(3)
#ifdef DYNLATE   /* (tried: slower) */
        {   /* V3.15: the harbour's dynamic cells scanned here, not in the game logic (the master's free time at the end
               of a frame instead of the start, where the logic holds it and the slave draws alone); interrupts off:
               the logic must not write cells between a scan reading them and clearing their marks */
            u32 sr; __asm__ volatile("stc sr,%0" : "=r"(sr)); __asm__ volatile("ldc %0,sr" :: "r"(sr | 0xF0));
            dyn3_scan(P);
#ifndef NOD2
            dyn2_scan(P);
#endif
            __asm__ volatile("ldc %0,sr" :: "r"(sr)); }
#endif
        { extern volatile u16 fl_drawn; fl_drawn = P->flrow; }    /* V3.11: the flame row now on screen */
        u32 tq0 = wdt();
        n++;
        CT(&split_y)[0] = sy;
#ifndef NOWS
#ifdef WSROWS
        CT(&ws_top)[0] = 0; CT(&ws_bot)[0] = SCR_H / 8;
#else
        CT(&ws_top)[0] = 0; CT(&ws_bot)[0] = (SCR_H + WSB - 1) / WSB;
#endif
#endif
        CT(&frame_go)[0] = n;                   /* slave: palette, then (after the flip) skip mask + CRAM */
        while (CT(&go2)[0] != (int)n) anim_send();
#ifdef TSTAT
        { volatile u32 *T = (volatile u32 *)0x26007004; T[0]++; T[1] += (wdt() - tq0) & 255; }
#endif
        cache_purge();                          /* the slave's palette tables */
        u32 tq1 = CT(&slave_pt)[0];
        anim_send();
        u32 tq2 = wdt(), lq2 = logic_ticks; TRC(4)
#ifndef NOWS
        ws_render(0);
#else
        render_band(0, sy);
#endif
        anim_send();
        TRC(5) CT(&master_done)[0] = n;
        u32 tw = wdt();
        while (CT(&slave_done)[0] != (int)n) anim_send();
        int wm = (wdt() - tw) & 255, wsl = CT(&slave_time)[0];
#ifdef TSTAT
        { volatile u32 *T = (volatile u32 *)0x26007004; T[2] += (tw - tq2) & 255; T[3] += (u32)(logic_ticks - lq2); T[4] += wm; T[5] += CT(&slave_rt)[0]; T[6] += wsl; T[7] += tq1; }
#endif
        int mrt = (int)((tw - tq2) & 255) - (int)(logic_ticks - lq2), srt = CT(&slave_rt)[0];   /* render times without the game logic */
        { u32 a = tq1, b = mrt < 0 ? 0 : mrt, c = CT(&slave_rt)[0], d = sy >> 2;
          if (a > tmx[0]) tmx[0] = a; if (b > tmx[1]) tmx[1] = b; if (c > tmx[2]) tmx[2] = c; tmx[3] = d;
          if (++tmn >= 30) { for (int i = 0; i < 4; i++) { tshow[i] = tmx[i]; tmx[i] = 0; } tmn = 0; } }
        /* balance the finishing times: whoever waited gets more lines */
        { int d = (wm - wsl) * 3; if (d > 16) d = 16; if (d < -16) d = -16; (void)srt;
          sy += d; if (sy < 16) sy = 16; if (sy > SCR_H - 16) sy = SCR_H - 16; }
        dbg_split = sy;
        TRC(6) flip_request();
        frames_drawn++;
#ifdef DBG23
        { const u16 *m = (const u16 *)P->b3; int nz = 0, ok = 0; for (int i = 0; i < 0x2000; i += 2) if (m[i]) { nz++; if (tile23(3, m[i])) ok++; } MARS_SYS_COMM10 = nz; MARS_SYS_COMM8 = ok & 0x7FFF; }
#endif
        skacc += __builtin_popcount(CT(&skipmask)[0]);
        dbg_notes = 0;
    }
}
void render_slave(void)
{
    int seen = 0;
    while (CT(&boot_done)[0] != 0x600DB007) ;
    *(volatile u16 *)0xFFFFFE80 = 0xA51F; *(volatile u16 *)0xFFFFFE80 = 0x5A00; *(volatile u16 *)0xFFFFFE80 = 0xA53F;
#ifdef SPROF   /* test: sample the slave PC (crt0 _s_wdt) into 0x26007004-0x260076F3 */
    *(volatile u32 *)0x26007000 = 4; *(volatile u32 *)0x260076F8 = 0;
    *(volatile u16 *)0xFFFFFE80 = 0xA53E;
    *(volatile u16 *)0xFFFFFEE4 = (65 << 8) | (*(volatile u16 *)0xFFFFFEE4 & 0x7F);
    *(volatile u16 *)0xFFFFFEE2 = (*(volatile u16 *)0xFFFFFEE2 & 0xFF0F) | 0x00F0;
    __asm__ volatile("stc sr,r0; and #0x0F,r0; ldc r0,sr" ::: "r0");
#endif
    for (;;) {
        int g;
        while ((g = CT(&frame_go)[0]) == seen) ;
        seen = g;
        cache_purge();
        P = PK[CT(&pk_r)[0]];
        u32 tp0 = wdt();
#ifdef PRESTAT
        int fp0 = CT(&flip_pending)[0];
#endif
        for (int r = 0; r < 29; r++) rowsig[r] = 0;
        rowhas = 0;
        if (P->stage != bgpri_stage) bgpri_init(P->stage);   /* before the palette: its fixed CRAM slots */
        build_palette();                        /* (the master runs the game logic: the slave does this) */
        sprite_cover();
        CT(&slave_pt)[0] = (wdt() - tp0) & 255;
#ifdef PRESTAT
        { volatile u32 *T = (volatile u32 *)0x26007004; u32 pt = (wdt() - tp0) & 255; int fp1 = CT(&flip_pending)[0]; T[60]++;
          if (!fp0) { T[63]++; T[64] += pt; } else if (!fp1) { T[61]++; T[62] += pt; } }
#endif
#ifdef TSTAT
        u32 tf0 = wdt();
#endif
        if (++ptn >= 60) { for (int i = 0; i < 8; i++) { CT(&ptshow[i])[0] = ptm[i]; ptm[i] = 0; } ptn = 0; }
        while (CT(&flip_pending)[0]) ;          /* the back buffer is free after the flip */
#ifdef TSTAT
        { u32 t1 = wdt(); ((volatile u32 *)0x26007004)[8] += (t1 - tf0) & 255; tf0 = t1; }
#endif
        compute_skip();
#if HWCLR
        hw_preclear();
#endif
        { volatile u16 *d = CT(&cram_next[0]); for (int i = 0; i < 256; i++) d[i] = cram_build[i]; }
#ifdef DBGCRAM
        { volatile u16 *d = (volatile u16 *)0x26007200; for (int i = 0; i < 56; i++) d[i] = cram_build[200 + i]; d[56] = bgpri_slot0; d[57] = bgpri_stage; }
#endif
#ifdef TSTAT
        ((volatile u32 *)0x26007004)[9] += (wdt() - tf0) & 255;
#endif
        CT(&go2)[0] = g;
        u32 ts0 = wdt();
#ifndef NOWS
        ws_render(1);
#else
        render_band(CT(&split_y)[0], SCR_H);
#endif
        CT(&slave_rt)[0] = (wdt() - ts0) & 255;
        CT(&slave_done)[0] = g;
        u32 tw = wdt();
        while (CT(&master_done)[0] != g) ;
        CT(&slave_time)[0] = (wdt() - tw) & 255;      /* how long the slave waited */
    }
}
