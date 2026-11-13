/* Native versions of arcade routines (V2.6), entered from cpu_run in place of the translated code.
   Each one does exactly what the 68000 code does: the same memory writes (the stack included), the same
   registers and flags at its rts.  Checked against the translated code on the host (host/xref_split
   NATCHK=1: both run every frame, WRAM / OBJ RAM / registers compared). */
#include "rt.h"

#ifdef HOST
#define NH
#else
#define NH __attribute__((section(".nathot")))
#endif
#define SXW(v) ((u32)(s32)(s16)(v))
#define LO(r, v) ((r) = ((r) & 0xFFFF0000u) | ((v) & 0xFFFFu))

/* WRAM (page 0xFF) accesses straight to SDRAM; the host checks the page */
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
static void wchk(u32 a) { if (((a >> 16) & 0xFF) != 0xFF) { fprintf(stderr, "nat: not WRAM %06X\n", a & 0xFFFFFF); abort(); } }
#define FB(a) (wchk(a), RB(a))
#define FW(a) (wchk(a), RW(a))
#define FL(a) (wchk(a), RL(a))
#define FWB(a, v) (wchk(a), WB(a, v))
#define FWW(a, v) (wchk(a), WW(a, v))
#define FWL(a, v) (wchk(a), WL(a, v))
#else
#define FP(a) ((u8 *)0x06000000 + ((a) & 0xFFFF))
#define FB(a) (*(volatile u8 *)FP(a))
#define FW(a) (*(u16 *)FP(a))
#define FL(a) (((u32)FW(a) << 16) | FW((a) + 2))
#define FWB(a, v) (*FP(a) = (v))
#define FWW(a, v) (*(u16 *)FP(a) = (v))
#define FWL(a, v) do { u32 _a = (a), _v = (v); FWW(_a, _v >> 16); FWW(_a + 2, _v); } while (0)
#endif

/* the sprite table (page 0x92) */
#ifdef HOST
#define OWW(a, v) WW(a, v)
#else
extern u8 *m68_page[256];
#define OWW(a, v) (*(u16 *)(m68_page[0x92] + ((a) & 0xFFFF)) = (v))
#endif

/* 0xB2DA: one object (A6) into the sprite table (A4); D4/D5 the slots left */
static u32 d0, d1, d2, d3, d4, d5, d6, d7, a0, a1, a2, a3, a4, a6, sp;

