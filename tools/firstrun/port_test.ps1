# port_test.ps1: builds and runs tools/firstrun/port_test.cpp -- the first start's walls and ringstage ports
# (exe src/main/arcade_firstrun_ports.cpp; its computation src/romimport/ri_collide.cpp, its labels port
# arcade_firstrun_labels.cpp) against the mod's build, table by table (firstrun, 2026-11-17).
#   powershell -File tools/firstrun/port_test.ps1        (from anywhere: paths are the worktree's own)
# Needs the mod built (mod/build/mod_six/mod.elf, mod_binary.bin; mod/build/wmport.bin) and the ROM in roms/.
# The port is compiled on its own with the exe's flags (CMakeLists.txt, Release: -O3, -march=nehalem, ...), then linked
# with the test. Outputs go to mod/build/port_test/.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cxx = Join-Path $root 'toolchain\llvm-mingw\bin\clang++.exe'
$py = Join-Path $root 'toolchain\python\python.exe'
$main = Join-Path $root 'exe\WWFNoMercyRecomp\src\main'
$out = Join-Path $root 'mod\build\port_test'
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $root
try {
    & $py tools\firstrun\port_syms.py mod\build\mod_six\mod.elf mod\build\port_test\port_syms.txt
    if ($LASTEXITCODE) { throw 'port_syms.py failed' }
    # the port as the exe compiles it
    & $cxx -std=c++20 -O3 -DNDEBUG -march=nehalem -fno-strict-aliasing -fms-extensions -include intrin.h -DNOMINMAX `
        -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR -c exe\WWFNoMercyRecomp\src\main\arcade_firstrun_ports.cpp -o mod\build\port_test\ports.o
    if ($LASTEXITCODE) { throw 'compiling arcade_firstrun_ports.cpp failed' }
    # (run_port's labels port: arcade_firstrun_labels.cpp, the same flags -- without it the link fails)
    & $cxx -std=c++20 -O3 -DNDEBUG -march=nehalem -fno-strict-aliasing -fms-extensions -include intrin.h -DNOMINMAX `
        -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR -c exe\WWFNoMercyRecomp\src\main\arcade_firstrun_labels.cpp -o mod\build\port_test\labels.o
    if ($LASTEXITCODE) { throw 'compiling arcade_firstrun_labels.cpp failed' }
    # (its computation, shared with the ROM importer: src/romimport/ri_collide.cpp, compiled the same way)
    & $cxx -std=c++20 -O3 -DNDEBUG -march=nehalem -fno-strict-aliasing -fms-extensions -include intrin.h -DNOMINMAX `
        -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR -c exe\WWFNoMercyRecomp\src\romimport\ri_collide.cpp -o mod\build\port_test\collide.o
    if ($LASTEXITCODE) { throw 'compiling ri_collide.cpp failed' }
    & $cxx -std=c++20 -O2 "-I$main" -c tools\firstrun\port_test.cpp -o mod\build\port_test\port_test.o
    if ($LASTEXITCODE) { throw 'compiling port_test.cpp failed' }
    & $cxx mod\build\port_test\port_test.o mod\build\port_test\ports.o mod\build\port_test\labels.o mod\build\port_test\collide.o -o mod\build\port_test\port_test.exe
    if ($LASTEXITCODE) { throw 'linking failed' }
    & mod\build\port_test\port_test.exe . mod\build\port_test\port_syms.txt
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
