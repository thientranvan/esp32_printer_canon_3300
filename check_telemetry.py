"""Live telemetry boundary checks, no print commands: python3 check_telemetry.py."""
import json
import socket
import urllib.request
import urllib.error
from capt_client import CaptClient
from capt_tcp_client import CaptTCPClient


def main():
    c = CaptClient()
    initial = json.loads(c.request('/status'))
    assert not initial['job_control']['active'] and not initial['cloud']['job'], 'Station must be idle'
    host = initial['ip']
    previous = next((r for r in initial.get('clients', {}).get('items', [])
                     if r.get('os') == 'macos' and r.get('name', '').lower() == socket.gethostname().lower()), None)
    client_id = previous['id'] if previous else ('mac-' + socket.gethostname() + '-' + 'telemetry-check').replace('.', '_')[:64]
    report = dict(id=client_id, name='Telemetry check', os='macos', queue='Diagnostic only', truncated=False, jobs=[])

    def post(value, expected=202, content_type='application/json'):
        data = json.dumps(value, ensure_ascii=False).encode()
        req = urllib.request.Request(f'http://{host}/printer/client-report', data=data, headers={'Content-Type': content_type})
        try:
            with urllib.request.urlopen(req, timeout=3) as r:
                code = r.status
        except urllib.error.HTTPError as e:
            code = e.code
        assert code == expected, (code, expected)

    post(report, 403, 'text/plain')
    post({**report, 'os': 'unknown'}, 400)
    post({**report, 'jobs': [dict(id='1', title='x'*129, state='queued')]}, 400)
    post({**report, 'jobs': [dict(id='1', title='test', state='queued', total=-1)]}, 400)
    post({**report, 'jobs': [dict(id='1', title='test', state='queued')]*5}, 400)
    post({**report, 'jobs': [dict(id='1', title='test', state='queued\n')]}, 400)
    post({**report, 'jobs': [dict(id='1', title='test', state='queued', stage='unknown')]}, 400)
    post(report)
    state = json.loads(c.request('/status'))
    saved = next(r for r in state['clients']['items'] if r['id'] == client_id)
    assert not saved['stale'] and saved['jobs'] == []
    assert state['clients']['scope'] == 'reporting_clients_only'
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
    if previous:
        post(previous)
    print('PASS: report boundaries, empty queue, capabilities, metadata ACK; no print submitted')


if __name__ == '__main__':
    main()
