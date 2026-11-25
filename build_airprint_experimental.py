"""Build opt-in AirPrint image. Never change stable firmware/latest.json or flash a board."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,tempfile,argparse
ROOT=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--version',type=int,default=2026112501)
VERSION=parser.parse_args().version
if not 1<=VERSION<=0xffffffff:parser.error('version must be 1..4294967295')
folder=ROOT/'firmware/experimental';folder.mkdir(exist_ok=True)
env=os.environ.copy()
python_envs=sorted((Path.home()/'.espressif/python_env').glob('idf*_env/bin'))
if python_envs:env['PATH']=str(python_envs[-1])+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='esp-airprint-build-') as work:
 subprocess.run([shutil.which('arduino-cli') or '/opt/homebrew/bin/arduino-cli','compile',
  '--fqbn','esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB',
  '--build-property','tools.ctags.cmd.path=/usr/bin/true',
  '--build-property',f'compiler.cpp.extra_flags=-DESP32_PUBLIC_OTA_BUILD -DESP32_AIRPRINT_EXPERIMENTAL -DESP32_FIRMWARE_VERSION={VERSION}',
  '--build-property','compiler.c.elf.extra_flags=-Wl,--wrap=enum_install,--wrap=enum_process','--build-path',work,str(ROOT/'station')],check=True,env=env)
 data=Path(work,'station.ino.bin').read_bytes();assert 288<=len(data)<=3145728 and data[0]==0xe9
 secret=ROOT/'station/device_secret.h'
 if secret.exists():
  value=re.search(r'^#define\s+DEVICE_PASSWORD\s+("(?:[^"\\]|\\.)*")',secret.read_text(),re.M)
  if value:assert json.loads(value[1]).encode() not in data,'Private credential found; refusing artifact'
 name=f'airprint-{VERSION}.bin';(folder/name).write_bytes(data)
 (folder/f'airprint-{VERSION}.json').write_text(json.dumps({'schema':1,'channel':'experimental-airprint','version':VERSION,'board':'esp32-s3-n16r8','file':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'hardware_validated':False},indent=2)+'\n')
 print(f'Experimental AirPrint {VERSION}: {len(data)} bytes; stable OTA untouched; hardware validation pending.')
