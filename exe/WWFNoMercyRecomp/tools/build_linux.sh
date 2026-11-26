#!/bin/bash
# [xplat 2026-11-12] The Linux / Steam Deck build in one go: FFmpeg (if not already built), configure, compile with clang, check, and pack a tarball.
#
#   tools/build_linux.sh [--build <folder>] [--out <folder>]
#
# What it makes, in <out> (default: <build>/dist):
#   UltimateNoMercy-linux-x86_64/             the program "Ultimate No Mercy", assets/, recompcontrollerdb.txt, README-LINUX.txt,
#     lib/                                  FFmpeg's shared libraries (LGPL 2.1+, never static), SDL2's (zlib; not with BUNDLE_SDL=0)
#     licenses/                             FFmpeg's licence and build line, SDL2's, plume's (RT64) licence
#   UltimateNoMercy-linux-x86_64.tar.xz       the same, packed
# The program carries an old-style RPATH of $ORIGIN/lib (so lib/ beside it is found, and the libraries there find each other), and is NOT
# linked to FFmpeg: it opens the libraries from lib/ on the first video, so it also starts when they are missing (D3, arcade_ffmpeg_lazy.h).
# The tarball holds no ROM and no game data: those are the player's own (README-LINUX.txt says where they go).
#
# Needs: cmake, ninja, clang/clang++ 16 or newer with libstdc++ from GCC 12 or newer (the port is built with clang everywhere), SDL2 development files, libva headers (the Titantron videos'
# hardware decoding; without them FFmpeg is built CPU-only), nasm (optional: faster FFmpeg), dxc (shaders -> SPIR-V) and plume (the Vulkan layer).
# Environment (all optional but the first two when not found):
#   ARCADE_DXC           path of dxc (else the one on PATH)           ARCADE_PLUME_DIR   plume's folder (default <exe_src>/third_party/plume)
#   ARCADE_FFMPEG_DIR    an installed FFmpeg 8.1.3 (include/, lib/); if unset it is built from FFMPEG_SRC (a source tree) or FFMPEG_TARBALL
#   JOBS                 compile jobs (default: all cores)            CMAKE_EXTRA        more arguments for cmake
#   BUNDLE_SDL           [smallfix 2026-11-13, the user's choice] 1 (the default: the standalone tarball): SDL2 is shipped in lib/ with its
#                        licence -- SDL2_LIB=<folder with libSDL2-2.0.so.0>, else the one the built program is linked against; SDL2_LICENSE=
#                        <its LICENSE.txt>, else the one beside that SDL2 (SDL's own install) or the distribution's copy.
#                        0: SDL2 is not shipped and comes from the system the game runs on -- the Steam package (build_steamrt.sh), whose
#                        SDL2 is the Steam Linux Runtime's.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
BUILD=""; OUT=""
while [ $# -gt 0 ]; do case "$1" in --build) BUILD=$2; shift 2;; --out) OUT=$2; shift 2;; *) echo "usage: build_linux.sh [--build <folder>] [--out <folder>]"; exit 1;; esac; done
BUILD=${BUILD:-$HERE/build-linux}; mkdir -p "$BUILD"; BUILD=$(cd "$BUILD" && pwd)
OUT=${OUT:-$BUILD/dist}; mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
JOBS=${JOBS:-$(nproc)}
for t in cmake ninja clang clang++; do command -v $t > /dev/null || { echo "build_linux.sh: $t not found"; exit 1; }; done

# The toolchain floor, found by trying older ones (an Ubuntu 20.04 root: clang 12 + GCC 10's libstdc++ stop in three places, NOTES.md):
#   clang 16+ (src/game/lifted.cpp has labels at the end of blocks; clang 16 accepts them as C++23 does; tested: 18.1.3)
#   a C++ library with C++20 atomic wait/notify and constexpr std::string: libstdc++ from GCC 12+ (librecomp's recomp.cpp, recompinput's input_types.cpp)
CLV=$(clang --version | sed -n 's/.*clang version \([0-9]*\)\..*/\1/p' | head -1)
[ "${CLV:-0}" -ge 16 ] || { echo "build_linux.sh: clang $CLV is too old: clang 16 or newer is needed (src/game/lifted.cpp)"; exit 1; }
printf '#include <atomic>\n#include <array>\n#include <string>\nstd::atomic<int> a; constexpr std::array<std::string,1> f(){return {};}\nvoid g(){a.wait(0);a.notify_all();}\n' | clang++ -std=c++20 -x c++ - -fsyntax-only 2> /dev/null \
    || { echo "build_linux.sh: clang++'s C++ library lacks C++20 atomic wait/notify or constexpr std::string: libstdc++ from GCC 12 or newer is needed (install g++-12 or newer)"; exit 1; }

# 1. FFmpeg 8.1.3, our own LGPL shared build (the release rules: shared only)
if [ -z "$ARCADE_FFMPEG_DIR" ]; then
    ARCADE_FFMPEG_DIR="$BUILD/ffmpeg"
    if [ ! -f "$ARCADE_FFMPEG_DIR/lib/libavcodec.so" ]; then
        echo "== FFmpeg 8.1.3 (LGPL, shared) -> $ARCADE_FFMPEG_DIR"
        JOBS=$JOBS sh "$HERE/tools/build_ffmpeg_linux.sh" "$BUILD/ffmpeg-work" "$ARCADE_FFMPEG_DIR"
    fi
fi
[ -f "$ARCADE_FFMPEG_DIR/include/libavcodec/avcodec.h" ] || { echo "no FFmpeg in $ARCADE_FFMPEG_DIR"; exit 1; }

# 2. configure and build (clang; the Vulkan picture; FFmpeg loaded on first use)
echo "== configure and build in $BUILD/obj"
cmake -S "$HERE" -B "$BUILD/obj" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DARCADE_VIEW_BACKEND=vulkan -DARCADE_FFMPEG_DIR="$ARCADE_FFMPEG_DIR" ${ARCADE_PLUME_DIR:+-DARCADE_PLUME_DIR="$ARCADE_PLUME_DIR"} \
      ${ARCADE_DXC:+-DARCADE_DXC="$ARCADE_DXC"} -DARCADE_FFMPEG_LAZY=ON ${CMAKE_EXTRA} > "$BUILD/configure.log" 2>&1 || { tail -20 "$BUILD/configure.log"; exit 1; }
ninja -C "$BUILD/obj" -j"$JOBS" NoMercyRecompiled > "$BUILD/build.log" 2>&1 || { tail -30 "$BUILD/build.log"; exit 1; }
EXE="$BUILD/obj/Ultimate No Mercy"

# 3. checks the release depends on
echo "== checks"
DYN=$(readelf -d "$EXE")
echo "$DYN" | grep -q "(RPATH).*\[\$ORIGIN/lib\]" || { echo "FAILED: no old-style RPATH of \$ORIGIN/lib in the program"; exit 1; }
echo "$DYN" | grep -q "(RUNPATH)" && { echo "FAILED: a RUNPATH is set (the libraries could not inherit it)"; exit 1; }
echo "$DYN" | grep -E "NEEDED.*lib(avcodec|avformat|avutil|swscale|swresample)" && { echo "FAILED: the program is linked to FFmpeg (it must load it on first use)"; exit 1; }
echo "  RPATH \$ORIGIN/lib: yes; linked to FFmpeg: no"

# 4. the folder and the tarball
echo "== pack"
P="$OUT/UltimateNoMercy-linux-x86_64"; rm -rf "$P"; mkdir -p "$P/lib" "$P/licenses"
cp "$EXE" "$P/"
cp -a "$BUILD/obj/assets" "$BUILD/obj/recompcontrollerdb.txt" "$P/"
# [portable 2026-11-15] the first start's builder (firstrun/: Python code, the plan of what it makes and their hashes -- no game
# data): it makes the ROM-made files from the player's ROMs, run by the program with the system's python3 (main.cpp). Without
# its Windows Python runtime (firstrun/python): the Deck and Linux have python3. And roms/, where the README says the ROMs go.
if [ -d "$HERE/firstrun" ]; then
    mkdir -p "$P/firstrun" "$P/roms"
    (cd "$HERE/firstrun" && find . -path ./python -prune -o -type f ! -name '*.pyc' -print | while read -r f; do mkdir -p "$P/firstrun/$(dirname "$f")"; cp -a "$f" "$P/firstrun/$f"; done)
    # (the AKI table unpacker, built for Linux: without it firstrun unpacks in Python, the same bytes, slower; the Windows .exe goes)
    rm -f "$P/firstrun/aki_unpack.exe"; clang -O2 "$HERE/firstrun/aki_unpack.c" -o "$P/firstrun/aki_unpack" || echo "  (aki_unpack not built: firstrun will unpack in Python)"
fi
# (the version-named libraries and their links: libavcodec.so.62 -> libavcodec.so.62.28.103; the unversioned "dev" links stay out)
for f in "$ARCADE_FFMPEG_DIR"/lib/lib*.so.[0-9]*; do cp -a "$f" "$P/lib/"; done
BUNDLE_SDL=${BUNDLE_SDL:-1}
if [ "$BUNDLE_SDL" = 1 ]; then
    # [smallfix 2026-11-13] SDL2_LIB unset: the folder of the libSDL2-2.0.so.0 the program was linked against (what cmake found)
    if [ -z "$SDL2_LIB" ]; then
        SO=$(ldd "$EXE" | sed -n 's/^[[:space:]]*libSDL2-2\.0\.so\.0 => \(\/[^ ]*\) .*/\1/p' | head -1)
        [ -n "$SO" ] || { echo "FAILED: the program's libSDL2-2.0.so.0 is not found (set SDL2_LIB=<folder with libSDL2-2.0.so.0>)"; exit 1; }
        SDL2_LIB=$(dirname "$SO")
    fi
    [ -f "$SDL2_LIB/libSDL2-2.0.so.0" ] || { echo "FAILED: no libSDL2-2.0.so.0 in $SDL2_LIB"; exit 1; }
    cp -a "$SDL2_LIB"/libSDL2-2.0.so.0* "$P/lib/"
    # [deck 2026-11-13] its licence (zlib) goes with it: SDL2_LICENSE=<file>, else the one SDL's own install puts beside its lib/
    # [smallfix 2026-11-13] ... else a distribution's copy (Arch/Fedora: licenses/SDL2; Debian/Ubuntu: the package's copyright file)
    SL=$SDL2_LICENSE
    [ -n "$SL" ] || for c in "$SDL2_LIB/../share/licenses/SDL2/LICENSE.txt" /usr/share/licenses/SDL2/LICENSE.txt /usr/share/licenses/sdl2/LICENSE.txt \
                             /usr/share/doc/libsdl2-2.0-0/copyright; do [ -f "$c" ] && { SL=$c; break; }; done
    [ -f "$SL" ] || { echo "FAILED: SDL2 is bundled but its licence was not found (set SDL2_LICENSE=<its LICENSE.txt>)"; exit 1; }
    cp "$SL" "$P/licenses/SDL2-LICENSE.txt"
    # [smallfix 2026-11-13] A newer system's SDL2 may be sdl2-compat: SDL2's interface over SDL3 (the sniper runtime's 2.32.74 is one;
    # newer distributions ship it too). It opens libSDL3.so.0 itself when it starts (dlopen, so ldd doesn't list it), so SDL3 goes with
    # it, and SDL3's licence. (The program's old-style RPATH covers that dlopen too: lib/ is searched first.)
    if grep -a -q -F "libSDL3.so.0" "$SDL2_LIB/libSDL2-2.0.so.0"; then
        S3=$(ls "$SDL2_LIB"/libSDL3.so.0 2>/dev/null || ldconfig -p | sed -n 's/^[[:space:]]*libSDL3\.so\.0 (.*x86-64.*) => //p' | head -1)
        [ -n "$S3" ] && [ -f "$S3" ] || { echo "FAILED: this SDL2 is sdl2-compat, and the libSDL3.so.0 it needs was not found"; exit 1; }
        cp -a "$(dirname "$S3")"/libSDL3.so.0* "$P/lib/"
        L3=$SDL3_LICENSE
        [ -n "$L3" ] || for c in "$(dirname "$S3")/../share/licenses/SDL3/LICENSE.txt" /usr/share/licenses/SDL3/LICENSE.txt /usr/share/licenses/sdl3/LICENSE.txt \
                                 /usr/share/doc/libsdl3-0/copyright; do [ -f "$c" ] && { L3=$c; break; }; done
        [ -f "$L3" ] || { echo "FAILED: SDL3 is bundled (sdl2-compat needs it) but its licence was not found (set SDL3_LICENSE=<its LICENSE.txt>)"; exit 1; }
        cp "$L3" "$P/licenses/SDL3-LICENSE.txt"
        echo "  SDL2 here is sdl2-compat: SDL3 shipped with it"
    fi
fi
cp "$ARCADE_FFMPEG_DIR/LICENSE.txt" "$P/licenses/FFmpeg-LICENSE.txt"; cp "$ARCADE_FFMPEG_DIR/BUILD.txt" "$P/licenses/FFmpeg-BUILD.txt"
PL="${ARCADE_PLUME_DIR:-$HERE/third_party/plume}"
[ -f "$PL/LICENSE" ] && cp "$PL/LICENSE" "$P/licenses/plume-LICENSE"
[ -f "$PL/../plume_RT64_LICENSE" ] && cp "$PL/../plume_RT64_LICENSE" "$P/licenses/plume_RT64_LICENSE"
# (what the README says of SDL2 follows BUNDLE_SDL, smallfix 2026-11-13)
if [ "$BUNDLE_SDL" = 1 ]; then SDLNOTE="lib/ holds SDL2 (zlib licence; licenses/SDL2-LICENSE.txt)."; ls "$P"/lib/libSDL3* > /dev/null 2>&1 && SDLNOTE="lib/ holds SDL2 and the SDL3 it runs on (zlib licence; licenses/SDL2-LICENSE.txt, SDL3-LICENSE.txt)."
else SDLNOTE="SDL2 is the system's (2.26 or newer; on Steam, the Steam Linux Runtime's)."; fi
README=$(cat <<'EOF'
Ultimate No Mercy for Linux / Steam Deck (x86_64)

This package holds no game data (the user, 2026-11-14: "No game data shipped, rom(s) needed.").
Put your WWF No Mercy (USA) (Rev 1) ROM, a .z64 file, in roms/ (the game says so at start-up if it is missing); the
WrestleMania 2000, WCW/nWo Revenge and No Mercy Plus V3 ROMs / patch add their parts. On the first start the game makes
everything it makes from a ROM, from yours, and checks each file (firstrun/firstrun.py, with the system's python3).
Run:  ./"Ultimate No Mercy"

Needs from the system: a Vulkan driver (Mesa RADV on the Steam Deck), libstdc++. @SDLNOTE@
Video: lib/ holds FFmpeg 8.1.3 (LGPL 2.1 or later, shared; licenses/FFmpeg-LICENSE.txt). Its hardware decoding uses VAAPI (libva.so.2, which
SteamOS has). If lib/ or libva is missing the game still starts: Titantron videos from common files are off and entrances play the old way
(the log says "[media] FFmpeg: ...").
EOF
)
printf '%s\n' "${README/@SDLNOTE@/$SDLNOTE}" > "$P/README-LINUX.txt"
# [privacy 2026-11-13] Nothing of the build machine in the package. The compilers write their name and the distribution that made
# them into each program's .comment section ("GCC: (Ubuntu ...)"): that section goes, from the program and the libraries
# (the code and the symbols stay). Then the package must not hold the build machine's folders -- this source folder, the
# build folder, the home folder (the user's name is in it) -- or the release stops here.
objcopy --remove-section=.comment "$P/Ultimate No Mercy"
for f in "$P"/lib/lib*.so*; do [ -L "$f" ] || objcopy --remove-section=.comment "$f"; done
# (smallfix 2026-11-13, approved by the user for 0.82) Only folders two or more deep. Inside the Steam build's container these three
# are /src, /out and /root: they name nothing of the machine and are in every container build (librecomp's assert messages say
# /src/lib/..., FFmpeg's configure line says --prefix=/out/ffmpeg), so the check stopped every Steam build. A machine's own folders
# (/home/<name>/..., /mnt/<drive>/...) are always two or more deep, so they are still looked for.
LEAK_ARGS=(); for d in "$HERE" "$BUILD" "$HOME"; do case "$d" in /?*/?*) LEAK_ARGS+=(-e "$d");; esac; done
LEAKS=""; [ ${#LEAK_ARGS[@]} -gt 0 ] && LEAKS=$(find "$P" -type f -exec grep -l -a -F "${LEAK_ARGS[@]}" {} + || true)
[ -z "$LEAKS" ] || { echo "FAILED: the build machine's folders are in: $LEAKS"; exit 1; }
echo "  the build machine's folders in the package: none"
( cd "$OUT" && tar -cJf UltimateNoMercy-linux-x86_64.tar.xz UltimateNoMercy-linux-x86_64 )

# 5. the packed program starts the way the player will start it: libraries found in lib/, nothing else needed from FFmpeg
echo "== the tarball's program: libraries"
( cd "$P" && ldd "./Ultimate No Mercy" | grep -E "not found" && { echo "FAILED: a library is missing (above)"; exit 1; } || echo "  every library the program needs is found" )
# [smallfix 2026-11-13] and SDL2 from where BUNDLE_SDL says: lib/ (the standalone tarball), or the system's (the Steam package: on the Deck,
# the Steam Linux Runtime's; here, the build root's copy of it)
SDLSO=$(cd "$P" && ldd "./Ultimate No Mercy" | sed -n 's/^[[:space:]]*libSDL2-2\.0\.so\.0 => \([^ ]*\) .*/\1/p' | head -1)
SDLDIR=$( [ -n "$SDLSO" ] && cd "$P" && dirname "$(readlink -f "$SDLSO")" || true )
case "$BUNDLE_SDL:$SDLDIR" in
    1:"$(readlink -f "$P/lib")") echo "  SDL2: lib/ (shipped)";;
    0:/*) ls "$P"/lib/libSDL* > /dev/null 2>&1 && { echo "FAILED: BUNDLE_SDL=0 but lib/ holds SDL"; exit 1; }; echo "  SDL2: the system's, not shipped ($SDLSO here)";;
    *) echo "FAILED: SDL2 is not where BUNDLE_SDL=$BUNDLE_SDL says (ldd: ${SDLSO:-none})"; exit 1;;
esac
ls -la "$OUT/UltimateNoMercy-linux-x86_64.tar.xz"
echo "done: $OUT/UltimateNoMercy-linux-x86_64.tar.xz"
