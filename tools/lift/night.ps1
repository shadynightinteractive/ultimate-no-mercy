# The converted-code set made again (after a layout or translation change: converted functions bake in addresses):
# the whole pipeline, the set written and its names checked, built, tested broadly at 6x (converted code runs all
# over the game: the general set and the 32-slot set), then the start-up test at normal speed, which deploys.
#   .\tools\lift\night.ps1 -Version 0.59.0
param([string]$Version = '', [string]$From = 'analysis',   # (-From candidates: the analysis and independence already done)
      [switch]$FullQA,      # (the whole QA set whatever the score: see "The QA set" below. NIGHT_FULL_QA=1 does the same.)
      [switch]$NoQA)        # (convert 2026-11-22: stop after the build, check-modrep and the score -- the QA set is run apart,
                            #  by whoever holds the shared game slot (notes\slot_<agent>.lock), in a scratch install, never game\)
# (shardfold 2026-11-17, the review's fix 1: LIFT_GAME_SHARDS -- see the random test below -- is ON only as a whole number 2 to
#  64, written plainly; unset or empty is OFF; anything else stops the night here, before any step. Until now any non-empty
#  text took the ON branch: "1" made the first long pass exit (lift.py refuses it) and the night went on from an old
#  report.txt, "0" ran the longer passes unsharded on all the workers.)
$gameShards = $env:LIFT_GAME_SHARDS
if ($null -ne $gameShards -and $gameShards -ne '') {
    if ($gameShards -cnotmatch '\A[1-9][0-9]?\z' -or [int]$gameShards -lt 2 -or [int]$gameShards -gt 64) {
        throw "LIFT_GAME_SHARDS='$gameShards': a whole number 2 to 64, or unset -- not running"
    }
} else { $gameShards = $null }
# (and out of the environment until the long passes: every step before them -- the analysis, the independence test, the
#  candidates -- runs exactly as with the switch off)
Remove-Item Env:LIFT_GAME_SHARDS, Env:LIFT_GAME_SHARDS_READ -ErrorAction SilentlyContinue
# (above-normal: this run is the critical path from the agents' fixes to a build; below-normal only while the user's own
#  game is open, so it stays smooth -- 2026-11-08)
$theirGame = Get-Process | Where-Object { ($_.ProcessName -like 'NoMercy*' -or $_.ProcessName -eq 'Ultimate No Mercy') -and $_.Path -notlike '*\qa\game*' }
(Get-Process -Id $PID).PriorityClass = $(if ($theirGame) { 'BelowNormal' } else { 'AboveNormal' })
# (convert 2026-11-22: NIGHT_PRIORITY=BelowNormal / Idle keeps the whole night -- the builds, m2c, the steps -- below the other
#  agents and the user's programs, as the user's "yield CPU to others" rule asks when the PC is shared)
if ($env:NIGHT_PRIORITY -in 'Idle', 'BelowNormal', 'Normal') { (Get-Process -Id $PID).PriorityClass = $env:NIGHT_PRIORITY }
$D = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location $D
$work = "$D\scratch\lift"
New-Item -ItemType Directory -Force $work | Out-Null
"== start $(Get-Date)"
# (2026-11-17, pipefix) THE MOD THAT SHIPS IS BUILT FIRST. The mod's copies below (modrep.py) are translated from the mod's build,
#  and until 0.84 that was the build left by the last run: this run's "build mod" came after the conversion, so the 0.84 night
#  converted and tested 157 copies of which 133 were not the shipped mod's (their hash differed at "build exe": the loader ran the
#  mod's own for them) -- and the score counted them. So the version is stamped and the mod built here, before anything reads
#  it; Stage 1, the copies, the analysis and the native mod all read this one build, and nothing builds the mod again in this run
#  (checked before the exe is built: $modBuildHash). lift.py score also refuses any copy whose hash is not this build's.
if ($Version) {
    (Get-Content mod\recomp\mod.toml) -replace '^version = "[0-9.]+"', "version = `"$Version`"" | Set-Content mod\recomp\mod.toml
    # (and the README's: deploy.ps1 names its backup of the game after the version the README says -- left behind, every
    #  deploy went into the same folder, 2026-11-06)
    (Get-Content mod\README.txt) -replace 'Version [0-9]+\.[0-9]+\.[0-9]+', "Version $Version" | Set-Content mod\README.txt
}
# (the mod first: the exe links the mod's ahead-of-time translation, made by build_exe.ps1 from the mod as built, 2026-11-11)
"== build mod $(Get-Date)"; $null = .\build_mod.ps1 2>&1
$modFiles = 'mod\build\mod_six\mod_binary.bin', 'mod\build\mod_six\mod_syms.bin', 'mod\build\mod_six\mod.json'
if (@($modFiles | Where-Object { -not (Test-Path $_) }).Count) { throw 'build mod: the six-man build is missing -- not converting an old one' }
$modBuildHash = ($modFiles | ForEach-Object { (Get-FileHash $_).Hash }) -join ','
# (the mod's replacement functions, translated from its build to be converted beside the game's: modrep.py. Before the
#  analysis, which reads them too.)
"== the mod's copies"; python tools\lift\modrep.py 2>&1 | Select-Object -Last 1
# (Not fatal: without the mod's copies the night's run is the run it was before them, and a night isn't lost. So none are left
#  over from an earlier build, either.)
if ($LASTEXITCODE) { "the mod's copies could not be made: going on without them"; Remove-Item tools\lift\modrep\out -Recurse -Force -ErrorAction SilentlyContinue }
# (2026-11-12, 0.81, final081: the game functions the mods hook convert with their hook calls only where Stage 1's hooked copies
#  -- src\game\mod_aot, which build_exe.ps1 makes from the mod as built and the ROM -- are their translated originals with the
#  calls in (tools\lift\hooks.py). Made by the last build, they may be another mod's or ROM's: then those functions are refused,
#  safely, until the next run. So they are made again here when they aren't the mods as they are now (aot.py --check).)
"== Stage 1 for the hooked functions"; python tools\aot\aot.py --check 2>&1 | Select-Object -Last 1
if ($LASTEXITCODE) { python tools\aot\aot.py 2>&1 | Select-Object -Last 2 }
if ($From -eq 'analysis') {
    "== analysis";       python tools\lift\analyse.py 2>&1 | Select-Object -Skip 1 -First 2
    # (2026-11-12, the speed agent: the second analysis only differs from the first through what the independence test found.
    #  When independent.json is byte for byte what the first one read, its analysis.json is too -- analyse.py gives the same file
    #  for the same inputs, checked -- so it isn't made again.)
    $indepBefore = (Get-FileHash tools\lift\independent.json -ErrorAction SilentlyContinue).Hash
    "== independence";   python tools\lift\lift.py independence 100000 2>&1 | Select-Object -Last 4
    if ((Get-FileHash tools\lift\independent.json -ErrorAction SilentlyContinue).Hash -eq $indepBefore -and $indepBefore) {
        "== analysis again: not needed (independent.json unchanged)"
    } else {
        "== analysis again"; python tools\lift\analyse.py 2>&1 | Select-Object -Skip 1 -First 2
    }
}
# (the translated code as MIPS assembly, m2c's input: to_asm.py writes it and its index fresh -- candidates reads them)
"== disassembly $(Get-Date)"; python tools\lift\to_asm.py "$work\asm" 2>&1 | Select-Object -Last 3
if ($LASTEXITCODE) { throw 'to_asm failed' }
"== candidates $(Get-Date)"; python tools\lift\lift.py candidates "$work\asm" "$work\work" 2>&1 | Select-Object -Last 12
if ($LASTEXITCODE) { throw 'candidates failed: not testing an old list' }
# (2026-11-17, shardgame: LIFT_GAME_SHARDS=K, set for the night -- APPROVED by the user 2026-11-17 to split a slow function's
#  tests across idle threads; reviewed by reviewsh (APPROVE WITH FIXES, the fixes folded by shardfold). Read and checked at the
#  top of this script ($gameShards). The three long passes below then run every function as K shards in one pool of all the
#  LIFT_WORKERS (lift.py game_shard_results) -- not 5 minutes unsharded and then 4 shards a function with the workers cut to a
#  quarter -- and this first pass, not sharded, also takes a K-shard line that met the bar (LIFT_GAME_SHARDS_READ). Unset:
#  every pass as before.)
Remove-Item Env:LIFT_GAME_SHARDS, Env:LIFT_GAME_SHARDS_READ -ErrorAction SilentlyContinue
if ($gameShards) { "== game shards: $gameShards a function in the long passes (LIFT_GAME_SHARDS)"; $env:LIFT_GAME_SHARDS_READ = $gameShards }
# (-Last 12 for the fuzz runs, 2026-11-12: with two or more DIFFERENT the "tested ... from the cache" line fell outside 8)
"== random test $(Get-Date)"; python tools\lift\lift.py fuzz 200000 2>&1 | Select-Object -Last 12
Remove-Item Env:LIFT_GAME_SHARDS_READ -ErrorAction SilentlyContinue
# (the long-run pass: functions the 40 s limit stopped short of 200,000 tests get 5 minutes each on their own -- the
#  standard is 200,000 for every proved function, 2026-11-08)
"== long tests $(Get-Date)"; python tools\lift\lift.py too-few "$work\too_few.txt" 2>&1 | Select-Object -Last 1
if ((Get-Item "$work\too_few.txt").Length -gt 0) {
    $env:LIFT_SUBSET = "$work\too_few.txt"; $env:FUZZ_MS = "300000"
    if ($gameShards) { $env:LIFT_GAME_SHARDS = $gameShards }      # (shardgame: K shards a function, all the workers)
    python tools\lift\lift.py fuzz 200000 2>&1 | Select-Object -Last 12
    Remove-Item Env:LIFT_SUBSET, Env:FUZZ_MS
    Remove-Item Env:LIFT_GAME_SHARDS -ErrorAction SilentlyContinue
}
# (the longer pass, 2026-11-11: those still short after 5 minutes get 20. 0.77.0 left 52 functions (7,784 instructions) short of
#  200,000 after the 5-minute pass; at the rates measured there, 32 of them (4,421) reach it in 4 times the time. The standard is
#  unchanged: they count only at 200,000 tests. The result cache keeps a line made with at least the time asked, so the next
#  run's 5-minute pass doesn't redo them.)
"== longer tests $(Get-Date)"; python tools\lift\lift.py too-few "$work\too_few.txt" 2>&1 | Select-Object -Last 1
if ((Get-Item "$work\too_few.txt").Length -gt 0) {
    # (2026-11-12, 0.81: each function's tests split over 4 tester processes with seeds of their own, lines added up -- lift.py
    #  fuzz_shards, the "tail" agent's, approved by the user 2026-11-12. 200,000 stays 200,000: a quarter each, made 4 at a time.
    #  So a slow function -- one whose tests are slow, not rare: func_801067B4_ovl_c proved with the old tools only at FUZZ_MS
    #  3,600,000 -- gets 4 times the tester time in the same 20 minutes. A quarter as many functions at once, so the pass
    #  takes no more of the PC than before.)
    if ($gameShards) {
        # (shardgame: K shards a function in one pool of all the workers -- see LIFT_GAME_SHARDS above)
        $env:LIFT_SUBSET = "$work\too_few.txt"; $env:FUZZ_MS = "1200000"; $env:LIFT_GAME_SHARDS = $gameShards
        python tools\lift\lift.py fuzz 200000 2>&1 | Select-Object -Last 12
        Remove-Item Env:LIFT_SUBSET, Env:FUZZ_MS, Env:LIFT_GAME_SHARDS
    } else {
    $env:LIFT_SUBSET = "$work\too_few.txt"; $env:FUZZ_MS = "1200000"; $env:LIFT_SHARDS = "4"
    $kept_workers = $env:LIFT_WORKERS
    $env:LIFT_WORKERS = "$([Math]::Max(1, [int](([Environment]::ProcessorCount - 4) / 4)))"
    python tools\lift\lift.py fuzz 200000 2>&1 | Select-Object -Last 12
    Remove-Item Env:LIFT_SUBSET, Env:FUZZ_MS, Env:LIFT_SHARDS
    if ($kept_workers) { $env:LIFT_WORKERS = $kept_workers } else { Remove-Item Env:LIFT_WORKERS }
    }
}
# (the longest pass, 2026-11-14, rest100: those still short after 4 x 20 minutes but with at least 40,000 plain tests -- a fifth
#  of the count or more -- get 4 x 90. Measured in rest100's tools copy on 0.82's short ones: modrep_func_801214C8,
#  func_8013CBAC, func_80027FD0, func_800EC9EC, func_80144CC4 proved so (1,449 instructions), and modrep_func_80020B58 too
#  once the reach pass after it gave back what the longer run's own seeds missed (761); func_80025F40 did not (151,898: a few
#  tests per shard are very slow). The standard is unchanged: 200,000 plain tests. The cache keeps the line, so a later run
#  with the same function doesn't spend the time again.)
"== longest tests $(Get-Date)"
$env:LIFT_TOO_FEW_AT_LEAST = "40000"; python tools\lift\lift.py too-few "$work\too_few.txt" 2>&1 | Select-Object -Last 1
Remove-Item Env:LIFT_TOO_FEW_AT_LEAST
if ((Get-Item "$work\too_few.txt").Length -gt 0) {
    if ($gameShards) {
        # (shardgame: K shards a function in one pool of all the workers -- see LIFT_GAME_SHARDS above)
        $env:LIFT_SUBSET = "$work\too_few.txt"; $env:FUZZ_MS = "5400000"; $env:LIFT_GAME_SHARDS = $gameShards
        python tools\lift\lift.py fuzz 200000 2>&1 | Select-Object -Last 12
        Remove-Item Env:LIFT_SUBSET, Env:FUZZ_MS, Env:LIFT_GAME_SHARDS
    } else {
    $env:LIFT_SUBSET = "$work\too_few.txt"; $env:FUZZ_MS = "5400000"; $env:LIFT_SHARDS = "4"
    $kept_workers = $env:LIFT_WORKERS
    $env:LIFT_WORKERS = "$([Math]::Max(1, [int](([Environment]::ProcessorCount - 4) / 4)))"
    python tools\lift\lift.py fuzz 200000 2>&1 | Select-Object -Last 12
    Remove-Item Env:LIFT_SUBSET, Env:FUZZ_MS, Env:LIFT_SHARDS
    if ($kept_workers) { $env:LIFT_WORKERS = $kept_workers } else { Remove-Item Env:LIFT_WORKERS }
    }
}
# (the reach pass: functions with no difference and an instruction no test got to get more runs from other starting points)
"== reach tests $(Get-Date)"; python tools\lift\lift.py reach 6 40000 2>&1 | Select-String -Pattern '^reach pass'
"== release $(Get-Date)"; python tools\lift\lift.py release 2>&1 | Select-Object -Last 6
"== names";          $names = python tools\lift\lift.py check-names 2>&1 | Select-Object -Last 3
$namesExit = $LASTEXITCODE
$names
# (2026-11-13, final082: the run STOPS here unless the names are proved to change nothing. check-names compiles lifted.cpp
#  with its names and comments and without, and compares the machine code byte for byte; on DIFFERENT a name or comment
#  changed the program -- fieldfix found one, 0.81's GAME_TABLE offsets as constexpr calls -- and the run used to go on and
#  build and deploy it. Nothing is built or deployed from here: lifted.cpp is the release just written, the deployed game
#  is untouched. Fix the names (or fields.json) and run check-names again. A check that couldn't run -- a compile error,
#  anything but "identical" -- stops it too: the names are then not proved either.)
if (($names -join "`n") -match 'DIFFERENT') { throw "check-names: DIFFERENT machine code with the names -- not building or deploying ($($names -join ' '))" }
if ($namesExit -or -not (($names -join "`n") -match '(?m)^identical')) { throw "check-names did not prove the names change nothing (exit $namesExit) -- not building or deploying" }
(Get-Item exe\WWFNoMercyRecomp\src\game\lifted.cpp).LastWriteTime = Get-Date   # (a copy's date would keep old objects)
# (2026-11-16, honesty: the release is two files -- lifted.cpp includes lifted_changed.inc, the functions the mod has a hand in --
#  so both are dated and backed up together)
if (Test-Path exe\WWFNoMercyRecomp\src\game\lifted_changed.inc) { (Get-Item exe\WWFNoMercyRecomp\src\game\lifted_changed.inc).LastWriteTime = Get-Date }
if ($Version) {
    New-Item -ItemType Directory -Force "$D\backups\lifted_$Version" | Out-Null
    Copy-Item exe\WWFNoMercyRecomp\src\game\lifted.cpp "$D\backups\lifted_$Version\" -Force
    if (Test-Path exe\WWFNoMercyRecomp\src\game\lifted_changed.inc) { Copy-Item exe\WWFNoMercyRecomp\src\game\lifted_changed.inc "$D\backups\lifted_$Version\" -Force }
    # (the version is stamped into mod.toml / the README at the start now, before the mod is built: pipefix 2026-11-17)
    # (the base game's preservation snapshot of this release, into Development\preservation\base_game_$Version: the original
    #  game's converted functions and the rest as MIPS and mechanical C, with the newest snapshot's stock proof. 2026-11-13, "preserve")
    # (convert2 2026-11-22: the stock translation is the one the exe build keeps, RecompiledFuncs.stock -- N64Recomp of the stock
    #  ROM with the port's fixers, as preserve.py's own was, with the port's hand edits, which preserve.py takes out (strip_hand_edits;
    #  checked against the 0.84 snapshot: the same text for all 469 of its not-converted functions). The new tree has no N64Recomp
    #  command-line build, so preserve.py could not make its own: 0.85's snapshot was not written.)
    "== preservation"; python tools\lift\preserve.py $Version --stock-translated exe\WWFNoMercyRecomp\RecompiledFuncs.stock 2>&1 | Select-Object -Last 1
}
# (the layout the converted code is made with: install.py checks a later layout change against it)
Copy-Item exe\WWFNoMercyRecomp\src\main\six_layout.h exe\WWFNoMercyRecomp\src\game\lifted_layout.h -Force
# (pipefix 2026-11-17: the mod was built at the start -- the build the copies were converted from. Not built again here: a
#  second build could differ from the one converted and tested, if the mod's source changed during the run. Checked instead.)
"== the mod build: still the one built at the start"
if ((($modFiles | ForEach-Object { (Get-FileHash $_).Hash }) -join ',') -ne $modBuildHash) {
    throw "the mod's build changed during the run (mod\build\mod_six): its copies were converted and tested from another build -- not building or deploying"
}
# (2026-11-14, modnative7) The native mod: the mod's own functions proved against Stage 1's translation of the build just made
#  (tools\modnative\pipeline.py: candidates, negative controls, lift.py prove "native"), then released into src\game\mod_native.cpp
#  and checked to be exactly the proved text (as check-names). A DIFFERENT check stops the run, as check-names does; a proof
#  that couldn't run leaves the last release, which aot.py links only for the mod build it was proved against.
"== native mod $(Get-Date)"
python tools\modnative\pipeline.py native 2>&1 | Select-Object -Last 4
# (merge084b, 2026-11-15, the coordinator's order: the bar is the two seeds AND the three all-shard seed families --
#  pipeline.py's report_native_fd / _fs / _ft, each given to release and check as one more --both. pipeline.py removes those
#  reports first, so when it stopped before they were made, nothing new is released: the last release stays, and aot.py
#  links it only for the mod build it was proved against.)
$natReports = @('report_native.txt', 'report_native_s2.txt', 'report_native_fd.txt', 'report_native_fs.txt', 'report_native_ft.txt') | ForEach-Object { "tools\lift\fuzz\$_" }
$natMissing = @($natReports | Where-Object { -not (Test-Path $_) })
if ((Test-Path tools\lift\fuzz\report_native.txt) -and $natMissing.Count) { "== native mod NOT released: no $($natMissing -join ', ') (the pipeline did not finish every seed run)" }
if (-not $natMissing.Count) {
    # (and under a second seed: a function must meet the bar in both runs, pipeline.py's prove_s2 -- 2026-11-14)
    # (2026-11-14, modwhole: $env:NM_NATIVE_WHOLE = '1' also releases the gameplay tier -- every other candidate that compiles,
    #  NOT proved, src\game\mod_native_whole.cpp, from pipeline.py's tools\modnative\whole\candidates.json (made apart from the proved
    #  ones) -- which the exe links only with $env:ARCADE_MOD_NATIVE_WHOLE = '1'. Off by default.)
    $wholeArgs = if ($env:NM_NATIVE_WHOLE -eq '1') { @('--whole', 'tools\modnative\whole\candidates.json') } else { @() }
    $bothArgs = @($natReports[1..4] | ForEach-Object { '--both', $_ })
    python tools\aot\mod_native.py release --report tools\lift\fuzz\report_native.txt @bothArgs @wholeArgs 2>&1 | Select-Object -Last 3
    $nat = python tools\aot\mod_native.py check --report tools\lift\fuzz\report_native.txt @bothArgs 2>&1 | Select-Object -Last 4
    $nat
    if (($nat -join "`n") -notmatch 'IDENTICAL') { throw "mod_native check: the shipped native mod is not the proved text -- not building or deploying" }
}
"== build exe $(Get-Date)"
$before = (Get-Item 'exe\build\Ultimate No Mercy.exe').LastWriteTime
$log = .\build_exe.ps1 2>&1
$after = (Get-Item 'exe\build\Ultimate No Mercy.exe').LastWriteTime
if ($after -eq $before) { $log | Select-String -Pattern 'error|FAILED' | Select-Object -First 8 | ForEach-Object { "  $_" }; throw 'exe build failed' }
# (the converted copies of the mod's are still the mod's copies as just built: otherwise the loader runs the mod's own and
#  they wait for the next conversion -- said, not stopped)
python tools\lift\lift.py check-modrep 2>&1 | Select-Object -Last 4
# The QA set (2026-11-14, fastnight; the user's ask: "make night runs much cheaper without weakening any proof").
# FULL -- the 14 tests at 6x, then at normal speed the start-up test, the ten-minute attract soak and three cold starts, the last
# of which deploys if all passed (deploy.hold still stops it) -- only for a LANDMARK CANDIDATE: a build whose score (lift.py
# score's consistent counted-once line, as the landmark install reads it) is at or past the next install landmark (93%, 97%,
# 100%: the first above the installed build's score, landmark_installed.txt), or when -FullQA / NIGHT_FULL_QA=1 forces it, or
# when the score or the installed build's can't be read (then nothing is taken on trust). A landmark install needs this set
# passed: it ends "== QA full set passed (exe <md5>)".
# CORE -- every other night: the start-up test at normal speed (never deploying: only a landmark build is installed) and, at 6x,
#   modes_regular_boot   power-on: the legal page, logos, intro movie, title, then Regular's menus -- the original game's start
#   hennig_win_taunt     a whole match to its end: the moves, the pin, the winner's taunt (the match code, ~20 s)
#   music_rumble_late    a Royal Rumble: entrants coming in one after another, their entrances and themes, the winner -- the
#                        six-man engine's Rumble code and the mod's media hooks
# and the start-up test itself is the arcade's attract loop, a coin and Start, and the one-player menu: the mod loaded and its
# own menus (every test runs the game with the mod and its native code). Measured on 0.82's run: the 14 at 6x took 18 minutes
# with 3 at once, these 3 about 4 (their longest, modes_regular_boot, 166 s). Scenarios a night's changes are known to touch can
# be added to either set: NIGHT_QA_EXTRA (comma-separated). Which scenario runs which converted code isn't recorded today, so
# the set can't follow the changed functions by itself.
$landmarks = @(93, 97, 100)
$score = $null
$scoreText = python tools\lift\lift.py score 2>&1
if ($NoQA) {
    $scoreText | Where-Object { $_ -notmatch '^  ' } | ForEach-Object { "  $_" }
    "== QA not run here (-NoQA): the build is ready for the QA set, run apart with the game slot $(Get-Date)"
    "== done $(Get-Date)"
    return
}
$line = $scoreText | Select-String -Pattern 'headline plus copies plus stubs: ([0-9.]+)%' | Select-Object -Last 1
if (-not $line) { $line = $scoreText | Select-String -Pattern 'headline plus copies: ([0-9.]+)%' | Select-Object -Last 1 }
if ($line) { $score = [double]$line.Matches[0].Groups[1].Value }
$installed = $null
if (Test-Path "$D\landmark_installed.txt") {
    $t = (Get-Content "$D\landmark_installed.txt" -TotalCount 1) -as [string]
    if ($t -match '^\s*([0-9.]+)') { $installed = [double]$Matches[1] }
}
$next = if ($null -ne $installed) { $landmarks | Where-Object { $_ -gt $installed } | Select-Object -First 1 } else { $null }
$why = if ($FullQA -or $env:NIGHT_FULL_QA -eq '1') { 'forced (-FullQA / NIGHT_FULL_QA)' }
       elseif ($null -eq $score) { "the score couldn't be read" }
       elseif ($null -eq $installed) { "the installed build's score couldn't be read (landmark_installed.txt)" }
       elseif ($null -eq $next) { $null }      # (past the last landmark: no install is left to make)
       elseif ($score -ge $next) { "LANDMARK CANDIDATE: $score% is at or past the next landmark, $next% (installed: $installed%)" }
       else { $null }
$extra = @(if ($env:NIGHT_QA_EXTRA) { $env:NIGHT_QA_EXTRA -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ } })
$parallel = $(if ($env:QA_PARALLEL) { [int]$env:QA_PARALLEL } else { 2 })
(Get-Process -Id $PID).PriorityClass = 'Normal'
# (2026-11-17, qa12x, folded by pipefix) The fast tests at 12x: the game's clock in lockstep (QA_LOCKSTEP=1: a frame the PC is
#  late for comes late, never skipped), the menus at the match's speed (QA_MENU_SPEED=12) and only the captured pictures drawn
#  (QA_DRAW_SHOTS=1) -- qa.ps1 -Lockstep / -MenuSpeed / -DrawShots. Proved with the lockstep hashes: smoke_singles,
#  menu_from_attract and music_rumble_late played 1x and 12x THE SAME on every retrace (33 structures + the whole memory +
#  pictures, pads, host calls); the 14 below all PASS so (no2k15, 2026-11-23: 13 -- hd_select went with the 2K15 slots), as at 6x (old scratch\agents\qa12x\STATE.md). Only these 14 (and the
#  core 3 among them) run so; a scenario added by NIGHT_QA_EXTRA runs at 6x as before. The normal-speed tests have none of it.
$fastProved = @('modes_regular_boot','modes_reg_rumble_rows','menu_from_attract','hennig_win_taunt','story_upgrade_b','tron_entrances','picture_walk','announcers_front','music_rumble_late','versus_1p','blocks_hogan','rumble_ring10','rumble_ring32')
function Invoke-FastSet([string[]]$tests) {
    $at12 = @($tests | Where-Object { $fastProved -contains $_ }); $at6 = @($tests | Where-Object { $fastProved -notcontains $_ })
    $out = @()
    if ($at12.Count) {
        $env:QA_LOCKSTEP = '1'; $env:QA_MENU_SPEED = '12'; $env:QA_DRAW_SHOTS = '1'
        try { $out += .\qa_set.ps1 -Speed 12 -Parallel $parallel -Tests $at12 2>&1 | Where-Object { $_ -notmatch '  ok$' } }
        finally { Remove-Item Env:QA_LOCKSTEP, Env:QA_MENU_SPEED, Env:QA_DRAW_SHOTS -ErrorAction SilentlyContinue }
    }
    if ($at6.Count) { $out += .\qa_set.ps1 -Speed 6 -Parallel $parallel -Tests $at6 2>&1 | Where-Object { $_ -notmatch '  ok$' } }
    $out
}
Remove-Item Env:QA_LOCKSTEP, Env:QA_MENU_SPEED, Env:QA_DRAW_SHOTS -ErrorAction SilentlyContinue
if ($why) {
    "== QA: the full set -- $why $(Get-Date)"
    "== fast tests (12x lockstep; extras 6x) $(Get-Date)"
    $all = $fastProved
    $r = Invoke-FastSet @($all + ($extra | Where-Object { $all -notcontains $_ }))
    $r
    if ($r -match 'not passing') { 'not all passed fast: not deploying'; exit 0 }
    "== start-up at normal speed $(Get-Date)"
    $r = .\qa_set.ps1 -Tests menu_from_attract 2>&1 | Where-Object { $_ -notmatch '  ok$' }
    $r
    if ($r -match 'not passing') { 'the start-up test did not pass: not deploying'; exit 0 }
    "== attract soak at normal speed $(Get-Date)"
    $r = .\qa_set.ps1 -Tests attract_soak 2>&1 | Where-Object { $_ -notmatch '  ok$' }
    $r
    if ($r -match 'not passing') { 'the attract soak did not pass: not deploying'; exit 0 }
    foreach ($i in 1..2) {
        "== cold start $i $(Get-Date)"
        $r = .\qa_set.ps1 -Tests menu_from_attract 2>&1 | Where-Object { $_ -notmatch '  ok$' }
        $r
        if ($r -match 'not passing') { "cold start $i did not pass: not deploying"; exit 0 }
    }
    "== cold start 3 (deploys if it passes) $(Get-Date)"
    $r = .\qa_set.ps1 -Tests menu_from_attract -Deploy 2>&1 | Where-Object { $_ -notmatch '  ok$' }
    $r
    if ($r -match 'not passing') { 'cold start 3 did not pass: not deploying'; exit 0 }
    $md5 = python -c "import hashlib,sys; print(hashlib.md5(open(sys.argv[1],'rb').read()).hexdigest())" 'exe\build\Ultimate No Mercy.exe'
    "== QA full set passed (exe $md5) $(Get-Date)"
} else {
    "== QA: the core set -- score $score%, next landmark $(if ($null -ne $next) { "$next%" } else { 'none left' }), installed $installed% $(Get-Date)"
    "== fast tests (12x lockstep; extras 6x) $(Get-Date)"
    $core = @('modes_regular_boot','hennig_win_taunt','music_rumble_late')
    $r = Invoke-FastSet @($core + ($extra | Where-Object { $core -notcontains $_ }))
    $r
    if ($r -match 'not passing') { 'not all passed fast'; exit 0 }
    # (never -Deploy here: a build that isn't a landmark candidate is built and tested, not installed)
    "== start-up at normal speed (core set: not deploying) $(Get-Date)"
    .\qa_set.ps1 -Tests menu_from_attract 2>&1 | Where-Object { $_ -notmatch '  ok$' }
}
"== done $(Get-Date)"
