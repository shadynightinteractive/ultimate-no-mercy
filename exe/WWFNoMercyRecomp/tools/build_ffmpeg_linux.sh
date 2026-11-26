#!/bin/sh
# build_ffmpeg_linux.sh <work dir> <out dir>: the game's own FFmpeg for Linux, from the same source and with the same
# trimmed list as tools/ffmpeg_build/build_ffmpeg.sh makes the Windows DLLs (xplat, 2026-11-12: the prototype of
# PLAN.md step 6). LGPL 2.1 or later, shared libraries (.so), never static: the same rules as on Windows.
# What differs from the Windows build, and only this:
#   --target-os=linux, the system's compiler, POSIX threads (not w32threads);
#   the graphics chip's decoders through VAAPI (Mesa's on AMD and Intel, the Steam Deck's included) instead of
#   Direct3D 11 / DXVA2 -- the player's code already asks for VAAPI on Linux (arcade_media_decode.cpp hardware_type);
#   no run path in the libraries themselves: the game is linked with an old-style RPATH ($ORIGIN/lib,
#   --disable-new-dtags), which also covers what its libraries need, so they find each other beside it. (Writing
#   $ORIGIN through FFmpeg's configure and make was tried twice: it came out "279445ORIGIN", then "RIGIN".)
#   no dav1d yet (AV1 then plays only where the graphics chip decodes it, as ARCADE_FFMPEG_AV1=0 does on Windows).
# [xplat 2026-11-12] moved to tools/ with build_linux.sh; FFMPEG_SRC added (a tarball can't always be reached).
# Environment: FFMPEG_TARBALL (the official ffmpeg-8.1.3.tar.xz, signature checked as NOTES.md says) or FFMPEG_SRC (a tree), NASM (an x86
# assembler; without one, --disable-x86asm), VAAPI_SYSROOT (libva's headers and .so, when not in /usr: configure finds
# libva through pkg-config, so PKG_CONFIG names one when it isn't on the PATH), JOBS.
set -e
WORK="$1"; OUT="$2"
[ -n "$WORK" ] && [ -n "$OUT" ] || { echo "usage: build_ffmpeg_linux.sh <work dir> <out dir>"; exit 1; }
mkdir -p "$WORK" "$OUT"
WORK=$(cd "$WORK" && pwd); OUT=$(cd "$OUT" && pwd)   # (2026-11-13: whole paths, for the install's DESTDIR and BUILD.txt's stand-ins below)
cd "$WORK"
# The source: FFMPEG_SRC (a folder holding FFmpeg 8.1.3's tree: a checkout of git tag n8.1.3, say) or FFMPEG_TARBALL (the official
# ffmpeg-8.1.3.tar.xz). Both are the same source; only the tarball has the release signature to check.
if [ ! -d ffmpeg-8.1.3 ]; then
    if [ -n "$FFMPEG_SRC" ]; then cp -a "$FFMPEG_SRC" ffmpeg-8.1.3; else tar xf "${FFMPEG_TARBALL:?set FFMPEG_TARBALL or FFMPEG_SRC}"; fi
fi
grep -q '^8\.1\.3' ffmpeg-8.1.3/RELEASE || { echo "not FFmpeg 8.1.3: stopped"; exit 1; }
mkdir -p build && cd build
DEMUXERS=mov,matroska,avi,asf,ogg,mp3,flac,wav,mpegps,mpegts,aac,ac3,mpegvideo,m4v,h264,hevc
VIDEO=h264,hevc,vp8,vp9,av1,mpeg4,msmpeg4v1,msmpeg4v2,msmpeg4v3,h263,wmv1,wmv2,wmv3,vc1,mpeg1video,mpeg2video,mjpeg
AUDIO=aac,aac_latm,mp3float,mp3,mp2float,opus,vorbis,flac,ac3,eac3,wmav1,wmav2,wmapro,wmalossless
PCM=pcm_s16le,pcm_s16be,pcm_u8,pcm_s8,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_f32le,pcm_f32be,pcm_f64le,pcm_alaw,pcm_mulaw,adpcm_ms,adpcm_ima_wav,adpcm_ima_qt
PARSERS=h264,hevc,vp8,vp9,av1,mpeg4video,h263,vc1,mpegvideo,mjpeg,aac,aac_latm,mpegaudio,opus,vorbis,flac,ac3
BSFS=h264_mp4toannexb,hevc_mp4toannexb,vp9_superframe_split,av1_frame_split,extract_extradata,aac_adtstoasc,mpeg4_unpack_bframes
HW=""
if [ -n "$VAAPI_SYSROOT" ] || [ -f /usr/include/va/va.h ]; then
    HW="--enable-vaapi --enable-hwaccel=h264_vaapi,hevc_vaapi,vp9_vaapi,av1_vaapi"
    if [ -n "$VAAPI_SYSROOT" ]; then
        export PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1 PKG_CONFIG_ALLOW_SYSTEM_LIBS=1 PKG_CONFIG_SYSROOT_DIR="$VAAPI_SYSROOT" PKG_CONFIG_LIBDIR="$VAAPI_SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$VAAPI_SYSROOT/usr/share/pkgconfig"
    fi
    HW="$HW --pkg-config=${PKG_CONFIG:-pkg-config}"
