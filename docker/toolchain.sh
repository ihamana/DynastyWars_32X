#!/bin/bash
# Cross toolchains used by the build: binutils 2.42 + gcc 13.2.0 (C only) for sh-elf (SH-2, with libgcc) and
# m68k-elf (68000; the MD/Mega-CD code links SGDK 2.11's libgcc.a like the original builds).
set -e
J=$(nproc)
mkdir -p /tmp/tc && cd /tmp/tc
M=${GNU_MIRROR:-https://ftpmirror.gnu.org}
wget -q $M/binutils/binutils-2.42.tar.xz && wget -q $M/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
tar xf binutils-2.42.tar.xz && tar xf gcc-13.2.0.tar.xz
COMMON="--disable-nls --disable-werror --disable-gdb --disable-gdbserver --disable-sim --disable-gprofng MAKEINFO=true"
GCOM="--enable-languages=c --without-headers --disable-nls --disable-threads --disable-shared --disable-multilib --disable-libssp --disable-lto --disable-plugin --disable-libquadmath --disable-libgomp --disable-bootstrap MAKEINFO=true"
# SH-2
mkdir b-sh && cd b-sh && ../binutils-2.42/configure --target=sh-elf --prefix=/opt/tc/sh $COMMON > /dev/null && make -j$J MAKEINFO=true > /dev/null && make install MAKEINFO=true > /dev/null && cd ..
export PATH=/opt/tc/sh/bin:/opt/tc/m68k/bin:$PATH
mkdir g-sh && cd g-sh && ../gcc-13.2.0/configure --target=sh-elf --prefix=/opt/tc/sh --with-cpu=m2 --with-multilib-list=m2 $GCOM > /dev/null
make -j$J all-gcc MAKEINFO=true > /dev/null && make install-gcc MAKEINFO=true > /dev/null
make -j$J all-target-libgcc MAKEINFO=true > /dev/null && make install-target-libgcc MAKEINFO=true > /dev/null && cd ..
# 68000
mkdir b-68k && cd b-68k && ../binutils-2.42/configure --target=m68k-elf --prefix=/opt/tc/m68k $COMMON --disable-readline --disable-libdecnumber --disable-gprof --without-zstd > /dev/null && make -j$J MAKEINFO=true > /dev/null && make install MAKEINFO=true > /dev/null && cd ..
mkdir g-68k && cd g-68k && ../gcc-13.2.0/configure --target=m68k-elf --prefix=/opt/tc/m68k --with-cpu=68000 $GCOM > /dev/null
make -j$J all-gcc MAKEINFO=true > /dev/null && make install-gcc MAKEINFO=true > /dev/null && cd ..
cd / && rm -rf /tmp/tc
sh-elf-gcc --version | head -1; m68k-elf-gcc --version | head -1
