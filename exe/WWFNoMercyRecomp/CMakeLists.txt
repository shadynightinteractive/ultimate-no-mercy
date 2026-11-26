# CMakeLists.txt for WWF No Mercy: Recompiled (bootstrap; adapted from Revenge/World Tour)
# The recompiled game + ultramodern + librecomp + recompinput + the native renderer (src/main/arcade_view.cpp). RT64 and recompui are gone.
# Adapted from BMHero. Configure with clang-cl (see tools/env-msvc.ps1):
#   cmake -S . -B build-msvc -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-msvc --target NoMercyRecompiled
cmake_minimum_required(VERSION 3.20)
project(NoMercyRecompiled LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()
if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
    add_compile_definitions(NOMINMAX)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # Both SDL2 copies in this build (rt64's vendored 2.26.3 and the FetchContent
        # 2.30.3 devel headers) define _m_prefetch in SDL_endian.h behind
        # #ifndef __PRFCHWINTRIN_H, as a workaround for old clang. clang 20+ makes
        # _m_prefetch a target builtin and hard-errors on that redefinition
        # (-fno-builtin-<name> does NOT apply to target builtins — verified on CI's
        # clang-cl 22). Force-including intrin.h first lets clang's own prfchwintrin.h
        # set the guard, so SDL's hack is skipped in every TU that includes SDL.
        if (CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
            add_compile_options(/FIintrin.h)
        else()
            add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-include intrin.h>")
        endif()
    endif()
endif()
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Privacy (2026-11-13): the exe must not carry the folders it was built in. __FILE__ (asserts, error messages in
# librecomp, ultramodern, sljit, main.cpp) and debug info would otherwise hold the absolute source path; the compiler
# rewrites the source folder to ".", the build folder to "build" and the compiler's own folder to "toolchain" (for its
# headers). -ffile-prefix-map covers __FILE__, debug info and coverage at once. Set here, before any add_subdirectory,
# so every target gets it: the runtime libraries, the recompiled game, lifted.cpp and the mod's Stage 1 sources. It
# only changes text the compiler writes, never the machine code. (GCC tries the last map first and clang the longest,
# so the build folder, which can sit inside the source folder, comes last and wins with both.)
get_filename_component(ARCADE_TOOLCHAIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)   # (its bin folder)
get_filename_component(ARCADE_TOOLCHAIN_DIR "${ARCADE_TOOLCHAIN_DIR}" DIRECTORY)
set(ARCADE_PREFIX_MAPS
    "-ffile-prefix-map=${ARCADE_TOOLCHAIN_DIR}/=toolchain/"
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=./"
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}/=build/")
if (CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
    list(TRANSFORM ARCADE_PREFIX_MAPS PREPEND "/clang:")   # (clang-cl passes them on with /clang:)
endif()
if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach (MAP IN LISTS ARCADE_PREFIX_MAPS)
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${MAP}>")
    endforeach()
endif()

set(NMR ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime)

# --- Runtime core (ultramodern + librecomp) ---
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${NMR})

set(RECOMP_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${NMR}/ultramodern/include
    ${NMR}/librecomp/include
    ${NMR}/N64Recomp/include
)

# --- RecompiledFuncs: the recompiled game ---
add_library(RecompiledFuncs STATIC)
# (2026-11-13, final082, the ARM64 proof: every file with the game's float code -- RecompiledFuncs, lifted.cpp, the mod's Stage 1
#  sources -- is compiled with -ffp-contract=off. The N64 has no fused multiply-add in these operations: a compiler that fuses
#  a * b + c (clang does by default where the CPU has the instruction: ARM64, the Mac, x86 with -mfma) gives other bits than the
#  game. On today's x86 build nothing can be fused, so it changes no code there. recomp_canary.c below checks it at start-up.)
# (relclean 2026-11-19) -fno-slp-vectorize: clang's SLP vectorizer packed runs of the translated game code's constant stores --
#  e.g. a display list's gDPLoadTLUT words, written one by one with the game's own immediates -- into 16-byte constants in the
#  program's data, where the release audit found them equal to the ROMs (eight places, 32-byte runs). Without it each word stays
#  an immediate of its own store instruction: the same stores, the same results (integer stores; no float is re-associated).
#  -mllvm -combiner-store-merging=0: the code generator's own store merging did the same in two more places (funcs_45 / 48:
#  the same gDPLoadTLUT run as a 32-byte constant); off, those stores stay one word each too. (The A/B: lockstep replay_diff the
#  same state; the cost measured -- private notes STATE_relclean.md.)
target_compile_options(RecompiledFuncs PRIVATE -Wno-everything -ffp-contract=off -fno-slp-vectorize "SHELL:-mllvm -combiner-store-merging=0")
target_sources(RecompiledFuncs PRIVATE ${CMAKE_SOURCE_DIR}/src/game/recomp_canary.c)
target_include_directories(RecompiledFuncs PRIVATE ${RECOMP_INCLUDES})
file(GLOB FUNC_C_SOURCES ${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c)
target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES})

# --- [wcw romhack] Functions the post-recompile pipeline repaired (aki-roadmap#29) ---
# tools/romhack/list_repaired_functions.py writes RecompiledFuncs/repaired_functions.txt; the runtime warns when a
# ROM-patch mod changes one of them (the live recompiler has none of the repairs). Regenerated when the list changes.
set(REPAIRED_LIST ${CMAKE_SOURCE_DIR}/RecompiledFuncs/repaired_functions.txt)
set(REPAIRED_CPP ${CMAKE_CURRENT_BINARY_DIR}/repaired_functions.cpp)
set(REPAIRED_NAMES "")
if (EXISTS ${REPAIRED_LIST})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REPAIRED_LIST})
    file(STRINGS ${REPAIRED_LIST} REPAIRED_NAMES REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
else()
    message(WARNING "No ${REPAIRED_LIST}: ROM-patch mods won't be checked against repaired functions "
                    "(run tools/romhack/list_repaired_functions.py after the post-recompile pipeline).")
endif()
list(LENGTH REPAIRED_NAMES REPAIRED_COUNT)
set(REPAIRED_TEXT "// Generated by CMakeLists.txt from RecompiledFuncs/repaired_functions.txt. Do not edit.\n#include \"recomp.h\"\n#include \"librecomp/mods.hpp\"\n\n")
foreach (NAME IN LISTS REPAIRED_NAMES)
    string(APPEND REPAIRED_TEXT "extern \"C\" void ${NAME}(uint8_t* rdram, recomp_context* ctx);\n")
endforeach()
string(APPEND REPAIRED_TEXT "\nvoid wcw_register_repaired_functions() {\n")
if (REPAIRED_COUNT GREATER 0)
    string(APPEND REPAIRED_TEXT "    static recomp_func_t* const funcs[] = {\n")
    foreach (NAME IN LISTS REPAIRED_NAMES)
        string(APPEND REPAIRED_TEXT "        ${NAME},\n")
    endforeach()
    string(APPEND REPAIRED_TEXT "    };\n    recomp::mods::register_repaired_functions(funcs, recomp::mods::RepairedFunctionPolicy::Warn);\n")
else()
    string(APPEND REPAIRED_TEXT "    recomp::mods::register_repaired_functions({}, recomp::mods::RepairedFunctionPolicy::Warn);\n")
endif()
string(APPEND REPAIRED_TEXT "}\n")
file(CONFIGURE OUTPUT ${REPAIRED_CPP} CONTENT "${REPAIRED_TEXT}" @ONLY)
message(STATUS "Repaired functions registered for ROM-patch mods: ${REPAIRED_COUNT}")

# (Patches pipeline omitted in bootstrap — reintroduce from World Tour's
#  CMakeLists when patches/ exists. The idle-thread fix is a toml
#  instruction patch in revenge.toml, not a C patch.)

# --- SDL2 (the window, the pads; recompinput expects sdl2_SOURCE_DIR on Windows) ---
if (WIN32)
    include(FetchContent)
    set(SDL2_VERSION "2.30.3")
    FetchContent_Declare(sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip)
    FetchContent_MakeAvailable(sdl2)
else()
    # [xplat 2026-11-12] Linux and the Mac: the system's SDL2 (a distribution's libsdl2-dev, Homebrew's sdl2, or one given
    # with -DSDL2_DIR=<its cmake folder>). recompinput reads SDL2_INCLUDE_DIRS. (Windows keeps the download above.)
    find_package(SDL2 REQUIRED)
    message(STATUS "SDL2 ${SDL2_VERSION}: ${SDL2_INCLUDE_DIRS}")
endif()

# --- recompinput (the pads, the keyboard) ---
set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH ${NMR})
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompinput)
target_include_directories(recompinput PUBLIC ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/lib/GamepadMotionHelpers)

