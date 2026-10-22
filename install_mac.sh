#!/bin/sh
set -eu
# Run with sudo. Installs local queue only; this Mac is not a shared print server.
if [ "$(id -u)" -ne 0 ]; then
  echo "Run: sudo sh install_mac.sh" >&2
  exit 1
fi
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python_bin=/opt/homebrew/bin/python3.13
driver_ppd=/etc/cups/ppd/Canon_LBP3300_1.ppd
target_dir=/Library/Printers/ESP32Printer
test -x "$python_bin"
test -f "$driver_ppd"
install -d -m 755 "$target_dir"
install -d -m 700 /Library/Caches/ESP32Printer
install -m 644 "$source_dir/mac_backend.pl" "$target_dir/mac_backend.pl"
"$python_bin" - "$driver_ppd" "$target_dir/Canon_ESP32.ppd" <<'PY'
from pathlib import Path
import re, sys
text = Path(sys.argv[1]).read_text()
for option, value in [('InputSlot', 'Cas1'), ('PageSize', 'A4'), ('PageRegion', 'A4'), ('Duplex', 'None')]:
    text = re.sub(r'(?m)^\*Default' + option + r':.*$', '*Default' + option + ': ' + value, text)
Path(sys.argv[2]).write_text(text)
PY
backend_tmp=$(mktemp)
trap 'rm -f "$backend_tmp"' EXIT
printf '#!/bin/sh\nexec /usr/bin/perl %s/mac_backend.pl "$@"\n' "$target_dir" > "$backend_tmp"
install -m 700 "$backend_tmp" /usr/libexec/cups/backend/esp32capt
/usr/sbin/lpadmin -p Canon_LBP3300_ESP32 -E \
  -v esp32capt://192.168.1.199:9100 \
  -P "$target_dir/Canon_ESP32.ppd" \
  -D 'Canon LBP3300 (ESP32 LAN)' -L 'ESP32 192.168.1.199' \
  -o printer-is-shared=false -o printer-error-policy=stop-printer
echo 'Installed: Canon LBP3300 (ESP32 LAN). Select A4, Cassette 1, one-sided.'
