# merge084 (2026-11-15): the full pipeline for 0.84.0, as merge083's run_083.ps1 ran 0.83: after apply_merge084.ps1 -Apply.
# 0.84: narrow cache keys + QA tiers (fastnight; the FIRST 0.84 night runs everything once: the key format changed); the replay
# tool (lockstep, record / replay, per-retrace hashes, headless: all behind environment variables); names 084b + one fields.json
# about; LiftWord, shard seeds; N64Recomp's unordered compares (Stage 1 made again by the fixed recompiler: tools\aot\aot.py
# rebuilds its tool from the fixed library) + the tester's steering for them; modwhole's whole-mod native build (gameplay tier:
# made by the night, linked only with ARCADE_MOD_NATIVE_WHOLE=1 -- NOT in this run's exe), its converter changes and the
# re-aimed negative controls; revfix ("replaces", F2 removals, commentary = WWE '13's lines, select_pages.json, QA ignores
# physical pads, handlers) + skinqa's skin step; portable (no ROM words in the exe, roms\ folder, first-start hook, "Ultimate
# No Mercy").
# merge084b (2026-11-15, the staged set staged_084b): + rules084 as reviewed by reviewr084 (checker rules T -- with the review's
# fix -- Q, L, U, I's words, "a break ends the way"; the tester's step limit, strict, and the write cut for func_80008D6C; rule P
# NOT merged: dropped by the user), + modnative7/8's 0.84 native-mod tester batch (approved by reviewnm084), + gameplayfix (the
# Rumble hands crash, Rumble meters, NUMBER rows, ring-void guard), visualfix (plane layers, edge clamp, mat logo, Rumble start,
# NUMBER layout) and the arena labels in the bundled Anton font. THE APPROVED SWITCHES ARE ON in this run (below); rule P is
# kept off explicitly.
# deploy.hold stays on: night.ps1's -Deploy can't install. A landmark install (93% / 97% / 100%) is a separate step after it:
# landmark_084.ps1. "== wrapper end" = returned or threw.
#   Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',
#       '<Development>\scratch\agents\merge084\run_084.ps1','-Log','<Development>\scratch\lift\run_084_<stamp>.log'
# -NoWhole: the night without the gameplay tier (NM_NATIVE_WHOLE unset: as 0.83's night).
# -NoNativeRules: modnative7/8's 0.84 native-mod rules OFF in the native step (by default ON since merge084b: NM_PURE_THRU=1,
#   NM_DECLARED_PARAMS=1, both approved by reviewnm084; the guard and the aligned rule are on by default in its tester).
#   (-NativeRules, the earlier switch, is still taken and changes nothing: they are on.)
# -NoRules: the reviewed checker / tester switches OFF (the 0.84 tools then test and count exactly as with no rules084 change).
# (newhome 2026-11-17: kept in the project as tools\lift\run_night.ps1 -- it was scratch\agents\merge084\run_084.ps1 -- with the
#  version a parameter and the log's default in scratch\lift. Start it hidden and watch its process ID (the user's rule):
#    bash: env -u PSModulePath powershell.exe -NoProfile -Command "Start-Process powershell -WindowStyle Hidden -PassThru
#          -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','tools\lift\run_night.ps1','-Version','0.85.0'"
#  The list of approved switches below changes only with the user's approval.)
# -GameShards K (shardfold 2026-11-17): LIFT_GAME_SHARDS for the long passes -- 4 by default, as the user approved (2026-11-17)
#   and the review (reviewsh: APPROVE WITH FIXES, fixes folded) recommends; -GameShards off: unset, the passes as before 0.85.
#   night.ps1 refuses anything but a whole number 2 to 64.
param([Parameter(Mandatory)][string]$Version, [string]$Log = '', [int]$Workers = 16, [switch]$NoWhole, [switch]$NativeRules, [switch]$NoNativeRules, [switch]$NoRules,
      [string]$GameShards = '4', [switch]$NoQA)
