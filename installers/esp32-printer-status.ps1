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
