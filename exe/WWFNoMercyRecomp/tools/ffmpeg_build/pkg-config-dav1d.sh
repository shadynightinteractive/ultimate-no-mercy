#!/bin/sh
# Stands in for pkg-config while FFmpeg's configure runs (tools/build_ffmpeg.sh passes it as --pkg-config). 2026-11-10.
# This PC has no pkg-config, and configure only ever asks it about one library here: dav1d, the AV1 decoder built by
# tools/build_dav1d.sh into obj/dav1d-out. This answers those questions from that folder and says no to anything else.
# (merge084b, 2026-11-16: the project is the nearest folder above this one with third_party/ -- tools/ffmpeg_build/ or tools/)
ROOT=$(cd "$(dirname "$0")" && pwd); while [ ! -d "$ROOT/third_party" ] && [ "$(dirname "$ROOT")" != "$ROOT" ]; do ROOT=$(dirname "$ROOT"); done
DAV1D=$(cd "$ROOT/obj/dav1d-out" 2>/dev/null && { pwd -W 2>/dev/null || pwd; })
case "$*" in *--version) echo 0.29.2; exit 0 ;; esac
case "$*" in *dav1d*) ;; *) exit 1 ;; esac
[ -f "$DAV1D/lib/libdav1d.a" ] || exit 1
for arg in "$@"; do
    case "$arg" in
        --exists) exit 0 ;;
        --cflags) echo "-I$DAV1D/include"; exit 0 ;;
        --libs) echo "-L$DAV1D/lib -ldav1d"; exit 0 ;;
        --variable=includedir) echo "$DAV1D/include"; exit 0 ;;
        --modversion) sed -n 's/^Version: //p' "$DAV1D/lib/pkgconfig/dav1d.pc"; exit 0 ;;
    esac
done
exit 1
