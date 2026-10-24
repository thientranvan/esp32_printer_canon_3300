#!/usr/bin/python3
"""CUPS PDF -> existing CAPT backend stream. Layout is handled by cups-filters."""
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import isolated, limits, pbm


def packet(op, body=b''):
    return struct.pack('<HH', op, len(body)+4)+body


def options(text):
    result={}
    for word in shlex.split(text):
        key, _, value=word.partition('=');result[key.lower()]=value
    if result.get('pagesize', 'A4')!='A4' or result.get('media', 'A4').split(',')[0] not in ('A4','iso_a4_210x297mm'):
        raise ValueError('Only A4 supported; choose A4 in application')
    duplex=result.get('duplex', 'None')
    if duplex not in ('None','DuplexNoTumble','DuplexTumble'):
        raise ValueError('Unsupported duplex option')
    if result.get('sides','one-sided') not in ('one-sided','two-sided-long-edge','two-sided-short-edge'):
        raise ValueError('Unsupported sides option')
    return duplex=='DuplexTumble' or result.get('sides')=='two-sided-short-edge'


# ponytail: A4/cassette 1, <=42 laid-out sides; extend media parameters and
# streaming only after physical printer validation.
def stream(source, short_edge=False):
    from PIL import Image
    if os.getuid()==0:
        raise ValueError('PDF filter must run unprivileged')
    if not 8<=source.stat().st_size<=20*1024*1024 or not source.read_bytes().startswith(b'%PDF-'):
        raise ValueError('Expected PDF <=20 MiB')
    with tempfile.TemporaryDirectory(prefix='esp-cups-') as temp:
        folder=Path(temp);(folder/'input.pdf').write_bytes(source.read_bytes());(folder/'input.pdf').chmod(0o444)
        info=isolated(['/usr/bin/pdfinfo','input.pdf'],folder).decode()
        count=next(int(line.split(':',1)[1]) for line in info.splitlines() if line.startswith('Pages:'))
        if not 1<=count<=42:raise ValueError('Maximum 42 laid-out sides per job')
        output=bytearray(b'dBMpM`obED|~|000ccp0')
        for page in range(1,count+1):
            # Poppler keeps aspect ratio. Rotate landscape to physical portrait A4,
            # then center/pad without stretching glyphs; Pillow uses native C code.
            isolated(['/usr/bin/pdftoppm','-f',str(page),'-l',str(page),'-singlefile',
                      '-mono','-scale-to','7014','input.pdf','page'],folder)
            with Image.open(folder/'page.pbm') as image:
                if image.width*image.height>36_000_000:raise ValueError('Raster too large')
                if image.width>image.height:image=image.transpose(Image.Transpose.ROTATE_90 if hasattr(Image,'Transpose') else Image.ROTATE_90)
                image.thumbnail((4960,7014))
                canvas=Image.new('1',(4960,7014),1)
                canvas.paste(image,((4960-image.width)//2,(7014-image.height)//2))
                canvas.save(folder/'page.pbm')
            compressed=subprocess.run([str(Path(__file__).resolve().parent/'capt-compress')],
                input=pbm((folder/'page.pbm').read_bytes()),stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                check=True,timeout=120,preexec_fn=limits).stdout
            output+=page_stream(compressed,short_edge)
            output+=packet(3) if page==count else packet(2)
            if len(output)>20*1024*1024:raise ValueError('CAPT exceeds 20 MiB')
        return output


def page_stream(compressed,short_edge):
    if len(compressed)<12:raise ValueError('Truncated compressed data')
    params=bytearray.fromhex('a0d02c000000942a020000000000000000110400010102000000780078005002761a6013661b000001000000')
    if short_edge:params[9]|=8
    output=params+packet(0xD0A4,compressed[:8])+packet(0xD0A1)
    count=struct.unpack_from('<I',compressed,8)[0];pos=12
    if not 1<=count<=4096:raise ValueError('Invalid band count')
    for _ in range(count):
        if pos+4>len(compressed):raise ValueError('Truncated band header')
        n=struct.unpack_from('<I',compressed,pos)[0];pos+=4
        if not n or n>len(compressed)-pos:raise ValueError('Truncated band')
        output+=struct.pack('<HHI',0x8200,0,n+8)+compressed[pos:pos+n];pos+=n
    if pos!=len(compressed):raise ValueError('Trailing compressed data')
    return output+packet(0xC0A4)+packet(0xD0A2)+packet(4,bytes(4))


def main():
    if len(sys.argv) not in (6,7):raise ValueError('Invalid CUPS arguments')
    short=options(sys.argv[5])
    with tempfile.TemporaryDirectory(prefix='esp-input-') as temp:
        source=Path(temp)/'input.pdf'
        incoming=open(sys.argv[6],'rb') if len(sys.argv)==7 else sys.stdin.buffer
        try:data=incoming.read(20*1024*1024+1)
        finally:
            if len(sys.argv)==7:incoming.close()
        if len(data)>20*1024*1024:raise ValueError('PDF exceeds 20 MiB')
        source.write_bytes(data)
        # Own pdftopdf stage always gets one copy. Backend applies copies/collate
        # after duplex pairing, so native CUPS cannot multiply the job twice.
        ppd=Path(__file__).resolve().parent/'printer.ppd'
        laid_out=Path(temp)/'layout.pdf'
        with tempfile.TemporaryDirectory(prefix='esp-layout-') as layout_temp:
            folder=Path(layout_temp)
            (folder/'input.pdf').write_bytes(data)
            (folder/'printer.ppd').write_bytes(ppd.read_bytes())
            laid_out.write_bytes(isolated(['/usr/bin/env','PPD=/work/printer.ppd',
                '/usr/lib/cups/filter/pdftopdf',sys.argv[1],sys.argv[2],sys.argv[3],
                '1',sys.argv[5],'input.pdf'],folder))
        if laid_out.stat().st_size>20*1024*1024:raise ValueError('Laid-out PDF exceeds 20 MiB')
        sys.stdout.buffer.write(stream(laid_out,short))


if __name__=='__main__':
    try:main()
    except (ValueError,OSError,subprocess.SubprocessError,StopIteration) as e:
        print('ERROR: Ubuntu CAPT render failed: '+str(e),file=sys.stderr);sys.exit(1)
