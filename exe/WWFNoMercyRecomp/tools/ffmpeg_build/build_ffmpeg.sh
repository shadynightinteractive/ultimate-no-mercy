#!/bin/sh
# Builds the game's own FFmpeg from source: LGPL, shared DLLs, trimmed to what the Titantron videos need (2026-11-10).
# Run it through tools/ffmpeg_build/build_ffmpeg.ps1 (which lowers the priority and finds Git's sh), or directly:
#     sh tools/ffmpeg_build/build_ffmpeg.sh [clean | resume]
#
# What it uses, all already on this PC but for the assembler:
#   - the project's compiler, llvm-mingw (clang, llvm-ar ..., and its own mingw32-make), read-only;
#   - a POSIX shell with the usual small tools for FFmpeg's configure: Git for Windows' sh (Git Bash); and, for make's
#     own commands, the busybox sh inside llvm-mingw (why: at MAKE_SHELL below);
#   - NASM 2.16.03 for FFmpeg's hand-written x86 code (tools/nasm/nasm.exe). A build tool only: nothing of it is in the DLLs.
#     Without it the build still works (add --disable-x86asm) but decodes on the CPU several times slower.
# The source is third_party/ffmpeg-src/ffmpeg-8.1.3 (the official tarball, its signature checked: NOTES.md).
# Objects go to obj/ffmpeg-build. The result goes to third_party/ffmpeg: include/, lib/, bin/ (the DLLs), the licence,
# and BUILD.txt (this configure line, as it ran).
#
# LGPL: no --enable-gpl, no --enable-nonfree, no --enable-version3. configure's own summary must end "License: LGPL
# version 2.1 or later": the script stops if it doesn't.
set -e

