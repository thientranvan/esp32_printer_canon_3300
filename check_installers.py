"""Offline packaging check; run with python3 check_installers.py."""
from pathlib import Path
import re

source = Path("station/station.ino").read_text()
for kind, filename in [("setup", "esp32-printer-setup-v3.ps1"), ("diagnostics", "esp32-printer-diagnostics.ps1"), ("remote", "esp32-printer-remote-v3.ps1")]:
    script = Path("installers", filename).read_text()
    assert script and ")PS\";" not in script
    assert "main/installers/" + filename in source
    assert "windows_" + kind + ".h" not in source
    if kind != "setup":
        assert script.startswith("param([string]$Token)")
        assert "token=$Token" in script and "token='TOKEN'" not in script
assert "ssh-ed25519 AAAA" not in Path("installers/esp32-printer-remote-v3.ps1").read_text()
print("Installer packaging checks passed")
