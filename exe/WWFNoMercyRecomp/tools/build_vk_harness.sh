#!/bin/bash
# [xplat 2026-11-12] Builds tools/vk_harness.cpp with the Vulkan picture backend, for a Linux check without the game (clang, as the port is).
#   tools/build_vk_harness.sh <root of exe_src> <plume folder> <dxc> <out folder>      e.g.  tools/build_vk_harness.sh . third_party/plume /usr/bin/dxc /tmp/vkh
# Needs: clang++, SDL2 headers, the Vulkan loader (libvulkan.so.1) at run time. Makes <out>/vk_harness. (CMake does the same for the game itself.)
set -e
R=$(cd "$1" && pwd); P=$(cd "$2" && pwd); DXC=$3; O=$4; mkdir -p "$O/shaders"
S=$R/src/main/shaders
mk() { n=$1; src=$2; ent=$3; prof=$4; fl=""; case $prof in vs*) fl="-fvk-invert-y";; esac
  [ "$O/shaders/$n.spv.h" -nt "$S/$src" ] && return 0
  "$DXC" -spirv -fspv-target-env=vulkan1.1 -T $prof -E $ent $fl "$S/$src" -Fo "$O/shaders/$n.spv" && cmake -DIN="$O/shaders/$n.spv" -DOUT="$O/shaders/$n.spv.h" -DNAME=spv_$n -P "$R/tools/spv_to_header.cmake"; }
mk scene_vs arcade_view_scene.vk.hlsl vs vs_6_0; mk scene_ps arcade_view_scene.vk.hlsl ps ps_6_0; mk scene_screen arcade_view_scene.vk.hlsl ps_screen ps_6_0
mk scene_shape arcade_view_scene.vk.hlsl ps_shape ps_6_0; mk post_vs arcade_view_post.vk.hlsl vs vs_6_0; mk post_ps arcade_view_post.vk.hlsl ps ps_6_0; mk post_blit arcade_view_post.vk.hlsl ps_blit ps_6_0
INC="-I$R/src/main -I$R/src/game -I$R/include -I$P -I$P/contrib/volk -I$P/contrib/Vulkan-Headers/include -I$P/contrib/VulkanMemoryAllocator/include -I$O/shaders -I/usr/include/SDL2 -DPLUME_SDL_VULKAN_ENABLED"
CXX="clang++ -std=c++20 -O1 -g -Wno-everything -fms-extensions"
[ -f "$O/plume.o" ] || $CXX -c $INC "$P/plume_vulkan.cpp" -o "$O/plume.o"
$CXX -c $INC "$R/src/main/arcade_view_vulkan.cpp" -o "$O/arcade_view_vulkan.o"
$CXX -c $INC "$R/src/main/arcade_platform_posix.cpp" -o "$O/arcade_platform_posix.o"
$CXX -c $INC "$R/tools/vk_harness.cpp" -o "$O/vk_harness.o"
$CXX "$O"/*.o -o "$O/vk_harness" -lSDL2 -ldl -lpthread
echo "built $O/vk_harness"