# -NoQA (convert 2026-11-22): night.ps1 -NoQA -- the conversion, release, names check, native mod, build and score; the QA set
#   is run apart by the agent holding the shared game slot.
$D = (Resolve-Path "$PSScriptRoot\..\..").Path     # the source tree (the old Development folder's layout)
Set-Location $D
if (-not $Log) { $Log = Join-Path $D ("scratch\lift\run_{0}_{1}.log" -f ($Version -replace '\.', ''), (Get-Date -Format yyyyMMdd_HHmm)) }
New-Item -ItemType Directory -Force (Split-Path $Log -Parent) | Out-Null
# (2026-11-15: started from PowerShell 7, this Windows PowerShell inherits 7's module path, and Get-FileHash -- which the build
#  and QA scripts use -- is missing: what stopped the 0.83 run's first start at 19:09. Its own modules first, whoever starts it.)
$env:PSModulePath = "$PSHOME\Modules;$env:PSModulePath"
Import-Module Microsoft.PowerShell.Utility -Force -ErrorAction SilentlyContinue
"== wrapper start $(Get-Date) pid $PID" | Out-File -FilePath $Log -Encoding utf8 -Append
try {
    if (-not (Test-Path "$D\deploy.hold")) { throw 'deploy.hold is missing: not running' }
    Remove-Item Env:LIFT_NO_CACHE -ErrorAction SilentlyContinue
    $env:QA_PARALLEL = '3'
    $env:LIFT_WORKERS = "$Workers"         # (merge083's measurement, logs\workers.txt there: 16 at low priority)
    $env:LIFT_PRIORITY = 'low'
    # (modwhole: the gameplay tier is made and released apart -- src\game\mod_native_whole.cpp -- for the A/B runs that decide
    #  whether it ships; the exe links it only with ARCADE_MOD_NATIVE_WHOLE=1, which this run does not set)
    if ($NoWhole) { Remove-Item Env:NM_NATIVE_WHOLE -ErrorAction SilentlyContinue } else { $env:NM_NATIVE_WHOLE = '1' }
    Remove-Item Env:ARCADE_MOD_NATIVE_WHOLE -ErrorAction SilentlyContinue
    if (-not $NoNativeRules) { $env:NM_PURE_THRU = '1'; $env:NM_DECLARED_PARAMS = '1' }
    else { Remove-Item Env:NM_PURE_THRU, Env:NM_DECLARED_PARAMS -ErrorAction SilentlyContinue }
    # (merge084b: the switches the independent review reviewr084 APPROVED, with the exact names of rules084\INTEGRATION.md --
    #  checker rules: unreach.py; step limit and write cut: lift.py / the tester. Rule P: REJECTED, and dropped by the user for
    #  good -- its code is not in 0.84 at all; LIFT_RULE_P is removed here in case an old shell has it set.)
    # (argcheck, 2026-11-16: the strict read-set checker with rule B, and the tester's argument compare -- approved by the user
    #  2026-11-16; reviewac's review APPROVE WITH FIXES, fixes done (scratch\agents\argcheck\FIXES_DONE.txt); they count only
    #  once the re-check passes, which the coordinator confirms before this run. ALWAYS BOTH: lift.py refuses to score
    #  FUZZ_ARGS_STRICT=1 without LIFT_CHECK_READS=1. FUZZ_ARGS_STRICT retests every function (its keys all change).)
    # (convert2 2026-11-22: + LIFT_RULE_RANGES, the checker rule "path ranges" -- approved by the user 2026-11-22 ("Use your
    #  recommendations"), reviewed by reviewranges: APPROVE, notes\REVIEW_RANGES.txt. Its claims are in each function's key.)
    $approved = 'LIFT_RULE_T', 'LIFT_RULE_Q', 'LIFT_RULE_L', 'LIFT_RULE_U', 'LIFT_RULE_I_WORDS', 'LIFT_RULE_BREAK', 'LIFT_RULE_RANGES', 'LIFT_STEP_LIMIT', 'LIFT_WRITE_CUT',
                'LIFT_CHECK_READS', 'FUZZ_ARGS_STRICT'
    foreach ($s in $approved) { if ($NoRules) { Remove-Item "Env:$s" -ErrorAction SilentlyContinue } else { Set-Item "Env:$s" '1' } }
    Remove-Item Env:LIFT_RULE_P, Env:FUZZ_STEP_MIXED_ASIDE -ErrorAction SilentlyContinue
    # (shardfold: the game-function shards of the long passes, LIFT_GAME_SHARDS -- see night.ps1; LIFT_SHARDS is never set with it)
    Remove-Item Env:LIFT_SHARDS, Env:LIFT_GAME_SHARDS_READ -ErrorAction SilentlyContinue
    if ($GameShards -eq 'off' -or $GameShards -eq '') { Remove-Item Env:LIFT_GAME_SHARDS -ErrorAction SilentlyContinue } else { $env:LIFT_GAME_SHARDS = $GameShards }
    "== LIFT_WORKERS $Workers, LIFT_PRIORITY low, NM_NATIVE_WHOLE $($env:NM_NATIVE_WHOLE), native rules $(-not $NoNativeRules), reviewed rules $(if ($NoRules) { 'OFF' } else { 'ON: ' + ($approved -join ' ') }); LIFT_RULE_P unset; LIFT_GAME_SHARDS $(if ($env:LIFT_GAME_SHARDS) { $env:LIFT_GAME_SHARDS } else { 'unset' })" | Out-File -FilePath $Log -Encoding utf8 -Append
    & { .\tools\lift\night.ps1 -Version $Version -NoQA:$NoQA } 2>&1 | Out-File -FilePath $Log -Encoding utf8 -Append
    "== wrapper end (returned) $(Get-Date)" | Out-File -FilePath $Log -Encoding utf8 -Append
} catch {
    "== wrapper end (THREW) $(Get-Date): $_" | Out-File -FilePath $Log -Encoding utf8 -Append
}
