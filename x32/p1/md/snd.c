/* Sound (Mega-CD, cart mode 1): the sub-CPU program (cd/) plays the music from CD audio tracks
 * (MSU-MD style commands), the arcade OKI voices and the sound effects (PCM chip), with its data read from
 * the CD data track.  The 32X sends the arcade sound commands through COMM8 (0x4000 | cmd). */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned long u32;
#define R8(a)  (*(volatile u8 *)(a))
#define R16(a) (*(volatile u16 *)(a))
#define R32(a) (*(volatile u32 *)(a))
#include "mus_tab.h"
#ifndef NOJ
#define NOJ 0
#endif
extern const u8 sub_bin[], sub_bin_end[];
/* V3.5: the arcade's sound effects on the YM2612, played by a Z80 program (tools/fm/fmsfx.py: data/fm/fmsfx.bin in the
 * cartridge at 0x36B800, seen through the 1MB bank window): +0 96 x {u16 offset, u16 length, u16 offset / length of the
 * channel 3 version, u8 slot, u8 index}, +0x480 64 x u64 cut masks, +0x680 u16 n + Z80 image, u16 n + slot controls
 * (Z80 0x1F00: +0 state, +1 effect, +5 YM2612 channel, +6 special mode).  An effect is copied into its slot's buffer in
 * Z80 RAM and started there; the arcade's priority rules decide whether it may cut the one playing.  Effects whose
 * YM2151 coarse detune matters play on channel 3 in its special mode when the other slot is not using it. */
