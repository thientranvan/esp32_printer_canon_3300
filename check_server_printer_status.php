<?php
// php check_server_printer_status.php /var/www/lop1/app/printer_repository.php
$source=file_get_contents($argv[1]??'/var/www/lop1/app/printer_repository.php');
$start=strpos($source,'function printerStatus(');
$end=strpos($source,'function printerList(',$start);
if($start===false||$end===false)throw new Exception('printerStatus not found');
function printerEspHeartbeat(array $h): array {return $h;}
eval(substr($source,$start,$end-$start));
function expectReason(array $h,string $expected,?string $seen=null): void {
 $s=['enabled'=>1,'last_seen'=>$seen??gmdate('Y-m-d H:i:s'),'heartbeat_json'=>json_encode($h)];
 $r=printerStatus($s);
 if($r['reason']!==$expected||$r['ready'])throw new Exception('Unexpected status: '.$r['reason']);
}
$h=['telemetry_seq'=>1,'printer'=>['state'=>'disconnected','usb_connected'=>false,'usb_responsive'=>false,'capt'=>['fresh'=>false,'age_ms'=>null]]];
expectReason($h,'Máy in chưa kết nối USB');
$h['printer']['state']='recovering';expectReason($h,'Đang khôi phục USB');
$h['printer']['state']='unresponsive';$h['printer']['usb_connected']=true;expectReason($h,'Máy in không phản hồi');
$h['printer']['error_code']=7;expectReason($h,'Lỗi USB máy in (mã 7)');
$h['printer']['error_code']=0;$h['printer']['state']='idle';$h['printer']['usb_responsive']=true;expectReason($h,'Chưa có trạng thái CAPT mới');
expectReason($h,'Mất kết nối ESP32','2000-01-01 00:00:00');
echo "PASS: USB state precedes stale CAPT; offline and readiness preserved\n";
