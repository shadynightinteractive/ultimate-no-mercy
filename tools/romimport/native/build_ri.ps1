# build_ri.ps1 (phase3, 2026-11-19): builds ri_cli.exe -- the ROM importer's steps (exe\WWFNoMercyRecomp\src\romimport) with a
# command-line driver, for the checks against the Python reference. The game builds the same sources in (CMakeLists.txt).
#   .\tools\romimport\native\build_ri.ps1 [-Out <folder>] [-Debug]
param([string]$Out = '', [switch]$Debug)
$ErrorActionPreference = 'Stop'
$src = (Resolve-Path "$PSScriptRoot\..\..\..").Path
if (-not $Out) { $Out = Join-Path $src 'scratch\ri' }
New-Item -ItemType Directory -Force $Out | Out-Null
$ri = "$src\exe\WWFNoMercyRecomp\src\romimport"
$tp = "$src\exe\WWFNoMercyRecomp\lib\N64ModernRuntime\thirdparty"
$opt = @(if ($Debug) { '-O0'; '-g' } else { '-O2' })
$objs = @()
# miniz (C), once; its export header is made by CMake in the game's build: a plain one here
Set-Content -Encoding ascii (Join-Path $Out 'miniz_export.h') "#pragma once`n#define MINIZ_EXPORT"
foreach ($m in Get-ChildItem "$tp\miniz\miniz*.c") {
    $mz = Join-Path $Out ($m.BaseName + '.o')
    if (-not (Test-Path $mz)) {
        & clang -c @opt "-I$tp\miniz" "-I$Out" $m.FullName -o $mz
        if ($LASTEXITCODE) { throw 'miniz failed' }
    }
    $objs += $mz
}
# Pillow's median cut (C, its own code: src\main\pillow_mediancut), for the first start's stage sets the pack builder shares
$pm = "$src\exe\WWFNoMercyRecomp\src\main\pillow_mediancut"
foreach ($m in Get-ChildItem "$pm\*.c") {
    $po = Join-Path $Out ('pm_' + $m.BaseName + '.o')
    if (-not (Test-Path $po) -or (Get-Item $po).LastWriteTime -lt $m.LastWriteTime) {
        & clang -c @opt "-I$pm" $m.FullName -o $po
        if ($LASTEXITCODE) { throw "compile failed: $($m.Name)" }
    }
    $objs += $po
}
# (src\main\arcade_firstrun_stages.cpp: the game's ten arenas' stage sets, which the pack builder ri_wmarenas uses too)
$cpps = @(Get-ChildItem "$ri\*.cpp" | ForEach-Object { $_.FullName }) + @("$src\exe\WWFNoMercyRecomp\src\main\arcade_firstrun_stages.cpp") + @("$PSScriptRoot\ri_cli.cpp")
foreach ($c in $cpps) {
    $o = Join-Path $Out ([IO.Path]::GetFileNameWithoutExtension($c) + '.o')
    if ((Test-Path $o) -and (Get-Item $o).LastWriteTime -gt (Get-Item $c).LastWriteTime -and
        -not (@(Get-ChildItem "$ri\*.h") + @(Get-ChildItem "$ri\*.inc") + @(Get-ChildItem "$PSScriptRoot\*.inc") | Where-Object { $_.LastWriteTime -gt (Get-Item $o).LastWriteTime })) { $objs += $o; continue }
    & clang++ -c -std=c++20 @opt -ffp-contract=off "-I$ri" "-I$tp" "-I$tp\miniz" "-I$Out" $c -o $o
    if ($LASTEXITCODE) { throw "compile failed: $c" }
    $objs += $o
}
& clang++ @opt @objs -o (Join-Path $Out 'ri_cli.exe')
if ($LASTEXITCODE) { throw 'link failed' }
"built $(Join-Path $Out 'ri_cli.exe')"
