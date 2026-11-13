| ADPCM (OKI MSM6295) decode of one ring segment into the RF5C164 wave RAM (pcm.c)
| void dec_seg(Voice *w, vu8 *out, int cnt)    out: wave RAM byte address (stride 2), cnt >= 1
| Voice: p 0, end 4 (exclusive), g 8, st 10 (step x64), nib 12, on 13, vol 14, cur 15, wp 16, quiet 18, lut 20 (centered)
        .text
        .globl  dec_seg
dec_seg:
        movem.l %d2-%d7/%a2-%a4,-(%sp)
        move.l  40(%sp),%a4
        move.l  44(%sp),%a1
        move.l  48(%sp),%d7
        subq.w  #1,%d7
        tst.b   13(%a4)
        beq     zfill
        move.l  (%a4),%a0
        move.l  4(%a4),%d6
        move.w  8(%a4),%d0
        move.w  10(%a4),%d1
        lea     dtab4,%a2
        move.l  20(%a4),%a3
        move.w  #2047,%d5
        move.w  #-2048,%d2
        tst.b   12(%a4)
        bne     lo
hi:
        move.b  (%a0),%d4
        lsr.b   #2,%d4
        and.w   #0x3C,%d4
        add.w   %d1,%d4
        move.l  0(%a2,%d4.w),%d4
        move.w  %d4,%d1
        swap    %d4
        add.w   %d4,%d0
        cmp.w   %d5,%d0
        bgt     1f
        cmp.w   %d2,%d0
        bge     2f
        move.w  %d2,%d0
        bra     2f
1:      move.w  %d5,%d0
2:      move.w  %d0,%d4
        asr.w   #4,%d4
        move.b  0(%a3,%d4.w),(%a1)
        addq.l  #2,%a1
        subq.w  #1,%d7
        bmi     endhi
lo:
        move.b  (%a0)+,%d4
        and.w   #0x0F,%d4
        add.w   %d4,%d4
        add.w   %d4,%d4
        add.w   %d1,%d4
        move.l  0(%a2,%d4.w),%d4
        move.w  %d4,%d1
        swap    %d4
        add.w   %d4,%d0
        cmp.w   %d5,%d0
        bgt     1f
        cmp.w   %d2,%d0
        bge     2f
        move.w  %d2,%d0
        bra     2f
1:      move.w  %d5,%d0
2:      move.w  %d0,%d4
        asr.w   #4,%d4
        move.b  0(%a3,%d4.w),(%a1)
        addq.l  #2,%a1
        cmp.l   %a0,%d6
        bls     ended
        subq.w  #1,%d7
        bpl     hi
        clr.b   12(%a4)
        bra     save
endhi:
        move.b  #1,12(%a4)
save:
        move.l  %a0,(%a4)
        move.w  %d0,8(%a4)
        move.w  %d1,10(%a4)
        movem.l (%sp)+,%d2-%d7/%a2-%a4
        rts
ended:
        clr.b   13(%a4)
        clr.b   12(%a4)
        move.l  %a0,(%a4)
        move.w  %d0,8(%a4)
        move.w  %d1,10(%a4)
        subq.w  #1,%d7
zfill:
        tst.w   %d7
        bmi     3f
        moveq   #0,%d0
4:      move.b  %d0,(%a1)
        addq.l  #2,%a1
        dbra    %d7,4b
3:      movem.l (%sp)+,%d2-%d7/%a2-%a4
        rts