NH static void obj_b2da(void)
{
    if (!(FB(a6 + 1) & 0xFF)) return;
    LO(d0, FW(a6 + 0x2a));
    if (!(d0 & 0xFFFF)) return;
    if ((d5 & 0xFFFF) < (d0 & 0xFFFF)) return;
    LO(d5, d5 - d0);
    LO(d1, FW(a6 + 0x2c));
    a2 = RL(0xB418 + SXW(d1));
    LO(d3, FW(a6 + 0x2e));
    if ((d3 & 0xFFFF) >= 0x100) {
        LO(d4, d4 - 1);
        if (d3 & 0x20) {
            LO(d1, FW(a6 + 0x10));
            u32 t = ((d3 & 0xFFFF) >> 4) & 0xF0;
            LO(d2, t);
            LO(d1, d1 - t);
            LO(d1, d1 - RW(a2)); a2 += 2;
            LO(d1, d1 - 0x10);
        } else {
            LO(d1, FW(a6 + 0x10) + RW(a2)); a2 += 2;
        }
        LO(d2, FW(a6 + 0x14) + RW(a2)); a2 += 2;
        LO(d1, d1 + 0x40);
        d2 = (d2 & ~0xFFu) | (~d2 & 0xFF);
        LO(d2, d2 + 1);
        a1 = FL(a6 + 0x22) + 8;
        OWW(a4, d1); OWW(a4 + 2, d2); OWW(a4 + 4, RW(a1)); OWW(a4 + 6, FW(a6 + 0x2e)); a4 += 8;
        C.z = 0; C.n = 0; C.v = 0; C.c = 0;       /* (move.w ($2e,A6),(A4)+: flags of that word) */
        { u16 w = FW(a6 + 0x2e); C.z = !w; C.n = w >> 15; }
        return;
    }
    LO(d4, d4 - d0);
    LO(d0, d0 - 1);
    if (d3 & 0x20) {
        LO(d1, FW(a6 + 0x10) - RW(a2)); a2 += 2;
        LO(d1, d1 - 0x10);
        LO(d2, FW(a6 + 0x14) + RW(a2)); a2 += 2;
    } else {
        LO(d1, FW(a6 + 0x10) + RW(a2)); a2 += 2;
        LO(d2, FW(a6 + 0x14) + RW(a2)); a2 += 2;
    }
    LO(d1, d1 + 0x40);
    d2 = (d2 & ~0xFFu) | (~d2 & 0xFF);
    LO(d2, d2 + 1);
    a1 = FL(a6 + 0x22) + 8;
    for (;;) {
        OWW(a4, d1); OWW(a4 + 2, d2); OWW(a4 + 4, RW(a1)); a1 += 2; OWW(a4 + 6, d3); a4 += 8;
        if (!(d0 & 0xFFFF)) break;
        if (d3 & 0x20) LO(d1, d1 - RW(a2)); else LO(d1, d1 + RW(a2));
        a2 += 2;
        LO(d2, d2 + RW(a2)); a2 += 2;
        LO(d0, d0 - 1);
    }
    C.z = 1; C.n = 0; C.v = 0; C.c = 0;           /* tst.w D0 (zero) */
}
static inline void push(u32 v) { sp -= 4; FWL(sp, v); }
static inline void call_b2da(u32 ret) { push(ret); obj_b2da(); sp += 4; }
/* 0xB28A: an object, its children ($4b count, $4c list) first, then its link ($4e) */
NH static void obj_b28a(void)
{
    d6 = (d6 & ~0xFFu) | (FB(a6 + 0x4b) & 0xFF);
    if (d6 & 0xFF) {
        push(a6);
        LO(d6, SXW((s8)d6));
        LO(d6, d6 - 1);
        a3 = SXW(FW(a6 + 0x4c));
        for (;;) {
            a6 = SXW(FW(a3)); a3 += 2;
            call_b2da(0xB2A2);
            LO(d6, d6 - 1);
            if ((d6 & 0xFFFF) == 0xFFFF) break;
        }
        a6 = FL(sp); sp += 4;
        FWB(a6 + 0x4b, 0);
        FWW(a6 + 0x4c, a3);
    }
    call_b2da(0xB2B6);
    LO(d0, FW(a6 + 0x4e));
    C.z = !(d0 & 0xFFFF); C.n = (d0 >> 15) & 1; C.v = 0; C.c = 0;
    if (!(d0 & 0xFFFF)) return;
    push(a6);
    a6 = SXW(d0);
    u32 b = FB(a6) & 0xFF;
    if (b) {
        u32 c2 = FB(a6 + 2) & 0xFF;
        if (c2 < 6) call_b2da(0xB2D4);
        else { C.z = 0; C.c = 0; C.v = 0; C.n = ((c2 - 6) >> 7) & 1; }
    } else { C.z = 1; C.n = 0; C.v = 0; C.c = 0; }
    a6 = FL(sp); sp += 4;                       /* movem: flags kept */
}
static inline void call_b28a(u32 ret) { push(ret); obj_b28a(); sp += 4; }
static void list_plain(u32 cnt, u32 ptr, u32 ret)
{
    d7 = (d7 & ~0xFFu) | (FB(cnt) & 0xFF);
    if (!(d7 & 0xFF)) return;
    LO(d7, SXW((s8)d7)); LO(d7, d7 - 1);
    a0 = SXW(FW(ptr));
    do { a6 = SXW(FW(a0)); a0 += 2; call_b28a(ret); LO(d7, d7 - 1); } while ((d7 & 0xFFFF) != 0xFFFF);
    FWB(cnt, 0); FWW(ptr, a0);
}
static void list_sorted(u32 cnt, u32 ptr, u32 ret)
{
    d7 = (d7 & ~0xFFu) | (FB(cnt) & 0xFF);
    if (!(d7 & 0xFF)) return;
    LO(d7, SXW((s8)d7)); LO(d7, d7 - 1);
    a0 = SXW(FW(ptr));
    LO(d3, d7); a1 = a0;
    for (;;) {                                  /* B0F8: exchange sort on ($14) - ($36) */
        LO(d2, d3);
        if (!(d2 & 0xFFFF)) break;
        LO(d2, d2 - 1);
        a3 = SXW(FW(a1)); a1 += 2; a2 = a1;
        for (;;) {
            a6 = SXW(FW(a2)); a2 += 2;
            LO(d0, FW(a3 + 0x14) - FW(a3 + 0x36));
            LO(d1, FW(a6 + 0x14) - FW(a6 + 0x36));
            if ((d1 & 0xFFFF) < (d0 & 0xFFFF)) { FWW(a2 - 2, a3); FWW(a1 - 2, a6); a3 = a6; }
            LO(d2, d2 - 1);
            if ((d2 & 0xFFFF) == 0xFFFF) break;
        }
        LO(d3, d3 - 1);
        if ((d3 & 0xFFFF) == 0xFFFF) break;
    }
    do { a6 = SXW(FW(a0)); a0 += 2; call_b28a(ret); LO(d7, d7 - 1); } while ((d7 & 0xFFFF) != 0xFFFF);
    FWB(cnt, 0); FWW(ptr, a0);
}
/* 0xB0C6: the nine object lists */
static void lists_b0c6(void)
{
    u32 a5 = C.a[5];
    list_plain(a5 + 0x590c, a5 + 0x5916, 0xB0DA);
    list_sorted(a5 + 0x590f, a5 + 0x591c, 0xB130);
    list_plain(a5 + 0x5912, a5 + 0x5922, 0xB150);
    list_plain(a5 + 0x590d, a5 + 0x5918, 0xB170);
    list_sorted(a5 + 0x5910, a5 + 0x591e, 0xB1C6);
    list_plain(a5 + 0x5913, a5 + 0x5924, 0xB1E6);
    list_plain(a5 + 0x590e, a5 + 0x591a, 0xB206);
    list_sorted(a5 + 0x5911, a5 + 0x5920, 0xB25C);
    list_plain(a5 + 0x5914, a5 + 0x5926, 0xB27C);
}
/* 0xB06E: build the sprite table (0x928000, or 0x920000 for the other buffer) */
#ifdef HOST
#include <stdlib.h>
#include <stdio.h>
#endif
int nat_b06e(void)
{
#ifdef HOST
    static int off = -1; if (off < 0) off = getenv("NONAT") != 0; if (off) return 0;
#endif
    u32 a5 = C.a[5];
    d0 = C.d[0]; d1 = C.d[1]; d2 = C.d[2]; d3 = C.d[3]; d4 = C.d[4]; d5 = C.d[5]; d6 = C.d[6]; d7 = C.d[7];
    a0 = C.a[0]; a1 = C.a[1]; a2 = C.a[2]; a3 = C.a[3]; a4 = C.a[4]; a6 = C.a[6]; sp = C.a[7];
    u32 f6, f8, fc;                             /* the count fields: 1c6/1ca/1c8 or 1cc/1d0/1ce */
    if (RB(a5 + 0x1d2) & 0xFF) { a4 = 0x928000; f6 = 0x1c6; f8 = 0x1ca; fc = 0x1c8; }
    else {
        a4 = 0x920000;
        if (RB(a5 + 0x15d) & 0x80) { f6 = 0x1cc; f8 = 0x1d0; fc = 0x1ce; }
        else { a4 = 0x928000; f6 = 0x1c6; f8 = 0x1ca; fc = 0x1c8; }
    }
    LO(d4, 0x100); LO(d5, 0x100);
    push(f6 == 0x1c6 ? 0xB094 : 0xB3E6); lists_b0c6(); sp += 4;
    WW(a5 + f6, d4); WW(a5 + f8, d5);
    u32 o = RW(a5 + fc);
    C.x = (d4 & 0xFFFF) < o;                    /* sub.w ($1c8,A5),D4: the last X before the rts */
    if ((d4 & 0xFFFF) > o) {
        LO(d4, d4 - o);
        d1 = 0x180; d2 = 0;
        do { WL(a4, d1); WL(a4 + 4, d2); a4 += 8; LO(d4, d4 - 1); } while ((d4 & 0xFFFF) != 0xFFFF);
    } else LO(d4, d4 - o);
    WW(a5 + fc, RW(a5 + f6));
    WB(a5 + 1, 0);
    C.d[0] = d0; C.d[1] = d1; C.d[2] = d2; C.d[3] = d3; C.d[4] = d4; C.d[5] = d5; C.d[6] = d6; C.d[7] = d7;
    C.a[0] = a0; C.a[1] = a1; C.a[2] = a2; C.a[3] = a3; C.a[4] = a4; C.a[6] = a6;
    C.z = 1; C.n = 0; C.v = 0; C.c = 0;          /* clr.b ($1,A5) */
    return 1;
}

