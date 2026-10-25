"""Exercise production Perl submission across printer disconnect: python3 check_lan_recovery.py."""
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading


def packet(op, data=b''):
    return struct.pack('<HH', op, len(data)+4)+data


def read(s, n):
    data=b''
    while len(data)<n:
        part=s.recv(n-len(data))
        if not part:raise EOFError()
        data+=part
    return data


source=Path('mac_backend.pl').read_text().split('# Native CUPS monitor:',1)[0]
listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(15)
port=listener.getsockname()[1]
errors=[];images=[];ids=[]

def printer():
    try:
        for attempt in range(2):
            s,_=listener.accept();s.settimeout(10);decoded=0;faults=3
            with s:
                while True:
                    op,n=struct.unpack('<HH',read(s,4));data=read(s,n-4)
                    reply=b'\0\1'
                    if op==0xF002:ids.append(data)
                    if op==0xF005:
                        assert data==bytes([4,2,4,2]);reply+=struct.pack('<I',0 if attempt==0 else 2)
                    if op==0xF001:assert struct.unpack('<I',data)[0]==4
                    if op==0xA1A1:reply=b'\0\0\x94\x2a'
                    if op==0xA2A0:reply=b'\0\1\x07\0'
                    if op==0xA0A8:
                        reply=bytearray(36);struct.pack_into('<HHHH',reply,14,decoded,decoded,decoded//2*2,decoded//2*2)
                        if attempt==0 and decoded==2 and faults:
                            if faults==1:struct.pack_into('<H',reply,8,0x4000)
                            else:struct.pack_into('<H',reply,12,0x100 if faults==3 else 0x4000)
                            faults-=1
                    if op==0xC0A0:
                        images.append(data[0])
                        if attempt==0 and len(images)==3:break  # USB loss during second sheet.
                    if op==0xC0A4:decoded+=1
                    if op>>8 in (0xC0,0xD0):continue
                    s.sendall(packet(op,reply))
                    if op==0xE0A9:break
        assert ids[0]==ids[1]
        assert images==[2,1,4,4,3],images  # Reprint both sides of interrupted sheet only.
    except BaseException as e:errors.append(e)
    finally:listener.close()

with tempfile.TemporaryDirectory() as tmp:
    worker=threading.Thread(target=printer,daemon=True);worker.start()
    harness=source+'''\nno warnings 'redefine'; *check_control=sub {};*control_snapshot=sub {return {job_control=>{request=>''}};};\n'''+f'''
$stationHost='127.0.0.1';$stationPort={port};$recoverySupported=1;$metadataSupported=1;
$cache=$ARGV[0];$pending="$cache/pending.txt";$ARGV[2]='Test';
$socket=IO::Socket::INET->new(PeerAddr=>$stationHost,PeerPort=>$stationPort,Proto=>'tcp',Timeout=>5);
my @pages;for my $i (1..4){{my $p="\\0" x 64;push @pages,[$p,[chr($i)]];}}
my @plan=plan_pages(\\@pages,1,1,1);print_pages(@plan);
die 'Pending marker retained after completion' if -e $pending;
'''
    result=subprocess.run(['/usr/bin/perl','-e',harness,tmp],capture_output=True,text=True,timeout=20)
    worker.join(timeout=2)
    assert not worker.is_alive(),'Fake printer did not finish'
    if errors:raise errors[0]
    assert result.returncode==0,result.stderr
print('PASS: production Mac/Linux backend, jam/cover/paper clear in same session, reconnect with same ID, skip completed sheet, reprint duplex pair, clear marker')
