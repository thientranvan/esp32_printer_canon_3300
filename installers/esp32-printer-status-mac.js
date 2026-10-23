// Native macOS status window. Run: osascript -l JavaScript FILE IP QUEUE NAME
function deviceView(s) {
    if (!s) return {title:'Mất kết nối trạm in', detail:'Kiểm tra nguồn ESP và mạng LAN.', error:true};
    if (!s.connected || !s.responsive) return {title:'Máy in chưa kết nối', detail:'Kiểm tra nguồn máy in và dây USB.', error:true};
    if (!s.capt || !s.capt.fresh) return {title:'Chưa có trạng thái mới', detail:'Chưa thể xác nhận tiến trình máy in.', error:true};
    var c=s.capt, errors=[];
    if(c.service_error) errors.push('Canon '+c.service_code+' — cần kiểm tra / bảo dưỡng máy in.');
    if(c.paper_jam) errors.push('Kẹt giấy. Kiểm tra đường giấy.');
    if(c.cover_open) errors.push('Nắp máy in đang mở.');
    if(c.paper_required) errors.push('Kiểm tra giấy, khổ giấy và khay giấy đã chọn.');
    if(c.toner_warning) errors.push('Cảnh báo mực.');
    return {title:errors.length ? (c.service_error ? 'Canon '+c.service_code : 'Máy in cần xử lý') : (s.active?'Đang in':'Sẵn sàng'), detail:errors.join('\n') || (s.active?'Máy in đang xử lý lệnh.':'Máy in đang rảnh.'), error:errors.length>0};
}
function run(argv) {
    ObjC.import('AppKit');ObjC.import('Foundation');
    if(argv.length!==3 || !/^\d{1,3}(\.\d{1,3}){3}$/.test(argv[0]) || argv[0].split('.').some(function(x){return +x>255;}) || !/^[A-Za-z0-9_-]+$/.test(argv[1])) throw Error('Expected valid IPv4, CUPS queue and name');
    var host=argv[0], queue=argv[1], name=argv[2], app=$.NSApplication.sharedApplication;
    app.setActivationPolicy($.NSApplicationActivationPolicyRegular);
    var window=$.NSWindow.alloc.initWithContentRectStyleMaskBackingDefer($.NSMakeRect(0,0,760,540),15,$.NSBackingStoreBuffered,false);
    window.title=$(name+' — Canon LBP3300');window.releasedWhenClosed=false;window.center;
    var content=window.contentView;
    function label(text,x,y,w,h,size,bold) {
        var l=$.NSTextField.alloc.initWithFrame($.NSMakeRect(x,540-y-h,w,h));l.stringValue=$(text);l.editable=false;l.selectable=true;l.bezeled=false;l.drawsBackground=false;
        l.font=bold?$.NSFont.boldSystemFontOfSize(size):$.NSFont.systemFontOfSize(size);content.addSubview(l);return l;
    }
    label(name,24,20,710,32,23,true);label('Canon LBP3300  |  '+host,24,58,710,23,13,false);
    var state=label('Đang kết nối…',24,106,710,36,23,true);
    var message=label('',24,151,710,66,14,false);
    var progressText=label('Chưa có tổng số mặt in được xác nhận',24,226,710,25,13,false);
    var progress=$.NSProgressIndicator.alloc.initWithFrame($.NSMakeRect(24,270,712,12));progress.indeterminate=false;progress.minValue=0;progress.maxValue=100;content.addSubview(progress);
    label('Hàng đợi trên Mac',24,288,710,25,15,true);
    var scroll=$.NSScrollView.alloc.initWithFrame($.NSMakeRect(24,112,712,108));scroll.hasVerticalScroller=true;scroll.borderType=$.NSBezelBorder;
    var jobs=$.NSTextView.alloc.initWithFrame($.NSMakeRect(0,0,690,108));jobs.editable=false;jobs.font=$.NSFont.monospacedSystemFontOfSizeWeight(12,0);scroll.documentView=jobs;content.addSubview(scroll);
    var footer=label('Bộ đếm tính theo mặt in; spool không xác nhận giấy đã ra.',24,438,710,28,11,false);
    var request=null, spool=null, lastJobs=null, lastError='', nextPoll=0, target;
    function show() {window.makeKeyAndOrderFront(null);app.activateIgnoringOtherApps(true);console.log('STATUS: opened '+queue);}
    function task(path,args) {
        var p=$.NSPipe.pipe,t=$.NSTask.alloc.init;t.executableURL=$.NSURL.fileURLWithPath($(path));t.arguments=$(args);t.standardOutput=p;t.standardError=$.NSFileHandle.fileHandleWithNullDevice;t.launch;
        return {task:t,pipe:p};
    }
    function read(t) {return ObjC.unwrap($.NSString.alloc.initWithDataEncoding(t.pipe.fileHandleForReading.readDataToEndOfFile,$.NSUTF8StringEncoding)) || '';}
    function apply(s) {
        var v=deviceView(s);state.stringValue=$(v.title);message.stringValue=$(v.detail);state.textColor=v.error?$.NSColor.systemRedColor:$.NSColor.systemGreenColor;
        var known=s && s.capt && s.capt.fresh && s.active && s.total>0;
        progress.doubleValue=known?Math.max(0,Math.min(100,100*s.completed/s.total)):0;
        progressText.stringValue=$(known?s.completed+' / '+s.total+' mặt in hoàn tất':'Chưa có tổng số mặt in được xác nhận');
        var error=v.error?v.title+v.detail:'';if(error && error!==lastError)show();lastError=error;
        footer.stringValue=$('Cập nhật '+new Date().toLocaleTimeString()+' | Spool không xác nhận giấy đã ra.');
    }
    ObjC.registerSubclass({name:'ESP32PrinterStatusActions',superclass:'NSObject',methods:{
        'tick:':{types:['void',['id']],implementation:function() {
            try {
                if(request && !request.task.running) {var s=null;try{s=JSON.parse(read(request));}catch(e){} request=null;apply(s);}
                if(spool && !spool.task.running) {
                    var output=read(spool), ids=output.split('\n').filter(function(l){return l.indexOf(queue+'-')===0;}).map(function(l){return l.split(/\s+/)[0];});
                    // CUPS retains completed job IDs, so quick jobs still trigger opening.
                    if(lastJobs!==null && ids.some(function(id){return lastJobs.indexOf(id)<0;}))show();lastJobs=ids;
                    spool=null;
                    // History identifies new submissions; the next task shows pending jobs only.
                    var active=task('/usr/bin/lpstat',['-W','not-completed','-o',queue]);activeOnly=active;
                }
                if(activeOnly && !activeOnly.task.running) {var text=read(activeOnly);jobs.string=$(text.trim()?text.trim().split('\n').map(function(line){var parts=line.split(/\s+/);return 'Lệnh #'+parts[0].slice(queue.length+1)+'   |   '+parts[1]+'   |   '+Math.ceil(Number(parts[2])/1024)+' KB';}).join('\n'):'Không có lệnh đang chờ trên Mac.');activeOnly=null;}
                if(Date.now()>=nextPoll && !request && !spool && !activeOnly) {
                    request=task('/usr/bin/curl',['--silent','--fail','--noproxy','*','--connect-timeout','1','--max-time','2','--max-filesize','16384','http://'+host+'/printer/status']);
                    spool=task('/usr/bin/lpstat',['-W','all','-o',queue]);nextPoll=Date.now()+1000;
                }
            }catch(e){footer.stringValue=$('Không đọc được trạng thái: '+e.message);}
        }},
        'queue:':{types:['void',['id']],implementation:function(){ task('/usr/bin/open',['-a','/System/Applications/Utilities/Print Center.app']); }},
        'settings:':{types:['void',['id']],implementation:function(){ $.NSWorkspace.sharedWorkspace.openURL($.NSURL.URLWithString($('x-apple.systempreferences:com.apple.Print-Scan-Settings.extension'))); }},
        'web:':{types:['void',['id']],implementation:function(){ $.NSWorkspace.sharedWorkspace.openURL($.NSURL.URLWithString($('http://'+host))); }},
        'hide:':{types:['void',['id']],implementation:function(){window.orderOut(null);}},
        'applicationShouldHandleReopen:hasVisibleWindows:':{types:['bool',['id','bool']],implementation:function(){show();return true;}}
    }});
    var activeOnly=null;target=$.ESP32PrinterStatusActions.alloc.init;app.delegate=target;
    function button(text,x,width,selector){var b=$.NSButton.alloc.initWithFrame($.NSMakeRect(x,22,width,34));b.title=$(text);b.bezelStyle=$.NSBezelStyleRounded;b.target=target;b.action=selector;content.addSubview(b);}
    button('Hàng đợi Mac',24,158,'queue:');button('Cài đặt máy in',190,158,'settings:');button('Quản lý trạm in',356,170,'web:');button('Ẩn cửa sổ',580,156,'hide:');
    var timer=$.NSTimer.scheduledTimerWithTimeIntervalTargetSelectorUserInfoRepeats(0.25,target,'tick:',null,true);
    app.finishLaunching;show();app.run;
}
