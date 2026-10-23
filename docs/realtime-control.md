# ESP32: trạng thái trực tiếp và điều khiển job — bàn giao 23/10/2026

Origin duy nhất: `https://quanly.lophocbinhminh.com`.
Tài liệu này bổ sung contract v1, không thay schema dữ liệu in `espjob-v1`.
Không thay mã ứng dụng, DB, nginx hoặc service trên server trong đợt bàn giao này.

## Vấn đề xác nhận trên server

Đã đọc `printer_api.php`, `app/printer_repository.php`,
`assets/scripts/app-printers.js`, `docs/printers.md` tại `/var/www/lop1`.
`printerHeartbeat()` lưu nguyên JSON; `printerStatus()` chỉ xét trạng thái tổng quát
và USB `error_code`, không chuyển `printer.capt.service_code` thành lý do cụ thể.
Giao diện poll 5 giây, online timeout 90 giây. Đây là nguyên nhân web vẫn ghi lỗi
chung dù ESP đã đọc **E100 0000**. Không dùng `printer.error_code=0` để kết luận
máy không lỗi: trường này là lỗi USB, không phải mã dịch vụ Canon.

## Phần firmware/client đã viết

- USB status idle đọc mỗi 500 ms, cùng arbiter, không chen giữa giao dịch CAPT.
- HTTPS telemetry task độc lập với tải/in, giữ kết nối HTTP khi server hỗ trợ.
  Kiểm tra thay đổi mỗi 250 ms; gửi khi có thay đổi và heartbeat tối đa cách 1 giây
  khi mạng khỏe. Timeout HTTPS 1,5 giây; mạng lỗi không chặn USB hoặc web ESP.
- Dashboard ESP nhận SSE port 81, tối đa ba viewer; snapshot theo thay đổi mỗi
  250 ms, keepalive mỗi giây. Không chặn main WebServer port 80. Nếu SSE đứt,
  fallback GET `/status` mỗi giây. SSE dùng nonce CSRF của dashboard, giới hạn
  cùng LAN và CORS chỉ origin `http://<IP ESP>`; không đưa cloud token vào URL.
  Truy cập dashboard bằng IP; hostname/mDNS có thể fallback polling do CORS.
- Một bản ghi job thực thi: ID, nguồn LAN/cloud, state, request, capability,
  total/completed mặt in. GET `/status` và `/printer/status` có `job_control`;
  heartbeat có `job`. Không suy ra tờ giấy từ mặt in; không coi spool xong là in xong.
- Cloud engine và bridge Windows/Mac mới kiểm tra điều khiển trước mỗi mặt.
  Pause: không gửi mặt tiếp theo, giữ TCP/USB bằng lệnh status trong lúc đợi.
  Trang đã đệm vẫn có thể ra. Resume chỉ tiếp tục phiên sống, không tái phát job.
  Cancel: ngừng gửi thêm và giữ kết quả `uncertain`, không tự phát lại/khẳng định
  đã xóa bộ nhớ máy in. Chưa xác minh lệnh CAPT abort/purge vật lý, không tự gửi.
- Metadata TCP `F002`: ID ASCII `[A-Za-z0-9_-]`, 1..64 byte. ACK như CAPT
  (opcode F002, length 6, payload 00 01). Gửi trước F001.
  `F003`: payload một byte 1 xác nhận paused, 2 xác nhận resumed; ACK tương tự.
  Client cũ không gửi F002 chỉ được theo dõi, `controllable=false`; không giả báo
  pause/cancel thành công. Bộ cài/client mới phải đi cùng firmware mới.
- Job uncertain giữ khóa; không xóa/tiếp tục/in lại. Đối soát là xác nhận riêng,
  không chữa E100 hoặc xóa capture/pending trên Windows/Mac.
- Queue web: ESP chỉ giữ tối đa 16 metadata; tài liệu và hàng đợi bền vững vẫn
  trên VPS. Adapter bên dưới phải được dev server triển khai mới hoạt động.

