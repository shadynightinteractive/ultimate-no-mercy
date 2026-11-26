# A set of QA scenarios (qa.ps1 each), and the deploy if every one passed.
#   .\qa_set.ps1 -Tests menu_from_attract,versus_1p [-Speed 6] [-Deploy] [-Parallel 2]
# Each test waits until the user's own game is closed (never a test window while they play). Per test: its result,
# the checks that failed, the game's updates a second, the crash / freeze reports, and a 3 x 3 sheet of its pictures
# (qa\runs\<run>\sheet.png, qa\nine.py). -Speed: fast-forward (qa.ps1 -Speed: a match at that speed, the rest at 2).
# -Parallel: how many at once, each in its own copy of the game (qa\game, qa\game2, ...) -- the user's ask, 2026-11-07
# ("Multiple windows please"); a further one starts only with 4 GB free (a 32-man Rumble's game is ~1.5 GB, and three
# at once had the system stop a run for low memory), else it waits for one to finish. 1: one after the other, as before.
# -Deploy: .\deploy.ps1 -Exe after, only if all passed and no game is open.
param([string[]]$Tests = @('menu_from_attract', 'announcers_front',   # (no2k15: hd_select gone with the 2K15 slots)
      'tron_entrances', 'music_rumble_late', 'versus_1p', 'blocks_hogan'),
      [int]$Speed = 0, [switch]$Deploy, [int]$Parallel = 2)