# --- Main executable ---
add_executable(NoMercyRecompiled)

# App icon: exe resource (Windows title bar/taskbar/Explorer).
if (WIN32)
    enable_language(RC)
    target_sources(NoMercyRecompiled PRIVATE ${CMAKE_SOURCE_DIR}/icons/app.rc)
    # The RC compiler doesn't emit depfiles for referenced resources, so without this
    # an app.ico change leaves a stale app.rc.res and the exe keeps the OLD icon
    # (bit us when replacing the bootstrap's Wm2k-derived icon).
    set_property(SOURCE ${CMAKE_SOURCE_DIR}/icons/app.rc PROPERTY
        OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/icons/app.ico)
endif()
set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/main/main.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_pads.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_stories.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_pages.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_hd.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_scene.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_view.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_bones.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_host.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_audio.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_audio_filter.cpp   # [audiotable 2026-11-18] the resampler's filter from the player's ROM
    ${CMAKE_SOURCE_DIR}/src/main/arcade_vi_mode.cpp        # [relclean 2026-11-19] the runtime's stand-in video mode from the player's ROM
    ${CMAKE_SOURCE_DIR}/src/main/arcade_cont.cpp
    ${CMAKE_SOURCE_DIR}/src/main/nm_replay.cpp   # [replay 2026-11-14] recording, replay, per-retrace hashes, no-graphics mode
    ${CMAKE_SOURCE_DIR}/src/game/lifted.cpp
    ${CMAKE_SOURCE_DIR}/src/game/lift_canary.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_packs.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_firstrun.cpp         # (firstrun 2026-11-17) the first start: the mod made from the player's ROM
    ${CMAKE_SOURCE_DIR}/src/main/arcade_startup_status.cpp   #   (startup 2026-11-19) the window that says what a start is making
    ${CMAKE_SOURCE_DIR}/src/main/arcade_firstrun_ports.cpp   #   (its tables computed from the game's data, made again in C++)
    ${CMAKE_SOURCE_DIR}/src/main/arcade_firstrun_labels.cpp  #   (the menu lettering cut from the game's labels)
    ${CMAKE_SOURCE_DIR}/src/main/arcade_firstrun_stages.cpp  #   (the game's ten arenas' stage sets)
    ${CMAKE_SOURCE_DIR}/src/main/pillow_mediancut/Quant_mediancut.c   #   (Pillow's median cut, its own code: LICENSE.txt there)
    ${CMAKE_SOURCE_DIR}/src/main/pillow_mediancut/QuantHash.c
    ${CMAKE_SOURCE_DIR}/src/main/pillow_mediancut/QuantHeap.c
    # (phase3 2026-11-19) the ROM importer: the other games' extras built natively on the first start (src/romimport)
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_core.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_roms.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/vcdiff.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_mips.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_images.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_json.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_models.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_capture.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_arenascan.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_data.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_movelib.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_vadpcm.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_place.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_themes.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/revplay.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_redux.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_build.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/rr_core.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_hdrfill.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_content.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_imports2.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_wmroster.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_akimodel.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_wmmoves.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_wmmedia.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_collide.cpp      # (p3collide) arenas_collide.py's computation: also the first start's walls / ringstage ports'
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_collide_io.cpp   #   (its inputs from the headers, the result files' JSON)
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_wmport.cpp       # (p3wmport) gen/wmport.py: the pack wmport.bin and its 11 headers
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_wmarenas.cpp     #   the arcade arenas in it (arenas*.py, rr_arena.py; + ri_wmarenas2.inc)
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_wmmesh.cpp       #   arenas_ring.py Mesh
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_wmvenues.cpp     #   (p3wmvenues) arenas_wm.py: WrestleMania 2000's seven venues
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_wmimport.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_imports2.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_goldust.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_revimport.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_revmoves.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_ports.cpp
    ${CMAKE_SOURCE_DIR}/src/romimport/ri_photos.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_arenas.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_texpacks.cpp
    ${CMAKE_SOURCE_DIR}/src/main/lodepng/lodepng.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_wrestlers.cpp
    ${CMAKE_SOURCE_DIR}/src/main/commentary/arcade_commentary.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_media.cpp
    ${CMAKE_SOURCE_DIR}/src/main/arcade_media_decode.cpp
    ${CMAKE_SOURCE_DIR}/src/main/commentary/director.cpp
    ${CMAKE_SOURCE_DIR}/src/main/commentary/arcade_commentary_packs.cpp
    ${CMAKE_SOURCE_DIR}/src/main/commentary/arcade_commentary_build.cpp
    ${CMAKE_SOURCE_DIR}/src/main/crash_report.cpp
    ${CMAKE_SOURCE_DIR}/src/main/register_overlays.cpp
    ${REPAIRED_CPP}
    ${CMAKE_SOURCE_DIR}/src/game/recomp_api.cpp
    ${CMAKE_SOURCE_DIR}/src/game/raytracing.cpp
    ${CMAKE_SOURCE_DIR}/src/ra/ra_client.cpp
)
# [xplat 2026-11-12] What the port asks of the operating system (src/main/arcade_platform.h): one file per system.
if (WIN32)
    list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/main/arcade_platform_win32.cpp)
