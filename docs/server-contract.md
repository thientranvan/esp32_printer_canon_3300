# Contract ESP32–quanly.lophocbinhminh.com, v1

> Bổ sung dữ liệu cửa sổ trạng thái, hàng đợi OS và capability: [status-parity.md](status-parity.md).

> Cập nhật 22/10/2026: Máy 1 đã nạp firmware qua COM, cloud bật và heartbeat HTTP 204.
> Đã xác minh server lưu lỗi máy thật **E100 0000**. Chưa nghiệm thu cloud đầu-cuối
> với job thực sau cập nhật này. Trường lỗi/progress và phần UI server cần bổ sung:
> [trạng thái CAPT](printer-status.md). Ghi chú “chưa nạp” trong bản bàn giao server
> bên dưới là lịch sử, không phải trạng thái hiện tại.

## Phạm vi và trạng thái

Firmware đã có HTTPS heartbeat, claim/download, kiểm tra SHA-256, chạy CAPT chuẩn hóa,
USB arbiter chung với LAN, journal NVS chống tự in lặp. Mặc định cloud **tắt**.
Đã có renderer PDF Ubuntu bản đầu trong `server-renderer/`; trang thử đã in ra chữ đầy đủ.
Endpoint/dashboard/worker đã có trên server theo docs bàn giao; chưa có kiểm thử cloud đầu-cuối.
Đã truy cập SSH `binhminh` và bàn giao tài liệu tại
`/var/www/lop1/docs/esp32-printer-contract.md`; source renderer tại
`/var/www/lop1/tools/esp32-renderer`. Chỉ thêm tài liệu/công cụ độc lập; không sửa mã
ứng dụng, database, dịch vụ, cấu hình hoặc thay đổi chưa commit của dev server.
Contract độc lập framework.

Source ESP: https://github.com/thientranvan/esp32_printer_canon_3300
Các đường dẫn `station/*`, `prepare_cloud_job.py`, `check_cloud.py` trong tài liệu
thuộc repo ESP này, không phải repo ứng dụng `/var/www/lop1`.

Ba bo dùng cùng source, cấu hình riêng qua trang quản trị:

| Tên | IP dự kiến | ID |
| --- | --- | --- |
| Máy 1 | 192.168.1.199 | `esp32-<12 ký tự MAC lowercase>` của bo 1 |
| Máy 2 | 192.168.1.200 | ID của bo 2 |
| Máy 3 | 192.168.1.201 | ID của bo 3 |

ID không đổi khi đổi tên/IP; server dùng ID làm khóa, không dùng IP làm danh tính.
Dành các IP trên router để tránh DHCP cấp trùng. IP là địa chỉ LAN của ESP,
**không phải** địa chỉ để VPS kết nối. Không mở port router, không công khai TCP 9100.

## Luồng in từ web

1. Người dùng đăng nhập web, chọn máy in, tài liệu và tùy chọn in. Server kiểm tra quyền
   theo trạm và giới hạn tài liệu, tạo job với idempotency key do trình duyệt gửi.
2. Worker trên server render tài liệu thành raster CAPT LBP3300, áp dụng N-up,
   ngang/dọc và xoay mặt sau theo cạnh lật, rồi tạo file `espjob-v1` bên dưới.
3. Chỉ job đã render/xác minh xong, có kích thước và SHA-256 cuối cùng mới vào `queued`.
4. ESP chủ động gọi HTTPS ra server, claim một job, tải toàn bộ vào PSRAM và kiểm tra
   trước khi chạm USB. Không cần thiết bị của người dùng nằm trong LAN máy in.
5. ESP giữ USB độc quyền cho cả job; hàng đợi Windows/Mac vẫn in trực tiếp LAN,
   không cần server. Khi LAN đang in, cloud chờ lượt tiếp theo.
6. Server lưu trạng thái/tiến độ, trình duyệt nhận qua polling/SSE/WebSocket tùy ứng dụng.

**Renderer bắt buộc chạy phía server.** ESP không đọc Word/PDF/HTML/JPEG,
không chạy driver CAPT gốc. TCP 9100 và endpoint tải file không biến PDF thành CAPT.

`server-renderer/render.py` đã chạy trên Ubuntu 22.04 x86_64 của VPS: PDF →
Poppler → raster Hi-SCoA → `espjob-v1`, không cần máy Windows/Mac thường trực.
Đã kiểm tra parser firmware, giải nén đối chiếu từng band và in thực tế một trang
qua LAN; người dùng xác nhận giấy/chữ đầy đủ ngày 2026-10-22.
Xem `server-renderer/README.md` để build, chạy không-root và yêu cầu sandbox.

