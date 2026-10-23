# Trạng thái Canon LBP3300: CAPT → ESP → máy tính/server

## Kiểm chứng trên Máy 1, 22/10/2026

- Nạp/debug qua COM `/dev/cu.usbmodem5CF90462221`, MAC `98:c3:77:a1:36:b0`.
  Máy in nối trực tiếp native USB → OTG; bo đã được người dùng sửa hai mối hàn.
- Đọc đúng **E100 0000** từ máy thật: A0A8 có cờ service; A2A2 trả
  `a2 a2 0c 00 00 01 00 00 00 01 00 00`.
- Phản hồi A0A8 60 byte bị chia thành **6 + 54 byte** khi lỗi. Bộ đọc cũ đòi
  một transfer đủ cả frame, bỏ sót đuôi và làm lệch các lệnh tiếp theo.
  Firmware mới ghép theo chiều dài CAPT; telemetry cũng ghép thụ động mọi phản hồi.
  Query chỉ đọc còn xả phản hồi cũ/tail của client bị ngắt trước khi tìm đúng opcode;
  không gửi lại lệnh in để khôi phục đồng bộ.
- `/status` và `/printer/status` đọc được `service_code="E100 0000"`.
  Heartbeat HTTP 204; đọc trực tiếp `print_stations.heartbeat_json` trên server
  đã thấy `printer.state="error"` và mã E100 trong `printer.capt.service_code`.
- Mac CUPS đã nhận `printer-state-message="Canon service fault / E100 0000"`
  và `other-error`, giữ nguyên `paused` của hàng đợi cũ. Không tự tiếp tục job.
- Windows 10/11 x64: đã biên dịch trên máy thật và chạy bridge self-test đạt.
  Đã cập nhật bridge SYSTEM của Máy 1, queue báo PaperOut/UserIntervention thay
  vì Ready; JSON trạng thái có E100 0000. Pending/captured job được giữ nguyên hash.
  Lỗi Winspool đã sửa: cần ACCESS_USE | ACCESS_ADMINISTER và GetPrinter level 6
  hỏi kích thước trước khi cấp bộ đệm (máy x64 trả 8 byte, không phải 4).
- Test LAN chỉ đọc giữ phiên TCP 14 giây, lặp A0A8: E100 vẫn được cập nhật
  nhờ truy vấn A2A2 trong cùng owner, không gửi dữ liệu in hoặc byte thừa cho client.
- Chưa nghiệm thu vật lý hết giấy, kẹt giấy, mở nắp, hộp mực trên bo này.
  Không cố gây kẹt hoặc tự xóa service fault để thử.

## Dữ liệu và độ trễ

Task trạng thái riêng poll CAPT mỗi 1,5 giây khi lấy được USB arbiter; vẫn chạy
khi cloud tắt/mất Internet. Không chen lệnh USB vào job LAN/cloud. Khi job đang
chạy, bắt thụ động phản hồi A0A8 của chính client in. Không có phản hồi mới trên
10 giây hoặc USB ngắt: `fresh=false`; không coi số liệu cũ là trạng thái hiện tại.
Mã service quá 10 giây không đọc lại được sẽ để trống, không giữ mã cũ vô hạn.
Proxy TCP bổ sung query A2A2 trong chính phiên sở hữu khi A0A8 báo service fault;
đáp án A2A2 chỉ dùng cho telemetry, không chen byte vào phản hồi của client.

Endpoint LAN chỉ đọc, không cần mật khẩu admin: `GET /printer/status`.
Chặn client ngoài subnet; không mở cổng router. Không chứa token/cấu hình/bí mật.
Trang quản trị `/status` vẫn cần xác thực. Giao diện ESP poll 3 giây; heartbeat
server chu kỳ nền khoảng 5 giây cộng thời gian HTTPS; UI server hiện poll 5 giây.
Đây là cập nhật gần thời gian thực, không phải bảo đảm trễ cố định khi mạng lỗi.

