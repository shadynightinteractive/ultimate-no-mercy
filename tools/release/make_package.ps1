# Makes the release package: the folder (and zip) a player downloads. Only what the project made and the third-party
# programs it ships with -- NO game data: no ROM, no content packs, no wrestler or arena folders made from a game, no
# commentary audio. The player supplies the ROM (see release\README.txt). docs\RELEASE.md says what is in and why.
#   .\tools\release\make_package.ps1 [-Out <folder>] [-NoZip] [-NoAudit]
#     -Out     where the package folder goes (default scratch\release\<this tree's folder name>\UltimateNoMercy-<version>;
#              scratch\ is git-ignored). The first-start plan is made beside it, in <Out>.plan (one per package).
#     -NoZip   the folder only
#     -NoAudit skip tools\release\release_audit.py --package (never for a package that goes out)
# Needs a build: .\build_mod.ps1 then .\build_exe.ps1 (the exe must be built from this very mod: aot.py --check), the content
# packs in mod\build (content.bin, wmport.bin) and the mod built from the player-configuration headers
# (below: checked before anything is made).
param([string]$Out = '', [switch]$NoZip, [switch]$NoAudit)
$ErrorActionPreference = 'Stop'
$S = (Resolve-Path "$PSScriptRoot\..\..").Path
$port = "$S\exe\WWFNoMercyRecomp"
$ver = (Select-String -Path "$S\mod\recomp\mod.toml" -Pattern '^version = "(.+)"').Matches[0].Groups[1].Value
# (relcheck 2026-11-19) scratch\ is the SHARED private scratch folder in every worktree (a junction): two trees packaging at
#  once used to write the same scratch\firstrun_plan and the same default package folder, and one package carried another
#  branch's mod. Now the default package folder is per tree, the plan is per package (<Out>.plan), and <Out>.lock stops a
#  second run on the same folder.
if (-not $Out) { $Out = "$S\scratch\release\$(Split-Path $S -Leaf)\UltimateNoMercy-$ver" }
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)   # (a relative -Out: from the current location)
. "$S\toolchain\env.ps1"

# --- before anything is made: what the package needs from this tree's build (relcheck 2026-11-19: a fresh mod\build or the
#     developer's headers made a package whose first start stopped at "the plan has no pack table" / a hole that doesn't fit)
# (1) the content packs: mkplan.py writes their sizes and SHA-256 into the plan (the pack table the first start builds
#     content\wmport_base.bin by, and the check the extras are filled against); a fresh mod\build has none.
$missing = @(foreach ($b in 'content.bin', 'wmport.bin') {   # (no2k15, 2026-11-23: hd.bin / hd_rock.bin, the 2K15 test slots', gone)
    if (-not (Test-Path "$S\mod\build\$b")) { $b } })
if ($missing.Count) {
    throw ("mod\build has no " + ($missing -join ', ') + ": the plan would have no pack table (the first start stops with " +
           "'the plan has no pack table'). Put the content packs the mod was built with into $S\mod\build (the developer's " +
           "are in the private folder's build\mod\build, or make them: mod\gen / ri_cli) and run this again.")
}
# (2) the five player-configuration headers: the mod must be built from the versions the player's first start makes
#     (ri_cli modextra), not from the private folder's developer copies, or the extras' holes won't fit the player's tables.
#     A tree whose mod\src still links (or holds the same bytes as) the private folder's copy of one of them is refused.
#     (If the private folder's copies are ever replaced by the player-configuration versions, drop this check.)
#     (pkgcheck 2026-11-20, the user's approval: wmport_files.h no longer has to DIFFER from the private folder's copy -- since
#     stagescale (main 53f7f88) private\generated\mod\src\wmport_files.h IS the player-configuration version, so a correct
#     tree holds the same bytes. It must still be there and still be a plain file, not a link; the other four headers are
#     checked exactly as before.)
# (the private folder: $env:UNM_PRIVATE, else beside the tree, else -- a worktree in private\wt\<name> -- the one it is in)
$priv = @($env:UNM_PRIVATE, (Join-Path (Split-Path $S -Parent) 'private'), (Split-Path (Split-Path $S -Parent) -Parent)) |
    Where-Object { $_ -and (Test-Path "$_\generated\mod\src") } | Select-Object -First 1