**Giới hạn renderer cloud hiện tại:** PDF bố cục A4 dọc, một mặt, cassette 1,
1–42 trang, copies 1–99, tối đa 4 MiB đầu ra. Chưa có tham số duplex/N-up/ngang;
API phải từ chối tùy chọn chưa hỗ trợ, không âm thầm in sai. Bước 2 mô tả đích đầy đủ,
không khẳng định bản đầu đã hỗ trợ tất cả. Driver LAN Windows/Mac hỗ trợ nhiều tùy chọn
là luồng khác. Ứng dụng phải xuất Word/HTML/ảnh thành PDF trước.
Worker server đã nối renderer; còn đăng ký thiết bị, nạp firmware và nghiệm thu HTTPS đầu-cuối.

`prepare_cloud_job.py input.capt output.espjob` chuyển **capture đã render bởi driver
Canon Windows** sang định dạng ESP; không render tài liệu đầu vào. Script từ chối ghi đè
file đích. Đầu ra giữ metadata copies/collate/duplex; renderer phải xoay ảnh mặt sau đúng.

## Xác thực và giao vận

Origin cố định: `https://quanly.lophocbinhminh.com`.
Base path: `/api/print/v1/stations/{station_id}`.
Firmware xác minh CA/hostname bằng ESP-IDF certificate bundle, cần NTP trước HTTPS.
Không theo redirect, không nhận URL tải tùy ý từ server. Cấu hình reverse proxy phục vụ
đúng origin/path, không chuyển ESP qua màn hình đăng nhập, Cloudflare challenge hoặc URL khác.

Tất cả request thiết bị:

```http
Authorization: Bearer <token-riêng-của-trạm>
Accept-Encoding: identity
Content-Type: application/json
```

Server cấp token ngẫu nhiên base64url 32–96 ký tự (`A-Z a-z 0-9 _ -`), mỗi bo một token;
chỉ lưu hash token phía server, ràng buộc token với đúng station ID. Admin dán token
vào cấu hình ESP và bật cloud. Không đưa token vào Git, URL, log hoặc JavaScript trình duyệt.
Token thiết bị chỉ được heartbeat/claim/download/events của chính trạm, không có quyền
quản trị hoặc đọc tài liệu trạm khác. Người dùng web dùng cơ chế đăng nhập của ứng dụng.

JSON UTF-8, body phản hồi tối đa 4096 byte. Timestamp online dùng đồng hồ server,
không dựa vào `uptime_s`. Heartbeat/claim khoảng 5 giây; lỗi mạng backoff 5–30 giây.
Trong tải/in cũng heartbeat ở các điểm chờ; timeout mạng có thể làm khoảng cách dài hơn.
Ngưỡng offline đề nghị **90 giây** kể từ request hợp lệ cuối cùng.

## Endpoint khớp firmware

### POST `/heartbeat` → `204 No Content`

Ví dụ body (thêm field tương thích được phép):

```json
{
  "schema": 1,
  "boot_id": "af127c9b42",
  "name": "Máy 1",
  "ip": "192.168.1.199",
  "uptime_s": 120,
  "printer": {
    "state": "busy_cloud",
    "ready_verified": false,
    "usb_connected": true,
    "usb_responsive": true,
    "error_code": 0,
    "capt": {"opcode": "a0a8", "frame_hex": "", "age_ms": null}
  },
  "lan": {"active": false, "total": 0, "completed": 0},
  "cloud": {
    "enabled": true, "state": "printing", "error": "", "http": 204,
    "job": "job_123", "progress_known": true,
    "total": 3, "submitted": 2, "completed": 1
  }
}
```

`printer.state`: `recovering`, `error` (USB error), `disconnected`, `unresponsive`,
`busy_lan`, `busy_cloud`, `idle`. `idle` chỉ có nghĩa không có phiên in đang hoạt động,
**không xác nhận đủ giấy, hết kẹt, có toner hoặc mọi cảm biến đều tốt**.
`usb_responsive` là phản hồi GET_DEVICE_ID, không phải xác nhận cơ cấu in sẵn sàng.

`capt.frame_hex` là toàn bộ frame A0A8 quan sát gần nhất, gồm header little-endian
opcode/length 4 byte; `age_ms` là tuổi mẫu. Idle cloud có truy vấn A0A8 khi chiếm USB
được; khi LAN in, lấy mẫu thụ động từ phiên LAN. Không coi mẫu cũ là trạng thái hiện tại.
Firmware chưa giải mã đầy đủ cờ kẹt giấy/hết mực/nắp mở; giao diện phải ghi “lỗi chưa
xác định” kèm mã/raw, không tự gán “thiếu giấy” chỉ vì timeout hoặc chọn khay sai.
Có thể bổ sung decoder sau khi có capture và kiểm chứng từng lỗi trên LBP3300.

