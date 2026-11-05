# HTTP/TLS firmware 2026110501 — 05/11/2026

## Bằng chứng và giới hạn kết luận

Đọc docs/esp32-network-server-findings-20261105.md từ /var/www/lop1.
Nginx xác nhận Máy 2 khai Content-Length 2052 nhưng đóng khi chỉ nhận
285 byte header (400 trước PHP). Máy 1 có 499 sau ~1.5 giây khi upstream
chưa trả header. Tách PHP pool không giải quyết tất cả các lỗi này.
Không kết luận mọi 400 là cùng lỗi TLS hoặc mọi 499 do server chậm.

## Thay đổi

station/cloud_print.h dùng API native ESP-IDF esp_http_client_perform thay
vòng open/write/fetch_headers/read thủ công. POST field giữ nguyên bộ nhớ
đến khi synchronous perform kết thúc; ESP-IDF sở hữu gửi TLS/body và framing.
Callback gom response tối đa 8192 byte, hỗ trợ nhiều fragment; chỉ chấp nhận
khi perform=ESP_OK và is_complete_data_received=true. 204 không tự đọc thêm.
Lỗi setup/transport, thiếu body hoặc quá giới hạn trả -1; không xử lý JSON dở.

Mỗi handle vẫn thuộc một task. Callback context, userdata và POST pointer
được gỡ trước khi return. Broken connection bị close trước lần heartbeat kế.
Không thêm vòng retry POST claim/events trong ứng dụng: kết quả sau lỗi có
thể chưa rõ; heartbeat sau là snapshot mới có telemetry_seq mới. Giữ TLS CA,
không redirect, không giảm bảo mật hoặc tự reset USB/ESP khi mạng lỗi.

Heartbeat timeout 1500 -> 5000ms, dựa trên log valid body nhưng upstream
chưa trả sau 1.45s. Task telemetry độc lập nên không chặn USB/local controls.
Đây là timeout I/O SDK, không hứa deadline tổng DNS/TLS tuyệt đối 5 giây.
Không giữ Ready giả: một request lỗi vẫn cập nhật cloudOnline=false.
Không thay đổi ngưỡng stale/offline server trong bản này.

## Diagnostic bổ sung trong /status.cloud và heartbeat.cloud

- heartbeat_duration_ms: thời gian request gần nhất (millis monotonic).
- heartbeat_failures: số heartbeat không thành công liên tiếp; 200/204 đặt 0.
- heartbeat_diagnostic: lỗi gần nhất, không xóa khi hồi phục. Gồm HTTP code,
  phase (open/connected/headers_sent/response_headers/read/finish), setup,
  perform (esp_err_t), errno, response bytes, overflow và free heap.
  Với HTTP status thật, diagnostic có thể chỉ có HTTP code.

Không có body/token/password/URL ký trong diagnostic. `headers_sent` chỉ
là mốc event cuối, không chứng minh body đã gửi hoặc chưa gửi; dùng log byte
nginx để đối chiếu. duration loại khỏi key so sánh telemetry để không tự tạo
vòng gửi nhanh chỉ vì mỗi request có latency khác nhau.
Dev giữ các trường mới khi lưu heartbeat, có thể hiển thị lỗi gần nhất cùng
số lần lỗi liên tiếp; không dùng lỗi lịch sử làm trạng thái lỗi hiện tại.

## Kiểm thử

check_cloud_http.py chạy production transport với HTTP giả: POST 2052 byte,
fragmented response, 204/503, giới hạn 8192, truncated body, setup/perform lỗi,
đóng stream lỗi, gỡ pointer trên reused handle, không POST replay và mã HTTP
heartbeat/claim/events độc lập. check_cloud.py, check_cloud_recovery.py,
check_paper_wait.py, check_firmware_update.py đạt. Build public loại secret.

Kiểm thử mô phỏng không tái tạo RF/TLS thực. Cần ghi version, uptime và log
nginx sau nạp; theo dõi ít nhất 15–30 phút và khi tải web/render. Nếu tái diễn,
đọc diagnostic mới và request_id/timing nginx; không tuyên bố hết lỗi chỉ vì
vài phút HTTP200. Không phát lệnh in trong phép thử transport.

## Nạp thực tế và quan sát ban đầu

Đã OTA Máy 1 và Máy 2 lúc rảnh; cả hai đọc lại 2026110501, reset_reason=3
(do OTA), Wi-Fi/USB nối lại, CAPT fresh và heartbeat200. Server DB giữ đúng
version cùng heartbeat_diagnostic/duration/failures mới. Không gửi lệnh in thử.
Một số lần upload bị khóa admission khi trạm chưa lấy được quyền USB/claim;
server trả cập nhật thất bại, firmware cũ giữ nguyên; không bypass khóa.
Sau khi xác nhận lệnh người dùng đã in xong, nạp thành công.
Log nginx sau 11:22:35 UTC: mẫu đầu 97 heartbeat Máy 2 và 93 Máy 1 đều200,
request_time tối đa lần lượt0.150s/0.246s; chưa có400/499 trong mẫu này.
Đây chỉ là quan sát ngắn ban đầu, không phải nghiệm thu15–30phút.
Máy 3 không truy cập được trong LAN; chưa nạp trực tiếp bo này.