else()
    list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/main/arcade_platform_posix.cpp)
endif()
# [xplat 2026-11-12] The picture: arcade_view.cpp reads the game's lists; one backend file draws them (src/main/arcade_view_gpu.h).
# Windows: Direct3D 12. Linux (and the Steam Deck): Vulkan through plume, when plume and dxc are found; otherwise the stand-in
# that draws nothing (ARCADE_VIEW_BACKEND=none asks for it; =vulkan makes a missing plume or dxc an error instead of a fall-back).
set(ARCADE_VIEW_BACKEND "auto" CACHE STRING "Picture backend on Linux: auto, vulkan or none (Windows: vulkan adds it beside Direct3D 12)")
set(ARCADE_PLUME_DIR "${CMAKE_SOURCE_DIR}/third_party/plume" CACHE PATH "plume (RT64's render interface, MIT) with its contrib/ folders")
set(ARCADE_DXC "" CACHE FILEPATH "dxc (DirectX Shader Compiler) used to turn the HLSL in src/main/shaders into SPIR-V at build time")
set(ARCADE_VIEW_IS_VULKAN OFF)
# [vkwin 2026-11-12] Windows with -DARCADE_VIEW_BACKEND=vulkan: Direct3D 12 stays the picture, and the Vulkan backend is built in
# beside it, chosen at run time by WCW_NATIVE_GPU=vulkan, so the two can be compared picture by picture on one machine.
# Any other value on Windows (the default "auto"): Direct3D 12 alone, the build exactly as before.
if (WIN32)
    list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/main/arcade_view_d3d12.cpp)
endif()
if (NOT ARCADE_VIEW_BACKEND STREQUAL "none" AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR (WIN32 AND ARCADE_VIEW_BACKEND STREQUAL "vulkan")))     # (the Mac later: plume's Metal or MoltenVK, PLAN.md step 8)
    if (NOT ARCADE_DXC)
        find_program(ARCADE_DXC_FOUND dxc HINTS ENV DXC_PATH ENV VULKAN_SDK PATH_SUFFIXES bin)
        set(ARCADE_DXC "${ARCADE_DXC_FOUND}")
    endif()
    if (EXISTS "${ARCADE_PLUME_DIR}/plume_vulkan.cpp" AND ARCADE_DXC)
        set(ARCADE_VIEW_IS_VULKAN ON)
    elseif (ARCADE_VIEW_BACKEND STREQUAL "vulkan")
        message(FATAL_ERROR "ARCADE_VIEW_BACKEND=vulkan needs plume (ARCADE_PLUME_DIR, now '${ARCADE_PLUME_DIR}') and dxc (ARCADE_DXC, now '${ARCADE_DXC}')")
    else()
        message(WARNING "No plume (${ARCADE_PLUME_DIR}) or no dxc (${ARCADE_DXC}): this build has no picture (arcade_view_none.cpp). Set ARCADE_PLUME_DIR and ARCADE_DXC.")
    endif()