`cloud.state`: `disabled`, `connecting`, `idle`, `server_error`, `downloading`,
`waiting_printer`, `printing`, `completed`, `failed`, `uncertain`, `resolved`, `blocked`.
`cloud.http` là mã HTTP gần nhất hoặc -1 lỗi giao vận, không phải mã lỗi máy in.

Tiến độ có đơn vị **ảnh/mặt in sau dàn trang**, đã tính copies; không phải trang tài liệu
Word gốc hay số tờ vật lý. Ví dụ 4 trang N-up 2, duplex: 2 ảnh in / 1 tờ.
`submitted` chỉ đã gửi engine; `completed` lấy counter A0A8 payload offset 20.
Không báo hoàn tất dựa trên tải xong/gửi xong. `lan.total=0` nghĩa chưa biết tổng khi
client cũ chưa gửi metadata. `progress_known=false` sau reboot: giữ số cuối server đã
thấy, không diễn giải completed=0 là chưa có tờ nào được in.

### POST `/jobs/claim` → `204` không có việc, hoặc `200` JSON

Request:

```json
{"schema":1,"max_bytes":4194304,"format":"espjob-v1"}
```

Response có việc:

```json
{
  "id":"job_123",
  "lease":"random_base64url_lease_32_chars",
  "format":"espjob-v1",
  "bytes":26284,
  "sha256":"f79621084e73a334f0749da0bd90af51afe2c39eca29e261d4d87e09c6ac7a83"
}
```

ID: 1–64 ký tự `A-Z a-z 0-9 _ -`. Lease: 16–128 ký tự cùng tập ký tự, ngẫu nhiên.
SHA-256: 64 ký tự hex lowercase. Bytes: số nguyên 16..4194304.
Claim atomically khóa đúng một job của đúng trạm, chỉ một lease chưa giải quyết/trạm.
Không giao job vượt khả năng ESP; trả lỗi render lên web trước bước claim.

**Lease không được tự hết hạn rồi tự đưa job sang queued.** Claim có thể đã tới ESP
và giấy có thể đã chạy, dù HTTP response/heartbeat sau đó mất. Nếu claim response bị
mất trước khi ESP lưu journal, server vẫn giữ job leased/uncertain, chờ đối soát admin.
Không tự claim lại cùng job bằng lease mới khi ESP đã có journal hoặc kết quả chưa rõ.

### GET `/jobs/{id}/data` → `200 application/octet-stream`

Thêm header `X-Print-Lease: <lease>`; kiểm tra cả token/trạm/job/lease.
Bắt buộc `Content-Length` chính xác, không chunked, không nén, không redirect.
Dữ liệu bất biến sau claim, tối đa 4 MiB. Tải có giới hạn 120 giây; hỏng checksum,
thiếu PSRAM hoặc parse lỗi không gửi USB, trả kết quả failed.

### POST `/jobs/{id}/events` → `204` sau khi transaction lưu xong

Thêm `X-Print-Lease`. Ví dụ:

```json
{"schema":1,"state":"completed","error":"","progress_known":true,"total":3,"completed":3}
```

State terminal từ ESP: `completed`, `failed`, `uncertain`, `resolved`.
Request lặp do ACK mất phải idempotent theo `(station_id, job_id, lease, state)`.
Cho phép `uncertain` chuyển `resolved`; không cho `completed` bị heartbeat cũ hạ cấp.
Không trả 204 trước khi lưu bền vững. Sau reboot ESP phát lại kết quả, không in lại.
Chấp nhận sự kiện của lease đã lưu dù trạm mất mạng lâu; token vẫn phải hợp lệ.

- `completed`: engine báo hoàn tất và đã kết thúc reservation; ESP lưu journal rồi gửi.
- `failed`: chưa chạm USB; thông báo nguyên nhân. Chỉ admin/người dùng tạo lần thử mới.
- `uncertain`: có thể đã gửi USB hoặc mất điện khi đang in. Không tự in lại, không xóa job.
  ESP giữ khóa cloud dù server đã ACK uncertain. LAN vẫn theo arbiter; admin phải kiểm tra
  job dở trong máy in trước khi gửi bất kỳ job mới.
- `resolved`: người vận hành đã kiểm tra giấy/công việc dở rồi dùng nút bỏ khóa tại trang
  ESP. Không in lại job này. Sau server ACK, ESP xóa journal và có thể claim job khác.

