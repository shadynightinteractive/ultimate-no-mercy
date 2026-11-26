# tools\modnative\ab_queue.ps1 (modwhole, 2026-11-14; from modnative6/7's qa_queue.ps1): the gameplay A/B runs. Each scenario runs
# in a copy of the game (qa\<-Copy>, default qa\ab), with one exe and the mod as built (-ModDir, default mod\), once per mode, one
# game at a time:
#   native      every native function on (the proved tier and, in a build with ARCADE_MOD_NATIVE_WHOLE, the gameplay tier)
#   translated  NM_NATIVE_OFF=all: Stage 1's translation of every mod function (the same exe)
#   <other>     NM_NATIVE_OFF=<other> (a list of the mod's files or functions: to find which one changes a scenario)
# Each result goes to ab_<tag>_<mode>_<scenario>.txt in -Out (default tools\modnative\ab) and a line per run to ab_queue.log: the RESULT line and every
# "[native] <name> (<address>) in use" the game printed. tools\modnative\whole_ab.py reads them. Waits while the pipeline is in its game
# tests or near them (the last "== " line of its newest log: native mod, build exe, tests at 6x, start-up) or any other game runs.
#   .\ab_queue.ps1 -Exe <folder with "Ultimate No Mercy.exe"> -Tag <name> [-Modes native,translated] [-Speed 6] <scenario> ...
#   -Replay <recording>: the replay hook (agent "replay"'s exe changes: NM_REPLAY_PLAY / NM_REPLAY_HASH) -- each run replays that
#                        recording and writes its per-retrace hashes to ab_hashes\<tag>_<mode>_<scenario>\hashes.csv, which
#                        tools\modnative\whole_ab.py compares mode against mode (every structure column; not the same-build "ram" one)
[CmdletBinding(PositionalBinding = $false)]
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$Tag, [string]$Modes = 'native,translated',
      [int]$Speed = 6, [string]$Replay = '', [string]$Copy = 'ab', [string]$ModDir = '', [string]$Out = '', [int]$After = 0, [Parameter(ValueFromRemainingArguments)][string[]]$Scenarios)
# (not at below-normal priority: the game and qa_tool.py would inherit it, and a game slowed by the pipeline's workers runs out of
#  its scenario's time -- qa.ps1 runs the game at normal priority, as the night's QA does)
$dev = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$here = if ($Out) { $Out } else { "$PSScriptRoot\ab" }
New-Item -ItemType Directory -Force $here | Out-Null
if (-not $ModDir) { $ModDir = "$dev\mod" }
$ModeList = @($Modes -split ',' | Where-Object { $_ })
# (-After <process id>: an earlier queue first -- one game at a time in this copy)
if ($After) { $p_ = Get-Process -Id $After -ErrorAction SilentlyContinue; if ($p_) { $p_.WaitForExit() } }
$exePath = Join-Path $Exe 'Ultimate No Mercy.exe'
if (-not (Test-Path $exePath)) { throw "no $exePath" }
"$(Get-Date -Format s) start $Tag pid $PID : $($Scenarios -join ' ') ; modes $Modes" | Add-Content "$here\ab_queue.log"
foreach ($s in $Scenarios) {
    foreach ($m in $ModeList) {
        # (ab_stop_<tag>.txt here: this queue stops before its next run)
        if (Test-Path "$here\ab_stop_$Tag.txt") { "$(Get-Date -Format s) stopped $Tag (ab_stop_$Tag.txt)" | Add-Content "$here\ab_queue.log"; exit 0 }
        while ($true) {
            $l = Get-ChildItem "$dev\scratch\lift\run_*.log" | Sort-Object LastWriteTime | Select-Object -Last 1
            $last = (Select-String -Path $l.FullName -Pattern '^== ' | Select-Object -Last 1).Line
            if ($last -notmatch 'native mod|build exe|tests at 6x|start-up') { break }   # (and from its native step on: its QA follows, and qa.ps1 refuses while another copy's game runs)
            Start-Sleep 60
        }
        # (and no other game: waited for, then a random few seconds and looked again -- another agent's queue may have seen the
        #  same free moment)
        do {
            while (Get-Process 'Ultimate No Mercy', 'NoMercy Arcade' -ErrorAction SilentlyContinue) { Start-Sleep 20 }
            Start-Sleep (Get-Random -Minimum 2 -Maximum 7)
        } while (Get-Process 'Ultimate No Mercy', 'NoMercy Arcade' -ErrorAction SilentlyContinue)
        $out = "$here\ab_${Tag}_${m}_$s.txt"
        if ($m -eq 'native') { Remove-Item Env:NM_NATIVE_OFF -ErrorAction SilentlyContinue }
        elseif ($m -eq 'translated') { $env:NM_NATIVE_OFF = 'all' }
        else { $env:NM_NATIVE_OFF = $m }
        if ($Replay) {
            $hd = "$here\ab_hashes\${Tag}_${m}_$s"
            New-Item -ItemType Directory -Force $hd | Out-Null
            $env:NM_REPLAY_PLAY = (Resolve-Path $Replay).Path; $env:NM_REPLAY_HASH = "$hd\hashes.csv"
        } else { Remove-Item Env:NM_REPLAY_PLAY, Env:NM_REPLAY_HASH -ErrorAction SilentlyContinue }
        $t0 = Get-Date
        & "$dev\qa.ps1" $s -Speed $Speed -Copy $Copy -Exe $exePath -ModDir $ModDir *> $out
        $run = ((Get-Content $out | Select-String '^-> ' | Select-Object -Last 1).Line -replace '^-> ', '').Trim()
        $res = (Get-Content $out | Select-String '^RESULT' | Select-Object -Last 1).Line
        $nat = if ($run -and (Test-Path "$run\game_stderr.txt")) { @(Select-String -Path "$run\game_stderr.txt" -Pattern '^\[native\] (\S+) \((\w+)\) in use( \(gameplay tier\))?' | ForEach-Object { $g = $_.Matches[0].Groups; if ($g[3].Success) { "$($g[1].Value)@$($g[2].Value)+g" } else { "$($g[1].Value)@$($g[2].Value)" } }) } else { @() }
        "$(Get-Date -Format s) $Tag $m $s : $res ; $([int]((Get-Date) - $t0).TotalSeconds) s ; native in use: $($nat.Count) ($($nat -join ' ')) ; $run" | Add-Content "$here\ab_queue.log"
    }
}
Remove-Item Env:NM_NATIVE_OFF, Env:NM_REPLAY_PLAY, Env:NM_REPLAY_HASH -ErrorAction SilentlyContinue
"$(Get-Date -Format s) end $Tag" | Add-Content "$here\ab_queue.log"