/* ======== V3.6: more natives (cart ROM: loops run from the cache), entered through nat_sw() from cpu_run (x68.py NATLIST, tools/natgen.py) ========
   Each returns the 68000 pc to continue at (the popped return address for an rts) with registers, flags and memory
   exactly as the 68000 code leaves them; 0 = not handled (run the translated code). */
#define ZN16(r) do { C.z = !((r) & 0xFFFF); C.n = ((r) >> 15) & 1; } while (0)
#define ZN8(r) do { C.z = !((r) & 0xFF); C.n = ((r) >> 7) & 1; } while (0)
#define RTS() ({ u32 _r = RL(C.a[7]); C.a[7] += 4; _r; })

/* object-list skip loop: tst.b/w (d,An) ; beq L ; [active body] ... L: lea (s,An),An ; dbra Dk,head (objects in WRAM) */
static inline __attribute__((always_inline)) u32 nskip(int w, u32 d, int an, u32 st, int dn, u32 act, u32 ex)
{
    u32 a = C.a[an]; int k = C.d[dn] & 0xFFFF;
    if (((a >> 16) & 0xFF) != 0xFF) return 0;
    for (;;) {
        u32 v = w ? FW(a + d) : FB(a + d);
        if (v) { C.a[an] = a; LO(C.d[dn], k); C.z = 0; C.n = (v >> (w ? 15 : 7)) & 1; C.v = 0; C.c = 0; return act; }
        a += st;
        if (--k < 0) { C.a[an] = a; LO(C.d[dn], 0xFFFF); C.z = 1; C.n = 0; C.v = 0; C.c = 0; return ex; }
    }
}

