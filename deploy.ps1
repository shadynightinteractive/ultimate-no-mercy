# Puts the built mod (and with -Exe the built exe) into the game, the folder above this one.
# First the game's current files are backed up to backups\<its version> (the mod, its README; the exe with -Exe),
# then the new ones copied in and checked (MD5).
#   .\deploy.ps1          mod\build\mod_six\nomercy_arcade.nrm + mod\README.txt  -> the game
#                         (+ mod\build\stories\*: the custom stories and _roster.json -> the game's stories folder)
#   .\deploy.ps1 -Exe     also exe\build\Ultimate No Mercy.exe
#   .\deploy.ps1 -Commentary   only the commentary pack: mod\build\commentary.pak (+ .1, ...; mod\gen\compak.py pack)
param([switch]$Exe, [switch]$Commentary)
# (newhome 2026-11-17: the game is the folder "game" beside this source tree -- ..\game -- or $env:UNM_GAME; it used to be
#  the folder above Development)
# (a hold: deploy.hold in this folder says why -- the user asked for no deploys for a while, 2026-11-07 "No need to deploy
#  anything until tonight". Nothing is copied while it is there; delete it to deploy again.)
if (Test-Path "$PSScriptRoot\deploy.hold") { "deploy held: " + (Get-Content "$PSScriptRoot\deploy.hold" -Raw).Trim(); exit 0 }
if ($Commentary) {
    $ErrorActionPreference = 'Stop'
    $game = $(if ($env:UNM_GAME) { $env:UNM_GAME } else { Join-Path (Split-Path $PSScriptRoot -Parent) 'game' })
    if (Get-Process -Name 'Ultimate No Mercy', 'NoMercy Arcade' -ErrorAction SilentlyContinue | Where-Object { $_.Path -notlike '*\qa\game*' }) { throw "the game is running: close it first" }   # (a QA test copy isn't the game being replaced, 2026-11-07)
    $new = @(Get-ChildItem "$PSScriptRoot\mod\build\commentary.pak*")
    if (-not $new) { throw "not built: mod\build\commentary.pak" }
    $bk = "$PSScriptRoot\backups\commentary_$(Get-Date -Format yyyy-MM-dd_HHmm)"
    New-Item -ItemType Directory -Force $bk | Out-Null
    Get-ChildItem "$game\commentary.pak*" | Move-Item -Destination $bk   # (moved: an old .pak.N mustn't stay behind)
    Write-Host "backed up the game's pack -> $bk"
    foreach ($f in $new) {
        Copy-Item $f.FullName "$game\$($f.Name)"
        if ((Get-FileHash $f.FullName -Algorithm MD5).Hash -ne (Get-FileHash "$game\$($f.Name)" -Algorithm MD5).Hash) { throw "copy differs: $($f.Name)" }
        Write-Host "  $($f.Name)  ok"
    }
    return
}
$ErrorActionPreference = 'Stop'
$game = $(if ($env:UNM_GAME) { $env:UNM_GAME } else { Join-Path (Split-Path $PSScriptRoot -Parent) 'game' })
$nrm = "$PSScriptRoot\mod\build\mod_six\nomercy_arcade.nrm"
$readme = "$PSScriptRoot\mod\README.txt"
$newExe = "$PSScriptRoot\exe\build\Ultimate No Mercy.exe"
$stories = "$PSScriptRoot\mod\build\stories"
if (Get-Process -Name 'Ultimate No Mercy', 'NoMercy Arcade' -ErrorAction SilentlyContinue | Where-Object { $_.Path -notlike '*\qa\game*' }) { throw "the game is running: close it first" }   # (a QA test copy isn't the game being replaced, 2026-11-07)
foreach ($f in @($nrm, $readme, "$stories\_roster.json") + @(if ($Exe) { $newExe })) { if (-not (Test-Path $f)) { throw "not built: $f" } }

# (the exe has the mods' ahead-of-time translation linked in, tools\aot: it must be this mod's, or the game would quietly
#  translate the mod as it loads it. Checked for the exe being deployed; a mod deployed alone leaves the installed exe on
#  the live recompiler until an exe built from it follows -- said. 2026-11-11)
. "$PSScriptRoot\toolchain\env.ps1"
python "$PSScriptRoot\tools\aot\aot.py" --check | Out-Host
if ($LASTEXITCODE) {
    if ($Exe) { throw "the exe was not built from this mod: .\build_exe.ps1, then deploy" }
    Write-Warning "the mod alone: the game's exe will translate it as it loads it (no linked translation) until an exe built from it is deployed"
} elseif (-not $Exe) { Write-Warning "the mod alone: the installed exe uses its linked translation only if it was built from this very mod (deploy -Exe to be sure)" }