Mục tiêu khi mạng khỏe: phát hiện cảm biến idle + gửi + render UI khoảng dưới
1–1,5 giây; không phải cam kết hard realtime. USB/driver có thể phản hồi chậm,
mạng đứt không thể báo ngay. Ghi received_at/age và trạng thái stale, không giữ
chữ “đang in/sẵn sàng” vô hạn. Server browser phải push, không chỉ giảm poll 5 giây.

## Heartbeat mở rộng tương thích

Endpoint cũ: `POST /api/print/v1/stations/{station_id}/heartbeat`, Bearer token,
schema=1. Giữ toàn bộ `printer`, `lan`, `cloud`, thêm:

```json
{
  "boot_id": "f94b...",
  "telemetry_seq": 42,
  "job": {
    "id": "20261023-001122-1234567", "source": "lan",
    "state": "paused", "request": "pause_requested", "active": true,
    "controllable": true, "total": 3, "completed": 1
  },
  "command_ack": {"seq": 7, "result": "accepted"},
  "queue_command": {
    "id": "queue-00112233-44556677", "job_id": "web_job_123",
    "action": "cancel", "revision": "revision_42", "result": "pending"
  }
}
```

`command_ack.accepted` chỉ nói firmware nhận yêu cầu hợp lệ, **không phải** máy đã
ngừng. UI căn cứ `job.state=paused`, terminal state và counter để xác nhận kết quả.
Giữ `request` để UI hiện pending. Hủy sau khi đã gửi giấy có thể kết thúc completed
(nếu phiên đã gửi hết và máy hoàn tất trước checkpoint), hoặc uncertain; không hứa
thu hồi trang đã in. Không áp dụng điều khiển cho ID khác/current job đã thay đổi.

Server vẫn có thể trả 204 nếu chưa có adapter; firmware tiếp tục gửi telemetry
nhưng dashboard ESP ghi hàng đợi server chưa hỗ trợ. Khi triển khai, trả HTTP 200:

```json
{
  "command": {
    "seq": 8, "boot_id": "f94b...", "job_id": "20261023-001122-1234567",
    "action": "resume"
  },
  "queue": {
    "revision": "revision_43", "truncated": false,
    "items": [
      {"id": "web_job_123", "state": "queued", "title": "Đề kiểm tra"}
    ]
  },
  "queue_ack": {"id": "queue-00112233-44556677", "result": "accepted"}
}
```

Các trường tùy chọn; JSON response <=8192 byte, queue sau lọc <=4096 byte,
<=16 item, title <=128 byte UTF-8. revision/ID chỉ `[A-Za-z0-9_-]`, 1..64.
`command.seq` integer 1..2147483647, tăng trong mỗi boot, boot_id khớp chính xác.
Firmware bỏ command của boot khác/seq cũ; không phát lại pause cũ sau resume.
Chỉ gửi một command đang chờ theo thứ tự; server lưu bền vững intent, ack,
expected job/boot, issuer, timestamps. Các command không sống qua reboot mặc định
(expire và yêu cầu người dùng kiểm tra lại); không tự phát lại resume sau reboot.

`queue_command.result=pending` là intent ESP→server, lưu NVS **trước** trả 202,
phát lại cùng ID sau reboot/mất ACK. Server xử lý idempotent bằng
`(station_id, queue_command.id)` và trả cùng kết quả cũ. Chỉ ACK sau commit DB.
Không xử lý `preparing/acknowledging/accepted/rejected` như intent mới.
Kiểm tra revision và trạng thái trong transaction: job đã được claim thì từ chối
cancel/delete queued, không âm thầm chuyển sang hủy job đang in. Job phải thuộc
đúng trạm/token. Server không cho station token quản lý máy khác.
ACK accepted nghĩa thao tác queue đã commit; lý do chi tiết có thể thêm trên UI
server, firmware hiện chỉ hiện accepted/rejected. Không trả accepted rồi xóa file
đang được worker/ESP đọc. File GC chỉ sau terminal + hết lease/retention.

## API web ESP

Các POST đều Basic admin + CSRF hiện có, body form, không đặt token trong URL:

