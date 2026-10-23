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
for field in ['support-start','support-stop','support-mac']:
    assert f"copyField('{field}')" in html
assert 'Enable-NetFirewallRule -Name ESP32PrinterSupport' in html
assert 'Disable-NetFirewallRule -Name ESP32PrinterSupport' in html
assert 'Remove-NetFirewallRule' not in html, 'Stop must preserve rule for restart'
assert 'ESP32CaptBridge' not in html, 'Support commands must not stop printing'

for action in ['/station', '/wifi', '/usb/start', '/cloud/resolve']:
    assert re.search(r'<form[^>]*action="'+action+r'"[^>]*>.*?name="token"[^>]*value="__CSRF__"', html, re.S)
script = re.search(r'<script>(.*?)</script>', html, re.S)[1].replace('}refresh();', '}')
check = r'''
const assert=require('assert');
const input=JSON.parse(require('fs').readFileSync(0,'utf8'));
const nodes=Object.fromEntries(input.ids.map(id=>[id,{textContent:'',value:'',hidden:false,dataset:{},removeAttribute(k){delete this[k]},addEventListener(){},focus(){},select(){},children:[],replaceChildren(){this.children=[]},append(item){this.children.push(item)}}]));
const button={disabled:true},link={addEventListener(){}};
const document={createElement(){return {textContent:'',children:[],append(item){this.children.push(item)},addEventListener(){}}},getElementById(id){assert(nodes[id],'Unknown ID '+id);return nodes[id]},querySelector(s){return s==='[data-config]'?button:link},execCommand(){return false}};
let status={support_command:'Start-Service sshd',capt:{fresh:true,age_ms:1000},healthy:true,station:{name:'Máy 1'},connected:true,ssid:'Wi-Fi',ip:'192.168.1.199',rssi:-50,network:{ip:'192.168.1.199',gateway:'192.168.1.1',mask:'255.255.255.0',dns:'192.168.1.1'},usb:{installed:true,connected:true,responsive:true,error:0,details:'Canon LBP3300'},cloud:{enabled:false,state:'disabled',error:'',total:0,completed:0},tcp:{state:0,error:0,total:0,completed:0},uptime:100,flash:16777216,psram:8388608};
let offline=false;
const refresh=new Function('document','fetch','setTimeout',input.script+';return refresh;')(document,async()=>{if(offline)throw Error();return {ok:true,json:async()=>status}},()=>{});
(async()=>{
 await refresh();assert.equal(nodes['support-command'].value,'Start-Service sshd');assert.equal(nodes.support.hidden,false);assert.equal(nodes.printer.textContent,'Máy in đã kết nối');assert.equal(nodes['cloud'].textContent,'Web: Chưa bật');assert.equal(button.disabled,false);
 nodes['station-name'].value='Tên đang sửa';status.tcp={state:2,error:0,total:3,completed:1};await refresh();assert.equal(nodes['station-name'].value,'Tên đang sửa');assert.equal(nodes.printer.textContent,'Đang in qua LAN');assert.equal(nodes.progress.value,1);assert.equal(nodes.progress.max,3);
 status.tcp.state=0;status.cloud={enabled:true,state:'printing',total:4,completed:2,progress_known:true};await refresh();assert.equal(nodes.printer.textContent,'Đang in từ web');assert.equal(nodes.progress.value,2);
 status.cloud.state='uncertain';await refresh();assert.equal(nodes.resolve.hidden,false);
 status.capt={fresh:true,service_error:true,service_code:'E100 0000',age_ms:100};await refresh();assert.equal(nodes.printer.textContent,'E100 0000');
 status.server_queue={supported:true,age_ms:100,revision:'rev-1',items:[{id:'web-1',title:'<script>test</script>',state:'queued'}]};status.queue_command={id:'cmd-1',result:'pending'};
 await refresh();assert.equal(nodes['server-jobs'].children[0].children[0].textContent,'<script>test</script> · web-1 · queued');assert.equal(nodes['server-jobs'].children[0].children[1].disabled,true);
 await refresh({job_control:{id:'lan-1',state:'paused',source:'lan',active:true,controllable:true,total:3,completed:1},capt:{fresh:true,age_ms:5}});assert.equal(nodes['job-resume'].disabled,false);assert(nodes['control-state'].textContent.includes('Đã dừng'));assert.equal(nodes.wifi.textContent,'Wi-Fi');
 status.capt.fresh=false;await refresh();assert.equal(nodes.printer.textContent,'Đang đọc trạng thái máy in');
 status.usb.connected=false;status.usb.responsive=false;await refresh();assert.equal(nodes.printer.textContent,'Chưa kết nối máy in');
 offline=true;await refresh();assert.equal(nodes.connection.dataset.tone,'error');assert.equal(nodes.job.hidden,true);assert.equal(nodes.resolve.hidden,true);assert.equal(nodes.printer.textContent,'Chưa có trạng thái mới');
 console.log('PASS: IDs, CSRF forms, LAN/cloud progress, edit preservation, USB disconnect, stale status');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
subprocess.run(['node', '-e', check], input=json.dumps({'ids': ids, 'script': script}), text=True, check=True)
print(f'Homepage: {len(html.encode()):,} bytes before placeholder substitution')