fi
ASM="--x86asmexe=${NASM:-nasm}"
# (FFmpeg 8's assembly needs NASM 2.15 or newer: Ubuntu 20.04's 2.14 stops in vp8dsp.asm with "(ALLOC_STACK:4) expecting `)'")
NV=$(${NASM:-nasm} -v 2>/dev/null | awk '{print $3}')
if [ -n "$NV" ] && [ "$(printf '2.15\n%s\n' "$NV" | sort -V | head -1)" != 2.15 ]; then echo "NASM $NV is too old for FFmpeg 8 (needs 2.15+): set NASM=<a newer nasm>"; exit 1; fi
[ -n "$NASM" ] || command -v nasm > /dev/null || ASM="--disable-x86asm"
# shellcheck disable=SC2086
# (privacy 2026-11-13: --prefix=/ffmpeg and "make install DESTDIR" below, not --prefix="$OUT": configure's line is kept inside
# libavutil (avutil_configuration) and BUILD.txt, so it must not hold the build machine's folders)
../ffmpeg-8.1.3/configure --prefix=/ffmpeg --arch=x86_64 --target-os=linux $ASM \
 --enable-shared --disable-static --disable-programs --disable-doc --disable-debug \
 --disable-autodetect --disable-network --disable-avdevice --disable-avfilter \
 --enable-pthreads --disable-everything --enable-protocol=file \
 --enable-demuxer=$DEMUXERS --enable-decoder=$VIDEO,$AUDIO,$PCM --enable-parser=$PARSERS --enable-bsf=$BSFS \
 $HW > configure.out 2>&1 || { tail -30 configure.out; tail -30 ffbuild/config.log; exit 1; }
grep -q "License: LGPL version 2.1 or later" configure.out || { echo "not LGPL 2.1+: stopped"; grep License configure.out; exit 1; }
grep -E "^(License|External libraries|Enabled hwaccels|hwaccels)" -A2 configure.out | head -12
# (privacy 2026-11-13) __FILE__ in FFmpeg's asserts and messages: from the source's folder on ("ffmpeg-8.1.3/libavcodec/..."), not
# from / down (a home folder has the user's name in it). In config.mak, not configure's line, which is kept in libavutil.
SRC_ABS=$(cd ../ffmpeg-8.1.3 && pwd)
echo "CFLAGS += -ffile-prefix-map=$SRC_ABS/=ffmpeg-8.1.3/" >> ffbuild/config.mak
nice -n 19 make -j"${JOBS:-1}" > make.out 2>&1 || { tail -30 make.out; exit 1; }
rm -rf "$WORK/stage"
make install DESTDIR="$WORK/stage" > install.out 2>&1
cp -a "$WORK/stage/ffmpeg/." "$OUT/"
cp ../ffmpeg-8.1.3/COPYING.LGPLv2.1 "$OUT/LICENSE.txt"
# (privacy 2026-11-13: the configure line without the build machine's folders: <work> and <home> stand for them)
{ echo "configure line (run in <work>/build; its output: configure.out there):"
  if [ "${#HOME}" -gt 1 ]; then H=$HOME; else H='<no home folder>'; fi    # (HOME=/ in a container: nothing to stand in for)
  head -1 ffbuild/config.log | sed -e "s#$WORK#<work>#g" -e "s#$OUT#<out>#g" -e "s#$H#<home>#g"; } > "$OUT/BUILD.txt"
ls -la "$OUT/lib"
