/* Dynasty Wars 32xM - master/slave SH-2.
 * Master: runs the arcade 68000 program (translated, see tools/x68.py) once per vblank,
 * then both CPUs draw sprites + scroll1 (text/HUD layer) into the 32X frame buffer.
 * Backgrounds (scroll2/3) will be drawn by the Mega Drive (not yet).
 * COMM: 0x20004020 (COMM0) from the 68000: inputs (in0 << 8 | in1 low byte)
 *       COMM2/4 to the 68000: sound command ring (written by io_wb), COMM8.. debug. */
#include "../../gen/rt.h"
#include "mars.h"
#include "render.h"

Cpu C;
u8 *m68_page[256];
u8 io_rd[0x200] __attribute__((aligned(4)));   /* I/O read shadow: inputs, DIP, CPS-B id */
u16 io_cpsa[0x20], io_cpsb[0x20];
u16 cpspal_live[2048] __attribute__((section(".rd2"), aligned(4)));                           /* palette pages 0-3, latched at the palette write */
volatile u32 pal_serial;
volatile u8 snd_q[64]; volatile u32 snd_w;
volatile u32 vcount, dbg_bad, logic_on;

extern volatile u8 pal_dirty;
static void pal_latch(void)
{
    /* CPS-B palette control: only the enabled pages are in the source, in order (a disabled page
     * after the first enabled one still takes its slot) */
    u32 b = ((u32)io_cpsa[5] * 256) & 0x3fc00;
    u16 ctrl = io_cpsb[0x22 / 2];
    static u32 lb = ~0u, lctrl = ~0u;
    /* V2.5: the game latches every frame; nothing to copy when the palette RAM (page 0x90: writes seen by
       s23_note) and the controls are as at the last latch */
    if (!pal_dirty && b == lb && ctrl == lctrl && b < 0x10000) {
#ifdef DCHK
        { extern void pal_chk(u32, u32); pal_chk(b, ctrl); }
#endif
        return;
    }
    pal_dirty = 0; lb = b; lctrl = ctrl;
    const u32 *s = (const u32 *)(m68_page[0x90 + (b >> 16)] + (b & 0xFFFF));
    u32 *d = (u32 *)cpspal_live;
    int src = 0;
    for (int page = 0; page < 4; page++) {
        if (ctrl & (1 << page)) { u32 *dd = d + page * 0x100; const u32 *ss = s + src; for (int o = 0; o < 0x100; o++) dd[o] = ss[o]; src += 0x100; }
        else if (src) src += 0x100;
    }
    pal_serial++;
}
void io_wb(u32 a, u32 v)
{
    a &= 0xFFFF; v &= 0xFF;
    if (a == 0x181) { if (v >= 0x01 && v <= 0xF1 && v != 0xF7) { snd_q[snd_w & 63] = v; snd_w++; } return; }
    if (a >= 0x100 && a < 0x140) { u16 *r = &io_cpsa[(a - 0x100) >> 1]; *r = (a & 1) ? (*r & 0xFF00) | v : (*r & 0xFF) | (v << 8); if (a == 0x10B) pal_latch(); if ((a >> 1) == 0x81) s1_base_changed(); return; }
    if (a >= 0x140 && a < 0x180) { u16 *r = &io_cpsb[(a - 0x140) >> 1]; *r = (a & 1) ? (*r & 0xFF00) | v : (*r & 0xFF) | (v << 8); return; }
}
void io_ww(u32 a, u32 v)
{
    a &= 0xFFFF; v &= 0xFFFF;
    if (a >= 0x100 && a < 0x140) { io_cpsa[(a - 0x100) >> 1] = v; if (a == 0x10A) pal_latch(); if (a == 0x102) s1_base_changed(); return; }
    if (a >= 0x140 && a < 0x180) { io_cpsb[(a - 0x140) >> 1] = v; return; }
    io_wb(a, v >> 8); io_wb(a + 1, v);
}
extern u32 s1_lo; void s1_note(u32 a); void s23_note(u32 a);
void slow_wb(u32 a, u32 v)
{
    if ((a & 0xFF0000) == 0x800000) { io_wb(a, v); return; }
    m68_page[0x90][a & 0xFFFF] = v;
    if ((a & 0xFFFF) - s1_lo < 0x4000) s1_note(a);
    else s23_note(a);
}
void slow_ww(u32 a, u32 v)
{
    if ((a & 0xFF0000) == 0x800000) { io_ww(a, v); return; }
    *(u16 *)(m68_page[0x90] + (a & 0xFFFF)) = v;
    if ((a & 0xFFFF) - s1_lo < 0x4000) s1_note(a);
    else s23_note(a);
}
void slow_wl(u32 a, u32 v)
{
    if ((a & 0xFF0000) == 0x800000) { io_ww(a, v >> 16); io_ww(a + 2, v); return; }
    u8 *p = m68_page[0x90] + (a & 0xFFFF); *(u16 *)p = v >> 16; *(u16 *)(p + 2) = v;
    if ((a & 0xFFFF) - s1_lo < 0x4000) s1_note(a);
    else s23_note(a);
}
extern void render_bad(u32 pc);
void cpu_bad(u32 pc) { dbg_bad = pc | 0x80000000; MARS_SYS_COMM6 = 0xBAD0; MARS_SYS_COMM4 = pc; render_bad(pc); for (;;); }
void *memset(void *d, int c, unsigned n) { u8 *p = d; while (n--) *p++ = c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static void cpy(u8 *d, const u8 *s, u32 n) { u32 *dd = (u32 *)d; const u32 *ss = (const u32 *)s; for (n >>= 2; n; n--) *dd++ = *ss++; }
static void clr(u8 *d, u32 n) { u32 *dd = (u32 *)d; for (n >>= 2; n; n--) *dd++ = 0; }

#define CART       0x02000000u
#define PRG_OFS    0x00100000u
#define G90        ((u8 *)0x06010000)
#define G92        ((u8 *)0x06020000)
static volatile u32 hot_ready;
extern u8 __hot_vma[], __hot_lma[], __hot_end[], __rd1_start[], __rd1_end[], __rd2_start[], __rd2_end[], __hb_start[], __hb_end[];

void master_pwm(void) {}
void slave_vint(void) {} void slave_cmd(void) {} void slave_pwm(void) {} void slave_dma(void) {}

void master_main(void)
{
#ifdef STACKPAINT   /* (V3.6 test) paint the master stack to find its low watermark (0x06006250..) */
    { volatile u32 *p = (volatile u32 *)0x06006280; while ((u32)p < (u32)__builtin_frame_address(0) - 128) *p++ = 0xA5A5A5A5; }
#endif
#ifdef STKDBG
    for (u32 a = 0x26006380; a < 0x26006800; a += 4) *(volatile u32 *)a = 0x5AA55AA5;
    for (u32 a = 0x26006900; a < 0x26006E80; a += 4) *(volatile u32 *)a = 0x5AA55AA5;
#endif
    cpy(__hot_vma, __hot_lma, (__hot_end - __hot_vma + 3) & ~3);
    clr(__rd1_start, __rd1_end - __rd1_start); clr(__rd2_start, __rd2_end - __rd2_start); clr(__hb_start, __hb_end - __hb_start);
    *(volatile u32 *)((u32)&hot_ready | 0x20000000) = 0x484F5421;
    for (int i = 0; i < 256; i++) m68_page[i] = (u8 *)(CART + PRG_OFS);
    for (int i = 0; i < 16; i++) m68_page[i] = (u8 *)(CART + PRG_OFS + (i << 16));
    m68_page[0x80] = io_rd; m68_page[0x90] = G90; m68_page[0x92] = G92; m68_page[0xFF] = WRAM_BASE;
    clr(WRAM_BASE, 0x1000); clr(WRAM_BASE + 0x7000, 0x9000); clr(G90, 0xE000); clr(G92, 0x1000); clr(G92 + 0x8000, 0x1000);
    for (int i = 0; i < 0x200; i++) io_rd[i] = 0xFF;
    io_rd[0x160] = 0x00; io_rd[0x161] = 0x02;            /* CPS-B id */
    /* 68000 reset */
    C.s = 1; C.ipl = 7; C.a[7] = RL(0); C.pc = RL(4); C.irq = 0;
    render_init();
#ifdef PROF   /* sample the master PC (WDT interval interrupt, crt0 _m_wdt) into 0x26007004-0x260076FF */
    *(volatile u32 *)0x26007000 = 4;
    *(volatile u16 *)0xFFFFFE80 = 0xA53E;                /* /4096: one sample every 1M cycles (45 ms) */
    *(volatile u16 *)0xFFFFFEE4 = (65 << 8) | (*(volatile u16 *)0xFFFFFEE4 & 0x7F);
    *(volatile u16 *)0xFFFFFEE2 = (*(volatile u16 *)0xFFFFFEE2 & 0xFF0F) | 0x00F0;
#endif
    *(volatile u32 *)((u32)&logic_on | 0x20000000) = 1;
    render_loop();
}
#ifdef REPLAY
static u16 replay_input(u32 n)
{
    static const u8 dirs[9] = { 0x01, 0x09, 0x05, 0x08, 0x04, 0x02, 0x01, 0x09, 0x05 };
    static u32 x = REPLAY, lastn = 0xFFFFFFFF; static u8 d = 1;
    u32 in1 = 0xFF, in0 = 0xFF;
#ifdef REPLAYCUT
    { const volatile u8 *w = (const volatile u8 *)0x26000000; if (n > REPLAYCUT && w[0x81D8] != w[0x81D7]) return 0xFFFF; }   /* test: no input outside the stage scenes */
#endif
    if (REPLAY && n >= 1864) {
        if (n % 90 == 0 && n != lastn) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; d = dirs[x % 9]; }
#ifdef REPLAYFWD
        if (n % 90 == 0 && (x >> 4) % 4) d = 0x01;          /* test: mostly to the right (gets through the stages) */
#endif
        in1 &= ~d;
        u32 k = n % 8;
        if (k < 2) in1 &= ~0x10;
        if (k >= 4 && k < 6) in1 &= ~0x20;
    }
    lastn = n;
    u32 m = n % 600;
    if (REPLAY || n < 1864) { if (m < 4) in0 &= ~0x01; if (m >= 60 && m < 64) in0 &= ~0x10; }
    return (in0 << 8) | in1;
}
#endif
/* The arcade frame (IRQ2) runs once per 32X frame in the vblank interrupt.  (Test option HINTLOGIC: in the
 * H interrupt at line ~100 (HCOUNT) instead: its
 * picture is then ready before the vblank and both SH-2s get the whole next frame to draw it
 * (in the vblank interrupt, the drawing could only start after the logic).  The vblank interrupt
 * runs it instead when the H interrupt did not come.  Measured on PicoDrive: not faster.) */
