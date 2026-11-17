# Dynasty Wars 32xM build environment.  See README.md: ./build.sh /path/to/dynwar.zip
FROM python:3.13-slim-bookworm
RUN apt-get update && apt-get install -y --no-install-recommends build-essential wget ca-certificates xz-utils \
        flex bison libgmp-dev libmpfr-dev libmpc-dev zlib1g-dev && rm -rf /var/lib/apt/lists/*
COPY docker/toolchain.sh /tmp/toolchain.sh
RUN bash /tmp/toolchain.sh
# SGDK 2.11's m68k libgcc.a (the Mega-CD sub CPU program links it: __mulsi3, __divsi3 ...)
RUN mkdir -p /opt/sgdk && wget -q -O /opt/sgdk/libgcc.a https://raw.githubusercontent.com/Stephane-D/SGDK/v2.11/lib/libgcc.a && \
    echo "8666030a1081078260a8e2b43949d6fa09d77bed  /opt/sgdk/libgcc.a" | sha1sum -c -
# Source Han Serif TC (variable, 2.003; SIL OFL 1.1): some of the Chinese glyphs
RUN mkdir -p /opt/fonts && wget -q -O /opt/fonts/SourceHanSerifTC-VF.ttf \
        https://github.com/adobe-fonts/source-han-serif/raw/2.003R/Variable/TTF/SourceHanSerifTC-VF.ttf && \
    (echo "71354ed752104c8a3cbcff18943c6110d179d01cc6eaaf1aff7ea14c4a447879  /opt/fonts/SourceHanSerifTC-VF.ttf" | sha256sum -c - || \
     echo "WARNING: Source Han Serif file differs from the one the release used: a few Chinese glyphs may differ")
RUN pip install --no-cache-dir numpy==2.5.3 scipy==1.18.1 pillow==12.3.0 openpyxl==3.1.5 fonttools==4.67.0
ENV PATH=/opt/tc/sh/bin:/opt/tc/m68k/bin:$PATH
WORKDIR /dw
ENTRYPOINT ["/dw/scripts/build_all.sh"]
CMD ["/rom/dynwar.zip", "/dw/out"]
