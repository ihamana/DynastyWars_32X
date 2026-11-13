#include "m68k.h"
static const unsigned char *g_mem; static unsigned g_size, g_base;
static unsigned rd8(unsigned a){ a-=g_base; return a<g_size?g_mem[a]:0; }
unsigned int m68k_read_disassembler_8(unsigned int a){ return rd8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a){ return (rd8(a)<<8)|rd8(a+1); }
unsigned int m68k_read_disassembler_32(unsigned int a){ return (m68k_read_disassembler_16(a)<<16)|m68k_read_disassembler_16(a+2); }
unsigned int m68k_read_memory_8(unsigned int a){return 0;} unsigned int m68k_read_memory_16(unsigned int a){return 0;} unsigned int m68k_read_memory_32(unsigned int a){return 0;}
void m68k_write_memory_8(unsigned int a,unsigned int d){} void m68k_write_memory_16(unsigned int a,unsigned int d){} void m68k_write_memory_32(unsigned int a,unsigned int d){}
void dl_setmem(const unsigned char*m,unsigned size,unsigned base){ g_mem=m; g_size=size; g_base=base; }
int dl_dasm(unsigned pc,char*buf){ return m68k_disassemble(buf,pc,M68K_CPU_TYPE_68000); }
