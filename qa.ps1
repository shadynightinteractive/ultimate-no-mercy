# QA: runs a scenario in the real game -- the exe and mod just built, in a copy of the game (qa\game; the game
# itself is never touched) -- and checks what happened.
#   .\qa.ps1 <scenario>            qa\scenarios\<scenario>.json (see qa\qa_tool.py for the format)
#   .\qa.ps1 <scenario> -Installed the game's own exe and mod instead of the ones just built
# Out: qa\runs\<date>_<time>_<scenario>\  report.txt (PASS / FAIL, each check), contact.png (the screenshots),
#      shots\*.png, state.csv (the game's state 20x a second), game_stdout.txt / game_stderr.txt (its log).
# The game's window opens while it runs; nothing else needs doing.
#   .\qa.ps1 <scenario> -Speed 6   fast-forward: the game runs 6 times as fast (WCW_SPEED; the scenario's "speed", or
#                                  QA_SPEED, else 1); its "seconds" are the game's, the run takes seconds / speed
#   .\qa.ps1 <scenario> -Copy game2 the copy of the game it runs in (qa\game2): two tests at once, each in its own
#   .\qa.ps1 <scenario> -Exe <path> another build's exe (a second build folder: tried while the first is under test)
#   .\qa.ps1 <scenario> -Speed 12 -MenuSpeed 12   the menus, start-up and entrances at that speed too (WCW_SPEED_MENUS; 2 unless
#                                  set, at most -Speed; QA_MENU_SPEED the same)
#   .\qa.ps1 <scenario> -DrawShots  draw only what's captured: the game draws one picture each "shots" seconds of its own and no
#                                  others, and keeps each as the shot (shots\<game seconds>s.bmp; WCW_QA_DRAW_EVERY,
#                                  WCW_NATIVE_SHOTS); its logic runs every frame as ever. QA_DRAW_SHOTS=1 the same. (2026-11-17)
#   .\qa.ps1 <scenario> -Lockstep   the game's clock in lockstep (NM_LOCKSTEP=1, ultramodern lockstep.hpp): a frame the PC is too
#                                  slow for comes late (slow motion), never skipped, so the game plays the same at any speed;
#                                  paced to the wall clock times the speed. QA_LOCKSTEP=1 the same. (2026-11-17)
#   .\qa.ps1 -Record               no scenario: the game as it is (coins, menus, your settings) for YOU to play, recorded
#                                  -- the state, a screenshot every 10 s, and at once when a model goes bad -- until you
#                                  close it; then the report. For bugs that need a person playing to show up.
param([string]$Scenario = '', [switch]$Installed, [switch]$Record, [int]$Speed = 0, [string]$Copy = 'game', [string]$Exe = '', [string]$ModDir = '',
      [int]$MenuSpeed = 0, [switch]$DrawShots, [switch]$Lockstep)
