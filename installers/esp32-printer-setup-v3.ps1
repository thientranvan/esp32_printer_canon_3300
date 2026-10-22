#requires -RunAsAdministrator
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$root=Join-Path $env:ProgramData 'ESP32Printer'
$printerName='Canon LBP3300 (ESP32 LAN)'
$driverName='Canon LBP3300 ESP32 Render'
$portName='ESP32_LOCAL_19100'
if (![Environment]::Is64BitProcess) { throw 'Run 64-bit Windows PowerShell as administrator.' }
$source=Get-CimInstance Win32_PrinterDriver | Where-Object Name -eq 'Canon LBP3300,3,Windows x64'
if (!$source) { throw 'Install the official Canon LBP3300 x64 driver first, then rerun this installer.' }
$existing=Get-Printer -Name $printerName -ErrorAction SilentlyContinue
if ($existing -and $existing.JobCount -ne 0) { throw 'Print queue is not empty. Resolve existing jobs first; no jobs were deleted.' }
if ((Test-Path "$root\pending.txt") -or @(Get-ChildItem "$root\*.capt" -ErrorAction SilentlyContinue).Count) { throw 'Unresolved bridge job found. Inspect pending.txt/status.txt before reinstalling; nothing was resent.' }
$port=Get-PrinterPort -Name $portName -ErrorAction SilentlyContinue
if ($port -and ($port.PrinterHostAddress -ne '127.0.0.1' -or $port.PortNumber -ne 19100)) { throw 'Existing bridge port has conflicting settings.' }
$listener=Get-NetTCPConnection -LocalPort 19100 -State Listen -ErrorAction SilentlyContinue
foreach ($connection in $listener) {
 if ((Get-Process -Id $connection.OwningProcess).Path -ne "$root\bridge.exe") { throw 'Local port 19100 is used by another program.' }
}
New-Item -ItemType Directory -Force $root | Out-Null
& icacls.exe $root /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot secure bridge directory.' }
$sourceCode=@'
using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Collections.Generic;
using System.Threading;
using System.Diagnostics;

