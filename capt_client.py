"""Diagnostic CAPT transport through ESP32; default operation is read-only."""
import argparse
import base64
import json
from pathlib import Path
import re
import secrets
import struct
import time
import urllib.error
import urllib.parse
import urllib.request


class CaptClient:
    def __init__(self, base="http://192.168.1.199"):
        self.base = base.rstrip("/")
        self.session = secrets.randbelow(0xFFFFFFFD) + 2
        password = re.search(r'"([0-9a-f]+)"', Path(__file__).with_name("station").joinpath("device_secret.h").read_text())[1]
        self.headers = {"Authorization": "Basic " + base64.b64encode(("admin:" + password).encode()).decode()}
        page = self.request("/").decode()
        self.token = re.search(r'name="token" value="([0-9a-f]+)"', page)[1]

    def request(self, path, fields=None):
        body = None if fields is None else urllib.parse.urlencode(fields).encode()
        request = urllib.request.Request(self.base + path, data=body, headers=self.headers)
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.read()

    def transfer(self, data=None, read=0):
        fields = {"token": self.token, "session": self.session}
        fields.update({"read": read} if read else {"data": data.hex()})
        self.request("/printer/command", fields)
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            result = json.loads(self.request("/printer/command?session=" + str(self.session)))
            if result["state"] == 3:
                if result["error"]:
                    raise RuntimeError("USB transfer failed: " + str(result["error"]))
                return bytes.fromhex(result["data"])
            time.sleep(0.03)
        raise TimeoutError("USB operation still pending; do not retry print data blindly")

    def close(self):
        self.request("/printer/release", {"token": self.token, "session": self.session})

    def command(self, opcode, payload=b"", reply=True):
        if len(payload) > 4092:
            raise ValueError("CAPT payload exceeds 4092 bytes")
        self.transfer(struct.pack("<HH", opcode, len(payload) + 4) + payload)
        if not reply:
            return b""
        result = self.transfer(read=64)
        if len(result) < 4:
            raise ValueError("Truncated CAPT reply header: " + result.hex())
        actual, length = struct.unpack_from("<HH", result)
        if actual != opcode or not 4 <= length <= 4096:
            raise ValueError("Invalid CAPT reply: " + result.hex())
        while len(result) < length:
            part = self.transfer(read=min(4096, ((length - len(result) + 63) // 64) * 64))
            if not part:
                raise ValueError("Empty CAPT reply fragment")
            result += part
        if len(result) != length:
            raise ValueError("CAPT reply length mismatch: " + result.hex())
        return result[4:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="http://192.168.1.199")
    args = parser.parse_args()
    client = CaptClient(args.base)
    for fields, expected in [
        ({"token": "wrong", "data": "a1a10400"}, 403),
        ({"token": client.token, "data": "a1a10500"}, 400),
        ({"token": client.token, "data": "zzzzzzzz"}, 400),
        ({"token": client.token, "read": "63"}, 400),
        ({"token": client.token, "read": "4097"}, 400),
        ({"token": client.token, "read": "64x"}, 400),
        ({"token": client.token, "read": "64", "data": "a1a10400"}, 400),
    ]:
        try:
            client.request("/printer/command", fields)
        except urllib.error.HTTPError as error:
            assert error.code == expected, error.code
        else:
            raise AssertionError("Invalid CAPT request accepted")
    print("PASS: CAPT CSRF, hex, packet length, read size validation")
    before = json.loads(client.request("/status"))
    assert "MDL:LBP3300;CMD:CAPT;" in before["usb"]["details"], before
    for opcode in (0xA1A1, 0xE0A0, 0xA0A0, 0xA0A8):
        reply = client.command(opcode)
        print(f"{opcode:04X}: {reply.hex(' ')}")
        assert len(reply) >= 2, (opcode, reply)
    after = json.loads(client.request("/status"))
    assert after["uptime"] >= before["uptime"], (before, after)
    assert after["usb"]["connected"] and after["usb"]["error"] == 0, after
    client.close()
    print("PASS: CAPT bulk OUT/IN, matching reply commands, printer still connected")


if __name__ == "__main__":
    main()
