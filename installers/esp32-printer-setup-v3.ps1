#requires -RunAsAdministrator
param([string]$StationIP, [string]$PrinterName)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
if (!$StationIP) { $StationIP=Read-Host 'ESP IP (for example 192.168.1.199)' }
$address=$null
if ($StationIP -notmatch '^([0-9]{1,3}\.){3}[0-9]{1,3}$' -or ![Net.IPAddress]::TryParse($StationIP,[ref]$address) -or $address.AddressFamily -ne 'InterNetwork') { throw 'Invalid ESP IPv4 address.' }
$StationIP=$address.ToString()
$profile=Invoke-RestMethod -Uri "http://$StationIP/setup/profile.json" -TimeoutSec 10
if ($profile.schema -ne 1 -or $profile.model -ne 'LBP3300' -or $profile.id -notmatch '^esp32-[0-9a-f]{12}$' -or $profile.id -eq 'esp32-000000000000' -or $profile.ip -ne $StationIP) { throw 'Target is not a supported ESP32 printer station, or its IP differs.' }
$stationId=[string]$profile.id
$portName="ESP32_LOCAL_$stationId"
$stationQueues=@(Get-Printer | Where-Object { $_.PortName -eq $portName })
if (!$PrinterName) {
 $defaultName=if ($stationQueues.Count) { $stationQueues[0].Name } else { [string]$profile.name }
 $answer=Read-Host "Printer name [$defaultName]"
 $PrinterName=if ([string]::IsNullOrWhiteSpace($answer)) { $defaultName } else { $answer.Trim() }
}
if (!$PrinterName -or $PrinterName.Length -gt 64 -or $PrinterName -match '[\x00-\x1f\x7f\\/";]') { throw 'Printer name must contain 1..64 characters, without control characters, slash, quote or semicolon.' }
Write-Host "Station: $PrinterName / $stationId / $StationIP"
$root=Join-Path $env:ProgramData "ESP32Printer\Stations\$stationId"
$taskName="ESP32CaptBridge-$stationId"
$driverName='Canon LBP3300 ESP32 Render'
$portName="ESP32_LOCAL_$stationId"
$port=Get-PrinterPort -Name $portName -ErrorAction SilentlyContinue
if ($port) { $localPort=[int]$port.PortNumber }
else {
 $used=@(Get-PrinterPort | ForEach-Object { $_.PortNumber }) + @(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue | ForEach-Object { $_.LocalPort })
 $localPort=19101..19200 | Where-Object { $_ -notin $used } | Select-Object -First 1
 if (!$localPort) { throw 'No free local bridge port in 19101..19200.' }
}
if ($localPort -lt 19101 -or $localPort -gt 19200) { throw 'Invalid existing station bridge port.' }
if (![Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Requires Windows 10/11 x64 and 64-bit Windows PowerShell as administrator. Windows ARM64 is not supported.' }
$nameConflict=Get-Printer -Name $printerName -ErrorAction SilentlyContinue
$existing=$stationQueues | Where-Object { $_.Name -eq $printerName } | Select-Object -First 1
if (!$existing) { $existing=$stationQueues | Select-Object -First 1 }
if ($nameConflict -and $nameConflict.PortName -ne $portName) { throw 'Printer name belongs to another station. Choose a different name; no queue was changed.' }
# Status UI runs in each user's desktop, separately from the SYSTEM bridge.
$statusRoot=Join-Path $env:ProgramData "ESP32PrinterStatus\$stationId"
New-Item -ItemType Directory -Force $statusRoot | Out-Null
& icacls.exe $statusRoot /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot secure status UI directory.' }
$statusSource=@'
param([Parameter(Mandatory=$true)][string]$Config)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
$settings=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$created=$false
$mutex=New-Object Threading.Mutex($true,"Local\ESP32PrinterStatus-$($settings.id)",[ref]$created)
if (!$created) { exit }
$form=New-Object Windows.Forms.Form
$form.Text="$($settings.name) - ESP32 / Canon LBP3300"
$form.Size=New-Object Drawing.Size(620,360)
$form.StartPosition='CenterScreen'
$text=New-Object Windows.Forms.TextBox
$text.Multiline=$true;$text.ReadOnly=$true;$text.Dock='Fill';$text.ScrollBars='Vertical'
$text.Font=New-Object Drawing.Font('Segoe UI',12)
$form.Controls.Add($text)
$tray=New-Object Windows.Forms.NotifyIcon
$tray.Icon=[Drawing.SystemIcons]::Information
$tray.Text='ESP32 printer status';$tray.Visible=$true
$tray.add_DoubleClick({$form.Show();$form.WindowState='Normal';$form.Activate()})
$menu=New-Object Windows.Forms.ContextMenuStrip
[void]$menu.Items.Add('Open status',$null,{$form.Show();$form.Activate()})
[void]$menu.Items.Add('Exit monitor',$null,{$script:exitMonitor=$true;$form.Close()})
$tray.ContextMenuStrip=$menu
$form.add_FormClosing({param($sender,$eventArgs) if (!$script:exitMonitor) {$eventArgs.Cancel=$true;$form.Hide()}})
$script:lastSignal='';$script:lastJobs='';$script:polling=$false
$timer=New-Object Windows.Forms.Timer
$timer.Interval=2000
$timer.add_Tick({
 if ($script:polling) { return };$script:polling=$true
 try {
  $settings=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
  $form.Text="$($settings.name) - ESP32 / Canon LBP3300"
  $jobs=@(Get-PrintJob -PrinterName $settings.name -ErrorAction SilentlyContinue)
  $jobKey=($jobs | ForEach-Object { $_.ID }) -join ','
  $lines=@("$($settings.name) | $($settings.ip)","Updated: $(Get-Date -Format HH:mm:ss)")
  $signal=''
  try {
   $s=Invoke-RestMethod -Uri "http://$($settings.ip)/printer/status" -TimeoutSec 1
   if (!$s.connected -or !$s.responsive) { $signal='Printer USB disconnected / not responding' }
   elseif (!$s.capt.fresh) { $signal='Printer status stale; progress unknown' }
   else {
    $errors=@()
    if ($s.capt.service_error) { $errors+="Canon $($s.capt.service_code) - printer service fault; inspect printer" }
    if ($s.capt.paper_jam) { $errors+='Paper jam' }
    if ($s.capt.cover_open) { $errors+='Cover open' }
    if ($s.capt.paper_required) { $errors+='Check paper size / source (not proof tray is empty)' }
    if ($s.capt.toner_warning) { $errors+='Toner warning' }
    $signal=$errors -join "`r`n"
   }
   $lines+=if ($signal) {$signal} elseif ($s.active) {'Printing'} else {'Printer idle'}
   if ($s.active -and $s.total -gt 0) { $lines+="Completed impressions: $($s.completed) / $($s.total)" }
   elseif ($s.capt.fresh) { $lines+="Printer counters: decoded $($s.capt.decoded), started $($s.capt.started), completed $($s.capt.completed). Job total unavailable." }
  } catch { $signal='ESP offline / network unavailable';$lines+=$signal }
  $queue=Get-Printer -Name $settings.name -ErrorAction SilentlyContinue
  if ($queue.PrinterStatus -match 'UserIntervention') {
   $lines+='Bridge/queue requires attention. An uncertain previous job may be held; do not resend automatically.'
   $signal+="`r`nQueue requires attention"
  }
  $lines+="Windows queue: $($jobs.Count) job(s). Spool completion does not mean paper output."
  foreach ($job in $jobs) { $lines+="#$($job.ID) $($job.DocumentName): $($job.JobStatus); spool pages $($job.PagesPrinted)/$($job.TotalPages)" }
  $text.Text=$lines -join "`r`n`r`n"
  if (($signal -and $signal -ne $script:lastSignal) -or ($jobKey -and $jobKey -ne $script:lastJobs)) { $form.Show();$form.WindowState='Normal';$form.Activate() }
  $script:lastSignal=$signal;$script:lastJobs=$jobKey
 } catch { $text.Text="Status monitor error: $($_.Exception.Message)" }
 finally { $script:polling=$false }
})
$timer.Start()
try { [Windows.Forms.Application]::Run($form) } finally { $timer.Dispose();$tray.Dispose();$mutex.Dispose() }

'@
[IO.File]::WriteAllText("$statusRoot\status.ps1",$statusSource,[Text.UTF8Encoding]::new($true))
@{id=$stationId;ip=$StationIP;name=$(if ($existing) {$existing.Name} else {$PrinterName})} | ConvertTo-Json | Set-Content -Encoding UTF8 "$statusRoot\station.json"
$shell=New-Object -ComObject WScript.Shell
foreach ($folder in @([Environment]::GetFolderPath('CommonStartup'),[Environment]::GetFolderPath('CommonPrograms'))) {
 $shortcut=$shell.CreateShortcut((Join-Path $folder "ESP32-$stationId.lnk"))
 $shortcut.TargetPath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
 $shortcut.Arguments="-NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$statusRoot\status.ps1`" -Config `"$statusRoot\station.json`""
 $shortcut.Description="ESP32 printer status: $PrinterName"
 $shortcut.Save()
}
$uiUser=[Security.Principal.WindowsIdentity]::GetCurrent().Name
$uiAction=New-ScheduledTaskAction -Execute $shortcut.TargetPath -Argument $shortcut.Arguments
$uiPrincipal=New-ScheduledTaskPrincipal -UserId $uiUser -LogonType Interactive -RunLevel Limited
$uiSettings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName "ESP32PrinterStatus-$stationId" -Action $uiAction -Principal $uiPrincipal -Settings $uiSettings -Force | Out-Null
Start-ScheduledTask -TaskName "ESP32PrinterStatus-$stationId"

if (@(Get-Printer | Where-Object { $_.PortName -eq $portName -and $_.JobCount -ne 0 }).Count) { throw 'Status window installed. Print queue is not empty. Resolve existing jobs first; no jobs were deleted.' }
if ((Test-Path "$root\pending.txt") -or @(Get-ChildItem "$root\*.capt" -ErrorAction SilentlyContinue).Count) { throw 'Status window installed. Unresolved bridge job found. Inspect pending.txt/status.txt before reinstalling; nothing was resent.' }
$port=Get-PrinterPort -Name $portName -ErrorAction SilentlyContinue
if ($port -and ($port.PrinterHostAddress -ne '127.0.0.1' -or $port.PortNumber -ne $localPort)) { throw 'Existing bridge port has conflicting settings.' }
$listener=Get-NetTCPConnection -LocalPort $localPort -State Listen -ErrorAction SilentlyContinue
foreach ($connection in $listener) {
 if ((Get-Process -Id $connection.OwningProcess).Path -ne "$root\bridge.exe") { throw 'Selected local port is used by another program.' }
}
New-Item -ItemType Directory -Force $root | Out-Null
& icacls.exe $root /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot secure bridge directory.' }
# Canon driver stays on Canon CDN; no driver binary is redistributed by this repo.
function Install-CanonDriver {
 $url='https://gdlp01.c-wss.com/gds/4/0100002764/04/LBP3300_R150_V330_W64_uk_EN_2.exe'
 $sha256='480956b6175404f4b330f70f258b3c208a60dfacdf313ec2507cb87ed604bcfd'
 $cache=Join-Path $root 'DriverCache';New-Item -ItemType Directory -Force $cache | Out-Null
 $archive=Join-Path $cache 'canon.exe'
 if (!(Test-Path $archive) -or (Get-FileHash $archive -Algorithm SHA256).Hash -ne $sha256) {
  Write-Host 'Downloading official Canon CAPT 3.30 x64 (12 MB); this can take a few minutes.'
  Write-Host 'Canon information/license: https://vn.canon/en/support/0100276401'
  [Net.ServicePointManager]::SecurityProtocol=[Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
  $partial=$archive+'.partial'
  Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $partial -TimeoutSec 180
  if ((Get-FileHash $partial -Algorithm SHA256).Hash -ne $sha256) { throw 'Canon download SHA-256 mismatch. Nothing was executed or installed.' }
  Move-Item -Force $partial $archive
 }
 $signature=Get-AuthenticodeSignature $archive
 if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Canon') { throw 'Canon Authenticode signature is not trusted. Driver was not installed.' }
 $unpacked=Join-Path $cache ([Guid]::NewGuid().ToString())
 try {
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [IO.Compression.ZipFile]::ExtractToDirectory($archive,$unpacked)
  $inf=Join-Path $unpacked 'Driver\CNAB5STD.INF'
  if (!(Test-Path $inf)) { throw 'Canon package is missing the LBP3300 INF.' }
  Write-Host 'Staging signed Canon driver in Windows Driver Store...'
  & "$env:windir\System32\pnputil.exe" /add-driver $inf | Out-Host
  if ($LASTEXITCODE -eq 3010) { throw 'Windows requires a restart. Restart and run this installer again.' }
  if ($LASTEXITCODE -ne 0) { throw "Canon driver staging failed: $LASTEXITCODE. No signing/security policy was changed." }
  $package=Get-WindowsDriver -Online -All | Where-Object {
   [IO.Path]::GetFileName($_.OriginalFileName) -ieq 'CNAB5STD.INF' -and $_.ProviderName -eq 'Canon' -and [string]$_.Version -eq '3.30.0.0'
  } | Select-Object -First 1
  if (!$package) { throw 'Signed Canon package was not found in Driver Store.' }
  Write-Host 'Registering Canon LBP3300 renderer...'
  Add-PrinterDriver -Name 'Canon LBP3300' -InfPath $package.OriginalFileName
 } finally {
  if (Test-Path $unpacked) { Remove-Item -LiteralPath $unpacked -Recurse -Force }
 }
 $driver=Get-CimInstance Win32_PrinterDriver | Where-Object Name -eq 'Canon LBP3300,3,Windows x64'
 if (!$driver) { throw 'Canon driver registration failed. Check Windows PrintService logs.' }
 return $driver
}
$source=Get-CimInstance Win32_PrinterDriver | Where-Object Name -eq 'Canon LBP3300,3,Windows x64'
if (!$source) { $source=Install-CanonDriver }
else { Write-Host 'Canon LBP3300 driver already installed; no driver download needed.' }
$sourceCode=@'
using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Collections.Generic;
using System.Threading;
using System.Diagnostics;

// ponytail: LBP3300 Windows CAPT wrapper, A4/cassette 1, 100 pages/20 MiB.
// Extend only after capturing and testing another model/paper/duplex format.
public static class Esp32CaptBridge {
 const int Limit = 20 * 1024 * 1024;
 static readonly string Root = Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location);
 static readonly string Pending = Path.Combine(Root, "pending.txt");
 sealed class Page { public byte[] Parameters; public List<byte[]> Bands = new List<byte[]>(); }
 sealed class Job { public List<Page> Pages = new List<Page>(); public int Copies = 1; public bool Collate = true; public int Duplex = 1; }
 static ushort U16(byte[] b, int p) { return BitConverter.ToUInt16(b,p); }
 static uint U32(byte[] b, int p) { return BitConverter.ToUInt32(b,p); }
 static void Put16(byte[] b, int p, int n) { Buffer.BlockCopy(BitConverter.GetBytes((ushort)n),0,b,p,2); }
 static void Put32(byte[] b, int p, uint n) { Buffer.BlockCopy(BitConverter.GetBytes(n),0,b,p,4); }
 static void Need(bool ok, string message) { if (!ok) throw new InvalidDataException(message); }
 static byte[] Packet(int op, byte[] data) {
  if(data==null) data=new byte[0];
  Need(data.Length<=65531,"Packet too large");
  byte[] b=new byte[data.Length+4]; Put16(b,0,op); Put16(b,2,b.Length); Buffer.BlockCopy(data,0,b,4,data.Length); return b;
 }
 static byte[] ReadExact(Stream s,int n) {
  byte[] b=new byte[n];int p=0;
  while(p<n){int k=s.Read(b,p,n-p);if(k==0) throw new EndOfStreamException("Connection closed; do not resend blindly");p+=k;}
  return b;
 }
 static byte[] Part(byte[] b,int p,int n) { byte[] d=new byte[n];Buffer.BlockCopy(b,p,d,0,n);return d; }
 static byte[] Next(byte[] b,ref int p,out int op) {
  Need(p+4<=b.Length,"Truncated job header");op=U16(b,p);long n=U16(b,p+2);int h=4;
  if(op==0x8200){Need(p+8<=b.Length,"Truncated raster header");n=U32(b,p+4);h=8;}
  Need(n>=h && n<=b.Length-p,"Invalid packet length");byte[] data=Part(b,p+h,(int)n-h);p+=(int)n;return data;
 }
 static Job Parse(byte[] b) {
  Need(b.Length<=Limit,"Job exceeds 20 MiB");int p=0,op;byte[] d=Next(b,ref p,out op);
  Need(op==1 && d.Length==36 && U32(d,0)==0xFADEDACE,"Unsupported Windows Canon wrapper");
  Job job=new Job();
  job.Copies=(int)U32(d,4);Need(job.Copies>=1 && job.Copies<=99,"Copies must be 1..99");
  Need(U32(d,8)<=1,"Unsupported collation metadata");job.Collate=U32(d,8)==1;
  job.Duplex=(int)U32(d,12);Need(job.Duplex>=1 && job.Duplex<=3,"Unsupported duplex mode");
  d=Next(b,ref p,out op);
  while(true){
   Need(op==2 && d.Length==108 && U32(d,0)==9,"Only A4 paper is supported");
   Page page=new Page();MemoryStream parameters=new MemoryStream();
   d=Next(b,ref p,out op);Need(op==0xD0A0 && d.Length==40 && U16(d,2)==0x2A94,"LBP3300 parameters required");
   int shift=d[13];Need((U16(d,30)>>(shift&15))==2480 && (U16(d,32)>>(shift>>4))==3507,"Only A4 600 dpi geometry is supported");
   Need(d[4]==0 || d[4]==2,"Unsupported paper kind");d[4]=2;d[36]=1;
   byte[] packet=Packet(op,d);parameters.Write(packet,0,packet.Length);
   d=Next(b,ref p,out op);Need(op==0xD0A4 && d.Length==8,"Missing compression parameters");packet=Packet(op,d);parameters.Write(packet,0,packet.Length);
   d=Next(b,ref p,out op);Need(op==0xD0A1 && d.Length==0,"Missing raster start");packet=Packet(op,d);parameters.Write(packet,0,packet.Length);
   d=Next(b,ref p,out op);
   while(op==0x8200){Need(d.Length>0,"Empty raster band");page.Bands.Add(d);d=Next(b,ref p,out op);}
   Need(page.Bands.Count>0 && op==0xC0A4 && d.Length==0,"Missing raster end");
   d=Next(b,ref p,out op);Need(op==0xD0A2 && d.Length==0,"Missing page end");packet=Packet(op,d);parameters.Write(packet,0,packet.Length);
   page.Parameters=parameters.ToArray();parameters.Dispose();
   d=Next(b,ref p,out op);Need(op==4 && d.Length==4,"Missing page trailer");job.Pages.Add(page);Need(job.Pages.Count<=100,"Maximum 100 pages");
   d=Next(b,ref p,out op);if(op==3){Need(d.Length==0 && p==b.Length,"Invalid job end");return job;}
  }
 }
 static byte[] Command(Stream s,int op,byte[] data,bool reply) {
  byte[] b=Packet(op,data);s.Write(b,0,b.Length);if(!reply)return new byte[0];
  b=ReadExact(s,4);Need(U16(b,0)==op && U16(b,2)>=4 && U16(b,2)<=4096,"Invalid CAPT reply");return ReadExact(s,U16(b,2)-4);
 }
 static byte[] Ack(Stream s,int op,byte[] data) { byte[] b=Command(s,op,data,true);Need(b.Length>=2 && (b[0]&128)==0,"Printer rejected "+op.ToString("X4"));return b; }
 static ushort[] Status(Stream s) {
  byte[] b=Ack(s,0xA0A8,null);Need(b.Length>=36,"Truncated printer status");
  if((b[2]&8)!=0){byte[] e=Ack(s,0xA2A2,null);string code=e.Length>=8&&e[1]>0?"E"+U16(e,4).ToString("X3")+" "+U16(e,6).ToString("X4"):"service fault";
   throw new IOException("Canon "+code+"; inspect printer before resuming");}
  return new ushort[]{U16(b,14),U16(b,16),U16(b,18),U16(b,20)};
 }
 static byte[] Info(int id,int stage,int page) {
  byte[] b=new byte[72];DateTime t=DateTime.Now;Put32(b,4,(uint)page);b[16]=(byte)stage;b[17]=1;Put16(b,18,id);Put16(b,20,65116);Put16(b,22,65116);Put16(b,24,t.Year);
  b[26]=(byte)(t.Month-1);b[27]=(byte)t.Day;b[28]=(byte)t.Hour;b[29]=(byte)t.Minute;b[30]=(byte)t.Second;b[31]=1;return b;
 }
 static TcpClient Connect() {
  TcpClient c=new TcpClient();try{
   IAsyncResult a=c.BeginConnect("__STATION_IP__",9100,null,null);
   using(a.AsyncWaitHandle){if(!a.AsyncWaitHandle.WaitOne(5000))throw new TimeoutException("ESP not reachable");c.EndConnect(a);}
   c.NoDelay=true;c.ReceiveTimeout=70000;c.SendTimeout=70000;return c;
  }catch{c.Close();throw;}
 }
 static List<Page> Plan(Job job) {
  List<Page> result=new List<Page>();int step=job.Duplex==1?1:2;
  for(int group=0;group<(job.Collate?job.Copies:1);group++)
   for(int i=0;i<job.Pages.Count;i+=step)
    for(int copy=0;copy<(job.Collate?1:job.Copies);copy++){
     bool paired=step==2 && i+1<job.Pages.Count;
     // CAPT duplex engine consumes back image before front image.
     for(int side=0;side<(paired?2:1);side++){
      Page original=job.Pages[paired && side==0?i+1:i];
      Page page=new Page();page.Parameters=(byte[])original.Parameters.Clone();page.Bands=original.Bands;
      page.Parameters[9]=(byte)((page.Parameters[9]&~6)|(paired?(side==0?4:2):0));
      result.Add(page);
     }
    }
  return result;
 }
 // ponytail: two decoded pages ahead of engine start; validate before enlarging window.
 static void PrintJob(Stream s,Job job,Action<int,string> progress) {
  Ack(s,0xF001,BitConverter.GetBytes(job.Pages.Count*job.Copies));
  byte[] model=Ack(s,0xA1A1,null);Need(model.Length>=4 && U16(model,2)==0x2A94,"Wrong printer model");
  ushort[] counters=Status(s);Need(counters[0]==counters[1] && counters[0]==counters[2] && counters[0]==counters[3],"Printer has an unresolved page");
  progress(0,"Starting job");
  Ack(s,0xA3A2,null);byte[] reservation=new byte[8];Put16(reservation,2,30);byte[] r=Ack(s,0xA2A0,reservation);Need(r.Length==4,"Invalid reservation");int id=U16(r,2);
  Ack(s,0xE1A1,Info(id,1,0));Ack(s,0xE0A3,null);Ack(s,0xE0A2,null);Ack(s,0xE0A4,null);
  byte[] init=new byte[16];Put32(init,0,0xADEADBEE);Ack(s,0xE0A5,init);
  List<Page> plan=Plan(job);int total=plan.Count;
  for(int number=1;number<=total;number++){
   // Bound decoded pages ahead of engine start, not sheets awaiting output.
   Stopwatch timer=Stopwatch.StartNew();
   while(number>2){counters=Status(s);if(counters[1]>=number-2)break;if(timer.Elapsed.TotalSeconds>60)throw new IOException("Page completion uncertain; inspect printer before retry");Thread.Sleep(200);}
   Page page=plan[number-1];
   byte[] source=new byte[]{2,1,0xB0,9,0xB3,13,1,(byte)(page.Parameters[9]&8)};r=Ack(s,0xE1A9,source);Need(r.Length==2 && r[0]==0 && r[1]==1,"A4 cassette 1 unavailable");
   byte[] parameters=(byte[])page.Parameters.Clone();parameters[10]=1;Command(s,0xD0A9,parameters,false);
   foreach(byte[] band in page.Bands)for(int p=0;p<band.Length;p+=4092)Command(s,0xC0A0,Part(band,p,Math.Min(4092,band.Length-p)),false);
   Command(s,0xC0A4,null,false);progress(number,"Raster sent");timer.Restart();
   while(true){counters=Status(s);if(counters[0]>=number)break;if(timer.Elapsed.TotalSeconds>20)throw new IOException("Page decode timeout; outcome uncertain");Thread.Sleep(200);}
   Need(counters[0]==number,"Unexpected decoded page count; inspect printer before retry");
   Ack(s,0xE1A1,Info(id,2,number));byte[] pageNumber=new byte[2];Put16(pageNumber,0,number);Ack(s,0xE0A7,pageNumber);Ack(s,0xE1A1,Info(id,6,number));
   progress(number,"Submitted");
  }
  Stopwatch completion=Stopwatch.StartNew();
  int lastCompleted=-1;
  while(true){counters=Status(s);if(counters[3]!=lastCompleted){progress(counters[3],"Printer completed");lastCompleted=counters[3];}if(counters[3]>=total)break;if(completion.Elapsed.TotalSeconds>60)throw new IOException("Job completion uncertain; inspect printer before retry");Thread.Sleep(200);}
  Ack(s,0xE1A1,Info(id,4,total));byte[] release=new byte[2];Put16(release,0,id);Ack(s,0xE0A9,release);
  progress(total,"Completed");
 }
 [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
 struct PrinterDefaults { public IntPtr DataType,DevMode; public uint Access; }
 [System.Runtime.InteropServices.DllImport("winspool.drv",EntryPoint="OpenPrinterW",CharSet=System.Runtime.InteropServices.CharSet.Unicode,SetLastError=true)]
 static extern bool OpenPrinter(string name,out IntPtr handle,ref PrinterDefaults defaults);
 [System.Runtime.InteropServices.DllImport("winspool.drv",EntryPoint="SetPrinterW",SetLastError=true)]
 static extern bool SetPrinter(IntPtr handle,uint level,ref uint status,uint command);
 [System.Runtime.InteropServices.DllImport("winspool.drv",EntryPoint="GetPrinterW",SetLastError=true)]
 static extern bool GetPrinter(IntPtr handle,uint level,IntPtr data,uint size,out uint needed);
 [System.Runtime.InteropServices.DllImport("winspool.drv")] static extern bool ClosePrinter(IntPtr handle);
 static bool Flag(string json,string key){return System.Text.RegularExpressions.Regex.IsMatch(json,"\\\""+key+"\\\"\\s*:\\s*true");}
 static bool monitorStarted=false;
 static void MonitorPrinter(){
  string name=System.Text.Encoding.UTF8.GetString(Convert.FromBase64String("__PRINTER_NAME_BASE64__"));
  while(true){
   uint status=0;string json="";
   try{
    System.Net.HttpWebRequest request=(System.Net.HttpWebRequest)System.Net.WebRequest.Create("http://__STATION_IP__/printer/status");
    request.Proxy=null;request.Timeout=2500;request.ReadWriteTimeout=2500;
    using(System.Net.WebResponse response=request.GetResponse())using(StreamReader reader=new StreamReader(response.GetResponseStream())){
     char[] data=new char[65537];int length=0,count;while(length<data.Length && (count=reader.Read(data,length,data.Length-length))>0)length+=count;
     if(length>65536)throw new InvalidDataException("Status too large");json=new string(data,0,length);
    }
    if(!Flag(json,"connected")||!Flag(json,"responsive"))status=0x80;
    else if(!Flag(json,"fresh"))status=0x2;
    else {if(Flag(json,"service_error"))status|=2;if(Flag(json,"paper_jam"))status|=8;if(Flag(json,"paper_required"))status|=0x10;
     if(Flag(json,"cover_open"))status|=0x00400000;if(Flag(json,"toner_warning"))status|=0x20000;if(Flag(json,"active"))status|=0x400;}
   }catch{status=0x80;}
   if(File.Exists(Pending))status|=0x100002;
   try{File.WriteAllText(Path.Combine(Root,"printer-state.json"),json);
    IntPtr handle;PrinterDefaults defaults=new PrinterDefaults();defaults.Access=12;
    if(OpenPrinter(name,out handle,ref defaults)){try{uint needed;GetPrinter(handle,6,IntPtr.Zero,0,out needed);
      if(needed<4||needed>64)throw new IOException("Invalid printer status size");
      IntPtr data=System.Runtime.InteropServices.Marshal.AllocHGlobal((int)needed);uint current;
      try{if(!GetPrinter(handle,6,data,needed,out needed))throw new System.ComponentModel.Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error());current=(uint)System.Runtime.InteropServices.Marshal.ReadInt32(data);}
      finally{System.Runtime.InteropServices.Marshal.FreeHGlobal(data);}status=(current&~(uint)(0x80|2|8|0x10|0x00400000|0x20000|0x400|0x100000))|status;if(!SetPrinter(handle,6,ref status,0))File.WriteAllText(Path.Combine(Root,"monitor-error.txt"),"SetPrinter: "+System.Runtime.InteropServices.Marshal.GetLastWin32Error());}finally{ClosePrinter(handle);}}else throw new System.ComponentModel.Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error());
   }catch(Exception e){try{File.WriteAllText(Path.Combine(Root,"monitor-error.txt"),e.Message);}catch{}}
   Thread.Sleep(2000);
  }
 }
 static void Note(string text){File.WriteAllText(Path.Combine(Root,"status.txt"),DateTime.Now.ToString("s")+" "+text);}
 static void Send(string file) {
  Job job=Parse(File.ReadAllBytes(file));Need(!File.Exists(Pending),"Uncertain previous job; inspect pending.txt");
  Stopwatch elapsed=Stopwatch.StartNew();
  while(true){
   try{
    using(TcpClient c=Connect())PrintJob(c.GetStream(),job,delegate(int number,string state){
     if(number==0){using(FileStream marker=new FileStream(Pending,FileMode.CreateNew,FileAccess.Write,FileShare.Read)){
      byte[] text=System.Text.Encoding.UTF8.GetBytes(file+" job started; no automatic retry.\r\n");marker.Write(text,0,text.Length);marker.Flush(true);
     }}
     string message=state+" page "+number+" at "+elapsed.Elapsed.TotalSeconds.ToString("F2")+"s";
     File.AppendAllText(file+".progress",message+"\r\n");Note(message);
    });
    File.Delete(Pending);break;
   }catch(Exception e){if(File.Exists(Pending) || e is InvalidDataException)throw;Note("Waiting for ESP: "+e.Message);Thread.Sleep(3000);}
  }
  Note("Completed "+job.Pages.Count*job.Copies+" page(s) in "+elapsed.Elapsed.TotalSeconds.ToString("F2")+"s");
 }
 static void Receive(NetworkStream s,string file) {
  s.ReadTimeout=60000;int total=0;using(FileStream f=new FileStream(file,FileMode.CreateNew,FileAccess.Write,FileShare.Read)){
   while(true){byte[] h=ReadExact(s,4);int op=U16(h,0),header=4;long n=U16(h,2);byte[] extra=null;
    if(op==0x8200){extra=ReadExact(s,4);n=U32(extra,0);header=8;}
    Need(n>=header && n<=Limit-total,"Job exceeds limit or invalid packet length");f.Write(h,0,4);if(extra!=null)f.Write(extra,0,4);
    int left=(int)n-header;while(left>0){byte[] block=ReadExact(s,Math.Min(65536,left));f.Write(block,0,block.Length);left-=block.Length;}total+=(int)n;
    if(op==3){Need(n==4,"Invalid job end");f.Flush(true);return;}
   }
  }
 }
 sealed class TestPrinter : Stream {
  readonly Queue<byte> replies=new Queue<byte>();
  public int Reservations,Releases,Decoded,Printing,Printed,MaxAhead,MaxWaiting,StatusReads;public List<int> Raster=new List<int>();
  public override void Write(byte[] b,int offset,int count){
   int op=U16(b,offset);byte[] data=new byte[2];data[1]=1;
   if(op==0xA1A1){data=new byte[4];Put16(data,2,0x2A94);}
   if(op==0xA2A0){Reservations++;data=new byte[4];Put16(data,2,7);}
   if(op==0xE0A9)Releases++;
   if(op==0xC0A0)Raster.Add(b[offset+4]);
   if(op==0xC0A4){Decoded++;StatusReads=0;MaxAhead=Math.Max(MaxAhead,Decoded-Printed);MaxWaiting=Math.Max(MaxWaiting,Decoded-Printing);}
   if(op==0xA0A8){
    // Page one starts before it exits; page three must arrive before any exit.
    Need(++StatusReads<32,"Pipeline waited for paper exit instead of engine start");
    Printing=Math.Max(0,Decoded-1);if(Decoded>=3)Printed=Decoded;
    data=new byte[36];Put16(data,14,Decoded);Put16(data,16,Printing);Put16(data,18,Printed);Put16(data,20,Printed);
   }
   if((op>>8)==0xC0 || (op>>8)==0xD0)return;
   foreach(byte v in Packet(op,data))replies.Enqueue(v);
  }
  public override int Read(byte[] b,int offset,int count){int n=Math.Min(count,replies.Count);for(int i=0;i<n;i++)b[offset+i]=replies.Dequeue();return n;}
  public override bool CanRead{get{return true;}}public override bool CanWrite{get{return true;}}public override bool CanSeek{get{return false;}}
  public override long Length{get{throw new NotSupportedException();}}public override long Position{get{throw new NotSupportedException();}set{throw new NotSupportedException();}}
  public override void Flush(){}public override long Seek(long o,SeekOrigin so){throw new NotSupportedException();}public override void SetLength(long n){throw new NotSupportedException();}
 }
 static void SelfTest() {
  MemoryStream f=new MemoryStream();Action<int,byte[]> add=delegate(int op,byte[] d){byte[] b=Packet(op,d);f.Write(b,0,b.Length);};
  byte[] start=new byte[36];Put32(start,0,0xFADEDACE);Put32(start,4,1);Put32(start,8,1);Put32(start,12,1);add(1,start);byte[] metadata=new byte[108];Put32(metadata,0,9);add(2,metadata);
  byte[] p=new byte[40];Put16(p,2,0x2A94);p[13]=0x11;Put16(p,30,4960);Put16(p,32,7014);add(0xD0A0,p);add(0xD0A4,new byte[8]);add(0xD0A1,null);
  byte[] raster=new byte[9];Put16(raster,0,0x8200);Put16(raster,2,256);Put32(raster,4,9);raster[8]=1;f.Write(raster,0,raster.Length);add(0xC0A4,null);add(0xD0A2,null);add(4,new byte[4]);add(3,null);
  byte[] job=f.ToArray();Job parsed=Parse(job);Need(parsed.Pages.Count==1,"Parser self-test failed");
  Need(parsed.Pages[0].Parameters[8]==2 && parsed.Pages[0].Parameters[40]==1,"A4/plain normalization failed");
  byte[] copies=(byte[])job.Clone();Put32(copies,8,2);Put32(copies,12,0);Job copyJob=Parse(copies);Need(copyJob.Copies==2 && !copyJob.Collate,"Copy metadata failed");
  bool rejected=false;try{Parse(Part(job,0,job.Length-1));}catch(InvalidDataException){rejected=true;}Need(rejected,"Truncated job accepted");
  MemoryStream multi=new MemoryStream();multi.Write(job,0,40);multi.Write(job,40,job.Length-44);multi.Write(job,40,job.Length-44);multi.Write(job,job.Length-4,4);
  Need(Parse(multi.ToArray()).Pages.Count==2,"Multipage parser failed");
  byte[] bad=(byte[])job.Clone();Put32(bad,16,4);rejected=false;try{Parse(bad);}catch(InvalidDataException){rejected=true;}Need(rejected,"Invalid duplex mode accepted");
  Job pipeline=Parse(multi.ToArray());pipeline.Pages[0].Bands[0][0]=11;pipeline.Pages[1].Bands[0][0]=22;pipeline.Copies=2;
  foreach(bool collate in new bool[]{true,false}){
   pipeline.Collate=collate;TestPrinter printer=new TestPrinter();
   PrintJob(printer,pipeline,delegate(int n,string state){});
   Need(printer.Reservations==1 && printer.Releases==1 && printer.Decoded==4 && printer.MaxAhead==3 && printer.MaxWaiting==2,"Job session/pipeline regression");
   Need(String.Join(",",printer.Raster.ToArray())==(collate?"11,22,11,22":"11,11,22,22"),"Copy order regression");
  }
  pipeline.Duplex=2;pipeline.Collate=true;
  List<Page> duplexPlan=Plan(pipeline);
  Need(duplexPlan.Count==4 && duplexPlan[0].Bands[0][0]==22 && duplexPlan[1].Bands[0][0]==11,"Duplex back/front order failed");
  Need((duplexPlan[0].Parameters[9]&6)==4 && (duplexPlan[1].Parameters[9]&6)==2,"Duplex side flags failed");
  pipeline.Pages.Add(pipeline.Pages[0]);pipeline.Copies=1;duplexPlan=Plan(pipeline);
  Need(duplexPlan.Count==3 && (duplexPlan[2].Parameters[9]&6)==0,"Odd final sheet failed");
  Console.WriteLine("PASS: parser, A4, truncation, one job session, two-page pipeline, copy ordering");
 }
 public static int Main(string[] args) {
  try{
   if(args.Length==1 && args[0]=="--monitor"){Directory.CreateDirectory(Root);MonitorPrinter();return 0;}
   if(args.Length==1 && args[0]=="--self-test"){SelfTest();return 0;}
   if(args.Length==2 && args[0]=="--check"){Job j=Parse(File.ReadAllBytes(args[1]));Console.WriteLine(j.Pages.Count+" page(s), "+j.Copies+" copies");return 0;}
   Directory.CreateDirectory(Root);bool created;
   using(Mutex mutex=new Mutex(true,"Global\\ESP32CaptBridge-__STATION_ID__",out created)){
    Need(created,"Another bridge is running");
    if(args.Length==2 && args[0]=="--send"){Send(args[1]);return 0;}
    Need(args.Length==0,"Unsupported arguments");
    Thread monitor=new Thread(MonitorPrinter);monitor.IsBackground=true;monitor.Start();monitorStarted=true;
    Need(!File.Exists(Pending) && Directory.GetFiles(Root,"*.capt").Length==0,"Uncertain previous job; inspect bridge folder. Status monitor remains active.");
    TcpListener listener=new TcpListener(IPAddress.Loopback,__LOCAL_PORT__);listener.Start(1);Note("Ready on localhost:__LOCAL_PORT__");
    while(true){string file=Path.Combine(Root,DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fffffff")+".capt");
     bool empty=false;
     using(TcpClient client=listener.AcceptTcpClient()){
      try{Receive(client.GetStream(),file);}
      catch(EndOfStreamException){if(File.Exists(file) && new FileInfo(file).Length==0){File.Delete(file);empty=true;}else throw;}
     }
     if(empty)continue;
     Note("Printing "+Path.GetFileName(file));Send(file);File.Copy(file+".progress",Path.Combine(Root,"last-job.txt"),true);File.Delete(file);File.Delete(file+".progress");
    }
   }
  }catch(Exception e){try{Directory.CreateDirectory(Root);Note("STOPPED: "+e.Message);}catch{}Console.Error.WriteLine(e.ToString());if(monitorStarted){while(true)Thread.Sleep(1000);}return 1;}
 }
}
'@
$sourceCode=$sourceCode.Replace('__STATION_IP__',$StationIP).Replace('__STATION_ID__',$stationId).Replace('__LOCAL_PORT__',[string]$localPort).Replace('__PRINTER_NAME_BASE64__',[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($PrinterName)))
[IO.File]::WriteAllText("$root\bridge.cs",$sourceCode)
$staged=Join-Path $root 'bridge.new.exe'
if (Test-Path $staged) { Remove-Item $staged }
Add-Type -TypeDefinition $sourceCode -OutputAssembly $staged -OutputType ConsoleApplication
& $staged --self-test
if ($LASTEXITCODE -ne 0) { throw 'Bridge self-test failed. Existing bridge was not stopped.' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class EspRenderDriver {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
 public struct INFO3 {
  public uint cVersion;
  public string pName,pEnvironment,pDriverPath,pDataFile,pConfigFile,pHelpFile,pDependentFiles,pMonitorName,pDefaultDataType;
 }
 [DllImport("winspool.drv", EntryPoint="AddPrinterDriverExW",CharSet=CharSet.Unicode,SetLastError=true)]
 public static extern bool Add(string server,uint level,ref INFO3 info,uint flags);
}
'@
$info=New-Object EspRenderDriver+INFO3
$info.cVersion=3
$info.pName=$driverName
$info.pEnvironment='Windows x64'
$info.pDriverPath=$source.DriverPath
$info.pDataFile=$source.DataFile
$info.pConfigFile=$source.ConfigFile
$info.pHelpFile=$source.HelpFile
$info.pDependentFiles=($source.DependentFiles -join "`0")+"`0`0"
$info.pMonitorName=$null
$info.pDefaultDataType='RAW'
if (!(Get-PrinterDriver -Name $driverName -ErrorAction SilentlyContinue) -and ![EspRenderDriver]::Add($null,3,[ref]$info,0x18)) { throw (New-Object ComponentModel.Win32Exception([Runtime.InteropServices.Marshal]::GetLastWin32Error())) }
$task=Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($task) { Stop-ScheduledTask -TaskName $taskName; Start-Sleep -Seconds 1 }
Move-Item -Force $staged "$root\bridge.exe"
& icacls.exe "$root\*" /reset /T /Q | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot secure bridge files.' }
$a=New-ScheduledTaskAction -Execute "$root\bridge.exe"
$s=New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $taskName -Action $a -Trigger (New-ScheduledTaskTrigger -AtStartup) -Settings $s -User SYSTEM -RunLevel Highest -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
$ready=$false
for($i=0;$i -lt 10;$i++) {
 Start-Sleep -Seconds 1
 if (Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort $localPort -State Listen -ErrorAction SilentlyContinue) { $ready=$true;break }
}
if (!$ready) { Get-Content "$root\status.txt" -ErrorAction SilentlyContinue;throw 'Bridge did not start.' }
if (!$port) { Add-PrinterPort -Name $portName -PrinterHostAddress '127.0.0.1' -PortNumber $localPort }
if ($existing) {
 if ($existing.Name -ne $printerName) { Rename-Printer -Name $existing.Name -NewName $printerName }
 Set-Printer -Name $printerName -DriverName $driverName -PortName $portName
 foreach ($duplicate in $stationQueues) {
  if ($duplicate.Name -ne $existing.Name -and $duplicate.Name -ne $printerName) { Remove-Printer -Name $duplicate.Name }
 }
}
else { Add-Printer -Name $printerName -DriverName $driverName -PortName $portName }
if (!$existing) { Set-PrintConfiguration -PrinterName $printerName -PaperSize A4 -DuplexingMode OneSided }
@{id=$stationId;ip=$StationIP;name=$PrinterName} | ConvertTo-Json | Set-Content -Encoding UTF8 "$statusRoot\station.json"
$created=Get-Printer -Name $printerName
if ($created.PortName -ne $portName -or $created.DriverName -ne $driverName) { throw 'Printer verification failed.' }
Write-Host "Ready: $printerName"
Write-Host 'Use A4 and choose portrait/landscape, simplex/duplex and layout in the Canon driver. Cassette 1 is selected by the bridge.'
Write-Host 'Local bridge starts with Windows. No Mac, remote SSH, or always-on intermediary is required.'
Write-Host "If printing stops, inspect $root\status.txt and pending.txt. Never resend an uncertain page blindly."
