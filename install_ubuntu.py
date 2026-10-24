#!/usr/bin/python3
"""Install Ubuntu 22.04/24.04 CUPS printer. sudo python3 install_ubuntu.py IP [NAME]"""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import urllib.request

BASE='https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/'
# Filled with exact source digests; mismatches stop before installing code.
ASSETS = {'ubuntu/filter.py': '5cdbf223b3be3b9cb41258a84f6ff5a67ad5a50d277f296a37e1f6cd3dffd02e', 'ubuntu/esp32-lbp3300.ppd': '518f8341cd55f3228e2f1908a3727bb38a4c2d1497e29fb2d07652538895092d', 'mac_backend.pl': '45a06419a32f3b39bcb116ef735fd427e68f5fda2bfdfc368913abb6bb497661', 'server-renderer/render.py': '7ee565a3bb81ceaedf3e18481dff18b1a544f056be7a56f77d913f9b62403bd9', 'server-renderer/build.sh': 'c3881a5d89ad271ab66217ec2563cd101cefc364c3d2b859db1f380315eb7c36', 'server-renderer/compress.c': 'd67bf57f7c133c73d74767d9a4dc54563a67c3daf1ba8c9f5082e8c30b9f2ecf', 'server-renderer/check.py': '665b0b30830db487f2412f8313c84ef1f13460a707794fca332c555ab19b3eba'}  # ASSET_MANIFEST
TARGET=Path('/usr/lib/cups/filter/esp32printer')


def run(*args, **kwargs):
    return subprocess.run(args,check=True,**kwargs)


def fetch(url,limit):
    with urllib.request.urlopen(url,timeout=30) as response:data=response.read(limit+1)
    if len(data)>limit:raise ValueError('Download too large')
    return data


def profile(host):
    host=str(ipaddress.IPv4Address(host))
    # Local station data is untrusted. Never turn it into shell text or a path.
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open('http://'+host+'/setup/profile.json',timeout=15) as r:data=r.read(4097)
    if len(data)>4096:raise ValueError('Profile too large')
    p=json.loads(data)
    if p.get('schema')!=1 or p.get('model')!='LBP3300' or p.get('ip')!=host or not re.fullmatch(r'esp32-[0-9a-f]{12}',p.get('id','')) or p['id']=='esp32-000000000000':
        raise ValueError('Invalid station identity')
    return host,p


def valid_name(name):
    return isinstance(name,str) and 0<len(name.encode('utf8'))<=192 and not any(ord(c)<32 or ord(c)==127 or c in '\\/";' for c in name)


