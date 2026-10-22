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
- Khi có job Windows hoặc capture chưa xác nhận, bộ cài chỉ cập nhật cửa sổ
  trạng thái rồi dừng trước thay bridge/queue. Không xóa, phát lại hoặc coi
  job đó thành công. Hiện máy thử còn hai job Windows và capture cũ đang giữ.
- Monitor chỉ chứa IP/ID/tên trạm, file được Users đọc/chạy nhưng không sửa.
  Không nới quyền thư mục CAPT chứa tài liệu. Không token trong UI.

Kiểm tra: `powershell -NoProfile -File .\check_windows_status.ps1` trên Windows.
Đã chạy test chọn queue/đổi tên/xung đột và handler UI E100/offline/unknown-total
trên Windows thật; task UI đang chạy. Chưa kiểm chứng trực quan cửa sổ trên
màn hình người dùng, chưa in lại vì lỗi E100 và job chưa xác nhận.
