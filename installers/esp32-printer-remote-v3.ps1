param([string]$Token)
# Run once in an elevated Windows PowerShell. Dedicated LAN-only support endpoint.
$ErrorActionPreference = 'Stop'
if (!$Token) { $Token = Read-Host 'ESP report token (from authenticated ESP web page)' }
Write-Host 'ESP32 remote support v3 - host key ACL fix'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Open PowerShell as administrator.' }
$login = $identity.Name.ToLowerInvariant()
if ($env:USERDOMAIN -eq $env:COMPUTERNAME) { $login = $env:USERNAME.ToLowerInvariant() }
$root = Join-Path $env:ProgramData 'ESP32PrinterSupport'
$taskName = 'ESP32PrinterSupport'
$macIP = Read-Host 'Support computer IPv4 address'
$parsedIP = $null
if (![Net.IPAddress]::TryParse($macIP, [ref]$parsedIP) -or $parsedIP.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) { throw 'Invalid IPv4 address.' }
$publicKey = Read-Host 'Support computer SSH public key (ssh-ed25519)'
if ($publicKey -notmatch '^ssh-ed25519 [A-Za-z0-9+/]{68}={0,2}( .*)?$') { throw 'Invalid Ed25519 public key.' }
$port = 22222
if (![Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Run 64-bit Windows PowerShell on an x64 Windows PC.' }
New-Item -ItemType Directory -Force -Path $root | Out-Null
& icacls.exe $root /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot secure support directory.' }
$archive = Join-Path $root 'OpenSSH-Win64.zip'
$expectedHash = 'b67ff8becd203502190ec42ac7787c24c2cd3325ed7700485fe7a1c83d2a7957'
$download = 'https://github.com/PowerShell/Win32-OpenSSH/releases/download/10.4.0.0p1-Preview/OpenSSH-Win64.zip'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
if (!(Test-Path $archive) -or (Get-FileHash $archive -Algorithm SHA256).Hash -ne $expectedHash) {
  Write-Host 'Downloading Microsoft OpenSSH portable (6 MB). Windows Update is not used.'
  Invoke-WebRequest -UseBasicParsing -Uri $download -OutFile $archive -TimeoutSec 120
}
if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Download SHA256 mismatch. Nothing will be executed.' }
$existingTask = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existingTask) { Stop-ScheduledTask -TaskName $taskName }
Write-Host 'Download verified. Extracting and configuring LAN support.'
Expand-Archive -LiteralPath $archive -DestinationPath $root -Force
$bin = Join-Path $root 'OpenSSH-Win64'
$sshd = Join-Path $bin 'sshd.exe'
$keygen = Join-Path $bin 'ssh-keygen.exe'
if (!(Test-Path $sshd)) { throw 'OpenSSH executable missing from verified archive.' }
# Unprivileged SSH children must be able to traverse to and execute public binaries.
& icacls.exe $root /grant:r '*S-1-5-32-545:RX' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot grant support directory traversal.' }
& icacls.exe $bin /grant:r '*S-1-5-32-545:(OI)(CI)RX' /T /Q | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot grant SSH program read/execute.' }
& icacls.exe $bin /setowner '*S-1-5-32-544' /T /Q | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot set SSH program ownership.' }
# Dedicated host key; leave the normal SSH service and configuration untouched.
$hostKeyFile = Join-Path $root 'ssh_host_ed25519_key'
if (!(Test-Path $hostKeyFile)) {
  $start = New-Object Diagnostics.ProcessStartInfo
  $start.FileName = $keygen
  $start.Arguments = '-q -t ed25519 -N "" -f "' + $hostKeyFile + '"'
  $start.UseShellExecute = $false
  $process = [Diagnostics.Process]::Start($start)
  $process.WaitForExit()
  if ($process.ExitCode -ne 0) { throw 'Cannot create SSH host key.' }
}
# ssh-keygen may add an explicit creator ACE. sshd runs as SYSTEM, not that user.
& icacls.exe $hostKeyFile /reset | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot reset SSH host key permissions.' }
& icacls.exe $hostKeyFile /inheritance:r /grant:r '*S-1-5-18:F' '*S-1-5-32-544:F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot restrict SSH host key permissions.' }
& icacls.exe $hostKeyFile /setowner '*S-1-5-18' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot set SYSTEM owner on SSH host key.' }
$authorizedKeys = Join-Path $root 'authorized_keys'
('from="' + $macIP + '",no-agent-forwarding,no-port-forwarding,no-X11-forwarding ' + $publicKey) | Set-Content -Encoding ASCII $authorizedKeys
$config = Join-Path $root 'sshd_config'
$keyPath = $authorizedKeys.Replace('\','/')
$hostKey = $hostKeyFile.Replace('\','/')
@"
Port $port
AddressFamily inet
HostKey "$hostKey"
AuthorizedKeysFile "$keyPath"
AllowUsers $login
PubkeyAuthentication yes
PasswordAuthentication no
KbdInteractiveAuthentication no
AuthenticationMethods publickey
AllowTcpForwarding no
AllowAgentForwarding no
PermitTunnel no
StrictModes yes
LogLevel VERBOSE
Subsystem sftp internal-sftp
"@ | Set-Content -Encoding ASCII $config
& $sshd -t -f $config
if ($LASTEXITCODE -ne 0) { throw 'SSH configuration validation failed.' }
Get-NetFirewallRule -Name $taskName -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -Name $taskName -DisplayName 'ESP32 printer support from Mac only' -Direction Inbound -Action Allow -Protocol TCP -LocalPort $port -RemoteAddress $macIP -Program $sshd -Profile Any | Out-Null
# Win32 opens the log exclusively; re-executed session/auth processes need unique logs.
$action = New-ScheduledTaskAction -Execute $sshd -Argument "-D -f `"$config`" -E `"$root\sshd-%P-%p.log`""
$trigger = New-ScheduledTaskTrigger -AtStartup
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -User SYSTEM -RunLevel Highest -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
$listening = $null
for ($i=0; $i -lt 10; $i++) {
  Start-Sleep -Seconds 1
  $listening = Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue
  if ($listening) { break }
}
if (!$listening) {
  Stop-ScheduledTask -TaskName $taskName
  Disable-ScheduledTask -TaskName $taskName | Out-Null
  Remove-NetFirewallRule -Name $taskName
  Get-ChildItem "$root\sshd-*.log" | Sort-Object LastWriteTime -Descending | Select-Object -First 3 | ForEach-Object { Get-Content $_.FullName -Tail 30 -ErrorAction SilentlyContinue }
  throw 'Support SSH endpoint did not start.'
}
$addresses = @(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like '192.168.1.*' } | Select-Object -ExpandProperty IPAddress)
$fingerprint = & $keygen -lf "$hostKeyFile.pub"
$report = @{support=@{user=$login;addresses=$addresses;port=$port;host_key=(Get-Content "$hostKeyFile.pub" -Raw).Trim();fingerprint="$fingerprint"}} | ConvertTo-Json -Depth 5
$report | Set-Content -Encoding UTF8 "$root\connection.json"
try {
  Invoke-WebRequest -UseBasicParsing -Method Post -Uri 'http://192.168.1.199/support/windows' -Body @{token=$Token;data=$report} | Out-Null
  Write-Host 'Remote support ready. Connection details uploaded to ESP.'
} catch {
  Write-Warning 'SSH is ready but upload failed. Send the connection details below.'
}
Write-Host $report
Write-Host 'Disable access anytime (administrator): Stop-ScheduledTask -TaskName ESP32PrinterSupport; Disable-ScheduledTask -TaskName ESP32PrinterSupport; Remove-NetFirewallRule -Name ESP32PrinterSupport'
