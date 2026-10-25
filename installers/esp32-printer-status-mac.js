// Native macOS status window. Run: osascript -l JavaScript FILE IP QUEUE NAME
function statusView(title,detail,kind) {
    return {title:title,detail:detail,kind:kind,error:kind!=='ready'&&kind!=='busy'};
}
function deviceView(s) {
    if(!s)return statusView('Mất kết nối trạm in','Kiểm tra nguồn ESP và mạng LAN.','offline');
    if(!s.connected)return statusView('Máy in chưa kết nối','Kiểm tra nguồn máy in và dây USB.','offline');
    if(!s.responsive||!s.capt||!s.capt.fresh)return statusView('Chưa có trạng thái mới','Chưa thể xác nhận tiến trình máy in.','warning');
    var c=s.capt,errors=[];
    if(c.service_error)errors.push('Canon '+c.service_code+' — cần kiểm tra / bảo dưỡng máy in.');
    if(c.paper_jam)errors.push('Kẹt giấy. Kiểm tra đường giấy.');
    if(c.cover_open)errors.push('Nắp máy in đang mở.');
    if(c.paper_required)errors.push('Kiểm tra giấy, khổ giấy và khay giấy đã chọn.');
    if(c.toner_warning)errors.push('Cảnh báo mực.');
    if(errors.length)return statusView(c.service_error?'Canon '+c.service_code:'Máy in cần xử lý',errors.join('\n'),c.service_error||c.paper_jam?'error':'warning');
    if(s.job_control&&s.job_control.state==='waiting_printer')return statusView('Đang chờ khôi phục','Bật lại máy in và xử lý lỗi; ESP tự in lại từ tờ chưa hoàn tất.','warning');
    if(s.job_control&&s.job_control.state==='uncertain')return statusView('Lệnh cần đối soát','Chưa rõ kết quả lệnh trước; không tự in lại.','warning');
    if(s.job_control&&s.job_control.state==='paused')return statusView('Đã tạm dừng','Lệnh đang tạm dừng gửi.','warning');
    return statusView(s.active?'Đang in':'Sẵn sàng',s.active?'Máy in đang xử lý lệnh.':'CAPT phản hồi, không có cờ lỗi đã giải mã.',s.active?'busy':'ready');
}
function jobView(s) {
    var j=s&&s.job_control, states={waiting_printer:'Chờ máy in để tự in tiếp',printing:'Đang in',paused:'Đã tạm dừng',failed:'Lệnh lỗi',uncertain:'Chưa rõ kết quả',completed:'Đã hoàn tất',resolved:'Đã đối soát'};
    if(!s)return {text:'Mất kết nối — chưa có tiến trình mới',value:0};
    if(j && j.id && j.state==='uncertain')return {text:'Lệnh cần đối soát — chưa rõ kết quả in'+(j.total>0?' · Đã ghi nhận '+j.completed+' / '+j.total+' mặt in':''),value:j.total>0?Math.max(0,Math.min(100,100*j.completed/j.total)):0};
    if(!j || !j.id || (!j.active&&j.state!=='waiting_printer'))return {text:'Trạm hiện không có lệnh đang chạy',value:0};
    return {text:(states[j.state]||j.state)+' · '+(j.total>0?'Đã ghi nhận '+j.completed+' / '+j.total+' mặt in':'Chưa có tổng số mặt in'),value:j.total>0?Math.max(0,Math.min(100,100*j.completed/j.total)):0};
}
function queueView(printer,jobs) {
    var blocked=jobs.filter(function(j){return j['job-state']===6||String(j['job-state-reasons']||'').indexOf('printer-stopped')>=0;});
    if(Number(printer['printer-state'])!==5&&!blocked.length)return null;
    var message=String((blocked[0]||{})['job-printer-state-message']||printer['printer-state-message']||'Hàng đợi Mac đang tạm dừng.');
    if(/Expected LBP3300 A4/.test(message))message='Tài liệu chọn khổ giấy chưa hỗ trợ. Chọn A4 trong Word/PDF rồi gửi lại; xóa lệnh sai khổ còn chờ.';
    return statusView('Hàng đợi Mac bị dừng',message+'\nLỗi tại Mac; trạng thái này không phải lỗi máy in trên ESP.','warning');
}
function printerAppIcon() {
    var image=$.NSImage.alloc.initWithSize($.NSMakeSize(256,256));image.lockFocus;
    function box(x,y,w,h,r,red,green,blue){$.NSColor.colorWithSRGBRedGreenBlueAlpha(red,green,blue,1).setFill;$.NSBezierPath.bezierPathWithRoundedRectXRadiusYRadius($.NSMakeRect(x,256-y-h,w,h),r,r).fill;}
    box(12,12,232,232,52,0.03,0.42,0.38);
    box(66,54,106,72,6,0.88,0.96,0.96);
    box(43,98,170,88,18,0.68,0.85,0.87);
    box(57,119,142,50,10,0.09,0.22,0.28);
    box(69,147,118,68,6,1,1,1);
    box(87,166,82,7,3,0.58,0.72,0.75);box(87,183,62,7,3,0.58,0.72,0.75);
    $.NSColor.colorWithSRGBRedGreenBlueAlpha(0.26,0.89,0.64,1).setFill;
    $.NSBezierPath.bezierPathWithOvalInRect($.NSMakeRect(182,142,10,10)).fill;
    $.NSColor.whiteColor.setStroke;
    [24,14].forEach(function(radius){var arc=$.NSBezierPath.bezierPath;arc.lineWidth=5;arc.lineCapStyle=$.NSRoundLineCapStyle;arc.appendBezierPathWithArcWithCenterRadiusStartAngleEndAngleClockwise($.NSMakePoint(193,202),radius,30,150,false);arc.stroke;});
    $.NSColor.whiteColor.setFill;$.NSBezierPath.bezierPathWithOvalInRect($.NSMakeRect(190,199,6,6)).fill;
    image.unlockFocus;return image;
}
function run(argv) {
    ObjC.import('AppKit');ObjC.import('Foundation');
    if(argv.length!==3 || !/^\d{1,3}(\.\d{1,3}){3}$/.test(argv[0]) || argv[0].split('.').some(function(x){return +x>255;}) || !/^[A-Za-z0-9_-]+$/.test(argv[1])) throw Error('Expected valid IPv4, CUPS queue and name');
    var host=argv[0], queue=argv[1], name=argv[2], app=$.NSApplication.sharedApplication;
    app.setActivationPolicy($.NSApplicationActivationPolicyRegular);app.applicationIconImage=printerAppIcon();
    var window=$.NSWindow.alloc.initWithContentRectStyleMaskBackingDefer($.NSMakeRect(0,0,760,540),15,$.NSBackingStoreBuffered,false);
    window.title=$(name+' — Canon LBP3300');window.releasedWhenClosed=false;window.center;
    var content=window.contentView;
    function label(text,x,y,w,h,size,bold) {
        var l=$.NSTextField.alloc.initWithFrame($.NSMakeRect(x,540-y-h,w,h));l.stringValue=$(text);l.editable=false;l.selectable=true;l.bezeled=false;l.drawsBackground=false;
        l.font=bold?$.NSFont.boldSystemFontOfSize(size):$.NSFont.systemFontOfSize(size);content.addSubview(l);return l;
    }
    label(name,24,20,710,32,23,true);label('Canon LBP3300  |  '+host,24,58,710,23,13,false);
    var statusIcon=$.NSImageView.alloc.initWithFrame($.NSMakeRect(24,386,56,56));statusIcon.imageScaling=$.NSImageScaleProportionallyUpOrDown;content.addSubview(statusIcon);
    var state=label('Đang kết nối…',94,111,640,30,23,true);
    var message=label('',24,151,710,66,14,false);
    var progressText=label('Chưa có tổng số mặt in được xác nhận',24,226,710,25,13,false);
    var progress=$.NSProgressIndicator.alloc.initWithFrame($.NSMakeRect(24,270,712,12));progress.indeterminate=false;progress.minValue=0;progress.maxValue=100;content.addSubview(progress);
    label('Hàng đợi trên Mac',24,288,710,25,15,true);
    var rows=[], controlTask=null, actionNote='', actionUntil=0;
    var scroll=$.NSScrollView.alloc.initWithFrame($.NSMakeRect(24,142,712,78));scroll.hasVerticalScroller=true;scroll.borderType=$.NSBezelBorder;
    var table=$.NSTableView.alloc.initWithFrame($.NSMakeRect(0,0,710,78));table.allowsMultipleSelection=false;
    [['id','Lệnh',95],['name','Tài liệu',320],['state','Trạng thái',270]].forEach(function(c){var column=$.NSTableColumn.alloc.initWithIdentifier($(c[0]));column.title=$(c[1]);column.width=c[2];table.addTableColumn(column);});
    scroll.documentView=table;content.addSubview(scroll);
    var footer=label('Điều khiển hàng đợi Mac; trang đã gửi vẫn có thể được in.',24,448,710,38,11,false);
    var request=null, spool=null, lastJobs=null, lastError='', nextPoll=0, target, localProblem=null, lastStatus=null;
    function show() {window.makeKeyAndOrderFront(null);app.activateIgnoringOtherApps(true);console.log('STATUS: opened '+queue);}
    function task(path,args,captureError,input) {
        var p=$.NSPipe.pipe,t=$.NSTask.alloc.init;t.executableURL=$.NSURL.fileURLWithPath($(path));t.arguments=$(args);t.standardOutput=p;t.standardError=captureError?p:$.NSFileHandle.fileHandleWithNullDevice;var stdin=input?$.NSPipe.pipe:null;if(stdin)t.standardInput=stdin;t.launch;if(stdin){stdin.fileHandleForWriting.writeData($(input).dataUsingEncoding($.NSUTF8StringEncoding));stdin.fileHandleForWriting.closeFile;}
        return {task:t,pipe:p};
    }
    function read(t) {return ObjC.unwrap($.NSString.alloc.initWithDataEncoding(t.pipe.fileHandleForReading.readDataToEndOfFile,$.NSUTF8StringEncoding)) || '';}
    function apply(s) {
        lastStatus=s;var v=deviceView(s);if(localProblem&&v.kind!=='error'&&v.kind!=='offline')v=localProblem;state.stringValue=$(v.title);message.stringValue=$(v.detail);var color={ready:$.NSColor.systemGreenColor,busy:$.NSColor.systemBlueColor,warning:$.NSColor.colorWithSRGBRedGreenBlueAlpha(0.52,0.33,0,1),error:$.NSColor.systemRedColor,offline:$.NSColor.secondaryLabelColor}[v.kind];state.textColor=color;
        var symbol={ready:'checkmark.circle.fill',busy:'printer.fill',warning:'exclamationmark.triangle.fill',error:'xmark.octagon.fill',offline:'wifi.slash'}[v.kind];statusIcon.image=$.NSImage.imageWithSystemSymbolNameAccessibilityDescription($(symbol),$(v.title));statusIcon.contentTintColor=color;
        var view=jobView(s);progress.doubleValue=view.value;progressText.stringValue=$(localProblem?'Lệnh đang bị chặn tại hàng đợi Mac. Xem nguyên nhân bên trên.':view.text);
        var error=v.error?v.title+v.detail:'';if(s&&s.job_control&&s.job_control.state==='uncertain')error+='uncertain:'+s.job_control.id;if(error && error!==lastError)show();lastError=error;
        if(Date.now()>actionUntil)footer.stringValue=$('Cập nhật '+new Date().toLocaleTimeString()+' | Điều khiển hàng đợi Mac; trang đã gửi vẫn có thể được in.');
    }
    function selectedJob(){var row=Number(table.selectedRow);return row>=0&&row<rows.length?rows[row]:null;}
    function control(action){
        var job=selectedJob();if(!job || controlTask)return;
        if(action==='cancel') {
            var alert=$.NSAlert.alloc.init;alert.messageText=$('Hủy lệnh #'+job.id+'?');
            alert.informativeText=$('Lệnh sẽ bị xóa khỏi hàng đợi Mac. Trang đã gửi vào máy in vẫn có thể ra giấy. Nếu đang gửi dữ liệu, cần kiểm tra kết quả trước khi in lại.');
            alert.addButtonWithTitle($('Hủy lệnh'));alert.addButtonWithTitle($('Quay lại'));if(Number(alert.runModal)!==1000)return;
        }
        var id=queue+'-'+job.id;
        controlTask=action==='cancel'?task('/usr/bin/cancel',[id],true):task('/usr/bin/lp',['-i',id,'-H',action==='pause'?'hold':'resume'],true);
        actionNote=action==='cancel'?'Đã yêu cầu hủy lệnh.':action==='pause'?'Đã yêu cầu giữ lệnh trên Mac.':'Đã yêu cầu tiếp tục; cần khắc phục lỗi máy in nếu hàng đợi bị dừng.';
        footer.stringValue=$('Đang gửi yêu cầu…');actionUntil=Date.now()+10000;
    }
    ObjC.registerSubclass({name:'ESP32PrinterStatusActions',superclass:'NSObject',methods:{
        'numberOfRowsInTableView:':{types:['long',['id']],implementation:function(){return rows.length;}},
        'tableView:objectValueForTableColumn:row:':{types:['id',['id','id','long']],implementation:function(view,column,row){var item=rows[Number(row)];return $(item?String(item[ObjC.unwrap(column.identifier)]||''):'');}},
        'pause:':{types:['void',['id']],implementation:function(){control('pause');}},
        'resume:':{types:['void',['id']],implementation:function(){control('resume');}},
        'cancel:':{types:['void',['id']],implementation:function(){control('cancel');}},
        'tick:':{types:['void',['id']],implementation:function() {
            try {
                var chosen=selectedJob();pauseButton.enabled=!!chosen&&!controlTask&&chosen.code!==4;resumeButton.enabled=!!chosen&&!controlTask&&chosen.code===4;cancelButton.enabled=!!chosen&&!controlTask;
                if(controlTask && !controlTask.task.running){var output=read(controlTask);footer.stringValue=$(Number(controlTask.task.terminationStatus)===0?actionNote:'Không thực hiện được: '+output.trim());actionUntil=Date.now()+10000;controlTask=null;nextPoll=0;}
                if(request && !request.task.running) {var s=null;try{s=JSON.parse(read(request));}catch(e){} request=null;apply(s);}
                if(spool && !spool.task.running) {
                    var output=read(spool), ids=output.split('\n').filter(function(l){return l.indexOf(queue+'-')===0;}).map(function(l){return l.split(/\s+/)[0];});
                    // CUPS retains completed job IDs, so quick jobs still trigger opening.
                    if(lastJobs!==null && ids.some(function(id){return lastJobs.indexOf(id)<0;}))show();lastJobs=ids;
                    spool=null;
                    // History identifies new submissions; the next task shows pending jobs only.
                    var query='{ OPERATION Get-Printer-Attributes GROUP operation-attributes-tag ATTR charset attributes-charset utf-8 ATTR language attributes-natural-language en ATTR uri printer-uri $uri ATTR keyword requested-attributes printer-state,printer-state-message STATUS successful-ok }\n'+
                        '{ OPERATION Get-Jobs GROUP operation-attributes-tag ATTR charset attributes-charset utf-8 ATTR language attributes-natural-language en ATTR uri printer-uri $uri ATTR keyword requested-attributes job-id,job-name,job-state,job-state-reasons,job-printer-state-message STATUS successful-ok }\n';
                    activeOnly=task('/usr/bin/ipptool',['-X','-T','2','ipp://localhost/printers/'+queue,'/dev/stdin'],false,query);
                }
                if(activeOnly && !activeOnly.task.running) {
                    var xml=read(activeOnly);activeOnly=null;
                    var result=ObjC.deepUnwrap($.NSPropertyListSerialization.propertyListWithDataOptionsFormatError($(xml).dataUsingEncoding($.NSUTF8StringEncoding),0,null,null));
                    if(!result || !result.Successful)throw Error('Không đọc được hàng đợi CUPS');
                    var attributes=result.Tests[0].ResponseAttributes.filter(function(a){return a['printer-state']!==undefined;})[0]||{}, jobs=result.Tests[1].ResponseAttributes||[];localProblem=queueView(attributes,jobs);apply(lastStatus);
                    var selected=selectedJob(), selectedId=selected?selected.id:null;
                    var states={3:'Đang chờ',4:'Đang giữ',5:'Đang xử lý',6:'Đã dừng'};
                    rows=jobs.filter(function(j){return !!j['job-id'];}).map(function(j){return {id:j['job-id'],name:j['job-name']||'(Xem tài liệu trong Print Center)',code:j['job-state'],state:(states[j['job-state']]||j['job-state'])+(j['job-state-reasons']?' · '+j['job-state-reasons']:'')};});
                    table.reloadData;
                    var index=rows.map(function(j){return j.id;}).indexOf(selectedId);
                    if(index>=0)table.selectRowIndexesByExtendingSelection($.NSIndexSet.indexSetWithIndex(index),false);
                }
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
    var activeOnly=null;target=$.ESP32PrinterStatusActions.alloc.init;app.delegate=target;table.dataSource=target;
    function button(text,x,width,selector){var b=$.NSButton.alloc.initWithFrame($.NSMakeRect(x,22,width,34));b.title=$(text);b.bezelStyle=$.NSBezelStyleRounded;b.target=target;b.action=selector;content.addSubview(b);return b;}
    function jobButton(title,x,width,selector,symbol){var b=button(title,x,width,selector);b.setFrameOrigin($.NSMakePoint(x,98));b.image=$.NSImage.imageWithSystemSymbolNameAccessibilityDescription($(symbol),$(title));b.imagePosition=$.NSImageLeft;b.enabled=false;return b;}
    var pauseButton=jobButton('Tạm dừng',24,150,'pause:','pause.fill'),resumeButton=jobButton('Tiếp tục',184,150,'resume:','play.fill'),cancelButton=jobButton('Xóa lệnh',344,150,'cancel:','trash');
    button('Hàng đợi Mac' ,24,158,'queue:');button('Cài đặt máy in',190,158,'settings:');button('Quản lý trạm in',356,170,'web:');button('Ẩn cửa sổ',580,156,'hide:');
    var timer=$.NSTimer.scheduledTimerWithTimeIntervalTargetSelectorUserInfoRepeats(0.25,target,'tick:',null,true);
    app.finishLaunching;show();app.run;
}