# (-ModDir: another copy of the mod folder, built with build_mod.ps1 -ModDir -- work on the mod beside a pipeline run, 2026-11-07)
$ModRoot = if ($ModDir) { $ModDir } else { "$PSScriptRoot\mod" }
$env:QA_MOD_DIR = $(if ($ModDir) { $ModDir } else { "" })   # (qa_tool.py: that mod's mod.toml for the defaults)
if (-not $Record -and -not $Scenario) { throw "a scenario, or -Record" }
if ($Record) { $Scenario = "$PSScriptRoot\qa\scenarios\_record.json" }
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\toolchain\env.ps1"
# (newhome 2026-11-17: the game is the folder "game" beside this source tree -- ..\game -- or $env:UNM_GAME; it used to be
#  the folder above Development)
$game = $(if ($env:UNM_GAME) { $env:UNM_GAME } else { Join-Path (Split-Path $PSScriptRoot -Parent) 'game' })
$qa = "$PSScriptRoot\qa"
$scn = if (Test-Path $Scenario) { (Resolve-Path $Scenario).Path } else { "$qa\scenarios\$Scenario.json" }
if (-not (Test-Path $scn)) { throw "no scenario $scn" }
$s = Get-Content $scn -Raw | ConvertFrom-Json
$name = [IO.Path]::GetFileNameWithoutExtension($scn)
$run = "$qa\runs\$(Get-Date -Format yyyy-MM-dd_HHmmss)_$name"
New-Item -ItemType Directory -Force "$run\shots" | Out-Null
# (not while the user's own game is open, nor in a copy another test is using; another copy's test is fine)
# (2026-11-15, merge084: any copy under qa\ is a test's copy -- agents' copies are named for them, not game* (agent replay found
#  that while its runs in the copy qa\replay went on, every other qa.ps1 refused); only a game running from outside qa\ is the user's)
if (Get-Process -Name 'Ultimate No Mercy', 'NoMercy Arcade' -ErrorAction SilentlyContinue | Where-Object { $_.Path -notlike "$qa\*" }) { throw "the game is running: close it first" }
if (Get-Process -Name 'Ultimate No Mercy', 'NoMercy Arcade' -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$qa\$Copy\*" }) { throw "a test is running in qa\$Copy" }

# the copy of the game: what the game folder has (not Development, saves kept as they are), then the new build
$copy = "$qa\$Copy"
robocopy $game $copy /MIR /XD "$game\Development" "$game\crash_reports" /XF NoMercyRecompiled*.log /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "copying the game failed (robocopy $LASTEXITCODE)" }
Remove-Item "$copy\crash_reports" -Recurse -Force -ErrorAction SilentlyContinue   # (this run's reports only)
New-Item -ItemType Directory -Force "$copy\crash_reports" | Out-Null
# (Not the user's controller layouts in a scripted test: with the layout set up 2026-11-08 19:57 and that pad plugged in, C-right never registered on the
#  Superstar Select and hd_select failed 11 runs of 11, 0.69.0's build too; with this file gone it passed. -Record is the user playing: theirs stays.)
if (-not $Record) { Remove-Item "$copy\controller_map.txt" -Force -ErrorAction SilentlyContinue }
# (QA_SET_ASIDE, to find what in the user's game folder changes a test: ';'-separated paths in the copy, each removed, or "path=file" replaced
#  by that file. The copy only; off when unset. 2026-11-08)
# (a scenario's own "set_aside": the same, a list in its file -- e.g. the save, for the game's first start with none:
#  first_boot_nosave, 2026-11-12)
$asides = @($env:QA_SET_ASIDE -split ';') + @((Get-Content $scn -Raw | ConvertFrom-Json).set_aside)
foreach ($e in ($asides | Where-Object { $_ })) {
    $p, $from = $e -split '=', 2
    if ($from) { Copy-Item $from "$copy\$p" -Force } else { Remove-Item "$copy\$p" -Force -ErrorAction SilentlyContinue }
}
if (-not $Installed) {
    $nrm = "$ModRoot\build\mod_six\nomercy_arcade.nrm"; $exe = if ($Exe) { $Exe } else { "$PSScriptRoot\exe\build\Ultimate No Mercy.exe" }   # (-Exe: another builds)
    foreach ($f in $nrm, $exe) { if (-not (Test-Path $f)) { throw "not built: $f (.\build_mod.ps1 / .\build_exe.ps1)" } }
    Copy-Item $nrm "$copy\assets\mods\" -Force; Copy-Item $nrm "$copy\mods\" -Force
    # (the HD pictures of a model drawn whole, src/hd.c: texture packs, each a folder in the game's mods)
    # (each pack mirrored, not added to: RT64 takes a .dds over a .png of the same name, so an old file left there wins)
    foreach ($pk in Get-ChildItem "$ModRoot\texturepack" -Directory -ErrorAction SilentlyContinue) { robocopy $pk.FullName "$copy\mods\$($pk.Name)" /MIR /NFL /NDL /NJH /NJS /NP | Out-Null; $global:LASTEXITCODE = 0 }
    Copy-Item $exe "$copy\Ultimate No Mercy.exe" -Force
    # the custom stories (the exe compiles <program folder>\stories\*.json; _roster.json: the names) from build_mod.ps1
    New-Item -ItemType Directory -Force "$copy\stories" | Out-Null
    if (Test-Path "$ModRoot\build\stories") { Copy-Item "$ModRoot\build\stories\*" "$copy\stories\" -Force }
    # the commentary pack, when one is built (compak.py pack -> mod\build; deploy.ps1 -Commentary puts it in the game)
    if (Test-Path "$ModRoot\build\commentary.pak") {
        Remove-Item "$copy\commentary.pak*" -Force -ErrorAction SilentlyContinue
        Copy-Item "$ModRoot\build\commentary.pak*" $copy -Force
    }
    # the Superstar Select's pages (the exe reads <program folder>\select_pages.json; mod\select_pages.json ships)
    if (Test-Path "$ModRoot\select_pages.json") { Copy-Item "$ModRoot\select_pages.json" $copy -Force }
    # (automerge, 2026-11-23) MERGE SAME WRESTLERS' data, the roster editor's own (one source: the private folder's editor\data),
    #  as make_package.ps1 puts it in the game's data folder
    $ed = @($env:UNM_PRIVATE, (Join-Path (Split-Path $PSScriptRoot -Parent) 'private'), (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent)) |
        Where-Object { $_ -and (Test-Path "$_\editor\data\aliases.json") } | Select-Object -First 1
    if ($ed) {
        New-Item -ItemType Directory -Force "$copy\data" | Out-Null
        foreach ($d in 'aliases.json', 'builtin_roster.json') { Copy-Item "$ed\editor\data\$d" "$copy\data\$d" -Force }
    }
}
# a scenario's own pages file ("pages": a file in qa\pages_test, or "none": no file -- the game's own pages)
$scnPages = (Get-Content $scn -Raw | ConvertFrom-Json).pages
if ($scnPages -eq 'none') { Remove-Item "$copy\select_pages.json" -Force -ErrorAction SilentlyContinue }
elseif ($scnPages) { Copy-Item "$qa\pages_test\$scnPages" "$copy\select_pages.json" -Force }
# a scenario's own test stories ("stories": file names in qa\stories_test -- for that run only: /MIR clears them next time)
$scnStories = (Get-Content $scn -Raw | ConvertFrom-Json).stories
foreach ($st in @($scnStories)) { if ($st) { Copy-Item "$qa\stories_test\$st" "$copy\stories\" -Force } }
python "$qa\qa_tool.py" config $scn $copy
if ($LASTEXITCODE) { throw "the scenario's settings failed" }
Copy-Item $scn "$run\scenario.json"
# (the mods the game should load, from the copy's mods folder and mods.json as they are before it starts: the report checks
#  each logged "Loading mod" -- twice in 1,118 runs one was never loaded and nothing noticed, 2026-11-12)
python "$qa\qa_tool.py" mods $copy $run

# screenshots of the game's window: PrintWindow (whole content, even behind other windows)
# (plain GDI: PowerShell 7's System.Drawing isn't whole; a .bmp, which qa_tool.py turns into a .png)
if (-not ('QaShot' -as [type])) { Add-Type -TypeDefinition @'
using System; using System.IO; using System.Runtime.InteropServices;
public static class QaShot {
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern bool IsHungAppWindow(IntPtr h);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleBitmap(IntPtr dc, int w, int h);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
    [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr o);
    [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);
    [DllImport("gdi32.dll")] static extern int GetDIBits(IntPtr dc, IntPtr bmp, uint start, uint lines, byte[] bits, byte[] bmi, uint usage);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public static bool Save(IntPtr h, string path) {
        SetProcessDPIAware();
        if (IsHungAppWindow(h)) return false;   // (PrintWindow waits on the window: a game making no frames would stop the run)
        RECT r; if (!GetClientRect(h, out r)) return false;
        int w = r.R - r.L, ht = r.B - r.T; if (w < 16 || ht < 16) return false;
        IntPtr wdc = GetDC(h), mdc = CreateCompatibleDC(wdc), bmp = CreateCompatibleBitmap(wdc, w, ht), old = SelectObject(mdc, bmp);
        bool ok = PrintWindow(h, mdc, 3);   // (1 client area | 2 full content: what DWM shows, D3D included)
        SelectObject(mdc, old);
        byte[] bits = new byte[w * ht * 4], bmi = new byte[40];
        BitConverter.GetBytes(40).CopyTo(bmi, 0); BitConverter.GetBytes(w).CopyTo(bmi, 4); BitConverter.GetBytes(ht).CopyTo(bmi, 8);
        BitConverter.GetBytes((short)1).CopyTo(bmi, 12); BitConverter.GetBytes((short)32).CopyTo(bmi, 14);
        if (ok) ok = GetDIBits(mdc, bmp, 0, (uint)ht, bits, bmi, 0) == ht;
        DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(h, wdc);
        if (!ok) return false;
        using (var f = new BinaryWriter(File.Create(path))) {
            f.Write((byte)'B'); f.Write((byte)'M'); f.Write(54 + bits.Length); f.Write(0); f.Write(54);
            f.Write(bmi); f.Write(bits);
        }
        return true;
    }
}
'@ }

$secs = if ($s.seconds) { [int]$s.seconds } else { 90 }
$every = if ($s.shots) { [double]$s.shots } else { 2.0 }
# fast-forward: -Speed, else the scenario's "speed", else QA_SPEED, else 1 (a recording is always at the real pace). The
# scenario's seconds and shot interval are the game's; the run's are those over the speed.
if (-not $Speed) { $Speed = if ($s.speed) { [int]$s.speed } elseif ($env:QA_SPEED) { [int]$env:QA_SPEED } else { 1 } }
if ($Record) { $Speed = 1 }
$Speed = [Math]::Max(1, [Math]::Min(16, $Speed))
$gameSecs = $secs
# (the exe runs a match at -Speed and the rest at WCW_SPEED_MENUS: with fast-forward WCW_QUIT_AFTER counts the game's
#  seconds, so it gets the scenario's own; the run's own limit below is for the slowest case, all at the menus' speed)
# (2026-11-17, qa12x: -MenuSpeed / QA_MENU_SPEED, the menus' speed, up to -Speed; 2 unless set, as before. Measured at 12x:
#  with -Lockstep and -DrawShots the menus keep up, 12.0x; without them 5.6x -- reading every frame's lists on the game's
#  thread, and the retraces Windows' sleeps miss -- which is why 2 was the safe default)
if (-not $MenuSpeed -and $env:QA_MENU_SPEED) { $MenuSpeed = [int]$env:QA_MENU_SPEED }
$menuSpeed = if ($Speed -le 1) { 1 } elseif ($MenuSpeed -gt 0) { [Math]::Max(1, [Math]::Min($Speed, $MenuSpeed)) } else { [Math]::Min(2, $Speed) }
$secs = if ($Speed -gt 1) { [int][Math]::Ceiling($gameSecs / $menuSpeed) } else { $gameSecs }
$everyGame = $every
$every = $every / $Speed
# The scenario's seconds counted in the game's own pictures, not its clock (2026-11-12). The game's clock runs at the speed
# asked for whether or not the game keeps up, and on a busy PC the game skips what it has no time for (0.77's tests: ~30
# updates a second for 58): WCW_QUIT_AFTER came with a fifth to a half of the game played, and checks failed for want of
# time (versus_1p, story_upgrade_b, rumble_ring32, blocks_hogan). So the run ends when the game has made the pictures its
# seconds are at full pace (state.csv "swaps", the pictures the game finished: 30 a second of its clock at full pace, its
# logic frames -- qa_tool.py PICTURES_A_SECOND): qa.ps1 closes the window then (the game quits as when a player closes
# it). Not "frame" (the mod's count of the video interrupts): held to one CPU beside two busy loops the game took every
# interrupt (100% of them) and finished only 37% of its pictures, and played two matches where it plays four. WCW_QUIT_AFTER
# stays, $stretch times the seconds, as the backstop: a game slower than 1 / $stretch of its pace still ends on the clock,
# and the report calls that run STARVED. On a quiet PC the pictures are made a little before the clock's end (it gave
# 300 to 1,600 frames more: a share of a short scenario's time), and no check was first met that late: in 61 scenarios'
# newest passes that ended on the clock, every check was first true within 74% of its seconds (qa agent's margin_all.py).
# ("end": "clock" in a scenario: the old way, the clock alone -- WCW_QUIT_AFTER's end also saves state.rdram.bin, which
# qa\pages_dump.py reads; QA_STRETCH sets the backstop; a recording is the clock's, it ends when you close it.)
$picBudget = if ($Record -or $s.end -eq 'clock') { 0 } else { $gameSecs * 30 }
$stretch = if ($picBudget -and $env:QA_STRETCH) { [Math]::Max(1.0, [double]$env:QA_STRETCH) } elseif ($picBudget) { 3.0 } else { 1.0 }
$env:WCW_SPEED = "$Speed"
if ($Speed -gt 1) { $env:WCW_SPEED_MENUS = "$menuSpeed" } else { Remove-Item Env:WCW_SPEED_MENUS -ErrorAction SilentlyContinue }
# (2026-11-17, qa12x: draw only what's captured -- the game draws a picture each "shots" seconds of its own, keeps it as the
#  shot and draws nothing else; its logic is the same frame for frame (lockstep hashes, scratch\agents\qa12x). This loop's own
#  timed shots are then not taken; the moments it finds (_WHEN, _BAD) still are, of the picture last drawn. Not for -Record.)
#  (no closer together than this loop could ever take them at this speed -- a look every 0.1 s of the wall clock, so 1.2 s of
#  the game at 12x: a scenario's "shots" 0.5 would otherwise make twice today's pictures, and each costs the report ~0.2 s)
$drawEvery = [Math]::Max($everyGame, 0.1 * $Speed)
if (-not $Record -and ($DrawShots -or $env:QA_DRAW_SHOTS -eq '1')) { $DrawShots = $true; $env:WCW_QA_DRAW_EVERY = $drawEvery.ToString([Globalization.CultureInfo]::InvariantCulture); $env:WCW_NATIVE_SHOTS = "$run\shots" }
else { $DrawShots = $false; Remove-Item Env:WCW_QA_DRAW_EVERY, Env:WCW_NATIVE_SHOTS -ErrorAction SilentlyContinue }
# (2026-11-17, qa12x: lockstep -- measured at -Speed 12: without it Windows' millisecond sleeps held a match to ~7.5x and the game
#  skipped the retraces it was late for (a different match from 1x); with it, 12.0x, and the same game as at 1x frame for frame)
$lockstepWas = $env:NM_LOCKSTEP
if (-not $Record -and ($Lockstep -or $env:QA_LOCKSTEP -eq '1')) { $Lockstep = $true; $env:NM_LOCKSTEP = '1' } else { $Lockstep = $false }
$env:WCW_QA_STATE = "$run\state.csv"
$env:WCW_QUIT_AFTER = "$([int][Math]::Ceiling($gameSecs * $stretch))"   # (from the game's first frame: real seconds, or with fast-forward the game's; the hard stop below allows for start-up)
$env:WCW_GUARD_MOD = '1'   # (the arcade's packs read-only: a write there is a crash report naming the writer)
# (the arcade's chosen words watched with the CPU's debug registers -- a scenario's, not a recording's: a write to one is in
# the log with what wrote it)
if ($Record) { Remove-Item Env:WCW_WATCH_MOD -ErrorAction SilentlyContinue } else { $env:WCW_WATCH_MOD = '1' }
if (Test-Path "$copy\qa_pads.txt") { $env:WCW_VPADS = '2'; $env:WCW_VPADS_SCRIPT = "$copy\qa_pads.txt" }
else { Remove-Item Env:WCW_VPADS, Env:WCW_VPADS_SCRIPT -ErrorAction SilentlyContinue }
# (2026-11-15, revfix) A scripted test's pads are the virtual ones only: every physical controller is ignored (SDL's
#  SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, naming only the virtual pads' vendor / product 0x0000 / 0x0000), so a controller
#  the user has plugged in can't take player 1 and leave the script's presses unread (the select tours rf_pages_after
#  2026-11-15_052007 FAIL with one plugged in, rf_pages_after_nopads 055458 PASS with this). Not for -Record (the user plays);
#  a scenario's own "env" may set it otherwise; QA_KEEP_PADS=1 keeps the physical ones.
if (-not $Record -and -not $env:QA_KEEP_PADS) { $env:SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT = '0x0000/0x0000' }
else { Remove-Item Env:SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT -ErrorAction SilentlyContinue }
# (a scenario's own "env": variables for the game, set for this run only -- e.g. SDL_GAMECONTROLLER_IGNORE_DEVICES with the
#  PC's own controller in it, so the scripted pad 1 is player 1: with that pad plugged in, pad 1 was player 2 and the menus
#  took none of its presses, editor_confirm 2026-11-12_123619)
$scnEnv = @((Get-Content $scn -Raw | ConvertFrom-Json).env.PSObject.Properties | Where-Object { $_ })
# (2026-11-14, merge083: NM_AOT as the game has it, the scenario's own included -- a twin scenario says "env": {"NM_AOT": "0"},
#  the twin mod build having no linked translation -- kept for run.json below, and the caller's own values put back after)
$envBefore = @{}; foreach ($v in $scnEnv) { $envBefore[$v.Name] = [Environment]::GetEnvironmentVariable($v.Name) }
# (2026-11-17, pipefix: "<source>" in a value is this source tree's folder, so a scenario can name a folder in it without a path
#  of the PC's -- "WCW_WRESTLERS_DIR": "<source>\\qa\\wrestlers_test")
foreach ($v in $scnEnv) { Set-Item "Env:$($v.Name)" ("$($v.Value)".Replace('<source>', $PSScriptRoot)) }
$gameNmAot = "$env:NM_AOT"
Write-Host ("running $name for up to $([int]($secs * $stretch)) s" + $(if ($Speed -gt 1) { " (x${Speed}: $gameSecs s of game)" } else { "" }) + $(if ($picBudget) { ", to $picBudget of the game's pictures" } else { "" }) + " ...")
# (newuser 2026-11-17: -Installed tests the game as installed -- its own content folder and native_view.pak, as a player's
#  start reads them; they used to come from the build here too, so a game folder without them still passed)
if ($Installed) { $env:WCW_CONTENT_DIR = "$copy\content" }   # (no2k15: no native_view.pak any more)
else {
$env:WCW_CONTENT_DIR = "$ModRoot\build"     # (the content packs just built: exe src\main\arcade_packs.cpp)
}
# (the game writes its log itself, stdout and stderr both, to game_stderr.txt -- WCW_LOG_FILE: a pipe of
#  Start-Process's could fill and hold the game in its writes, 2026-11-06; what still comes through the pipes, from
#  before the game opens its file, goes to game_stdout.txt / game_pipe_err.txt)
$env:WCW_LOG_FILE = "$run\game_stderr.txt"
$p = Start-Process -FilePath "$copy\Ultimate No Mercy.exe" -WorkingDirectory $copy -PassThru `
    -RedirectStandardOutput "$run\game_stdout.txt" -RedirectStandardError "$run\game_pipe_err.txt"
# the newest row of state.csv (the game is writing it): a column's value
$badCol = -1; $lastBad = 0; $badShots = 0; $nextBad = 0.0; $cols = @(); $whenShots = 0; $nextWhen = 0.0
$shotWhen = if ($s.shot_when) { [scriptblock]::Create("param(`$c) $($s.shot_when)") } else { $null }
function Last-Row([string]$path) {
    try {
        $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
        try { $len = $fs.Length; $fs.Position = [Math]::Max(0, $len - 8192); $buf = New-Object byte[] ($len - $fs.Position); [void]$fs.Read($buf, 0, $buf.Length) }
        finally { $fs.Close() }
        $lines = [Text.Encoding]::ASCII.GetString($buf) -split "`n" | Where-Object { $_.Trim() }
        if ($lines.Count -ge 2) { return $lines[-2] }   # (the last may be half written)
    } catch {}
    return $null
}
# "swaps" in the first row with a picture shown (> 0): where the game's pictures are counted from, as in qa_tool.py
function First-Picture([string]$path, [int]$sc) {
    try {
        $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite'); $rd = New-Object IO.StreamReader($fs)
        try {
            [void]$rd.ReadLine()
            while ($null -ne ($l = $rd.ReadLine())) {
                $v = $l.Trim() -split ','; $sw = 0L
                if ($v.Count -gt $sc -and [long]::TryParse($v[$sc], [ref]$sw) -and $sw -gt 0) { return $sw }
            }
        } finally { $rd.Close() }
    } catch {}
    return -1L
}
$swapsCol = -1; $pic0 = -1L; $nextPics = 0.0; $closedAt = 0.0; $frameLimit = $false
$t0 = Get-Date; $next = 0.0; $n = 0; $killed = $false
while (-not $p.HasExited) {
    $t = ((Get-Date) - $t0).TotalSeconds
    if ($t -gt $secs * $stretch + 450) { Write-Warning "still running after $([int]$t) s: stopped"; $p.Kill(); $killed = $true; break }
    if ($t -gt 420 -and (Get-Item "$run\state.csv" -ErrorAction SilentlyContinue).Length -lt 2000) {
        Write-Warning "no game state after $([int]$t) s (the mod's frames aren't running): stopped"; $p.Kill(); $killed = $true; break
    }
    $p.Refresh()
    $h = $p.MainWindowHandle
    if ($t -ge $next) {
        if (-not $DrawShots -and $h -ne [IntPtr]::Zero -and [QaShot]::Save($h, ("$run\shots\{0:000.0}s.bmp" -f $t))) { $n++ }
        $next += $every
    }
    # a model's matrices went bad (the scene's safety net, gameplay.c): pictures of that moment, at once
    if ($badCol -lt 0 -and (Test-Path "$run\state.csv")) {
        $hdr = Get-Content "$run\state.csv" -TotalCount 1
        if ($hdr) { $cols = $hdr.Trim() -split ','; $badCol = [Array]::IndexOf($cols, 'bad_mtx'); $swapsCol = [Array]::IndexOf($cols, 'swaps') }
    }
    # the scenario's pictures made: the game closed then, as a player closes it (above, 2026-11-12; 4 looks a second)
    if ($picBudget -and $swapsCol -ge 0 -and -not $frameLimit -and $t -ge $nextPics) {
        $nextPics = $t + 0.25
        $row = Last-Row "$run\state.csv"
        if ($row) {
            $v = $row.Trim() -split ','; $sw = 0L
            if ($v.Count -gt $swapsCol -and [long]::TryParse($v[$swapsCol], [ref]$sw) -and $sw -gt 0) {
                if ($pic0 -lt 0) { $pic0 = First-Picture "$run\state.csv" $swapsCol }
                if ($pic0 -ge 0 -and $sw - $pic0 -ge $picBudget -and $p.CloseMainWindow()) {
                    Write-Host "the game has made its $picBudget pictures after $([int]$t) s: closing it"
                    $frameLimit = $true; $closedAt = $t
                }
            }
        }
    }
    if ($frameLimit -and $t -gt $closedAt + 60) { Write-Warning "still open 60 s after it was closed: stopped"; $p.Kill(); $killed = $true; break }
    # the scenario's own moment ("shot_when": a PowerShell test on the newest state row, $c.<column>), every 2 s at most
    if ($shotWhen -and $badCol -ge 0 -and $whenShots -lt 40 -and $t -ge $nextWhen) {
        $row = Last-Row "$run\state.csv"
        if ($row) {
            $v = $row.Trim() -split ','; $c = @{}   # (trimmed: the row's last column keeps the line's carriage return -- a hex one there, rr_next, stopped ToInt64, 2026-11-07)
            for ($k = 0; $k -lt $cols.Count -and $k -lt $v.Count; $k++) { $x = 0.0; $c[$cols[$k]] = if ($v[$k] -match '^0x') { [Convert]::ToInt64($v[$k], 16) } elseif ([double]::TryParse($v[$k], [ref]$x)) { $x } else { $v[$k] } }
            $hit = $false; try { $hit = [bool](& $shotWhen $c) } catch {}
            if ($hit -and $h -ne [IntPtr]::Zero -and [QaShot]::Save($h, ("$run\shots\{0:000.0}s_WHEN.bmp" -f $t))) { $whenShots++; $n++; $nextWhen = $t + 2.0 }
        }
    }
    if ($badCol -ge 0 -and $badShots -lt 30 -and $t -ge $nextBad) {
        $row = Last-Row "$run\state.csv"
        if ($row) {
            $v = 0; [void][int]::TryParse(($row -split ',')[$badCol], [ref]$v)
            if ($v -gt $lastBad) {
                if ($h -ne [IntPtr]::Zero -and [QaShot]::Save($h, ("$run\shots\{0:000.0}s_BAD.bmp" -f $t))) { $badShots++; $n++ }
                $lastBad = $v; $nextBad = $t + 0.5
            }
        }
    }
    Start-Sleep -Milliseconds 100
}
$p.WaitForExit()
foreach ($v in $scnEnv) { if ($null -ne $envBefore[$v.Name]) { Set-Item "Env:$($v.Name)" $envBefore[$v.Name] } else { Remove-Item "Env:$($v.Name)" -ErrorAction SilentlyContinue } }
Remove-Item Env:WCW_QA_STATE, Env:WCW_QUIT_AFTER, Env:WCW_GUARD_MOD, Env:WCW_WATCH_MOD, Env:WCW_VPADS, Env:WCW_VPADS_SCRIPT, Env:WCW_SPEED, Env:WCW_SPEED_MENUS, Env:WCW_QA_DRAW_EVERY, Env:WCW_NATIVE_SHOTS -ErrorAction SilentlyContinue
if ($Lockstep) { if ($null -ne $lockstepWas) { $env:NM_LOCKSTEP = $lockstepWas } else { Remove-Item Env:NM_LOCKSTEP -ErrorAction SilentlyContinue } }
if ($DrawShots) { $n += @(Get-ChildItem "$run\shots\*s.bmp" -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch '_' }).Count }
Write-Host "game exited ($($p.ExitCode)); $n screenshots"
# (frame_limit: closed by qa.ps1 on the scenario's pictures; nm_aot: NM_AOT as the game had it, for the report's linked check)
@{ exit_code = $p.ExitCode; seconds = [int]((Get-Date) - $t0).TotalSeconds; limit = [int]($secs * $stretch); killed = $killed; speed = $Speed; menu_speed = $menuSpeed; draw_shots = [bool]$DrawShots; lockstep = [bool]$Lockstep
   frame_limit = $frameLimit; picture_budget = $picBudget; stretch = $stretch; nm_aot = $gameNmAot } |
    ConvertTo-Json | Set-Content "$run\run.json"
if (Get-ChildItem "$copy\crash_reports" -ErrorAction SilentlyContinue) { Copy-Item "$copy\crash_reports" $run -Recurse }
python "$qa\qa_tool.py" report $scn $run
Write-Host "-> $run"
