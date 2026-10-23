// Run: node check_mac_status.js
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync('installers/esp32-printer-status-mac.js','utf8');
const context={};vm.createContext(context);vm.runInContext(source,context);
assert(context.deviceView(null).error);
let s={connected:true,responsive:true,capt:{fresh:true,service_error:true,service_code:'E100 0000'}};
assert.equal(context.deviceView(s).title,'Canon E100 0000');
s.capt.service_error=false;assert.equal(context.deviceView(s).title,'Sẵn sàng');
s.active=true;assert.equal(context.deviceView(s).title,'Đang in');
s.capt.paper_required=true;assert(context.deviceView(s).detail.includes('khổ giấy'));
s.capt.fresh=false;assert.equal(context.deviceView(s).title,'Chưa có trạng thái mới');
assert(source.includes("['-W','all','-o',queue]"));
assert(!source.includes('cupsaccept') && !source.includes('cupsenable'));
console.log('PASS: Mac status faults, stale/offline, activity and retained job history');

s.job_control={id:'same-job',state:'failed',active:false,total:2,completed:0};
const stable=context.jobView(s).text;
for(const active of [true,false,true,false]) {s.active=active;s.total=active?0:2;assert.equal(context.jobView(s).text,stable);}
assert(stable.includes('0 / 2') && stable.includes('Lệnh lỗi'));
assert(source.includes("control('pause')") && source.includes("control('resume')") && source.includes("control('cancel')"));
