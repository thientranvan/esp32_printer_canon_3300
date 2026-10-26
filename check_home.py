"""Offline homepage behavior check: python3 check_home.py (requires Node)."""
from pathlib import Path
import json
import re
import subprocess

source = Path('station/station.ino').read_text()
html = source.split('void home() {', 1)[1].split('R"HTML(', 1)[1].split(')HTML"', 1)[0]
ids = re.findall(r'\bid="([^"]+)"', html)
assert len(ids) == len(set(ids)), 'Duplicate HTML IDs'
assert 'fetch(\'/support/command' not in html
assert 'innerHTML' not in html
assert '<details id="led-guide">' in html and '<details id="led-guide" open' not in html
assert 'Chớp nhẹ 1 lần mỗi 3 giây' in html and 'Chớp đôi mỗi 1,6 giây' in html
for field in ['support-start','support-stop','support-mac']:
    assert f"copyField('{field}')" in html
assert 'Enable-NetFirewallRule -Name ESP32PrinterSupport' in html
assert 'Disable-NetFirewallRule -Name ESP32PrinterSupport' in html
assert 'Remove-NetFirewallRule' not in html, 'Stop must preserve rule for restart'
assert 'ESP32CaptBridge' not in html, 'Support commands must not stop printing'

for action in ['/station', '/wifi', '/wifi/ap', '/usb/start', '/cloud/resolve']:
    assert re.search(r'<form[^>]*action="'+action+r'"[^>]*>.*?name="token"[^>]*value="__CSRF__"', html, re.S)
