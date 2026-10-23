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