#define FM_ROM ((const u8 *)(0x900000 + (0x36B800 - 0x300000)))
#define Z80RAM 0xA00000
static u8 fm_ok;
static void z80_bus(void) { R16(0xA11100) = 0x100; while (R16(0xA11100) & 0x100); }
static void z80_free(void) { R16(0xA11100) = 0; }
static void fm_init(void)
{
    const u8 *f = FM_ROM; u16 n = (f[0x680] << 8) | f[0x681], i; const u8 *s = f + 0x682;
    if (n == 0 || n > 0x1F00) return;
    R16(0xA11100) = 0x100; R16(0xA11200) = 0x100;
    while (R16(0xA11100) & 0x100);
    for (i = 0; i < n; i++) R8(Z80RAM + i) = s[i];
    s += n; n = (s[0] << 8) | s[1]; s += 2;
    for (i = 0; i < n; i++) R8(Z80RAM + 0x1F00 + i) = s[i];
    R16(0xA11200) = 0; z80_free();
    { volatile u16 k; for (k = 0; k < 64; k++); }
    R16(0xA11200) = 0x100;
    fm_ok = 1;
}
static int fm_play(u8 c)
{
    const u8 *f = FM_ROM, *e = f + (c - 0x20) * 12;
    u8 slot = e[8], idx = e[9]; u16 off = (e[0] << 8) | e[1], len = (e[2] << 8) | e[3], i;
    if (slot > 1) return 0;
    u32 ctl = Z80RAM + 0x1F00 + slot * 0x20, oth = Z80RAM + 0x1F00 + (slot ^ 1) * 0x20;
    u8 ch = slot ? 0 : 1, multi = 0;
    z80_bus();
    if (R8(ctl)) {                                   /* playing: may this one cut it? */
        u8 cur = R8(ctl + 1) & 63;
        if (!(f[0x480 + idx * 8 + 7 - (cur >> 3)] & (1 << (cur & 7)))) { z80_free(); return 1; }
    }
    if (e[6] | e[7]) {                               /* a channel 3 version: when the other slot is not on channel 3 */
        if (!(R8(oth) && R8(oth + 5) == 2)) { off = (e[4] << 8) | e[5]; len = (e[6] << 8) | e[7]; ch = 2; multi = 1; }
    }
    { const u8 *s = f + off; volatile u8 *d = (volatile u8 *)(Z80RAM + (slot ? 0x1400 : 0x0900)); for (i = 0; i < len; i++) d[i] = s[i]; }
    R8(ctl + 5) = ch; R8(ctl + 6) = multi; R8(ctl + 1) = idx; R8(ctl) = 1;
    z80_free();
    return 1;
}
static void fm_vol(u16 v)
{
    static const u8 att[11] = { 127, 32, 24, 19, 16, 13, 11, 9, 8, 6, 5 };   /* TL units (0.75 dB); 5 at full: effects / CD music balance as in the arcade */
    if (!fm_ok) return;
    z80_bus(); R8(Z80RAM + 0x1F40) = att[v > 10 ? 10 : v]; z80_free();
}
static u16 snd_state;               /* 0 none, 1 starting, 2 ready */
static u8 ring_w;
void snd_start(void)
{
#ifndef NOFM
    fm_init();
#endif
    if (R8(0xA10001) & 0x20) return;                    /* no Mega-CD (version register: DISK) */
    R8(0xA12001) = 2;                                   /* sub CPU: reset + bus request */
    { u32 k = 0; while (!(R8(0xA12001) & 2) && ++k < 100000); }
    R8(0xA12002) = 0;                                   /* PRG-RAM write protect off */
    R8(0xA12003) = 0;                                   /* PRG-RAM bank 0 */
    { const u16 *s = (const u16 *)sub_bin; volatile u16 *d = (volatile u16 *)0x420000;
      while (s < (const u16 *)sub_bin_end) *d++ = *s++; }
    { u16 i; for (i = 0; i < 16; i++) R8(0xA12010 + i) = 0; }
    R8(0xA12003) = 2;                                   /* 2M Word RAM to the sub CPU (DMNA) */
    R8(0xA12001) = 1;                                   /* run */
    ring_w = 0;
    snd_state = 1;
}
static u8 bg_busy; static u16 pend_cmd; static u32 pend_arg;
static void msu_cmd(u16 cmd, u32 arg)
{
    if (bg_busy) { pend_cmd = cmd; pend_arg = arg; return; }   /* the sub CPU is reading a background: send it after */
    { u32 k = 0; while (R8(0xA12020) != 0 && ++k < 20000); }  /* sub CPU waiting for a command (SUB_STAT_READY) */
    R16(0xA12010) = cmd;
    R32(0xA12012) = arg;
    R8(0xA1201F) = R8(0xA1201F) + 1;
}
/* called from the main loop */
#ifdef SNDTEST
static u8 snd_test;
void snd_cmd(u8 c);
extern volatile u16 vbl_count;
static void snd_script(void)
{
    static u16 t0, last; static u8 st;
    if (snd_state != 2) return;
    if (!st) { t0 = vbl_count; st = 1; }
    u16 t = vbl_count - t0;
    if (t == last) return;
    last = t;
    snd_test = 1;
#ifdef FMALL                                          /* every YM2612 effect, one per 2 s */
    { static const u8 ids[] = { 0x20,0x21,0x22,0x23,0x24,0x25,0x27,0x28,0x2A,0x2B,0x2C,0x2D,0x31,0x33,0x34,0x38,0x39,0x3E,0x3F,0x40,
                                0x41,0x43,0x44,0x45,0x46,0x48,0x49,0x4D,0x4F,0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A };
      static u16 nt; static u8 k; if (!nt) nt = 60; if (t >= nt && k < sizeof ids) { snd_cmd(ids[k++]); nt += 300; } }
#elif defined(JTEST)
    if (t == 60) snd_cmd(0x04);
    if (t == 400) snd_cmd(0x16);
#else
    if (t == 60) snd_cmd(0x52);
    if (t >= 100 && t <= 220 && ((t - 100) & 15) == 0) snd_cmd(0x51);
    if (t == 400) snd_cmd(0x25);
    if (t == 600) snd_cmd(0x81);
    if (t == 700) snd_cmd(0x8f);
    if (t == 800) { snd_cmd(0x85); snd_cmd(0x91); }
#endif
    snd_test = 0;
}
#endif
void snd_poll(void)
{
#ifdef SNDTEST
    snd_script();
#endif
    if (snd_state == 2) return;
    { u16 i; for (i = 0; i < 16; i++) R8(0xFF0F40 + i) = R8(0xA12020 + i); R16(0xFF0F34) = snd_state; }
    if (snd_state == 1 && R8(0xA12020) == 0 && (R8(0xA12022) & 0x80)) snd_state = 2;   /* sub ready (data loaded, or failed: music only) */
}
/* stage background data loaded into Word RAM and returned to the main CPU */
/* 1: Word RAM has it, -1: use the cartridge copy (no Mega-CD, or the CD data could not be read), 0: wait */
int snd_bg_ready(void)
{
    if (snd_state == 0) return -1;
    if (snd_state != 2) return 0;
    if (R8(0xA12022) == 0x80) return (R8(0xA12003) & 1) ? 1 : 0;
    return -1;
}
void snd_cmd(u8 c)
{
#ifdef SNDTEST
    if (!snd_test) return;                        /* test build: only the scripted commands */
#endif
    if (c >= 0x20 && c <= 0x7E && fm_ok && fm_play(c)) return;   /* V3.5: YM2612 */
    if (snd_state != 2) return;
    if (c >= 0x01 && c <= 0x1F) {
        u16 i;
        for (i = 0; i < sizeof mus_tab / sizeof mus_tab[0]; i++)
            if (mus_tab[i].cmd == c) {
                u16 lo = mus_tab[i].loopofs;
                if (c == 0x16 && !NOJ) {                  /* level-up jingle: PCM effect 0x7F over the CD music (V2.1: no stop / seek) */
                    R8(0xA12016 + (ring_w & 7)) = 0x7F; ring_w++; R8(0xA1201E) = ring_w; return;
                }
                else if (lo > 1) msu_cmd(0x1A00 | mus_tab[i].track, lo - 1);
                else if (lo == 1) msu_cmd(0x1200 | mus_tab[i].track, 0);
                else msu_cmd(0x1100 | mus_tab[i].track, 0);
                return;
            }
        msu_cmd(0x1300 | 30, 0);
    } else if (c == 0xF0 || c == 0xF1) {
        msu_cmd(0x1300 | 20, 0);
    } else if (c >= 0x20 && c <= 0xEF) {
#ifdef NOFX
        return;
#endif
        R8(0xA12016 + (ring_w & 7)) = c;
        ring_w++;
        R8(0xA1201E) = ring_w;
    }
}