#ifndef LOGIC_SR
#define LOGIC_SR 0x70
#endif
static void logic_frame(void);
/* V1.9 2P: the MD alternates COMM0 between pad 1 and pad 2 | 0x4000 (md/boot.s); sampled here and in the
   renderer's main loop (render.c anim_send) */
volatile u16 pad_raw[2];
void pads_sample(void) { u16 v = MARS_SYS_COMM0; if (v & 0x4000) pad_raw[1] = v & 0x0FFF; else pad_raw[0] = v; }
/* render.c msg_send: harbour cell messages sent from the CMD interrupt while the logic runs, per frame
   (the flames are scroll2 cells changing every frame: hundreds of messages, the logic was down to 30 fps) */
#ifndef LOGIC_MSGS
#define LOGIC_MSGS 40
#endif
volatile u8 in_logic, logic_missed; volatile int logic_msgs; volatile u32 vint_ticks, vint_busy;
static volatile u8 logic_due; volatile u32 logic_skipped;
volatile u32 vint_t, hint_ph;
void master_vint(void)
{
    vcount++; vint_t = *(volatile u8 *)0xFFFFFE81; pads_sample();
#ifdef TRACE
    { volatile u32 *Q = (volatile u32 *)0x26007200; u32 i = Q[0]; Q[1 + (i & 127)] = (1u << 28) | ((vcount & 0xFFFFF) << 8) | vint_t; Q[0] = i + 1; }
#endif
    { u8 t = *(volatile u8 *)0xFFFFFE81; render_vint(); t = *(volatile u8 *)0xFFFFFE81 - t; extern volatile u32 vint_ticks, vint_busy; vint_ticks += t; }
    if (!logic_on) return;
#ifdef HINTLOGIC
    logic_due = 1;
#else
    /* the logic runs with the interrupt level at 7, so the MD's CMD interrupts (message taken: render.c
       master_cmd sends the next) are served meanwhile; a vblank in the middle only does render_vint.
       V1.9: a game frame longer than a 32X frame (the stage 2 flames: twice the objects) used to lose the
       vblank it ran into, so the game ran at 30 fps.  Now that vblank is caught up at once (one game frame
       right after), but never two in a row, so the drawing still gets the rest of the next frame:
       2 game frames per 3 vblanks instead of 1 per 2.  (The V1.x skip-next rule timed the logic with the
       watchdog counter, which PicoDrive does not count during interrupts.) */
    if (in_logic) { vint_busy++; logic_missed = 1; return; }
    in_logic = 1; logic_msgs = LOGIC_MSGS;
    u32 sr; __asm__ volatile("stc sr,%0" : "=r"(sr)); __asm__ volatile("ldc %0,sr" :: "r"((sr & ~0xF0u) | LOGIC_SR));
    logic_missed = 0;
    logic_frame();
#ifndef NOCATCHUP
    static u8 caught;
    if (logic_missed && !caught) { caught = 1; logic_missed = 0; logic_msgs = LOGIC_MSGS; logic_frame(); logic_skipped++; }
    else caught = 0;
#endif
    __asm__ volatile("ldc %0,sr" :: "r"(sr));
    in_logic = 0;
#ifdef TRACE
    { volatile u32 *Q = (volatile u32 *)0x26007200; u32 i = Q[0]; Q[1 + (i & 127)] = (2u << 28) | ((vcount & 0xFFFFF) << 8) | *(volatile u8 *)0xFFFFFE81; Q[0] = i + 1; }
#endif
#endif
}
void master_hint(void)
{
#ifndef HINTLOGIC
    return;
#endif
    if (!logic_on || !logic_due) return;
    logic_due = 0; hint_ph = (*(volatile u8 *)0xFFFFFE81 - vint_t) & 255;
    logic_frame();
}
volatile u32 lframes;
static void logic_frame(void)
{
    static u32 frames, vf;
    extern u16 ui_frame(u16 raw, int *run); extern void ui_after(void);
    pads_sample();
    u16 raw = pad_raw[0];
#ifdef DBGPAD
    { static u16 seen; if (raw & 0x0F) seen = raw; MARS_SYS_COMM12 = seen; MARS_SYS_COMM14 = raw; }
#endif
#ifdef PADTEST
    { extern u16 padtest(u32 n); raw = padtest(vf); }
#endif
    vf++;
    int run = 1;
    u16 in = ui_frame(raw, &run);
#ifdef REPLAY
#ifdef REPLAYSTOP
    if (frames < REPLAYSTOP)
#endif
    { in = replay_input(frames); run = 1; }    /* test build: deterministic player (tools: bot.py) */
#endif
    { extern volatile u8 ui_in2;
      io_rd[1] = io_rd[3] = io_rd[5] = io_rd[7] = in & 0xFF;     /* P1 */
      io_rd[0] = io_rd[2] = io_rd[4] = io_rd[6] = ui_in2; }     /* P2 (V1.9) */
    io_rd[0x18] = in >> 8;
    if (run) {
        if (frames) C.irq = 2;
        u32 lt0 = *(volatile u8 *)0xFFFFFE81;
#ifdef BADTEST
        if (frames == BADTEST) cpu_bad(0x4ABCDE);
#endif
        cpu_run();
#ifdef SLOWTEST
        { for (volatile int i = 0; i < SLOWTEST; i++) ; }
#endif
        { extern volatile u32 logic_ticks; logic_ticks += (*(volatile u8 *)0xFFFFFE81 - lt0) & 255; }
        ui_after();
#ifdef NOCREDIT
        { volatile u8 *w = (volatile u8 *)0x26000000; static u32 kc; if (++kc > NOCREDIT) { w[0x81A2] = 0; w[0x81A3] = 0; } }   /* test: the credits gone (back to the attract demo) */
#endif
#ifdef DIETEST
        { volatile u8 *w = (volatile u8 *)0x26000000; static u32 kc; if (++kc > DIETEST && kc < DIETEST + 3 && w[0x81D8] == w[0x81D7]) { w[0x8288] = 0; w[0x8289] = 0; } }   /* test: the 1P troops gone (continue screen) */
#endif
#ifdef KILLCHAR
        { volatile u8 *w = (volatile u8 *)0x26000000; static u32 kc; if (++kc > 11000 && w[0x81D8] == w[0x81D7]) w[0x828F] = KILLCHAR; }   /* test: the boss kill picture of another character */
#endif
#ifdef STAGESEL
        { volatile u8 *w = (volatile u8 *)0x26000000;          /* test: start in stage STAGESEL (ROUND screen), invincible */
          if (w[0x81D8] == 48 && w[0x81D7] == 0) w[0x81D7] = STAGESEL;
#ifndef INVTEST
          w[0x8289] = 0x56; w[0x828B] = 0x56;
#endif
        }
#endif
        frames++; { extern volatile u32 lframes; lframes = frames; }
    }
    render_logic_done();
    { static u32 f0, v0, l0; if (vcount - v0 >= 60) { extern volatile int fps_game, fps_logic; fps_game = (frames_drawn - f0) * 60 / (vcount - v0); fps_logic = (frames - l0) * 60 / (vcount - v0); { extern volatile u32 logic_ticks; static u32 t0; extern volatile int logic_busy; logic_busy = (logic_ticks - t0) / 56; t0 = logic_ticks; }
#ifdef VINTDBG
{ extern volatile u32 vint_ticks, vint_busy; static u32 t1; extern volatile int logic_busy; logic_busy = vint_busy - t1; t1 = vint_busy; }
#endif
 f0 = frames_drawn; l0 = frames; v0 = vcount; } }

}
void slave_main(void) { while (*(volatile u32 *)((u32)&hot_ready | 0x20000000) != 0x484F5421) ; render_slave(); }