```json
{
  "schema": 1,
  "connected": true,
  "responsive": true,
  "active": false,
  "capt": {
    "fresh": true,
    "age_ms": 700,
    "service_error": true,
    "service_code": "E100 0000",
    "paper_jam": false,
    "cover_open": false,
    "paper_required": false,
    "toner_warning": false,
    "toner_percent": null,
    "decoded": 0,
    "started": 0,
    "exiting": 0,
    "completed": 0
  },
  "total": 0,
  "completed": 0
}
```

`capt` còn có `opcode`, `frame_hex` để chẩn đoán. Flags/counters chỉ dùng khi
`fresh=true`; false khi stale là **chưa biết**, không được suy ra máy hết lỗi.
`paper_required` là yêu cầu giấy/khổ/nguồn giấy, không khẳng định khay vật lý trống.
`toner_warning` yêu cầu kiểm tra hộp mực; không khẳng định phần trăm hay mực đã hết.
Không có số phần trăm đáng tin từ LBP3300: giữ `toner_percent=null`.

A0A8 payload: service byte 2 bit 3; jam word 12 bit 8; cover word 12 bit 14;
toner word 12 bit 13; giấy word 8 bit 14. Bốn counter u16 tại offset
14/16/18/20: decoded/started/exiting/completed. A2A2: count full-frame byte 5,
record đầu tại full-frame byte 8 gồm u16 code/subcode; hiển thị hex `E%03X %04X`.
Ánh xạ service/jam/cover/toner đối chiếu luồng xử lý driver Canon; cần các ca vật lý
để nghiệm thu đầy đủ. Không tuyên bố giải mã mọi cảm biến hay mọi service code.

Counter là **mặt ảnh in**, không phải phần trăm byte truyền, không phải tờ giấy.
In hai mặt/N-up phải dùng số mặt sau layout. Counter riêng không cho biết tổng;
chỉ hiển thị `completed/total` khi job metadata có total hợp lệ và job đang active.
LAN dùng `total/completed` ngoài `capt`; cloud dùng `cloud.progress_known/total/completed`.
USB bị ngắt/timeout sau side effect: giữ kết quả uncertain, không tự gửi lại.

## Máy tính

Chạy lại bộ cài mới để có monitor; bộ cài cũ không tự được thay thế bởi firmware.

- Mac: cùng backend Perl, `--monitor IP QUEUE`; bộ cài tạo LaunchDaemon riêng
  mỗi station. Monitor dùng JSON::PP/curl/ipptool có sẵn, CUPS qua Unix socket
  với PeerCred. Cập nhật message/reasons kể cả khi rảnh; giữ lý do scheduler như
  `paused`, không resume job. Khi in, backend gửi STATE, job-media-progress và
  PAGE theo counter hoàn tất; duplex đếm tờ sau khi đủ hai mặt.
- Mac hiện tại: đã chạy monitor bằng LaunchAgent của user cho queue cũ
  `Canon_LBP3300_ESP32`; không thay backend root cũ nếu chưa chạy lại bộ cài.
- Windows: bridge SYSTEM có thread poll mỗi 2 giây, SetPrinter level 6 cập nhật
  ERROR/PAPER_JAM/PAPER_OUT/DOOR_OPEN/TONER_LOW/OFFLINE/PRINTING; giữ các bit khác.
  Mã CAPT đầy đủ và counter lưu trong thư mục station `printer-state.json`;
  `status.txt`/job `.progress` ghi tiến trình thực. Windows có thể chỉ hiện lỗi
  chuẩn, không có chỗ hiển thị chuỗi E100 tùy ý trong giao diện queue mặc định.
  Spooler có thể đánh dấu tài liệu đã chuyển sang bridge trước khi giấy ra;
  không lấy tiến trình spooler làm xác nhận vật lý.
- Đã nghiệm thu đọc lỗi trên Windows thật qua SSH. Giữ SSH dạng task
  `ESP32PrinterSupport`, không nhầm thành dịch vụ `sshd`. Nếu IP Mac đổi, cần cập
  nhật whitelist firewall và `from=` của authorized_keys; không mở ra Internet.
  Bản bridge mới vẫn chạy monitor khi bị khóa bởi pending job; muốn in lại phải
  kiểm tra/đối soát job rồi khởi động lại bridge, không tự phát lại file cũ.

