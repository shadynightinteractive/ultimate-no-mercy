#!/bin/sh
# Builds dav1d (the AV1 decoder, BSD 2-clause) from source as a static library, for FFmpeg's avcodec DLL to take in
# (tools/build_ffmpeg.sh calls this first). 2026-11-10. Without it AV1 videos only play through the graphics chip.
#     sh tools/ffmpeg_build/build_dav1d.sh [clean]
# Uses: the project's compiler (llvm-mingw), NASM (tools/nasm), Meson run from its source in tools/meson with the
# Python already on this PC (nothing installed), and the project's ninja. The source is third_party/dav1d-src/dav1d-1.5.4
# (the official tarball, its sha256 checked). Objects: obj/dav1d-build. Result: obj/dav1d-out (include/, lib/libdav1d.a).
set -e
# (merge084b, 2026-11-16: this script lives in tools/ffmpeg_build/, beside nasm/ and meson/: HERE is that folder, ROOT the
#  project -- the nearest folder above it with third_party/ -- wherever the script is kept)
HERE=$(cd "$(dirname "$0")" && { pwd -W 2>/dev/null || pwd; })
ROOT=$HERE; while [ ! -d "$ROOT/third_party" ] && [ "$(dirname "$ROOT")" != "$ROOT" ]; do ROOT=$(dirname "$ROOT"); done
[ -d "$ROOT/third_party" ] || { echo "no project (a folder with third_party/) above $HERE"; exit 1; }
REL=${HERE#"$ROOT"/}        # (this folder, from the project: tools/ffmpeg_build)
# (newhome 2026-11-17: the project's own toolchain by default -- toolchain/ two folders above the exe source, no fixed path)
TOOLCHAIN=${ARCADE_LLVM_MINGW:-$(cd "$ROOT/../../toolchain/llvm-mingw" && { pwd -W 2>/dev/null || pwd; })}
NINJA_DIR=${ARCADE_NINJA_DIR:-$(cd "$ROOT/../../toolchain/ninja" && { pwd -W 2>/dev/null || pwd; })}
PYTHON=${ARCADE_PYTHON:-python}
JOBS=${ARCADE_FFMPEG_JOBS:-4}
PATH="$(cd "$TOOLCHAIN/bin" && pwd):$(cd "$HERE/nasm" && pwd):$(cd "$NINJA_DIR" && pwd):$PATH"
export PATH
SRC="$ROOT/third_party/dav1d-src/dav1d-1.5.4"
BUILD="$ROOT/obj/dav1d-build"
OUT="$ROOT/obj/dav1d-out"
[ -f "$SRC/meson.build" ] || { echo "no dav1d source in third_party/dav1d-src/dav1d-1.5.4"; exit 1; }
if [ "$1" = clean ]; then rm -rf "$BUILD" "$OUT"; fi

# Static, release, the library only (no programs, no tests), both bit depths (8 and 10-bit AV1 are both common).
echo "== dav1d: meson setup"
CC=x86_64-w64-mingw32-clang AR=x86_64-w64-mingw32-ar \
"$PYTHON" "$HERE/meson/meson.py" setup "$BUILD" "$SRC" \
    --prefix="$OUT" --libdir=lib --buildtype=release --default-library=static \
    -Denable_tools=false -Denable_tests=false -Denable_examples=false -Denable_docs=false \
    > "$ROOT/obj/dav1d-setup.out" 2>&1 || { tail -30 "$ROOT/obj/dav1d-setup.out"; exit 1; }
echo "== dav1d: ninja -j$JOBS"
ninja -C "$BUILD" -j"$JOBS" > "$ROOT/obj/dav1d-ninja.out" 2>&1 || { tail -30 "$ROOT/obj/dav1d-ninja.out"; exit 1; }
"$PYTHON" "$HERE/meson/meson.py" install -C "$BUILD" --no-rebuild > "$ROOT/obj/dav1d-install.out" 2>&1 || { tail -30 "$ROOT/obj/dav1d-install.out"; exit 1; }
ls -l "$OUT/lib"
echo "== dav1d done"