function version-of($readmePath) {
    if (-not (Test-Path $readmePath)) { return 'none' }    # (newhome 2026-11-17: a fresh game folder has no README yet)
    $m = Select-String -Path $readmePath -Pattern 'Version (\d+(\.\d+)+)' | Select-Object -First 1
    if ($m) { $m.Matches[0].Groups[1].Value } else { 'unknown' }
}
$old = version-of "$game\ARCADE README.txt"
$new = (Select-String -Path "$PSScriptRoot\mod\recomp\mod.toml" -Pattern '^version = "(.+)"').Matches[0].Groups[1].Value
if ($old -eq $new) { Write-Warning "the game already has ${new}: bump the version in mod\recomp\mod.toml and mod\README.txt for a new one" }

# back up what's there (newhome 2026-11-17: only what there is -- a fresh game folder has none of it)
$bk = "$PSScriptRoot\backups\$old"
New-Item -ItemType Directory -Force $bk | Out-Null
foreach ($f in @("$game\assets\mods\nomercy_arcade.nrm", "$game\ARCADE README.txt") + @(if ($Exe) { "$game\Ultimate No Mercy.exe", "$game\NoMercy Arcade.exe" })) {
    if (Test-Path $f) { Copy-Item $f $bk -Force }
}
if (Test-Path "$game\stories\*") {
    New-Item -ItemType Directory -Force "$bk\stories" | Out-Null
    Copy-Item "$game\stories\*" "$bk\stories\" -Force
}
Write-Host "backed up $old -> $bk"

# copy in, and check
$pairs = @(@($nrm, "$game\assets\mods\nomercy_arcade.nrm"), @($nrm, "$game\mods\nomercy_arcade.nrm"),
           @($readme, "$game\ARCADE README.txt"))
if ($Exe) { $pairs += , @($newExe, "$game\Ultimate No Mercy.exe") }
# (2026-11-11: the media player's FFmpeg -- our own LGPL build, linked as DLLs -- goes beside the exe with its licence and how it
#  was built, as the LGPL asks. CMake puts them in exe\build next to the exe. The game starts without them: themes and videos in
#  common formats are then skipped, the old files still play.)
if ($Exe) {
    $ff = @(Get-ChildItem "$PSScriptRoot\exe\build" -File | Where-Object { $_.Name -match '^(avcodec|avformat|avutil|swresample|swscale)-\d+\.dll$|^FFmpeg-.*\.txt$' })
    if ($ff.Count -lt 8) { throw "FFmpeg's DLLs and licences not all in exe\build ($($ff.Count) of 8): build the exe first" }
    foreach ($f in $ff) { $pairs += , @($f.FullName, "$game\$($f.Name)") }
}
New-Item -ItemType Directory -Force "$game\stories" | Out-Null
foreach ($f in Get-ChildItem "$stories\*" -File) { $pairs += , @($f.FullName, "$game\stories\$($f.Name)") }
# the HD pictures of a model drawn whole (src/hd.c): texture packs, each a folder in the game's mods. Each folder emptied first: RT64 takes a
# .dds over a .png of the same name, so an old file left there would win. (Only the folders that are ours, named in mod\texturepack.)
foreach ($pk in Get-ChildItem "$PSScriptRoot\mod\texturepack" -Directory -ErrorAction SilentlyContinue) {
    if (Test-Path "$game\mods\$($pk.Name)") { Remove-Item "$game\mods\$($pk.Name)" -Recurse -Force }
}
# (RT64's packs, which nothing reads now. Texture packs are the game's texturepacks folder: exe src\main\arcade_texpacks.cpp.)
New-Item -ItemType Directory -Force "$game\texturepacks" | Out-Null
Copy-Item "$PSScriptRoot\mod\texturepacks\README.txt" "$game\texturepacks\README.txt" -Force
# (automerge, 2026-11-23) F2's MERGE SAME WRESTLERS: the roster editor's rule data (one source: the private folder's editor\data)
$ed = @($env:UNM_PRIVATE, (Join-Path (Split-Path $PSScriptRoot -Parent) 'private'), (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent)) |
    Where-Object { $_ -and (Test-Path "$_\editor\data\aliases.json") } | Select-Object -First 1
