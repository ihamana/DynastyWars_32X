OUTPUT_ARCH(sh)
ENTRY(_master_start)
MEMORY
{
  lo  (rwx) : ORIGIN = 0x06001000, LENGTH = 0x5800
  rd1 (rw)  : ORIGIN = 0x0601D800, LENGTH = 0x2800
  rd2 (rw)  : ORIGIN = 0x06021000, LENGTH = 0x7000
  hot (rwx) : ORIGIN = 0x06029000, LENGTH = 0x17000
  hlm (rx)  : ORIGIN = 0x02010000, LENGTH = 0x18000
  rom (rx)  : ORIGIN = 0x02200000, LENGTH = 0xE0000
  rom2 (rx) : ORIGIN = 0x02152000, LENGTH = 0x6D00
  rom3 (rx) : ORIGIN = 0x02360000, LENGTH = 0x10000
  rom4 (rx) : ORIGIN = 0x0235D000, LENGTH = 0x3000
}
SECTIONS
{
  .vectors : { KEEP(*(.vectors)) } > lo
  .text : { *crt0.o(.text) *sys.o(.text) *main.o(.text .text.*) *libgcc.a:*(.text .text.*) } > lo
  .rodata : { *main.o(.rodata .rodata.*) *libgcc.a:*(.rodata .rodata.*) } > lo
  .data : { *(.data) *(.data.*) . = ALIGN(4); } > lo
  .bss (NOLOAD) : { __bss_start = .; *(.bss) *(.bss.*) *(COMMON) . = ALIGN(4); __bss_end = .; } > lo
  ASSERT(__bss_end <= 0x06006400, "lo overflow (stacks at 0x06006400-0x06007000: master 0x6900 down, slave 0x7000 down)")
  .rd1 (NOLOAD) : { ___rd1_start = .; *(.rd1) . = ALIGN(4); ___rd1_end = .; } > rd1
  .rd2 (NOLOAD) : { ___rd2_start = .; *(.rd2) . = ALIGN(4); ___rd2_end = .; } > rd2
  .hot : { ___hot_vma = .; *render.o(.text .text.* .rodata .rodata.*) *(.nathot) *hot.o(.text .text.* .rodata .rodata.*) *run.o(.text .text.* .rodata .rodata.*) . = ALIGN(4); ___hot_end = .; } > hot AT > hlm
  ___hot_lma = LOADADDR(.hot);
  .hb (NOLOAD) : { ___hb_start = .; *(.hb) . = ALIGN(4); ___hb_end = .; } > hot
  .rom2 : { *p01.o(.text .text.* .rodata .rodata.*) } > rom2
  .rom3 : { *p05.o(.text .text.* .rodata .rodata.*) *(.rom2ui) *(.rom2uid) *ui.o(.text .text.* .rodata .rodata.*) } > rom3
  .rom4 : { *p15.o(.text .text.* .rodata .rodata.*) } > rom4   /* V3.20: cold code 4 (the ending code made .rom overflow) */
  .rom : { *(.romui) *(.text) *(.text.*) *(.rodata) *(.rodata.*) } > rom
}
