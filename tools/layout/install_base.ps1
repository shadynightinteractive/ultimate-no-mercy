# Puts the program's base files into a fresh game folder (newhome, 2026-11-17) -- what deploy.ps1 never copied, because the
# old game folder had them from the original port's release: the UI assets and the AkiMods (exe\WWFNoMercyRecomp\assets),
# SDL2.dll (from the build), the controller databases, the licence, portable.txt (settings and saves beside the exe).
# Then .\deploy.ps1 -Exe puts the build in, the importers make the wrestler folders (tools\handlers\import_all.ps1), and the
# player's own ROM goes in <game>\roms\.
#   .\tools\layout\install_base.ps1 [-Game <folder>]      (default: ..\game beside the source tree, or $env:UNM_GAME)
param([string]$Game = '')
$ErrorActionPreference = 'Stop'
$S = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Game) { $Game = if ($env:UNM_GAME) { $env:UNM_GAME } else { Join-Path (Split-Path $S -Parent) 'game' } }
$port = "$S\exe\WWFNoMercyRecomp"
if (-not (Test-Path "$S\exe\build\SDL2.dll")) { throw "build the exe first (.\build_exe.ps1): SDL2.dll comes from the build" }
New-Item -ItemType Directory -Force $Game, "$Game\mods", "$Game\roms" | Out-Null
robocopy "$port\assets" "$Game\assets" /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "copying the assets failed (robocopy $LASTEXITCODE)" }
Copy-Item "$port\assets\mods\aki_*.nrm" "$Game\mods\" -Force
Copy-Item "$S\exe\build\SDL2.dll", "$port\recompcontrollerdb.txt", "$port\COPYING" $Game -Force
Copy-Item "$port\third_party\gamecontrollerdb\gamecontrollerdb.txt" $Game -Force
Copy-Item "$port\third_party\gamecontrollerdb\LICENSE.txt" "$Game\gamecontrollerdb_LICENSE.txt" -Force
if (-not (Test-Path "$Game\portable.txt")) { New-Item -ItemType File "$Game\portable.txt" | Out-Null }
"base files in ${Game} -- next .\deploy.ps1 -Exe, then the wrestler folders and your ROM (roms\)"
