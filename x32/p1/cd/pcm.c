/* OKI MSM6295 voices on the Mega-CD RF5C164.
 * The arcade's own OKI ADPCM ROM (up to its last phrase, 237 KB) is read from the CD into PRG-RAM 0x44C00-0x7FFFF and decoded
 * here at the OKI's native rate (1 MHz / 132 = 7576 Hz), like the original board: no offline resampling.
 * Each OKI channel i plays on PCM channel i through an 8 KB ring in wave RAM (i * 0x2000); the timer IRQ
 * (~3.9 ms) decodes ahead of the PCM play pointer by LEAD samples.
 * Sound effects (arcade commands 0x20..0x7F: the YM2151 effects, rendered offline to 16 kHz PCM, loaded
 * from the CD data track) play like on the arcade's YM2151: each effect has its channel (6 or 7 -> PCM
 * channel 4 or 5) and a new one on a busy channel starts only if the arcade driver lets it cut the one
 * playing (sfx_pri table from the CD, measured with the Z80 driver: tools/mksfxpri.py).
 * Main -> sub: sound commands in an 8-byte ring at CMD_MAIN[6..13], write count at CMD_MAIN[14].
 * voice_tab: per command OKI phrase, channel/volume byte, stop mask. */
#include "sub.h"
#define PCM(r)   (*(vu8 *)(0xFF0001 + ((r) << 1)))
#define WAVE(a)  (*(vu8 *)(0xFF2001 + (((a) & 0xFFF) << 1)))
#define OKI      ((const u8 *)0x44C00)          /* the arcade OKI ROM up to its last phrase (0x3B400 bytes) */
#define RING     0x1FFF
#define LEAD     320
extern McdRegs *mcd;
extern const u8 voice_tab[];
static const short steps[49] = {16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552};
static const signed char sadj[8] = {-1,-1,-1,-1,2,4,6,8};
static const u8 okivol[16] = {0x20,0x16,0x10,0x0b,0x08,0x06,0x04,0x03,0x02,0,0,0,0,0,0,0};
typedef struct { const u8 *p, *end; short g; u16 st; u8 nib, on, vol, cur; u16 wp, quiet; const u8 *lut; u8 kon, pad; } Voice;   /* pcm_asm.s */
extern void dec_seg(Voice *w, vu8 *out, int cnt);
static Voice v[4];
static u8 rd, ready;
/* sound effects: sfx_tab[cmd - 0x20] = { sub address, length } (from the CD), 0 = none */
typedef Voice Fx;                                  /* effects are OKI ADPCM too (16 kHz, from the CD) */
static Fx fx[4];
#define FXVOL 0x10                                  /* same level as the v1.0 8-bit samples */
u32 *sfx_tab;
const u8 *sfx_pri;                     /* u8 n, 3 pad, n x { id, ch, 0, 0, u32 cuts 0-31, u32 cuts 32-63 (bit j: may cut effect j) } */
static u8 fx_cur[2];
#define FXLEAD 640

