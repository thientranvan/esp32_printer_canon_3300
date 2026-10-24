param([Parameter(Mandatory=$true)][string]$Config)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$settings=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$created=$false
$mutex=New-Object Threading.Mutex($true,"Local\ESP32PrinterStatus-$($settings.id)",[ref]$created)
if (!$created) {exit}
function Get-DeviceView($s) {
 $title='Sẵn sàng';$detail='Máy in đang rảnh.';$kind='ready'
 if (!$s) {$title='Mất kết nối trạm in';$detail='Kiểm tra nguồn ESP và mạng LAN.';$kind='offline'}
 elseif (!$s.connected) {$title='Máy in chưa kết nối';$detail='Kiểm tra nguồn máy in và dây USB.';$kind='offline'}
 elseif (!$s.responsive -or !$s.capt.fresh) {$title='Chưa có trạng thái mới';$detail='Không thể xác nhận tiến trình máy in.';$kind='warning'}
 else {
  $errors=@()
  if ($s.capt.service_error) {$errors+="Canon $($s.capt.service_code) — lỗi máy in, cần kiểm tra / bảo dưỡng."}
  if ($s.capt.paper_jam) {$errors+='Kẹt giấy. Kiểm tra đường giấy.'}
  if ($s.capt.cover_open) {$errors+='Nắp máy in đang mở.'}
  if ($s.capt.paper_required) {$errors+='Kiểm tra giấy, khổ giấy và khay giấy đã chọn.'}
  if ($s.capt.toner_warning) {$errors+='Cảnh báo mực.'}
  if ($errors.Count) {$title=if ($s.capt.service_error) {"Canon $($s.capt.service_code)"} else {'Máy in cần xử lý'};$detail=$errors -join "`r`n";$kind=if ($s.capt.service_error -or $s.capt.paper_jam) {'error'} else {'warning'}}
  elseif ($s.job_control.state -eq 'uncertain') {$title='Lệnh cần đối soát';$detail='Chưa rõ kết quả lệnh trước; không tự in lại.';$kind='warning'}
  elseif ($s.job_control.state -eq 'paused') {$title='Đã tạm dừng';$detail='Lệnh đang tạm dừng gửi.';$kind='warning'}
  elseif ($s.active) {$title='Đang in';$detail='Máy in đang xử lý lệnh.';$kind='busy'}
 }
 $j=$s.job_control
 $phases=@{printing='Đang in';paused='Đã tạm dừng';failed='Lệnh lỗi';uncertain='Chưa rõ kết quả';completed='Đã hoàn tất';resolved='Đã đối soát'}
 $progress=if (!$s) {'Mất kết nối — chưa có tiến trình mới'} elseif ($j.id -and $j.state -eq 'uncertain') {'Lệnh cần đối soát — chưa rõ kết quả in'} elseif (!$j.id -or !$j.active) {'Trạm hiện không có lệnh đang chạy'} else {
  $phase=if ($phases.ContainsKey([string]$j.state)) {$phases[[string]$j.state]} else {$j.state}
  if ($j.total -gt 0) {"$phase · Đã ghi nhận $($j.completed) / $($j.total) mặt in"} else {"$phase · Chưa có tổng số mặt in"}
 }
 [pscustomobject]@{Title=$title;Detail=$detail;Kind=$kind;Progress=$progress}
}
# Native vector icon, shared visual design with the Mac Dock icon. No download.
Add-Type -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
public static class ESP32StatusIcon {
 [DllImport("shell32.dll", CharSet=CharSet.Unicode)] public static extern int SetCurrentProcessExplicitAppUserModelID(string id);
 [DllImport("user32.dll")] static extern bool DestroyIcon(IntPtr icon);
 static void Box(Graphics g, int x,int y,int w,int h,int r,Color color) {
  using(var p=new GraphicsPath()) using(var b=new SolidBrush(color)) {
   p.AddArc(x,y,r*2,r*2,180,90);p.AddArc(x+w-r*2,y,r*2,r*2,270,90);
   p.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);p.AddArc(x,y+h-r*2,r*2,r*2,90,90);p.CloseFigure();g.FillPath(b,p);
  }
 }
 public static Icon Create() {
  using(var bitmap=new Bitmap(256,256)) {
   using(var g=Graphics.FromImage(bitmap)) {
    g.SmoothingMode=SmoothingMode.AntiAlias;
    Box(g,12,12,232,232,52,Color.FromArgb(8,107,97));
    Box(g,66,54,106,72,6,Color.FromArgb(224,245,245));
    Box(g,43,98,170,88,18,Color.FromArgb(173,217,222));
    Box(g,57,119,142,50,10,Color.FromArgb(23,56,71));
    Box(g,69,147,118,68,6,Color.White);
    Box(g,87,166,82,7,3,Color.FromArgb(148,184,191));Box(g,87,183,62,7,3,Color.FromArgb(148,184,191));
    using(var b=new SolidBrush(Color.FromArgb(66,227,163))) g.FillEllipse(b,182,104,10,10);
    using(var pen=new Pen(Color.White,5)) {pen.StartCap=pen.EndCap=LineCap.Round;g.DrawArc(pen,169,30,48,48,210,120);g.DrawArc(pen,179,40,28,28,210,120);}
    g.FillEllipse(Brushes.White,190,51,6,6);
   }
   IntPtr handle=bitmap.GetHicon();try {using(var icon=Icon.FromHandle(handle))return (Icon)icon.Clone();} finally {DestroyIcon(handle);}
  }
 }
}
"@ -ReferencedAssemblies System.Drawing
[void][ESP32StatusIcon]::SetCurrentProcessExplicitAppUserModelID("ESP32Printer.Status.$($settings.id)")
$appIcon=[ESP32StatusIcon]::Create()
$form=New-Object Windows.Forms.Form
$form.Icon=$appIcon
$form.Text="$($settings.name) — Canon LBP3300";$form.ClientSize=New-Object Drawing.Size(780,610)
$form.MinimumSize=$form.Size;$form.StartPosition='CenterScreen';$form.BackColor=[Drawing.Color]::FromArgb(242,245,250)
$form.Font=New-Object Drawing.Font('Segoe UI',10)
function Add-Label($value,$x,$y,$w,$h,$size=10) {
 $l=New-Object Windows.Forms.Label;$l.Text=$value;$l.SetBounds($x,$y,$w,$h);$l.Font=New-Object Drawing.Font('Segoe UI',$size);$form.Controls.Add($l);return $l
}
$heading=Add-Label $settings.name 26 18 720 34 20
$identity=Add-Label "Canon LBP3300  |  $($settings.ip)" 28 57 720 24
$card=New-Object Windows.Forms.Panel;$card.SetBounds(26,94,728,132);$card.BackColor=[Drawing.Color]::White;$form.Controls.Add($card)
$icon=New-Object Windows.Forms.Label;$icon.SetBounds(16,6,68,68);$icon.Font=New-Object Drawing.Font('Segoe UI Symbol',38);$icon.TextAlign='MiddleCenter';$icon.AccessibleName='Trạng thái máy in';$card.Controls.Add($icon)
$state=New-Object Windows.Forms.Label;$state.SetBounds(100,8,606,64);$state.TextAlign='MiddleLeft';$state.Font=New-Object Drawing.Font('Segoe UI',17,[Drawing.FontStyle]::Bold);$card.Controls.Add($state)
$message=New-Object Windows.Forms.Label;$message.SetBounds(102,80,596,44);$card.Controls.Add($message)
$progressText=Add-Label '' 28 240 720 24
$bar=New-Object Windows.Forms.ProgressBar;$bar.SetBounds(28,270,724,10);$form.Controls.Add($bar)
$queueTitle=Add-Label 'Hàng đợi Windows' 28 296 720 26 12
$list=New-Object Windows.Forms.ListView;$list.SetBounds(28,328,724,126);$list.View='Details';$list.FullRowSelect=$true
foreach ($column in @(@('ID',45),@('Tài liệu',315),@('Trạng thái',200),@('Trang spool',130))) {[void]$list.Columns.Add($column[0],$column[1])}
$form.Controls.Add($list)
$footer=Add-Label 'Đang kết nối…' 28 510 724 32 9
function Add-Button($caption,$x,$width,$action) {$b=New-Object Windows.Forms.Button;$b.Text=$caption;$b.SetBounds($x,556,$width,34);$b.FlatStyle='Flat';$b.BackColor=[Drawing.Color]::White;$b.add_Click($action);$form.Controls.Add($b)}
Add-Button 'Mở hàng đợi' 28 145 {Start-Process rundll32.exe -ArgumentList "printui.dll,PrintUIEntry /o /n `"$($settings.name)`""}
Add-Button 'Tùy chọn in' 185 145 {Start-Process rundll32.exe -ArgumentList "printui.dll,PrintUIEntry /e /n `"$($settings.name)`""}
Add-Button 'Quản lý trạm in' 342 165 {Start-Process "http://$($settings.ip)"}
Add-Button 'Ẩn cửa sổ' 607 145 {$form.Hide()}
function Control-LocalJob($action) {
 if ($list.SelectedItems.Count -ne 1) {return}
 $id=[int]$list.SelectedItems[0].Text
 if ($action -eq 'cancel' -and [Windows.Forms.MessageBox]::Show("Hủy lệnh #${id}? Trang đã gửi vào máy in vẫn có thể ra giấy. Nếu đang gửi dữ liệu, cần kiểm tra kết quả trước khi in lại.",'Hủy lệnh in','YesNo','Warning') -ne 'Yes') {return}
 try {
  switch ($action) {
   'pause' {Suspend-PrintJob -PrinterName $settings.name -ID $id -ErrorAction Stop}
   'resume' {Resume-PrintJob -PrinterName $settings.name -ID $id -ErrorAction Stop}
   'cancel' {Remove-PrintJob -PrinterName $settings.name -ID $id -ErrorAction Stop}
  }
  $script:nextPoll=[datetime]::MinValue
 } catch {[void][Windows.Forms.MessageBox]::Show($_.Exception.Message,'Không thực hiện được','OK','Error')}
}
$pause=New-Object Windows.Forms.Button;$pause.Text='Ⅱ Tạm dừng';$pause.SetBounds(28,466,145,34);$pause.Enabled=$false;$pause.add_Click({Control-LocalJob 'pause'});$form.Controls.Add($pause)
$resume=New-Object Windows.Forms.Button;$resume.Text='▶ Tiếp tục';$resume.SetBounds(185,466,145,34);$resume.Enabled=$false;$resume.add_Click({Control-LocalJob 'resume'});$form.Controls.Add($resume)
$cancel=New-Object Windows.Forms.Button;$cancel.Text='× Xóa lệnh';$cancel.SetBounds(342,466,145,34);$cancel.Enabled=$false;$cancel.add_Click({Control-LocalJob 'cancel'});$form.Controls.Add($cancel)
$list.add_SelectedIndexChanged({$selected=$list.SelectedItems.Count -eq 1;$pause.Enabled=$selected;$resume.Enabled=$selected;$cancel.Enabled=$selected})
function Show-Monitor {$form.Show();$form.WindowState='Normal';$form.Activate()}
$tray=New-Object Windows.Forms.NotifyIcon;$tray.Icon=$appIcon;$tray.Text='Canon LBP3300 — ESP32';$tray.Visible=$true
$tray.add_DoubleClick({Show-Monitor})
$menu=New-Object Windows.Forms.ContextMenuStrip
[void]$menu.Items.Add('Mở trạng thái',$null,{Show-Monitor})
[void]$menu.Items.Add('Thoát theo dõi',$null,{$script:exitMonitor=$true;$form.Close()})
$tray.ContextMenuStrip=$menu
$form.add_FormClosing({param($sender,$eventArgs) if (!$script:exitMonitor) {$eventArgs.Cancel=$true;$form.Hide()}})
$eventPath=Join-Path (Split-Path $Config) 'print-event.txt'
$script:lastEvent=if (Test-Path $eventPath) {[IO.File]::ReadAllText($eventPath)} else {''}
$script:lastSignal='';$script:lastJobs='';$script:nextPoll=[datetime]::MinValue;$script:download=$null;$script:client=$null
$timer=New-Object Windows.Forms.Timer;$timer.Interval=250
$timer.add_Tick({
 try {
  if (Test-Path $eventPath) {
   try {$eventId=[IO.File]::ReadAllText($eventPath);if ($eventId -and $eventId -ne $script:lastEvent) {$script:lastEvent=$eventId;Show-Monitor}} catch {}
  }
  if ($script:download -and ($script:download.IsCompleted -or ((Get-Date)-$script:requestStart).TotalSeconds -ge 2)) {
   $s=$null
   try {if ($script:download.IsCompleted) {$s=$script:download.GetAwaiter().GetResult() | ConvertFrom-Json}} catch {}
   $script:client.CancelAsync();$script:client.Dispose();$script:download=$null
   $view=Get-DeviceView $s
   $state.Text=$view.Title;$message.Text=$view.Detail;$progressText.Text=$view.Progress
   $state.ForeColor=switch ($view.Kind) {'error' {[Drawing.Color]::FromArgb(185,45,45)} 'warning' {[Drawing.Color]::FromArgb(133,84,0)} 'offline' {[Drawing.Color]::DimGray} 'busy' {[Drawing.Color]::FromArgb(23,84,161)} default {[Drawing.Color]::FromArgb(26,110,85)}}
   $icon.ForeColor=$state.ForeColor
   $icon.Text=switch ($view.Kind) {'ready' {[string][char]0x2713} 'busy' {[string][char]0x21BB} 'warning' {[string][char]0x26A0} 'error' {[string][char]0x2715} default {[string][char]0x2298}}
   $bar.Style='Continuous';$bar.Value=0
   if ($s.job_control.active -and $s.job_control.total -gt 0) {$bar.Value=[int][Math]::Max(0,[Math]::Min(100,100.0*$s.job_control.completed/$s.job_control.total))}
   elseif ($s.job_control.active) {$bar.Style='Marquee'}
   $signal=if ($view.Kind -in @('error','warning','offline')) {$view.Title+$view.Detail} else {''}
   if ($signal -and $signal -ne $script:lastSignal) {Show-Monitor};$script:lastSignal=$signal
   $footer.Text="Cập nhật $(Get-Date -Format HH:mm:ss) | Bộ đếm tính theo mặt in; spool không xác nhận giấy đã ra."
  }
  if (!$script:download -and (Get-Date) -ge $script:nextPoll) {
   $settings=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
   $heading.Text=$settings.name;$identity.Text="Canon LBP3300  |  $($settings.ip)"
   $jobs=@(Get-PrintJob -PrinterName $settings.name -ErrorAction SilentlyContinue)
   $key=($jobs | ForEach-Object {$_.ID}) -join ','
   if ($key -and $key -ne $script:lastJobs) {Show-Monitor};$script:lastJobs=$key
   $queueTitle.Text="Hàng đợi Windows — $($jobs.Count) lệnh"
   $selectedId=if ($list.SelectedItems.Count -eq 1) {$list.SelectedItems[0].Text} else {''}
   $list.BeginUpdate();$list.Items.Clear()
   foreach ($job in $jobs) {
    $item=New-Object Windows.Forms.ListViewItem([string]$job.ID)
    [void]$item.SubItems.Add([string]$job.DocumentName);[void]$item.SubItems.Add([string]$job.JobStatus)
    [void]$item.SubItems.Add("$($job.PagesPrinted) / $($job.TotalPages)");[void]$list.Items.Add($item);if ($item.Text -eq $selectedId) {$item.Selected=$true}
   }
   $list.EndUpdate()
   $script:client=New-Object Net.WebClient;$script:client.Encoding=[Text.Encoding]::UTF8;$script:client.Proxy=$null
   $script:requestStart=Get-Date;$script:download=$script:client.DownloadStringTaskAsync("http://$($settings.ip)/printer/status")
   $script:nextPoll=(Get-Date).AddSeconds(1)
  }
 } catch {$footer.Text="Không đọc được trạng thái: $($_.Exception.Message)"}
})
$timer.Start()
try {[Windows.Forms.Application]::Run($form)} finally {$timer.Dispose();if ($script:client) {$script:client.CancelAsync();$script:client.Dispose()};$tray.Dispose();$appIcon.Dispose();$mutex.Dispose()}
