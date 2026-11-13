! DW 32xM timing test - SH2 startup (both CPUs). Runs from SDRAM (copied by the 32X boot ROM).
        .section .vectors,"ax"
        .global _master_vbr, _slave_vbr, _master_start, _slave_start

        .macro  VTAB start,stack,err,irq
        .long   \start, \stack, \start, \stack
        .long   \err, 0, \err, 0, 0, \err, \err, \err, \err
        .rept 19
        .long   0
        .endr
        .rept 32
        .long   \err
        .endr
        .long   \irq\()_frt, \irq\()_wdt, \irq\()_dma, \irq\()_pwm
        .long   \irq\()_cmd, \irq\()_hint, \irq\()_vint, \irq\()_vres
        .endm

        .align 4
_master_vbr:
        VTAB    _master_start, 0x06006900, _m_err, _m
_slave_vbr:
        VTAB    _slave_start, 0x06007000, _s_err, _s

        .text
        .align 2
_master_start:
        mov.l   _m_stack,r15
        mov     #0,r0
        ldc     r0,gbr
        mov.l   _intclr,r1              ! clear pending 32X interrupts
        mov     #0,r0
        mov.w   r0,@-r1
        mov.w   r0,@r1
        mov.w   r0,@-r1
        mov.w   r0,@r1
        mov.w   r0,@-r1
        mov.w   r0,@r1
        mov.w   r0,@-r1
        mov.w   r0,@r1
        mov.w   r0,@-r1
        mov.w   r0,@r1
        bsr     _frt_init
        nop
        mov.l   _cctl,r0                ! purge + enable cache
        mov     #0x11,r1
        mov.b   r1,@r0
        ! clear bss
        mov.l   _bss_s,r1
        mov.l   _bss_e,r2
        mov     #0,r0
1:      cmp/hs  r2,r1
        bt      2f
        mov.l   r0,@r1
        bra     1b
        add     #4,r1
2:
        ! wait for 68000 to acknowledge (it clears COMM0)
        mov.l   _comm0,r0
        mov.l   _mok,r1
3:      mov.l   @r0,r2
        cmp/eq  r1,r2
        bt      3b
        mov.l   _adapter,r1
        mov     #0x80,r0
        mov.b   r0,@r1                  ! FM = 1 : SH2 owns the 32X VDP
        mov     #0x08,r0                ! VINT enable
        mov.b   r0,@(1,r1)
        mov     #0x20,r0                ! SR: I=2 (allow VINT level 12)
        ldc     r0,sr
        mov.l   _mmain,r0
        jmp     @r0
        nop

_slave_start:
        mov.l   _s_stack,r15
        mov     #1,r0
        ldc     r0,gbr
        bsr     _frt_init
        nop
        mov.l   _cctl,r0
        mov     #0x11,r1
        mov.b   r1,@r0
        mov.l   _comm4,r0
        mov.l   _sok,r1
4:      mov.l   @r0,r2
        cmp/eq  r1,r2
        bt      4b
        mov.l   _adapter,r1
        mov     #0x00,r0                ! slave: interrupts off for now
        mov.b   r0,@(1,r1)
        mov     #0xF0,r0
        ldc     r0,sr
        mov.l   _smain,r0
        jmp     @r0
        nop

_frt_init:                              ! FRT setup used for 32X int acknowledge
        mov.l   _frt,r1
        mov     #0,r0
        mov.b   r0,@(0,r1)
        mov     #0xE2,r0
        mov.b   r0,@(7,r1)
        mov     #0,r0
        mov.b   r0,@(4,r1)
        mov     #1,r0
        mov.b   r0,@(5,r1)
        mov     #0,r0
        mov.b   r0,@(6,r1)
        mov     #1,r0
        mov.b   r0,@(1,r1)
        mov     #0,r0
        mov.b   r0,@(3,r1)
        rts
        mov.b   r0,@(2,r1)

        .align 2
_m_stack: .long 0x06006900   ! V2.1: master 0x6900 down (uses ~0.9 KB), slave 0x7000 down (1.75 KB: was 1 KB and the renderer used ~1.25 KB)
_s_stack: .long 0x06007000
_intclr:  .long 0x2000401E
_cctl:    .long 0xFFFFFE92
_bss_s:   .long __bss_start
_bss_e:   .long __bss_end
_comm0:   .long 0x20004020
_comm4:   .long 0x20004024
_mok:     .ascii "M_OK"
_sok:     .ascii "S_OK"
_adapter: .long 0x20004000
_mmain:   .long _master_main
_smain:   .long _slave_main
_frt:     .long 0xFFFFFE10

