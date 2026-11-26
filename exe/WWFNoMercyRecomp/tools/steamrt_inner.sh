#!/bin/bash
# [xplat 2026-11-12] Runs INSIDE the build container (called by build_steamrt.sh): fetches what a clean Debian/Ubuntu-based root lacks, then
# runs build_linux.sh. Works in Valve's Steam Runtime "sniper" SDK image and in any root with the same old glibc (an Ubuntu 20.04 root
# was used where the image could not be pulled: NOTES.md). Idempotent: a second run is a rebuild.
# Expects (build_steamrt.sh sets them): /src = exe_src, /out = the build folder, /plume, /dxc (dxc's folder with bin/dxc), optional /ffmpeg-src.
set -e
export DEBIAN_FRONTEND=noninteractive
have() { command -v "$1" > /dev/null 2>&1; }
if have apt-get && [ "$(id -u)" = 0 ]; then
    apt-get update -q > /dev/null 2>&1 || true
    # (names that exist on Debian 11 and Ubuntu 20.04; one missing name must not stop the rest, so one at a time when the batch fails)
    PK="clang make git ca-certificates pkg-config nasm xz-utils file binutils python3-pip gcc g++ libva-dev libasound2-dev libpulse-dev libx11-dev libxext-dev
        libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev libdrm-dev libgbm-dev libudev-dev libdbus-1-dev
        libegl1-mesa-dev libgl1-mesa-dev"
    apt-get install -y -q --no-install-recommends $PK > /dev/null 2>&1 || for p in $PK; do apt-get install -y -q --no-install-recommends "$p" > /dev/null 2>&1 || echo "steamrt_inner: could not install $p"; done
fi
# clang under the plain names build_linux.sh uses (Debian/Ubuntu name it clang-NN)
if ! have clang; then c=$(ls /usr/bin/clang-[0-9]* 2>/dev/null | sort -V | tail -1); [ -n "$c" ] && ln -sf "$c" /usr/local/bin/clang && ln -sf "${c/clang/clang++}" /usr/local/bin/clang++; fi
# clang 16+ is needed (build_linux.sh checks and says why). Debian 11's own is 11: take LLVM's apt build when the image's is older.
clangv() { clang --version 2>/dev/null | sed -n 's/.*clang version \([0-9]*\)\..*/\1/p' | head -1; }
if [ "${CV:=$(clangv)}" -lt 16 ] 2>/dev/null || [ -z "$CV" ]; then
    echo "steamrt_inner: clang ${CV:-none} is too old; trying LLVM's apt repository (apt.llvm.org) for clang 18"
    # [deck 2026-11-13] llvm.sh refuses the Steam Runtime (it knows Debian and Ubuntu code names, not "sniper"), so the repository is
    # added by hand, for the Debian release the runtime is built from (sniper: Debian 11 "bullseye"; soldier: Debian 10 "buster").
    . /etc/os-release
    case "$VERSION_CODENAME" in sniper) DEB=bullseye;; soldier) DEB=buster;; *) DEB=$VERSION_CODENAME;; esac
    if have wget && have gpg && have apt-get && [ -n "$DEB" ]; then
        wget -q -O- https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor > /usr/share/keyrings/apt-llvm-org.gpg
        echo "deb [signed-by=/usr/share/keyrings/apt-llvm-org.gpg] http://apt.llvm.org/$DEB/ llvm-toolchain-$DEB-18 main" > /etc/apt/sources.list.d/apt-llvm-org-18.list
        { apt-get update -q && apt-get install -y -q --no-install-recommends clang-18; } > /tmp/llvm.log 2>&1 || { tail -20 /tmp/llvm.log; echo "steamrt_inner: could not install clang 18"; }
    fi
    [ -x /usr/bin/clang-18 ] && ln -sf /usr/bin/clang-18 /usr/local/bin/clang && ln -sf /usr/bin/clang++-18 /usr/local/bin/clang++
fi
# [deck 2026-11-13] The C++ library: the Steam Runtime's own libstdc++ is GCC 10's (too old: build_linux.sh says why). The sniper SDK
# also carries GCC 14 in /usr/lib/gcc-14, built without shared run-time libraries (libstdc++.a; libgcc.a holds the unwinder; there is
# no libstdc++.so, libgcc_s or libgcc_eh), so what it builds needs only libc/libm/libpthread at run time, nothing newer than the
# runtime has. clang is pointed at that GCC (--gcc-toolchain) and links the way g++-14 itself does (libstdc++ and libgcc static,
# --unwindlib=none because the unwinder is already in libgcc.a), through two small wrappers in /usr/local/bin.
# (Written again on every run, from the real clang, so a second run in a kept container is the same as the first.)
REALCC=$(ls /usr/bin/clang-[0-9]* 2>/dev/null | grep -E '/clang-[0-9]+$' | sort -V | tail -1); REALXX=${REALCC/clang/clang++}
if [ -d /usr/lib/gcc-14/lib/gcc/x86_64-linux-gnu/14 ] && [ -x "$REALXX" ] && ! printf '#include <atomic>\nvoid g(std::atomic<int>&a){a.wait(0);}\n' | "$REALXX" -std=c++20 -x c++ - -fsyntax-only 2> /dev/null; then
    for w in clang clang++; do
        if [ $w = clang ]; then r=$REALCC; LINK="-static-libgcc --unwindlib=none"; else r=$REALXX; LINK="-static-libstdc++ -static-libgcc --unwindlib=none"; fi
        rm -f /usr/local/bin/$w
        cat > /usr/local/bin/$w <<EOF
