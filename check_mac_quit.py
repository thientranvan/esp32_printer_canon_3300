"""macOS: verify Quit AppleEvent exits status app without rebooting or changing queues."""
from pathlib import Path
import subprocess
import tempfile
import time
import plistlib

source=Path('installers/esp32-printer-status-mac.js').read_text()
assert 'app.run' not in source and 'app.delegate=target' not in source
installer=Path('install_mac.sh').read_text()
agent=installer.split('cat > "$work/window.plist" <<EOF\n',1)[1].split('\nEOF',1)[0]
settings=plistlib.loads(agent.encode())
assert settings['RunAtLoad'] and settings['KeepAlive']=={'SuccessfulExit':False}
assert '-replace OSAAppletStayOpen -bool true' in installer
source=source.replace('function run(argv) {','function startStatus(argv) {')+'\nfunction run(){startStatus(["127.0.0.1","ESP32_QUIT_CHECK","Printer quit check"]);}\n'
with tempfile.TemporaryDirectory(prefix='.esp32-quit-',dir=Path.home()/'Applications') as folder:
    root=Path(folder);script=root/'status.js';script.write_text(source);app=root/'PrinterQuitCheck.app'
    subprocess.run(['/usr/bin/osacompile','-s','-l','JavaScript','-o',str(app),str(script)],check=True)
    info=app/'Contents/Info.plist'
    p=plistlib.loads(info.read_bytes());p['CFBundleIdentifier']='com.esp32printer.quitcheck';info.write_bytes(plistlib.dumps(p))
    subprocess.run(['/usr/bin/codesign','--force','--sign','-',str(app)],check=True,capture_output=True)
    with (root/'output').open('w+') as output:
        process=subprocess.Popen(['/usr/bin/open','-W','-n',str(app),'--stdout',str(root/'output'),'--stderr',str(root/'output')],stdout=subprocess.DEVNULL,stderr=output)
        try:
            time.sleep(2)
            if process.poll() is not None:
                output.seek(0);raise AssertionError('Status app exited before Quit: '+output.read())
            processes=subprocess.check_output(['ps','-axo','pid=,command='],text=True).splitlines()
            pid=next(int(line.strip().split(None,1)[0]) for line in processes if line.strip().split(None,1)[-1]==str(app/'Contents/MacOS/applet'))
            code="ObjC.import('AppKit');if(!$.NSRunningApplication.runningApplicationWithProcessIdentifier("+str(pid)+").terminate)throw Error('Quit rejected');"
            result=subprocess.run(['/usr/bin/osascript','-l','JavaScript','-e',code],capture_output=True,text=True,timeout=8)
            assert result.returncode==0,result.stderr
            assert process.wait(timeout=5)==0,'Quit was not a successful exit'
        except BaseException:
            output.flush();output.seek(0);print("app exit status:",process.poll(),"output:",output.read());raise
        finally:
            if process.poll() is None:process.kill();process.wait()
            import os
            for line in subprocess.check_output(['ps','-axo','pid=,command='],text=True).splitlines():
                fields=line.strip().split(None,1)
                if len(fields)==2 and fields[1]==str(app/'Contents/MacOS/applet'):
                    try:os.kill(int(fields[0]),15)
                    except ProcessLookupError:pass
print('PASS: native status app accepts Quit AppleEvent and exits normally; no shutdown/reboot performed')
