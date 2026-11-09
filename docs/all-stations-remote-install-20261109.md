# Một bộ cài — ba máy in — không cần cùng LAN

## Quyết định sản phẩm đã được người dùng yêu cầu

Một lần chạy bộ cài trên Windows/Mac/Ubuntu thêm hoặc cập nhật đủ **Máy 1, Máy 2, Máy 3**. Không chọn IP, không đăng nhập, không chọn tài khoản chủ trạm, không tải JSON, không nhờ admin cấp ticket, không cần ESP cùng LAN hoặc đang online khi cài. Driver/render vẫn chạy trên chính máy gửi; server chỉ nhận ESPJOB1 rồi chuyển cho ESP. Mỗi hệ điều hành có bộ cài phù hợp, một bản dùng chung cho cả ba trạm.

Người dùng đã chấp nhận chính sách đăng ký công khai từ Internet cho ba trạm. Khóa client vẫn được tạo và lưu tự động để định danh, chống trùng/thu hồi; không tạo bước người dùng. Đây không phải cơ chế chặn người lạ đăng ký. Không nhúng bearer ESP, khóa dùng chung hoặc mật khẩu vào bộ cài.

| Tên mặc định | Station ID | IP LAN gợi ý |
|---|---|---|
| Máy 1 | esp32-98c377a136b0 | 192.168.1.199 |
| Máy 2 | esp32-1051db78a8fc | 192.168.1.200 |
| Máy 3 | esp32-1051db786a34 | 192.168.1.201 |

Danh sách ba trạm đã đóng gói trong bộ cài, chưa cần thêm API catalog. Danh tính trạm là station_id; không suy ra từ IP trên mạng khác.

## API cần dev server triển khai

**POST `https://quanly.lophocbinhminh.com/api/print/v1/client/install/enroll`**

Public HTTPS; không cookie, bearer cũ, ticket hoặc tài khoản chủ. Body JSON tối đa 1024 byte:

```json
{
  "schema": 1,
  "station_id": "esp32-1051db786a34",
  "client_id": "<32 lowercase hex>",
  "verifier": "<64 lowercase hex>",
  "client_os": "macos",
  "name": "Printer computer"
}
```

- Chỉ cho ba station_id trong bảng, trạm còn enabled, client chưa revoked. Mặc định bật luồng remote-open cho ba trạm theo yêu cầu; không chờ bật công tắc/chọn account. ESP offline vẫn cho cấp client.
- `schema` đúng integer 1; client_os windows/macos/linux; name 1–96 byte UTF-8, không ký tự điều khiển. Validate JSON/kiểu/độ dài trước giao dịch; không cho client tự chỉ URL/server hoặc quyền.
- Tái sử dụng identity `(station_id,client_id)` và cơ chế salt/HMAC/credential_id của enrollment hiện tại. Lưu challenge `SHA256(ASCII(verifier))`, không lưu raw verifier/token. Luồng ticket cũ và remote phải dùng chung identity để Mac đã cấp Máy 3 không bị trùng.
- Tạo hoặc trả lại **cùng credential** khi cùng identity/verifier. Nếu client_id tồn tại mà verifier khác: 409; đã thu hồi: 403, không tự hồi sinh. Tuyệt đối không đổi verifier/credential theo client_id công khai. Transaction và unique constraint bảo đảm hai request đồng thời chỉ tạo một client.
- Principal `station_client`, user_id null; quyền submit/status/receipt gắn đúng trạm/client, không cấp quyền quản trị. Dùng audit event riêng không chứa bí mật.
- Giới hạn tốc độ chống quá tải theo IP/client/trạm, cho phép ít nhất ba request nối tiếp/máy và retry; trả 429/Retry-After thay vì khóa trạm. Không biến quota thành bước phê duyệt thủ công. Không tạo/chạm print_job hoặc gọi renderer khi đăng ký.
- Không log request body/verifier/response token trong PHP/Nginx/APM. Không CORS cho web tùy ý; request này dành bộ cài native. TLS bắt buộc.

