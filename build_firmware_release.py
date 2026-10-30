"""Build public OTA image without device secrets; produce firmware/latest.json + station.bin."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parent
version=int(re.search(r'^#define ESP32_FIRMWARE_VERSION (\d+)$',(ROOT/'station/firmware_update.h').read_text(),re.M)[1])
env=os.environ.copy()
python_envs=sorted((Path.home()/'.espressif/python_env').glob('idf*_env/bin'))
if python_envs:env['PATH']=str(python_envs[-1])+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='esp32-public-ota-') as build:
 subprocess.run([shutil.which('arduino-cli') or '/opt/homebrew/bin/arduino-cli','compile',
  '--fqbn','esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB',
  '--build-property','tools.ctags.cmd.path=/usr/bin/true',
  '--build-property','compiler.cpp.extra_flags=-DESP32_PUBLIC_OTA_BUILD',
  '--build-property','compiler.c.elf.extra_flags=-Wl,--wrap=enum_install,--wrap=enum_process',
  '--build-path',build,str(ROOT/'station')],check=True,env=env)
 image=Path(build,'station.ino.bin').read_bytes()
 assert 288<=len(image)<=3145728 and image[0]==0xe9,'Invalid/oversized ESP image'
 secret=ROOT/'station/device_secret.h'
 if secret.exists():
  value=re.search(r'^#define\s+DEVICE_PASSWORD\s+("(?:[^"\\]|\\.)*")',secret.read_text(),re.M)
  if value:
   password=json.loads(value[1]).encode()
   assert len(password)>=8 and password not in image,'Refusing to publish firmware containing private password'
 target=ROOT/'firmware';target.mkdir(exist_ok=True)
 # One Git commit must publish both files. Hash mismatch across CDN caches fails closed.
 (target/'station.bin').write_bytes(image)
 (target/'latest.json').write_text(json.dumps({'schema':1,'version':version,'board':'esp32-s3-n16r8','bytes':len(image),'sha256':hashlib.sha256(image).hexdigest()},indent=2)+'\n')
 print(f'Public OTA {version}: {len(image)} bytes; no private build artifacts copied.')