/* 0x46B4 / 0x46E8: is the object (A6) inside the box of its type (table 0x47C4 -> 0x471C)?
   ($1,A6) = 1 / 0; inside: 46B4 jmp (A1), 46E8 rts; outside: rts */
static u32 n_box(int jmpa1)
{
    u32 a6 = C.a[6];
    u32 t = FW(a6 + 0x2c);
    u32 d0 = (C.d[0] & 0xFFFF0000u) | RW(0x47C4 + (t >> 1)); C.d[0] = d0;
    u32 a0 = 0x471C + SXW(d0);
    u32 r = FW(a6 + 0x10) + RW(a0);
    u32 ok = RW(a0 + 2) > (r & 0xFFFF);
    a0 += 4;
    if (ok) {
        r = FW(a6 + 0x14) + RW(a0); a0 += 2;
        ok = RW(a0) > (r & 0xFFFF);
    }
    LO(C.d[1], r); C.x = (r >> 16) & 1;
    C.a[0] = a0; C.v = 0; C.c = 0; C.n = 0;
    u32 sp = C.a[7], ret = jmpa1 && ok ? C.a[1] : FL(sp);
    if (!(jmpa1 && ok)) C.a[7] = sp + 4;
    FWB(a6 + 1, ok); C.z = !ok;
    return ret;
}