if ($ed) {
    New-Item -ItemType Directory -Force "$game\data" | Out-Null
    foreach ($d in 'aliases.json', 'builtin_roster.json') { $pairs += , @("$ed\editor\data\$d", "$game\data\$d") }
} else { Write-Host "  data\aliases.json: the roster editor's data isn't found (set UNM_PRIVATE): MERGE SAME WRESTLERS merges nothing" }
# the Superstar Select's pages: mod\select_pages.json, only if the game hasn't one (one there may be the user's own)
if (-not (Test-Path "$game\select_pages.json")) { $pairs += , @("$PSScriptRoot\mod\select_pages.json", "$game\select_pages.json") }
else { Write-Host "  select_pages.json: the game's own kept (mod\select_pages.json is the default)" }
foreach ($p in $pairs) {
    Copy-Item $p[0] $p[1] -Force
    if ((Get-FileHash $p[0] -Algorithm MD5).Hash -ne (Get-FileHash $p[1] -Algorithm MD5).Hash) { throw "copy differs: $($p[1])" }
    Write-Host "  $($p[1].Substring($game.Length + 1))  ok"
}
# (exename 2026-11-20) The program was "NoMercy Arcade.exe" before it became "Ultimate No Mercy.exe". An old install still has
#  the old one -- our own build, backed up above with the rest -- so it goes once the new one is in and checked: the game folder
#  holds one program, not a stale second one. Only that file; nothing of the player's is touched.
if ($Exe -and (Test-Path -LiteralPath "$game\NoMercy Arcade.exe")) {
    if (-not (Test-Path -LiteralPath "$bk\NoMercy Arcade.exe")) { throw "the old NoMercy Arcade.exe is not backed up in ${bk}: not removing it" }
    Remove-Item -LiteralPath "$game\NoMercy Arcade.exe" -Force
    Write-Host "  NoMercy Arcade.exe  removed (the program's old name; backed up in $bk)"
}
# Wrestlers from folders: only the template. The game's other folders in there are the user's own, and are left alone.
New-Item -ItemType Directory -Force "$game\wrestlers\_example" | Out-Null
Copy-Item "$PSScriptRoot\mod\wrestlers\_example\wrestler.json" "$game\wrestlers\_example\wrestler.json" -Force
Copy-Item "$PSScriptRoot\mod\wrestlers\README.txt" "$game\wrestlers\README.txt" -Force
# Arenas from folders: only the template, as with wrestlers.
New-Item -ItemType Directory -Force "$game\arenas\_example" | Out-Null
Copy-Item "$PSScriptRoot\mod\arenas\_example\arena.json" "$game\arenas\_example\arena.json" -Force
Copy-Item "$PSScriptRoot\mod\arenas\README.txt" "$game\arenas\README.txt" -Force
# The content packs: plain files the program loads at the start (exe src\main\arcade_packs.cpp).
New-Item -ItemType Directory -Force "$game\content" | Out-Null
# (2026-11-13, movelib2: and the built-in move library, Revenge's whole move list -- the exe reads it, arcade_wrestlers.cpp)
# (2026-11-13, wm2klib: and WrestleMania 2000's, the moves its Superstars use that No Mercy has no move for -- read after it)
foreach ($n in 'content.bin','wmport.bin','movelib_revenge.bin','movelib_wm2000.bin') {   # (no2k15: hd.bin, hd_rock.bin gone)
    Copy-Item "$PSScriptRoot\mod\build\$n" "$game\content\$n" -Force
    if ((Get-FileHash "$PSScriptRoot\mod\build\$n" -Algorithm MD5).Hash -ne (Get-FileHash "$game\content\$n" -Algorithm MD5).Hash) { throw "copy differs: $n" }
    Write-Host "  content\$n  ok"
}
# (no2k15, 2026-11-23: native_view.pak -- the WWE 2K15 test slots' pictures for the native renderer -- is no longer copied: the
#  slots are removed. A game folder that still has one keeps it unread.)
# (privacy 2026-11-16) a landmark install: the build folders' CMake files lose this PC's Windows build number
& "$PSScriptRoot\toolchain\python\python.exe" "$PSScriptRoot\tools\privacy_scrub_build.py" | Select-Object -Last 1
Write-Host "deployed $new"