| Endpoint | Body ngoài token | Kết quả |
| --- | --- | --- |
| `/jobs/control` | job_id, action=pause/resume/cancel/delete | 202 nhận yêu cầu, 409 không áp dụng |
| `/jobs/resolve` | job_id | Xác nhận đã đối soát uncertain, không phát lại |
| `/jobs/queue/control` | job_id, action=pause/resume/cancel/delete | 202 đã lưu intent gửi VPS; chưa thực hiện |

Delete trên job hiện hành chỉ xóa metadata terminal trên trạm, không xóa tài liệu
máy gửi/VPS. Không cho xóa uncertain hoặc active. Queue server chưa hỗ trợ, snapshot
cũ >5 giây, job không nằm trong snapshot hoặc còn intent pending thì trả 409.
Dữ liệu hiển thị stale là dữ liệu cũ, không phải trạng thái máy hiện tại.

## Việc dev server phải triển khai

1. Sửa `printerStatus()` ưu tiên fresh CAPT: service_code (E100 0000), jam, cover,
   paper_required, toner_warning. Có thể đồng thời nhiều cờ; giữ tất cả. Paper required
   nghĩa kiểm tra giấy/khổ/nguồn, không khẳng định khay trống. Service có cờ nhưng
   code trống thì ghi “Lỗi dịch vụ Canon, đang đọc mã”, không bịa mã.
2. Lưu heartbeat snapshot cuối + sequence theo station/boot; không để request cũ
   ghi đè snapshot mới. Giữ event terminal hiện có, không hạ completed về printing
   từ heartbeat chậm. Job LAN hiển thị là LAN, không gán vào web job khác.
   Heartbeat chứa job ID + counter là tiến trình trực tiếp; kết quả bền vững cloud
   vẫn qua `/jobs/{id}/events` với lease. Không dùng heartbeat để tự xác nhận completed.
3. Transaction commit rồi publish station/job event tới SSE/WebSocket của ứng dụng.
   SSE endpoint browser cùng origin, dùng session/quyền hiện tại, `Last-Event-ID`,
   snapshot khi reconnect, bounded history và chống event cũ bằng seq. Không nhúng
   station token vào browser. Mọi user event được lọc quyền theo station/job.
   Tránh SSE PHP-FPM giữ hết worker: ưu tiên broker/stream service sẵn có; nếu SSE
   qua PHP phải dự trù pool riêng, `fastcgi_buffering off`, `X-Accel-Buffering:no`.
   Không reload nginx/services trong bước bàn giao docs này.
4. Heartbeat khoảng 1s: UI online/offline dựa server received_at; khuyến nghị stale
   >3s, offline >6s có debounce, phân biệt mất ESP với USB ngắt/máy lỗi. Đây là
   ngưỡng mất kết nối, không phải khoảng cập nhật trạng thái. Phát event offline
   bằng timer server, không chờ request từ ESP vốn đã mất mạng.
5. Trả queue/command/queue_ack theo JSON trên. API thao tác từ trang quản lý phải
   CSRF + quyền hiện tại + job ID + idempotency key. Không coi HTTP202 là đã dừng.
   Lưu command pending → accepted/rejected → applied khi telemetry xác nhận.
6. Queue chưa claim: pause chuyển queued→paused, resume paused→queued, cancel
   queued/paused→cancelled. Xóa chỉ terminal và theo retention/quyền; giữ audit.
   Rendering/downloading/leased phải kiểm tra ownership/lease, không xóa nóng.
   Job printing: phát command đúng boot/ID; nếu client legacy không controllable
   thì trả unsupported. Uncertain không resume/retry/delete; có luồng đối soát
   riêng với xác nhận giấy và audit. Không dùng resolved để giả job đã in thành công.
7. Hiện web mới chỉ renderer A4 dọc một mặt trong `docs/printers.md`; không nhân
   tiện bật duplex/N-up bằng suy đoán. Renderer/option scope là hạng mục riêng.

## Giới hạn cần phân biệt, chưa được đánh dấu hoàn thiện

