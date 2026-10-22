# ESP32-S3 Canon LBP3300 LAN print server

Repository: https://github.com/thientranvan/esp32_printer_canon_3300

## Kết nối phần cứng và debug hiện tại — 22/10/2026

- Ưu tiên **cổng COM → Mac** để nạp firmware và đọc log UART 115200 baud trực tiếp.
  Đã đọc được log boot `STATION: PASS` qua `/dev/cu.usbmodem5CF90462221`;
  tên cổng có thể thay đổi, cần kiểm tra lại trước mỗi lần dùng.
- Giữ **cổng USB native → OTG → Canon LBP3300** cho USB Host khi debug qua COM.
  Người dùng xác nhận: sau khi hàn hai mối nối USB–OTG, máy in nối thẳng đã nhận,
  **không cần hub, không cần cấp nguồn riêng cho đường USB/OTG**.
  Chưa ghi nhận chính xác hai điểm hàn/pin; không suy đoán hoặc hướng dẫn hàn theo pin chưa xác minh.
- Chỉ khi không cắm được COM mới quay lại cách thao tác/nạp qua mạng (OTA) và
  bố trí USB/OTG trước đây. Đọc log không chủ động reset bo nếu không cần.
- ESP vẫn cần nguồn nuôi; kết quả trên không có nghĩa máy in cấp nguồn cho ESP.
  Việc nhận thiết bị nối thẳng do người dùng xác nhận, chưa nghiệm thu lại một lượt in
  với sơ đồ mới trong phiên cập nhật tài liệu này.
- Các mục về hub/VBUS bên dưới là lịch sử thử nghiệm trước khi sửa hai mối hàn,
  không phải yêu cầu dùng hub cho bo đã sửa. Bo khác chưa sửa cần kiểm chứng riêng.


## Cài đặt nhanh

- Windows 10/11 x64, kể cả máy trắng: tải [bộ cài đầy đủ PowerShell](installers/esp32-printer-setup-v3.ps1), chạy bằng PowerShell quản trị và dùng IP/tên trạm như mục nhiều trạm bên dưới. Nếu chưa có CAPT, bộ cài tự tải gói Canon chính thức 3.30 x64, xác minh rồi cài driver trước khi tạo máy in LAN. Chọn tên trạm đã cấu hình, ví dụ **Máy 1**, trong ứng dụng.
- macOS: tải riêng [install_mac.sh](install_mac.sh), chạy `sudo sh install_mac.sh`. Không cần Python, clone repo hay có sẵn hàng đợi USB. Thiếu driver sẽ tự tải và cài gói Canon CAPT 10.0.10; thiếu backend sẽ tải bản đã cố định SHA-256 từ GitHub.
- ESP: mở http://192.168.1.199 để xem trạng thái, tải bộ cài và sao chép lệnh hiện tại. Các đường dẫn `/setup/*.ps1` cũ chuyển hướng sang GitHub; firmware không nhúng bộ cài.
- Hiện hỗ trợ A4, cassette 1, một/hai mặt cạnh dài hoặc ngắn, ngang/dọc và nhiều trang trên một mặt qua driver/ứng dụng. Mỗi máy chạy cầu nối riêng; ESP giữ USB, không cần máy trung gian bật cố định.

Chẩn đoán: [script](installers/esp32-printer-diagnostics.ps1) nhận `-StationIP` và `-Token` từ trang ESP đã đăng nhập; token đổi khi ESP khởi động lại. Hỗ trợ từ xa: [script](installers/esp32-printer-remote-v3.ps1) hỏi IP và khóa công khai máy hỗ trợ, chỉ dành cho quản trị có chủ đích; không cần cho việc in.

## Nhiều trạm và in từ web

Mở trang quản trị của từng ESP, mục **Cấu hình trạm in**, đặt tên/IP riêng:

| Tên | IP |
| --- | --- |
| Máy 1 | 192.168.1.199 |
| Máy 2 | 192.168.1.200 |
| Máy 3 | 192.168.1.201 |

ID tự lấy từ MAC, không đổi theo tên/IP. Cấu hình lưu NVS, áp dụng sau reboot.
Mỗi bo mới cần cấu hình riêng qua Wi-Fi AP `ESP32-Printer-<6 ký tự MAC cuối>`
tại `http://192.168.4.1`; đừng đưa cả ba bo mặc định .199 vào cùng LAN trước khi đổi IP.
Gateway/subnet/DNS sửa được; cần dành địa chỉ trên router để tránh trùng DHCP.

Trên từng PC/Mac, mở đúng trang IP của máy in, tải bộ cài và sao chép lệnh ngay trên
trang. Lệnh đã chứa đúng IP; bộ cài đọc ID/tên từ `/setup/profile.json` của ESP đó
trước khi thay đổi máy tính. Có thể chỉ định tên khác khi cài:

```powershell
.\esp32-printer-setup-v3.ps1 -StationIP 192.168.1.200 -PrinterName "Máy 2"
```

```sh
sudo sh install_mac.sh 192.168.1.200 'Máy 2'
```

Không truyền tham số: hỏi IP rồi lấy tên từ trạm. Một máy cài được cả ba máy in.
Windows dùng thư mục/task/mutex/port riêng theo station ID, port loopback 19101..19200;
Mac dùng queue và cache/pending/lock riêng theo ID. Driver Canon dùng chung.
Đổi IP: cài lại cùng ID để cập nhật địa chỉ, không cần sửa firmware/code.
Đổi tên Windows bằng bộ cài sẽ tạo hàng đợi mang tên mới, hàng đợi cũ được giữ nguyên;
Mac giữ queue ID và cập nhật tên hiển thị. Hàng đợi legacy “Canon LBP3300 (ESP32 LAN)”
không tự xóa; vẫn dùng .199, cache/task cũ và cần dọn thủ công sau khi hết việc nếu không dùng nữa.

