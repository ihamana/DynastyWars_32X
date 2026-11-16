| Dynasty Wars 32xM - Mega Drive side (P1): boot, pad -> COMM0 every vblank.
| The game itself runs on the 32X master SH-2; backgrounds (MD planes) come later.
        .include "sh2syms.inc"
        .section .header,"ax"
        .long   0x01000000
        .rept 63
        .long   0x000003F0
        .endr
        .org    0x100
        .ascii  "SEGA 32X        "
        .ascii  "(C)SEGA 2026.NOV"
        .ascii  "TENCHI WO KURAU 32XM                            "
        .ascii  "DYNASTY WARS 32XM                               "
        .ascii  "GM T32XM-0001 "
        .word   0x0000
        .ascii  "J6              "
        .long   0x00000000,0x003FFFFF
        .long   0x00FF0000,0x00FFFFFF
        .ascii  "            "
        .ascii  "            "
        .ascii  "                                        "
        .ascii  "JUE             "
        .org    0x200
        .macro  jump address
        .word   0x4EF9
        .long   \address
        .endm
        jump    0x880800                | 1 reset
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_vblank               | 30 level 6 (VBlank)
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        jump    md_rte
        .org    0x3C0
        .ascii  "DW32XM          "
        .long   0x00000000
        .long   SH2_ROM_OFS
        .long   0x00001000
        .long   SH2_SIZE
        .long   SH2_MSTART
        .long   SH2_SSTART
        .long   SH2_MVBR
        .long   SH2_SVBR
        .include "icd_block.inc"

        .section .text,"ax"
        .global _start
_start:
        move.w  #0x2700,sr
        lea     0x00FFFE00,sp
        lea     0xC00004,a0
        move.w  #0x8004,(a0)
        move.w  #0x8134,(a0)
        move.w  #0x8230,(a0)
        move.w  #0x8407,(a0)
        move.w  #0x8700,(a0)
        move.w  #0x8B00,(a0)
        move.w  #0x8C81,(a0)
        move.w  #0x8D3D,(a0)            | hscroll table 0xF400
        move.w  #0x8578,(a0)            | sprite table 0xF000
        move.w  #0x8F02,(a0)
        move.w  #0x9001,(a0)
        lea     0xC00000,a1
        move.l  #0x40000000,(a0)        | clear VRAM
        move.w  #0x3FFF,d0
1:      clr.l   (a1)
        dbra    d0,1b
        move.l  #0xC0000000,(a0)        | CRAM: all black
        moveq   #31,d0
2:      clr.l   (a1)
        dbra    d0,2b
        move.b  #0x40,0xA10009          | pad 1 TH output
        move.b  #0x40,0xA1000B          | pad 2 TH output
        move.b  #0x40,0xA10005
        move.b  #0x40,0xA10003
4:      cmp.l   #0x4D5F4F4B,0xA15120    | 'M_OK'
        bne.s   4b
5:      cmp.l   #0x535F4F4B,0xA15124    | 'S_OK'
        bne.s   5b
        move.w  #0x0000,0xA15120        | inputs: nothing pressed
        clr.w   0xA15124
        lea     __bss_start,a1              | clear C bss
        lea     __bss_end,a2
7:      cmp.l   a2,a1
        bcc.s   8f
        clr.w   (a1)+
        bra.s   7b
8:      move.w  #0x8174,(a0)            | display on, VINT on
        move.w  #0x2000,sr
        jsr     md_main
6:      bra.s   6b

| VBlank: pad 1 -> COMM0 = IN0 << 8 | IN1 (P1 byte), active low like the arcade
md_vblank:
        movem.l d0-d4/a0,-(sp)
        tst.w   0xC00004
        lea     0xA10003,a0
        move.b  #0x40,(a0)
        nop
        nop
        move.b  (a0),d0                 | ?1CBRLDU
        move.b  #0x00,(a0)
        nop
        nop
        move.b  (a0),d1                 | ?0SA00DU
        moveq   #-1,d4
        move.b  #0x40,(a0)
        nop
        nop
        move.b  #0x00,(a0)
        nop
        nop
        move.b  #0x40,(a0)
        nop
        nop
        move.b  #0x00,(a0)
        nop
        nop
        move.b  (a0),d3                 | ?0SA0000 on a 6-button pad
        move.b  #0x40,(a0)
        nop
        nop
        andi.b  #0x0F,d3
        bne.s   9f
        move.b  (a0),d4                 | ?1CBMXYZ
        not.b   d4
        move.w  #1,six_pad
        bra.s   10f
