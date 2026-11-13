OUTPUT_ARCH(m68k)
MEMORY { prg : ORIGIN = 0x0, LENGTH = 0x40000 }
SECTIONS {
  .text 0x0 : { *(.vectors) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); } > prg
  .data : { *(.data .data.*) . = ALIGN(4); } > prg
  .bss (NOLOAD) : { _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(4); _ebss = .; } > prg
}