if (-not $priv) { throw "the private folder (its generated\mod\src, for the header check below) isn't found: set UNM_PRIVATE" }
$dev = @(foreach ($h in 'content_index.h', 'wmport_files.h', 'rev_photos.h', 'jarrett_photo.h', 'meanie_photo.h') {
    $here = "$S\mod\src\$h"; $there = "$priv\generated\mod\src\$h"
    if (-not (Test-Path $here)) { "$h (missing)"; continue }
    if ((Get-Item -LiteralPath $here).LinkType -eq 'HardLink') { "$h (a link to the private folder's copy)"; continue }
    if ($h -eq 'wmport_files.h') { continue }   # (pkgcheck: the private folder's copy is the player-configuration one now)
    if ((Test-Path $there) -and (Get-FileHash $here).Hash -eq (Get-FileHash $there).Hash) { "$h (the private folder's developer copy)" }
})
if ($dev.Count) {
    throw ("mod\src: " + ($dev -join '; ') + ". The package's mod must be built from the player-configuration headers as " +
           "plain files (ri_cli modextra writes them; e.g. copy them from a tree that has them, remove the links first), " +
           "then .\build_mod.ps1 and .\build_exe.ps1 again.")
}
# (romsets 2026-11-24) Revenge's move library's count and stamp, which the exe keeps without the Revenge ROM (arcade_wrestlers.cpp
#     move_library_numbers_only: tools\handlers\handlers\revenge\library_head.json, shipped in firstrun\romimport), must be the
#     ones the mod is built with (mod\src\move_lib.h MOVELIB_N / MOVELIB_STAMP), or WrestleMania 2000's moves play without
#     Revenge in no install.
$mlh = Get-Content -Raw "$S\mod\src\move_lib.h" -ErrorAction SilentlyContinue
$head = Get-Content -Raw "$S\tools\handlers\handlers\revenge\library_head.json" | ConvertFrom-Json
if (-not $mlh -or $mlh -notmatch '#define MOVELIB_N (\d+)') { throw "mod\src\move_lib.h has no MOVELIB_N" }
$mlN = [int]$Matches[1]
if ($mlh -notmatch '#define MOVELIB_STAMP (0x[0-9A-Fa-f]+)u') { throw "mod\src\move_lib.h has no MOVELIB_STAMP" }
$mlStamp = [Convert]::ToUInt32($Matches[1].Substring(2), 16)
if ($mlN -ne [int]$head.moves -or $mlStamp -ne [Convert]::ToUInt32($head.stamp.Substring(2), 16)) {
    throw ("tools\handlers\handlers\revenge\library_head.json ($($head.moves) moves, $($head.stamp)) isn't the mod's move_lib.h " +
           "($mlN, 0x$($mlStamp.ToString('X8'))): write the mod's count and stamp into it")
}
# (3) one run per package folder
New-Item -ItemType Directory -Force (Split-Path $Out -Parent) | Out-Null
$lock = "$Out.lock"
try { [System.IO.File]::Open($lock, 'CreateNew', 'Write').Close() }
catch { throw "another make_package run is making $Out ($lock is there; remove it if none is running)" }
try {

# --- what goes in: [source, path in the package]. Everything else is left out on purpose (docs\RELEASE.md).
$exe = "$S\exe\build\Ultimate No Mercy.exe"
$nrm = "$S\mod\build\mod_six\nomercy_arcade.nrm"
foreach ($f in $exe, $nrm, "$S\exe\build\SDL2.dll", "$S\mod\build\stories\_roster.json") { if (-not (Test-Path $f)) { throw "not built: $f (.\build_mod.ps1, .\build_exe.ps1)" } }
python "$S\tools\aot\aot.py" --check | Out-Host
if ($LASTEXITCODE) { throw "the exe was not built from this mod: .\build_exe.ps1 first" }
$ff = @(Get-ChildItem "$S\exe\build" -File | Where-Object { $_.Name -match '^(avcodec|avformat|avutil|swresample|swscale)-\d+\.dll$|^FFmpeg-.*\.txt$' })
if ($ff.Count -lt 8) { throw "FFmpeg's DLLs and licences not all in exe\build ($($ff.Count) of 8): build the exe first" }

$files = [System.Collections.Generic.List[object]]::new()
function add($from, $to) { $files.Add(@($from, $to)) }
add $exe 'Ultimate No Mercy.exe'
add "$S\exe\build\SDL2.dll" 'SDL2.dll'
foreach ($f in $ff) { add $f.FullName $f.Name }
add "$S\release\README.txt" 'README.txt'                       # the player's first page: what to do
add "$S\mod\README.txt" 'ARCADE README.txt'                     # the arcade itself, and the changelog
add "$port\COPYING" 'COPYING'                                   # GPL-3.0 (the port and the mod)
add "$port\recompcontrollerdb.txt" 'recompcontrollerdb.txt'
add "$port\third_party\gamecontrollerdb\gamecontrollerdb.txt" 'gamecontrollerdb.txt'
add "$port\third_party\gamecontrollerdb\LICENSE.txt" 'gamecontrollerdb_LICENSE.txt'
if (Test-Path "$port\third_party\sdl2-src\COPYING.txt") { add "$port\third_party\sdl2-src\COPYING.txt" 'SDL2_LICENSE.txt' }
add "$port\src\main\pillow_mediancut\LICENSE.txt" 'Pillow_LICENSE.txt'   # (firstrun 2026-11-17: Pillow's median cut is in the exe -- the first start's stage-set picture)
# (commentary2 2026-11-18) the commentary builder's libraries, linked into the exe: their licences ship beside it
add "$port\third_party\libvorbis\COPYING" 'libvorbis_LICENSE.txt'
add "$port\third_party\libogg\COPYING" 'libogg_LICENSE.txt'
add "$port\third_party\zstd\LICENSE" 'zstd_LICENSE.txt'
add "$port\third_party\coremath\LICENSE" 'CORE-MATH_LICENSE.txt'
add "$port\third_party\coremath\musl_COPYRIGHT" 'musl_LICENSE.txt'
add "$port\src\main\commentary\packbuild\vgmstream\COPYING" 'vgmstream_LICENSE.txt'
# the program's own files: UI fonts, icons, the AkiMods; the mod (also in mods\, where the loader reads it)
foreach ($f in Get-ChildItem "$port\assets" -Recurse -File) { add $f.FullName ('assets\' + $f.FullName.Substring("$port\assets\".Length)) }
# (firstrun 2026-11-17) NOT the mod as built: it has tables made from the games in it. The package has it without them --
# firstrun\mod_binary.holes (those bytes zero), its other two members, and the plan the game's first start fills them by
# from the player's ROM (tools\firstrun\mkplan.py; exe src\main\arcade_firstrun.cpp); the first start writes
# assets\mods\nomercy_arcade.nrm. The plan maker needs the ROM (roms\) to check its plan rebuilds the build's mod from it.
$plan = "$Out.plan"     # (relcheck 2026-11-19: one per package, not the shared scratch\firstrun_plan -- see the top)
if (Test-Path $plan) { Remove-Item $plan -Recurse -Force }
python "$S\tools\firstrun\mkplan.py" --build "$S\mod\build\mod_six" --content "$S\mod\build" --out $plan | Out-Host
if ($LASTEXITCODE) { throw "tools\firstrun\mkplan.py: the plan doesn't rebuild the build's mod from the ROM (see $plan\plan_report.txt)" }
# (relcheck 2026-11-19, approved by the user on 2026-11-19: "I looked at your list you needed from me and I concur with your
#  recommendations" -- REVIEW_STARTUP_MASK.txt's optional item (a)) the load-speed fix's proof, every package: the exe's linked
#  translation is taken for a player's extras-filled mod by a hash with the extras holes masked, which is right only while those
#  holes hold data alone. mask_proof.py checks it from this build and the plan shipped here (the masks == the plan's extras
#  holes; none in code, a function, a jump table or outside one data object; no instruction relocation in one). aot.py --check
#  (above) has already checked the exe's masks are these holes.
python "$S\tools\aot\mask_proof.py" --build "$S\mod\build\mod_six" --plan "$plan\firstrun\plan.bin" --out "$plan\mask_proof.txt" | Out-Host
if ($LASTEXITCODE) { throw "tools\aot\mask_proof.py: the extras holes are not proved data-only for this build (see $plan\mask_proof.txt)" }
foreach ($f in 'plan.bin', 'mod_binary.holes', 'mod.json', 'mod_syms.bin', 'hdrfill.bin') { add "$plan\firstrun\$f" "firstrun\$f" }
# (phase3 2026-11-19) the ROM importer's data (exe src\romimport: the other games' extras built on the first start): ours only --
# the RAM images' load maps (addresses and offsets), frozen indexes (file numbers, IDs, our plans), the handlers' tables
foreach ($f in Get-ChildItem "$S\tools\romimport\loadmaps" -File -Filter *.json) { add $f.FullName "firstrun\romimport\loadmaps\$($f.Name)" }
foreach ($f in Get-ChildItem "$S\tools\romimport\frozen" -File -Filter *.json) { add $f.FullName "firstrun\romimport\frozen\$($f.Name)" }
add "$S\tools\romimport\names_index.json" 'firstrun\romimport\names_index.json'
foreach ($f in Get-ChildItem "$S\tools\handlers\handlers\revenge" -File -Filter *.json) { add $f.FullName "firstrun\romimport\handlers\revenge\$($f.Name)" }
# (p3folders) the Revenge handler's per-man fixes (wrestlers\<string id>.json: ours) -- the folders' maker reads them
foreach ($f in Get-ChildItem "$S\tools\handlers\handlers\revenge\wrestlers" -File -Filter *.json) { add $f.FullName "firstrun\romimport\handlers\revenge\wrestlers\$($f.Name)" }
# (p3folders) the WrestleMania 2000 handler's per-man fixes (roster index, reference ordinal, name fixes: ours) and the
#  string IDs of No Mercy's game numbers (a folder's based_on: mod\gen\roster_ids.json, ours)
foreach ($f in Get-ChildItem "$S\tools\handlers\handlers\wm2000\wrestlers" -File -Filter *.json) { add $f.FullName "firstrun\romimport\handlers\wm2000\wrestlers\$($f.Name)" }
add "$S\tools\handlers\handlers\wm2000\rivals.json" 'firstrun\romimport\handlers\wm2000\rivals.json'   # (rostertags 2026-11-21: WrestleMania 2000's own rivals, copied into its collection)
add "$S\mod\gen\roster_ids.json" 'firstrun\romimport\roster_ids.json'
# (p3ports) the game's roster names (mod\gen\roster_base.json, ours): the content pack's maker reads them (ri_content)
add "$S\mod\gen\roster_base.json" 'firstrun\romimport\roster_base.json'
# (the built-in extras' tables: game data, never in a package -- kept beside the content packs for a developer's own use)
Copy-Item "$plan\modextra.bin" "$S\mod\build\modextra.bin" -Force
foreach ($f in Get-ChildItem "$port\assets\mods\aki_*.nrm") { add $f.FullName "mods\$($f.Name)" }
# the project's own content: stories, the Superstar Select's pages, the folder templates and READMEs
foreach ($f in Get-ChildItem "$S\mod\build\stories" -File) { add $f.FullName "stories\$($f.Name)" }
add "$S\mod\select_pages.json" 'select_pages.json'
# (automerge, 2026-11-23) F2's MERGE SAME WRESTLERS: the roster editor's rule data, one source -- the editor's own data folder
#  (the private folder's editor\data): the known-identity list and the built-in Superstars in roster order (ours, as text)
foreach ($d in 'aliases.json', 'builtin_roster.json') {
    if (-not (Test-Path "$priv\editor\data\$d")) { throw "the roster editor's $d isn't in $priv\editor\data (MERGE SAME WRESTLERS reads it)" }
    add "$priv\editor\data\$d" "data\$d"
}
add "$S\mod\wrestlers\README.txt" 'wrestlers\README.txt'
add "$S\mod\wrestlers\_example\wrestler.json" 'wrestlers\_example\wrestler.json'
add "$S\mod\arenas\README.txt" 'arenas\README.txt'
add "$S\mod\arenas\_example\arena.json" 'arenas\_example\arena.json'
add "$S\mod\texturepacks\README.txt" 'texturepacks\README.txt'
add "$S\release\roms_README.txt" 'roms\README.txt'              # where the player's ROM goes

# --- make the folder
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null
foreach ($p in $files) {
    $to = Join-Path $Out $p[1]
    New-Item -ItemType Directory -Force (Split-Path $to -Parent) | Out-Null
    Copy-Item -LiteralPath $p[0] $to -Force
}
New-Item -ItemType File "$Out\portable.txt" -Force | Out-Null   # (saves, settings and the log beside the program)
$n = @(Get-ChildItem $Out -Recurse -File).Count
$mb = [math]::Round((Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
"package: $Out -- $n files, $mb MB (version $ver)"

# --- the audit: no game data (rules + every byte against the ROMs and the content packs), no PC names
if (-not $NoAudit) {
    $forbid = @(foreach ($b in 'content.bin','wmport.bin','hd.bin','hd_rock.bin','movelib_revenge.bin','movelib_wm2000.bin','commentary.pak','modextra.bin') {
        if (Test-Path "$S\mod\build\$b") { '--forbid'; "$S\mod\build\$b" } })
    # (relcheck 2026-11-19) --mod-build: firstrun\mod_binary.holes is checked against the build it was made from (rule 'holes')
    # (relprep2) no earlier run's report is read below: it is deleted first, and a report that can't be deleted (locked)
    #  stops the package here
    if (Test-Path -LiteralPath "$Out.audit.txt") { Remove-Item -LiteralPath "$Out.audit.txt" -Force -ErrorAction Stop }
    python "$S\tools\release\release_audit.py" --package $Out --mod-build "$S\mod\build\mod_six" @forbid --report "$Out.audit.txt"
    $audit = $LASTEXITCODE
    "audit report: $Out.audit.txt"
    # (relprep2 2026-11-24) the report's own RESULT word: REFUSED, or INCOMPLETE (a source the audit needed was not there)
    $word = 'REFUSED'
    if (Test-Path "$Out.audit.txt") { $m = Select-String -Path "$Out.audit.txt" -Pattern '^RESULT: (\w+)'; if ($m) { $word = $m.Matches[0].Groups[1].Value } }
    if ($audit) { Write-Warning "the audit said $word for this package: it must not go out (see the report)"; if (-not $NoZip) { 'not zipped'; exit 1 } }
}
if (-not $NoZip) {
    $zip = "$Out.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path "$Out\*" -DestinationPath $zip
    "zip: $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
}

} finally { Remove-Item $lock -Force -ErrorAction SilentlyContinue }
