# Toolchain image: binutils 2.42 + gcc 13.2.0 for sh-elf (SH-2) and m68k-elf (68000), built once by GitHub Actions
# (.github/workflows/toolchain.yml) and published as ghcr.io/ihamana/dynastywars32x-toolchain, so a build does not
# compile them (20-40 minutes).  build.sh / build.ps1 build it locally from this file when the image cannot be pulled.
# GCC and binutils are GPL software: their sources are the GNU release tarballs docker/toolchain.sh downloads.
FROM python:3.13-slim-bookworm
RUN apt-get update && apt-get install -y --no-install-recommends build-essential wget ca-certificates xz-utils \
        flex bison libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev && rm -rf /var/lib/apt/lists/*
COPY docker/toolchain.sh /tmp/toolchain.sh
RUN bash /tmp/toolchain.sh && rm /tmp/toolchain.sh
ENV PATH=/opt/tc/sh/bin:/opt/tc/m68k/bin:$PATH