u32 dtab4[49 * 16];                                /* ADPCM: (step, code) -> delta << 16 | next step x64 */
static u8 vlut[16][256];                           /* voice output: [volume index][128 + (12-bit sample >> 4)] -> sign/magnitude */
static u8 fxlut[256];
static u8 chmask = 0xFF;                           /* PCM channel on/off (1 = off) */
static u8 dbg_max, dbg_behind[2]; static u32 dbg_ns, dbg_dt;
static void chan_sel(u16 ch) { PCM(7) = 0xC0 | ch; }
void pcm_init(void)
{
    u16 ch, i, b;
    for (i = 0; i < 49 * 16; i++) {
        u16 st = i >> 4, n = i & 15; short d = ((2 * (n & 7) + 1) * steps[st]) >> 3;
        short t = st + sadj[n & 7];
        dtab4[i] = ((u32)(u16)(n & 8 ? -d : d) << 16) | ((t < 0 ? 0 : t > 48 ? 48 : t) << 6);
    }
    for (i = 0; i < 16; i++) for (b = 0; b < 256; b++) {
        short o = (short)(((long)((short)(signed char)(b - 128) * 16 + 8) * okivol[i]) >> 9);   /* /0x20 x 8 bit: no clipping (ENV 0xFF for the level) */
        vlut[i][b] = o >= 0 ? 0x80 | (o > 0x7E ? 0x7E : o) : (o < -0x7F ? 0x7F : -o);
    }
    for (b = 0; b < 256; b++) { short o = (short)(signed char)(b - 128); fxlut[b] = o >= 0 ? 0x80 | (o > 0x7E ? 0x7E : o) : (o < -0x7F ? 0x7F : -o); }
    PCM(8) = 0xFF;                                     /* all channels off */
    for (b = 0; b < 16; b++) { PCM(7) = 0x80 | b; for (i = 0; i < 0x1000; i++) WAVE(i) = 0; }
    for (ch = 0; ch < 8; ch++) {
        u16 base = ch << 13;
        PCM(7) = 0x80 | ((base + RING) >> 12); WAVE(base + RING) = 0xFF;   /* loop marker */
        chan_sel(ch);
        PCM(0) = ch < 4 || ch == 7 ? 0xFF : 0x58; PCM(1) = 0xFF;  /* ENV (balance against the CD music; 7: the jingle), PAN */
        if (ch < 4) { PCM(2) = 0xDD; PCM(3) = 0x01; }  /* FD 0x1DD: 7582 Hz */
        else { PCM(2) = 0xEF; PCM(3) = 0x03; }         /* FD 0x3EF: 16000 Hz */
        PCM(4) = 0; PCM(5) = base >> 8;                /* loop start */
        PCM(6) = base >> 8;                            /* start */
        Voice *w = ch < 4 ? &v[ch] : &fx[ch - 4];
        w->wp = 0; w->on = 0; w->kon = 0; w->lut = fxlut + 128;
    }
    chmask = 0xFF; PCM(8) = chmask;                    /* keyed on when a voice starts (pcm_service) */
    PCM(7) = 0x80;
    rd = mcd->CMD_MAIN[14];
    ready = 1;
}
static void fx_cmd(u8 c)
{
    if (!sfx_tab || c < 0x20 || c > 0x7F) return;
    u32 a = sfx_tab[(c - 0x20) * 2], n = sfx_tab[(c - 0x20) * 2 + 1];
    u16 fd = (n >> 31) ? 0x2F3 : 0x3EF;             /* V3.2: length bit 31 = a 12 kHz effect (FD 0x2F3), else 16 kHz */
    n &= 0x7FFFFFFF;
    if (!a || !n) return;
    u16 i = 0, k, np = sfx_pri ? sfx_pri[0] : 0;
    const u8 *e = 0;
    for (k = 0; k < np; k++) if (sfx_pri[4 + k * 12] == c) { e = sfx_pri + 4 + k * 12; break; }   /* V3.2: 12-byte entries, 64-bit masks */
    if (e) {
        i = e[1] == 6 ? 0 : 1;                         /* YM channel 6 / 7 */
        if (fx[i].on) {
            const u8 *q = e + (fx_cur[i] < 32 ? 4 : 8);
            u32 m = ((u32)q[0] << 24) | ((u32)q[1] << 16) | ((u32)q[2] << 8) | q[3];
            if (!(m & (1UL << (fx_cur[i] & 31)))) return;     /* the playing one has priority */
        }
        fx_cur[i] = k;
    } else if (c == 0x7F) i = 3;                     /* the level-up jingle: its own channel (PCM 7, full ENV) */
    else i = 2;
    Fx *f = &fx[i];
    chan_sel(4 + i); PCM(2) = fd & 0xFF; PCM(3) = fd >> 8;
    f->on = 0; f->p = (const u8 *)a; f->end = (const u8 *)a + n; f->nib = 0; f->g = 0; f->st = 0; f->lut = fxlut + 128; f->quiet = 0; f->on = 1;
}