9:      moveq   #0,d4
10:     move.b  #0x00,(a0)
        nop
        nop
        move.b  #0x40,(a0)
        | COMM0 = raw pad, 1 = pressed: 0 U 1 D 2 L 3 R 4 A 5 B 6 C 7 START 8 X 9 Y 10 Z 11 MODE, 15 = 6-button pad
        not.b   d0
        not.b   d1
        moveq   #0,d3
        move.b  d0,d3
        andi.w  #0x0F,d3
        btst    #4,d1
        beq.s   11f
        bset    #4,d3                   | A
11:     btst    #4,d0
        beq.s   12f
        bset    #5,d3                   | B
12:     btst    #5,d0
        beq.s   13f
        bset    #6,d3                   | C
13:     btst    #5,d1
        beq.s   14f
        bset    #7,d3                   | START
14:     tst.b   d3
        moveq   #0,d2
        move.b  d4,d2
        beq.s   15f
        andi.w  #0x0F,d2                | Z Y X MODE -> bits 10 9 8 11
        btst    #0,d2
        beq.s   16f
        bset    #10,d3
16:     btst    #1,d2
        beq.s   17f
        bset    #9,d3
17:     btst    #2,d2
        beq.s   18f
        bset    #8,d3
18:     btst    #3,d2
        beq.s   15f
        bset    #11,d3
15:     tst.w   six_pad
        beq.s   19f
        bset    #15,d3
19:
        | V1.9 2P: COMM0 alternates per vblank: pad 1 (as above), pad 2 | 0x4000 (3-button read: 0 U 1 D 2 L 3 R 4 A 5 B 6 C 7 START)
        eori.w  #1,pad_turn
        beq.s   22f
        lea     0xA10005,a0
        move.b  #0x40,(a0)
        nop
        nop
        move.b  (a0),d0                 | ?1CBRLDU
        move.b  #0x00,(a0)
        nop
        nop
        move.b  (a0),d1                 | ?0SA00DU
        move.b  #0x40,(a0)
        not.b   d0
        not.b   d1
        moveq   #0,d3
        move.b  d0,d3
        andi.w  #0x0F,d3
        btst    #4,d1
        beq.s   23f
        bset    #4,d3
23:     btst    #4,d0
        beq.s   24f
        bset    #5,d3
24:     btst    #5,d0
        beq.s   25f
        bset    #6,d3
25:     btst    #5,d1
        beq.s   26f
        bset    #7,d3
26:     bset    #14,d3
22:
        move.w  d3,0xA15120
        move.l  0xA15120,0xFF0F00         | debug: COMM0-15 -> RAM
        move.l  0xA15124,0xFF0F04
        move.l  0xA15128,0xFF0F08
        move.l  0xA1512C,0xFF0F0C
        addq.w  #1,0xFF0F10
        movem.l (sp)+,d0-d4/a0
        movem.l d0-d1/a0-a1,-(sp)
        jsr     md_vblank_count
        tst.w   vdp_busy
        beq.s   22f
        move.w  #1,sc_late              | V3.21: the main loop does it when its VDP sequence ends
        bra.s   21f
22:     jsr     md_vblank_scroll
21:     movem.l (sp)+,d0-d1/a0-a1
md_rte: rte

| 32x32 -> 32 multiply for gcc (no libgcc for the 68000 here)
        .global __mulsi3
__mulsi3:
        move.w  4(sp),d0
        mulu.w  10(sp),d0
        move.w  8(sp),d1
        mulu.w  6(sp),d1
        add.w   d1,d0
        swap    d0
        clr.w   d0
        move.w  6(sp),d1
        mulu.w  10(sp),d1
        add.l   d1,d0
        rts

| Mega-CD sub-CPU program (cd/), copied to PRG-RAM by snd_start
        .balign 2
        .global sub_bin, sub_bin_end
sub_bin:
        .incbin "sub.bin"
        .balign 2
sub_bin_end:

        .bss
        .global six_pad
six_pad: .skip 2
pad_turn: .skip 2
        .text
