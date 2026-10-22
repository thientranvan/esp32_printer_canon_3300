"""Run unprivileged on Ubuntu after build.sh: python3 check.py"""
from pathlib import Path
import os
import struct
import subprocess
import tempfile
from render import pbm, render, HERE

# Raster starts with whitespace bytes: header parser must not swallow them.
raw = b'\n' * (620*7014)
assert pbm(b'P4\n# comment\n4960 7014\n'+raw) == b'\n'*(592*6774)
try:
    pbm(b'P4\n4960 7014\n'+raw[:-1])
except ValueError:
    pass
else:
    raise AssertionError('Truncated raster accepted')
# Every band is internally decoded and byte-compared; exercise non-text data too.
for image in [bytes(592*6774), os.urandom(592*6774)]:
    result = subprocess.run([str(HERE/'capt-compress')], input=image,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, timeout=120).stdout
    count = struct.unpack_from('<I', result, 8)[0]
    offset = 12
    for _ in range(count):
        size = struct.unpack_from('<I', result, offset)[0]
        assert size > 0
        offset += 4+size
    assert count == 97 and offset == len(result)
with tempfile.TemporaryDirectory() as folder:
    source, output = Path(folder)/'bad.pdf', Path(folder)/'job'
    source.write_bytes(b'NOT A PDF DOCUMENT')
    try:
        render(source, output)
    except ValueError:
        pass
    else:
        raise AssertionError('Non-PDF accepted')
    assert not output.exists()
print('PASS: PBM whitespace/truncation, blank/random band roundtrip, invalid PDF rejection')
