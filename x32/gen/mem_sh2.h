/* SH-2 memory map for the translated program.
   68000 address space (24-bit) -> SH-2 pointer via a 256-entry page table (64KB pages).
   SDRAM layout (see t32/sh2/main.c): 0xFF page at WRAM_BASE (constant), 0x90/0x92 gfx RAM,
   0x80 I/O read shadow (writes go through io_w*()), program ROM pages point into the cart.
   Big-endian like the 68000, so no byte swapping. */
extern u8 *m68_page[256];
void io_wb(u32 a, u32 v); void io_ww(u32 a, u32 v);
/* writes into the scroll1 map (page 0x90) mark map rows dirty for the 32X renderer */
/* pages 0x80 (I/O) and 0x90 (gfx RAM: the renderer tracks scroll1 changes) take the slow path */
void slow_wb(u32 a, u32 v); void slow_ww(u32 a, u32 v); void slow_wl(u32 a, u32 v);
#define SLOWPG(a) __builtin_expect(((((a) >> 16) & 0xEF) == 0x80), 0)
#ifndef NOCONSTPG
#define NOCONSTPG 0
#endif
#define WRAM_BASE ((u8 *)0x06000000)
#define PG(a) ((NOCONSTPG==0 && __builtin_constant_p(a) && (((a) >> 16) & 0xFF) == 0xFF) ? WRAM_BASE + ((a) & 0xFFFF) : m68_page[((a) >> 16) & 0xFF] + ((a) & 0xFFFF))
#define ISIO(a) __builtin_expect(((((a) >> 16) & 0xFF) == 0x80), 0)
#define MI RTI
#ifdef SIMX86
#undef WRAM_BASE
extern u8 sim_wram[0x10000];
#define WRAM_BASE sim_wram
MI u32 RB(u32 a) { return *PG(a); }
MI u32 RW(u32 a) { u8 *p = PG(a); return (p[0] << 8) | p[1]; }
MI u32 RL(u32 a) { u8 *p = PG(a); return ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
MI void WB(u32 a, u32 v) { if (ISIO(a)) io_wb(a, v); else *PG(a) = v; }
MI void WW(u32 a, u32 v) { if (ISIO(a)) io_ww(a, v); else { u8 *p = PG(a); p[0] = v >> 8; p[1] = v; } }
MI void WL(u32 a, u32 v) { WW(a, v >> 16); WW(a + 2, v); }
#else
MI u32 RB(u32 a) { return *(volatile u8 *)PG(a); }
MI u32 RW(u32 a) { return *(u16 *)PG(a); }
MI u32 RL(u32 a) { u8 *p = PG(a); return ((u32) * (u16 *)p << 16) | *(u16 *)(p + 2); }
MI void WB(u32 a, u32 v) { if (SLOWPG(a)) slow_wb(a, v); else *PG(a) = v; }
MI void WW(u32 a, u32 v) { if (SLOWPG(a)) slow_ww(a, v); else *(u16 *)PG(a) = v; }
MI void WL(u32 a, u32 v) { if (SLOWPG(a)) slow_wl(a, v); else { u8 *p = PG(a); *(u16 *)p = v >> 16; *(u16 *)(p + 2) = v; } }
#endif
