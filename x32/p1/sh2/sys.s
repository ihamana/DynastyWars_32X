! cpu id via GBR, spinlock via TAS on cache-through SDRAM
        .text
        .global _cpu_id, _shared_lock, _shared_unlock, _cache_purge
_cpu_id:
        rts
        stc     gbr,r0
_shared_lock:
        mov.l   1f,r1
2:      tas.b   @r1
        bf      2b
        rts
        nop
_shared_unlock:
        mov.l   1f,r1
        mov     #0,r0
        rts
        mov.b   r0,@r1
_cache_purge:
        mov.l   3f,r1
        mov     #0x11,r0
        rts
        mov.b   r0,@r1
        .align 2
1:      .long   _lockbyte+0x20000000
3:      .long   0xFFFFFE92
        .data
        .align 2
_lockbyte: .long 0
