"""Read-only hardware check. --cycle: turn printer off, wait for loss, then on."""
import argparse
import json
import time
from capt_client import CaptClient


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cycle", action="store_true")
    args = parser.parse_args()
    client = CaptClient()
    previous_uptime = 0
    lost = False
    samples = 0
    previous = None
    deadline = time.monotonic() + (180 if args.cycle else 12)
    while time.monotonic() < deadline:
        status = json.loads(client.request("/status"))
        usb = status["usb"]
        if previous is None and args.cycle:
            assert usb["responsive"], "Start cycle check with printer connected and responding"
        assert status["uptime"] >= previous_uptime, "ESP rebooted during check"
        previous_uptime = status["uptime"]
        if usb["responsive"]:
            assert usb["connected"] and not usb["recovering"], usb
            assert usb["response_age_ms"] is not None and usb["response_age_ms"] <= 8000, usb
        summary = (usb["connected"], usb["responsive"], usb["recovering"], usb["recoveries"])
        if summary != previous:
            print("USB connected, responding, recovering, recoveries:", summary, flush=True)
            previous = summary
        if not usb["responsive"]:
            lost = True
        elif args.cycle and lost:
            print("PASS: printer reconnected, HTTP stayed available, ESP did not reboot")
            return
        elif not args.cycle:
            samples += 1
        time.sleep(1)
    if args.cycle:
        raise TimeoutError("Power-cycle recovery not observed; do not claim success")
    assert samples >= 8 and not lost, "Printer did not respond continuously"
    print("PASS: live USB heartbeat and HTTP status invariants")


if __name__ == "__main__":
    main()
