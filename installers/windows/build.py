"""Build .NET Framework x64 EXE with Mono mcs or Windows Framework csc.

python3 installers/windows/build.py
MCS=/path/mcs.exe MONO=/path/mono python3 installers/windows/build.py
"""
from pathlib import Path
import hashlib
import math
import os
import shutil
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent


def icon():
    images=[]
    for size in (32,48,128):
        pixels=bytearray()
        for y in reversed(range(size)):
            for x in range(size):
                u=x*32/size;v=y*32/size;color=(0,0,0,0)
                if 5<=u<27 and 13<=v<25:color=(29,78,216,255)
                if 9<=u<23 and 7<=v<15:color=(241,245,249,255)
                if 9<=u<23 and 21<=v<30:color=(241,245,249,255)
                if 11<=u<21 and (23<=v<24 or 26<=v<27):color=(71,85,105,255)
                if math.hypot(u-24,v-17)<1.2:color=(74,222,128,255)
                # Wi-Fi arcs above printer, centered over right corner.
                distance=math.hypot(u-25,v-10)
                if v<=7 and u>=17 and (abs(distance-5)<0.7 or abs(distance-9)<0.7):color=(37,99,235,255)
                r,g,b,a=color;pixels+=bytes((b,g,r,a))
        mask=bytes(((size+31)//32)*4*size)
        images.append(struct.pack('<IiiHHIIiiII',40,size,size*2,1,32,0,len(pixels),0,0,0,0)+pixels+mask)
    result=bytearray(struct.pack('<HHH',0,1,len(images)));offset=6+16*len(images)
    for size,data in zip((32,48,128),images):
        result+=struct.pack('<BBBBHHII',size,size,0,0,1,32,len(data),offset);offset+=len(data)
    return result+b''.join(images)


def resources(ico,manifest):
    # Native .res records: manifest + icon group and its DIB images.
    def record(kind,number,body):
        header=struct.pack('<IIHHHHIHHII',len(body),32,0xffff,kind,0xffff,number,0,0x1030,0,0,0)
        return header+body+bytes((-len(body))%4)
    count=struct.unpack_from('<H',ico,4)[0]
    result=record(0,0,b'');group=bytearray(ico[:6])
    for index in range(count):
        entry=ico[6+index*16:22+index*16]
        size,offset=struct.unpack_from('<II',entry,8)
        result+=record(3,index+1,ico[offset:offset+size])
        group+=entry[:12]+struct.pack('<H',index+1)
    return result+record(14,1,group)+record(24,1,manifest)


if __name__=='__main__':
    compiler=os.environ.get('MCS') or shutil.which('mcs') or os.environ.get('CSC')
    if not compiler:raise SystemExit('Set MCS or CSC to Mono mcs / .NET Framework csc compiler')
    output=ROOT/'installers/ESP32PrinterSetup.exe'
    with tempfile.TemporaryDirectory(prefix='esp32-exe-') as tmp:
        res=Path(tmp)/'printer.res';res.write_bytes(resources(icon(),(HERE/'setup.manifest').read_bytes()))
        command=([os.environ['MONO'],compiler] if compiler.endswith('.exe') and os.environ.get('MONO') else [compiler])
        command += ['-nologo','-target:winexe','-platform:x64','-optimize+',
            '-reference:System.Windows.Forms.dll','-reference:System.Drawing.dll',
            '-reference:System.Runtime.Serialization.dll',
            '-win32res:'+str(res),
            '-resource:'+str(ROOT/'installers/esp32-printer-setup-v3.ps1')+',setup.ps1',
            '-out:'+str(output),str(HERE/'Setup.cs')]
        subprocess.run(command,check=True)
    digest=hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix('.exe.sha256').write_text(digest+'  '+output.name+'\n')
    print(output.name,digest)
