# llvm-mingw on Windows itself (toolchain.cmake was the same for cross-building on Linux). LLVM_MINGW: its folder
# (build_exe.ps1 sets it: the one on PATH). No case shims: Windows finds <Windows.h> and Winmm.lib by any case.
set(LM $ENV{LLVM_MINGW})
set(CMAKE_C_COMPILER ${LM}/bin/x86_64-w64-mingw32-clang.exe)
set(CMAKE_CXX_COMPILER ${LM}/bin/x86_64-w64-mingw32-clang++.exe)
# the assembler named as the very same string as the C compiler: zstd compares the two as strings and, if they
# differ (even only in the case of a letter), builds its .S file as C, which fails
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
set(CMAKE_RC_COMPILER ${LM}/bin/x86_64-w64-mingw32-windres.exe)
set(CMAKE_AR ${LM}/bin/llvm-ar.exe)
set(CMAKE_RANLIB ${LM}/bin/llvm-ranlib.exe)
set(DXH $ENV{NM_MINGW_DEPS}/DirectX-Headers/include)
set(MINGW_EXTRA "-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -I${DXH}/directx -I${DXH}/dxguids -I${DXH}")
set(CMAKE_C_FLAGS_INIT "${MINGW_EXTRA}")
set(CMAKE_CXX_FLAGS_INIT "${MINGW_EXTRA}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