/* 0x4F2A: animation timer of the object (A6) */
static u32 n_4f2a(void)
{
    u32 a6 = C.a[6];
    u32 o = FW(a6 + 0x20), r = (o - 1) & 0xFFFF;
    FWW(a6 + 0x20, r);
    u32 d0 = C.d[0];
    if (r) { C.x = (o == 0); }
    else {
        u32 a0 = FL(a6 + 0x26);
        FWL(a6 + 0x22, a0);
        LO(d0, RW(a0)); FWW(a6 + 0x2a, d0);
        FWW(a6 + 0x2c, RW(a0 + 2));
        u32 d1 = C.d[1]; LO(d1, RW(a0 + 6)); FWW(a6 + 0x2e, d1);
        LO(d1, d1 & 0xFF00); C.d[1] = d1;
        if (d1 & 0xFFFF) d0 = 1;
        u32 s = (d0 + d0) & 0xFFFF; LO(d0, s);
        a0 = a0 + 8 + SXW(d0);
        u32 ng = (0 - s) & 0xFFFF; C.x = (s != 0); LO(d0, ng);
        LO(d0, RW(a0 - 4 + SXW(d0)));
        if (d0 & 0x8000) { LO(d0, d0 & 0xFF); a0 = RL(a0); }
        FWL(a6 + 0x26, a0); FWW(a6 + 0x20, d0);
        C.a[0] = a0;
    }
    u32 b = FB(a6 + 0x38); C.d[0] = (d0 & ~0xFFu) | b;
    u32 m = (FB(a6 + 0x2f) | b) & 0xFF; FWB(a6 + 0x2f, m);
    ZN8(m); C.v = 0; C.c = 0;
    u32 sp = C.a[7]; C.a[7] = sp + 4;
    return FL(sp);
}

/* 0x6354: walk the 10-byte table (A0) until ($3,A0) < 0 or (A0) > D0.w; continues at 0x636A */
static u32 n_6354(void)
{
    u32 a0 = C.a[0], d0 = C.d[0] & 0xFFFF;
    for (;;) {
        a0 += 10;
        u32 b = RB(a0 + 3);
        if (b & 0x80) { C.n = 1; C.z = 0; C.v = 0; C.c = 0; break; }
        u32 s = RW(a0), r = (d0 - s) & 0xFFFF;
        if (s > d0) { C.c = 1; C.z = 0; C.n = r >> 15; C.v = (((d0 ^ s) & (d0 ^ r)) >> 15) & 1; break; }
    }
    C.a[0] = a0;
    return 0x636A;
}

/* 0x524 (VBlank): the 16 task slots' sleep counters */
static u32 n_0524(void)
{
    u32 a0 = C.a[0]; int k = C.d[0] & 0xFFFF;
    u32 st, t = 1;
    for (;;) {
        st = FB(a0);
        if (st == 1) {
            t = FB(a0 + 1); u32 q = (t - 1) & 0xFF;
            FWB(a0 + 1, q); C.x = (t == 0);
            if (!q) FWB(a0, 4);
        }
        a0 += 16;
        if (--k < 0) break;
    }
    /* flags of the last slot's last instruction */
    if (st != 1) { u32 r = (st - 1) & 0xFF; C.z = 0; C.n = r >> 7; C.c = st < 1; C.v = (st == 0x80); }
    else { u32 q = (t - 1) & 0xFF; if (!q) { C.z = 0; C.n = 0; C.v = 0; C.c = 0; } else { C.z = 0; C.n = q >> 7; C.c = (t == 0); C.v = (t == 0x80); } }
    C.a[0] = a0; LO(C.d[0], 0xFFFF);
    return 0x540;
}

