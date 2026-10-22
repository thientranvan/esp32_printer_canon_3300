"""Normalize a Canon Windows CAPT capture to espjob-v1; no PDF rendering here.

Usage: python3 prepare_cloud_job.py input.capt output.espjob
Run checks: python3 prepare_cloud_job.py --self-test
"""
from pathlib import Path
import struct
import sys
import hashlib

LIMIT = 4 * 1024 * 1024


def convert(raw):
    def need(ok, message):
        if not ok:
            raise ValueError(message)

    offset = 0

    def packet():
        nonlocal offset
        need(offset + 4 <= len(raw), 'Truncated packet')
        op, size = struct.unpack_from('<HH', raw, offset)
        header = 4
        if op == 0x8200:
            need(offset + 8 <= len(raw), 'Truncated band header')
            size = struct.unpack_from('<I', raw, offset + 4)[0]
            header = 8
        need(header <= size <= len(raw) - offset, 'Invalid packet size')
        body = raw[offset + header:offset + size]
        offset += size
        return op, body

    need(len(raw) <= 20 * 1024 * 1024, 'Capture exceeds 20 MiB')
    op, wrapper = packet()
    need(op == 1 and len(wrapper) == 36, 'Expected Canon Windows wrapper')
    magic, copies, collate, duplex = struct.unpack_from('<IIII', wrapper)
    need(magic == 0xFADEDACE and 1 <= copies <= 99 and collate in (0, 1) and duplex in (1, 2, 3), 'Unsupported wrapper')
    pages = []
    total_bands = 0
    while True:
        op, body = packet()
        if op == 3:
            need(not body and offset == len(raw) and pages, 'Invalid job end')
            break
        need(op == 2 and len(body) == 108 and struct.unpack_from('<I', body)[0] == 9, 'Only A4 supported')
        parameters = bytearray()
        for expected, length in [(0xD0A0, 40), (0xD0A4, 8), (0xD0A1, 0)]:
            op, body = packet()
            need(op == expected and len(body) == length, 'Invalid page parameters')
            parameters += struct.pack('<HH', op, length + 4) + body
        need(struct.unpack_from('<H', parameters, 6)[0] == 0x2A94 and parameters[8] in (0, 2), 'Wrong model or media')
        shift = parameters[17]
        w, h = struct.unpack_from('<HH', parameters, 34)
        need(w >> (shift & 15) == 2480 and h >> (shift >> 4) == 3507, 'Only A4 600 dpi supported')
        parameters[8], parameters[40] = 2, 1
        bands = []
        op, body = packet()
        while op == 0x8200:
            need(body and len(bands) < 4096, 'Invalid band')
            bands.append(body)
            op, body = packet()
        need(op == 0xC0A4 and not body and bands, 'Missing raster end')
        op, body = packet()
        need(op == 0xD0A2 and not body, 'Missing page end')
        parameters += struct.pack('<HH', 0xD0A2, 4)
        op, body = packet()
        need(op == 4 and len(body) == 4, 'Missing trailer')
        total_bands += len(bands)
        need(total_bands <= 4096, 'Maximum 4096 bands per job')
        pages.append(parameters + struct.pack('<I', len(bands)) + b''.join(struct.pack('<I', len(b)) + b for b in bands))
        need(len(pages) <= 100, 'Maximum 100 pages')
    result = struct.pack('<8sHHBBH', b'ESPJOB1\n', len(pages), copies, duplex, collate, 0) + b''.join(pages)
    need(len(result) <= LIMIT, 'Prepared job exceeds 4 MiB')
    return result


def self_test():
    def p(op, b=b''):
        return struct.pack('<HH', op, len(b) + 4) + b
    page = bytearray(40)
    struct.pack_into('<H', page, 2, 0x2A94)
    struct.pack_into('<HH', page, 30, 2480, 3507)
    raw = (p(1, struct.pack('<IIII', 0xFADEDACE, 2, 1, 3) + bytes(20)) + p(2, struct.pack('<I', 9) + bytes(104)) +
           p(0xD0A0, page) + p(0xD0A4, bytes(8)) + p(0xD0A1) + struct.pack('<HHI', 0x8200, 0x100, 11) +
           b'abc' + p(0xC0A4) + p(0xD0A2) + p(4, bytes(4)) + p(3))
    result = convert(raw)
    assert result[:16] == struct.pack('<8sHHBBH', b'ESPJOB1\n', 1, 2, 3, 1, 0)
    assert result[16 + 8] == 2 and result.endswith(b'abc')
    for bad in [raw[:-1], raw + b'x', bytes(len(raw))]:
        try:
            convert(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid capture accepted')
    print('PASS: Windows capture normalization, options, truncation, model validation')


if __name__ == '__main__':
    if sys.argv[1:] == ['--self-test']:
        self_test()
    elif len(sys.argv) == 3:
        output = convert(Path(sys.argv[1]).read_bytes())
        # Refuse overwriting an existing job artifact.
        with Path(sys.argv[2]).open('xb') as f:
            f.write(output)
        print(f'{len(output)} bytes; SHA-256 {hashlib.sha256(output).hexdigest()}')
    else:
        raise SystemExit(__doc__)
