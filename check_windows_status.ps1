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
$model=[scriptblock]::Create([regex]::Match($ui,'(?s)function Get-DeviceView\(\$s\) \{.*?\n\}').Value)
. $model
$s=[pscustomobject]@{connected=$true;responsive=$true;active=$false;capt=[pscustomobject]@{fresh=$true;service_error=$true;service_code='E100 0000'}}
$v=Get-DeviceView $s
if ($v.Title -notmatch 'E100 0000' -or $v.Kind -ne 'error') {throw 'Exact Canon fault missing'}
if ((Get-DeviceView $null).Kind -ne 'error') {throw 'Offline not an error'}
$s.capt.service_error=$false;$s.active=$true;$s | Add-Member NoteProperty job_control ([pscustomobject]@{id='test';state='printing';active=$true;total=0;completed=0})
if ((Get-DeviceView $s).Progress -match '/') {throw 'Invented page total'}
$s.job_control.total=3;$s.job_control.completed=1
if ((Get-DeviceView $s).Progress -notmatch '1 / 3') {throw 'Physical progress missing'}
$s.job_control.active=$false;$s.job_control.state='failed'
if ((Get-DeviceView $s).Progress -match '1 / 3') {throw 'Inactive history shown as current progress'}
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