/* 0x4143A: fill a scroll1 column, 2 longs per row */
static u32 n_4143a(void)
{
    u32 a2 = C.a[2], v = C.d[0], k = C.d[3] & 0xFFFF;
    do { WL(a2, v); WL(a2 + 4, v); a2 += 0x80; k = (k - 1) & 0xFFFF; } while (k != 0xFFFF);
    C.a[2] = a2; LO(C.d[3], k);
    C.z = !v; C.n = v >> 31; C.v = 0; C.c = 0;
    return 0x41448;
}

/* 0x41456: a scroll1 column from bytes (A0)+, tile = byte + D2.w, attribute D1.w */
static u32 n_41456(void)
{
    u32 a0 = C.a[0], a2 = C.a[2], d0 = C.d[0], d1 = C.d[1] & 0xFFFF, d2 = C.d[2] & 0xFFFF, k = C.d[3] & 0xFFFF;
    do {
        u32 b = RB(a0); u32 s = b + d2; C.x = s >> 16; d0 = s & 0xFFFF; WW(a2, d0); WW(a2 + 2, d1);
        b = RB(a0 + 1); s = b + d2; C.x = s >> 16; d0 = s & 0xFFFF; WW(a2 + 4, d0); WW(a2 + 6, d1);
        a0 += 2; a2 += 0x80; k = (k - 1) & 0xFFFF;
    } while (k != 0xFFFF);
    C.d[0] = d0; C.a[0] = a0; C.a[2] = a2; LO(C.d[3], k);
    C.z = !d1; C.n = d1 >> 15; C.v = 0; C.c = 0;
    return 0x41478;
}

/* 0x48202: 16 rows of 2x2 scroll1 blocks (gfx RAM) from words (A0)+ / ($1e,A0), attribute D1.w */
static u32 n_48202(void)
{
    u32 a0 = C.a[0], a1 = C.a[1], d0 = C.d[0], d1 = C.d[1] & 0xFFFF, d3 = C.d[3], k = C.d[2] & 0xFFFF;
    do {
        LO(d0, RW(a0)); a0 += 2; LO(d3, RW(a0 + 0x1e));
        WW(a1, d0); WW(a1 + 2, d1); WW(a1 + 8, d3); WW(a1 + 0xa, d1);
        LO(d0, d0 + 1); LO(d3, d3 + 1);
        WW(a1 + 0x80, d0); WW(a1 + 0x82, d1); WW(a1 + 0x88, d3); WW(a1 + 0x8a, d1);
        LO(d0, d0 + 0xf); LO(d3, d3 + 0xf);
        WW(a1 + 4, d0); WW(a1 + 6, d1); WW(a1 + 0xc, d3); WW(a1 + 0xe, d1);
        LO(d0, d0 + 1); C.x = (d3 & 0xFFFF) == 0xFFFF; LO(d3, d3 + 1);
        WW(a1 + 0x84, d0); WW(a1 + 0x86, d1); WW(a1 + 0x8c, d3); WW(a1 + 0x8e, d1);
        a1 += 0x100; k = (k - 1) & 0xFFFF;
    } while (k != 0xFFFF);
    C.d[0] = d0; C.d[3] = d3; LO(C.d[2], k); C.a[0] = a0; C.a[1] = a1;
    C.z = !d1; C.n = d1 >> 15; C.v = 0; C.c = 0;
    return 0x4825E;
}