- Server ứng dụng chưa thay đổi; adapter/push UI ở trên là bàn giao, không phải đã
  hoạt động. Cần dev server triển khai rồi test kết nối hai chiều.
- ESP quản lý job đã tới trạm và metadata queue VPS. Không thấy/hủy được mọi job
  còn ở spooler Windows/CUPS của các máy chưa gửi tới ESP. Muốn quản lý những job
  này từ ESP/server phải thêm **agent trên từng client** đăng ký an toàn, báo inventory
  và nhận command có xác thực/ACK. Không mở điều khiển spooler bằng endpoint LAN
  không auth. Bridge/status UI hiện tại chưa có agent inventory đó.
- Lịch sử LAN hiện chỉ bản ghi RAM cuối trên ESP; capture/pending của backend mới
  là khóa phục hồi bền vững. Sau reboot, không coi ESP không có record là máy gửi
  không còn job. Không tự xóa hàng đợi/capture chưa được người dùng cho phép bỏ.
- Chưa xác minh cancel/pause trên giấy thật do máy đang lỗi E100. Các cờ cảm biến
  khác chưa đủ test vật lý. Không báo “hỗ trợ hết lỗi Canon” khi chưa có dữ liệu.

## Kiểm tra nghiệm thu phối hợp

- Có E100: ESP và web cùng hiện E100 0000, không chỉ USB error0/User intervention.
- Server chậm 10s: USB/status/local UI vẫn hoạt động; không fake tiến độ/online.
- Dừng giữa 3 mặt: yêu cầu pending rồi paused ở checkpoint, mặt đã đệm có thể ra;
  resume cùng phiên/ID, không gửi lại mặt cũ. Hủy giữ uncertain và chặn retry.
- Bấm hai lần cancel queued/reboot ESP sau commit trước ACK: cùng request ID chỉ
  một lần chuyển trạng thái. Queue revision đổi/claim tranh lượt: reject đúng.
- Job LAN phải có ID riêng, không cập nhật nhầm web job. Client cũ nút điều khiển
  bị khóa. Viewer thứ tư fallback poll, không gây treo task USB.
- SSE reconnect/mạng đứt: snapshot/seq, stale/offline, không hiển thị completed giả.
- Đo latency thực tế USB→ESP snapshot→server commit→browser; ghi p50/p95 và thử
  nhiều client trước khi tuyên bố realtime đầu-cuối đã nghiệm thu.

## Kết quả thử trên thiết bị/server thật

Firmware đã nạp COM, station PASS, không gửi raster. Heap khoảng 129 KB trước TLS, sau khi giữ kết nối HTTPS ổn định khoảng 76–80 KB trong lượt theo dõi 5 phút.
SSE trên port81 trả snapshot fresh với E100 0000. API từ chối job ID không đúng
bằng409, không làm thay đổi máy in. Heartbeat HTTPS nhận204 từ production; truy vấn
read-only DB cho thấy E100 0000/fresh=true và telemetry_seq94→95 ở hai lần đọc.
Ứng dụng server vẫn trả204 (chưa có queue/command adapter); dashboard ESP ghi rõ
chưa hỗ trợ hàng đợi server, không hiện các nút giả đã hoạt động.

Bridge C# mới đã biên dịch và chạy self-test trên Windows thật. Windows đã được cập nhật bridge mới sau khi queue trống; bộ cài cho nâng cấp
bridge STOPPED, giữ nguyên hash capture/marker và giữ khóa in. Capture đang chạy
vẫn bị chặn nâng cấp. Backend Mac mới đã qua parser/CAPT regression, chưa thay bản root
đang cài và không resume queue cũ. Mac đã xóa job/pending cũ theo yêu cầu người dùng; Windows đã lưu capture cũ vào Archived theo yêu cầu xử lý khóa, hash giữ nguyên,
không gửi lại; listener được mở lại nhưng máy in vẫn báo E100 0000. Bộ cài có
`-ResolvePending` cho thao tác này, chỉ khi queue trống và bridge đã STOPPED.
