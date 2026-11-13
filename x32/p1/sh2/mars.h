/* 32X hardware registers (SH2 view, cache-through) */
#ifndef MARS_H
#define MARS_H
#include <stdint.h>
#define MARS_CRAM           (*(volatile uint16_t *)0x20004200)
#define MARS_FRAMEBUFFER    (*(volatile uint16_t *)0x24000000)
#define MARS_OVERWRITE_IMG  (*(volatile uint16_t *)0x24020000)
#define MARS_SYS_INTMSK     (*(volatile uint16_t *)0x20004000)
#define MARS_SYS_DMACTR     (*(volatile uint16_t *)0x20004006)
#define MARS_SYS_DMASAR     (*(volatile uint32_t *)0x20004008)
#define MARS_SYS_DMADAR     (*(volatile uint32_t *)0x2000400C)
#define MARS_SYS_DMALEN     (*(volatile uint16_t *)0x20004010)
#define MARS_SYS_DMAFIFO    (*(volatile uint16_t *)0x20004012)
#define MARS_SYS_COMM0      (*(volatile uint16_t *)0x20004020)
#define MARS_SYS_COMM2      (*(volatile uint16_t *)0x20004022)
#define MARS_SYS_COMM4      (*(volatile uint16_t *)0x20004024)
#define MARS_SYS_COMM6      (*(volatile uint16_t *)0x20004026)
#define MARS_SYS_COMM8      (*(volatile uint16_t *)0x20004028)
#define MARS_SYS_COMM10     (*(volatile uint16_t *)0x2000402A)
#define MARS_SYS_COMM12     (*(volatile uint16_t *)0x2000402C)
#define MARS_SYS_COMM14     (*(volatile uint16_t *)0x2000402E)
#define MARS_PWM_CTRL       (*(volatile uint16_t *)0x20004030)
#define MARS_PWM_CYCLE      (*(volatile uint16_t *)0x20004032)
#define MARS_PWM_LEFT       (*(volatile uint16_t *)0x20004034)
#define MARS_PWM_RIGHT      (*(volatile uint16_t *)0x20004036)
#define MARS_PWM_MONO       (*(volatile uint16_t *)0x20004038)
#define MARS_VDP_DISPMODE   (*(volatile uint16_t *)0x20004100)
#define MARS_VDP_SHIFT      (*(volatile uint16_t *)0x20004102)
#define MARS_VDP_FILLEN     (*(volatile uint16_t *)0x20004104)
#define MARS_VDP_FILADR     (*(volatile uint16_t *)0x20004106)
#define MARS_VDP_FILDAT     (*(volatile uint16_t *)0x20004108)
#define MARS_VDP_FBCTL      (*(volatile uint16_t *)0x2000410A)
#define MARS_VDP_PRIO_32X   0x0080
#define MARS_VDP_MODE_256   0x0001
#define MARS_VDP_MODE_32K   0x0002
#define MARS_VDP_VBLK       0x8000
#define MARS_VDP_HBLK       0x4000
#define MARS_VDP_PEN        0x2000
#define MARS_VDP_FEN        0x0002
#define MARS_VDP_FS         0x0001
#define MARS_SYS_DMA_68S    0x0004
#define MARS_SYS_DMA_EMPTY  0x4000
#define MARS_SYS_DMA_FULL   0x8000
#define SH2_FRT_TIER        (*(volatile uint8_t *)0xFFFFFE10)
#define SH2_FRT_FRCH        (*(volatile uint8_t *)0xFFFFFE12)
#define SH2_FRT_FRCL        (*(volatile uint8_t *)0xFFFFFE13)
#define SH2_FRT_TCR         (*(volatile uint8_t *)0xFFFFFE16)
#define SH2_DMA_SAR0        (*(volatile uint32_t *)0xFFFFFF80)
#define SH2_DMA_DAR0        (*(volatile uint32_t *)0xFFFFFF84)
#define SH2_DMA_TCR0        (*(volatile uint32_t *)0xFFFFFF88)
#define SH2_DMA_CHCR0       (*(volatile uint32_t *)0xFFFFFF8C)
#define SH2_DMA_DRCR0       (*(volatile uint8_t *)0xFFFFFE71)
#define SH2_DMA_SAR1        (*(volatile uint32_t *)0xFFFFFF90)
#define SH2_DMA_DAR1        (*(volatile uint32_t *)0xFFFFFF94)
#define SH2_DMA_TCR1        (*(volatile uint32_t *)0xFFFFFF98)
#define SH2_DMA_CHCR1       (*(volatile uint32_t *)0xFFFFFF9C)
#define SH2_DMA_DRCR1       (*(volatile uint8_t *)0xFFFFFE72)
#define SH2_DMA_DMAOR       (*(volatile uint32_t *)0xFFFFFFB0)
#define SH2_DMA_VCR0        (*(volatile uint32_t *)0xFFFFFFA0)
#define SH2_DMA_VCR1        (*(volatile uint32_t *)0xFFFFFFA8)
#define SH2_INT_IPRA        (*(volatile uint16_t *)0xFFFFFEE2)
#define MARS_PWM_STEREO     (*(volatile uint32_t *)0x20004034)
#endif
