# Run in Windows PowerShell: powershell -NoProfile -File .\check_windows_status.ps1
$ErrorActionPreference='Stop'
$setup=Get-Content "$PSScriptRoot\installers\esp32-printer-setup-v3.ps1" -Raw
$selection=[scriptblock]::Create([regex]::Match($setup,'(?s)\$nameConflict=Get-Printer.*?(?=# Status UI)').Value)
$portName='ESP32_LOCAL_esp32-123456789abc'
$stationQueues=@([pscustomobject]@{Name='Old name';PortName=$portName})
function Get-Printer { param($Name,$ErrorAction) $script:conflict }
$printerName='New name';$conflict=$null
. $selection
if ($existing.Name -ne 'Old name') {throw 'Rename must reuse station queue'}
$conflict=[pscustomobject]@{Name='New name';PortName='OTHER'}
$rejected=$false
try {. $selection} catch {$rejected=$true}
if (!$rejected) {throw 'Different station name collision accepted'}
$conflict=$null;$stationQueues=@();. $selection
if ($existing) {throw 'Fresh station reused unrelated queue'}
$ui=Get-Content "$PSScriptRoot\installers\esp32-printer-status.ps1" -Raw
$poll=[scriptblock]::Create([regex]::Match($ui,'(?s)\$timer.add_Tick\(\{(.*?)\n\}\)').Groups[1].Value)
$settings=[pscustomobject]@{name='Test';ip='192.168.1.199'}
$Config='ignored';$text=[pscustomobject]@{Text=''}
$form=New-Object PSObject
foreach ($method in @('Show','Activate')) {$form | Add-Member ScriptMethod $method {}}
$form | Add-Member NoteProperty WindowState 'Normal'
$form | Add-Member NoteProperty Text ''
function Get-Content {param($LiteralPath,[switch]$Raw) '{"name":"Test","ip":"192.168.1.199"}'}
function Get-PrintJob {param($PrinterName,$ErrorAction) [pscustomobject]@{ID=1;DocumentName='Test';JobStatus='Error';PagesPrinted=0;TotalPages=1}}
function Get-Printer {param($Name,$ErrorAction) [pscustomobject]@{PrinterStatus='UserIntervention'}}
function Invoke-RestMethod {param($Uri,$TimeoutSec) if ($script:offline) {throw 'Offline'};[pscustomobject]@{connected=$true;responsive=$true;active=$false;capt=[pscustomobject]@{fresh=$true;service_error=$true;service_code='E100 0000';decoded=1;started=0;completed=0}}}
$script:polling=$false;$script:lastSignal='';$script:lastJobs=''
& $poll
if ($text.Text -notmatch 'E100 0000' -or $text.Text -notmatch 'total unavailable' -or $text.Text -notmatch 'requires attention') {throw "Missing exact error / honest progress: $($text.Text)"}
$script:offline=$true
& $poll
if ($text.Text -notmatch 'ESP offline') {throw 'Offline left stale error visible'}
$guard=[scriptblock]::Create([regex]::Match($setup,'(?s)\$unresolved=.*?(?=\$port=Get-PrinterPort)').Value)
function Test-Path {param($Path) $script:hasCapture}
function Get-ChildItem {param($Path,$ErrorAction) if ($script:hasCapture) {[pscustomobject]@{Name='kept.capt'}}}
function Get-Content {param($Path,[switch]$Raw,$ErrorAction) $script:bridgeNote}
$WarningPreference='SilentlyContinue';$existing=[pscustomobject]@{Name='Test'}
$script:hasCapture=$true;$script:bridgeNote='2026-10-23 STOPPED: Uncertain previous job'
. $guard
if (!$unresolved) {throw 'Stopped uncertain job not detected'}
$script:bridgeNote='Printing kept.capt';$rejected=$false
try {. $guard} catch {$rejected=$true}
if (!$rejected) {throw 'Active capture allowed to upgrade'}
$script:hasCapture=$false;. $guard
if ($unresolved) {throw 'Fresh setup incorrectly blocked'}
'PASS: station identity, E100/offline, stopped upgrade allowed, active capture rejected, fresh setup allowed'
