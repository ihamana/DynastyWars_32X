/* Runtime for the translated 68000 program (host and SH-2 builds share this). */
#ifndef RT_H
#define RT_H
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#ifdef COLD
#define RTI static inline
#else
#define RTI static inline __attribute__((always_inline))
#endif
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32;

typedef struct {
    u32 d[8], a[8];
    u32 usp, ssp;          /* the inactive stack pointer is kept here */
    u32 x, n, z, v, c;     /* flags 0/1 */
    u32 s, ipl;            /* supervisor bit, interrupt mask */
    u32 pc, irq, ran;
} Cpu;
extern Cpu C;

#ifdef HOST
u8  host_rb(u32 a); u16 host_rw(u32 a); u32 host_rl(u32 a);
void host_wb(u32 a, u32 v); void host_ww(u32 a, u32 v); void host_wl(u32 a, u32 v);
#define RB(a) host_rb((a) & 0xFFFFFF)
#define RW(a) host_rw((a) & 0xFFFFFF)
#define RL(a) host_rl((a) & 0xFFFFFF)
#define WB(a, v) host_wb((a) & 0xFFFFFF, (v) & 0xFF)
#define WW(a, v) host_ww((a) & 0xFFFFFF, (v) & 0xFFFF)
#define WL(a, v) host_wl((a) & 0xFFFFFF, (v))
#else
#include "mem_sh2.h"
#endif

/* V3.6 movem fast path: WRAM (page 0xFF) straight, big-endian words */
#ifdef HOST
u8 *host_wram(void);
#define WRP(a) (host_wram() + ((a) & 0xFFFF))
#define MVW(p, v) ((p)[0] = (u8)((v) >> 8), (p)[1] = (u8)(v))
#define MVR(p) (((u32)(p)[0] << 8) | (p)[1])
#else
#define WRP(a) ((u8 *)0x06000000 + ((a) & 0xFFFF))
#define MVW(p, v) (*(u16 *)(p) = (v))
#define MVR(p) (*(const u16 *)(p))
#endif
#define ISWRAM(a) ((((a) >> 16) & 0xFF) == 0xFF)
/* (d,A5) with A5 fixed at 0xFFFF8000 by the arcade program */
#define A5C(x) (x)
void cpu_bad(u32 pc);
#ifdef HOST
void dlog(u32 pc);
#define DLOG(pc) dlog(pc)
#else
#define DLOG(pc)
#endif
#ifdef TRACE
void tr_hook(u32 pc);
#define TR(pc) tr_hook(pc)
#elif defined(PCMAP)   /* profiling: (SH-2 address, 68000 pc) pairs in the unloaded section .pcmap */
#define TR(pc) __asm__ volatile("1:\n\t.pushsection .pcmap,\"\"\n\t.long 1b, " #pc "\n\t.popsection")
#else
#define TR(pc)
#endif
void cpu_run(void);
/* V3.6 nested region calls (x68.py NESTED): see tools/x68.py */
extern unsigned char ncd;
u32 rcall(u32 a, int self);
#ifndef NCMAX
#define NCMAX 5
#endif
#define NEST(fn, T, R, LR) { if (ncd < NCMAX) { ncd++; u32 _r = fn(T); ncd--; if (_r == (R)) goto LR; return _r; } return (T); }
#define NESTD(self, R, LR) { if (ncd < NCMAX) { u32 _r = rcall(pc & 0xFFFFFF, self); if (_r != 0xFFFFFFFEu) { if (_r == (R)) goto LR; return _r; } } goto dispatch; }
#ifdef HOST
extern unsigned long rcalls[1024];
#define RCOUNT(r) rcalls[r]++
#else
#define RCOUNT(r)
#endif

