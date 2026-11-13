/* CD-ROM data sectors (Mode 1, 2048 bytes) read with the CDC (LC8951), polled, DMA to PRG-RAM or Word RAM.
 * The drive plays from the first sector; every decoded sector's header (MSF) is checked and the wanted
 * ones are DMA'd from the CDC buffer.  Used once at start-up (sound data from the data track). */
#include "sub.h"
extern McdRegs *mcd;
extern McdState mstate;
extern void cddCmdExec(u8 cmd, u32 arg);
extern u32 decToMsf(u32 val);
extern u32 msfToDec(u32 val);

static void cdc_w(u8 r, u8 v) { mcd->CDC_RADDR = r; mcd->CDC_RDATA = v; }
static u8 cdc_r(u8 r) { mcd->CDC_RADDR = r; return mcd->CDC_RDATA; }

/* lba: sector number in the disc (0 = 00:02:00); dest: sub-CPU address (PRG-RAM < 0x80000, Word RAM 0x80000+) */
u16 cd_read(u32 lba, u16 n, u32 dest)
{
    u16 got = 0, tries = 0;
    cdc_w(0x0F, 0);                                   /* RESET */
    cdc_w(0x01, 0x02);                                /* IFCTRL: DOUTEN (no interrupts: polled) */
    cdc_w(0x0A, 0x80 | 0x04);                         /* CTRL0: DECEN | WRRQ */
    cdc_w(0x0B, 0x00);                                /* CTRL1 */
    u32 want = lba;
    cddCmdExec(CDD_CMD_PLAY, decToMsf(want + 150));
    u32 last = 0xFFFFFFFF, idle = 0;
    while (got < n) {
        u32 h = ((u32)cdc_r(4) << 16) | ((u32)cdc_r(5) << 8) | cdc_r(6);
        if (h == last) {
            if (++idle > 150000) {                    /* nothing decoded for a long time: seek again */
                if (++tries > 8) break;
                cddCmdExec(CDD_CMD_PLAY, decToMsf(want + 150)); idle = 0;
            }
            continue;
        }
        idle = 0; last = h;
        if (h < 0x000200 || cdc_r(7) != 0x01) continue;  /* no (Mode 1) sector yet */
        u32 s = msfToDec(h) - 150;
        if (s > want + 1) {                               /* missed one: seek back */
            if (++tries > 8) break;
            cddCmdExec(CDD_CMD_PLAY, decToMsf(want + 150));
            last = 0xFFFFFFFF;
            continue;
        }
        if (s != want) continue;
        u16 pt = ((u16)cdc_r(8) | ((u16)cdc_r(9) << 8)) + 4;   /* block pointer + header */
        mcd->CDC_MOD = dest >= 0x80000 ? 7 : 5;       /* DD: Word RAM / PRG-RAM DMA */
        mcd->CDC_DADDR = (dest & (dest >= 0x80000 ? 0x3FFFF : 0x7FFFF)) >> 3;
        cdc_w(0x02, 0xFF); cdc_w(0x03, 0x07);         /* DBC = 2047 */
        cdc_w(0x04, pt & 0xFF); cdc_w(0x05, pt >> 8); /* DAC */
        cdc_w(0x06, 0);                               /* DTTRG */
        { u32 k = 0; while (!(mcd->CDC_MOD & 0x80) && ++k < 200000); }   /* EDT */
        cdc_w(0x07, 0);                               /* DTACK */
        dest += 2048; want++; got++;
    }
    cddCmdExec(CDD_CMD_PAUSE, 0);
    cdc_w(0x0A, 0);                                   /* decoder off */
    mcd->CMD_SUB[4] = last >> 16; mcd->CMD_SUB[5] = last >> 8; mcd->CMD_SUB[6] = last; mcd->CMD_SUB[7] = tries;
    mcd->CMD_SUB[8] = got;
    return got;
}
