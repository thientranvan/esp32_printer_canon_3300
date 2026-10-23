# Run with Windows PowerShell; no administrator privileges required.
$ErrorActionPreference='Stop'
$source=Get-Content "$PSScriptRoot\installers\esp32-printer-setup-v3.ps1" -Raw
$helper=[regex]::Match($source,'(?s)function Move-BridgePendingToArchive.*?(?=# End pending archive helper\.)').Value
if (!$helper) {throw 'Archive helper missing'}
. ([scriptblock]::Create($helper))
$folder=Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $folder | Out-Null
try {
 [IO.File]::WriteAllBytes("$folder\old.capt",[byte[]](0..255))
 [IO.File]::WriteAllText("$folder\pending.txt",'Do not replay')
 [IO.File]::WriteAllText("$folder\old.capt.progress",'0 completed')
 [IO.File]::WriteAllText("$folder\bridge.exe",'keep executable')
 $before=(Get-FileHash "$folder\old.capt").Hash
 $archive=Move-BridgePendingToArchive $folder
 if ((Get-FileHash "$archive\old.capt").Hash -ne $before) {throw 'Capture bytes changed'}
 if (Test-Path "$folder\pending.txt") {throw 'Pending marker still blocks bridge'}
 if (!(Test-Path "$archive\pending.txt") -or !(Test-Path "$archive\old.capt.progress")) {throw 'Recovery files missing'}
 if (!(Test-Path "$folder\bridge.exe")) {throw 'Bridge executable changed'}
 if (@(Get-ChildItem "$folder\*.capt").Count) {throw 'Active capture remains'}
 'PASS: archive preserves capture bytes, marker and progress; clears only active paths; keeps executable'
} finally {Remove-Item -LiteralPath $folder -Recurse -Force}
