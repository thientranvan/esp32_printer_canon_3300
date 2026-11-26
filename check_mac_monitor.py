"""Run production Mac monitor against local fakes; no CUPS or printer operations."""
from pathlib import Path
import json, subprocess, sys, tempfile

source=Path('mac_backend.pl').read_text().split('sub main {',1)[0]
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);calls=root/'calls';fixture=root/'status.json'
    state={'schema':1,'connected':True,'responsive':True,'capt':{'fresh':True,'service_error':True,'service_code':'E100 0000'},'job_control':{'id':'own-job','active':True,'total':100,'completed':20}}
    fixture.write_text(json.dumps(state))
    curl=root/'curl'
    curl.write_text('#!'+sys.executable+'\n'+f'''import sys,json
from pathlib import Path
with Path({str(calls)!r}).open('a') as f:f.write(json.dumps(sys.argv[1:])+"\\n")
if sys.argv[-1].endswith('/setup/profile.json'):
 print(json.dumps({{'schema':1,'model':'LBP3300','id':'esp32-1051db78a8fc'}}))
elif sys.argv[-1].endswith('/printer/status'):
 print(Path({str(fixture)!r}).read_text())
else:sys.exit(42)
''');curl.chmod(0o700)
    ipptool=root/'ipptool';ipptool.write_text('#!/bin/sh\nexit 42\n');ipptool.chmod(0o700)
    program=source.replace('/usr/bin/curl',str(curl)).replace('/usr/bin/ipptool',str(ipptool)).replace('/Library/Printers/ESP32Printer',tmp)
    program+='''
monitor('127.0.0.1','ESP32_esp321051db78a8fc',1);
$stationId='esp32-1051db78a8fc';$ARGV[0]=333;$ARGV[4]='job-uuid=urn:uuid:026e4380-17f2-3a97-5d45-39c4074a9071';
my $id=job_identity(['params',['raster']]);print $id; 
die 'Process restart changed job ID' unless $id eq job_identity(['params',['raster']]);
$pending=$ARGV[1];open(my $f,'>',$pending) or die $!;print $f JSON::PP::encode_json({schema=>1,id=>$id});close($f);
die 'Own pending receipt rejected' unless pending_matches($id);
die 'Other pending receipt accepted' if pending_matches('other');
$ARGV[4]='job-uuid=urn:uuid:126e4380-17f2-3a97-5d45-39c4074a9071';
die 'Different CUPS job reused ID' if $id eq job_identity(['params',['raster']]);
'''
    run=root/'check.pl';run.write_text(program)
    runs=[subprocess.run(['/usr/bin/perl',str(run),'unused',str(root/'pending')],check=True,capture_output=True) for _ in range(2)]
    assert runs[0].stdout==runs[1].stdout and runs[0].stdout.startswith(b'mac-')
    got=json.loads((root/'cloud-status-esp32-1051db78a8fc.json').read_text())
    assert got['status']['capt']==state['capt'] and got['status']['job_control']==state['job_control']
    commands=[json.loads(line) for line in calls.read_text().splitlines()]
    assert len(commands)==4 and all('--unix-socket' not in c and '--data-binary' not in c for c in commands)
    # A transient HTTP failure must not tear down the print transfer on its first sample.
    flaky=root/'flaky';attempts=root/'attempts'
    flaky.write_text('#!'+sys.executable+'\n'+f"""import sys
from pathlib import Path
p=Path({str(attempts)!r});n=int(p.read_text()) if p.exists() else 0;p.write_text(str(n+1))
if n<2:sys.exit(22)
print(Path({str(fixture)!r}).read_text())
""");flaky.chmod(0o700)
    snapshot=source.replace('/usr/bin/curl',str(flaky))+"\n$stationHost='127.0.0.1';my $s=control_snapshot('own-job');die 'Lost progress' unless $s->{job_control}{completed}==20;\n"
    run.write_text(snapshot)
    subprocess.run(['/usr/bin/perl',str(run)],check=True,capture_output=True)
    assert attempts.read_text()=='3'
print('PASS: monitor only reads ESP and caches exact faults/progress; no CUPS mutation; stable job identity and receipt isolation')