static void oki_cmd(u8 c)
{
    const u8 *e = voice_tab + ((c - 0x80) << 2);
    u8 ph = e[0], cv = e[1], stop = e[2], i;
    for (i = 0; i < 4; i++) if (stop & (8 << i)) v[i].on = 0;
    if (!ph) return;
    const u8 *p = OKI + (ph << 3);
    u32 s = (((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2]) & 0x3FFFF, t = (((u32)p[3] << 16) | ((u32)p[4] << 8) | p[5]) & 0x3FFFF;
    if (s >= t) return;
    for (i = 0; i < 4; i++)
        if (cv & (0x10 << i)) {
            Voice *w = &v[i];
            w->on = 0; w->p = OKI + s; w->end = OKI + t + 1; w->nib = 0; w->g = 0; w->st = 0; w->lut = vlut[cv & 15] + 128; w->quiet = 0; w->on = 1;
        }
}

/* decode n samples into channel ch's ring from w->wp (split at the 4 KB wave RAM banks and the ring end) */
static void fill(Voice *w, u16 ch, u16 n)
{
    u16 base = ch << 13, wp = w->wp;
    while (n) {
        u16 a = base + wp, k = 0x1000 - (a & 0xFFF), r = RING - wp;
        if (k > r) k = r;
        if (k > n) k = n;
        PCM(7) = 0x80 | (a >> 12);
        dec_seg(w, (vu8 *)(0xFF2001 + ((a & 0xFFF) << 1)), k); dbg_ns += k;
        if (!w->on) w->quiet += k;
        wp += k; if (wp == RING) wp = 0;
        n -= k;
    }
    w->wp = wp;
}
/* one PCM channel: key on at a voice start (ring primed), keep LEAD samples ahead, key off after the tail */
static void service(Voice *w, u16 ch, u16 lead, u16 cap)
{
    if (!w->kon) {
        if (!w->on) return;
        w->wp = 0; w->quiet = 0; fill(w, ch, cap);
        chmask &= ~(1 << ch); PCM(8) = chmask; w->kon = 1;
        return;
    }
    u16 base = ch << 13, hi, lo, p;
    do { hi = PCM(0x11 + (ch << 1)); lo = PCM(0x10 + (ch << 1)); } while (hi != PCM(0x11 + (ch << 1)));
    p = (((hi << 8) | lo) - base) & 0x1FFF;
    if (p >= RING) p = 0;
    u16 ahead = (w->wp + RING - p) % RING;
    if (ahead > lead + 64) { w->wp = p; ahead = 0; dbg_behind[ch >> 2]++; }   /* fell behind: restart at the play pointer */
    u16 n = ahead < lead ? lead - ahead : 0;
    if (n > cap) n = cap;                          /* spread a start over several ticks */
    if (n) fill(w, ch, n);
    if (!w->on && w->quiet >= lead + 256) { chmask |= 1 << ch; PCM(8) = chmask; w->kon = 0; }
}
/* effects / voices volume 0-10 (options): channel ENV */
static void set_vol(u8 lv)
{
    u16 ch;
    if (lv > 10) lv = 10;
    static const u8 lvv[11] = { 0, 25, 51, 76, 102, 127, 153, 178, 204, 229, 255 };     /* x / 10 */
    for (ch = 0; ch < 8; ch++) { chan_sel(ch); PCM(0) = ch < 4 || ch == 7 ? lvv[lv] : (u16)(0x58 * lvv[lv]) >> 8; }
}
void pcm_service(void)
{
    if (!ready) return;
    u16 t0 = mcd->SWATCH;
    u8 wc = mcd->CMD_MAIN[14];
    while (rd != wc) { u8 c = mcd->CMD_MAIN[6 + (rd & 7)]; if (c >= 0xF0) set_vol(c & 15); else if (c >= 0x80) oki_cmd(c); else fx_cmd(c); rd++; }
    u16 ch;
    for (ch = 0; ch < 4; ch++) service(&v[ch], ch, LEAD, 96);
    for (ch = 4; ch < 8; ch++) service(&fx[ch - 4], ch, FXLEAD, 192);
    { static u8 ncall, mx, nover; u16 dt = (mcd->SWATCH - t0) & 0xFFF; if (dt > mx) mx = dt > 255 ? 255 : dt; if (dt >= 127) nover++; dbg_dt += dt;
      if (++ncall == 0) { dbg_max = mx; mx = 0; mcd->CMD_SUB[3] = (u8)(dbg_dt * 100 / (256 * 128)); dbg_dt = 0; }
      mcd->CMD_SUB[9] = dbg_max; mcd->CMD_SUB[10] = dbg_behind[0]; mcd->CMD_SUB[11] = dbg_behind[1]; }
}
