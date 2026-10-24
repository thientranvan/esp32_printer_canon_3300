"""python3 check_ubuntu.py; add --render on Ubuntu with Poppler/Pillow/bwrap/compressor."""
import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'server-renderer'))
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
installer=load('installer',ROOT/'install_ubuntu.py');renderer=load('cups_renderer',ROOT/'ubuntu/filter.py')
for name,digest in installer.ASSETS.items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,name
assert installer.valid_name('Máy 2') and not installer.valid_name('bad\nname') and not installer.valid_name('../printer')
assert renderer.options('PageSize=A4 Duplex=DuplexTumble')
assert not renderer.options('PageSize=A4 Duplex=DuplexNoTumble')
assert "'1',sys.argv[5],'input.pdf'" in (ROOT/'ubuntu/filter.py').read_text()
assert 'application/pdf 0 esp32pdf' in (ROOT/'ubuntu/esp32-lbp3300.ppd').read_text()
for opts in ('PageSize=Letter','Duplex=bad','sides=bad'):
 try:renderer.options(opts)
 except ValueError:pass
 else:raise AssertionError(opts)
backend=installer.linux_backend((ROOT/'mac_backend.pl').read_text())
assert "client_os=>'linux'" in backend and "'/var/cache/cups/esp32printer'" in backend
compressed=bytes(8)+struct.pack('<II',1,3)+b'abc'
page=renderer.page_stream(compressed,True)
assert page[9]&8
for bad in (b'',compressed[:-1],compressed+b'x'):
 try:renderer.page_stream(bad,False)
 except ValueError:pass
 else:raise AssertionError('Invalid band accepted')
with tempfile.TemporaryDirectory() as tmp:
 folder=Path(tmp);b=folder/'backend.pl';b.write_text(backend)
 subprocess.run(['perl',str(b),'--self-test'],check=True)
 # Feed production parser exactly the bytes emitted by Ubuntu filter.
 start=backend.index('sub pages {');end=backend.index('# CUPS serializes',start)
 harness='use strict; use warnings; sub fail { die $_[0]; } sub packet {return pack("vv",$_[0],length($_[1]//"")+4).($_[1]//"");}\n'+backend[start:end]+'''\nlocal $/;my $job=<STDIN>;my @p=pages($job);die 'count' unless @p==1;'''
 subprocess.run(['perl','-e',harness],input=b'dBMpM`obED|~|000ccp0'+page+renderer.packet(3),check=True)
 if '--render' in sys.argv:
  # Real Ubuntu GS/Poppler/sandbox/compression path. No printer/network access.
  pdf=folder/'sample.pdf'
  # Small valid PDF, one text page, generated without external dependencies.
  content=b'BT /F1 24 Tf 72 720 Td (ESP32 UBUNTU RENDER TEST) Tj ET'
  objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
           b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595.276 841.89] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>',
           b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',b'<< /Length '+str(len(content)).encode()+b' >>\nstream\n'+content+b'\nendstream']
  data=bytearray(b'%PDF-1.4\n');offsets=[0]
  for i,obj in enumerate(objects,1):offsets.append(len(data));data+=str(i).encode()+b' 0 obj\n'+obj+b'\nendobj\n'
  xref=len(data);data+=b'xref\n0 6\n0000000000 65535 f \n'
  for offset in offsets[1:]:data+=f'{offset:010} 00000 n \n'.encode()
  data+=f'trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode();pdf.write_bytes(data)
  if os.environ.get('ESP32_PDFTOPDF'):
   pair=folder/'pair.pdf';laid_out=folder/'layout.pdf'
   subprocess.run(['pdfunite',str(pdf),str(pdf),str(pair)],check=True)
   with laid_out.open('wb') as output:
    subprocess.run([os.environ['ESP32_PDFTOPDF'],'1','nobody','CUPS layout test','1',
                    'PageSize=A4 number-up=2 orientation-requested=4 Duplex=DuplexTumble',str(pair)],
                   env={**os.environ,'PPD':str(ROOT/'ubuntu/esp32-lbp3300.ppd')},stdout=output,check=True)
   pdf=laid_out
  spool=renderer.stream(pdf)
  subprocess.run(['perl','-e',harness],input=spool,check=True)
  if Path('/usr/bin/cupstestppd').exists():subprocess.run(['cupstestppd','-q',str(ROOT/'ubuntu/esp32-lbp3300.ppd')],check=True)
print('PASS: Ubuntu manifest, options, input rejection, Linux backend and CAPT stream parser')