## Việc dev server cần làm — không tự sửa ứng dụng server

Heartbeat giữ schema 1 và enum cũ. Fault CAPT dùng `printer.state="error"`;
`printer.error_code` vẫn là **lỗi USB** cũ (có thể bằng 0 dù máy báo E100).
Các trường mới nằm trong `printer.capt`; server hiện nhận và lưu đầy đủ.
Không dùng `error_code==0` để kết luận máy sẵn sàng. Chặn claim nếu state error.

Hiện UI server vẫn gom `state=error` thành “Lỗi máy in chưa xác định”. Dev cần:

1. Đọc `printer.capt`, kiểm tra fresh và tuổi heartbeat (age_ms là tuổi mẫu tại
   lúc gửi; cộng thời gian từ last_seen để hiển thị tuổi hiện tại).
2. Hiển thị service_code nguyên văn khi service_error; thêm nhãn riêng cho jam,
   cover, paper_required, toner_warning. Unknown/stale dùng “Chưa có trạng thái mới”.
3. Hiển thị progress từ `lan` nếu busy_lan, từ `cloud` nếu busy_cloud; không coi
   downloaded/submitted là completed. Fault có thể cùng tồn tại với progress dở.
4. Online ESP khác máy in sẵn sàng. Giữ cơ chế uncertain/no-auto-reprint hiện có.

Không sửa PHP, DB, dịch vụ hoặc thay đổi dở của dev server. Bàn giao file này riêng.

### Windows: cửa sổ trạng thái và cài lại (23/10/2026)

- Bridge SYSTEM chỉ đặt được các cờ chuẩn của Windows; `User intervention`
  không mang chuỗi mã Canon. Cửa sổ CAPT gốc chưa hỗ trợ giao thức bridge này.
- Bộ cài cung cấp cửa sổ WinForms riêng trong phiên desktop người dùng: mã Canon
  (đã đọc E100 0000), trạng thái USB/mạng, cờ giấy/nắp/kẹt/mực đã giải mã,
  hàng đợi Windows và bộ đếm máy in. Tự mở khi thấy job mới hoặc lỗi đổi;
  đóng cửa sổ sẽ thu về tray; mở lại từ tray hoặc Start Menu `ESP32-<ID>`.
  Common Startup chạy monitor ở lần đăng nhập tiếp theo; task Interactive khởi chạy
  ngay cho tài khoản đang cài. Task SYSTEM không hiển thị cửa sổ.
- Poll mỗi 2 giây, HTTP timeout 1 giây; không phải push tức thời. Job quá ngắn
  có thể nằm giữa hai lần poll. Chỉ hiện mẫu số khi ESP có tổng hợp lệ cho job
  đang hoạt động. Counter CAPT là lượt in mặt, không phải số tờ hay trang Word.
  Spooler gửi xong không đồng nghĩa máy đã nhả giấy.
- Mã dịch vụ chỉ mô tả lỗi máy; không suy đoán linh kiện hỏng từ E100. Các tình
  huống kẹt giấy/nắp/mực vẫn cần kiểm chứng vật lý; không cố gây kẹt để thử.
- Bộ cài hỏi tên, Enter giữ tên đã cài (hoặc tên trạm cho máy mới). Tham số
  `-PrinterName` bỏ qua câu hỏi. ID ESP và cổng `ESP32_LOCAL_<ID>` quyết định
  danh tính, không phải tên hiển thị/IP. Đổi IP cùng ID cập nhật bridge;
  đổi tên sẽ đổi tên queue đang có, dọn queue trùng cùng cổng khi tất cả rảnh.
  Tên thuộc trạm/cổng khác bị từ chối. Máy USB Canon riêng không bị xóa.
- Khi còn job Windows hoặc capture đang chạy, bộ cài dừng trước thay bridge.
  Nếu queue đã trống và bridge đã STOPPED vì capture chưa xác nhận, cho phép
  cập nhật executable/monitor, giữ nguyên capture và khóa listener in. Không xóa
  hoặc phát lại capture. Đã cập nhật thành công Windows thật, hash capture/marker
  không đổi, một queue Máy 1. Mac đã hủy job và xóa pending cũ theo yêu cầu.