Response 201 lần đầu hoặc 200 khi cài lại, `Cache-Control: no-store`:

```json
{
  "schema": 1,
  "station_id": "esp32-1051db786a34",
  "client_id": "<same client_id>",
  "token": "<32..256 base64url characters>"
}
```

400/422 body sai; 403 trạm khóa/client thu hồi; 404 trạm không hỗ trợ; 409 verifier xung đột; 429 quá tải; 503 tạm lỗi. Không trả response lỗi chứa nội dung verifier. Bộ cài không in response credential ra console.

## Bộ cài đã đổi

- Windows: một EXE, nút **Cài đặt / Cập nhật cả 3 máy**; PowerShell mặc định cũng cài cả ba, không hỏi IP/tên. Driver CAPT tải một lần nếu thiếu.
- Mac: `sudo sh install_mac.sh`; Ubuntu: `sudo python3 install_ubuntu.py`. Lệnh cũ từ trang ESP có kèm IP vẫn mặc định cài cả ba. `--single` (Mac/Ubuntu), `-SingleStation` (PowerShell) giữ cho hỗ trợ kỹ thuật; không là luồng chính.
- Tạo/verifier ngẫu nhiên riêng từng trạm, root-only; máy cài lại giữ identity và credential đã có. Không cần đăng ký lại nếu đã có cấu hình hợp lệ. Chạy từng trạm, tổng hợp lỗi nếu chưa đủ ba, cho phép chạy lại; không tuyên bố thành công nếu server API chưa sẵn sàng.
- MAC dùng một backend chung, ba queue theo ID và cửa sổ trạng thái theo trạm. Windows có bridge riêng theo trạm để giữ hàng đợi độc lập; không cài ba bản driver.
- Cài lại theo ID, không tạo trùng. Không xóa hay gửi lại lệnh dở. Giữ các kiểm tra pending/receipt hiện có.

## Chọn đường gửi

Trước khi mở TCP in LAN, đọc `/setup/profile.json` và xác nhận đúng station_id. Trên mạng khác, .199/.200/.201 có thể thuộc thiết bị khác: không gửi CAPT vào đó. Không xác nhận đúng trạm hoặc không kết nối được trước khi gửi dữ liệu thì dùng cloud. Sau khi đã kết nối/gửi LAN không tự gửi lại cloud khi kết quả chưa rõ. Receipt/hash/idempotency chống trùng giữ nguyên. Job đã chọn cloud không đổi lại LAN khi mạng phục hồi.

API upload/status/receipt hiện có không thay đổi, server không render lại. Chỉ ESP báo telemetry; máy tính không đẩy trạng thái máy in lên server.

## Nghiệm thu bắt buộc

1. Máy sạch ở mạng ngoài: chạy một bộ cài, có đủ ba máy, không xuất hiện account/JSON/ticket/IP prompt. ESP có thể offline khi cài.
2. Cài lại và hai request đăng ký đồng thời: không tạo client/queue/driver trùng, không rotate credential đang dùng.
3. In thật một trang tới từng trạm từ mạng ngoài: render tại client, không renderer server, giấy xác nhận đúng; kiểm tra hai mặt/nhiều trang tiếp theo.
4. Mạng ngoài có thiết bị khác ở .199/.200/.201: không gửi một byte CAPT cho sai trạm; cloud nhận đúng station_id.
5. Mất phản hồi upload/retry/khôi phục LAN: cùng job không in trùng. Client bị thu hồi/trạm khóa không đăng ký lại bằng identity cũ.
6. Windows 10/11, Mac, Ubuntu: hàng đợi và cửa sổ trạng thái không báo thành công giả khi API từ chối.

Trước khi endpoint mới triển khai, không nghiệm thu luồng cài mới từ mạng ngoài. Máy 3 đã in thành công từ Mac qua client credential cấp riêng là bằng chứng relay/render; không thay cho nghiệm thu API đăng ký mở mới.
