"""Run production migration/guards with real files and spaces, without root."""
import os,subprocess,tempfile,json
from pathlib import Path
source=Path('install_mac.sh').read_text()
assert 'cache_dir="/Library/Application Support/ESP32Printer"' in source
block=source[source.index('if [ -n "$jobs" ]'):source.index('current_uri=$(')]
with tempfile.TemporaryDirectory() as work:
 root=Path(work);old=root/'Caches/ESP32Printer/station';new=root/'Application Support/ESP32Printer/station'
 old.mkdir(parents=True,mode=0o700)
 for name,data in {'cloud.json':'{"token":"test-only"}','enrollment.json':'{"client_id":"existing"}','cloud-completed.json':'{"id":"previous"}'}.items():
  f=old/name;f.write_text(data);f.chmod(0o600)
 env={**os.environ,'jobs':'','cache_dir':str(new),'legacy_cache_dir':str(old)}
 (old/'cloud-job.json').write_text('unresolved')
 assert subprocess.run(['/bin/sh','-ec',block],env=env,capture_output=True).returncode!=0
 assert not new.exists(),'Migrated while cloud receipt unresolved'
 (old/'cloud-job.json').unlink()
 subprocess.run(['/bin/sh','-ec',block],env=env,check=True)
 for name in ['cloud.json','enrollment.json','cloud-completed.json']:
  assert (new/name).read_bytes()==(old/name).read_bytes()
  assert (new/name).stat().st_mode&0o777==0o600
 (new/'cloud.json').write_text('keep persistent identity')
 subprocess.run(['/bin/sh','-ec',block],env=env,check=True)
 assert (new/'cloud.json').read_text()=='keep persistent identity','Merged stale legacy secrets'
 assert '/Library/Application Support/ESP32Printer' in Path('mac_backend.pl').read_text()
 assert '/Library/Application Support/ESP32Printer' in Path('configure_cloud.py').read_text()
print('PASS: legacy migration preserves identities, receipts and permissions; unresolved job blocks migration; existing persistent state wins')
