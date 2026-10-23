"""Live telemetry boundary checks, no print commands: python3 check_telemetry.py."""
import json
import urllib.request
import urllib.error
from capt_client import CaptClient
from capt_tcp_client import CaptTCPClient


def main():
    c = CaptClient()
    initial = json.loads(c.request('/status'))
    assert not initial['job_control']['active'] and not initial['cloud']['job'], 'Station must be idle'
    host = initial['ip']
    assert initial['status_source'] == 'esp32' and 'clients' not in initial
    request = urllib.request.Request(f'http://{host}/printer/client-report',
                                     data=b'{}', headers={'Content-Type': 'application/json'})
    try:
        urllib.request.urlopen(request, timeout=3)
        raise AssertionError('Removed reporting endpoint still exists')
    except urllib.error.HTTPError as error:
        assert error.code == 404, error.code
    state = initial
    assert state['capt']['capabilities']['toner_percent'] is False
    assert state['capt']['toner_percent'] is None
    job = state['job_control']
    assert not job['progress_known'] and not any(job['actions'].values())
    tcp = CaptTCPClient(host)
    try:
        assert tcp.command(0xF002, b'telemetry-check') == b'\0\1'
        meta = dict(title='Kiểm tra tiếng Việt', client_name='Telemetry check', client_os='macos')
        assert tcp.command(0xF004, json.dumps(meta, ensure_ascii=False).encode()) == b'\0\1'
        # No F001 or CAPT raster/reservation commands: printer never gets this metadata.
    finally:
        tcp.close()
    for _ in range(30):
        snapshot = json.loads(c.request('/status'))
        assert snapshot['uptime'] >= initial['uptime'], 'Station rebooted while assembling status'
    print('PASS: ESP-only status, removed report endpoint, capabilities, metadata ACK; no print submitted')


if __name__ == '__main__':
    main()