script = re.search(r'<script>(.*?)</script>', html, re.S)[1].replace('}refresh();', '}')
check = r'''
const assert=require('assert');
const input=JSON.parse(require('fs').readFileSync(0,'utf8'));
const nodes=Object.fromEntries(input.ids.map(id=>[id,{textContent:'',value:'',hidden:false,dataset:{},removeAttribute(k){delete this[k]},addEventListener(){},focus(){},select(){},children:[],replaceChildren(){this.children=[]},append(item){this.children.push(item)}}]));
const button={disabled:true},link={addEventListener(){}};
const document={createElement(){return {textContent:'',children:[],append(item){this.children.push(item)},addEventListener(){}}},getElementById(id){assert(nodes[id],'Unknown ID '+id);return nodes[id]},querySelector(s){return s==='[data-config]'?button:link},execCommand(){return false}};
let status={support_command:'Start-Service sshd',capt:{fresh:true,age_ms:1000},healthy:true,station:{name:'Máy 1',ap_ssid:'ESP32-Printer-May1'},connected:true,ssid:'Wi-Fi',ip:'192.168.1.199',rssi:-50,network:{ip:'192.168.1.199',gateway:'192.168.1.1',mask:'255.255.255.0',dns:'192.168.1.1'},usb:{installed:true,connected:true,responsive:true,error:0,details:'Canon LBP3300'},cloud:{enabled:false,state:'disabled',error:'',total:0,completed:0},tcp:{state:0,error:0,total:0,completed:0},uptime:100,flash:16777216,psram:8388608};
let offline=false;
const refresh=new Function('document','fetch','setTimeout',input.script+';return refresh;')(document,async()=>{if(offline)throw Error();return {ok:true,json:async()=>status}},()=>{});
(async()=>{
 await refresh();assert.equal(nodes['ap-name'].value,'ESP32-Printer-May1');assert.equal(nodes['printer-status'].dataset.tone,'ok');assert.equal(nodes['printer-icon'].textContent,'✓');assert.equal(nodes['support-command'].value,'Start-Service sshd');assert.equal(nodes.support.hidden,false);assert.equal(nodes.printer.textContent,'Máy in đã kết nối');assert.equal(nodes['cloud'].textContent,'Web: Chưa bật');assert.equal(button.disabled,false);
 nodes['station-name'].value='Tên đang sửa';nodes['ap-name'].value='AP đang sửa';status.tcp={state:2,error:0,total:3,completed:1};status.job_control={id:'lan-1',source:'lan',state:'printing',active:true,total:3,completed:1};await refresh();assert.equal(nodes['station-name'].value,'Tên đang sửa');assert.equal(nodes['ap-name'].value,'AP đang sửa');assert.equal(nodes.printer.textContent,'Đang in qua LAN');assert.equal(nodes.progress.value,1);assert.equal(nodes.progress.max,3);
 status.tcp.state=0;status.cloud={enabled:true,state:'printing',total:4,completed:2,progress_known:true};status.job_control={id:'web-1',source:'cloud',state:'printing',active:true,total:4,completed:2};await refresh();assert.equal(nodes.printer.textContent,'Đang in từ web');assert.equal(nodes.progress.value,2);
 status.job_control={id:'lan-old',source:'lan',state:'failed',active:false,total:2,completed:0};status.tcp={state:2,total:0,completed:0};await refresh();const stable=nodes['job-progress'].textContent;assert(stable.includes('0 / 2'));status.tcp={state:0,total:2,completed:0};await refresh();assert.equal(nodes['job-progress'].textContent,stable);assert.equal(nodes.job.hidden,true);assert(nodes['jobs-title'].textContent.includes('Lịch sử'));
 await refresh({job_control:{id:'lan-wait',source:'lan',state:'waiting_printer',active:false,total:4,completed:2},capt:{fresh:true,age_ms:5}});assert.equal(nodes.job.hidden,false);assert(nodes['control-state'].textContent.includes('tự tiếp tục'));assert.equal(nodes['job-delete'].disabled,true);
 status.cloud.state='uncertain';await refresh();assert.equal(nodes.resolve.hidden,false);
 status.capt={fresh:true,service_error:true,service_code:'E100 0000',age_ms:100};await refresh();assert.equal(nodes.printer.textContent,'E100 0000');assert.equal(nodes['printer-status'].dataset.tone,'error');
 status.capt={fresh:true,age_ms:100};status.cloud.wait_reason='previous_session';status.job_control={id:'web-wait',source:'cloud',state:'waiting_printer',active:true,total:8,completed:0};await refresh();assert.equal(nodes.printer.textContent,'Chờ khép phiên in trước');assert(nodes['printer-note'].textContent.includes('giữ nguồn ESP'));assert(!nodes['control-state'].textContent.includes('Chờ xử lý lỗi'));delete status.cloud.wait_reason;
 status.job_control={id:'web-cancel',source:'cloud',state:'cancelled',active:false,total:8,completed:0,printer_cancel:'cleared'};await refresh();assert(nodes['control-state'].textContent.includes('xác nhận bỏ dữ liệu đệm'));assert.equal(nodes['printer-status'].dataset.tone,'ok');
 status.job_control.printer_cancel='unverified';await refresh();assert(nodes['control-state'].textContent.includes('chưa xác minh'));assert.equal(nodes['printer-status'].dataset.tone,'warn');
 status.server_queue={supported:true,age_ms:100,revision:'rev-1',items:[{id:'web-1',title:'<script>test</script>',state:'queued'}]};status.queue_command={id:'cmd-1',result:'pending'};
 await refresh();assert.equal(nodes['server-jobs'].children[0].children[0].textContent,'<script>test</script> · web-1 · queued');assert.equal(nodes['server-jobs'].children[0].children[1].disabled,true);
 await refresh({job_control:{id:'lan-1',state:'paused',source:'lan',active:true,controllable:true,total:3,completed:1},capt:{fresh:true,age_ms:5}});assert.equal(nodes['job-resume'].disabled,false);assert(nodes['control-state'].textContent.includes('Đã dừng'));assert.equal(nodes.wifi.textContent,'Wi-Fi');
 status.capt.fresh=false;await refresh();assert.equal(nodes.printer.textContent,'Đang đọc trạng thái máy in');assert.equal(nodes['printer-status'].dataset.tone,'warn');
 status.usb.connected=false;status.usb.responsive=false;await refresh();assert.equal(nodes.printer.textContent,'Chưa kết nối máy in');
 offline=true;await refresh();assert.equal(nodes.connection.dataset.tone,'error');assert.equal(nodes['printer-status'].dataset.tone,'offline');assert.equal(nodes.job.hidden,true);assert.equal(nodes.resolve.hidden,true);assert.equal(nodes.printer.textContent,'Chưa có trạng thái mới');
 let scanCalls=0;
 const scan=new Function('document','fetch','setTimeout',input.script+';return {scanWifi,chooseWifi};')(document,async(path,options)=>{
  scanCalls++;if(options)return {ok:true,status:202};if(scanCalls===2)return {status:202};
  return {ok:true,status:200,json:async()=>({networks:[{ssid:'__hidden__',rssi:-70,secure:false},{ssid:'<unsafe>',rssi:-40,secure:true},{ssid:'<unsafe>',rssi:-80,secure:true}]})};
 },callback=>callback());
 await scan.scanWifi();assert.equal(scanCalls,3);assert.equal(nodes['wifi-list'].children.length,4);
 assert(nodes['wifi-list'].children[1].textContent.startsWith('<unsafe>'));assert.equal(nodes['wifi-scan'].disabled,false);
 nodes['wifi-list'].value='ssid:__hidden__';scan.chooseWifi();assert.equal(nodes['wifi-ssid'].value,'__hidden__');assert.equal(nodes['wifi-hidden-label'].hidden,true);
 nodes['wifi-list'].value='__hidden__';nodes['wifi-hidden'].value='Mạng ẩn';scan.chooseWifi();assert.equal(nodes['wifi-ssid'].value,'Mạng ẩn');assert.equal(nodes['wifi-hidden-label'].hidden,false);
 const failed=new Function('document','fetch','setTimeout',input.script+';return scanWifi;')(document,async()=>({ok:false,text:async()=>'Trạm đang in'}),callback=>callback());
 await failed();assert.equal(nodes['wifi-scan-note'].textContent,'Trạm đang in');assert.equal(nodes['wifi-scan'].disabled,false);
 console.log('PASS: Wi-Fi scan polling, deduplication, safe labels, hidden SSID and scan failure');
 console.log('PASS: IDs, CSRF forms, LAN/cloud progress, edit preservation, USB disconnect, stale status');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
subprocess.run(['node', '-e', check], input=json.dumps({'ids': ids, 'script': script}), text=True, check=True)
print(f'Homepage: {len(html.encode()):,} bytes before placeholder substitution')
