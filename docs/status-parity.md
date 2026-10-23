# Dữ liệu cửa sổ trạng thái và web server — 23/10/2026

Origin: `https://quanly.lophocbinhminh.com`. Đây là phần bổ sung tương thích
`schema=1`; không thay `espjob-v1`. Không sửa ứng dụng/DB server trong đợt này.

## Dữ liệu đã bổ sung vào heartbeat

| Trường | Hiển thị và ý nghĩa |
| --- | --- |
| `job.title`, `job.client_name`, `job.client_os` | Tên tài liệu và máy gửi của lệnh thực thi, nếu nguồn cung cấp chính xác |
| `job.progress_known`, `job.progress_unit` | Tổng đã biết hay chưa; đơn vị `impressions` là mặt in, không phải tờ giấy |
| `job.actions.pause/resume/cancel/delete` | Bật nút tương ứng cho đúng ID lệnh trên ESP; không dùng để điều khiển job OS khác |
| `printer.capt.capabilities` | Các trường đã giải mã; `all_faults_verified=false`, `toner_percent=false` |
| `printer.capt.data_source` | `capt`: trạng thái và bộ đếm lấy từ máy in |
| `clients.items[]` | Hàng đợi Windows/macOS được bộ theo dõi đang chạy báo về |
| `clients.scope` | `reporting_clients_only`: không tuyên bố đã thấy mọi máy trong LAN |
| `clients.data_source` | `lan_client_report`: dữ liệu do máy LAN báo, không phải cảm biến máy in |

Các trường cũ vẫn giữ: USB connected/responsive/recovering, service_code như
`E100 0000`, paper_required/jam, cover_open, toner_warning; job ID/source/state,
request, active/controllable, total/completed; cloud state/error và mã HTTP riêng
heartbeat/claim/event. Heartbeat có boot_id + telemetry_seq để server phân biệt
khởi động lại và snapshot mới. Không ghi đè snapshot mới bằng phản hồi trễ.

### Job thực thi

```json
{
  "job": {
    "id": "mac-1234567890-123", "title": "Bài tập tuần 1",
    "client_name": "MacBook", "client_os": "macos", "source": "lan",
    "state": "printing", "request": "", "active": true,
    "controllable": true, "total": 8, "completed": 2,
    "progress_known": true, "progress_unit": "impressions",
    "actions": {"pause": true, "resume": false, "cancel": true, "delete": false}
  }
}
```

- Cloud claim response có thể thêm `title` (UTF-8 tối đa 128 byte), ESP đưa vào
  job đang thực thi. Nếu thiếu, server tự join title từ chính job ID của mình.
- Backend CUPS mới gửi tên tài liệu từ đối số CUPS và tên host qua TCP `F004`.
  Chỉ gửi nếu `/setup/profile.json` quảng bá `lan_metadata_version >= 1`.
- Bridge Windows mới gửi host/platform. CAPT capture không có liên kết chắc chắn
  tới ID/tên job Windows: `job.title` để trống, không đoán từ tài liệu gần nhất.
  Tên tài liệu Windows có trong danh sách OS dưới đây. Chỉ nối capture ID với job ESP khi trùng ID thật; không tự nối OS
job ID với capture theo thứ tự,
  số trang hoặc thời điểm gần nhau. Ứng dụng nguồn không được xác nhận thì không
  hiển thị tên Word/Chrome như dữ liệu đã biết.
- Metadata mô tả tài liệu, không đổi quyết định in hay tổng số mặt của CAPT.
- `completed` của job là tiến trình phiên; không dùng trực tiếp counter CAPT tích
  lũy làm số mặt của một job mới. Lệnh inactive là lịch sử, không hiện đang in.
- `uncertain` phải đối soát; không tự xóa, tiếp tục hoặc in lại. Cancel chỉ ngừng
  gửi thêm, không đảm bảo thu hồi mặt đã đệm trong máy in.

### Hàng đợi máy tính

