"""One A4 test page using installed Canon LBP3300 filter and ESP USB transport."""
import argparse
import json
from datetime import datetime
from pathlib import Path
import struct
import subprocess
import tempfile
import time
from capt_client import CaptClient

PENDING = Path(__file__).with_name(".print-test-pending.json")


def packet(opcode, payload=b""):
    return struct.pack("<HH", opcode, len(payload) + 4) + payload


def cassette_request(parameters):
    if len(parameters) < 44 or parameters[:4] != packet(0xD0A0, bytes(40))[:4]:
        raise ValueError("Missing page parameters")
    width, height = struct.unpack_from("<HH", parameters, 0x22)
    shift = parameters[0x11]
    return struct.pack("<BBHHBB", parameters[8], parameters[0x28],
                       width >> (shift & 15), height >> (shift >> 4),
                       1, parameters[9] & 8)


def extract_page(job):
    # ponytail: this Mac CAPT wrapper and one LBP3300 page only; add other
    # wrapper versions after obtaining their format and hardware traces.
    if not job.startswith(b"dBMpM`obED|~|000ccp0"):
        raise ValueError("Unknown Canon job wrapper")
    start = job.find(packet(0xD0A0, b"\0" * 40)[:4])
    if start < 0:
        raise ValueError("No LBP3300 page parameters")
    position = start
    commands = []
    while position < len(job):
        if position + 4 > len(job):
            raise ValueError("Truncated job header")
        opcode, length = struct.unpack_from("<HH", job, position)
        header = 4
        if opcode == 0x8200:
            if position + 8 > len(job):
                raise ValueError("Truncated raster header")
            header = 8
            length = struct.unpack_from("<I", job, position + 4)[0]
        if length < header or position + length > len(job):
            raise ValueError("Invalid job packet length")
        if opcode not in (0xD0A0, 0xD0A4, 0xD0A1, 0x8200, 0xC0A4, 0xD0A2, 4, 3):
            raise ValueError(f"Unsupported job command {opcode:04X}")
        commands.append((opcode, job[position + header:position + length]))
        position += length
    ops = [op for op, _ in commands]
    if ops[:3] != [0xD0A0, 0xD0A4, 0xD0A1] or ops[-4:] != [0xC0A4, 0xD0A2, 4, 3]:
        raise ValueError("Expected exactly one complete page")
    if not ops[3:-4] or any(op != 0x8200 for op in ops[3:-4]):
        raise ValueError("Unexpected raster sequence")
    page = commands[0][1]
    if len(page) != 40 or page[2:4] != b"\x94\x2a" or page[4] != 2:
        raise ValueError("Expected LBP3300 A4 page")
    # Match the native backend: D0A9 groups all D0 parameters, including end-page;
    # 8200's 8-byte host header is replaced by C0A0 on the USB wire.
    parameters = b"".join(packet(op, payload) for op, payload in commands if op >> 8 == 0xD0)
    raster = [payload for op, payload in commands if op == 0x8200]
    return parameters, raster


def make_pdf():
    content = b"BT /F1 24 Tf 60 750 Td (ESP32-S3 LAN PRINT TEST) Tj /F1 14 Tf 0 -40 Td (Canon LBP3300 - one page) Tj ET\n"
    objects = [b"<< /Type /Catalog /Pages 2 0 R >>",
               b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
               b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
               b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
               b"<< /Length " + str(len(content)).encode() + b" >>\nstream\n" + content + b"endstream"]
    result = b"%PDF-1.4\n"
    offsets = [0]
    for i, obj in enumerate(objects, 1):
        offsets.append(len(result))
        result += f"{i} 0 obj\n".encode() + obj + b"\nendobj\n"
    xref = len(result)
    result += b"xref\n0 6\n0000000000 65535 f \n"
    result += b"".join(f"{offset:010d} 00000 n \n".encode() for offset in offsets[1:])
    return result + f"trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()


def job_info(job_id, stage, page):
    result = bytearray(72)
    struct.pack_into("<I", result, 4, page)
    result[16:18] = bytes([stage, 1])
    struct.pack_into("<HhhH", result, 18, job_id, -420, -420, datetime.now().year)
    now = datetime.now()
    result[26:32] = bytes([now.month - 1, now.day, now.hour, now.minute, now.second, 1])
    return bytes(result)