RTI u32 sr_get(void) { return (C.s << 13) | (C.ipl << 8) | (C.x << 4) | (C.n << 3) | (C.z << 2) | (C.v << 1) | C.c; }
RTI void ccr_set(u32 v) { C.x = (v >> 4) & 1; C.n = (v >> 3) & 1; C.z = (v >> 2) & 1; C.v = (v >> 1) & 1; C.c = v & 1; }
RTI void sr_set(u32 v)
{
    u32 s = (v >> 13) & 1;
    if (s != C.s) { if (s) { C.usp = C.a[7]; C.a[7] = C.ssp; } else { C.ssp = C.a[7]; C.a[7] = C.usp; } C.s = s; }
    C.ipl = (v >> 8) & 7; ccr_set(v);
}
RTI u32 exc(int vec, u32 ret)
{
    u32 old = sr_get();
    sr_set((old & 0x7FF) | 0x2000);
    if (vec >= 25 && vec <= 31) C.ipl = vec - 24;
    C.a[7] -= 4; WL(C.a[7], ret); C.a[7] -= 2; WW(C.a[7], old);
    return RL(vec * 4);
}
/* flags: sb = sign bit index (7/15/31); setx: also X */
RTI void fl_add(u32 s, u32 d, u32 r, int sb, int setx)
{
    u32 m = sb == 31 ? 0xFFFFFFFF : ((1u << (sb + 1)) - 1);
    C.n = (r >> sb) & 1; C.z = !(r & m);
    C.v = (((s ^ r) & (d ^ r)) >> sb) & 1;
    C.c = sb == 31 ? (r < d) : ((((s & m) + (d & m)) >> (sb + 1)) & 1);
    if (setx) C.x = C.c;
}
RTI void fl_sub(u32 s, u32 d, u32 r, int sb, int setx)
{
    u32 m = sb == 31 ? 0xFFFFFFFF : ((1u << (sb + 1)) - 1);
    C.n = (r >> sb) & 1; C.z = !(r & m);
    C.v = (((s ^ d) & (r ^ d)) >> sb) & 1;
    C.c = (s & m) > (d & m);
    if (setx) C.x = C.c;
}
RTI u32 sh_mask(int sb) { return sb == 31 ? 0xFFFFFFFF : ((1u << (sb + 1)) - 1); }
RTI u32 sh_lsl(u32 v, u32 n, int sb)
{
    u32 m = sh_mask(sb); v &= m; u32 r = v;
    if (n) { r = n > (u32)(sb + 1) ? 0 : (n == 32 ? 0 : (v << n)) & m; C.c = C.x = n > (u32)(sb + 1) ? 0 : (v >> (sb + 1 - n)) & 1; } else C.c = 0;
    C.n = (r >> sb) & 1; C.z = !r; C.v = 0; return r;
}
RTI u32 sh_lsr(u32 v, u32 n, int sb)
{
    u32 m = sh_mask(sb); v &= m; u32 r = v;
    if (n) { r = n > (u32)sb ? 0 : v >> n; C.c = C.x = n > (u32)(sb + 1) ? 0 : (v >> (n - 1)) & 1; } else C.c = 0;
    C.n = (r >> sb) & 1; C.z = !r; C.v = 0; return r;
}
RTI u32 sh_asl(u32 v, u32 n, int sb)
{
    u32 m = sh_mask(sb); v &= m; u32 r = v, vv = 0;
    for (u32 i = 0; i < n; i++) { u32 top = (r >> sb) & 1; C.c = C.x = top; r = (r << 1) & m; if (((r >> sb) & 1) != top) vv = 1; }
    if (!n) C.c = 0;
    C.n = (r >> sb) & 1; C.z = !r; C.v = vv; return r;
}
RTI u32 sh_asr(u32 v, u32 n, int sb)
{
    u32 m = sh_mask(sb); v &= m; u32 r = v, sg = (v >> sb) & 1;
    for (u32 i = 0; i < n; i++) { C.c = C.x = r & 1; r = (r >> 1) | (sg << sb); }
    if (!n) C.c = 0;
    C.n = (r >> sb) & 1; C.z = !(r & m); C.v = 0; return r & m;
}
RTI u32 sh_rol(u32 v, u32 n, int sb)
{
    u32 m = sh_mask(sb); v &= m; u32 r = v, w = sb + 1;
    for (u32 i = 0; i < n; i++) { u32 t = (r >> sb) & 1; r = ((r << 1) | t) & m; C.c = t; }
    (void)w; if (!n) C.c = 0;
    C.n = (r >> sb) & 1; C.z = !r; C.v = 0; return r;
}
RTI u32 sh_ror(u32 v, u32 n, int sb)
{
    u32 m = sh_mask(sb); v &= m; u32 r = v;
    for (u32 i = 0; i < n; i++) { u32 t = r & 1; r = (r >> 1) | (t << sb); C.c = t; }
    if (!n) C.c = 0;
    C.n = (r >> sb) & 1; C.z = !r; C.v = 0; return r;
}
RTI u32 sh_roxl(u32 v, u32 n, int sb)
{
    u32 m = sh_mask(sb); v &= m; u32 r = v;
    for (u32 i = 0; i < n; i++) { u32 t = (r >> sb) & 1; r = ((r << 1) | C.x) & m; C.x = t; }
    C.c = C.x;
    C.n = (r >> sb) & 1; C.z = !r; C.v = 0; return r;
}
RTI u32 sh_roxr(u32 v, u32 n, int sb)
{
    u32 m = sh_mask(sb); v &= m; u32 r = v;
    for (u32 i = 0; i < n; i++) { u32 t = r & 1; r = (r >> 1) | (C.x << sb); C.x = t; }
    C.c = C.x;
    C.n = (r >> sb) & 1; C.z = !r; C.v = 0; return r;
}
RTI u32 bcd_add(u32 d, u32 s)
{
    u32 lo = (s & 15) + (d & 15) + C.x, r;
    u32 res = lo + (s & 0xF0) + (d & 0xF0);
    if (lo > 9) res += 6;
    C.c = C.x = res > 0x99; if (C.c) res -= 0xA0;
    r = res & 0xFF;
    if (r) C.z = 0;
    C.n = (r >> 7) & 1; C.v = 0;
    return r;
}
RTI u32 bcd_sub(u32 d, u32 s)
{
    s32 lo = (s32)(d & 15) - (s32)(s & 15) - (s32)C.x;
    s32 res = (s32)(d & 0xF0) - (s32)(s & 0xF0) + lo;
    u32 c = 0;
    if (lo < 0) { res -= 6; }
    if (res < 0) { res -= 0x60; c = 1; }
    u32 r = (u32)res & 0xFF;
    C.c = C.x = c;
    if (r) C.z = 0;
    C.n = (r >> 7) & 1; C.v = 0;
    return r;
}
RTI void divu(int dn, u32 s)
{
    if (!s) { return; }
    u32 q = C.d[dn] / s, r = C.d[dn] % s;
    if (q > 0xFFFF) { C.v = 1; C.n = 1; C.c = 0; return; }
    C.d[dn] = (r << 16) | q; C.n = (q >> 15) & 1; C.z = !q; C.v = 0; C.c = 0;
}
RTI void divs(int dn, u32 s)
{
    s32 sv = (s16)s; if (!sv) return;
    s32 q = (s32)C.d[dn] / sv, r = (s32)C.d[dn] % sv;
    if (q > 32767 || q < -32768) { C.v = 1; C.n = 1; C.c = 0; return; }
    C.d[dn] = ((u32)(u16)r << 16) | (u16)q; C.n = (q >> 15) & 1; C.z = !(u16)q; C.v = 0; C.c = 0;
}
RTI void mulu(int dn, u32 s) { u32 r = (u16)C.d[dn] * (u16)s; C.d[dn] = r; C.n = r >> 31; C.z = !r; C.v = 0; C.c = 0; }
RTI void muls(int dn, u32 s) { s32 r = (s32)(s16)C.d[dn] * (s16)s; C.d[dn] = r; C.n = (u32)r >> 31; C.z = !r; C.v = 0; C.c = 0; }
#endif
