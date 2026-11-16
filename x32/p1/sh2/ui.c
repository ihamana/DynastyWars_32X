/* Front end added for the 32X version (runs on the master SH-2, from the cart):
 *  - pad (raw bits from the MD, COMM0) -> arcade inputs; no coins: START in the attract inserts one
 *  - title screen: the coin prompt is replaced by a START / OPTIONS menu
 *  - OPTIONS: charge speed x1-x4, experience x1-x4, lives 1-9 or infinite (V3.23: value 10), SFX / music volume 0-10, stage, invincible, button test,
 *    exit (the arcade program is paused while it is shown); B enter, A / C back, START exit (V2.3)
 *  - in game: charge / experience gains multiplied; on the continue screen START uses a life (no automatic continue)
 *  - V1.9 2P: pad 2 plays the arcade's player 2; START joins (coin + START 2) while its HUD panel shows the prompt,
 *    each player with his own lives */
#include "mars.h"
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef short s16;
#define WRAM ((volatile u8 *)0x06000000)
/* raw pad bits (COMM0, 1 = pressed) */
#define P_U 0x01
#define P_D 0x02
#define P_L 0x04
#define P_R 0x08
#define P_A 0x10
#define P_B 0x20
#define P_C 0x40
#define P_S 0x80
#define P_X 0x100
#define P_Y 0x200
#define P_Z 0x400
#define P_M 0x800
#define P_6 0x8000
extern int ui_title_visible(void);          /* render.c: title screen (CAPCOM logo rows) shown */
extern int ui_title_credit(void);           /* render.c: "CREDIT" shown on the title */
/* state shared with the renderer (copied into each packet) */
#ifndef UIOPT
#define UIOPT 1, 1, 3
#endif
#define NSEL 10                             /* charge, exp, lives, SFX volume, music volume, stage (V1.10), invincible (V2.6), full-screen attack (V3.8), button test, exit */
#ifndef INVTEST
#define INVTEST 0
#endif
#ifndef FSATEST
#define FSATEST 0
#endif
volatile u8 ui_mode, ui_sel, ui_opt[8] = { UIOPT, 10, 10, 1, INVTEST, FSATEST }, ui_six;
static u8 stage_go;                         /* stage chosen in the options: set on the first ROUND screen */
volatile u8 ui_volq;                        /* volume changed: bit 0 SFX, bit 1 music (render.c sends it to the MD) */
volatile u16 ui_pad, ui_hold;
static u16 prev;
static u16 inj_t;                           /* injected coin/start sequence: frames left */
static u8 inj_kind;                         /* 1: coin then start, 2: start only, 3: coin only */
static u8 lives_left, in_continue, playing;
#define LIVES_INF (ui_opt[2] > 9)               /* V3.23: lives = infinite (shown as the infinity sign): continues never use one up */
static u8 lives2, joined2, inj_pl;           /* 2P lives (after joining), joined this game, injection for player 2 */
volatile u8 ui_in2 = 0xFF;                 /* player 2 IN1 byte (main.c: io even bytes) */
static u16 prev2;
extern volatile u16 pad_raw[2];
extern int ui_panel_prompt(int k);          /* render.c: HUD panel k shows the press start / insert coin prompt */