Firmware đã có client HTTPS chủ động tới `quanly.lophocbinhminh.com`, heartbeat,
claim, tải prepared CAPT <=4 MiB, kiểm tra checksum, gửi USB và journal chống tự in lặp.
Cloud mặc định tắt, cần token riêng mỗi trạm từ server. In LAN vẫn độc lập server.
**Chưa có endpoint/dashboard/worker render trên server; chưa thể in tài liệu từ web thật.**
Dev server triển khai theo [contract đầy đủ](docs/server-contract.md), không cần mở
port router. ESP không chuyển PDF/Word sang CAPT; renderer phía server là bắt buộc.
Đã bàn giao contract tại `/var/www/lop1/docs/esp32-printer-contract.md` qua SSH
`binhminh`; chỉ thêm tài liệu, không sửa ứng dụng/database/dịch vụ server.

Kiểm tra mới: `python3 check_cloud.py` kiểm tra parser, chuẩn hóa capture và mô phỏng
engine C++ thực tế; `python3 check_installers.py` kiểm tra bộ cài. Chưa nghiệm thu
ba bo đồng thời hay cloud đầu-cuối. Bo .199 đã qua kiểm tra profile ID phần cứng,
lệnh cài gắn IP, JavaScript, từ chối cấu hình sai/CSRF, lưu cấu hình/reboot và TCP/HTTP
độc quyền với máy in thật; bộ cài nhiều trạm chưa chạy lại trên Windows vì SSH PC đang tắt.
Trạng thái hỗ trợ USB/phản hồi/phiên LAN/cloud,
ảnh in đã hoàn tất/tổng; chưa giải mã đầy đủ cảm biến kẹt giấy/toner của CAPT.

## Bộ cài đầy đủ, kể cả máy chưa có driver

ESP chỉ hiển thị liên kết GitHub và lệnh sao chép. Driver không nằm trong firmware
hay repo; tải trực tiếp từ Canon bằng HTTPS. Cần Internet và quyền quản trị.
Nếu driver đã có đủ, bỏ qua tải/cài driver; các hàng đợi đang dùng được giữ lại.
Bộ cài dừng khi hàng đợi ESP còn việc hoặc có pending marker, không tự xóa/in lại.

