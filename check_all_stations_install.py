"""Run remote enrollment and all-station dispatch without root, server or printer."""
from pathlib import Path
import subprocess,tempfile,json,re,os,socket,struct
ROOT=Path(__file__).resolve().parent
ids=['esp32-98c377a136b0','esp32-1051db78a8fc','esp32-1051db786a34']
# Exercise actual shell dispatch, including legacy IP invocation and partial failure.
shell=(ROOT/'install_mac.sh').read_text().split('source_dir=')[0]
shell=re.sub(r'if \[ "\$\(id -u\)" -ne 0 \]; then.*?\nfi\n','',shell,flags=re.S)
call='/bin/sh "$0" --remote "$station_ip" "$station_id" "$station_name"'
with tempfile.TemporaryDirectory() as tmp:
 folder=Path(tmp)
 for fail in [False,True]:
  fake='{ printf "%s\\n" "$station_id"; '+('[ "$station_id" != "'+ids[1]+'" ]' if fail else 'true')+'; }'
  path=folder/'all.sh';path.write_text(shell.replace(call,fake))
  for args in [[],['--all'],['192.168.1.199']]:
   r=subprocess.run(['sh',str(path),*args],capture_output=True,text=True)
   assert r.stdout.splitlines()==ids and r.returncode==int(fail),(r.stdout,r.stderr)
 # Actual Perl enrollment path: no LAN call, stable identity, reject response mismatch.
 source=(ROOT/'mac_backend.pl').read_text().split('sub cloud_config {')[0]
 source=source.replace("'/Library/Caches/ESP32Printer'",repr(str(folder))).replace('die "Enrollment requires root\\n" if $>;','')
 source+='''
sub cloud_config{return;}
my $base=$cache;my $id='esp32-1051db786a34';mkdir "$base/$id",0700;my $saved;my $wrong=0;my $calls=0;
{no warnings 'redefine';*enrollment_request=sub {
 my($url,$b,$lan)=@_;die 'LAN used for remote install' if $lan;
 die 'wrong API' unless $url eq 'https://quanly.lophocbinhminh.com/api/print/v1/client/install/enroll';
 die 'remote ticket required' if exists $b->{ticket};die 'invalid verifier' unless $b->{verifier}=~/\\A[0-9a-f]{64}\\z/;
 die 'identity changed' if $saved&&$saved ne $b->{client_id};$saved=$b->{client_id};$calls++;
 return {schema=>1,station_id=>($wrong?'esp32-000000000001':$id),client_id=>$b->{client_id},token=>('S'x32)};
};}
enroll('192.168.1.201',$id,1);unlink "$cache/cloud.json";$cache=$base;
enroll('192.168.1.201',$id,1);die 'retry identity' unless $calls==2;
unlink "$cache/cloud.json";$cache=$base;$wrong=1;
eval{enroll('192.168.1.201',$id,1)};die 'wrong station accepted' unless $@=~/Invalid cloud enrollment/;
die 'wrong credential saved' if -e "$cache/cloud.json";
'''
 f=folder/'remote.pl';f.write_text(source);subprocess.run(['perl',str(f)],check=True)
 # Production main() with wrong LAN profile: must not open printing TCP socket.
 cache=folder/'cache';(cache/ids[2]).mkdir(parents=True)
 curl=folder/'curl';curl.write_text('#!/usr/bin/env python3\nprint(\''+json.dumps({'schema':1,'id':ids[1],'model':'LBP3300'})+'\')\n');curl.chmod(0o755)
 backend=(ROOT/'mac_backend.pl').read_text().replace("'/Library/Caches/ESP32Printer'",repr(str(cache))).replace("'/usr/bin/curl'",repr(str(curl)))
 main=folder/'backend.pl';main.write_text(backend)
 def packet(op,data=b''):return struct.pack('<HH',op,len(data)+4)+data
 params=bytearray(40);params[2:5]=b'\x94\x2a\x02'
 body=packet(0xd0a0,params)+packet(0xd0a4,bytes(8))+packet(0xd0a1)+struct.pack('<HHI',0x8200,256,11)+b'abc'+packet(0xc0a4)+packet(0xd0a2)+packet(4,bytes(4))
 job=b'dBMpM`obED|~|000ccp0'+body+packet(3)
 server=socket.socket();server.bind(('127.0.0.1',0));server.listen();server.settimeout(.2)
 r=subprocess.run(['perl',str(main),'901','test','Identity guard','1',''],input=job,capture_output=True,env={**os.environ,'DEVICE_URI':f'esp32capt://127.0.0.1:{server.getsockname()[1]}/{ids[2]}'})
 assert r.returncode!=0 and b'cloud fallback not configured' in r.stderr,r.stderr
 try:server.accept();raise AssertionError('CAPT socket opened to wrong LAN station')
 except socket.timeout:pass
 finally:server.close()
ps=(ROOT/'installers/esp32-printer-setup-v3.ps1').read_text()
assert "Join-Path $PSHOME 'powershell.exe'" in ps and 'if ($LASTEXITCODE -ne 0)' in ps
assert "(!$RemoteStationID -and !$SingleStation)" in ps and '/client/install/enroll' in ps
for sid in ids:assert sid in ps
cs=(ROOT/'installers/windows/Setup.cs').read_text()
assert 'Program.RunnerAll()' in cs and 'Chọn trạm trong LAN' not in cs
assert 'VerifyLanStation();\n  TcpClient c=' in ps
print('PASS: all-three dispatch, legacy command, partial failure, remote identity persistence, no LAN registration, wrong-LAN print guard')
