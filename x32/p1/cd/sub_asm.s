| Sub-CPU program (Mode 1): MSU-MD compatible CD audio (krikzz msu-md sub.c) + OKI MSM6295 voices on the RF5C164
        .section .vectors,"ax"
        dc.l    0xF7F0, RST
        dc.l    INT,INT,INT,INT,INT,INT,INT
        dc.l    INT,INT,INT,INT,INT,INT,INT,INT
        dc.l    INT,INT,INT,INT,INT,INT,INT,INT
        dc.l    INT,INT,IE3,IE4,INT,INT
        .text
RST:
        move    #0x2700,%sr
        move.l  #0xF7F0,%sp
        lea     _sbss,%a0
1:      cmp.l   #_ebss,%a0
        bhs.s   2f
        clr.w   (%a0)+
        bra.s   1b
2:
        move    #0x2000,%sr
        jmp     msu_drv
IE3:
        movem.l %d0-%d1/%a0-%a1,-(%sp)
        jsr     pcm_service
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        rte
IE4:
        movem.l %d0-%d1/%a0-%a1,-(%sp)
        jsr     irq4
        movem.l (%sp)+,%d0-%d1/%a0-%a1
        rte
INT:
        rte