| Hệ điều hành | Driver tải tự động | Nguồn Canon | SHA-256 |
| --- | --- | --- | --- |
| Windows x64 | CAPT R1.50 / 3.30, 12 MB | [Canon LBP3300](https://vn.canon/en/support/0100276401) | `480956b6175404f4b330f70f258b3c208a60dfacdf313ec2507cb87ed604bcfd` |
| macOS 11.7.11–27 | CAPT 10.0.10, 43 MB | [Canon CAPT Mac](https://vn.canon/en/support/0101321320) | `f116426b93443a420181a15e35e50f3a1cfaa3cb69c1b25ada9a752eb51e2819` |

Windows kiểm tra SHA-256 và chữ ký Authenticode Canon của EXE rồi giải nén như ZIP,
**không chạy EXE tự giải nén**. `pnputil /add-driver` xác minh/stage INF và catalog
vào Driver Store; `Add-PrinterDriver` đăng ký Canon từ INF trong store. Sau đó mới
đăng ký alias renderer không language monitor, biên dịch cầu nối và tạo máy in LAN.
Không cần cắm máy in USB vào PC, không dùng Windows Update hoặc tắt kiểm tra chữ ký.
Driver Canon chính thức liệt kê Windows 10 x64; đường cài cũng nhắm Windows 11 x64
nhưng chưa có máy Windows 11 để xác nhận. Windows ARM64/32-bit chưa hỗ trợ.

Mac kiểm tra SHA-256 DMG, chữ ký **Canon Inc. (XE2XNRRXZ5)** và Gatekeeper trước khi
chạy Installer. PPD lấy trực tiếp từ bộ driver, không lấy từ máy in USB đã cấu hình.
Runtime/PPD đã có sẽ được giữ nguyên, không ép nâng cấp driver đang hoạt động.
Backend tải theo commit cố định và SHA-256; script chạy được chỉ với công cụ macOS
có sẵn. Khi cập nhật backend, phải cập nhật pin trong `install_mac.sh` (kiểm tra sẽ
báo lỗi nếu lệch). Hàng đợi ESP đã có giữ nguyên tùy chọn mặc định khi cài lại.

Đã tải/xác minh cả hai gói Canon. Nhánh chuẩn bị Windows đã giải nén, gọi PnPUtil
và đăng ký driver thành công trên PC hiện tại; bộ cài đầy đủ được chạy lại trên máy
đã có driver. Gói Mac đã xác minh chữ ký/notarization và chứa PPD LBP3300. Chưa có VM
Windows/Mac trắng để kiểm tra đầu-cuối nhánh cài mới; không xóa driver đang hoạt động
của người dùng để giả lập. Kiểm tra: `python3 check_installers.py`; thêm thư mục chứa
hai gói tải Canon làm đối số để kiểm tra checksum thực, ví dụ `python3 check_installers.py /tmp`.

## Tùy chọn in đã triển khai

Windows giữ renderer Canon gốc và các hộp thoại của nó. Chọn **Printing Preferences / Printer Properties** để chọn một/hai mặt, cạnh lật, ngang/dọc; dàn nhiều trang trên một mặt trong Word/Chrome hoặc Page Layout của driver. Không chọn đồng thời N-up ở cả ứng dụng và driver nếu không muốn thu nhỏ hai lần.

Mac: trong mục Canon chọn **Print Style / CNDuplex** và **Binding Location / BindEdge**; Pages per Sheet và Orientation ở hộp thoại in. Ví dụ CUPS: `lp -d Canon_LBP3300_ESP32 -o CNDuplex=True -o BindEdge=Left document.pdf` (cạnh dài), `BindEdge=Top` (cạnh ngắn). Backend từ chối `sides=two-sided-*` đơn lẻ để tránh driver chưa xoay mặt sau; dùng tùy chọn Canon tương ứng. N-up và chiều trang do renderer dựng vào raster, không do ESP bố trí lại.

Kết quả máy thật, 2026-10-22:

- Windows long-edge: ba trang, hai tờ; người dùng xác nhận trang 1–2 trên hai mặt đúng thứ tự/chiều, trang 3 ở tờ riêng.
- Windows short-edge: ba trang, hai tờ; người dùng xác nhận lật cạnh ngắn đúng chiều.
- Mac: PDF bốn trang do bộ lọc hệ điều hành dựng, `number-up=2 landscape CNDuplex=True BindEdge=Top`; một tờ hai mặt, mỗi mặt hai trang thu nhỏ, chữ đầy đủ và chiều đúng, người dùng xác nhận. Backend mới đã cài trên Mac này.
- Một PDF thử viết tay trước đó bị Canon renderer in thành dấu chấm dù xem trước có chữ. Thay bằng PDF do hệ điều hành tạo thì in đầy đủ với cùng backend. Đây chưa phải bằng chứng mọi PDF đầu vào đều tương thích; đường nhận tài liệu trực tiếp từ VPS vẫn cần renderer/chuẩn hóa PDF.

Phát hiện giao thức: header Windows full offset 12 là collation, 16 là duplex (bản cũ đã đọc nhầm). D0A0 full byte 9 bit `0x08` là hướng raster, không phải cờ duplex. Duplex dùng các bit `0x06`: `0x04` cho ảnh mặt sau gửi trước, `0x02` cho ảnh mặt trước; trang lẻ cuối gửi một mặt. Xếp bản không collate lặp theo cặp mặt của từng tờ, không lặp từng ảnh làm hỏng cặp. Raster đã giữ phép xoay cho cạnh dài/ngắn từ driver. Bộ cài cập nhật không ép mặc định của hàng đợi đang có về một mặt nữa.

Giới hạn còn lại: A4/plain/cassette 1, 100 trang nội dung, 20 MiB, 99 bản. Chưa xác minh khổ giấy/khay/media khác, booklet, watermark hoặc mọi mục nâng cao Canon. Không tuyên bố tương đương toàn bộ driver gốc.

## Build từ repo

Sao chép `station/device_secret.example.h` thành `station/device_secret.h`, đặt mật khẩu riêng (ít nhất 12 ký tự). Không commit file này. Wi-Fi được cấu hình trên thiết bị; không nằm trong repo. Dùng cấu hình Arduino ESP32-S3 và linker flags ghi bên dưới. Không đưa backups, khóa SSH, mật khẩu hoặc firmware chứa mật khẩu lên GitHub.

Bộ cài trên nhánh `main` nhận bản cập nhật mới; có thể dùng URL raw theo commit để cố định phiên bản. In vật lý đã xác nhận trên Mac và Windows qua hàng đợi hệ điều hành. Người dùng đã in ba trang từ Word; các tùy chọn mới được kiểm tra như ghi bên dưới.

---

# ESP32-S3 printer research

Board: ESP32-S3, 16 MiB quad flash, 8 MiB OPI PSRAM.

## Current result — 2026-10-22

**Physical LAN printing confirmed by user.** One generated A4 page traveled from
Mac over authenticated LAN HTTP to ESP32-S3, through USB host + powered hub to
Canon LBP3300, and came out as the test sheet. The corrected fresh-job sequence
selects cassette 1 with E1A9 and patches D0A0 source before raster delivery.
The earlier failed experiment and misleading counter result are documented below.

Native OS printing is installed and physically confirmed on this Mac and the
Windows test PC. Each client uses the installed Canon renderer plus a local
backend/bridge; ESP32-S3 remains the shared physical USB server. No permanently-on
Mac/Windows intermediary is required. OS spoolers queue jobs; ESP serializes USB
sessions. Windows additionally retains a captured job while executing it. Durable
ESP job storage and standalone ESP CAPT job execution for a future VPS are not
implemented. Earlier sections below record earlier firmware milestones.

`capt_client.py` uses stdlib HTTP with preemptive Basic authentication and CSRF.
Credentials stay in ignored `station/device_secret.h`. Firmware provides authenticated
`POST /printer/command` (one framed hex CAPT command or bulk read) and
`GET /printer/command` (poll result). One operation at a time, max 4096 bytes,
5-second USB deadline with halt/flush before releasing transfers. This is an admin
diagnostic interface, not a concurrent job queue. Do not run multiple clients.

```sh
python capt_client.py                 # identity/status and HTTP boundary checks
python print_test.py --self-test      # parser and cassette request assertions
python print_test.py                  # generate/validate only, no printing
python print_test.py --send           # one A4 page, cassette 1
python print_test.py --finish         # inspect/close completed pending job, no resend
```

Use a Python installation that runs locally; this Mac's working interpreter is
`/Users/tvt/.espressif/python_env/idf6.1_py3.13_env/bin/python`.
The ignored `.print-test-pending.json` prevents blind resends after uncertain results.
Keep it after failures; `--finish` does not recover an expired printer reservation.

Important protocol findings from the installed LBP3300 monitor
`/Library/Printers/Canon/CUPSCAPT2/Bidi/captmoncnab5`:

- The filter wrapper's `8200` block has an 8-byte header; its 32-bit length includes
  that header. USB uses `C0A0` with just the compressed payload. `D0A9` groups all
  `D0` commands, including `D0A2`. Model key is `94 2a`.
- Raw filter output selected source 0. The test stalled at `(1, 0, 0, 0)` despite
  A4 in cassette 1. `A0A1` showed cassette 1 present with A4.
- `E1A9` payload `02 01 b0 09 b3 0d 01 00` selects A4, media 1,
  2480×3507, cassette 1; reply `00 01` confirms selected source. Native backend
  then patches full `D0A0` packet byte `0x0A` with that source. Test script now does both.
- After a long pause, selection/clear commands returned `88 00`; reserving again
  with `A2A0` client 0 returned job 2. Supplying old job ID 1 as client was rejected
  (`90 00 00 00`). Re-reserving, selecting cassette 1, clearing error (`E0A2`),
  and going online (`E0A5`, page 1) changed counters to `(1, 1, 1, 1)` **without resending**, but produced no physical sheet.
  Pending ownership was updated to job 2 before final release.

Firmware build/OTA, real bidirectional CAPT, HTTP boundary/CSRF checks and parser
self-check passed. The corrected fresh-job cassette path has parser checks and
hardware-verified selection, and was subsequently exercised as a full print, confirmed physically by the user.
Protocol was observed from local binaries; no Canon driver source was copied.

## Diagnostic firmware

Uses installed Arduino ESP32 core 3.3.11. No printer or Wi-Fi credentials required.

```sh
export PATH=/Users/tvt/.espressif/python_env/idf6.1_py3.13_env/bin:$PATH
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB --build-property tools.ctags.cmd.path=/usr/bin/true --build-path /tmp/esp32-printer-diagnostic-build diagnostic
arduino-cli upload --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB --port /dev/cu.usbmodem101 --input-dir /tmp/esp32-printer-diagnostic-build diagnostic
```

The ctags override skips automatic function prototypes; this sketch defines functions before use.

Serial: 115200 baud. Checks flash/PSRAM sizes, a 64 KiB PSRAM write/read pattern,
Wi-Fi initialization and network scan. Prints PASS/FAIL and a heartbeat every 3 seconds.
Does not connect to a network or print network names.

Verified RGB pin: GPIO48. Brightness: 2/255, 100 ms every 3 seconds.
Green: PASS. Red: FAIL. Separate power LEDs cannot be dimmed by this firmware.

USB remains Serial/JTAG; printer USB Host is not enabled yet.

Original full 16 MiB flash backup: `backups/original-98c377a136b0.bin`.

## Wi-Fi station firmware

Wi-Fi v2 uses static LAN IP `192.168.1.199`, subnet `255.255.255.0`,
gateway/DNS `192.168.1.1`, verified against the current Mac LAN settings.
Exclude `.199` from the router DHCP pool or reserve it for the ESP station MAC
to prevent another client receiving the same address. These fixed settings apply
to the current LAN; the recovery AP remains at `192.168.4.1` on other networks.
Status displays connected SSID, RSSI and static IP; SSIDs are JSON escaped and
rendered with `textContent`.

`station/station.ino` provides an authenticated status/configuration page and HTTP firmware upload.
AP: `ESP32-Printer-36B0`, address `http://192.168.4.1`, username `admin`.
The random device password is in ignored local file `station/device_secret.h`;
it is also the WPA2 AP password. Preserve this header for future builds.
No router password is stored in source files or printed to serial.

Build the current station firmware (the enumeration wrapper flags are required):

```sh
export PATH=/Users/tvt/.espressif/python_env/idf6.1_py3.13_env/bin:$PATH
/opt/homebrew/bin/arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB \
  --build-property tools.ctags.cmd.path=/usr/bin/true \
  --build-property compiler.c.elf.extra_flags=-Wl,--wrap=enum_install,--wrap=enum_process \
  --build-path /tmp/esp32-printer-station-build station
```
Upload the generated **station.ino.bin** through the page; do not upload a merged,
bootloader or partition image. Future firmware must retain this OTA partition layout.

The 2.4 GHz connection is attempted for 30 seconds. Credentials are stored as one NVS
blob only after connection; failed connections restore saved credentials. The protected
AP remains available for recovery. Network changes may briefly disconnect AP clients.
LED remains a dim green heartbeat on healthy hardware, even before router connection.

Configuration and OTA use HTTP, Digest authentication and per-boot CSRF tokens.
Configure through the password-protected AP or a trusted LAN; do not expose port 80
to the Internet. Firmware signature verification is not enabled.
In v1/v2 USB remains Serial/JTAG. In v3 the native USB port switches to USB Host;
use COM/UART0 for flashing and serial debugging; Wi-Fi configuration/OTA is the fallback when COM cannot be connected. Do not use the native Host port as a serial console.

### USB monitor (v3)

`station/usb_monitor.h` installs the ESP-IDF USB Host library on the internal PHY.
It reads device/configuration descriptors and reports VID/PID, interfaces and endpoints
on the authenticated status page. It handles attachment/removal but does not claim
interfaces or send print data in v3. Current firmware selects one Canon LBP3300
behind the powered hub and claims interface 0 for a read-only Printer Class probe.
No board-specific VBUS GPIO is guessed: physical 5 V on the OTG port still needs
verification if no device enumerates. Power ESP from the separate 5 V supply while
its native USB port connects through OTG to the powered-on printer.

### USB diagnostics (v4)

Build station firmware with `CDCOnBoot=default` instead of `CDCOnBoot=cdc`.
Serial output uses UART0, leaving native USB exclusively for USB Host.
Library events run in a dedicated daemon task and client events in a separate task,
following the ESP-IDF USB Host example. The status endpoint reports root port
host mode, physical attachment, controller power command, device count and a
read-only HPRT snapshot. The power command does not measure physical VBUS voltage.
The enum filter records the last descriptor VID/PID/class and count, including hubs.
Daemon stack is 8 KiB. `/status` reports `reset_reason` using ESP-IDF reset reason
values. After a panic or watchdog reset, USB Host stays off so Wi-Fi OTA remains
available. Per user request, USB Host starts again after a brownout reset; hardware
brownout detection remains enabled. A new OTA software restart or power cycle
retries USB Host after panic/watchdog recovery.

### Captured hub crash

With hub `214B:7260`, a crash was captured in task `usb-daemon`:
`abort()` in ESP-IDF 5.5.5 USB `control_request_string` / `enum_process`.
The matching app ELF SHA256 starts with `57c67691f`. Original ELF, app image,
raw core dump and summary are preserved locally in ignored `backups/crash-v4/`.
The subsequent RTC trace below identifies the stale IDLE notification as the trigger;
the original optimized backtrace alone did not establish it.

Authenticated `GET /crash` returns the summary; `GET /crash.bin` returns the
validated raw core dump. USB Host pauses after panic/watchdog resets; a saved dump
does not prevent automatic startup after a normal restart. The original crash
has been backed up and the crash partition has not been erased.
These dumps may contain memory data; keep them private.

The authenticated page includes **Bật USB Host**. `POST /usb/start` requires the
per-boot CSRF token and starts Host once per boot without erasing the saved dump.
Repeated requests while started do not install a second USB Host instance.
An unresolved USB panic may reboot the board back into recovery.

### IDF 5.5.5 enumeration guard

RTC stage tracing reproduced `... 31 32 33 0`: enumeration completes, then a stale
notification calls `enum_process()` at IDLE, which aborts in this SDK.
`usb_trace.cpp` wraps `enum_install` and `enum_process`; it skips only IDLE calls
and delegates every active stage unchanged. Private stage offset `16` was checked
against the matching ELF debug types. Build is pinned to ESP-IDF 5.5.5 and requires:

```sh
--build-property compiler.c.elf.extra_flags=-Wl,--wrap=enum_install,--wrap=enum_process
```

Recheck the private layout or remove this workaround after an upstream SDK fix.

Hardware verification after the guard: Canon LBP3300 `04A9:267E` enumerates behind
the powered hub, interface 0 class `07`, subclass `01`, protocol `02`, bulk OUT
`0x01` and bulk IN `0x82`, both max packet size 64. Printing is not implemented yet.
Run `python check_station.py http://192.168.1.199 --usb` to also check printer
identity and uptime stability.

Run `python check_station.py http://192.168.4.1` after joining the AP to check
authentication, hardware status, page serving and CSRF rejection without changing settings.

## Printing progress (2026-10-22)

Validated topology: ESP native **USB** port → powered USB hub `214B:7260` →
powered-on Canon LBP3300. Supply ESP separately through 5V/GND.
Before the USB–OTG solder repair, direct OTG had not enumerated; missing VBUS
was an unmeasured hypothesis. After repair, the user confirmed direct enumeration
without a hub or separate USB power; see current wiring above. COM is USB–UART, not the printer Host port. Hub detection and the
SDK panic were separate problems from brownout (`reset_reason=9`). Never disable
the brownout detector to mask power problems.

Current firmware automatically sends standard read-only USB Printer Class
GET_DEVICE_ID (`A1 00 00 00 00 00 00 04`) after claiming interface 0 on `04A9:267E`.
Its response is shown in USB details. This proves control communication only;
it does not prove CAPT bulk traffic or a printed page. Transfers remain owned by
the USB task until completion, including unplug. SDK transfer timeout_ms is not
supported: an unanswered probe may remain pending until unplug/reboot; Wi-Fi/OTA
continue running. No automatic repeated probes or print jobs are sent.

The Mac already has Canon's ARM-compatible CAPT filter:
`/Library/Printers/Canon/CUPSCAPT2/Bins/capdftopdl`.
Generate the installed printer queue's final filter output with:

```sh
/usr/sbin/cupsfilter -e -p /etc/cups/ppd/Canon_LBP3300_1.ppd \
  -m printer/Canon_LBP3300_1 /path/to/one-page.pdf > /tmp/canon-job.bin
```

`-e` matters: without it, the observed output was PostScript. A one-page test
produced 28,269 bytes starting `dBMpM` with job metadata and embedded CAPT packets.
**Do not send this entire wrapper directly to USB.** The backend framing and
LBP3300 bidirectional initialization still need to be established. PDF, PCL and
PostScript are not established native inputs for this printer.

Reference research: https://github.com/mounaiban/captdriver (GPLv3).
Its command framing is little-endian 16-bit command + 16-bit total length;
its implemented LBP2900 sequence is not validated for LBP3300. No source from
that driver has been copied into this firmware. Actual page printing is pending.

For tooling, preemptive Basic auth over the trusted LAN avoids issuing a fresh
Digest challenge that invalidates the browser's current nonce. Keep credentials
in ignored `station/device_secret.h`; do not include them in scripts or docs.

Verified on hardware after OTA: `GET_DEVICE_ID` returned
`MFG:Canon;MDL:LBP3300;CMD:CAPT;VER:2.1;CLS:PRINTER;DES:Canon LBP3300`.
Run `python check_station.py http://192.168.1.199 --probe` to assert this response
and check Host uptime for 10 seconds. This full check creates an authentication
challenge and can require the browser to authenticate again.

### Hub discovery after power cycling

Observed Host active, physical link true, four enumerated USB devices, error 0,
but no selected printer. `NEW_DEV` callbacks stored only one address: another
hub child could overwrite the LBP3300 notification before the client opened it.
The monitor now revisits the installed Host address list every 500 ms while no
printer is open, rotating through addresses until `04A9:267E` is selected.
Descriptor failures close the temporary device handle so discovery can retry.
Use `check_station.py ... --probe` to verify discovery and the actual printer ID.
This software issue is distinct from physical_link=false / devices=0 and from
the separately observed brownout reset (9).

After deploying the address-list retry, Host remained stable for 40 seconds,
physical_link=true and devices=4, but LBP3300 was still not selected and no
new enumeration descriptors appeared after the user reported switching it on.
The event-order bug is fixed in code, but is not established as the cause of
this particular missing-printer observation; recovery is not yet verified.

Subsequent user report and authenticated status confirmed recovery: Canon
`04A9:267E` at USB address 7, interface 0, bulk OUT 0x01 / IN 0x82;
GET_DEVICE_ID returned `MFG:Canon;MDL:LBP3300;CMD:CAPT;VER:2.1` again.
Enumeration count had increased to 8. Latest boot reason was brownout (9),
so this observation does not prove a purely software or purely power cause.

### Correction after physical verification

User confirmed no sheet emerged. Do not report printing success from these counters.
With printer reportedly switched off, ESP still showed its enumerated USB descriptor
and four hub devices; a fresh A1A1 bulk operation failed with ESP_FAIL (-1).
USB enumeration and cached GET_DEVICE_ID do not establish printer power/readiness.
Whether the printer USB circuitry remains powered or the hub disconnect state is stale
is not established. UI now explicitly distinguishes USB identity from readiness.

### Printer power-cycle discovery

After user powered Canon back on, ESP uptime 267 s still showed no selected
printer, 3 devices and no Canon enumeration. Restarting ESP with the same firmware
restored 4 devices, Canon 04A9:267E at address 3 and successful GET_DEVICE_ID.
Fresh A1A1/A0A8 bulk responses also succeeded. This confirms recovery by ESP restart;
it does not establish the root cause of missed hub hot-plug events.

### Automatic USB liveness and recovery

Firmware now polls Printer Class GET_DEVICE_ID on endpoint 0 every 3 s. A valid
IEEE 1284 length is required; a descriptor cached at enumeration is not liveness.
GET_PORT_STATUS was tried on this LBP3300 and failed, so it is not used.
The control endpoint does not consume or interleave CAPT bulk replies. CAPT requests
wait for a pending control probe, bounded by the 5 s probe deadline.

`/status.usb` distinguishes `connected` (enumerated), `responsive` (recent live USB
response), `recovering`, `response_age_ms`, `heartbeat_error` and `recoveries`.
Responses expire after 8 s; failed probes/disconnects invalidate liveness sooner.
These fields do **not** claim that the printer engine is powered/ready or a sheet printed.

When absent, failed or stuck, the USB task cycles root-port power through the native
`usb_host_lib_set_root_port_power()` API, at most once per 30 s, off for 1 s.
This resets the controller connection, not necessarily physical 5 V from the hub.
Wi-Fi/HTTP stay running. Recovery waits for active bulk transfers and atomically
excludes new submissions during the reset. In-flight control transfers remain allocated
until their completion/disconnect callback. No print data is retried.
Device discovery still scans the address list every 500 ms. A real disconnect is
published immediately even if transfer cleanup is pending.

Validation: build/OTA passed; `python check_usb_recovery.py` passed 12 s of live
heartbeat/HTTP invariants; `python capt_client.py` passed HTTP boundary checks and
real CAPT requests with background control polling. During the GET_PORT_STATUS
experiment, automatic root recovery re-enumerated all four devices while uptime
continued (no ESP restart). Final-firmware physical printer power-cycle test pending.
Run `python check_usb_recovery.py --cycle` while printer responds, then turn printer
off for 15 s and back on; it requires loss/reconnection with HTTP available and no reboot.

### Successful physical test — corrected cassette setup

After the printer power cycle, A0A8 showed all four counters zero; the earlier local
failed-test record (already released) was archived under ignored `backups/`.
`python print_test.py --send` then sent exactly one fresh page. E1A9 returned
`00 01` before D0A9; D0A0 source byte was set to 1 before sending the 28 raster
blocks / 28,220 compressed bytes. No page data was resent.

Observed counters progressed `(1,0,0,0)` → `(1,1,0,0)` → `(1,1,1,0)` → `(1,1,1,1)`
over multiple seconds, unlike the earlier artificial jump after GoOnline(page=1).
E1A1 stage 4 and E0A9 both acknowledged; local pending marker was removed.
User explicitly confirmed: “Có, đã ra trang thử”. This is the first physically
confirmed print through ESP32-S3 in this project. Firmware liveness polling remained
active throughout. No new firmware flash was required for this successful test.

Working prototype command: `python print_test.py --send` (Mac with installed Canon
LBP3300 driver; A4 paper in cassette 1; printer and ESP on the current LAN).
Only this one-page test is supported by the script; it is not yet general PDF upload
or a shared print queue. Physical verification remains necessary for uncertain jobs.

## LAN printer integration — current deployment

Requirement: printing from Word, Chrome and PDF dialogs on Windows 10/11 and macOS,
without a permanently running computer. Each sending computer runs its own Canon
CAPT driver/filter. ESP owns physical USB and accepts bidirectional CAPT on TCP 9100.
No Mac shared-printer service or central rasterization server is needed.

### Firmware

`station/capt_tcp.h` accepts one active LAN session, preserving full CAPT framing
and replies. Large OUT frames (up to 65535 bytes) are streamed in <=4096-byte USB
transfers. A0/A1/A2/A3/E0/E1 commands receive framed replies; C0/D0 data commands do
not. Unexpected packet namespaces are rejected before forwarding. Socket I/O stops
on failure; print data is not retried by ESP. Idle TCP sessions close after 15 s.
Only clients on the station subnet are accepted; recovery-AP clients are excluded.
TCP is conventional unauthenticated LAN printer access; HTTP admin stays authenticated.

HTTP diagnostic requests now require a numeric `session` (2..UINT32_MAX). Firmware
leases the command channel to that client for 30 s, refreshed per request; another
HTTP client or TCP session cannot interleave commands. Authenticated POST
`/printer/release` releases an idle client's lease. Updated `capt_client.py` handles
session IDs and `.close()`. TCP uses an exclusive owner until connection closes.
`/status.tcp` exposes state, command count, last header and last USB/protocol error.

Backpressure/queues currently live in OS spoolers and the TCP listener backlog,
not on ESP flash. The earlier proposed 10 MiB flash spool is NOT implemented.
Firmware should gain a durable prepared-job queue for future VPS submissions.
Native-driver TCP connections alone do not convert PDF to CAPT.

### macOS: installed and CUPS test completed

Run `sudo sh install_mac.sh`, either standalone or from this checkout. Missing Canon
CAPT runtime/PPD is downloaded and installed automatically as described above.
Installer copies the verified `mac_backend.pl` into `/Library/Printers/ESP32Printer`,
registers root-only backend `/usr/libexec/cups/backend/esp32capt`, and creates the
non-shared `Canon_LBP3300_ESP32` queue, displayed as **Canon LBP3300 (ESP32 LAN)**.
Updating an already installed runtime leaves existing USB queues and the default printer unchanged.

The runtime uses Apple's system `/usr/bin/perl`, no Python helper service. CUPS sandbox
rejected Homebrew Python execution; the system interpreter works without disabling
sandboxing. Closing unused backend descriptors 3/4 lets the Canon filter fall back
to PPD defaults instead of waiting forever for its proprietary backchannel.

Backend validates ALL pages/model/A4/framing before connecting, supports up to
100 pages / 20 MiB / 99 copies, and selects cassette 1 per page. Supports A4
simplex/duplex, keeps one CAPT session for the document and uses the same engine-start
submission window as Windows. Duplex sheet images are sent back before front.
A local exclusive lock serializes backend instances. Before printer mutation,
`/Library/Caches/ESP32Printer/pending.txt` is created; errors keep it and stop the
CUPS queue (exit 4), preventing automatic retries of uncertain print data.
Inspect printer/output and resolve that marker before manually resuming a failed job.
Markers and local spool/cache are NOT published over LAN.

CUPS job 233, a two-page PDF, completed via the installed filter/backend and TCP USB
bridge: PAGE 1 at 09:24:15, PAGE 2 at 09:24:28; filter/backend exited successfully,
queue idle afterward. User confirmed both physical sheets: “Có đủ hai tờ”.
Temporary CUPS debug logging was restored to its original disabled setting.

### Windows: native queue and per-client bridge physically confirmed

**Continuous multi-page printing (2026-10-22):** Windows now keeps one TCP
connection and one CAPT reservation for the entire document, including copies.
Initialization/discard runs once; E1A1 stages 2/6 and E0A7 run for each decoded
page; stage 4 and E0A9 run only after the final page has completed.

The submission window allows two decoded pages ahead of the engine's
`page_printing` counter (A0A8 payload offset 16). It does **not** wait for
`page_completed` (offset 20) before submitting page three. An earlier window
based on completed sheets made sheets 1–2 continuous but introduced a pause
before sheet 3, confirmed by user. Final completion still checks output count.

Three numbered native Windows queue test pages completed in 17.28 seconds,
versus 23.65 seconds with the conservative output-based window. User confirmed
all three final test sheets came out consecutively. These are short-job timings,
not a rated pages-per-minute benchmark or a measured comparison with direct USB.
The Mac backend now uses this job session/window too; its physical duplex/N-up
validation is described below.

Self-test simulates delayed paper exit, requires page three before first output,
checks one reservation/release and verifies collated/uncollated copy order.
`C:\ProgramData\ESP32Printer\last-job.txt` retains only the last successful
job's raster/submission/completion timings. `status.txt` reports total duration.
The pending marker now covers the whole document; uncertain failure preserves
the capture and progress, stops, and never automatically resends the document.
Existing Windows test PC was upgraded remotely; other PCs rerun the latest
[installer](installers/esp32-printer-setup-v3.ps1) from the ESP website with an
empty queue. No firmware update or new printer selection is required.


Open `http://192.168.1.199`, authenticate, download **Windows setup v3**, run in
64-bit administrator PowerShell. Missing official Canon LBP3300 x64 driver is
downloaded, verified and installed automatically. The page includes a file-picker command to avoid Downloads
path/duplicate-filename errors. No machine-wide ExecutionPolicy change, Windows
Update, OpenSSH, Python, or always-on intermediary is needed for printing.

Installer source is `installers/esp32-printer-setup-v3.ps1` (one PowerShell script,
including C# source). It uses native `AddPrinterDriverExW` to register a separate
**Canon LBP3300 ESP32 Render** driver using the original Canon files but **without
CNAB5 Monitor**. The official USB driver/queues remain intact. It compiles the
stdlib .NET bridge with built-in `Add-Type`, validates its self-test, creates task
`ESP32CaptBridge` (SYSTEM, starts with Windows), and adds/updates queue
**Canon LBP3300 (ESP32 LAN)** on Standard TCP/IP port `ESP32_LOCAL_19100` pointing
to **127.0.0.1:19100**, not directly to ESP. (Legacy layout; new per-station installers use ports 19101..19200.) Listener is loopback-only. The bridge
sends parsed CAPT to ESP `192.168.1.199:9100`; ESP arbitrates all clients.

Support: A4, simplex/long-edge duplex/short-edge duplex, cassette 1, up to 100 pages/20 MiB, 1..99 collated or
uncollated copies. Windows wrapper starts with command 1, 40-byte total length,
magic `FADEDACE`; copies at full offset 8 (real two-copy capture confirmed),
collation boolean at 12, duplex mode at 16 (1 simplex, 2 long edge, 3 short edge); command 2 specifies DMPAPER_A4=9. Raster
uses the same 8200/D0 packets as Mac. Monitorless Windows D0A0 leaves paper/media
zero; after validating A4 metadata and 2480x3507 raster geometry the bridge sets
A4=2/plain=1, selects cassette with E1A9 and applies its source to D0A0.

`C:\ProgramData\ESP32Printer` holds bridge.exe, source, status.txt and any
unresolved job. SYSTEM/Administrators only. Input is size/sequence/model checked
before printer mutation. Captures are flushed to disk. A pending marker is saved
before mutating commands; an uncertain page stops the bridge without automatic
resend, preserving the capture and submission/completion progress. Completed captures
are removed. Safe pre-mutation connection failures retry; unresolved captures on
startup require inspection. This is not a durable ESP/VPS queue. To disable the
client bridge: `Stop-ScheduledTask -TaskName ESP32CaptBridge` then
`Disable-ScheduledTask -TaskName ESP32CaptBridge` (administrator PowerShell).

Validation: original driver successfully rendered a 26,544-byte capture after
language-monitor removal. That Windows raster physically printed through ESP
(user confirmed “ESP32 WINDOWS RENDER TEST”). Then the native Windows queue,
local bridge and ESP printed **ESP32 WINDOWS LAN BRIDGE TEST**, also physically
confirmed by user. Native .NET parser self-test covers A4 normalization,
multipage, malformed/truncated input, invalid duplex modes and duplex sheet ordering; real two-copy capture parses
as 1 page, 2 copies. Installer rerun succeeded with an empty queue and bridge
listening again. User subsequently printed three pages from Word through this queue.
Automated COM Word layout capture was unavailable in the remote session; do not
claim that every application/driver finishing option has been verified.

Earlier v1/v2 experiments: invalid `Add-PrinterPort -SNMP 0` prevented port
creation (0x80070057); omitting it fixed queue creation. But the real Canon
language monitor rejected direct Standard TCP/IP with **Incorrect Port** before
sending any TCP commands. The v3 monitorless renderer plus local bridge replaces
that incompatible direct path.

`/setup/windows-diagnostics.ps1` collects read-only driver, queue, port and monitor
metadata, saves `C:\esp32-printer-diagnostics.json`, and uploads it to ESP RAM.
Download requires administrator web authentication; upload uses a boot-scoped
CSRF capability embedded in that download. `/support/windows` requires admin
authentication to read the report. Upload is limited to 60,000 bytes; reboot loses
the report and invalidates previously downloaded script tokens. No printer
settings or jobs are changed by this diagnostic script.

User-authorized remote Windows repair: `/setup/windows-remote.ps1` downloads
Microsoft's portable OpenSSH x64 ZIP from the official PowerShell/Win32-OpenSSH
GitHub release `10.4.0.0p1-Preview`, verifies pinned SHA256
`b67ff8becd203502190ec42ac7787c24c2cd3325ed7700485fe7a1c83d2a7957`
before extraction/execution, and runs a dedicated SSH
endpoint on TCP 22222 as SYSTEM via task `ESP32PrinterSupport`. It allows only the
installing administrator, public-key authentication and Mac `192.168.1.194`
(firewall plus authorized-key source restriction); forwarding is disabled.
Existing SSH service/configuration stays unchanged. Private key is only on Mac
at `~/.ssh/esp32_printer_windows`; never include it in firmware or reports.
Connection IP/user/host public key are uploaded to the authenticated support
report (replacing the previous report; original diagnostics saved locally on Mac).
Endpoint persists across reboot. Web page includes commands to stop/disable the
task and remove its firewall rule. No router port forwarding or security-policy
disable is needed. This is repair access, not a required print intermediary.
The first capability-based installer stalled because this PC blocks Windows
Update. Portable v2 uses no capability/DISM/Windows Update commands and leaves
update-blocking settings unchanged. Stop the waiting old script with Ctrl+C,
download the current script, then run via the web page's file picker. ZIP download
has a 120-second timeout and verified cached ZIPs can be reused. Download was
independently checked on Mac against the release API digest (6,021,393 bytes).
First portable run downloaded/extracted successfully but sshd refused its host
key as `UNPROTECTED PRIVATE KEY FILE` when running as SYSTEM. Installer now
resets the key ACL (removing explicit creator permissions), restricts access to
SYSTEM/Administrators and sets SYSTEM as owner before validation/start. SSH
StrictModes stays enabled. Host-key checks subsequently passed; see resolved login below.
Follow-up screenshot still showed the old installer failure line (87), rather
than the corrected script's line. Download now explicitly uses filename
`esp32-printer-remote-v3.ps1` and prints `ESP32 remote support v3 - host key ACL fix`
so repeated downloads and file-picker selection can be distinguished.
V3 ran successfully and reported Windows `192.168.1.105:22222`, user `tff`.
Mac verified the reported host key but connections reset before the SSH banner.
After stopping the task, the log showed successful listening and two accepted
connections, with no authentication attempt. Earlier host-key warnings were
historical appended entries. Shared `-E sshd.log` is exclusively locked on this
Windows build; split session/auth processes also inherit `-E`. Next targeted fix
uses OpenSSH's `%P`/`%p` expansion for per-process log files. This alone did not fix login.

Live support commands: authenticated `GET /support/command` returns JSON
`{"command":"..."}`. The homepage shows it at the top, polls every 3 seconds,
and offers copy (never execution). Authenticated, CSRF-protected POST accepts
`command` (1..8192 UTF-8 bytes) and persists it in NVS before replacing the live
value. Failed refresh disables the copy button. Update using existing client:
`c = CaptClient(); c.request('/support/command', {'token': c.token, 'command': text})`.
Commands render through textarea `.value`, not HTML. No OTA/reboot is needed for
later command changes. The initial command applies the per-process SSH log fix.
Windows support uploads must be bounded before form encoding: .NET Framework's
URI escaping failed on the first large report, and gzip still exceeded the chosen
limit. Current collection sends only the newest 3 log tails, each capped at 2,000
characters, as compact JSON. Avoid serializing raw PowerShell provider objects
or decorated `Get-Content -Raw` output; convert needed fields to plain strings.
Bounded log upload succeeded. Session log contained the accepted connection but
no authentication; Mac still saw reset before banner. Next targeted repair grants
BUILTIN\Users read/execute on public OpenSSH binaries and traversal/read on the
support directory (non-inheriting), with Administrators owning binaries, matching
Microsoft's public-program permission model. Host private-key ACL is unchanged.
After this repair, key-authenticated SSH login succeeded as `desktop-h3hpdr1\tff`.
Live repair also enabled DEBUG3 temporarily. Print Spooler was found stopped and
started successfully; all Canon queues were empty. SSH is maintenance access only.

### Checks

`python check_lan_print.py --hardware` passed offline multi-page/malformed-input
checks, fragmented TCP responses, real CAPT over TCP, exclusive TCP/HTTP ownership,
release/reacquisition, and continued live USB. `perl mac_backend.pl --self-test`
checks the production backend parser and job-info layout. Firmware build and OTA
passed. CUPS integration was tested through an actual OS queue, not just a helper.

Renderer PDF Ubuntu: [server-renderer/README.md](server-renderer/README.md). Đã in thử thực tế; bản đầu A4 một mặt. API/worker cloud chưa tích hợp.

Cloud server docs synced 2026-10-22: [server deployment](docs/server-deployment.md).
Firmware origin corrected to `https://quanly.lophocbinhminh.com`; build/offline checks passed.
OTA deferred at user request. Real station registration/token and end-to-end cloud print remain pending.