def print_page(client, parameters, raster, pending_path=PENDING):
    if pending_path.exists():
        raise RuntimeError("Previous print result uncertain; resolve pending marker before another page")
    def ack(opcode, payload=b""):
        reply = client.command(opcode, payload)
        print(f"{opcode:04X}: {reply.hex(' ')}", flush=True)
        if len(reply) < 2 or reply[0] & 0x80:
            raise RuntimeError(f"Printer rejected {opcode:04X}: {reply.hex()}")
        return reply

    def status():
        reply = client.command(0xA0A8)
        if len(reply) < 36 or reply[0] & 0x80:
            raise RuntimeError("Invalid extended status: " + reply.hex())
        counters = struct.unpack_from("<4H", reply, 14)
        print("Status:", reply[:14].hex(' '), "pages:", counters, flush=True)
        return reply, counters

    info = client.command(0xA1A1)
    if len(info) < 4 or info[2:4] != b"\x94\x2a":
        raise RuntimeError("Printer model mismatch")
    initial, counters = status()
    if len(set(counters)) != 1:
        raise RuntimeError("Existing page still pending; refusing to add test job")
    ack(0xA3A2)
    reserved = ack(0xA2A0, struct.pack("<HHI", 0, 30, 0))
    if len(reserved) != 4:
        raise RuntimeError("Unexpected reservation reply")
    job_id = struct.unpack_from("<H", reserved, 2)[0]
    pending_path.write_text(json.dumps({"job_id": job_id, "page": 1}))
    print("Reserved job", job_id, flush=True)
    ack(0xE1A1, job_info(job_id, 1, 0))
    for opcode in (0xE0A3, 0xE0A2, 0xE0A4):
        ack(opcode)
    ack(0xE0A5, struct.pack("<IHBB8x", 0xADEADBEE, 0, 0, 0))
    status()
    # ponytail: A4 in cassette 1 only; add tray selection after verifying each source.
    selected = ack(0xE1A9, cassette_request(parameters))
    if selected != b"\x00\x01":
        raise RuntimeError("Cassette 1 unavailable: " + selected.hex())
    parameters = bytearray(parameters)
    parameters[0x0A] = selected[1]  # Native backend patches D0A0 source after E1A9.
    parameters = bytes(parameters)
    client.command(0xD0A9, parameters, reply=False)
    for band in raster:
        for offset in range(0, len(band), 4092):
            client.command(0xC0A0, band[offset:offset + 4092], reply=False)
    client.command(0xC0A4, reply=False)
    print("Raster sent:", sum(map(len, raster)), "bytes", flush=True)
    deadline = time.monotonic() + 20
    while True:
        reply, counters = status()
        if counters[0] >= 1:
            break
        if time.monotonic() > deadline:
            raise TimeoutError("Printer has not decoded page; no automatic resend")
        time.sleep(0.5)
    page = counters[0]
    ack(0xE1A1, job_info(job_id, 2, page))
    ack(0xE0A7, struct.pack("<H", page))
    ack(0xE1A1, job_info(job_id, 6, page))
    deadline = time.monotonic() + 60
    while True:
        reply, counters = status()
        if counters[3] >= page:
            break
        if time.monotonic() > deadline:
            raise TimeoutError("Page completion not confirmed; no automatic resend")
        time.sleep(1)
    ack(0xE1A1, job_info(job_id, 4, page))
    ack(0xE0A9, struct.pack("<H", job_id))
    pending_path.unlink(missing_ok=True)
    print("Counter threshold reached; job released. Physical printing remains unverified.")


def self_test():
    prefix = b"dBMpM`obED|~|000ccp0"
    page = bytearray(40)
    page[2:5] = b"\x94\x2a\x02"
    job = prefix + packet(0xD0A0, page) + packet(0xD0A4, b"12345678") + packet(0xD0A1)
    job += struct.pack("<HHI", 0x8200, 0x100, 11) + b"abc"
    job += packet(0xC0A4) + packet(0xD0A2) + packet(4, b"1234") + packet(3)
    params, bands = extract_page(job)
    assert bands == [b"abc"] and params.endswith(packet(0xD0A2))
    for bad in (b"PDF", job[:-1], job + job[len(prefix):], job.replace(b"\x94\x2a", b"\x30\x2a")):
        try:
            extract_page(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("Malformed or multi-page job accepted")
    assert len(job_info(1, 2, 3)) == 72
    sample = bytearray(packet(0xD0A0, page))
    sample[0x11] = 0x11
    sample[0x28] = 1
    struct.pack_into("<HH", sample, 0x22, 4960, 7014)
    assert cassette_request(sample) == struct.pack("<BBHHBB", 2, 1, 2480, 3507, 1, 0)
    print("PASS: one-page wrapper extraction, raster header removal, invalid input rejection")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--finish", action="store_true", help="check pending page and close completed job without resending")
    parser.add_argument("--send", action="store_true", help="send exactly one generated test page")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.finish:
        pending = json.loads(PENDING.read_text())
        if pending.get("physical_unverified"):
            raise RuntimeError("Previous job produced no sheet despite counter changes; resolve protocol before another test")
        client = CaptClient()
        reply = client.command(0xA0A8)
        if len(reply) < 36 or reply[0] & 0x80:
            raise RuntimeError("Cannot verify pending page")
        counters = struct.unpack_from("<4H", reply, 14)
        print("Pending page:", pending, "printer counters:", counters, "status:", reply[:14].hex(' '))
        if counters[3] < pending["page"]:
            print("Page still pending. No data resent; leave printer powered on.")
            return
        for opcode, payload in [(0xE1A1, job_info(pending["job_id"], 4, pending["page"])),
                                (0xE0A9, struct.pack("<H", pending["job_id"]))]:
            response = client.command(opcode, payload)
            if len(response) < 2 or response[0] & 0x80:
                raise RuntimeError("Cannot close completed job: " + response.hex())
        PENDING.unlink()
        print("Counter threshold reached; job closed. Physical printing remains unverified.")
        return
    if args.send and PENDING.exists():
        raise RuntimeError("Test job already pending; use --finish, never resend blindly")
    with tempfile.TemporaryDirectory(prefix="esp32-test-") as directory:
        pdf = Path(directory) / "test.pdf"
        pdf.write_bytes(make_pdf())
        result = subprocess.run(["/usr/sbin/cupsfilter", "-e", "-p", "/etc/cups/ppd/Canon_LBP3300_1.ppd", "-m", "printer/Canon_LBP3300_1", str(pdf)], capture_output=True, timeout=90, check=True)
    parameters, raster = extract_page(result.stdout)
    print("Validated one A4 page:", len(raster), "raster blocks,", sum(map(len, raster)), "bytes")
    if args.send:
        print_page(CaptClient(), parameters, raster)
    else:
        print("Prepared only. Use --send to print.")


if __name__ == "__main__":
    main()
