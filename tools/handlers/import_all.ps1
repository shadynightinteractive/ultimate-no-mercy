# Makes every imported wrestler folder the game has, from the player's own ROMs, into one folder (newhome, 2026-11-17).
# The steps the installed folders were made by, in order (the handlers' notes: IMPORT_CHECKS.md, each script's top):
#   1. tools\handlers\revlib.py      the Revenge move library's index (work\revenge_library\rows_all.json) the next step reads
#   2. nm_import.py                  wrestlers\No Mercy\    (the stock 64 + stock_roster.json: the ROM alone)
#   3. aki_import.py revenge --all --rest-only --legacy-ids 0x0340, then --refresh
#                                    wrestlers\Revenge\     (the never-ported men; the rig's captures for their models,
#                                                            their photos rendered from their models: tools\romrender)
#   4. aki_import.py revenge --ports-universal (with -Ports)
#                                    wrestlers\Revenge\     (the 18 built-in ports as folders that "replace" the built-ins:
#                                                            0.84 and later only)
#      (3: --records <movelib2's record changes>; numbers and library moves frozen: handlers\revenge\legacy_ids.json,
#       library_ids.json)
#   5. wm2k_import.py               wrestlers\WrestleMania 2000\  (photos: rendered from his model, tools\romrender)
#      + wm2k_refresh.py            (stances and winning taunts; the two built-in WM2000 men are left out)
#   6. imports2.py                  wrestlers\Imported\    (vader from Badd Blood, goldust from No Mercy Plus V3)
# The emulator rig's captures each step needs are in data\handlers_rig\<game> (revenge, wm2000, imports2): made by the rig
# scripts in tools\handlers\rig from the same ROMs (NM_HANDLERS_RIG points each step at its folder).
#   .\tools\handlers\import_all.ps1 -Out <folder> [-Ports] [-Library]
# -Library: make the Revenge library index again even when it is there. Every step's output: <Out>\logs\<step>.log.
# The result goes into the game with deploy-style copying by hand (or point WCW_WRESTLERS_DIR at <Out> to try it first).
param([Parameter(Mandatory)][string]$Out, [switch]$Ports, [switch]$Library)
$ErrorActionPreference = 'Stop'
$D = (Resolve-Path "$PSScriptRoot\..\..").Path
. "$D\toolchain\env.ps1"
$H = "$D\tools\handlers"
$rig = "$D\data\handlers_rig"
New-Item -ItemType Directory -Force "$Out\logs" | Out-Null
$env:PYTHONDONTWRITEBYTECODE = '1'
function Step($name, [string[]]$a) {
    $t0 = Get-Date
    # (the tool's own output, stdout and stderr, as it wrote it: Windows PowerShell with 'Stop' would end at the first line a
    #  tool writes to stderr, a warning included)
    $ErrorActionPreference = 'Continue'
    $quoted = ($a | ForEach-Object { '"' + $_ + '"' }) -join ' '
    cmd /c "python $quoted > `"$Out\logs\$name.log`" 2>&1"
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    "{0,-16} exit {1}  {2:N0} s" -f $name, $code, ((Get-Date) - $t0).TotalSeconds
    if ($code) { Get-Content "$Out\logs\$name.log" -Tail 15; throw "$name failed" }
}
$lib = "$H\work\revenge_library\rows_all.json"
if ($Library -or -not (Test-Path $lib)) { Step 'revlib' @("$H\revlib.py", "$H\work\revenge_library") }
Step 'nm_import' @("$H\nm_import.py", '--out', $Out)
$env:NM_HANDLERS_RIG = "$rig\revenge"
# (the never-ported men's numbers and library moves are frozen as data: handlers\revenge\legacy_ids.json, library_ids.json;
#  --records: movelib2's record changes from Revenge's own move data, as the installed folders were made)
$rec = "$H\work\revenge_library\records_damage.json"
Step 'rev_all' @("$H\aki_import.py", 'revenge', '--out', $Out, '--all', '--rest-only', '--legacy-ids', '0x0340', '--records', $rec)
Step 'rev_refresh' @("$H\aki_import.py", 'revenge', '--out', $Out, '--refresh')
if ($Ports) { Step 'rev_ports' @("$H\aki_import.py", 'revenge', '--out', $Out, '--ports-universal') }
$env:NM_HANDLERS_RIG = "$rig\wm2000"
# (romrender 2026-11-18, the user's decision A: the photos are rendered from each man's model by wm2k_import.py itself --
#  no screenshots; wm2k_photo.py's rig select pictures are no longer read)
Step 'wm2k_import' @("$H\wm2k_import.py", '--out', $Out)
# (wm2k_refresh.py, revfix 2026-11-15: each man's stance and winning taunt brought to the user's rule -- the installed folders
#  have it. The two WrestleMania 2000 men the game has built in, Jeff Jarrett and The Blue Meanie, are not installed as
#  folders (they would be twins of the built-ins): left out of the result, as the installed game has them.)
Step 'wm2k_refresh' @("$H\wm2k_refresh.py", $Out)
foreach ($twin in 'jeff_jarrett', 'the_blue_meanie') {
    if (Test-Path "$Out\WrestleMania 2000\$twin") { Move-Item "$Out\WrestleMania 2000\$twin" "$Out\logs\builtin_twin_$twin" -Force }
}
$env:NM_HANDLERS_RIG = "$rig\imports2"
$env:NM_STOCK_FOLDERS = "$Out\No Mercy"
Step 'imports2' @("$H\imports2.py", '--out', $Out)
Remove-Item Env:NM_HANDLERS_RIG, Env:NM_STOCK_FOLDERS -ErrorAction SilentlyContinue
"done: $Out"
