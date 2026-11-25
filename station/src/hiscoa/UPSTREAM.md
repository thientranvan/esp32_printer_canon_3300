Source: https://github.com/mounaiban/captdriver
Commit: 62719249ac34633338be54bc74beddd0e7003d38
Archive SHA-256: cb5977b0a5360431520c4770ca0bec7f414712717fd720d6feecf0cf71feffb8
License: GPL-3.0-or-later; see COPYING and file headers.
Local patch: restore 32-bit alignment guard in hiscoa-compress.c (same fix as server-renderer/build.sh).
Corresponding source included; no Canon binaries or third-party firmware copied.

Test-only upstream decoder copied to tests/airprint/hiscoa-decompress.{c,h}; changed include paths and disabled stderr tracing. Same upstream project license (COPYING above). Decoder is not linked into ESP firmware.
