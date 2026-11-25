"""Read-only IPP checks; no Print-Job, cancel, USB reset or document upload."""
import argparse
import http.client
import socket
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('host')
parser.add_argument('--completed-job', type=int)
args = parser.parse_args()

def attribute(tag, name, value):
    name = name.encode()
    value = value.encode() if isinstance(value, str) else value
    return bytes([tag])+struct.pack('>H', len(name))+name+struct.pack('>H', len(value))+value

def request(operation, target=None):
    uri = f'ipp://{args.host}:631/ipp/print'
    packet = struct.pack('>BBHI', 1, 1, operation, 97)+b'\x01'
    packet += attribute(0x47, 'attributes-charset', 'utf-8')
    packet += attribute(0x48, 'attributes-natural-language', 'en')
    packet += attribute(0x45, 'job-uri' if target else 'printer-uri', uri+f'/{target}' if target else uri)
    return packet+b'\x03'

def query(operation, target=None):
    connection = http.client.HTTPConnection(args.host, 631, timeout=3)
    try:
        connection.request('POST', '/ipp/print'+(f'/{target}' if target else ''), request(operation, target), {'Content-Type':'application/ipp'})
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