- Monitor chỉ chứa IP/ID/tên trạm, file được Users đọc/chạy nhưng không sửa.
  Không nới quyền thư mục CAPT chứa tài liệu. Không token trong UI.

Kiểm tra: `powershell -NoProfile -File .\check_windows_status.ps1` trên Windows.
Đã chạy test chọn queue/đổi tên/xung đột và handler UI E100/offline/unknown-total
trên Windows thật; task UI đang chạy. Chưa kiểm chứng trực quan cửa sổ trên
màn hình người dùng, chưa in lại vì lỗi E100 và job chưa xác nhận.

Cập nhật tiếp 23/10: [luồng trực tiếp, hàng đợi và điều khiển](realtime-control.md).
Firmware có SSE local, telemetry HTTPS riêng, job ID và checkpoint control. Server
production đã nhận E100 cụ thể nhưng còn cần triển khai adapter/push UI trong tài
liệu bàn giao; không coi giao diện server đã sửa chỉ vì heartbeat đã có dữ liệu.

### Đối soát capture Windows cũ

Bổ sung `-ResolvePending` cho bộ cài Windows. Chỉ áp dụng khi queue trống và
bridge đã STOPPED, không áp dụng capture đang in. Chuyển `.capt`, `.capt.progress`
và `pending.txt` vào `Stations/<ID>/Archived/<timestamp-guid>/`, không phát lại,
không sửa byte tài liệu; lỗi chuyển file sẽ đưa các file đã chuyển về vị trí cũ.
Sau đó bridge được cập nhật/khởi động, listener hoạt động lại. Không tự thực hiện
bước này khi cài thông thường; nếu không có cờ, nâng cấp vẫn giữ khóa uncertain.

Đã thử Windows thật: hash capture trước/sau lưu trữ khớp, pending active hết,
listener mở lại, chỉ một queue Máy 1. Mac đã hủy job và xóa marker pending root
cũ theo yêu cầu. Máy in vẫn báo fresh E100 0000; việc gỡ khóa không chữa mã dịch
vụ trên thiết bị và không được dùng để tự xác nhận máy in đã sẵn sàng.
Test: `powershell -NoProfile -File .\check_pending_archive.ps1`.

Windows SetPrinter(level6) có thể bỏ cờ ERROR đơn lẻ. Với service_error hoặc status
stale, bridge đặt thêm USER_INTERVENTION để queue không hiện Ready giả. Mã E100
cụ thể vẫn ở cửa sổ status/ESP/server; User intervention không phải tên lỗi Canon.

### Windows status window (2026-10-23)

The station monitor now uses a native WinForms dashboard: station name/IP,
colour-coded Canon fault card (including exact E100 0000), physical impression
progress when a fresh total exists, and a Windows spool job list. Buttons open
Windows queue, Canon printing preferences and the ESP dashboard. Closing hides
to the notification area; double-click restores it. Windows spool page counts
are labelled separately and never treated as confirmed paper output.

The local bridge publishes an opaque GUID in the station's Users-readable,
SYSTEM/Admin-writable `print-event.txt` when accepting a print connection.
The desktop monitor checks it every 250 ms, also checks spool job changes and
new device errors, and opens the window. No document content or credentials are
published. HTTP status uses an asynchronous request with a two-second deadline;
network status is sampled about once per second. Installation restarts only the
matching station monitor so upgrades replace the running UI. Windows can restrict
foreground focus; the monitor requests activation but cannot override OS policy.

Verified on the connected Windows machine: upgraded existing Máy 1 without adding
a queue; parser/bridge self-tests and status model tests passed. Captured the real
window showing E100 0000, hid it and verified an opaque test event made it visible
again. This popup test sends no CAPT pages. E100 remains a physical printer fault.

### ESP homepage and support commands (2026-10-23)