/* 0x326C: blank the part of a gauge column (A0) above D0.w; continues at 0x32AE (the rest) or 0x3288 */
static u32 n_326c(void)
{
    u32 a0 = C.a[0], a1 = C.a[1], d0 = C.d[0] & 0xFFFF, d1 = C.d[1], d4 = C.d[4] & 0xFFFF, k = C.d[3] & 0xFFFF;
    for (;;) {
        u32 o = d1 & 0xFFFF, r = (o - d4) & 0xFFFF; LO(d1, r); C.x = d4 > o;
        u32 q = (d0 - r) & 0xFFFF;
        if (d0 >= r) {
            C.c = 0; C.z = !q; C.n = q >> 15; C.v = (((d0 ^ r) & (d0 ^ q)) >> 15) & 1;
            C.a[0] = a0; C.a[1] = a1; C.d[1] = d1; LO(C.d[3], k); return 0x32AE;
        }
        WW(a0, 0x6020); WW(a0 + 0x80, 0x6020);
        a0 += 0x100; a1 += 2; k = (k - 1) & 0xFFFF;
        if (k == 0xFFFF) {
            C.n = 0; C.z = 0; C.v = 0; C.c = 0;
            C.a[0] = a0; C.a[1] = a1; C.d[1] = d1; LO(C.d[3], k); return 0x3288;
        }
    }
}

/* 0x32AE: the gauge column's tiles from the words (A1)+ (upper tile, lower = +1), attribute D2.w */
static u32 n_32ae(void)
{
    u32 a0 = C.a[0], a1 = C.a[1], d0 = C.d[0], d2 = C.d[2] & 0xFFFF, k = C.d[3] & 0xFFFF;
    do {
        LO(d0, RW(a1)); a1 += 2;
        WW(a0, d0);
        C.x = (d0 & 0xFF) == 0xFF; d0 = (d0 & ~0xFFu) | ((d0 + 1) & 0xFF);
        WW(a0 + 0x80, d0); WW(a0 + 2, d2); WW(a0 + 0x82, d2);
        a0 += 0x100; k = (k - 1) & 0xFFFF;
    } while (k != 0xFFFF);
    C.d[0] = d0; LO(C.d[3], k); C.a[0] = a0; C.a[1] = a1;
    C.z = !d2; C.n = d2 >> 15; C.v = 0; C.c = 0;
    return 0x32C8;
}

/* 0x447C4: 13 map words (A0,D0.w) one row (0x20) apart: animate the 0x03xx tiles (frame D6 / table 0x4484C) */
static u32 n_447c4(void)
{
    u32 a0 = C.a[0], d0 = C.d[0], d1 = C.d[1], d2 = C.d[2], d6 = C.d[6], k = C.d[3] & 0xFFFF;
    do {
        u32 ea = a0 + SXW(d0);
        LO(d1, RW(ea)); LO(d2, d1 & 0xFF00);
        if ((d2 & 0xFFFF) == 0x300) {
            LO(d1, d1 & 0xFF);
            u32 t = d1 & 0xFFFF;
            if (t < 0xA0) {
                if (t < 4) {
                    u32 b = d0 & 0x60; b = ((b << 3) | (b >> 5)) & 0xFF; b = (b + d6) & 0xFF;
                    LO(d2, (b & 3) + 0x300); WW(ea, d2);
                } else {
                    d2 = (d2 & ~0xFFu) | RB(0x4484C + t);
                    if ((d2 & 0xFF) != 0xFF) WW(ea, d2);
                }
            }
        }
        LO(d1, d0 & 0x7FF);
        if ((d1 & 0xFFFF) >= 0x7E0) LO(d0, d0 - 0x800);
        C.x = (d0 & 0xFFFF) >= 0x10000 - 0x20;
        LO(d0, (d0 + 0x20) & 0x3FFF);
        k = (k - 1) & 0xFFFF;
    } while (k != 0xFFFF);
    C.d[0] = d0; C.d[1] = d1; C.d[2] = d2; LO(C.d[3], k);
    C.z = !(d0 & 0xFFFF); C.n = 0; C.v = 0; C.c = 0;
    return 0x44828;
}