def linux_backend(source):
    replacements={
        "my $cache = '/Library/Caches/ESP32Printer';":"my $cache = '/var/cache/cups/esp32printer';",
        'my $job="mac-".time.': 'my $job="linux-".time.',
        "client_os=>'macos'":"client_os=>'linux'",
        "sub canon_duplex { return $_[0] =~ /(?:^|\\s)CNDuplex(?:=True)?(?:\\s|$)/i; }":
        "sub canon_duplex { return $_[0] =~ /(?:^|\\s)(?:CNDuplex(?:=True)?|Duplex=Duplex(?:NoTumble|Tumble)|sides=two-sided-(?:long|short)-edge)(?:\\s|$)/i; }",
    }
    for old,new in replacements.items():
        if source.count(old)!=1:raise ValueError('Backend source changed; update installer')
        source=source.replace(old,new)
    return source


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip');parser.add_argument('name',nargs='?');args=parser.parse_args()
    if os.geteuid()!=0:raise ValueError('Run with sudo python3 install_ubuntu.py IP [NAME]')
    release=dict(line.split('=',1) for line in Path('/etc/os-release').read_text().splitlines() if '=' in line)
    if release.get('ID','').strip('"')!='ubuntu' or release.get('VERSION_ID','').strip('"') not in ('22.04','24.04'):
        raise ValueError('Supported: Ubuntu 22.04 / 24.04 desktop, CUPS 2.x')
    host,p=profile(args.ip);name=args.name or p['name']
    if args.name is None and os.isatty(0):name=input('Printer name ['+name+']: ').strip() or name
    if not valid_name(name):raise ValueError('Invalid printer name')
    station=p['id'];queue='ESP32_'+station.replace('-','');uri=f'esp32capt://{host}:9100/{station}'
    cache=Path('/var/cache/cups/esp32printer')/station
    if any(Path('/var/cache/cups/esp32printer').glob('*/pending.txt')):raise ValueError('Previous result uncertain. Inspect pending.txt; no automatic reprint or deletion.')
    if shutil.which('lpstat'):
        jobs=subprocess.run(['lpstat','-W','not-completed','-o'],capture_output=True,text=True)
        if any(line.startswith('ESP32_esp32') for line in jobs.stdout.splitlines()):raise ValueError('Resolve queued jobs before reinstalling; nothing deleted')
        old=subprocess.run(['lpstat','-v',queue],capture_output=True,text=True)
        if old.returncode==0 and not old.stdout.strip().endswith('/'+station):raise ValueError('Queue belongs to another device')
    print('Installing:',name,host,station,flush=True)
    run('apt-get','update')
    run('apt-get','install','-y','cups','cups-client','cups-filters','cups-ipp-utils','python3',
        'poppler-utils','python3-pil','bubblewrap','build-essential','curl','perl','ca-certificates')
    with tempfile.TemporaryDirectory(prefix='esp32-install-') as temp:
        work=Path(temp)
        for relative,digest in ASSETS.items():
            data=fetch(BASE+relative,2*1024*1024)
            if hashlib.sha256(data).hexdigest()!=digest:raise ValueError('SHA-256 mismatch: '+relative)
            dest=work/relative;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        backend=work/'ubuntu/backend.pl';backend.write_text(linux_backend((work/'mac_backend.pl').read_text()))
        run('perl',str(backend),'--self-test')
        # Build as nobody; downloaded upstream archive is pinned and SHA-256 checked.
        run('chown','-R','nobody:nogroup',str(work));work.chmod(0o755)
        run('runuser','-u','nobody','--','sh',str(work/'server-renderer/build.sh'))
        renderer=work/'server-renderer'
        shutil.copyfile(work/'ubuntu/filter.py',renderer/'filter.py')
        run('runuser','-u','nobody','--','python3',str(renderer/'check.py'))
        TARGET.mkdir(parents=True,exist_ok=True)
        for file in renderer.iterdir():
            if file.name=='__pycache__':continue
            dest=TARGET/file.name
            if file.is_dir():
                shutil.copytree(file,dest,dirs_exist_ok=True)
            else:shutil.copyfile(file,dest)
        # The renderer/source tree must not remain writable by build user.
        run('chown','-R','root:root',str(TARGET));run('chmod','-R','go-w',str(TARGET))
        (TARGET/'capt-compress').chmod(0o755);(TARGET/'filter.py').chmod(0o755)
        shutil.copyfile(backend,TARGET/'backend.pl');(TARGET/'backend.pl').chmod(0o644)
        shutil.copyfile(work/'ubuntu/esp32-lbp3300.ppd',TARGET/'printer.ppd');(TARGET/'printer.ppd').chmod(0o644)
        wrapper=Path('/usr/lib/cups/backend/esp32capt')
        wrapper.write_text('#!/bin/sh\nexec /usr/bin/perl /usr/lib/cups/filter/esp32printer/backend.pl "$@"\n');wrapper.chmod(0o700)
        pdf=Path('/usr/lib/cups/filter/esp32pdf')
        pdf.write_text('#!/bin/sh\nexec /usr/bin/python3 /usr/lib/cups/filter/esp32printer/filter.py "$@"\n');pdf.chmod(0o755)
        cache.mkdir(parents=True,exist_ok=True);cache.chmod(0o700)
        run('cupstestppd','-q',str(TARGET/'printer.ppd'))
        run('systemctl','enable','--now','cups')
        run('lpadmin','-p',queue,'-v',uri,'-P',str(TARGET/'printer.ppd'),'-D',name,
            '-L','ESP32 '+host,'-o','printer-is-shared=false','-o','printer-error-policy=stop-printer')
        run('cupsaccept',queue);run('cupsenable',queue)
    print('Installed:',name,'('+host+')')
    print('Select A4 / Cassette 1. Duplex, layout and orientation: application print dialog.')
    print('Live ESP status: http://'+host+' ; local queue: lpstat -p '+queue+' -l')
    print('If interrupted after submission: inspect printer/ESP before resuming. No automatic resend.')


if __name__=='__main__':
    try:main()
    except (ValueError,OSError,subprocess.SubprocessError,KeyError) as error:
        raise SystemExit('Installation stopped: '+str(error))
