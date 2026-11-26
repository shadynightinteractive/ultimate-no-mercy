# Builds "Ultimate No Mercy.exe" (WWF No Mercy: Recompiled, standalone) with llvm-mingw (toolchain\llvm-mingw).
# The port's source is exe\WWFNoMercyRecomp. RT64 and recompui are out of it (backups\unused_libs).
#   .\build_exe.ps1     -> exe\build\Ultimate No Mercy.exe   (then .\deploy.ps1 -Exe puts it in the game)
$ErrorActionPreference = 'Continue'
. "$PSScriptRoot\toolchain\env.ps1"
$M = "$PSScriptRoot\toolchain\mingw"
$port = "$PSScriptRoot\exe\WWFNoMercyRecomp"
$B = "$PSScriptRoot\exe\build"
$env:LLVM_MINGW = "$PSScriptRoot\toolchain\llvm-mingw" -replace '\\', '/'
$env:NM_MINGW_DEPS = $M -replace '\\', '/'
$tc = "$PSScriptRoot\exe\patches\toolchain-win.cmake"

# The mods translated ahead of time (tools\aot: src\game\mod_aot), from the mods as they are built now. The exe's mod loader
# uses a mod's linked translation only for the very .nrm it was made from, so the mod is built first (build_mod.ps1), then
# this. (2026-11-11)
# (newhome 2026-11-17: aot.py's tool links N64Recomp's libraries from this build folder. A fresh build folder has none yet: it is
#  configured and only the N64Recomp target built first -- then aot.py, then the configure and build below as always. With the
#  libraries there, nothing changes.)
if (-not (Test-Path "$B\lib\N64ModernRuntime\librecomp\N64Recomp\libN64Recomp.a")) {
    $sdl0 = Join-Path $port 'third_party/sdl2-src'
    # (-D and its value as two arguments: Windows PowerShell splits a native argument that holds a drive path at its colon)
    $sdlArg0 = if (Test-Path "$sdl0/include") { @('-D', "FETCHCONTENT_SOURCE_DIR_SDL2=$($sdl0.Replace([char]92, [char]47))") } else { @() }
    cmake -S $port -B $B -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$tc" -DCMAKE_BUILD_TYPE=Release @sdlArg0
    if ($LASTEXITCODE) { throw "configure (for N64Recomp's libraries) failed" }
    ninja -C $B N64Recomp
    if ($LASTEXITCODE) { throw "N64Recomp's libraries did not build" }
}
python "$PSScriptRoot\tools\aot\aot.py"
if ($LASTEXITCODE) { throw "tools\aot\aot.py failed (the mods' ahead-of-time translation)" }
# (2026-11-14, modnative7: the native mod -- the mod's functions proved native, src\game\mod_native.cpp -- is linked when aot.py
#  found the mod as built to be the one they were proved against; $env:ARCADE_MOD_NATIVE = '0' builds without it)
$native = if ($env:ARCADE_MOD_NATIVE -eq '0') { 'OFF' } else { 'ON' }
# (2026-11-14, modwhole: $env:ARCADE_MOD_NATIVE_WHOLE = '1' also links the gameplay tier, src\game\mod_native_whole.cpp -- the
#  mod's other native functions, NOT proved: for the gameplay A/B runs. Off by default.)
$whole = if ($env:ARCADE_MOD_NATIVE_WHOLE -eq '1') { 'ON' } else { 'OFF' }
# (newhome 2026-11-17: SDL2's development files from the project's own copy, third_party\sdl2-src -- linked from the private
#  folder, tools\layout\private_links.txt -- when it is there, so a fresh build folder needs no download; without it CMake
#  downloads them as before)
$sdl = Join-Path $port 'third_party/sdl2-src'
$sdlArg = if (Test-Path "$sdl/include") { @('-D', "FETCHCONTENT_SOURCE_DIR_SDL2=$($sdl.Replace([char]92, [char]47))") } else { @() }
cmake -S $port -B $B -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$tc" -DCMAKE_BUILD_TYPE=Release "-DARCADE_MOD_NATIVE=$native" "-DARCADE_MOD_NATIVE_WHOLE=$whole" @sdlArg
if ($LASTEXITCODE) { throw "configure failed" }
ninja -C $B NoMercyRecompiled
if ($LASTEXITCODE) { throw "build failed" }
Get-Item "$B\Ultimate No Mercy.exe" | Select-Object Length, LastWriteTime, FullName