endif()
if (ARCADE_VIEW_IS_VULKAN)
    message(STATUS "Picture: Vulkan through plume (${ARCADE_PLUME_DIR}), shaders by ${ARCADE_DXC}")
    if (WIN32)
        # [vkwin 2026-11-12] Windows: plume's Vulkan file alone (its CMake would add its Direct3D 12 backend and D3D12MA too, which
        # nothing here uses). plume makes the Win32 surface from the game window's HWND itself, so no SDL Vulkan integration.
        add_library(plume STATIC EXCLUDE_FROM_ALL ${ARCADE_PLUME_DIR}/plume_vulkan.cpp)
        target_compile_features(plume PUBLIC cxx_std_17)
        target_compile_definitions(plume PUBLIC NOMINMAX)
        target_include_directories(plume PUBLIC ${ARCADE_PLUME_DIR} ${ARCADE_PLUME_DIR}/contrib/volk
            ${ARCADE_PLUME_DIR}/contrib/Vulkan-Headers/include ${ARCADE_PLUME_DIR}/contrib/VulkanMemoryAllocator/include)
    else()
        # plume, with SDL2's Vulkan surface (the window is the game's own SDL window)
        set(PLUME_SDL_VULKAN_ENABLED ON CACHE BOOL "" FORCE)
        add_subdirectory(${ARCADE_PLUME_DIR} ${CMAKE_BINARY_DIR}/plume EXCLUDE_FROM_ALL)
    endif()
    target_compile_options(plume PRIVATE -w)
    # The HLSL (the same text as Direct3D 12's, see src/main/shaders) to SPIR-V, then into headers the backend includes.
    set(ARCADE_SHADER_DIR ${CMAKE_SOURCE_DIR}/src/main/shaders)
    set(ARCADE_SPV_DIR ${CMAKE_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${ARCADE_SPV_DIR})
    set(ARCADE_SPV_HEADERS "")
    # name  source  entry  profile  (-fvk-invert-y only for vertex shaders: Vulkan's clip space has y down, the picture must come out the same way up)
    foreach (S "scene_vs|arcade_view_scene.vk.hlsl|vs|vs_6_0" "scene_ps|arcade_view_scene.vk.hlsl|ps|ps_6_0" "scene_screen|arcade_view_scene.vk.hlsl|ps_screen|ps_6_0"
               "scene_shape|arcade_view_scene.vk.hlsl|ps_shape|ps_6_0" "post_vs|arcade_view_post.vk.hlsl|vs|vs_6_0" "post_ps|arcade_view_post.vk.hlsl|ps|ps_6_0"
               "post_blit|arcade_view_post.vk.hlsl|ps_blit|ps_6_0" "post_resolve|arcade_view_resolve.vk.hlsl|ps_resolve|ps_6_0")     # [vkwin 2026-11-12] (+ the resolve)
        string(REPLACE "|" ";" S ${S})
        list(GET S 0 N) 
        list(GET S 1 SRC)
        list(GET S 2 ENTRY)
        list(GET S 3 PROFILE)
        set(FLAGS -spirv -fspv-target-env=vulkan1.1 -T ${PROFILE} -E ${ENTRY})
        if (PROFILE MATCHES "^vs")
            list(APPEND FLAGS -fvk-invert-y)
        endif()
        add_custom_command(OUTPUT ${ARCADE_SPV_DIR}/${N}.spv COMMAND ${ARCADE_DXC} ${FLAGS} ${ARCADE_SHADER_DIR}/${SRC} -Fo ${ARCADE_SPV_DIR}/${N}.spv
                           DEPENDS ${ARCADE_SHADER_DIR}/${SRC} COMMENT "dxc: ${SRC} ${ENTRY}")
        add_custom_command(OUTPUT ${ARCADE_SPV_DIR}/${N}.spv.h COMMAND ${CMAKE_COMMAND} -DIN=${ARCADE_SPV_DIR}/${N}.spv -DOUT=${ARCADE_SPV_DIR}/${N}.spv.h -DNAME=spv_${N}
                           -P ${CMAKE_SOURCE_DIR}/tools/spv_to_header.cmake DEPENDS ${ARCADE_SPV_DIR}/${N}.spv)
        list(APPEND ARCADE_SPV_HEADERS ${ARCADE_SPV_DIR}/${N}.spv.h)
    endforeach()
    add_custom_target(arcade_shaders DEPENDS ${ARCADE_SPV_HEADERS})
    list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/main/arcade_view_vulkan.cpp)
elseif (NOT WIN32)
    list(APPEND SOURCES ${CMAKE_SOURCE_DIR}/src/main/arcade_view_none.cpp)
endif()

