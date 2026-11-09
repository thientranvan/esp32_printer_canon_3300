# Bộ cài ba máy từ Internet — server 09/11/2026

Theo [contract remote install](esp32-all-stations-remote-install-20261109.md).
Thay bước bootstrap LAN/ticket quản trị bằng một request native cho mỗi trạm.

## Đã triển khai

`POST https://quanly.lophocbinhminh.com/api/print/v1/client/install/enroll`

- Public HTTPS, không user/session/ticket/bearer cũ. Đúng ba ID trong contract;
  trạm enabled thì mặc định chấp nhận, không cần ESP online.
- JSON <=1024 byte, chỉ nhận schema/station_id/client_id/verifier/client_os/name.
  Kiểm tra schema integer1, hex lowercase đúng độ dài, OS, tên UTF-8 1–96 byte
  không ký tự điều khiển. Không nhận user_id, URL hoặc quyền do client tự khai.
- 201 nếu tạo credential mới, 200 nếu credential đã có. Response theo contract,
  no-store, không cookie, không CORS; từ chối Origin và OPTIONS. Kiểm tra HTTPS
  từ FastCGI, không tin header X-Forwarded-Proto của người gọi.
- Trạm không hỗ trợ 404; disabled/revoked 403; verifier khác 409; invalid body422;
  quá lớn413; MIME sai415; quá tải429 + `Retry-After: 60`; lỗi tạm503.

## Identity, quyền và chống trùng

Dùng lại print_enrollment_identity, print_clients và guard transaction hiện có;
không migration mới. Cùng station/client ID dùng cùng challenge/salt/credential ID
và HMAC như ticket enrollment. Không đổi verifier/token khi cài lại. Máy 3 đã
đăng ký qua ticket cũ dùng cùng identity/verifier sẽ nhận lại đúng credential.
Client đã thu hồi không được hồi sinh; không đổi quyền theo client ID công khai.

Challenge lưu SHA256(ASCII(verifier)); chỉ lưu hash token, không lưu raw verifier
hoặc token. Principal station_client/user_id NULL chỉ có submit/status/receipt
đúng trạm và credential; không có heartbeat/claim/OTA/quản trị. Trạm disabled
chặn đăng ký và các API client hiện tại. Không tạo job, không renderer, không
đổi firmware, credential ESP, receipt hay artifact cũ.

Audit riêng `remote_install_enroll` khi tạo credential, không giả danh admin:
payload chỉ có station ID/client ID/principal type, không có bí mật. Không log
body/response credential. Hạ tầng APM/proxy bổ sung về sau cũng phải tắt capture
Authorization, body và response token cho các endpoint enrollment.

## Rate limit

Cửa sổ cố định60 giây: toàn tuyến180, IP60, trạm120, client/trạm12 requests.
Cho phép cài ba trạm nối tiếp và retry. Bucket hash-only, REMOTE_ADDR, không tin
header IP tự khai. Lỗi conflict/revoked cũng tính lượt. 429 không khóa trạm hoặc
đổi trạng thái credential. Không áp quota suốt đời20 client của tuyến ticket/LAN
lên remote-open: không cần nhờ admin dọn/duyệt client khi cài mới.

Đây là đăng ký **công khai**, không chặn người lạ tạo identity mới từ Internet.
Thu hồi một identity không chặn người đó tạo identity khác. Chính sách này đã
được chấp nhận trong contract; rate limit chỉ chống quá tải, không phải xác thực.

## Kiểm thử

```sh
node scripts/check_printer_remote_install.cjs
php scripts/check_printer_enrollment.php
php scripts/check_printer_client.php
php scripts/check_printer_station_principal.php
php scripts/check_printers.php
php scripts/check_migrations.php
```

Fixture SQLite/spool riêng: cả3 trạm offline không có chủ; 201/200; tương thích
ticket cũ; retry/request đồng thời cùng identity; scope/disabled/revoked/conflict;
validate/quá1024/CORS; 429/Retry-After; audit không chứa verifier/token; không có
print_job. Chưa thay thế test tranh chấp MySQL nhiều PHP workers hoặc cài/in vật lý.

Nginx/FPM hiện có đã route đúng `/api/print/v1/client/`; không cần đổi cấu hình
hay bật công tắc. Server chỉ triển khai endpoint; bộ cài nằm phía client theo
contract. Không tự cài lên Windows/Mac/Ubuntu hoặc gửi lệnh in nghiệm thu.

## Nghiệm thu còn lại

Máy sạch ở mạng ngoài cài đủ3 queue không prompt; reinstall giữ identity/driver;
in giấy thật mỗi trạm; LAN trùng IP nhưng sai station ID không được gửi CAPT;
retry upload/mất response không in lại; OS không báo thành công giả khi API lỗi.
Chưa xác nhận các ca này chỉ từ kiểm thử server.
