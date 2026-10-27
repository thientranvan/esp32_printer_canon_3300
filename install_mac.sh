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
host=${1:-}
if [ -z "$host" ]; then printf 'ESP IPv4 address: '; IFS= read -r host; fi
/usr/bin/perl -e 'exit !($ARGV[0] =~ /\A(?:[0-9]{1,3}\.){3}[0-9]{1,3}\z/ && !grep { $_ > 255 } split /\./, $ARGV[0])' "$host" || { echo 'Invalid IPv4 address.' >&2; exit 1; }
profile=$(/usr/bin/mktemp)
trap '/bin/rm -f "$profile"' EXIT
/usr/bin/curl --fail --silent --show-error --noproxy '*' --connect-timeout 10 --max-time 15 "http://$host/setup/profile.json" -o "$profile"
id=$(/usr/bin/plutil -extract id raw -o - "$profile")
name=${2:-$(/usr/bin/plutil -extract name raw -o - "$profile")}
/usr/bin/perl -e 'exit !($ARGV[0] =~ /\Aesp32-[0-9a-f]{12}\z/ && $ARGV[0] ne "esp32-000000000000" && length($ARGV[1]) && length($ARGV[1]) <= 192 && $ARGV[1] !~ /[\x00-\x1f\x7f\\\/";]/)' "$id" "$name" || { echo 'Invalid station ID/name.' >&2; exit 1; }
[ "$(/usr/bin/plutil -extract schema raw -o - "$profile")" = 1 ]
[ "$(/usr/bin/plutil -extract model raw -o - "$profile")" = LBP3300 ]
[ "$(/usr/bin/plutil -extract ip raw -o - "$profile")" = "$host" ]
/bin/rm -f "$profile"
trap - EXIT
queue=ESP32_$(printf '%s' "$id" | /usr/bin/tr -d '-')
cache_dir="$cache_dir/$id"
uri="esp32capt://$host:9100/$id"
printf 'Station: %s / %s / %s\n' "$name" "$id" "$host"
driver_filter=/Library/Printers/Canon/CUPSCAPT2/Bins/capdftopdl
driver_ppd=/Library/Printers/PPDs/Contents/Resources/CNMC2LBP3300AUK.ppd.gz
backend_sha=58e9f8f3d5615b429d42667803f96d9e4a21b18698773db9aa090af6e549f7d7
backend_url=https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/94e1dbd528538b48a8064a6cea72c20f8ab4aa3e/mac_backend.pl
jobs=$(/usr/bin/lpstat -W not-completed -o 2>/dev/null | /usr/bin/grep -E '^(ESP32_esp32|Canon_LBP3300_ESP32)-' || true)
if [ -n "$jobs" ] || [ -e "$cache_dir/pending.txt" ]; then
  echo 'Resolve existing/uncertain print jobs before installing; no jobs were deleted.' >&2
  exit 1
fi
current_uri=$(/usr/bin/lpstat -v "$queue" 2>/dev/null || true)
if [ -n "$current_uri" ]; then
  case "$current_uri" in
    *"esp32capt://"*":9100/$id") ;;
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
  /usr/sbin/lpadmin -p "$queue" -E \
    -v "$uri" -P "$target_dir/Canon_ESP32.ppd" \
    -D "$name" -L "ESP32 $host" \
    -o printer-is-shared=false -o printer-error-policy=stop-printer
else
  /usr/sbin/lpadmin -p "$queue" -v "$uri" -D "$name"
fi
label=com.esp32printer.status.$id
plist=/Library/LaunchDaemons/$label.plist
cat > "$work/status.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>Label</key><string>$label</string>
<key>ProgramArguments</key><array><string>/usr/bin/perl</string><string>$target_dir/mac_backend.pl</string><string>--monitor</string><string>$host</string><string>$queue</string></array>
<key>RunAtLoad</key><true/><key>KeepAlive</key><true/><key>ThrottleInterval</key><integer>10</integer>
<key>StandardErrorPath</key><string>$cache_dir/status-monitor.log</string></dict></plist>
EOF
/usr/bin/plutil -lint "$work/status.plist"
/bin/launchctl bootout "system/$label" 2>/dev/null || true
/usr/bin/install -o root -g wheel -m 644 "$work/status.plist" "$plist"
/bin/launchctl bootstrap system "$plist"
echo "Installed: $name ($host). Select A4, Cassette 1. Duplex: Canon Print Style / Binding Location; layout/orientation: application print dialog."
# Desktop monitor runs as the logged-in user, never as the CUPS/root daemon.
status_script="$source_dir/installers/esp32-printer-status-mac.js"
status_sha=23d5c53814bca6de1d50210179c66b51267245232f67a4dfec0267f9211caf4f
if [ ! -f "$status_script" ]; then
  status_script="$work/esp32-printer-status-mac.js"
  /usr/bin/curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 --connect-timeout 15 --max-time 60 \
    https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/94e1dbd528538b48a8064a6cea72c20f8ab4aa3e/installers/esp32-printer-status-mac.js -o "$status_script"