```json
{
  "clients": {
    "scope": "reporting_clients_only", "data_source": "lan_client_report",
    "ttl_ms": 15000,
    "items": [{
      "id": "win-PC1-esp32-98c377a136b0", "name": "PC1", "os": "windows",
      "queue": "Máy 1", "last_report_uptime_ms": 12500,
      "stale": false, "truncated": false,
      "captured_jobs_count": 0, "unresolved_submission": false,
      "jobs": [{"id": "42", "title": "Bài tập", "state": "Printing",
                "total": 8, "completed": 0,
                "progress_basis": "os_spooler", "remote_controllable": false}]
    }]
  }
}
```

Windows dùng Get-PrintJob; Mac dùng CUPS Get-Jobs. Báo khoảng mỗi giây khi bộ
cửa sổ trạng thái chạy và đọc OS thành công, kể cả cửa sổ đang ẩn. Không đọc được
OS thì không gửi snapshot rỗng giả. Tắt bộ theo dõi/log out làm dữ liệu cũ sau
15 giây; không được diễn giải thành hết hàng đợi hoặc hoàn tất lệnh in.
`last_report_uptime_ms` theo đồng hồ ESP; so với heartbeat uptime, trong cùng boot.
Server cũng phải xét tuổi heartbeat của cả trạm. Giới hạn bốn máy báo, bốn job/máy,
metadata 2 KiB/máy (bốn job tính cả spooler và bridge, ưu tiên bridge); `truncated=true` nghĩa là còn job không được liệt kê.
Nếu hết slot còn mới, report mới bị từ chối; không ghi đè ngầm máy đang hoạt động.
Đây là trần RAM cho ba trạm/một nhóm nhỏ; tăng giới hạn hoặc cho client gửi thẳng
VPS khi triển khai quy mô lớn. ESP không giữ tài liệu/hàng đợi OS bền vững.

`clients.items[].jobs[].stage` phân biệt `spooler` và `bridge`; progress_basis
tương ứng `os_spooler` và `bridge_capture`. Windows báo cả ID capture (chính ID
gửi qua F002 khi thực thi), trạng thái bridge_queued/bridge_pending_inspection.
Không diễn giải bridge_pending_inspection là xác nhận tài liệu đã ra giấy.

`captured_jobs_count` và `unresolved_submission` phản ánh hàng đợi CAPT riêng
của bridge Windows (sau khi spooler giao xong); null là không được nguồn báo,
không phải 0/false. Các job OS có `remote_controllable=false`: điều khiển OS tại
máy gửi. Điều khiển job thực thi trên ESP và hàng đợi web vẫn dùng contract cũ.
Không hiển thị nút thao tác OS từ web khi chưa có agent thực thi/ack cho OS job.

## Quy tắc hiển thị server

1. Dùng trạng thái mới nhất đúng boot/sequence. Máy không gửi heartbeat phải hiện
   mất kết nối/dữ liệu cũ; không giữ chữ sẵn sàng hoặc đang in vô thời hạn.
2. Khi CAPT `fresh=false`, không coi các cờ lỗi false là xác nhận hết lỗi. Hiện
   chưa có trạng thái mới. Khi fresh=true, hiện service_code và từng cờ lỗi cụ thể.
3. Hiện cảnh báo mực theo toner_warning. toner_percent=null nghĩa là chưa đo được.
   capabilities nói đã giải mã trường; không khẳng định mọi mã lỗi đã kiểm chứng.
4. Bảng job thực thi dùng job.progress_known và đơn vị mặt in. Bảng OS riêng dùng
   progress_basis=os_spooler; null total phải hiện chưa xác định. Không cộng hai
   tiến trình, không tự khớp ID OS với ID CAPT hoặc xác nhận giấy ra từ spool xong.
5. Bật nút station theo job.actions, gửi đúng job.id + boot_id và kiểm tra ack/state.
   Hàng đợi web do DB server quản lý; heartbeat không gửi ngược bản sao queue web
   vì server đã có dữ liệu gốc. Xem docs/realtime-control.md cho lệnh/ack.
