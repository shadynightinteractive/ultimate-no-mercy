# Two QA scenarios at once, each in its own copy of the game (qa\game, qa\game2), when memory allows (2.5 GB free at
# least, about 1.2 GB a game); otherwise one after the other. Then each one's result and checks.
#   .\qa\run_pair.ps1 -Tests a,b [-Speed 6]
param([string[]]$Tests, [int]$Speed = 6)
$Proj = Split-Path $PSScriptRoot -Parent   # (not $D: PowerShell names ignore case, and the loop's $d -- a run folder -- overwrote it)
Set-Location $Proj
function Free-GB { (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB }
$copies = @('game', 'game2')
for ($i = 0; $i -lt $Tests.Count; $i += 2) {
    $pair = @($Tests[$i]) + $(if ($i + 1 -lt $Tests.Count) { @($Tests[$i + 1]) } else { @() })
    if ($pair.Count -eq 2 -and (Free-GB) -lt 2.5) { "memory short ($([math]::Round((Free-GB), 1)) GB): one at a time"; $pair = @($pair[0]); $i-- }
    $jobs = for ($k = 0; $k -lt $pair.Count; $k++) {
        $sc = $pair[$k]; $cp = $copies[$k]
        # (qa.ps1 by its full path: a job's own working folder isn't always the one set -- the third of three found none)
        Start-Job -ScriptBlock { param($dir, $sc, $cp, $sp) Set-Location $dir; & "$dir\qa.ps1" $sc -Speed $sp -Copy $cp 2>&1 | Select-Object -Last 1 } -ArgumentList $Proj, $sc, $cp, $Speed
        Start-Sleep 20   # (the two start-ups apart: both copying the game at once is slower)
    }
    $jobs | Wait-Job | Out-Null
    for ($k = 0; $k -lt $pair.Count; $k++) {
        $r = Receive-Job $jobs[$k]; Remove-Job $jobs[$k]
        "$($pair[$k]) : $r"
        $d = (Get-ChildItem qa\runs -Directory -Filter "*_$($pair[$k])" | Sort-Object LastWriteTime | Select-Object -Last 1).FullName
        Select-String -Path "$d\report.txt" -Pattern '^(PASS|FAIL)' | ForEach-Object { '  ' + $_.Line.Substring(0, [Math]::Min(120, $_.Line.Length)) }
        python qa\nine.py $d "$d\sheet.png" | Out-Null
    }
}
