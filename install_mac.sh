#!/bin/sh
set -eu
export LC_ALL=C
if [ "$(id -u)" -ne 0 ]; then
  echo "Run: sudo sh install_mac.sh" >&2
  exit 1
fi
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
target_dir=/Library/Printers/ESP32Printer
cache_dir=/Library/Caches/ESP32Printer
driver_filter=/Library/Printers/Canon/CUPSCAPT2/Bins/capdftopdl
driver_ppd=/Library/Printers/PPDs/Contents/Resources/CNMC2LBP3300AUK.ppd.gz
backend_sha=46abdfc67f578dc630ae1a63ae8ee7258434a46f0173cd700542ba7dc8634870
backend_url=https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/db841d759aafb762096b5630f5d4c4bb8d054840/mac_backend.pl
jobs=$(/usr/bin/lpstat -W not-completed -o Canon_LBP3300_ESP32 2>/dev/null || true)
if [ -n "$jobs" ] || [ -e "$cache_dir/pending.txt" ]; then
  echo 'Resolve existing/uncertain print jobs before installing; no jobs were deleted.' >&2
  exit 1
fi
current_uri=$(/usr/bin/lpstat -v Canon_LBP3300_ESP32 2>/dev/null || true)
if [ -n "$current_uri" ]; then
  case "$current_uri" in
    *'esp32capt://192.168.1.199:9100') ;;
    *) echo 'Existing printer name has a different device URI; installation stopped.' >&2; exit 1 ;;
  esac
fi
work=$(/usr/bin/mktemp -d)
mounted=false
cleanup() {
  if [ "$mounted" = true ]; then /usr/bin/hdiutil detach "$work/mount" >/dev/null 2>&1 || true; fi
  /bin/rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
if [ ! -x "$driver_filter" ] || [ ! -f "$driver_ppd" ]; then
  major=$(/usr/bin/sw_vers -productVersion | /usr/bin/cut -d. -f1)
  if [ "$major" -lt 11 ] || [ "$major" -gt 27 ]; then
    echo 'Automatic CAPT installation supports macOS 11.7.11-27 only. Check Canon OS compatibility.' >&2
    exit 1
  fi
  if [ "$major" -eq 11 ] && ! /usr/bin/sw_vers -productVersion | /usr/bin/awk -F. '{exit !(($2 > 7) || ($2 == 7 && $3 >= 11))}'; then
    echo 'Canon requires macOS 11.7.11 or newer.' >&2; exit 1
  fi
  echo 'Downloading official Canon CAPT 10.0.10 (43 MB)...'
  echo 'Canon information/license: https://vn.canon/en/support/0101321320'
  /usr/bin/curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 --retry 2 --connect-timeout 15 --max-time 300 \
    https://gdlp01.c-wss.com/gds/3/0100013213/01/mac-capt-v10010-uken.dmg -o "$work/capt.dmg"
  actual=$(/usr/bin/shasum -a 256 "$work/capt.dmg" | /usr/bin/cut -d' ' -f1)
  if [ "$actual" != f116426b93443a420181a15e35e50f3a1cfaa3cb69c1b25ada9a752eb51e2819 ]; then
    echo 'Canon download SHA-256 mismatch. Nothing was installed.' >&2; exit 1
  fi
  /bin/mkdir "$work/mount"
  /usr/bin/hdiutil attach -readonly -nobrowse -mountpoint "$work/mount" "$work/capt.dmg" >/dev/null
  mounted=true
  package="$work/mount/MacOSX/Canon_CAPT_Installer.pkg"
  /usr/sbin/pkgutil --check-signature "$package" > "$work/signature.txt"
  if ! /usr/bin/grep -Fq 'Developer ID Installer: Canon Inc. (XE2XNRRXZ5)' "$work/signature.txt"; then
    echo 'Expected Canon installer identity not found. Nothing was installed.' >&2; exit 1
  fi
  /usr/sbin/spctl --assess --type install "$package"
  echo 'Installing signed Canon CAPT package...'
  /usr/sbin/installer -pkg "$package" -target /
else
  echo 'Canon CAPT already installed; no driver download needed.'
fi
test -x "$driver_filter"
test -f "$driver_ppd"
backend="$source_dir/mac_backend.pl"
if [ ! -f "$backend" ]; then
  backend="$work/mac_backend.pl"
  /usr/bin/curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 --retry 2 --connect-timeout 15 --max-time 60 \
    "$backend_url" -o "$backend"
fi
actual=$(/usr/bin/shasum -a 256 "$backend" | /usr/bin/cut -d' ' -f1)
if [ "$actual" != "$backend_sha" ]; then
  echo 'Backend SHA-256 mismatch. Download the current installer and matching backend again.' >&2; exit 1
fi
/usr/bin/perl "$backend" --self-test
/usr/bin/gzip -dc "$driver_ppd" > "$work/Canon_ESP32.ppd"
/usr/bin/grep -Fq 'Canon LBP3300' "$work/Canon_ESP32.ppd"
/usr/bin/perl -pi -e 's/^\*DefaultInputSlot:.*$/*DefaultInputSlot: Cas1/; s/^\*DefaultPageSize:.*$/*DefaultPageSize: A4/; s/^\*DefaultPageRegion:.*$/*DefaultPageRegion: A4/; s/^\*DefaultCNDuplex:.*$/*DefaultCNDuplex: False/' "$work/Canon_ESP32.ppd"
/usr/bin/install -d -m 755 "$target_dir"
/usr/bin/install -d -m 700 "$cache_dir"
/usr/bin/install -m 644 "$backend" "$target_dir/mac_backend.pl"
/usr/bin/install -m 644 "$work/Canon_ESP32.ppd" "$target_dir/Canon_ESP32.ppd"
printf '#!/bin/sh\nexec /usr/bin/perl %s/mac_backend.pl "$@"\n' "$target_dir" > "$work/esp32capt"
/usr/bin/install -m 700 "$work/esp32capt" /usr/libexec/cups/backend/esp32capt
if [ -z "$current_uri" ]; then
  /usr/sbin/lpadmin -p Canon_LBP3300_ESP32 -E \
    -v esp32capt://192.168.1.199:9100 -P "$target_dir/Canon_ESP32.ppd" \
    -D 'Canon LBP3300 (ESP32 LAN)' -L 'ESP32 192.168.1.199' \
    -o printer-is-shared=false -o printer-error-policy=stop-printer
fi
echo 'Installed: Canon LBP3300 (ESP32 LAN). Select A4, Cassette 1. Duplex: Canon Print Style / Binding Location; layout/orientation: application print dialog.'
