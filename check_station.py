"""Run on the ESP access point: python check_station.py http://192.168.4.1"""
import getpass
import json
import sys
import time
import urllib.error
import urllib.request

base = sys.argv[1].rstrip("/")
password = getpass.getpass("Device password: ")
credentials = urllib.request.HTTPPasswordMgrWithDefaultRealm()
credentials.add_password(None, base, "admin", password)
client = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(credentials))
try:
    urllib.request.urlopen(base + "/status", timeout=10)
    raise AssertionError("Status exposed without authentication")
except urllib.error.HTTPError as error:
    assert error.code == 401, error.code
with client.open(base + "/status", timeout=10) as response:
    status = json.load(response)
assert status["healthy"] and status["flash"] == 16777216 and status["psram"] == 8388608, status
assert status["static_ip"] is True, status
assert isinstance(status["ssid"], str) and isinstance(status["rssi"], int), status
assert status["usb"]["error"] == 0, status
if status["usb"]["installed"]:
    assert status["usb"]["host_mode"] and status["usb"]["power_command"], status
else:
    assert not status["usb"]["host_mode"] and not status["usb"]["connected"], status
    if status["reset_reason"] not in (4, 5, 6, 7):
        with client.open(base + "/crash", timeout=10) as response:
            assert "Task:" in response.read().decode(), status
assert isinstance(status["usb"]["physical_link"], bool), status
assert isinstance(status["usb"]["connected"], bool) and isinstance(status["usb"]["details"], str), status
if status["connected"]:
    assert status["ssid"] and status["ip"] == "192.168.1.199", status
with client.open(base + "/", timeout=10) as response:
    assert 'name="token"' in response.read().decode()
with client.open(base + "/support/command", timeout=10) as response:
    assert isinstance(json.load(response)["command"], str)
for path in ("/wifi", "/usb/start", "/printer/command", "/printer/release", "/support/command"):
    try:
        client.open(urllib.request.Request(base + path, data=b"ssid=test&password=12345678&token=wrong"), timeout=10)
        raise AssertionError("Invalid CSRF token accepted: " + path)
    except urllib.error.HTTPError as error:
        assert error.code == 403, error.code
print("PASS: authentication, diagnostic status, configuration page, CSRF rejection")
if not status["usb"]["installed"]:
    print("Recovery mode: USB Host disabled after abnormal reset", status["reset_reason"])
if "--usb" in sys.argv[2:] or "--probe" in sys.argv[2:]:
    assert status["usb"]["installed"] and status["usb"]["connected"], status
    assert "VID=04A9 PID=267E" in status["usb"]["details"], status
    time.sleep(10)
    with client.open(base + "/status", timeout=10) as response:
        later = json.load(response)
    assert later["uptime"] >= status["uptime"] + 9, (status, later)
    assert later["usb"]["connected"] and later["usb"]["error"] == 0, later
    assert "VID=04A9 PID=267E" in later["usb"]["details"], later
    print("PASS: LBP3300 enumerated and USB Host stayed running")

    if "--probe" in sys.argv[2:]:
        assert "GET_DEVICE_ID: MFG:Canon;MDL:LBP3300;CMD:CAPT;" in later["usb"]["details"], later
        print("PASS: USB Printer Class GET_DEVICE_ID returned Canon LBP3300 CAPT identity")