6. Sau khi nhận heartbeat, publish SSE/WebSocket cho browser. Không poll 5–10 giây.
   Firmware phát hiện idle CAPT mỗi 500 ms, kiểm tra telemetry 250 ms; mạng khỏe
   thường cập nhật dưới khoảng 1–1,5 giây, không cam kết hard realtime.

## Endpoint báo hàng đợi LAN

POST `http://<ESP>/printer/client-report`, Content-Type `application/json`.
Giới hạn cùng LAN như endpoint in TCP, không mở ra Internet, không có CORS cho
browser ngoài trạm. Endpoint chỉ báo metadata, không có quyền điều khiển/in.
Dữ liệu do máy LAN khai báo; không phải danh tính đã xác thực, không dùng cho
phân quyền, tính phí hoặc audit người dùng. Server phải escape tên/tài liệu.
ID client giới hạn ASCII `[A-Za-z0-9_-]`, tối đa 64 byte; name/queue 96 byte,
job ID 64, title 128, state 64; cấm control chars. total/completed null hoặc số
nguyên 0..1000000. Report quá giới hạn/không hợp lệ trả 400, sai LAN/type trả 403.
202 là nhận report, không xác nhận in. Report sau thay toàn bộ job của client,
bao gồm jobs=[] khi đọc OS thành công và thật sự không có job. Không coi POST
lặp lại là một lệnh in. Client ngừng báo chỉ làm stale, không purge tài liệu.

F004 payload là JSON UTF-8 `{title,client_name,client_os}` tối đa 1024 byte,
gửi sau F002, trước F001; ACK opcode F004 length6 payload 00 01. Không chuyển
frame metadata xuống USB. Client cũ vẫn in, trường chưa có giữ rỗng/null.

## Kiểm thử và triển khai

`python3 check_cloud.py`, `python3 check_installers.py`, `python3 check_home.py`,
`node check_mac_status.js`, `python3 check_lan_print.py`; kiểm tra live không in:
`python3 check_telemetry.py` (trạm phải rảnh). Đã build/nạp qua COM, kiểm tra
boundary report và F004 ACK trên bo. Bridge biên dịch và self-test trên Windows.
Cần nâng bộ theo dõi trên từng máy để có clients; firmware không tự đọc được
hàng đợi máy chưa cài/chưa chạy bộ theo dõi. Bộ cài mới nằm trên GitHub, ESP chỉ
trỏ link tải. Bản này không tự sửa UI/DB/service của web server; dev server áp dụng
các trường/rules ở trên vào UI và SSE/WebSocket.

Kiểm tra production: DB heartbeat đã lưu clients mới của Mac và Windows,
cả hai fresh; Windows báo một capture và unresolved_submission=true. Đã giữ
nguyên capture/khóa cũ khi nâng cấp; không có yêu cầu purge hoặc phát lại.
Mac đã nâng bộ cửa sổ theo dõi theo user; backend CUPS cài cấp hệ thống cần
chạy lại bộ cài bằng quyền quản trị để gửi F004 tên tài liệu của job thực thi.
Các máy khác cũng cần chạy bộ cài mới để bắt đầu báo queue.

COM stress follow-up: phát hiện stack canary loopTask khi ghép /status;
addr2line trỏ serverQueueSnapshot có buffer cục bộ 4097 byte, cộng frame WebServer
và metadata mới vượt stack 8 KiB. Chuyển riêng buffer snapshot này sang heap,
giữ khóa chỉ cho memcpy; lỗi cấp phát trả queue unavailable, không nhận điều khiển
bằng snapshot cũ. Check live đọc /status liên tiếp và kiểm tra uptime không giảm.

Sau sửa snapshot: 30 lượt /status liên tiếp không reset; COM uptime tăng liên tục,
USB connected và CAPT fresh, cloud idle/heartbeat200/claim204; chỉ hai reporter
thật Mac/Windows, Windows giữ nguyên một capture pending inspection.