# RetroAchievements prototype: vendored rcheevos (see src/ra/rcheevos/VENDORED.txt)
# compiled into the game exe. Softcore-only client, env-gated at runtime (WCW_RA=1).
file(GLOB RCHEEVOS_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src/*.c
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src/rcheevos/*.c
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src/rapi/*.c
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src/rhash/*.c
)
list(APPEND SOURCES ${RCHEEVOS_SOURCES})
target_include_directories(NoMercyRecompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/include
    ${CMAKE_SOURCE_DIR}/src/ra/rcheevos/src
)
target_compile_definitions(NoMercyRecompiled PRIVATE
    RC_DISABLE_LUA
    RC_CLIENT_SUPPORTS_HASH
)
if (WIN32)
    target_link_libraries(NoMercyRecompiled PRIVATE winhttp.lib)
endif()
target_include_directories(NoMercyRecompiled PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${NMR}/N64Recomp/include
    ${NMR}/thirdparty/sse2neon
    ${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompinput/include
    ${CMAKE_CURRENT_BINARY_DIR}
)
if (WIN32)
    target_include_directories(NoMercyRecompiled PRIVATE ${sdl2_SOURCE_DIR}/include)
    target_link_directories(NoMercyRecompiled PRIVATE ${sdl2_SOURCE_DIR}/lib/x64)
    target_link_libraries(NoMercyRecompiled PRIVATE SDL2 Winmm.lib d3d12 dxgi d3dcompiler_47 shell32 ole32)
    if (MINGW)
        # (MSVC links these from main.cpp's #pragma comment(lib))
        target_link_libraries(NoMercyRecompiled PRIVATE dbghelp -l:libwinpthread.a)
    endif()
    # Release is a windowed app (no console flash on double-click); stdio goes to a log
    # file in the app folder unless launched with --show-console or redirected handles
    # (see wcw_setup_logging in src/main/main.cpp). Debug keeps the console.
    # Always emit a linker map: release crash backtraces print RVAs that are resolved
    # against this file (see cycle.ps1 / BUILDING.md). Previously this came from an
    # uncached local configure flag, which fresh clones and CI would silently lose.
    # Bump the default thread stack reserve: ultramodern's gfx_thread (a std::thread, which
    # uses this PE default) reaches D3D12CreateDevice deep in its call stack, and that call
    # is very stack-hungry — on the default 1 MB it overflows (0xC0000409). 16 MB is safe.
    if (MINGW)
        # [wcw mingw] The same with the GNU-style driver (llvm-mingw).
        set_target_properties(NoMercyRecompiled PROPERTIES
            LINK_FLAGS_DEBUG "-mconsole"
            LINK_FLAGS_RELEASE "-mwindows")
        target_link_options(NoMercyRecompiled PRIVATE -Wl,-Map=NoMercyRecompiled.map -Wl,--stack,16777216)
        # Privacy (2026-11-13): no debug sections in the exe. Ours have none (Release), but the toolchain's prebuilt C runtime
        # brings some, with its own build machine's folders in them. Only debug sections go: the code, the symbols and the
        # map (crash backtraces) stay as they were.
        target_link_options(NoMercyRecompiled PRIVATE -Wl,--strip-debug)
        # Reproducible builds (2026-11-17, the user's decision): no link time in the PE header (lld writes 0 instead), so the
        # same source builds the same exe byte for byte, wherever and whenever it is built.
        target_link_options(NoMercyRecompiled PRIVATE -Wl,--no-insert-timestamp)
    else()
        set_target_properties(NoMercyRecompiled PROPERTIES
            LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
            LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
        if (MSVC)
            target_link_options(NoMercyRecompiled PRIVATE /OPT:NOICF)
        endif()
        target_link_options(NoMercyRecompiled PRIVATE /MAP:NoMercyRecompiled.map)
        target_link_options(NoMercyRecompiled PRIVATE /STACK:16777216)
        target_link_options(NoMercyRecompiled PRIVATE /Brepro)   # (reproducible builds: no link time stamp)
    endif()
    # Copy SDL2 next to the exe.
    add_custom_command(TARGET NoMercyRecompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll" $<TARGET_FILE_DIR:NoMercyRecompiled>)
    if (ARCADE_VIEW_IS_VULKAN)
        # [vkwin 2026-11-12] The Vulkan backend beside Direct3D 12 (WCW_NATIVE_GPU=vulkan picks it; arcade_view.cpp). Not
        # ARCADE_VIEW_VULKAN: that one makes main.cpp ask SDL for a Vulkan window, which Windows doesn't need (plume takes the HWND).
        target_link_libraries(NoMercyRecompiled PRIVATE plume)
        add_dependencies(NoMercyRecompiled arcade_shaders)
        target_include_directories(NoMercyRecompiled PRIVATE ${ARCADE_SPV_DIR})
        target_compile_definitions(NoMercyRecompiled PRIVATE ARCADE_VIEW_VULKAN_TOO=1)
    endif()
else()
    # [xplat 2026-11-12] The same SDL2 found above; threads and dlopen for the runtime (its live recompiler, mods).
    target_include_directories(NoMercyRecompiled PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(NoMercyRecompiled PRIVATE ${SDL2_LIBRARIES} pthread ${CMAKE_DL_LIBS})
    if (ARCADE_VIEW_IS_VULKAN)
        # [xplat 2026-11-12] The Vulkan picture: plume (it finds the Vulkan loader itself, through volk, at run time), the shader headers
        # made above, and a flag so main.cpp makes the game's window a Vulkan-capable one.
        target_link_libraries(NoMercyRecompiled PRIVATE plume)
        add_dependencies(NoMercyRecompiled arcade_shaders)
        target_include_directories(NoMercyRecompiled PRIVATE ${ARCADE_SPV_DIR})
        target_compile_definitions(NoMercyRecompiled PRIVATE ARCADE_VIEW_VULKAN=1)
    endif()
endif()

# --- [arcade] FFmpeg (2026-11-10): Titantron videos, and their sound, from common files (src/main/arcade_media_decode.cpp) ---
# Our own build of it, from source kept in the project: tools/ffmpeg_build/build_ffmpeg.ps1 makes third_party/ffmpeg (include/, lib/,
# bin/ with the DLLs, LICENSE.txt, BUILD.txt with the configure line) from third_party/ffmpeg-src. LGPL 2.1 or later,
# shared: linked as DLLs, never statically, which is what the LGPL asks of a program that isn't LGPL itself; the licence
# and BUILD.txt go beside the exe with them. Set ARCADE_FFMPEG_DIR to use another build (a Linux or Mac one later: there
# the system's FFmpeg can be found with pkg-config instead).
set(ARCADE_FFMPEG_DIR "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "An LGPL shared FFmpeg build: include/, lib/, bin/")
if (NOT EXISTS "${ARCADE_FFMPEG_DIR}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "No FFmpeg in ${ARCADE_FFMPEG_DIR}: build it with tools/ffmpeg_build/build_ffmpeg.ps1 (NOTES.md), or set ARCADE_FFMPEG_DIR.")
endif()
target_include_directories(NoMercyRecompiled PRIVATE "${ARCADE_FFMPEG_DIR}/include")
target_link_directories(NoMercyRecompiled PRIVATE "${ARCADE_FFMPEG_DIR}/lib")
# (by name: -lavformat finds libavformat.dll.a with the GNU-style driver, avformat.lib with clang-cl / MSVC)
# [xplat 2026-11-12] (D3) Linux / the Steam Deck / the Mac: the game must start without FFmpeg too, as on Windows. The linker can't delay-load
# there, so arcade_ffmpeg_lazy.cpp does it by hand: the program is NOT linked to the libraries (no DT_NEEDED), it opens them from lib/
# beside the game on the first video (src/main/arcade_ffmpeg_lazy.h). ARCADE_FFMPEG_LAZY=OFF links them the ordinary way instead.
option(ARCADE_FFMPEG_LAZY "Linux/Mac: load FFmpeg's libraries on first use, so the game starts without them" ON)
if (NOT WIN32 AND ARCADE_FFMPEG_LAZY)
    target_sources(NoMercyRecompiled PRIVATE ${CMAKE_SOURCE_DIR}/src/main/arcade_ffmpeg_lazy.cpp)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/main/arcade_media_decode.cpp ${CMAKE_SOURCE_DIR}/src/main/arcade_ffmpeg_lazy.cpp PROPERTIES COMPILE_DEFINITIONS ARCADE_FFMPEG_LAZY=1)
    target_link_libraries(NoMercyRecompiled PRIVATE ${CMAKE_DL_LIBS})
else()
    target_link_libraries(NoMercyRecompiled PRIVATE avformat avcodec swscale swresample avutil)
endif()
if (NOT WIN32)
    # Libraries are found beside the program in lib/ (SDL, FFmpeg): an old-style RPATH, which the libraries inherit, so they find each other there too.
    if (NOT APPLE)
        # (BUILD_WITH_INSTALL_RPATH and no link path: CMake would add the build machine's own library folders to it, a path that means nothing to a player)
        # [deck 2026-11-13] Through CMake's RPATH properties, not a raw "-Wl,-rpath,$ORIGIN/lib": CMake 3.25 (the sniper SDK's) quotes that one
        # wrongly for the shell, and the program got an RPATH of "$/lib". The properties come out right in every CMake version.
        set_target_properties(NoMercyRecompiled PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "\$ORIGIN/lib" INSTALL_RPATH_USE_LINK_PATH OFF)
        target_link_options(NoMercyRecompiled PRIVATE "-Wl,--disable-new-dtags")
        # Privacy (2026-11-13): no debug sections (the system libraries' own build folders), as on Windows.
        target_link_options(NoMercyRecompiled PRIVATE "-Wl,--strip-debug")
    endif()
    # So a build can be run from its own folder: FFmpeg's libraries (with their version links) go to <build>/lib/. (build_linux.sh does the same for the tarball.)
    add_custom_command(TARGET NoMercyRecompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NoMercyRecompiled>/lib"
        COMMAND sh -c "cp -a '${ARCADE_FFMPEG_DIR}'/lib/lib*.so* '$<TARGET_FILE_DIR:NoMercyRecompiled>/lib/' 2>/dev/null || true"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ARCADE_FFMPEG_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:NoMercyRecompiled>/FFmpeg-LICENSE.txt"
        VERBATIM)
endif()
if (WIN32)
    file(GLOB ARCADE_FFMPEG_DLLS "${ARCADE_FFMPEG_DIR}/bin/*.dll")
    add_custom_command(TARGET NoMercyRecompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ARCADE_FFMPEG_DLLS} $<TARGET_FILE_DIR:NoMercyRecompiled>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ARCADE_FFMPEG_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:NoMercyRecompiled>/FFmpeg-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ARCADE_FFMPEG_DIR}/BUILD.txt" "$<TARGET_FILE_DIR:NoMercyRecompiled>/FFmpeg-BUILD.txt")
    if (EXISTS "${ARCADE_FFMPEG_DIR}/dav1d-LICENSE.txt")       # (the AV1 decoder inside avcodec's DLL: its BSD licence asks for its notice to go along)
        add_custom_command(TARGET NoMercyRecompiled POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ARCADE_FFMPEG_DIR}/dav1d-LICENSE.txt" "$<TARGET_FILE_DIR:NoMercyRecompiled>/FFmpeg-dav1d-LICENSE.txt")
    endif()
    # The game must start without these DLLs (2026-11-10): each is delay-loaded, so Windows only looks for it at the first
    # call into it, and arcade_media_decode.cpp (ffmpeg_ready) looks for them itself before making that call. Missing:
    # logged once, and every entrance plays the old way.
    foreach (ARCADE_FFMPEG_DLL IN LISTS ARCADE_FFMPEG_DLLS)
        get_filename_component(ARCADE_FFMPEG_DLL_NAME "${ARCADE_FFMPEG_DLL}" NAME)
        if (MINGW)
            target_link_options(NoMercyRecompiled PRIVATE "-Wl,--delayload=${ARCADE_FFMPEG_DLL_NAME}")
        else()
            target_link_options(NoMercyRecompiled PRIVATE "/DELAYLOAD:${ARCADE_FFMPEG_DLL_NAME}")
        endif()
    endforeach()
    # (the small routine that loads a delay-loaded DLL: libdelayimp.a with llvm-mingw, delayimp.lib with MSVC)
    target_link_libraries(NoMercyRecompiled PRIVATE delayimp)
endif()
# The single-file sound decoders (dr_mp3, dr_flac, dr_wav: src/main/third_party) are big headers full of warnings of
# their own, compiled into this one file.
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/main/arcade_media_decode.cpp PROPERTIES COMPILE_OPTIONS "-Wno-everything")

# Copy the runtime UI assets and SDL controller db next to the exe so a fresh build runs
# from the build dir (recompui loads ./assets relative to the program dir).
add_custom_command(TARGET NoMercyRecompiled POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:NoMercyRecompiled>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/recompcontrollerdb.txt" "$<TARGET_FILE_DIR:NoMercyRecompiled>")

# PatchesLib MUST come before RecompiledFuncs: patched functions share their C symbol
# name with the base recompiled copies, and the patch takes effect by winning static-lib
# symbol resolution order (mirrors BMHero).
target_link_libraries(NoMercyRecompiled PRIVATE
    RecompiledFuncs
    recompinput
    librecomp
    ultramodern
)
target_sources(NoMercyRecompiled PRIVATE ${SOURCES})

# [commentary2 2026-11-18] The commentary pack builder (src/main/commentary/packbuild: the pack made from the player's own
# WWE '13 disc image on the first start that finds it) and its libraries, each with its licence beside it:
#   c2_vorbis   libogg 1.3.5 + libvorbis 1.3.7 (Xiph, BSD), forced to Linux/glibc behaviour by vorbis_compat (glibc's
#               rint, a stable qsort, CORE-MATH's correctly rounded exp/log/pow/atan/sin/cos): the pack's clips must come
#               out byte for byte as the developer's tools made them on Linux (third_party/vorbis_compat/c2_vorbis_compat.h)
#   c2_coremath CORE-MATH (MIT) + musl's software fma (MIT): exact on every x86-64 CPU, with or without FMA instructions
#   c2_zstd     zstd 1.5.6's decoder (BSD): RVZ disc images
#   c2_wwise    vgmstream's Wwise Vorbis code (ISC): WWE '13's lines
# All without floating-point contraction (-ffp-contract=off): the reference tools multiply and add separately.
set(C2_TP ${CMAKE_SOURCE_DIR}/third_party)
set(C2_PB ${CMAKE_SOURCE_DIR}/src/main/commentary/packbuild)
add_library(c2_coremath STATIC
    ${C2_TP}/coremath/exp/exp.c ${C2_TP}/coremath/log/log.c ${C2_TP}/coremath/pow/pow.c
    ${C2_TP}/coremath/atan/atan.c ${C2_TP}/coremath/sin/sin.c ${C2_TP}/coremath/cos/cos.c ${C2_TP}/coremath/c2_fma.c)
target_compile_options(c2_coremath PRIVATE -O2 -w -ffp-contract=off "SHELL:-include ${C2_TP}/coremath/c2_fma.h")
target_compile_definitions(c2_coremath PRIVATE __builtin_fma=c2_fma __builtin_roundeven=c2_roundeven)
file(GLOB C2_VORBIS_SOURCES ${C2_TP}/libvorbis/lib/*.c)
add_library(c2_vorbis STATIC ${C2_TP}/libogg/src/bitwise.c ${C2_TP}/libogg/src/framing.c ${C2_VORBIS_SOURCES}
    ${C2_TP}/vorbis_compat/c2_qsort_stable.c)
target_include_directories(c2_vorbis PUBLIC ${C2_TP}/libogg/include ${C2_TP}/libvorbis/include PRIVATE ${C2_TP}/libvorbis/lib)
target_compile_options(c2_vorbis PRIVATE -O2 -w -ffp-contract=off "SHELL:-include ${C2_TP}/vorbis_compat/c2_vorbis_compat.h")
target_link_libraries(c2_vorbis PRIVATE c2_coremath)
add_library(c2_zstd STATIC ${C2_TP}/zstd/zstddeclib.c)
target_include_directories(c2_zstd PUBLIC ${C2_TP}/zstd)
target_compile_options(c2_zstd PRIVATE -O2 -w)
add_library(c2_wwise STATIC ${C2_PB}/vgmstream/coding/vorbis_custom_utils_wwise.c ${C2_PB}/vgmstream/coding/vorbis_custom_utils_min.c
    ${C2_PB}/vgmstream/util/vorbis_codebooks.c)
target_compile_options(c2_wwise PRIVATE -O2 -w -ffp-contract=off)
target_link_libraries(c2_wwise PUBLIC c2_vorbis)
set(C2_PB_SOURCES ${C2_PB}/comm_builder.cpp ${C2_PB}/comm_pack.cpp ${C2_PB}/comm_encode.cpp ${C2_PB}/wwise_decode.cpp ${C2_PB}/wii_disc.cpp
    ${C2_PB}/aes128.cpp ${C2_PB}/comm_moves.cpp)
target_sources(NoMercyRecompiled PRIVATE ${C2_PB_SOURCES})
set_source_files_properties(${C2_PB_SOURCES} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
target_link_libraries(NoMercyRecompiled PRIVATE c2_wwise c2_vorbis c2_coremath c2_zstd)

# [standalone] The game alone: no launcher (the ROM is found in the game's folder), no config menu, no quit prompt.
# The program is "Ultimate No Mercy.exe" (Linux: "Ultimate No Mercy").
option(NOMERCY_STANDALONE "Build the standalone arcade program (no launcher, menus or prompts)" ON)
if (NOMERCY_STANDALONE)
    target_compile_definitions(NoMercyRecompiled PRIVATE NOMERCY_STANDALONE=1)
    set_target_properties(NoMercyRecompiled PROPERTIES OUTPUT_NAME "Ultimate No Mercy")
endif()
set_property(TARGET NoMercyRecompiled PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The sound chip's translated program: out of the build (src/main/arcade_audio.cpp does its work); on only to prove that again.
# Game functions converted to C++ (tools/lift/lift.py). They overflow as the N64 did, which C++ only promises with -fwrapv.
# (2026-11-13, the Linux proof) LIFT_BUILT_WITH_FWRAPV says these flags are on: lifted.cpp stops with #error without it, so a
# build (another platform's, another compiler's) that leaves them out can't make the converted functions quietly behave otherwise.
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/game/lifted.cpp PROPERTIES COMPILE_OPTIONS "-fwrapv;-fno-strict-aliasing;-ffp-contract=off;-DLIFT_BUILT_WITH_FWRAPV;-Wno-everything")
# (2026-11-16, honesty) src/game/lifted_changed.inc -- the converted functions the mod changes or adds, written by tools/lift/lift.py
# apart from the game's own -- is NOT a source of its own: lifted.cpp includes it at its end, so it is compiled in lifted.cpp's
# translation unit with exactly the options above. Compiled apart, the machine code of 382 converted functions changed (data
# reached through mingw's .refptr, inlining across the two files lost); in one unit it is the same as the one file's
# (scratch/agents/honesty logs/split_machine_code.txt). Ninja follows the #include, so a change to it rebuilds lifted.cpp.
# (2026-11-13, final082) The start-up canaries for lifted.cpp's flags: compiled with exactly its options, so a build that lost
# one says so in the log at start-up (lift_canary.cpp; main.cpp calls it).
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/game/lift_canary.cpp PROPERTIES COMPILE_OPTIONS "-fwrapv;-fno-strict-aliasing;-ffp-contract=off;-DLIFT_BUILT_WITH_FWRAPV;-Wno-everything")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/game)
# The mod translated ahead of time (Stage 1 step 1, 2026-11-11): tools/aot/aot.py writes src/game/mod_aot/*.c from the built
# mod, with N64Recomp's C generator -- the same kind of C as RecompiledFuncs, so the same options (no warnings; C, not C++).
# The mod loader uses it only for the mod build it was made from (librecomp/aot_mod.h), and translates any other live. With
# no files there (ARCADE_MOD_AOT off, or the tool not run) the game is as before: the loader finds no table.
# (A glob: the tool decides how many files; build_exe.ps1 runs the tool and configures on every build, so new ones are picked up.)
option(ARCADE_MOD_AOT "link the mod's ahead-of-time translation (src/game/mod_aot) into the game" ON)
if (ARCADE_MOD_AOT)
    file(GLOB MOD_AOT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/game/mod_aot/*.c)
    if (MOD_AOT_SOURCES)
        target_sources(${PROJECT_NAME} PRIVATE ${MOD_AOT_SOURCES})
        set_source_files_properties(${MOD_AOT_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-everything;-ffp-contract=off")
        target_include_directories(${PROJECT_NAME} PRIVATE ${NMR}/librecomp/include)
        list(LENGTH MOD_AOT_SOURCES MOD_AOT_COUNT)
        message(STATUS "The mod's ahead-of-time translation: ${MOD_AOT_COUNT} files linked in")
    else()
        message(STATUS "The mod's ahead-of-time translation: none (run tools/aot/aot.py); the mod is translated as it loads")
    endif()
endif()
# The native mod (2026-11-14, modnative7): the mod's own functions proved native by the random tester against Stage 1's
# translation, src/game/mod_native.cpp (tools/aot/mod_native.py release), linked in place of that translation under its names.
# aot.py says whether this Stage 1 translation is the mod build they were proved against (mod_aot/mod_native_on.txt); if not,
# or with ARCADE_MOD_NATIVE off, the game is Stage 1 alone. At run time NM_NATIVE_OFF=all (or a list of the mod's files)
# falls back to the translation too. Compiled with the tester's semantics: overflow wraps (-fwrapv), no strict aliasing,
# no fused float operations, char signed as on the N64 (-fsigned-char: ARM's default is unsigned) -- the file stops with
# #error without NM_NATIVE_BUILT_WITH_FWRAPV, as lifted.cpp does.
option(ARCADE_MOD_NATIVE "link the mod's functions proved native (src/game/mod_native.cpp) in place of their translation" ON)
option(ARCADE_MOD_NATIVE_WHOLE "also link the gameplay tier: the mod's other native functions, not proved (src/game/mod_native_whole.cpp)" OFF)
# (The proof ran where long is 32 bits, as on the N64 and on Windows; the native text still has the mod's `long` / `size_t`
#  in places, which on Linux / Mac / the Deck are 64 bits -- other overflow, and one import's stub won't compile. Until native.py
#  writes them as 32-bit types, the native mod is linked only where long is 4 bytes: elsewhere the game is Stage 1 alone.)
include(CheckTypeSize)
check_type_size("long" NM_SIZEOF_LONG LANGUAGE CXX)
if (NOT NM_SIZEOF_LONG EQUAL 4)
    set(ARCADE_MOD_NATIVE OFF)
endif()
if (ARCADE_MOD_AOT AND ARCADE_MOD_NATIVE AND MOD_AOT_SOURCES AND EXISTS ${CMAKE_SOURCE_DIR}/src/game/mod_aot/mod_native_on.txt
        AND EXISTS ${CMAKE_SOURCE_DIR}/src/game/mod_native.cpp)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/game/mod_native.cpp)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/game/mod_native.cpp PROPERTIES COMPILE_OPTIONS
        "-std=c++20;-fwrapv;-fno-strict-aliasing;-ffp-contract=off;-fsigned-char;-DNM_NATIVE_BUILT_WITH_FWRAPV;-Wno-everything")
    set_property(SOURCE ${MOD_AOT_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS ARCADE_MOD_NATIVE)
    file(READ ${CMAKE_SOURCE_DIR}/src/game/mod_aot/mod_native_on.txt MOD_NATIVE_ON)
    string(STRIP "${MOD_NATIVE_ON}" MOD_NATIVE_ON)
    message(STATUS "The native mod: linked (${MOD_NATIVE_ON})")
    # (2026-11-14, modwhole: the user's "B now, C later") The GAMEPLAY TIER: the mod's other functions, compiled natively from its C
    #  the same way but not proved by the random tester (src/game/mod_native_whole.cpp, mod_native.py release --whole) -- a build
    #  option, off by default, for the gameplay A/B runs (tools/modnative/whole_ab.py) that verify them. The same compile options;
    #  linked only when aot.py found the same mod build (mod_aot/mod_native_whole_on.txt). NM_NATIVE_OFF=gameplay turns it off at
    #  run time, as =all does both tiers.
    if (ARCADE_MOD_NATIVE_WHOLE AND EXISTS ${CMAKE_SOURCE_DIR}/src/game/mod_aot/mod_native_whole_on.txt
            AND EXISTS ${CMAKE_SOURCE_DIR}/src/game/mod_native_whole.cpp)
        target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/game/mod_native_whole.cpp)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/src/game/mod_native_whole.cpp PROPERTIES COMPILE_OPTIONS
            "-std=c++20;-fwrapv;-fno-strict-aliasing;-ffp-contract=off;-fsigned-char;-DNM_NATIVE_BUILT_WITH_FWRAPV;-Wno-everything")
        set_property(SOURCE ${MOD_AOT_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS ARCADE_MOD_NATIVE_WHOLE)
        file(READ ${CMAKE_SOURCE_DIR}/src/game/mod_aot/mod_native_whole_on.txt MOD_NATIVE_WHOLE_ON)
        string(STRIP "${MOD_NATIVE_WHOLE_ON}" MOD_NATIVE_WHOLE_ON)
        message(STATUS "The native mod's gameplay tier (not proved): linked (${MOD_NATIVE_WHOLE_ON})")
    endif()
else()
    message(STATUS "The native mod: not linked (ARCADE_MOD_NATIVE off, long not 32 bits here, or Stage 1 is not the mod build it was proved against)")
endif()

# The proof build: every candidate runs beside its original and is compared (src/game/lift_twin.cpp).
option(ARCADE_LIFT_TWIN "converted functions run beside the originals, compared" OFF)
if (ARCADE_LIFT_TWIN)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/game/lift_twin.cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARCADE_LIFT_TWIN)
endif()

option(ARCADE_AUDIO_TWIN "arcade_audio.cpp beside the translated sound program, compared" OFF)
if (ARCADE_AUDIO_TWIN)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/rsp/nomercy_audio.cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ARCADE_AUDIO_TWIN)
endif()

# librecomp's RSP vector-unit header (rsp_vu_impl.hpp, pulled in by rsp/nomercy_audio.cpp)
# uses SSSE3/SSE4.1 intrinsics; -march=nehalem enables them (mirrors BMHero).
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(NoMercyRecompiled PRIVATE
        -march=nehalem
        -fno-strict-aliasing
        -fms-extensions
    )
else()
    target_compile_options(NoMercyRecompiled PRIVATE
        -fno-strict-aliasing
        -fms-extensions
    )
endif()

# (phase3 2026-11-19) the ROM importer mirrors Python / the N64 float for float: no contraction (its files also say so by pragma)
file(GLOB RI_SOURCES ${CMAKE_SOURCE_DIR}/src/romimport/*.cpp)
set_source_files_properties(${RI_SOURCES} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
