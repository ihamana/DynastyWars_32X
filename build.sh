#!/bin/sh
# One-command build: ./build.sh /path/to/dynwar.zip   (needs Docker; output in ./out)
set -e
[ -f "$1" ] || { echo "usage: $0 /path/to/dynwar.zip"; exit 1; }
ZIP=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$(dirname "$0")"
TC=ghcr.io/ihamana/dynastywars32x-toolchain:gcc13.2-1     # prebuilt cross compilers (docker/toolchain.Dockerfile)
docker image inspect "$TC" >/dev/null 2>&1 || docker pull --platform linux/amd64 "$TC" || {
    echo "toolchain image not available: building the cross compilers (20-40 minutes, once)"
    docker build --platform linux/amd64 -f docker/toolchain.Dockerfile -t "$TC" .; }
docker build --platform linux/amd64 --build-arg TOOLCHAIN="$TC" -t dynastywars32x .
docker run --rm --platform linux/amd64 -v "$PWD":/dw -v "$ZIP":/rom/dynwar.zip:ro dynastywars32x