Homepage layout is denser: printer and network cards share the first row; web
registration is collapsed. The Cài máy in shortcut opens current GitHub installers,
including Windows dashboard commit fc10c49 and later. Installer files stay on GitHub.
Permanent support commands enable/disable the existing ESP32PrinterSupport task
and firewall rule; stopping also terminates sshd processes from its exact portable
installation path. They do not stop the CAPT bridge. Disabling preserves the rule
for the next support session. First-time SSH setup still requires the separate
support script, Mac IPv4 and public key; enabling does not expand the whitelist.
A Mac SSH command template is available beside the live diagnostic command.
No SSH private key, cloud token or device password is embedded in these commands.
Deployment verified through COM on station 98:c3:77:a1:36:b0: firmware hash verified,
live homepage includes installer link and all three support copy fields; Wi-Fi
192.168.1.199 and printer USB connected. COM reports STATION: PASS after reboot.
Both Windows support commands were parsed on the Windows host without executing
them, preserving the active support session.

### macOS desktop status window (2026-10-23)

CUPS state monitoring alone does not provide a desktop popup. The installer now
adds a separate native AppKit window, using macOS JavaScript for Automation
(`/usr/bin/osascript`), without Python, Homebrew or new dependencies. Its per-station
Aqua LaunchAgent runs in the logged-in desktop, while the CUPS monitor continues
separately. Status is fetched asynchronously about once per second, with a two-second
network timeout. The window shows exact Canon codes, physical impression progress
only with a fresh known denominator, and pending local job IDs. Buttons open native
Print Center, printer settings and the ESP dashboard. Closing hides the window;
the Dock icon reopens it. New faults and new CUPS job IDs request activation.
Completed CUPS job history is included in detection so fast spool jobs are not
missed when default CUPS history retention is enabled. The window never resumes,
cancels or retries existing print jobs automatically. It displays pending job IDs;
document names and full CUPS controls remain in native Print Center.

The Mac installer downloads a SHA-256-pinned monitor when only install_mac.sh was
downloaded, and installs /Library/LaunchAgents/com.esp32printer.window.<station>.plist.
Existing Macs need the new monitor; queue/driver presence alone is insufficient.
On this Mac the same script was installed into the current user's Application
Support with a per-user LaunchAgent, preserving the paused queue and old job 238.
A held test job 239 triggered the popup and was then canceled without printing.
The live native window was visually checked and showed Canon E100 0000.

### Stable progress and desktop queue controls (2026-10-23)

User-facing progress now comes from the persistent `job_control` record (or cloud
heartbeat `job`), including ID, source, phase, total and completed impressions.
Raw TCP connection state/counters include diagnostic reads and must not be used
as print-job activity or progress. `/printer/status` top-level active/total/completed
are serialized from the same job snapshot. Cloud heartbeat LAN activity also uses
the job record. ESP dashboard and both desktop monitors retain a failed job's
0/2 record instead of alternating with unknown totals when diagnostic sockets
open/close. Zero of two is a count, not successful job completion.

A second cause was the Windows bridge's preflight retry: it sends F001 metadata
before reading the service fault, repeatedly starting then failing the same job.
ESP now rejects F001 before job activation while a fresh CAPT fault is present.
It records a new failed-before-start job atomically, preserves the same failed
record across retries and never overwrites an active or uncertain job.

Mac window now has a selectable native table and SF Symbol pause/resume/trash
buttons, with actual CUPS job states from Get-Jobs. Hold/resume/cancel target the
exact selected local CUPS job via lp/cancel; they do not resume the printer queue,
clear E100 or release uncertain print checkpoints. Cancellation asks confirmation.
Windows window has equivalent selected-job Suspend/Resume/Remove-PrintJob buttons.
These are LOCAL spooler controls; ESP/web job controls remain separate. Pages already
sent to the printer cannot be recalled. If interrupting active delivery leaves an
uncertain checkpoint, inspect output and resolve it before sending the document again.

Tested production Mac handlers on held job 240: resume to pending, hold again,
then cancel; no paper was sent and existing job 238 was untouched. Model tests cover
TCP churn with a fixed failed job, and the C++ job tests cover 50 identical fault
retries plus preservation of uncertain results. Windows model/bridge tests passed;
its existing captured waiting document was preserved and the GUI updated separately.
