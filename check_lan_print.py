"""Offline parser checks, plus --hardware for TCP/HTTP exclusivity (no printing)."""
import argparse
import json
import socket
import struct
import threading
import subprocess
from pathlib import Path
import time
import urllib.error
from capt_client import CaptClient
from capt_tcp_client import CaptTCPClient
from print_test import packet


def self_test():
    subprocess.run(["/usr/bin/perl", str(Path(__file__).with_name("mac_backend.pl")), "--self-test"], check=True)
    # Fragmented TCP response must be reassembled exactly.
    left, right = socket.socketpair()
    client = CaptTCPClient.__new__(CaptTCPClient)
    client.socket = left

    def reply():
        assert right.recv(4) == packet(0xA1A1)
        for fragment in (b"\xa1", b"\xa1\x08", b"\x00", b"\x00\x00\x94\x2a"):
            right.sendall(fragment)
        right.close()

    worker = threading.Thread(target=reply)
    worker.start()
    assert client.command(0xA1A1) == b"\0\0\x94\x2a"
    client.close()
    worker.join()
    print("PASS: multi-page validation, malformed input, fragmented TCP response")


def hardware():
    http = CaptClient()
    first = http.command(0xA1A1)
    http.close()
    tcp = CaptTCPClient()
    try:
        assert tcp.command(0xA1A1) == first
        competitor = CaptClient()
        try:
            competitor.transfer(packet(0xA1A1))
        except urllib.error.HTTPError as error:
            assert error.code == 409
        else:
            raise AssertionError("Concurrent HTTP client stole TCP session")
        for _ in range(3):
            assert len(tcp.command(0xA0A8)) >= 36
            time.sleep(1)
    finally:
        tcp.close()
    time.sleep(0.1)
    assert http.command(0xA1A1) == first
    http.close()
    status = json.loads(http.request("/status"))
    assert status["usb"]["connected"] and status["tcp"]["commands"] >= 4, status
    print("PASS: real TCP CAPT, HTTP/TCP isolation, ownership release, live USB")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hardware", action="store_true")
    args = parser.parse_args()
    self_test()
    if args.hardware:
        hardware()