! ---------------- interrupt handlers ----------------
        .macro  IRQENTRY name, clrreg, cfunc
\name:
        mov.l   r0,@-r15
        mov.l   r1,@-r15
        mov.l   r2,@-r15
        mov.l   r3,@-r15
        mov.l   r4,@-r15
        mov.l   r5,@-r15
        mov.l   r6,@-r15
        mov.l   r7,@-r15
        sts.l   pr,@-r15
        sts.l   macl,@-r15
        sts.l   mach,@-r15
        mov.l   1f,r1                   ! FRT TOCR toggle = 32X int ack
        mov     #0xE0,r0
        mov.b   r0,@r1
        mov.l   2f,r1
        mov.w   r0,@r1                  ! clear the 32X interrupt source
        mov.l   3f,r0
        jsr     @r0
        nop
        mov.l   1f,r1
        mov     #0xE2,r0
        mov.b   r0,@r1
        lds.l   @r15+,mach
        lds.l   @r15+,macl
        lds.l   @r15+,pr
        mov.l   @r15+,r7
        mov.l   @r15+,r6
        mov.l   @r15+,r5
        mov.l   @r15+,r4
        mov.l   @r15+,r3
        mov.l   @r15+,r2
        mov.l   @r15+,r1
        mov.l   @r15+,r0
        rte
        nop
        .align 2
1:      .long 0xFFFFFE17
2:      .long \clrreg
3:      .long \cfunc
        .endm

        IRQENTRY _m_vint, 0x20004016, _master_vint
        IRQENTRY _m_cmd,  0x2000401A, _master_cmd
        IRQENTRY _m_hint, 0x20004018, _master_hint
        IRQENTRY _m_pwm,  0x2000401C, _master_pwm
        IRQENTRY _s_vint, 0x20004016, _slave_vint
        IRQENTRY _s_cmd,  0x2000401A, _slave_cmd
        IRQENTRY _s_pwm,  0x2000401C, _slave_pwm

! on-chip DMAC channel 1 end (slave, vector 66): the handler clears TE itself
_s_dma:
        mov.l   r0,@-r15
        mov.l   r1,@-r15
        mov.l   r2,@-r15
        mov.l   r3,@-r15
        mov.l   r4,@-r15
        mov.l   r5,@-r15
        mov.l   r6,@-r15
        mov.l   r7,@-r15
        sts.l   pr,@-r15
        sts.l   macl,@-r15
        sts.l   mach,@-r15
        mov.l   1f,r0
        jsr     @r0
        nop
        lds.l   @r15+,mach
        lds.l   @r15+,macl
        lds.l   @r15+,pr
        mov.l   @r15+,r7
        mov.l   @r15+,r6
        mov.l   @r15+,r5
        mov.l   @r15+,r4
        mov.l   @r15+,r3
        mov.l   @r15+,r2
        mov.l   @r15+,r1
        mov.l   @r15+,r0
        rte
        nop
        .align 2
1:      .long   _slave_dma

_m_err: _s_err:
_m_frt: _m_dma: _m_vres:
_s_frt: _s_hint: _s_vres:
        rte
        nop

        ! profiling (PROF builds only enable it): the WDT interval interrupt stores the interrupted master PC
        ! with the 68000 block (0x260076F8) in a ring at 0x26007004-0x260076F3 (offset of the last pair at 0x26007000)
        .align 2
_m_wdt:
_s_wdt:                                 ! (SPROF builds: the slave samples instead)
        mov.l   r0,@-r15
        mov.l   r1,@-r15
        mov.l   r2,@-r15
        mov.l   9f,r1
        mov.b   @r1,r0                  ! read WTCSR, then clear OVF
        mov.w   8f,r0
        mov.w   r0,@r1
        mov.l   7f,r2
        mov.l   @r2,r1
        add     #8,r1
        mov.w   6f,r0
        cmp/hs  r0,r1
        bf      1f
        mov     #4,r1
1:      mov.l   r1,@r2
        add     r2,r1
        mov.l   @(12,r15),r0
        mov.l   r0,@r1                  ! SH-2 pc
        mov.l   5f,r0
        mov.l   @r0,r0
        mov.l   r0,@(4,r1)              ! the 68000 block being run (cpu_run, PROF)
        mov.l   @r15+,r2
        mov.l   @r15+,r1
        mov.l   @r15+,r0
        rte
        nop
        .align 2
9:      .long   0xFFFFFE80
7:      .long   0x26007000
8:      .word   0xA53E                  ! interval, /4096 (PROF)
6:      .word   0x6F4                   ! ring 0x7004-0x76F3 in pairs (0x7700+ is arcade work RAM)
        .align 2
5:      .long   0x260076F8