#!/bin/sh
# [deck 2026-11-13] $r with the sniper SDK's GCC 14 and its static run-time libraries: see steamrt_inner.sh
for a in "\$@"; do case "\$a" in -c|-S|-E|-M|-MM|-fsyntax-only) exec "$r" --gcc-toolchain=/usr/lib/gcc-14 "\$@";; esac; done
exec "$r" --gcc-toolchain=/usr/lib/gcc-14 $LINK "\$@"
EOF
        chmod +x /usr/local/bin/$w
    done
    echo "steamrt_inner: clang uses the SDK's GCC 14 (libstdc++ and libgcc linked statically)"
fi
# cmake 3.20+ and ninja (the image's may be older or absent; PyPI's are fine)
cmake_ok() { have cmake && [ "$(printf '3.20\n%s\n' "$(cmake --version | head -1 | awk '{print $3}')" | sort -V | head -1)" = 3.20 ]; }
if ! cmake_ok || ! have ninja; then pip3 install -q cmake ninja; export PATH=/usr/local/bin:$PATH; fi
for t in clang clang++ cmake ninja git; do have $t || { echo "steamrt_inner: $t is missing"; exit 1; }; done
# SDL2 [smallfix 2026-11-13, the user's choice]: by default the Steam Runtime's own (build_steamrt.sh says why): the program is built against
# the SDK's SDL2 development files -- the same SDL2 the runtime's Platform carries -- and lib/ holds no SDL2, so on the Deck the runtime's
# libSDL2-2.0.so.0 is the one loaded. (SDL2_DIR is given outright, so a build folder configured the old way, with /opt/sdl2, can't keep it.)
# BUNDLE_SDL=1: the old way, SDL2 2.30.3 from source, shipped in lib/ (a root whose own SDL2 is older than the 2.26 the port needs).
if [ "${BUNDLE_SDL:-0}" = 1 ]; then
    SDLP=/opt/sdl2
    if [ ! -f $SDLP/lib/libSDL2-2.0.so.0 ]; then
        echo "== SDL2 2.30.3 -> $SDLP"
        T=$(mktemp -d)
        if [ -n "$SDL_SRC" ]; then cp -a "$SDL_SRC" "$T/s"; else git clone -q --depth 1 --branch release-2.30.3 https://github.com/libsdl-org/SDL.git "$T/s"; fi
        cmake -S "$T/s" -B "$T/b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_INSTALL_PREFIX=$SDLP -DSDL_STATIC=OFF -DSDL_TEST=OFF > "$T/cfg.log" 2>&1         || { tail -30 "$T/cfg.log"; echo "steamrt_inner: SDL2's configure failed"; exit 1; }   # [deck 2026-11-13] say why, not just stop
        ninja -C "$T/b" -j"${JOBS:-2}" > "$T/build.log" 2>&1 || { tail -30 "$T/build.log"; echo "steamrt_inner: SDL2's build failed"; exit 1; }
        ninja -C "$T/b" install > /dev/null
    fi
    export BUNDLE_SDL=1 SDL2_LIB=$SDLP/lib
    SDL_CMAKE="-DCMAKE_PREFIX_PATH=$SDLP"
else
    SDLD=$(ls -d /usr/lib/x86_64-linux-gnu/cmake/SDL2 /usr/lib/cmake/SDL2 /usr/lib64/cmake/SDL2 2>/dev/null | head -1)
    SDLV=$(pkg-config --modversion sdl2 2>/dev/null || true)
    [ -n "$SDLD" ] && [ -n "$SDLV" ] || { echo "steamrt_inner: this root has no SDL2 development files (the Steam Runtime SDK has them; elsewhere: BUNDLE_SDL=1)"; exit 1; }
    [ "$(printf '2.26\n%s\n' "$SDLV" | sort -V | head -1)" = 2.26 ] \
        || { echo "steamrt_inner: this root's SDL2 is $SDLV, older than the 2.26 the port needs (BUNDLE_SDL=1 builds 2.30.3)"; exit 1; }
    echo "== SDL2: the runtime's own, $SDLV ($SDLD), not shipped"
    export BUNDLE_SDL=0; unset SDL2_LIB
    SDL_CMAKE="-DSDL2_DIR=$SDLD"
fi
echo "glibc here: $(ldd --version | head -1)"
export ARCADE_PLUME_DIR=/plume ARCADE_DXC=/dxc/bin/dxc CMAKE_EXTRA="$SDL_CMAKE ${CMAKE_EXTRA}"
[ -d /ffmpeg-src ] && export FFMPEG_SRC=/ffmpeg-src
rc=0
bash /src/tools/build_linux.sh --build /out --out /out/dist || rc=$?
[ -n "$HOST_UID" ] && chown -R "$HOST_UID:$HOST_GID" /out 2>/dev/null || true
exit $rc
