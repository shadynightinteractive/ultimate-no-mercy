# run_queue.ps1: plays the scenarios listed in qa\queue.txt, one at a time, taking each off the top; lines can be added while
# it runs. Each result goes to qa\queue.log. Ends when the list is empty, or if qa\queue.stop exists.
#   .\qa\run_queue.ps1 [-After <file> -AfterText <text>]   (first waits for that text in that file: another batch's end)
param([string]$After = '', [string]$AfterText = '')
$qa = $PSScriptRoot; $dev = Split-Path $qa -Parent
$list = "$qa\queue.txt"; $log = "$qa\queue.log"
function Say([string]$t) { Add-Content $log "$(Get-Date -Format HH:mm:ss) $t" }
if ($After) { while (-not (Select-String -Path $After -SimpleMatch $AfterText -Quiet)) { Start-Sleep 10 } }
Say "QUEUE START"
while ($true) {
    if (Test-Path "$qa\queue.stop") { Say "QUEUE STOPPED"; break }
    $lines = @(Get-Content $list -ErrorAction SilentlyContinue | Where-Object { $_.Trim() })
    if ($lines.Count -eq 0) { Say "QUEUE EMPTY"; break }
    $s = $lines[0].Trim()
    Set-Content $list ($lines | Select-Object -Skip 1)
    # (the user's own game open: wait for it)
    while (Get-Process -Name 'Ultimate No Mercy', 'NoMercy Arcade' -ErrorAction SilentlyContinue) { Start-Sleep 20 }
    Say "START $s"
    $out = try { & "$dev\qa.ps1" $s 2>&1 | Out-String } catch { "RESULT: DID NOT RUN: $_" }
    $res = ($out -split "`n" | Where-Object { $_ -match '^RESULT' } | Select-Object -Last 1)
    $run = ($out -split "`n" | Where-Object { $_ -match '^-> ' } | Select-Object -Last 1)
    Say "DONE $s $res $run"
}