Set-Location $PSScriptRoot
function Wait-ForTheirGame {
    $said = $false
    while (Get-Process | Where-Object { ($_.ProcessName -like 'NoMercy*' -or $_.ProcessName -eq 'Ultimate No Mercy') -and $_.Path -notlike '*\qa\game*' }) {
        if (-not $said) { "waiting: the game is open"; $said = $true }
        Start-Sleep 30
    }
}
function Free-GB { (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB }
# (one finished test's lines, as the one-at-a-time set printed them)
function Report-Test([string]$sc, [string]$r) {
    "$sc : " + $r.Substring(0, [Math]::Min(60, $r.Length))
    $d = (Get-ChildItem qa\runs -Directory -Filter "*_$sc" | Sort-Object LastWriteTime | Select-Object -Last 1).FullName
    # (2026-11-14, merge083: a run that stopped before its report -- qa.ps1 threw, or the game never started -- has no
    #  report.txt, and Select-String on it threw under a caller's 'Stop', ending the whole set: say so and go on)
    if (-not $d -or -not (Test-Path "$d\report.txt")) { "  (no report.txt: the run stopped before its report)"; return }
    Select-String -Path "$d\report.txt" -Pattern '^FAIL' | Select-Object -First 3 | ForEach-Object { '  ' + $_.Line.Substring(0, [Math]::Min(140, $_.Line.Length)) }
    $u = if (Test-Path "$d\game_stderr.txt") { Select-String -Path "$d\game_stderr.txt" -Pattern '^\[view\] ([0-9.]+) pictures a second from ([0-9.]+) game' | Select-Object -Skip 2 | ForEach-Object { [double]$_.Matches[0].Groups[2].Value } | Sort-Object }
    if ($u) { "  updates a second: median $($u[[int]($u.Count / 2)]), lowest $($u[0])" }
    "  report files $((Get-ChildItem "$d\crash_reports\*.txt" -ErrorAction SilentlyContinue | Measure-Object).Count)"
    python qa\nine.py $d "$d\sheet.png" | Out-Null
}
Remove-Item Env:WCW_HEAP_TEST -ErrorAction SilentlyContinue
$bad = 0
$starved = [System.Collections.Generic.List[string]]::new()   # (STARVED: the PC too busy for the game's pace -- rerun, below)
$Parallel = [Math]::Max(1, [Math]::Min($Parallel, 4))
$copies = @('game', 'game2', 'game3', 'game4')[0..($Parallel - 1)]
$queue = [System.Collections.Generic.Queue[string]]::new(); foreach ($t in $Tests) { $queue.Enqueue($t) }
$running = @{}   # copy -> @{ job; test }
while ($queue.Count -gt 0 -or $running.Count -gt 0) {
    # a finished one: its report
    foreach ($cp in @($running.Keys)) {
        $j = $running[$cp]
        if ($j.job.State -in 'Completed', 'Failed', 'Stopped') {
            $r = (Receive-Job $j.job 2>&1 | Select-Object -Last 1); Remove-Job $j.job
            $r = if ($r) { $r.ToString() } else { 'RESULT: (no result)' }
            Report-Test $j.test $r
            if ($r -match 'RESULT: STARVED') { $starved.Add($j.test) } elseif ($r -notmatch 'RESULT: PASS') { $bad++ }
            $running.Remove($cp)
        }
    }
    # a free copy and a test waiting: start it (the first always; a further one only with the memory for it)
    # (a copy is free when no job holds it and no game is still open in it: a qa.ps1 that stopped on an error can leave its
    #  game running -- the next test there refused, "a test is running in qa\game", 2026-11-07)
    $open = @(Get-Process | Where-Object { $_.Path -like '*\qa\game*' } | ForEach-Object { Split-Path (Split-Path $_.Path -Parent) -Leaf })
    $free = @($copies | Where-Object { -not $running.ContainsKey($_) -and $open -notcontains $_ })
    if ($queue.Count -gt 0 -and $free.Count -gt 0 -and ($running.Count -eq 0 -or (Free-GB) -ge 4)) {
        Wait-ForTheirGame
        $sc = $queue.Dequeue(); $cp = $free[0]
        # (qa.ps1 by its full path: a job's own working folder isn't always the one set)
        $job = Start-Job -ScriptBlock {
            param($dir, $sc, $cp, $sp)
            Set-Location $dir
            if ($sp -gt 0) { & "$dir\qa.ps1" $sc -Speed $sp -Copy $cp 2>&1 | Select-Object -Last 1 } else { & "$dir\qa.ps1" $sc -Copy $cp 2>&1 | Select-Object -Last 1 }
        } -ArgumentList $PSScriptRoot, $sc, $cp, $Speed
        $running[$cp] = @{ job = $job; test = $sc }
        Start-Sleep 20   # (start-ups apart: two copying the game at once is slower)
        continue
    }
    Start-Sleep 5
}
# a test the report called STARVED (qa_tool.py: the game made too few of its pictures before its time ran out, and only
# checks that may not have been reached failed) runs again once, alone -- nothing else of the set beside it. Only its
# rerun's PASS counts as a pass; STARVED again is a test not passing, as before. 2026-11-12
foreach ($sc in $starved) {
    Wait-ForTheirGame
    "$sc : starved, run again alone"
    $r = if ($Speed -gt 0) { & "$PSScriptRoot\qa.ps1" $sc -Speed $Speed -Copy game 2>&1 | Select-Object -Last 1 } else { & "$PSScriptRoot\qa.ps1" $sc -Copy game 2>&1 | Select-Object -Last 1 }
    $r = if ($r) { $r.ToString() } else { 'RESULT: (no result)' }   # (qa_tool.py's last line, as the jobs' above)
    Report-Test $sc $r
    if ($r -notmatch 'RESULT: PASS') { $bad++ }
}
if ($bad) { "$bad not passing: not deploying"; exit 0 }
if (-not $Deploy) { "all passed"; exit 0 }
# (the user's own game only: a test copy open in qa\game* is not theirs -- 0.65.0 was held back by another job's test, 2026-11-07)
if (Get-Process | Where-Object { ($_.ProcessName -like 'NoMercy*' -or $_.ProcessName -eq 'Ultimate No Mercy') -and $_.Path -notlike '*\qa\game*' }) { "the game is open: not deploying"; exit 0 }
$r = .\deploy.ps1 -Exe 2>&1; ($r | Select-String '^deployed|backed up') | ForEach-Object { $_.ToString().Substring(0, [Math]::Min(90, $_.ToString().Length)) }
exit 0
