"""Read-only IPP checks by default. --incoming-job creates/cancels a header-only job when idle; no raster/page is sent."""
import argparse
import http.client
import socket
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('host')
parser.add_argument('--completed-job', type=int)
parser.add_argument('--incoming-job', action='store_true')
args = parser.parse_args()

def attribute(tag, name, value):
    name = name.encode()
    value = value.encode() if isinstance(value, str) else value
    return bytes([tag])+struct.pack('>H', len(name))+name+struct.pack('>H', len(value))+value

def request(operation, target=None, which=None):
    uri = f'ipp://{args.host}:631/ipp/print'
    packet = struct.pack('>BBHI', 1, 1, operation, 97)+b'\x01'
    packet += attribute(0x47, 'attributes-charset', 'utf-8')
    packet += attribute(0x48, 'attributes-natural-language', 'en')
    packet += attribute(0x45, 'job-uri' if target else 'printer-uri', uri+f'/{target}' if target else uri)
    if operation == 2:
        packet += attribute(0x49, 'document-format', 'image/pwg-raster')
    if operation == 10:
        packet += attribute(0x22, 'my-jobs', b'\x01')
        packet += attribute(0x42, 'requesting-user-name', 'LAN client')
        if which:
            packet += attribute(0x44, 'which-jobs', which)
    return packet+b'\x03'

def query(operation, target=None, which=None):
    connection = http.client.HTTPConnection(args.host, 631, timeout=3)
    try:
        connection.request('POST', '/ipp/print'+(f'/{target}' if target else ''), request(operation, target, which), {'Content-Type':'application/ipp'})
        response = connection.getresponse()
        data = response.read()
        assert response.status == 200 and data[:8] == struct.pack('>BBHI', 1, 1, 0, 97), (response.status, data[:8].hex())
        return data
    finally:
        connection.close()

# Hold one valid read-only request incomplete. Another connection must still answer.
packet = request(11)
with socket.create_connection((args.host, 631), timeout=3) as slow:
    slow.sendall((f'POST /ipp/print HTTP/1.1\r\nHost: {args.host}\r\nContent-Type: application/ipp\r\nContent-Length: {len(packet)}\r\n\r\n').encode()+packet[:-1])
    capabilities = query(11)
    assert attribute(0x44, 'pwg-raster-document-sheet-back', 'rotated') in capabilities
    assert attribute(0x44, '', 'DM3') in capabilities
print('PASS: a stalled read-only request does not block IPP polling; rotated/DM3 advertised')
if args.completed_job:
    assert attribute(0x23, 'job-state', struct.pack('>I', 9)) in query(9, args.completed_job)
    print('PASS: Get-Job-Attributes via job-uri reports completed')

if args.incoming_job:
    import time
    assert attribute(0x22, 'printer-is-accepting-jobs', b'\x01') in query(11), 'Station busy; test not started'
    packet = request(2)
    with socket.create_connection((args.host, 631), timeout=3) as upload:
        upload.sendall((f'POST /ipp/print HTTP/1.1\r\nHost: {args.host}\r\nContent-Type: application/ipp\r\nContent-Length: {len(packet)+4}\r\n\r\n').encode()+packet)
        prefix = b'\x21\x00\x06job-id\x00\x04'
        incoming = b''
        for _ in range(20):
            incoming = query(10)
            if prefix in incoming: break
            time.sleep(.05)
        assert prefix in incoming, 'Incoming job not published while upload waits for raster'
        job = struct.unpack('>I', incoming[incoming.index(prefix)+len(prefix):][:4])[0]
        assert attribute(0x23, 'job-state', struct.pack('>I', 4)) in query(9, job)
        time.sleep(1.1)  # Keep incoming job visible across idle CAPT status polls.
        assert attribute(0x23, 'job-state', struct.pack('>I', 4)) in query(9, job)
        query(8, job)
    for _ in range(30):
        final = query(9, job)
        if attribute(0x23, 'job-state', struct.pack('>I', 7)) in final: break
        time.sleep(.05)
    assert attribute(0x23, 'job-state', struct.pack('>I', 7)) in final
    assert prefix not in query(10), 'Default Get-Jobs returned a terminal job'
    assert prefix in query(10, which='completed')
    print('PASS: my-jobs finds pending-held job before raster; cancel sends no page; default Get-Jobs hides terminal history')
