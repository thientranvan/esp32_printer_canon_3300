#!/usr/bin/env python3
"""Enable LAN-to-cloud fallback: sudo python3 configure_cloud.py station.cloud.json.
Server export contains a client-scoped token. Never use station/ESP tokens.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile


def validate(raw,station=None):
    if len(raw)>4096:raise ValueError('Cloud configuration exceeds 4096 bytes')
    c=json.loads(raw)
    if (not isinstance(c,dict) or c.get('schema')!=1 or
        not re.fullmatch(r'esp32-[0-9a-f]{12}',c.get('station_id','')) or
        not re.fullmatch(r'[A-Za-z0-9_-]{32,256}',c.get('token','')) or
        (station and c['station_id']!=station)):
        raise ValueError('Invalid cloud configuration or station identity')
    return {k:c[k] for k in ('schema','station_id','token')}


def save(raw,folder,station):
    c=validate(raw,station)
    if not folder.is_dir():raise ValueError('Install LAN printer first')
    # Refuse replacing credentials while receipt is unresolved.
    if any((folder/n).exists() for n in ('cloud-job.json','cloud-job.txt')):
        raise ValueError('Unresolved cloud job; inspect server before changing credentials')
    fd,name=tempfile.mkstemp(prefix='.cloud-',dir=folder)
    try:
        with os.fdopen(fd,'w') as f:
            json.dump(c,f);f.flush();os.fsync(f.fileno())
        os.chmod(name,0o600);os.replace(name,folder/'cloud.json')
    finally:
        if os.path.exists(name):os.unlink(name)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('config',type=Path);a=p.parse_args()
    raw=a.config.read_bytes();c=validate(raw)
    if os.name=='nt':folder=Path(os.environ['ProgramData'])/'ESP32Printer/Stations'/c['station_id']
    elif sys.platform=='darwin':folder=Path('/Library/Caches/ESP32Printer')/c['station_id']
    else:folder=Path('/var/cache/cups/esp32printer')/c['station_id']
    save(raw,folder,c['station_id']);print('Cloud fallback configured:',c['station_id'])


if __name__=='__main__':main()
