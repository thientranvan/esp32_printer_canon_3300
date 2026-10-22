#!/usr/bin/env python3
"""Experimental Ubuntu PDF -> LBP3300 espjob-v1. No network or printer access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIMIT = 4 * 1024 * 1024


def limits():
    for kind, value in [(resource.RLIMIT_CPU, 120), (resource.RLIMIT_AS, 768*1024*1024),
                        (resource.RLIMIT_FSIZE, 16*1024*1024), (resource.RLIMIT_NOFILE, 64)]:
        resource.setrlimit(kind, (value, value))


def isolated(args, folder):
    command = ['bwrap', '--unshare-all', '--die-with-parent', '--new-session',
               '--ro-bind', '/usr', '/usr', '--ro-bind', '/lib', '/lib',
               '--ro-bind', '/lib64', '/lib64', '--proc', '/proc', '--dev', '/dev',
               '--tmpfs', '/tmp', '--bind', str(folder), '/work', '--chdir', '/work',
               '--clearenv', '--setenv', 'PATH', '/usr/bin:/bin', '--setenv', 'LANG', 'C']
    for path in ['/etc/fonts', '/etc/ld.so.cache']:
        if Path(path).exists():
            command += ['--ro-bind', path, path]
    command += ['--uid', str(os.getuid()), '--gid', str(os.getgid()), '--cap-drop', 'ALL', *args]
    return subprocess.run(command, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=150, preexec_fn=limits).stdout


def pbm(data):
    # Poppler P4: parse header without eating whitespace-valued raster bytes.
    pos, tokens = 0, []
    while len(tokens) < 3:
        while data[pos:pos+1] in (b' ', b'\n', b'\r', b'\t'):
            pos += 1
        if data[pos:pos+1] == b'#':
            pos = data.index(b'\n', pos) + 1
            continue
        end = pos
        while end < len(data) and data[end:end+1] not in (b' ', b'\n', b'\r', b'\t'):
            end += 1
        if end == pos:
            raise ValueError('Invalid PBM header')
        tokens.append(data[pos:end]); pos = end
    if tokens != [b'P4', b'4960', b'7014'] or data[pos:pos+1] not in (b' ', b'\n', b'\r', b'\t'):
        raise ValueError('Expected A4 4960x7014 PBM')
    pos += 1
    raster = data[pos:]
    if len(raster) != 620*7014:
        raise ValueError('Truncated PBM')
    return b''.join(raster[y*620+15:y*620+15+592] for y in range(120, 120+6774))


def render(source, destination, copies=1):
    if not 1 <= copies <= 99:
        raise ValueError('Copies must be 1..99')
    if os.getuid() == 0:
        raise ValueError('Run renderer as unprivileged worker, never root')
    if not 8 <= source.stat().st_size <= 20*1024*1024:
        raise ValueError('PDF must be <=20 MiB')
    with source.open('rb') as pdf:
        data = pdf.read(20*1024*1024+1)
    if len(data) > 20*1024*1024:
        raise ValueError('PDF exceeds 20 MiB')
    if not data.startswith(b'%PDF-'):
        raise ValueError('PDF input required')
    with tempfile.TemporaryDirectory(prefix='esp-render-') as temp:
        folder = Path(temp)
        (folder/'input.pdf').write_bytes(data)
        (folder/'input.pdf').chmod(0o444)
        info = isolated(['/usr/bin/pdfinfo', 'input.pdf'], folder).decode()
        pages = next(int(line.split(':', 1)[1]) for line in info.splitlines() if line.startswith('Pages:'))
        # ponytail: A4 portrait/simplex, <=42 pages (4096 bands); supply pre-layout PDF.
        # Add tested layout/duplex transforms before exposing those options on web.
        if not 1 <= pages <= 42:
            raise ValueError('Supported: 1..42 pages')
        result = bytearray(struct.pack('<8sHHBBH', b'ESPJOB1\n', pages, copies, 1, 1, 0))
        parameters = bytes.fromhex('a0d02c000000942a020000000000000000110400010102000000780078005002761a6013661b000001000000')
        for page in range(1, pages+1):
            isolated(['/usr/bin/pdftoppm', '-f', str(page), '-l', str(page), '-singlefile',
                      '-mono', '-scale-to-x', '4960', '-scale-to-y', '7014',
                      'input.pdf', 'page'], folder)
            raw = pbm((folder/'page.pbm').read_bytes())
            compressed = subprocess.run([str(HERE/'capt-compress')], input=raw,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
                timeout=120, preexec_fn=limits).stdout
            if len(compressed) < 12:
                raise ValueError('Compressor output truncated')
            result += parameters + struct.pack('<HH', 0xD0A4, 12) + compressed[:8]
            result += struct.pack('<HHHH', 0xD0A1, 4, 0xD0A2, 4) + compressed[8:]
            if len(result) > LIMIT:
                raise ValueError('Prepared job exceeds ESP 4 MiB limit')
    # Exclusive create: failed jobs never replace existing artifacts.
    try:
        with destination.open('xb') as out:
            out.write(result)
    except FileExistsError:
        raise
    except BaseException:
        destination.unlink(missing_ok=True)
        raise
    return {'format': 'espjob-v1', 'pages': pages, 'copies': copies,
            'bytes': len(result), 'sha256': hashlib.sha256(result).hexdigest()}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pdf', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--copies', type=int, default=1)
    args = parser.parse_args()
    try:
        print(json.dumps(render(args.pdf, args.output, args.copies)))
    except (ValueError, OSError, subprocess.SubprocessError, StopIteration) as error:
        parser.exit(1, f'Render failed: {error}\n')