// ponytail: LBP3300 Windows CAPT wrapper, A4/simplex/cassette 1, 100 pages/20 MiB.
// Extend only after capturing and testing another model/paper/duplex format.
public static class Esp32CaptBridge {
 const int Limit = 20 * 1024 * 1024;
 static readonly string Root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "ESP32Printer");
 static readonly string Pending = Path.Combine(Root, "pending.txt");
 sealed class Page { public byte[] Parameters; public List<byte[]> Bands = new List<byte[]>(); }
 sealed class Job { public List<Page> Pages = new List<Page>(); public int Copies = 1; public bool Collate = true; }
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
  Need(U32(d,8)==1,"Only one-sided jobs are supported");
  Need(U32(d,12)<=1,"Unsupported collation metadata");job.Collate=U32(d,12)==1;
  d=Next(b,ref p,out op);
  while(true){
   Need(op==2 && d.Length==108 && U32(d,0)==9,"Only A4 paper is supported");
   Page page=new Page();MemoryStream parameters=new MemoryStream();
   d=Next(b,ref p,out op);Need(op==0xD0A0 && d.Length==40 && U16(d,2)==0x2A94,"LBP3300 parameters required");
   Need((d[5]&8)==0,"Duplex is not supported yet");
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
 static byte[] Command(NetworkStream s,int op,byte[] data,bool reply) {
  byte[] b=Packet(op,data);s.Write(b,0,b.Length);if(!reply)return new byte[0];
  b=ReadExact(s,4);Need(U16(b,0)==op && U16(b,2)>=4 && U16(b,2)<=4096,"Invalid CAPT reply");return ReadExact(s,U16(b,2)-4);
 }
 static byte[] Ack(NetworkStream s,int op,byte[] data) { byte[] b=Command(s,op,data,true);Need(b.Length>=2 && (b[0]&128)==0,"Printer rejected "+op.ToString("X4"));return b; }
 static ushort[] Status(NetworkStream s) { byte[] b=Ack(s,0xA0A8,null);Need(b.Length>=36,"Truncated printer status");return new ushort[]{U16(b,14),U16(b,16),U16(b,18),U16(b,20)}; }
 static byte[] Info(int id,int stage,int page) {
  byte[] b=new byte[72];DateTime t=DateTime.Now;Put32(b,4,(uint)page);b[16]=(byte)stage;b[17]=1;Put16(b,18,id);Put16(b,20,65116);Put16(b,22,65116);Put16(b,24,t.Year);
  b[26]=(byte)(t.Month-1);b[27]=(byte)t.Day;b[28]=(byte)t.Hour;b[29]=(byte)t.Minute;b[30]=(byte)t.Second;b[31]=1;return b;
 }
 static TcpClient Connect() {
  TcpClient c=new TcpClient();try{
   IAsyncResult a=c.BeginConnect("192.168.1.199",9100,null,null);
   using(a.AsyncWaitHandle){if(!a.AsyncWaitHandle.WaitOne(5000))throw new TimeoutException("ESP not reachable");c.EndConnect(a);}
   c.NoDelay=true;c.ReceiveTimeout=70000;c.SendTimeout=70000;return c;
  }catch{c.Close();throw;}
 }
 static void PrintPage(NetworkStream s,Page page,string file,int number) {
  byte[] model=Ack(s,0xA1A1,null);Need(model.Length>=4 && U16(model,2)==0x2A94,"Wrong printer model");
  ushort[] counters=Status(s);Need(counters[0]==counters[1] && counters[0]==counters[2] && counters[0]==counters[3],"Printer has an unresolved page");
  using(FileStream marker=new FileStream(Pending,FileMode.CreateNew,FileAccess.Write,FileShare.Read)){
   byte[] text=System.Text.Encoding.UTF8.GetBytes(file+" page "+number+" started; no automatic retry.\r\n");marker.Write(text,0,text.Length);marker.Flush(true);
  }
  Ack(s,0xA3A2,null);byte[] reservation=new byte[8];Put16(reservation,2,30);byte[] r=Ack(s,0xA2A0,reservation);Need(r.Length==4,"Invalid reservation");int id=U16(r,2);
  Ack(s,0xE1A1,Info(id,1,0));Ack(s,0xE0A3,null);Ack(s,0xE0A2,null);Ack(s,0xE0A4,null);
  byte[] init=new byte[16];Put32(init,0,0xADEADBEE);Ack(s,0xE0A5,init);
  byte[] source=new byte[]{2,1,0xB0,9,0xB3,13,1,0};r=Ack(s,0xE1A9,source);Need(r.Length==2 && r[0]==0 && r[1]==1,"A4 cassette 1 unavailable");
  byte[] parameters=(byte[])page.Parameters.Clone();parameters[10]=1;Command(s,0xD0A9,parameters,false);
  foreach(byte[] band in page.Bands)for(int p=0;p<band.Length;p+=4092)Command(s,0xC0A0,Part(band,p,Math.Min(4092,band.Length-p)),false);
  Command(s,0xC0A4,null,false);Stopwatch timer=Stopwatch.StartNew();
  while(true){counters=Status(s);if(counters[0]>=1)break;if(timer.Elapsed.TotalSeconds>20)throw new IOException("Page decode timeout; outcome uncertain");Thread.Sleep(500);}
  int index=counters[0];Ack(s,0xE1A1,Info(id,2,index));byte[] pageNumber=new byte[2];Put16(pageNumber,0,index);Ack(s,0xE0A7,pageNumber);Ack(s,0xE1A1,Info(id,6,index));timer.Restart();
  while(true){counters=Status(s);if(counters[3]>=index)break;if(timer.Elapsed.TotalSeconds>60)throw new IOException("Page completion uncertain; inspect printer before retry");Thread.Sleep(1000);}
  Ack(s,0xE1A1,Info(id,4,index));byte[] release=new byte[2];Put16(release,0,id);Ack(s,0xE0A9,release);
  File.AppendAllText(file+".progress","Completed page "+number+"\r\n");File.Delete(Pending);
 }
 static void Note(string text){File.WriteAllText(Path.Combine(Root,"status.txt"),DateTime.Now.ToString("s")+" "+text);}
 static void Send(string file) {
  Job job=Parse(File.ReadAllBytes(file));Need(!File.Exists(Pending),"Uncertain previous page; inspect pending.txt");
  for(int number=0;number<job.Copies*job.Pages.Count;number++){
   int page=job.Collate?number%job.Pages.Count:number/job.Copies;
   while(true){
    try{using(TcpClient c=Connect())PrintPage(c.GetStream(),job.Pages[page],file,number+1);break;}
    catch(Exception e){if(File.Exists(Pending) || e is InvalidDataException)throw;Note("Waiting for ESP: "+e.Message);Thread.Sleep(3000);}
   }
  }
  Note("Completed "+job.Pages.Count*job.Copies+" page(s)");
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
 static void SelfTest() {
  MemoryStream f=new MemoryStream();Action<int,byte[]> add=delegate(int op,byte[] d){byte[] b=Packet(op,d);f.Write(b,0,b.Length);};
  byte[] start=new byte[36];Put32(start,0,0xFADEDACE);Put32(start,4,1);Put32(start,8,1);Put32(start,12,1);add(1,start);byte[] metadata=new byte[108];Put32(metadata,0,9);add(2,metadata);
  byte[] p=new byte[40];Put16(p,2,0x2A94);p[13]=0x11;Put16(p,30,4960);Put16(p,32,7014);add(0xD0A0,p);add(0xD0A4,new byte[8]);add(0xD0A1,null);
  byte[] raster=new byte[9];Put16(raster,0,0x8200);Put16(raster,2,256);Put32(raster,4,9);raster[8]=1;f.Write(raster,0,raster.Length);add(0xC0A4,null);add(0xD0A2,null);add(4,new byte[4]);add(3,null);
  byte[] job=f.ToArray();Job parsed=Parse(job);Need(parsed.Pages.Count==1,"Parser self-test failed");
  Need(parsed.Pages[0].Parameters[8]==2 && parsed.Pages[0].Parameters[40]==1,"A4/plain normalization failed");
  byte[] copies=(byte[])job.Clone();Put32(copies,8,2);Put32(copies,16,0);Job copyJob=Parse(copies);Need(copyJob.Copies==2 && !copyJob.Collate,"Copy metadata failed");
  bool rejected=false;try{Parse(Part(job,0,job.Length-1));}catch(InvalidDataException){rejected=true;}Need(rejected,"Truncated job accepted");
  MemoryStream multi=new MemoryStream();multi.Write(job,0,40);multi.Write(job,40,job.Length-44);multi.Write(job,40,job.Length-44);multi.Write(job,job.Length-4,4);
  Need(Parse(multi.ToArray()).Pages.Count==2,"Multipage parser failed");
  byte[] bad=(byte[])job.Clone();Put32(bad,12,2);rejected=false;try{Parse(bad);}catch(InvalidDataException){rejected=true;}Need(rejected,"Duplex job accepted");
  Console.WriteLine("PASS: Windows CAPT parser, A4 normalization, truncated-input rejection");
 }
 public static int Main(string[] args) {
  try{
   if(args.Length==1 && args[0]=="--self-test"){SelfTest();return 0;}
   if(args.Length==2 && args[0]=="--check"){Job j=Parse(File.ReadAllBytes(args[1]));Console.WriteLine(j.Pages.Count+" page(s), "+j.Copies+" copies");return 0;}
   Directory.CreateDirectory(Root);bool created;
   using(Mutex mutex=new Mutex(true,"Global\\ESP32CaptBridge",out created)){
    Need(created,"Another bridge is running");Need(!File.Exists(Pending),"Uncertain previous page; inspect pending.txt");
    if(args.Length==2 && args[0]=="--send"){Send(args[1]);return 0;}
    Need(args.Length==0,"Unsupported arguments");Need(Directory.GetFiles(Root,"*.capt").Length==0,"Unresolved captured job; inspect bridge folder");
    TcpListener listener=new TcpListener(IPAddress.Loopback,19100);listener.Start(1);Note("Ready on localhost:19100");
    while(true){string file=Path.Combine(Root,DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fffffff")+".capt");
     bool empty=false;
     using(TcpClient client=listener.AcceptTcpClient()){
      try{Receive(client.GetStream(),file);}
      catch(EndOfStreamException){if(File.Exists(file) && new FileInfo(file).Length==0){File.Delete(file);empty=true;}else throw;}
     }
     if(empty)continue;
     Note("Printing "+Path.GetFileName(file));Send(file);File.Delete(file);if(File.Exists(file+".progress"))File.Delete(file+".progress");
    }
   }
  }catch(Exception e){try{Directory.CreateDirectory(Root);Note("STOPPED: "+e.Message);}catch{}Console.Error.WriteLine(e.ToString());return 1;}
 }
}
'@
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
if (![EspRenderDriver]::Add($null,3,[ref]$info,0x18)) { throw (New-Object ComponentModel.Win32Exception([Runtime.InteropServices.Marshal]::GetLastWin32Error())) }
$task=Get-ScheduledTask -TaskName ESP32CaptBridge -ErrorAction SilentlyContinue
if ($task) { Stop-ScheduledTask -TaskName ESP32CaptBridge; Start-Sleep -Seconds 1 }
Move-Item -Force $staged "$root\bridge.exe"
& icacls.exe "$root\*" /reset /T /Q | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot secure bridge files.' }
$a=New-ScheduledTaskAction -Execute "$root\bridge.exe"
$s=New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName ESP32CaptBridge -Action $a -Trigger (New-ScheduledTaskTrigger -AtStartup) -Settings $s -User SYSTEM -RunLevel Highest -Force | Out-Null
Start-ScheduledTask -TaskName ESP32CaptBridge
$ready=$false
for($i=0;$i -lt 10;$i++) {
 Start-Sleep -Seconds 1
 if (Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 19100 -State Listen -ErrorAction SilentlyContinue) { $ready=$true;break }
}
if (!$ready) { Get-Content "$root\status.txt" -ErrorAction SilentlyContinue;throw 'Bridge did not start.' }
if (!$port) { Add-PrinterPort -Name $portName -PrinterHostAddress '127.0.0.1' -PortNumber 19100 }
if ($existing) { Set-Printer -Name $printerName -DriverName $driverName -PortName $portName }
else { Add-Printer -Name $printerName -DriverName $driverName -PortName $portName }
Set-PrintConfiguration -PrinterName $printerName -PaperSize A4 -DuplexingMode OneSided
$created=Get-Printer -Name $printerName
if ($created.PortName -ne $portName -or $created.DriverName -ne $driverName) { throw 'Printer verification failed.' }
Write-Host "Ready: $printerName"
Write-Host 'Use A4, one-sided printing. Cassette 1 is selected by the bridge.'
Write-Host 'Local bridge starts with Windows. No Mac, remote SSH, or always-on intermediary is required.'
Write-Host "If printing stops, inspect $root\status.txt and pending.txt. Never resend an uncertain page blindly."