static u16 arcade(u16 raw, u16 in0)
{
    u16 in1 = 0xFF;
    if (raw & P_R) in1 &= ~0x01;
    if (raw & P_L) in1 &= ~0x02;
    if (raw & P_D) in1 &= ~0x04;
    if (raw & P_U) in1 &= ~0x08;
    if (raw & P_A) in1 &= ~0x10;
    if (raw & P_B) in1 &= ~0x20;
    if (raw & P_C) in1 &= ~0x40;
    return (in0 << 8) | in1;
}
static u8 ret_opt;                                  /* V3.13: just left the options */
static void inject(u8 kind) { if (!inj_t) { inj_kind = kind; inj_t = 24; inj_pl = 0; } }
static void inject2(void) { if (!inj_t) { inj_kind = 1; inj_t = 24; inj_pl = 1; } }
/* injected coin (frames 24-21) and start (frames 10-7) */
static u16 inj_in0(void)
{
    if (!inj_t) return 0xFF;
    u16 t = inj_t--, in0 = 0xFF;
    if ((inj_kind == 1 || inj_kind == 3) && t > 20) in0 &= ~0x01;
    if ((inj_kind == 1 || inj_kind == 2) && t <= 10 && t > 6) in0 &= ~(inj_pl ? 0x20 : 0x10);
    return in0;
}
/* returns the arcade input word (IN0 << 8 | IN1); *run = 0 pauses the arcade program */
u16 ui_frame(u16 raw, int *run)
{
    u16 edge = raw & ~prev;
    prev = raw;
    u16 raw2 = pad_raw[1], edge2 = raw2 & ~prev2; prev2 = raw2;
    ui_in2 = 0xFF;
    if (raw & P_6) ui_six = 1;
    ui_pad = raw;
    *run = 1;
    u8 scene = WRAM[0x81D8];
#ifdef UITEST
    { static u32 fc; if (++fc == UITEST) { ui_mode = 2; ui_sel = 2; ui_opt[2] = 10; } }
#endif
    if (ui_mode >= 2) {                                   /* options / button test: game paused */
        *run = 0;
        if (ui_mode == 3) {
            if (raw & 0x0FFF) { if (++ui_hold >= 60) { ui_mode = 2; ui_hold = 0; prev = 0xFFFF; } }
            else ui_hold = 0;
            return 0xFFFF;
        }
        if (edge & P_U) ui_sel = (ui_sel + NSEL - 1) % NSEL;
        if (edge & P_D) ui_sel = (ui_sel + 1) % NSEL;
        if (ui_sel < 8 && (edge & (P_L | P_R))) {
            u8 vol = ui_sel == 3 || ui_sel == 4;
            u8 onoff = ui_sel == 6 || ui_sel == 7;
            u8 lo = vol || onoff ? 0 : 1, hi = vol ? 10 : ui_sel == 2 ? 10 : ui_sel == 5 ? 8 : onoff ? 1 : 4, v = ui_opt[ui_sel];
            if (vol) {                                    /* volumes stop at the ends */
                if ((edge & P_L) && v > lo) v--;
                if ((edge & P_R) && v < hi) v++;
            } else {
                if (edge & P_L) v = v > lo ? v - 1 : hi;
                if (edge & P_R) v = v < hi ? v + 1 : lo;
            }
            if (v != ui_opt[ui_sel] && vol) ui_volq |= 1 << (ui_sel - 3);
            ui_opt[ui_sel] = v;
        }
        if (edge & P_S) { ui_mode = 1; ui_sel = 1; ret_opt = 1; return 0xFFFF; }   /* V2.3: START = exit (back to the title) */
        if (edge & P_B) {                                 /* B: enter */
            if (ui_sel == 8) { ui_mode = 3; ui_hold = 0; }
            else if (ui_sel == 9) { ui_mode = 1; ui_sel = 1; ret_opt = 1; }
        }
        if (edge & (P_A | P_C)) { ui_mode = 1; ui_sel = 1; ret_opt = 1; }   /* A / C: back */
        return 0xFFFF;
    }
    u16 in0 = 0xFF;
    if (raw & P_Z) in0 &= ~0x01;                          /* Z: coin */
    int title = scene >= 40 && ui_title_visible();
    if (title) {
        if (ui_mode != 1) { ui_mode = 1; ui_sel = ret_opt; }   /* V3.13: back from the options: the cursor stays on Options (the title can drop out for a frame) */
        ret_opt = 0;
        if (edge & (P_U | P_D)) ui_sel ^= 1;
        if (edge & (P_B | P_S)) {                         /* B / START: enter */
            if (ui_sel == 0) { inject(ui_title_credit() ? 2 : 1); lives_left = ui_opt[2] - 1; playing = 1; joined2 = 0; stage_go = ui_opt[5] - 1; }
            else { ui_mode = 2; ui_sel = 0; *run = 0; return 0xFFFF; }
        }
        return (inj_in0() << 8) | 0xFF;
    }
    if (ui_mode == 1) ui_mode = 0;
    if (scene < 40) ret_opt = 0;
    if (scene == 49) { playing = 0; joined2 = 0; stage_go = 0; }   /* game over */
    if (stage_go && playing) {                            /* chosen stage: kept in place from the start until the first ROUND screen shows it */
        if (scene == 48 && WRAM[0x81D7] == stage_go) stage_go = 0;
        else if (WRAM[0x81D7] == 0) WRAM[0x81D7] = stage_go;
    }
    /* attract (no game started from the menu): START = insert a coin (the title then shows the menu) */
    if (!playing && (edge & P_S)) { inject(3); return (inj_in0() << 8) | 0xFF; }
    /* continue screens (44-47): the countdown runs; START continues (uses a life) while lives are left,
       otherwise the countdown ends in the game over */
    int cont = scene >= 44 && scene <= 47;
    if (cont && !in_continue && (edge & P_S) && lives_left) { in_continue = 1; lives_left -= !LIVES_INF; inject(1); }
    if (cont && !in_continue && (edge2 & P_S) && joined2 && lives2) { in_continue = 1; lives2 -= !LIVES_INF; inject2(); }
    /* 2P game: a player whose HUD panel shows the prompt joins / comes back with START (one of his lives) */
    if (playing && !cont && !inj_t) {
        if ((edge2 & P_S) && ui_panel_prompt(1)) {
            if (!joined2) { joined2 = 1; lives2 = ui_opt[2] - 1; inject2(); }
            else if (lives2) { lives2 -= !LIVES_INF; inject2(); }
        } else if ((edge & P_S) && ui_panel_prompt(0) && lives_left) { lives_left -= !LIVES_INF; inject(1); }
    }
    if (!cont) in_continue = 0;
    if (raw & P_S) in0 &= ~0x10;
    if (raw2 & P_S) in0 &= ~0x20;
    in0 &= inj_in0();
    ui_in2 = arcade(raw2, 0) & 0xFF;
    return arcade(raw, in0);
}
/* V2.1 (render.c): a stage chosen in the options is on its way (the MD loads its background / ROUND map early) */
int ui_stage_pending(void) { return stage_go && playing; }
/* option hooks called from the translated arcade code (tools/patch_gen.py) */
unsigned char chg_step = 1;                 /* charge: added per frame instead of 1 */
static u16 bcd2bin(u16 b) { return ((b >> 12) & 15) * 1000 + ((b >> 8) & 15) * 100 + ((b >> 4) & 15) * 10 + (b & 15); }
static u16 bin2bcd(u16 v) { u16 b = (v / 1000) << 12; v %= 1000; b |= (v / 100) << 8; v %= 100; return b | ((v / 10) << 4) | (v % 10); }
/* an experience item is taken: its value (4 BCD digits, x10 points), multiplied, without passing 9999 in all */
unsigned exp_item(unsigned item, unsigned have)
{
    u16 e = bcd2bin(item) * ui_opt[1], h = bcd2bin(have);
    if (h > 9999) h = 9999;
    if (e > 9999 - h) e = 9999 - h;
    return bin2bcd(e);
}
/* V3.19 invincible: called by every sbcd to memory (tools/patch_gen.py): 1 = do not take it (a player's troops), 2 = write 0 (ONEHIT test) */
int inv_bcd(unsigned ea, unsigned pc)
{
    unsigned a = ea & 0xFFFFFF;
#ifdef INVDBG
    if (a >= 0xFF8000 && a < 0xFF8400) { volatile u32 *L = (volatile u32 *)0x26007310; u32 i = L[0] & 63; L[2 + i * 2] = pc; L[3 + i * 2] = a; L[0]++; }
#else
    (void)pc;
#endif
#ifdef ONEHIT
    if (a != 0xFF8288 && a != 0xFF8289 && a != 0xFF828A && a != 0xFF828B) return 2;   /* test: any other life counter -> 0 */
#endif
    if (!ui_opt[6] || !playing) return 0;
    return a == 0xFF8288 || a == 0xFF8289 || (joined2 && (a == 0xFF828A || a == 0xFF828B));
}
void ui_after(void)
{
    chg_step = ui_opt[0];
    { extern void ui_title_port(void); ui_title_port(); }
    { extern void ui_end_credit(void); ui_end_credit(); }
    { extern unsigned char fsa_enable; fsa_enable = ui_opt[7] && (playing || FSATEST); }   /* V3.8 full-screen attack (gen/nat.c fsa_*) */
    /* V2.6 invincible: a player's troops (his life, BCD words 0xFF8288 / 0xFF828A, the top left counter) never go
       down: what a hit took is put back after the frame (gains stay); at least 50 so that one hit cannot end it */
    static u16 keep[2];
#if defined(REPLAY) && defined(INVDBG)
    playing = 1;
#endif
    for (int k = 0; k < 2; k++) {
        volatile u8 *t = WRAM + 0x8288 + k * 2;
        u16 v = (t[0] << 8) | t[1];
#ifdef INVDBG
        if (ui_opt[6] && playing && !(k && !joined2) && v == 0 && keep[k]) (*(volatile u32 *)0x26007300)++;
        if (!k) *(volatile u32 *)0x26007304 = (playing << 24) | (ui_opt[6] << 16) | v;
#endif
        if (!ui_opt[6] || !playing || (k && !joined2) || v == 0) { keep[k] = 0; continue; }
        if (keep[k] && v < keep[k]) v = keep[k];
        if (v < 0x50) v = 0x50;
        t[0] = v >> 8; t[1] = v; keep[k] = v;
    }
}

#ifdef PADTEST
/* test build: scripted pad (raw bits) by vblank number */
static const u16 pt[][2] = {
#include "padtest.h"
};
u16 padtest(u32 n)
{
    u16 v = 0;
    for (u32 i = 0; i < sizeof pt / sizeof pt[0]; i++) if (pt[i][0] <= n) v = pt[i][1];
    return v;
}
#endif
