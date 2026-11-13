#!/bin/sh
# One-command build: ./build.sh /path/to/dynwar.zip   (needs Docker; output in ./out)
set -e
[ -f "$1" ] || { echo "usage: $0 /path/to/dynwar.zip"; exit 1; }
ZIP=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$(dirname "$0")"
docker build --platform linux/amd64 -t dynastywars32x .
docker run --rm --platform linux/amd64 -v "$PWD":/dw -v "$ZIP":/rom/dynwar.zip:ro dynastywars32x
