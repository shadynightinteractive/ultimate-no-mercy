#!/bin/bash
# [xplat 2026-11-12] The Steam Deck build: build_linux.sh inside Valve's Steam Runtime 3 "sniper" SDK container, so the program and its libraries only
# need what SteamOS and the Steam Linux Runtime have (glibc 2.31). A build on a newer system (Ubuntu 24.04: glibc 2.38) does NOT run there.
# Needs a machine with podman or docker and a way to pull the image. No GPU: it only compiles.
#
#   tools/build_steamrt.sh --plume <plume folder> --dxc <dxc folder> [--ffmpeg-src <FFmpeg 8.1.3 tree>] [--build <folder>] [--image <ref>]
#     --dxc: the folder that holds bin/dxc and its libraries (a Linux build of the DirectX Shader Compiler).
#     --ffmpeg-src: a tree of FFmpeg 8.1.3 (the unpacked official tarball, or a checkout of tag n8.1.3).
#     result: <build>/dist/UltimateNoMercy-linux-x86_64.tar.xz
# Environment: ENGINE (podman or docker; default whichever is found), IMAGE (default registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest),
#   JOBS, BUNDLE_SDL (default 0, below), SDL_SRC (with BUNDLE_SDL=1: an SDL 2.30.3 tree, instead of fetching it).
# SDL2 [smallfix 2026-11-13, the user's choice]: the Steam package does NOT carry SDL2. Steam starts a sniper game inside the Steam Linux
#   Runtime (pressure-vessel), which has its own libSDL2-2.0.so.0 (2.32.74 in the sniper SDK and Platform this was tested with), so the
#   program is built against the SDK's copy of that same SDL2 and finds the runtime's at start-up. (The standalone Linux tarball,
#   build_linux.sh on its own, still ships SDL2 and its licence.) BUNDLE_SDL=1 brings back the old way: SDL 2.30.3 built in the
#   container and shipped in lib/ -- for a root that is not the Steam Runtime (an Ubuntu 20.04 root's SDL2 is too old).
# NOT RUN AS WRITTEN IN THE SESSION THAT WROTE IT: the image's registry could not be reached from there (NOTES.md). What was run is steamrt_inner.sh,
# the part that does the work, inside an Ubuntu 20.04 root (same glibc generation).
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
PLUME=""; DXC=""; FFSRC=""; BUILD="$HERE/build-steamrt"; IMAGE=${IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest}
while [ $# -gt 0 ]; do case "$1" in --plume) PLUME=$2; shift 2;; --dxc) DXC=$2; shift 2;; --ffmpeg-src) FFSRC=$2; shift 2;; --build) BUILD=$2; shift 2;; --image) IMAGE=$2; shift 2;; *) sed -n 2,18p "$0"; exit 1;; esac; done
[ -f "$PLUME/plume_vulkan.cpp" ] && [ -x "$DXC/bin/dxc" ] || { echo "need --plume <plume folder> and --dxc <folder with bin/dxc>"; exit 1; }
ENGINE=${ENGINE:-$(command -v podman || command -v docker || true)}; [ -n "$ENGINE" ] || { echo "neither podman nor docker found"; exit 1; }
mkdir -p "$BUILD"; BUILD=$(cd "$BUILD" && pwd)
MOUNTS=(-v "$HERE:/src:ro" -v "$BUILD:/out" -v "$(cd "$PLUME" && pwd):/plume:ro" -v "$(cd "$DXC" && pwd):/dxc:ro")
[ -n "$FFSRC" ] && MOUNTS+=(-v "$(cd "$FFSRC" && pwd):/ffmpeg-src:ro")
# [deck 2026-11-13] plume's RT64 licence lies beside plume's folder (build_linux.sh packs <plume>/../plume_RT64_LICENSE): there, it
# would be outside the /plume mount, so it is given its own, at the place that path means inside (/plume/.. = /)
RT64_LIC="$(cd "$PLUME" && pwd)/../plume_RT64_LICENSE"
[ -f "$RT64_LIC" ] && MOUNTS+=(-v "$(cd "$(dirname "$RT64_LIC")" && pwd)/plume_RT64_LICENSE:/plume_RT64_LICENSE:ro")
[ -n "$SDL_SRC" ] && MOUNTS+=(-v "$(cd "$SDL_SRC" && pwd):/sdl-src:ro" -e SDL_SRC=/sdl-src)
"$ENGINE" run --rm "${MOUNTS[@]}" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -e JOBS="${JOBS:-2}" -e BUNDLE_SDL="${BUNDLE_SDL:-0}" "$IMAGE" bash /src/tools/steamrt_inner.sh
ls -la "$BUILD/dist/UltimateNoMercy-linux-x86_64.tar.xz"
