"""Offline packaging check; run with python3 check_installers.py."""
from pathlib import Path
import re
import hashlib
import subprocess
import sys

source = Path("station/station.ino").read_text()
for kind, filename in [("setup", "esp32-printer-setup-v3.ps1"), ("diagnostics", "esp32-printer-diagnostics.ps1"), ("remote", "esp32-printer-remote-v3.ps1")]:
    script = Path("installers", filename).read_text()
    assert script and ")PS\";" not in script
    assert "main/installers/" + filename in source
    assert "windows_" + kind + ".h" not in source
    if kind != "setup":
        assert script.startswith("param([string]$Token, [string]$StationIP)")
        assert "token=$Token" in script and "token='TOKEN'" not in script
assert "ssh-ed25519 AAAA" not in Path("installers/esp32-printer-remote-v3.ps1").read_text()
assert 'esp_read_mac(mac,ESP_MAC_WIFI_STA)' in source, "Station ID must not depend on Wi-Fi interface startup timing"
mac = Path("install_mac.sh").read_text()
backend_hash = re.search(r"^backend_sha=([0-9a-f]{64})$", mac, re.M)[1]
assert hashlib.sha256(Path("mac_backend.pl").read_bytes()).hexdigest() == backend_hash, "Update installer backend pin when backend changes"
assert "/opt/homebrew" not in mac and "Canon_LBP3300_1.ppd" not in mac, "Fresh Mac must not depend on developer Python/USB queue"
subprocess.run(["/bin/sh", "-n", "install_mac.sh"], check=True)
windows = Path("installers/esp32-printer-setup-v3.ps1").read_text()
assert windows.index("Resolve existing jobs") < windows.index("$source=Get-CimInstance"), "Check active queue before installing drivers"
assert '"ESP32Printer\\Stations\\$stationId"' in windows
assert '$taskName="ESP32CaptBridge-$stationId"' in windows
assert 'IPAddress.Loopback,__LOCAL_PORT__' in windows and 'c.BeginConnect("__STATION_IP__",9100' in windows
assert '-StationIP __STATION_IP__' in source and '"$capt_installer" __STATION_IP__' in source
assert '/setup/profile.json' in windows and '/setup/profile.json' in mac
assert 'cache_dir="$cache_dir/$id"' in mac and '-v "$uri"' in mac
assert "Add-PrinterDriver -Name 'Canon LBP3300' -InfPath $package.OriginalFileName" in windows
if len(sys.argv) == 2:
    folder = Path(sys.argv[1])
    for name, script in [("LBP3300_R150_V330_W64_uk_EN_2.exe", windows), ("mac-capt-v10010-uken.dmg", mac)]:
        digest = hashlib.sha256((folder / name).read_bytes()).hexdigest()
        assert digest in script, "Driver checksum pin differs from verified Canon download: " + name
print("Installer packaging, backend pin and bootstrap checks passed")

ui = Path("installers/esp32-printer-status.ps1").read_text()
assert "$statusSource=@'\n" + ui + "\n'@" in windows, "Embedded status UI differs from source"

mac_ui=Path("installers/esp32-printer-status-mac.js").read_bytes()
assert hashlib.sha256(mac_ui).hexdigest() == re.search(r"^status_sha=([0-9a-f]{64})$", mac, re.M)[1]
assert "com.esp32printer.window.$id" in mac and "LimitLoadToSessionType" in mac
assert "console_uid" in mac and 'bootstrap "gui/$console_uid"' in mac