Download chỉ dùng PSRAM; journal NVS được ghi sau claim, **trước USB** và khi kết thúc.
Nếu lỗi ghi journal, trạm blocked và không tiếp tục. Không có cam kết exactly-once
về giấy vật lý khi mất điện; ưu tiên dừng để tránh bản in trùng không kiểm soát.
Hủy: chỉ hủy trước claim ở v1. Không triển khai remote cancel/resume khi giấy đã chạy.

Lỗi HTTP: 401/403 credential, 404 chưa triển khai/trạm/job sai, 409 conflict lease,
413 quá giới hạn, 429/5xx tạm lỗi. Không thay body lỗi bằng HTML hàng MB.

## Định dạng nhị phân `espjob-v1`

Tất cả số little-endian. Không nén thêm. SHA-256 tính trên toàn bộ file.

| Offset | Kiểu | Giá trị |
| --- | --- | --- |
| 0 | 8 bytes | ASCII `ESPJOB1` rồi LF (`45 53 50 4a 4f 42 31 0a`) |
| 8 | u16 | Số ảnh logical sau N-up, 1..100 |
| 10 | u16 | Copies 1..99 |
| 12 | u8 | Duplex 1 simplex, 2 long-edge, 3 short-edge |
| 13 | u8 | Collate 0/1 |
| 14 | u16 | Reserved = 0 |

Tiếp theo mỗi ảnh:

1. 64 byte parameters, nối frame `D0A0` (44 bytes), `D0A4` (12), `D0A1` (4), `D0A2` (4).
   Mỗi frame gồm header u16 opcode, u16 total length. Model D0A0 payload offset 2 = `0x2A94`.
2. u32 band_count 1..4096; tổng toàn job <=4096 bands để giới hạn RAM metadata.
3. Với mỗi band: u32 kích thước (>0), rồi đúng số byte raster CAPT đã nén bởi renderer.
4. Không có byte dư cuối file. Tổng file <=4 MiB, geometry A4 600 dpi 2480x3507 sau shift
   như driver Canon; cassette 1/plain. Khổ/media khác chưa hỗ trợ.

Gửi ảnh theo thứ tự tài liệu bình thường. ESP tự sắp mặt sau trước mặt trước cho duplex,
đổi side flags, xử lý trang lẻ và copies/collate. Không tự đảo mặt lần nữa trên server.
Duplex short/long có cùng thứ tự truyền; renderer phải tạo ảnh/xoay theo BindEdge đúng.
C0A4/trailer Windows không nằm trong phần bands. Bộ chuyển mẫu và validator:
`prepare_cloud_job.py`, `station/prepared_job.h`; engine: `station/cloud_print.h`.

## Phần dev server cần bàn giao

- Migration stations/jobs/attempts/events, đăng ký ba ID/token, quyền người dùng theo máy in.
- Worker render thật, kiểm tra duplex/N-up/font trên LBP3300; lưu artifact ngoài webroot,
  retention/xóa tài liệu theo chính sách ứng dụng. Không cho client upload CAPT tùy ý
  với quyền người dùng thông thường.
- Bốn endpoint đúng status/body/header ở trên, transaction claim và event idempotency.
- Trang máy in: station online/offline riêng với printer disconnected/error/idle,
  đang in LAN hay web, tiến độ có đơn vị, last_seen, lý do lỗi, job cần đối soát.
- Nút in kiểm tra quyền, báo render/queued trước khi ESP nhận; không hiển thị thành công
  chỉ vì người dùng bấm nút hoặc server nhận file.
- Test hai tab bấm cùng idempotency key, hai trạm song song, LAN tranh lượt, mất Wi-Fi,
  mất ACK completed, reboot giữa trang, USB ngắt và chọn sai khay; không tự in lặp.

Kiểm tra offline repo: `python3 check_cloud.py`, `python3 check_installers.py`.
Chưa thử ba bo thật đồng thời hoặc server cloud thật; không đánh dấu nghiệm thu trước
khi đủ renderer, API và lượt in vật lý từ mạng bên ngoài.

## Bổ sung ngày 23/10/2026

Xem [trạng thái trực tiếp và điều khiển job](realtime-control.md) cho telemetry task
độc lập, SSE, job ID/ACK, điều khiển tại checkpoint và adapter hàng đợi VPS.
Phần bổ sung thay giới hạn hủy trước claim của v1 đối với **client mới có capability**;
không biến cancel thành cam kết đã xóa trang trong bộ nhớ máy in. Giữ nguyên journal,
lease, kết quả uncertain và quy tắc không tự phát lại.