/* volume from the options, level 0-10: k 0 effects/voices (PCM: ring byte 0xF0 | level), 1 music (CD fader) */
void snd_vol(u16 k, u16 v)
{
    if (!k) fm_vol(v);
    if (snd_state != 2) return;
    if (v > 10) v = 10;
    if (k) { static const u8 lv[11] = { 0, 25, 51, 76, 102, 127, 153, 178, 204, 229, 255 }; msu_cmd(0x1500 | lv[v], 0); }
    else { R8(0xA12016 + (ring_w & 7)) = 0xF0 | v; ring_w++; R8(0xA1201E) = ring_w; }
}
/* stage backgrounds: give the 2M Word RAM back to the sub CPU and have it read stage st from the CD (sub.c 0x30) */
int snd_bg_load(u8 st)
{
    if (snd_state != 2 || bg_busy) return 0;
    R8(0xA12003) = R8(0xA12003) | 2;                   /* DMNA: Word RAM to the sub CPU */
    msu_cmd(0x3000 | st, 0);
    bg_busy = 1;
    return 1;
}
/* 0: still loading, 1: stage st in Word RAM (ours again), -1: failed */
int snd_bg_done(u8 st)
{
    u8 r = R8(0xA12021);
    if (r == 0xFF) { bg_busy = 2; return 0; }           /* the sub CPU took the command */
    if (bg_busy != 2 || (r != st && r != 0xFE)) return 0;
    if (!(R8(0xA12003) & 1)) return 0;                  /* RET: Word RAM back to the main CPU */
    bg_busy = 0;
    if (pend_cmd) { u16 c = pend_cmd; pend_cmd = 0; msu_cmd(c, pend_arg); }
    return r == st ? 1 : -1;
}
