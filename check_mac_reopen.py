"""Verify Quit then automatic reopen using a held CUPS job; no printer output."""
from pathlib import Path
import json, os, re, subprocess, tempfile, time

queue='ESP32_esp321051db78a8fc'
bundle='com.esp32printer.status.esp32-1051db78a8fc'
app='/Library/Printers/ESP32Printer/Status-esp32-1051db78a8fc.app/Contents/MacOS/applet'
assert os.getuid()!=0, 'Run in logged-in Mac user session'
subprocess.run(['/bin/launchctl','print',f'gui/{os.getuid()}/com.esp32printer.status-watcher'],check=True,stdout=subprocess.DEVNULL)
code="ObjC.import('AppKit');var a=$.NSRunningApplication.runningApplicationsWithBundleIdentifier($("+json.dumps(bundle)+"));for(var i=0;i<Number(a.count);i++)a.objectAtIndex(i).terminate;"
subprocess.run(['/usr/bin/osascript','-l','JavaScript','-e',code],check=True,capture_output=True)
def running():
    return any(line.strip()==app for line in subprocess.check_output(['ps','-axo','command='],text=True).splitlines())
for _ in range(20):
    if not running():break
    time.sleep(.25)
assert not running(), 'Status app did not Quit; test stopped'
with tempfile.TemporaryDirectory(prefix='esp32-held-check-') as tmp:
    document=Path(tmp)/'held.txt';document.write_text('ESP32 status window check. Held job; do not print.\n')
    result=subprocess.check_output(['/usr/bin/lp','-d',queue,'-H','hold','-t','ESP32 held status check',str(document)],text=True)
    match=re.search(re.escape(queue)+r'-[0-9]+',result);assert match,result
    job=match[0]
    try:
        for _ in range(24):
            if running():break
            time.sleep(.25)
        assert running(), 'New held CUPS job did not reopen status app'
    finally:
        subprocess.run(['/usr/bin/cancel',job],check=True)
print('PASS: installed app Quit, real held CUPS job reopened it; only test job cancelled, no printer data sent')
