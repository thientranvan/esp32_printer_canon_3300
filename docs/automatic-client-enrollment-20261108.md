# Tự đăng ký máy tính khi cài máy in LAN

Thay quy trình tải/import `.cloud.json` thủ công. Người dùng chỉ cài máy in, chọn IP/tên như bình thường. Installer tự đăng ký qua ESP trong LAN. ESP vẫn là nguồn telemetry duy nhất. Không render lại trên server; API nhận ESPJOB1 đã render trên máy gửi.

## API server cần triển khai

### POST /api/print/v1/stations/{station_id}/clients/enroll

Xác thực bằng bearer của chính ESP, qua HTTPS. Body JSON tối đa 512 byte:

```json
{"schema":1,"client_id":"<32 lowercase hex>","challenge":"<64 lowercase hex>","client_os":"windows","name":"Windows computer"}
```

`client_os`: windows/macos/linux. Tên 1–96 byte UTF-8, không ký tự điều khiển. ID/challenge đúng độ dài. Trả 200 hoặc 201:

```json
{"schema":1,"ticket":"<32..128 base64url characters>"}
```

Ticket ngẫu nhiên ít nhất 256 bit, TTL 60 giây, ràng buộc station_id/client_id/challenge; lưu hash ticket. Không log body/ticket/bearer. Giới hạn tần suất và số client theo trạm; 403 nếu chính sách không cho tự đăng ký. Trạm được phép tin cậy máy tính cài trong LAN; cần ánh xạ rõ sang tài khoản dịch vụ/chủ trạm đang hoạt động. Không tự gán tài khoản quản trị. Cấu hình chính sách một lần phía server, không yêu cầu người dùng tải JSON cho từng máy.

### POST /api/print/v1/client/enroll

Chỉ HTTPS, không cần bearer cũ. Body:

```json
{"schema":1,"station_id":"esp32-1051db786a34","client_id":"<32 hex>","ticket":"<ticket>","verifier":"<64 lowercase hex>"}
```

Kiểm tra `SHA256(ASCII(verifier)) == challenge` đã lưu với ticket; so sánh thời gian hằng. Kiểm tra TTL, policy và quyền hiện hành. Trả 200/201:

```json
{"schema":1,"station_id":"esp32-1051db786a34","client_id":"<32 hex>","token":"<32..256 base64url characters>"}
```

Token chỉ có quyền submit/status/receipt cho đúng trạm và client; không dùng token ESP. Server không khởi chạy renderer cho đường này. Dùng cơ chế client API hiện có.

Retry cùng client_id/challenge/verifier phải idempotent, kể cả mất phản hồi redemption. Cho phép lấy lại cùng credential trong cửa sổ retry hoặc cơ chế trả credential an toàn tương đương; không cấp nhiều client khi cài lại. Không cho đổi challenge/chiếm client chỉ bằng client_id công khai. Client đã thu hồi không tự hồi sinh qua retry cũ; quản trị viên phải cấp lại quyền. Giới hạn brute force và số ticket. Ticket sai/hết hạn trả 401/403; xung đột identity 409; quá tải 429. Mọi response `Cache-Control: no-store`.

## Firmware và bộ cài

Firmware 2026110801 quảng bá `client_enrollment_version:1`, có `POST /setup/enroll`. Chỉ nhận IP cùng subnet STA; yêu cầu header `X-ESP32-Enrollment: 1`, từ chối Origin, không mở CORS. Không nhận verifier/token trên LAN. Worker HTTPS riêng, một yêu cầu/20 giây, giữ kết quả 60 giây theo peer + identity. Trả 202 khi đang chờ; client lặp cùng body để lấy ticket. Chỉ đăng ký lúc trạm rảnh, không giành USB với job/OTA.

Installer tạo client_id 128 bit + verifier 256 bit bằng CSPRNG, lưu `enrollment.json` trong thư mục root/Administrators riêng của trạm. Giữ identity khi cài lại. Gửi challenge qua ESP; đổi ticket trực tiếp qua HTTPS; kiểm tra station/client binding; lưu `cloud.json` chỉ root/Administrators đọc. Không ghi credential vào log, GUI hoặc command-line. Giữ LAN hoạt động nếu API mới chưa có; cảnh báo cloud chưa kích hoạt, chạy lại installer sau khi server/ESP cập nhật. Cấu hình cloud hiện có giữ nguyên.

Chỉ fallback khi chưa kết nối TCP LAN và chưa gửi dữ liệu. Đã chọn cloud không tự đổi lại LAN cho cùng job. Giữ receipt/idempotency cũ; không in trùng khi retry. Không gửi telemetry từ máy tính lên server.

## Nghiệm thu server

1. Cài trên máy mới trong LAN, không tải JSON: chỉ sinh một client, credential không xuất hiện trong log.
2. Cài lại: giữ client_id, không sinh client/printer trùng. Mất phản hồi redeem rồi retry vẫn thành công.
3. Ticket bị chặn trên LAN không đổi được token nếu thiếu verifier. Sai trạm/challenge/verifier, ticket hết hạn và client bị thu hồi phải bị từ chối.
4. Chặn kết nối LAN của đúng client, in một trang: file ESPJOB1 đi qua client API, renderer server không chạy, ESP in và tự báo tiến trình.
5. Khôi phục LAN/retry cùng job: chỉ một bản giấy. Kiểm tra cả Windows, Mac, Ubuntu.

Chưa nghiệm thu toàn tuyến trước khi dev server triển khai hai endpoint mới. Không coi chỉ build/test converter là đã in thành công.