#ifdef HOST
static unsigned long *nat_cnt_p;
void nat_cnt_dump(void) { if (!getenv("NATCNT") || !nat_cnt_p) return; FILE *f = fopen(getenv("NATCNT"), "w"); for (int i = 0; i < 0x30000; i++) if (nat_cnt_p[i]) fprintf(f, "%06X %lu\n", i * 2, nat_cnt_p[i]); fclose(f); }
#endif
/* the entries: one function each (x68.py calls nat_<pc>() from the translated code) */
#ifdef HOST
static int nat_chk(u32 pc)
{
    static int off = -1; if (off < 0) off = getenv("NONAT") != 0; if (off) return 1;
    static unsigned long n[0x30000]; static int at; extern void nat_cnt_dump(void);
    if (!at) { at = 1; atexit(nat_cnt_dump); } n[pc >> 1]++; nat_cnt_p = n;
    return 0;
}
#define NATF(a, e) u32 nat_##a(void) { if (nat_chk(0x##a)) return 0; return e; }
#else
#define NATF(a, e) u32 nat_##a(void) { return e; }
#endif
NATF(46B4, n_box(1))
NATF(46E8, n_box(0))
NATF(4F2A, n_4f2a())
NATF(6354, n_6354())
NATF(524, n_0524())
NATF(4143A, n_4143a())
NATF(41456, n_41456())
NATF(48202, n_48202())
NATF(326C, n_326c())
NATF(32AE, n_32ae())
NATF(447C4, n_447c4())
static u32 n_b06e(void) { if (!nat_b06e()) return 0; u32 sp = C.a[7]; C.a[7] = sp + 4; return RL(sp); }
NATF(B06E, n_b06e())
#include "nat_tab.h"

/* ======== V3.8: full-screen attack (option; tools/patch_gen.py FSA patches in the hit tests 0xA430/0xA5D4/0xA678/0xA730)
   A0 = the attack object (list 0x8702), A1 = the target.  On: an attack of a player (owner ($4c,A0) = 0xFF8282 / 0xFF8322) hits every target on screen (x 0-0x17F), whatever its lane and distance.  The game's hit code then runs
   as usual for that target; instead of leaving the scan (its two pops), the registers saved here are put back at its rts
   and the scan goes on with the next target. */
unsigned char fsa_enable, fsa_saved;
static u32 fsa_d[8], fsa_a[7];
static int fsa_on(void)
{
#ifdef HOST
    { static int e = -1; if (e < 0) e = getenv("FSA") != 0; fsa_enable = e; }
#endif
    if (!fsa_enable) return 0;
    u32 o = RW(C.a[0] + 0x4c) & 0xFFFF;
#ifdef HOST
    if (getenv("FSADBG")) fprintf(stderr, "fsa A0=%06X own=%04X A1=%06X x=%04X\n", C.a[0] & 0xFFFFFF, o, C.a[1] & 0xFFFFFF, RW(C.a[1] + 0x10));
#endif
    return o == 0x8282 || o == 0x8322;           /* player 1 / player 2 (0xFF8282 / 0xFF8322) */
}
static int fsa_vis(void) { return (RW(C.a[1] + 0x10) & 0xFFFF) < 0x180; }
int fsa_lane(void) { if (!fsa_on()) return 0; return fsa_vis() ? 1 : 2; }          /* 1: same lane (Z = 1), 2: not */
int fsa_range(void)                                                                 /* 2: in range (C = 1), 1: not */
{
    if (!fsa_on()) return 0;
    if (!fsa_vis()) return 1;
    for (int i = 0; i < 8; i++) fsa_d[i] = C.d[i];
    for (int i = 0; i < 7; i++) fsa_a[i] = C.a[i];
    fsa_saved = 1;
    return 2;
}
void fsa_restore(void)
{
    if (!fsa_saved) return;
    for (int i = 0; i < 8; i++) C.d[i] = fsa_d[i];
    for (int i = 0; i < 7; i++) C.a[i] = fsa_a[i];
    fsa_saved = 0;
}
