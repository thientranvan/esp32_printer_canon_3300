"""Validate packaged Windows EXE: python3 check_windows_exe.py.
Runtime self-check on Windows/Mono: ESP32PrinterSetup.exe --self-test
"""
from pathlib import Path
import hashlib
import struct

root=Path(__file__).resolve().parent
exe=root/'installers/ESP32PrinterSetup.exe';data=exe.read_bytes()
assert data[:2]==b'MZ'
pe=struct.unpack_from('<I',data,0x3c)[0]
assert data[pe:pe+4]==b'PE\0\0'
assert struct.unpack_from('<H',data,pe+4)[0]==0x8664,'Expected x64'
optional=pe+24
assert struct.unpack_from('<H',data,optional)[0]==0x20b,'Expected PE32+'
assert struct.unpack_from('<H',data,optional+68)[0]==2,'Expected GUI subsystem'
assert b'requireAdministrator' in data,'UAC manifest missing'
assert (root/'installers/esp32-printer-setup-v3.ps1').read_bytes() in data,'Embedded installer stale or missing'
resource_rva=struct.unpack_from('<I',data,optional+112+2*8)[0]
sections=optional+struct.unpack_from('<H',data,pe+20)[0]
resource_offset=None
for i in range(struct.unpack_from('<H',data,pe+6)[0]):
    size,rva,raw_size,raw=struct.unpack_from('<IIII',data,sections+i*40+8)
    if rva<=resource_rva<rva+max(size,raw_size):resource_offset=raw+resource_rva-rva
assert resource_offset is not None
named,numbered=struct.unpack_from('<HH',data,resource_offset+12)
types={struct.unpack_from('<I',data,resource_offset+16+i*8)[0] for i in range(named+numbered)}
assert {3,14,24}<=types,'Icon images/group or UAC manifest resource missing'
assert hashlib.sha256(data).hexdigest()==exe.with_suffix('.exe.sha256').read_text().split()[0]
page=(root/'station/station.ino').read_text()
assert 'main/installers/ESP32PrinterSetup.exe' in page
assert 'Tải bộ cài Windows (.exe)' in page
print('PASS: Windows x64 GUI PE, UAC manifest, exact installer resource, SHA-256 and ESP download link')
