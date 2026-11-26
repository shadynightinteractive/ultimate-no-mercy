# Dot-source this to put the project's toolchain on PATH for the current shell:
#   . .\tools\env.ps1
#
# Puts the MinGW/CMake/Ninja toolchain (used to build N64Recomp and run gen_symbols)
# on PATH. Set WCW_MINGW / WCW_NINJA_DIR / WCW_CMAKE_DIR to their folders if you want
# other copies; otherwise the Development folder's own toolchain (Development\toolchain,
# portable copies) is used, and after that whatever is already on PATH.
# (2026-11-13: no install locations of one PC are written here any more -- the old
#  defaults named a user profile's folders. Folders come from the environment only.)

$devToolchain = Join-Path $PSScriptRoot '..\..\..\toolchain\env.ps1'
if (Test-Path $devToolchain) { . $devToolchain }

foreach ($d in @($env:WCW_MINGW, $env:WCW_NINJA_DIR, $env:WCW_CMAKE_DIR)) {
    if ($d -and (Test-Path $d)) { $env:Path = "$d;" + $env:Path }
}

Write-Host "Toolchain on PATH:" -ForegroundColor Cyan
foreach ($t in @("gcc","g++","cmake","ninja","python")) {
    $c = Get-Command $t -ErrorAction SilentlyContinue
    if ($c) { Write-Host ("  {0,-7} {1}" -f $t, $c.Source) }
    else    { Write-Host ("  {0,-7} (missing: put it on PATH or set WCW_MINGW / WCW_NINJA_DIR / WCW_CMAKE_DIR)" -f $t) -ForegroundColor Yellow }
}
