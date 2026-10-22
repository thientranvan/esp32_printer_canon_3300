param([string]$Token, [string]$StationIP)
$ErrorActionPreference = 'Stop'
if (!$StationIP) { $StationIP=Read-Host 'ESP IPv4 address' }
$address=$null
if ($StationIP -notmatch '^([0-9]{1,3}\.){3}[0-9]{1,3}$' -or ![Net.IPAddress]::TryParse($StationIP,[ref]$address) -or $address.AddressFamily -ne 'InterNetwork') { throw 'Invalid ESP IPv4 address.' }
$StationIP=$address.ToString()
if (!$Token) { $Token = Read-Host 'ESP report token (from authenticated ESP web page)' }
$data = [ordered]@{
  drivers = @(Get-CimInstance Win32_PrinterDriver | Where-Object { $_.Name -match 'LBP\s*3300' } | Select-Object Name,MonitorName,DriverPath,ConfigFile,DataFile,HelpFile,DependentFiles,DefaultDataType,SupportedPlatform,Version)
  printers = @(Get-Printer -Full | Where-Object { $_.DriverName -match 'LBP\s*3300' } | Select-Object Name,DriverName,PortName,PrintProcessor,Datatype,PrinterStatus,RenderingMode)
  ports = @(Get-PrinterPort | Select-Object Name,Description,PortMonitor,PrinterHostAddress,PortNumber,SNMPEnabled)
  monitors = @(Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors' | ForEach-Object { [pscustomobject]@{ Name=$_.PSChildName; Driver=(Get-ItemProperty $_.PSPath).Driver } })
}
$json = $data | ConvertTo-Json -Depth 6
$json | Set-Content -Encoding UTF8 C:\esp32-printer-diagnostics.json
Invoke-WebRequest -UseBasicParsing -Method Post -Uri "http://$StationIP/support/windows" -Body @{token=$Token;data=$json} | Out-Null
Write-Host 'Diagnostics uploaded. No printer settings changed.'
