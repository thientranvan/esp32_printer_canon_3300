# AirPrint / IPP — experimental only

`airprint-2026112501.bin` is an opt-in ESP32-S3 N16R8 application image (not merged flash image). **No phone/printer hardware acceptance yet.** Do not use as the stable OTA image. `../latest.json` and `../station.bin` remain unchanged.

Build: `python3 build_airprint_experimental.py`. Source and build flags are in the repository; Hi-SCoA source and GPL-3.0-or-later license are included under `station/src/hiscoa/`.

Only manually upload to an already provisioned, idle test station using the same 16MB/3MB OTA layout. Never flash this image at offset 0 or erase NVS. COM/UART0 preferred; no automatic installation on existing stations.

Feature limits and verification steps: [AirPrint implementation](../../docs/airprint-implementation-20261125.md).

Update 2026-11-25: user subsequently requested automatic rollout. Public OTA now carries AirPrint preview 2026112502; this archived 2026112501 image is not the automatic-update target. Hardware acceptance remains pending. See [OTA release](../../docs/airprint-ota-release-20261125.md).
