# [xplat 2026-11-12] Turns a compiled SPIR-V file into a C++ header holding it as an array of bytes, for the Vulkan picture backend
# (arcade_view_vulkan.cpp). Run by CMake at build time:   cmake -DIN=<x.spv> -DOUT=<x.h> -DNAME=<array_name> -P spv_to_header.cmake
# (No outside tool is needed: CMake reads the file as hex and writes it out as 0x.. bytes.)
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR size "${hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}" "// [xplat 2026-11-12] made by tools/spv_to_header.cmake from ${NAME}: do not edit (CMake makes it again when the shader changes)\n#pragma once\n#include <cstddef>\nstatic const unsigned char ${NAME}[] = {${bytes}};\nstatic const size_t ${NAME}_size = ${size};\n")