# (merge084b, 2026-11-16: this script lives in tools/ffmpeg_build/, beside nasm/ and meson/: HERE is that folder, ROOT the
#  project -- the nearest folder above it with third_party/ -- wherever the script is kept)
HERE=$(cd "$(dirname "$0")" && { pwd -W 2>/dev/null || pwd; })
ROOT=$HERE; while [ ! -d "$ROOT/third_party" ] && [ "$(dirname "$ROOT")" != "$ROOT" ]; do ROOT=$(dirname "$ROOT"); done
[ -d "$ROOT/third_party" ] || { echo "no project (a folder with third_party/) above $HERE"; exit 1; }
REL=${HERE#"$ROOT"/}        # (this folder, from the project: tools/ffmpeg_build)
SRC_REL=../../third_party/ffmpeg-src/ffmpeg-8.1.3
BUILD="$ROOT/obj/ffmpeg-build"
OUT="$ROOT/third_party/ffmpeg"
# (newhome 2026-11-17: the project's own toolchain by default -- toolchain/ two folders above the exe source, no fixed path)
TOOLCHAIN=${ARCADE_LLVM_MINGW:-$(cd "$ROOT/../../toolchain/llvm-mingw" && { pwd -W 2>/dev/null || pwd; })}
JOBS=${ARCADE_FFMPEG_JOBS:-4}
MAKE="$TOOLCHAIN/bin/mingw32-make.exe"
# make runs each command with a shell. With Git's sh, this make loses the "> file" of any command longer than about
# 8,000 characters (measured: the object list of avcodec's DLL is 9,231), so the link fails. With the busybox sh that
# llvm-mingw itself ships, it doesn't. So: Git's sh for configure, llvm-mingw's busybox sh for make's commands.
MAKE_SHELL="$TOOLCHAIN/busybox/bin/sh.exe"
# (the compiler and the assembler first on the PATH, for configure and make alike)
PATH="$(cd "$TOOLCHAIN/bin" && pwd):$(cd "$HERE/nasm" && pwd):$PATH"
export PATH

[ -f "$ROOT/third_party/ffmpeg-src/ffmpeg-8.1.3/configure" ] || { echo "no FFmpeg source in third_party/ffmpeg-src/ffmpeg-8.1.3"; exit 1; }
if [ "$1" = clean ]; then rm -rf "$BUILD"; fi
mkdir -p "$BUILD"
cd "$BUILD"

# ---- what's in it ----------------------------------------------------------------------------------------------------
# Containers: MP4 / MOV / M4A (mov), Matroska / WebM, AVI, ASF (WMV / WMA), Ogg, MP3, FLAC, WAV; MPEG program and
# transport streams (.mpg, .mts: where MPEG-1/2 video lives); raw AAC and AC-3 files.
DEMUXERS=mov,matroska,avi,asf,ogg,mp3,flac,wav,mpegps,mpegts,aac,ac3
# The raw-stream readers too: an MPEG program or transport stream doesn't say what its video is, and FFmpeg finds out by
# trying these on it. (Found by the test: without mpegvideo a .mpg file opened with its sound and no picture.)
DEMUXERS=$DEMUXERS,mpegvideo,m4v,h264,hevc
# Video: H.264, HEVC, VP8, VP9, AV1, MPEG-4 part 2 and Microsoft's three early versions of it, H.263 (MPEG-4's base),
# WMV 7 / 8 / 9, VC-1, MPEG-1/2, Motion JPEG (older cameras' AVI files).
# AV1 twice: "av1" is FFmpeg's own decoder, which only works through the graphics chip; "libdav1d" is dav1d (BSD
# 2-clause), which decodes it on the CPU for PCs whose chip can't. dav1d is built from source first (build_dav1d.sh) and
# goes inside avcodec's DLL. ARCADE_FFMPEG_AV1=0 leaves dav1d out: AV1 then plays only where the graphics chip does it.
VIDEO=h264,hevc,vp8,vp9,av1,mpeg4,msmpeg4v1,msmpeg4v2,msmpeg4v3,h263,wmv1,wmv2,wmv3,vc1,mpeg1video,mpeg2video,mjpeg
DAV1D=
if [ "${ARCADE_FFMPEG_AV1:-1}" != 0 ]; then
    [ -f "$ROOT/obj/dav1d-out/lib/libdav1d.a" ] || sh "$HERE/build_dav1d.sh"
    # (configure asks pkg-config where dav1d is; this PC has none, so a few lines of shell answer for it)
    # (privacy 2026-11-13: relative to obj/ffmpeg-build, where configure runs -- this line is kept inside avutil's DLL
    # (avutil_configuration) and BUILD.txt, so it must not hold the folders of the PC that built it)
    DAV1D="--enable-libdav1d --enable-decoder=libdav1d --pkg-config=../../$REL/pkg-config-dav1d.sh"
fi
# Sound (a video's own): AAC, MP3, MP2 (in .mpg files), Opus, Vorbis, FLAC, AC-3 / E-AC-3, WMA 1 / 2 / Pro / Lossless,
# and plain or lightly packed PCM as AVI, MOV and WAV carry it.
AUDIO=aac,aac_latm,mp3float,mp3,mp2float,opus,vorbis,flac,ac3,eac3,wmav1,wmav2,wmapro,wmalossless
PCM=pcm_s16le,pcm_s16be,pcm_u8,pcm_s8,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_f32le,pcm_f32be,pcm_f64le,pcm_alaw,pcm_mulaw,adpcm_ms,adpcm_ima_wav,adpcm_ima_qt
PARSERS=h264,hevc,vp8,vp9,av1,mpeg4video,h263,vc1,mpegvideo,mjpeg,aac,aac_latm,mpegaudio,opus,vorbis,flac,ac3
# (what a decoder needs between the container and itself; the decoders pull most of these in themselves)
BSFS=h264_mp4toannexb,hevc_mp4toannexb,vp9_superframe_split,av1_frame_split,extract_extradata,aac_adtstoasc,mpeg4_unpack_bframes
# The graphics chip's decoders on Windows: Direct3D 11 (both of FFmpeg's forms of it) and the older DXVA2, which comes free.
HWACCELS=h264_d3d11va,h264_d3d11va2,h264_dxva2,hevc_d3d11va,hevc_d3d11va2,hevc_dxva2,vp9_d3d11va,vp9_d3d11va2,vp9_dxva2,av1_d3d11va,av1_d3d11va2,av1_dxva2

CONFIGURE="$SRC_REL/configure \
 --prefix=../../third_party/ffmpeg \
 --arch=x86_64 --target-os=mingw32 --cross-prefix=x86_64-w64-mingw32- \
 --cc=x86_64-w64-mingw32-clang --cxx=x86_64-w64-mingw32-clang++ --x86asmexe=nasm \
 --enable-shared --disable-static \
 --disable-programs --disable-doc --disable-debug \
 --disable-autodetect --disable-network \
 --disable-avdevice --disable-avfilter \
 --enable-w32threads --disable-pthreads \
 --enable-d3d11va --enable-dxva2 \
 --disable-everything \
 --enable-protocol=file \
 --enable-demuxer=$DEMUXERS \
 --enable-decoder=$VIDEO,$AUDIO,$PCM \
 --enable-parser=$PARSERS \
 --enable-bsf=$BSFS \
 --enable-hwaccel=$HWACCELS \
 $DAV1D"

if [ "$1" != resume ]; then        # ("resume": configure already ran here; straight to make)
echo "== configure"
# shellcheck disable=SC2086
sh $CONFIGURE > configure.out 2>&1 || { tail -30 configure.out; tail -40 ffbuild/config.log; exit 1; }
# configure writes the source's folder the shell's way (/c/src/...), which this make, a plain Windows program, can't
# open: put the Windows way (C:/...) in the files make reads.
SRC_SHELL=$(cd "$SRC_REL" && pwd)
SRC_WINDOWS=$(cd "$SRC_REL" && { pwd -W 2>/dev/null || pwd; })
if [ "$SRC_SHELL" != "$SRC_WINDOWS" ]; then sed -i "s|$SRC_SHELL|$SRC_WINDOWS|g" Makefile ffbuild/config.mak; fi
# (privacy 2026-11-13) FFmpeg's asserts and error messages name their source file (__FILE__), which make gives from the drive
# down: the compiler writes it from the source's own folder on instead ("ffmpeg-8.1.3/libavcodec/..."). Added to config.mak,
# not to configure's line: that line is kept in avutil's DLL, and this folder would be in it again.
echo "CFLAGS += -ffile-prefix-map=$SRC_WINDOWS/=ffmpeg-8.1.3/" >> ffbuild/config.mak
fi
grep -E "^License:" configure.out
grep -q "^License: LGPL version 2.1 or later" configure.out || { echo "not an LGPL 2.1 configuration: stopped"; exit 1; }

echo "== make -j$JOBS"
"$MAKE" SHELL="$MAKE_SHELL" -j"$JOBS" > make.out 2>&1 || { tail -40 make.out; exit 1; }

echo "== install to third_party/ffmpeg"
rm -rf "$OUT"
"$MAKE" SHELL="$MAKE_SHELL" install > install.out 2>&1 || { tail -40 install.out; exit 1; }
# The import libraries beside the headers, as CMake and the test link them; the DLLs alone in bin/.
cd "$OUT"
rm -rf lib/pkgconfig share
for f in bin/*.lib bin/*.def; do [ -f "$f" ] && mv "$f" lib/; done
SRC="$ROOT/third_party/ffmpeg-src/ffmpeg-8.1.3"
cp "$SRC/COPYING.LGPLv2.1" LICENSE.txt
[ -n "$DAV1D" ] && cp "$ROOT/third_party/dav1d-src/dav1d-1.5.4/COPYING" dav1d-LICENSE.txt
{
  echo "FFmpeg $(cat "$SRC/RELEASE"), built for Ultimate No Mercy by $REL/build_ffmpeg.sh from third_party/ffmpeg-src."
  echo "Licence: LGPL version 2.1 or later (LICENSE.txt). Source: https://ffmpeg.org/releases/ffmpeg-$(cat "$SRC/RELEASE").tar.xz"
  echo "Compiler: $(x86_64-w64-mingw32-clang --version | head -1); assembler: $(nasm -v)"
  [ -n "$DAV1D" ] && echo "Inside avcodec's DLL: dav1d 1.5.4 (BSD 2-clause, dav1d-LICENSE.txt), built by $REL/build_dav1d.sh from third_party/dav1d-src. Source: https://downloads.videolan.org/pub/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz"
  echo
  echo "The configure line, run in obj/ffmpeg-build:"
  echo "$CONFIGURE" | tr -s ' '
} > BUILD.txt
ls -l bin
# (privacy 2026-11-13) what ships must not hold the folders of the PC that built it: stop if a DLL or BUILD.txt still does
# (the source and the build are both under $ROOT)
if grep -l -a -F "$ROOT" bin/*.dll BUILD.txt; then echo "FAILED: the build's folders are in the files above"; exit 1; fi
# (privacy 2026-11-16) ... nor any folder OUTSIDE the project: the BUILD.txt of 2026-11-11 kept a --pkg-config path in a
# trial folder beside the project, and the check above (the build's own folders only) would let that through.
# BUILD.txt: every path in the configure line is relative, so no whole path at all (a drive, a /c/-style or a home folder).
if grep -n -E "(^|[ =\"'])([A-Za-z]:[\\\\/]|/[a-z]/|/(home|Users|mnt)/)" BUILD.txt; then
    echo "FAILED: BUILD.txt holds a whole path (lines above): it ships beside the exe"; exit 1; fi
# The DLLs: not the folder the project sits in (so not a folder beside it either), either way of writing it. The project
# is the nearest folder above $ROOT that holds Development\ (a source package without one: the folder above $ROOT).
P=$(cd "$ROOT/.." && pwd); while [ "$P" != / ] && [ ! -d "$P/Development" ] && [ "$(dirname "$P")" != "$P" ]; do P=$(dirname "$P"); done
[ -d "$P/Development" ] || P=$(cd "$ROOT/.." && pwd)
TOP_W=$(cd "$P/.." && { pwd -W 2>/dev/null || pwd; })
TOP_S=$(cd "$P/.." && pwd)
if [ "${#TOP_W}" -gt 3 ]; then
    TOP_B=$(printf '%s' "$TOP_W" | tr / '\\')
    if grep -l -a -i -F -e "$TOP_W/" -e "$TOP_S/" -e "$TOP_B\\" bin/*.dll; then
        echo "FAILED: the DLLs above hold a folder outside the project"; exit 1; fi
fi
echo "== done"