fi
[ "$(/usr/bin/shasum -a 256 "$status_script" | /usr/bin/cut -d' ' -f1)" = "$status_sha" ] || { echo 'Status monitor checksum mismatch.' >&2; exit 1; }
/usr/bin/install -m 644 "$status_script" "$target_dir/status-mac.js"
# A named app bundle gives the Dock its own identity instead of "osascript".
status_app="$target_dir/Status-$id.app"
# Applets do not forward executable argv to the JavaScript run handler.
/usr/bin/osascript -l JavaScript - "$status_script" "$work/status-app.js" "$host" "$queue" "$name" <<'JXA'
ObjC.import('Foundation');
function run(args) {
 var source=ObjC.unwrap($.NSString.stringWithContentsOfFileEncodingError($(args[0]),$.NSUTF8StringEncoding,null));
 if(!source||source.indexOf('function run(argv) {')<0)throw Error('Invalid status source');
 source=source.replace('function run(argv) {','function startStatus(argv) {')+'\nfunction run() {return startStatus('+JSON.stringify(args.slice(2))+');}\n';
 if(!$(source).writeToFileAtomicallyEncodingError($(args[1]),true,$.NSUTF8StringEncoding,null))throw Error('Cannot write configured status app');
}
JXA
/usr/bin/osacompile -s -l JavaScript -o "$status_app" "$work/status-app.js"
/usr/bin/plutil -replace OSAAppletStayOpen -bool true "$status_app/Contents/Info.plist"
/usr/bin/plutil -replace CFBundleName -string "$name — Trạm in ESP32" "$status_app/Contents/Info.plist"
/usr/bin/plutil -replace CFBundleDisplayName -string "$name — Trạm in ESP32" "$status_app/Contents/Info.plist"
/usr/bin/plutil -replace CFBundleIdentifier -string "com.esp32printer.status.$id" "$status_app/Contents/Info.plist"
/usr/bin/codesign --force --sign - "$status_app"
window_label=com.esp32printer.window.$id
window_plist=/Library/LaunchAgents/$window_label.plist
name_xml=$(printf '%s' "$name" | /usr/bin/perl -pe 's/&/&amp;/g;s/</&lt;/g;s/>/&gt;/g')
cat > "$work/window.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>Label</key><string>$window_label</string>
<key>ProgramArguments</key><array><string>$status_app/Contents/MacOS/applet</string></array>
<key>RunAtLoad</key><true/><key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict><key>ThrottleInterval</key><integer>10</integer>
<key>LimitLoadToSessionType</key><string>Aqua</string></dict></plist>
EOF
/usr/bin/plutil -lint "$work/window.plist"
/usr/bin/install -d -m 755 /Library/LaunchAgents
/usr/bin/install -o root -g wheel -m 644 "$work/window.plist" "$window_plist"
console_uid=$(/usr/bin/stat -f %u /dev/console)
if [ "$console_uid" -ne 0 ]; then
  # Remove this monitor's older per-user launch override, if installed during upgrade.
  console_user=$(/usr/bin/stat -f %Su /dev/console)
  console_dir=$(/usr/bin/dscl . -read "/Users/$console_user" NFSHomeDirectory | /usr/bin/sed 's/^NFSHomeDirectory: //')
  if [ -n "$console_dir" ] && [ "$console_dir" != / ]; then
    /bin/rm -f "$console_dir/Library/LaunchAgents/$window_label.plist"
  fi
  /bin/launchctl bootout "gui/$console_uid/$window_label" 2>/dev/null || true
  /bin/launchctl bootstrap "gui/$console_uid" "$window_plist"
fi
echo 'Native status window installed; opens for new print jobs and new printer faults.'
