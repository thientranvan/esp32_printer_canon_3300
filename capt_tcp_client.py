"""Direct CAPT client for ESP32's LAN-only TCP endpoint."""
import socket
import struct


class CaptTCPClient:
    def __init__(self, host="192.168.1.199", port=9100):
        self.socket = socket.create_connection((host, port), timeout=70)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def _read(self, count):
        result = bytearray()
        while len(result) < count:
            data = self.socket.recv(count - len(result))
            if not data:
                raise ConnectionError("ESP closed CAPT connection; do not resend job blindly")
            result.extend(data)
        return bytes(result)

    def command(self, opcode, payload=b"", reply=True):
        if len(payload) > 65531:
            raise ValueError("CAPT frame exceeds 65535 bytes")
        self.socket.sendall(struct.pack("<HH", opcode, len(payload) + 4) + payload)
        if not reply:
            return b""
        actual, length = struct.unpack("<HH", self._read(4))
        if actual != opcode or not 4 <= length <= 4096:
            raise ValueError("Invalid CAPT response header")
        return self._read(length - 4)

    def close(self):
        self.socket.close()
