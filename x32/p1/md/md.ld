OUTPUT_ARCH(m68k)
SECTIONS
{
  .header 0x000000 : AT(0x000000) { *(.header) }
  .text 0x880800 : AT(0x000800) { *boot.o(.text) *(.text) *(.text.*) *(.rodata) *(.rodata.*) }
  .data : { *(.data) *(.data.*) }
  ASSERT(SIZEOF(.data) == 0, "no initialised data on the MD side")
  .bss 0xFF1000 (NOLOAD) : { __bss_start = .; *(.bss) *(.bss.*) *(COMMON) . = ALIGN(2); __bss_end = .; }
  ASSERT(__bss_end <= 0xFFF000, "MD bss")
}
