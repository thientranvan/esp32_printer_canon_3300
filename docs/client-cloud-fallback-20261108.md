# LAN mất kết nối: gửi CAPT đã render qua web server

## Bàn giao 08/11/2026

Client Windows/Mac/Ubuntu đã có source và bộ kiểm thử. Chưa nghiệm thu đầu-cuối:
server phải thêm API và cấp cấu hình client bên dưới trước khi bật fallback.
Không sửa application/database/service trên server trong lượt bàn giao này.
ESP0505 hiện có nhận espjob-v1; không cần nạp firmware mới chỉ để nhận tuyến này.

**Không gọi renderer server.** Windows driver Canon, filter Canon trên Mac hoặc
filter Ubuntu hiện tại render tại máy gửi. Backend đóng gói lại parameters/bands
đã nén thành espjob-v1; không giải nén, raster lại, đổi chất lượng hay bố cục.
Giữ copies/collate, short/long-edge và raster đã xoay/N-up từ driver/filter.
Giới hạn hiện hành: A4/cassette1,100ảnh logical,99copies,4096bands,4MiB/artifact.
Quá giới hạn phải trả lỗi rõ, không tự chia job hoặc render lại trên server.

## Chuyển tuyến và chống trùng

1. Có cloud.json đúng station thì thử kết nối LAN9100 tối đa5giây.
2. Connect thất bại trước khi gửi byte nào: lưu receipt ở máy gửi, PUT artifact.
3. Khi LAN đã connect và bắt đầu giao thức: không chuyển cloud trong phiên đó.
   Đã in một phần/mất LAN thì giữ cơ chế recovery LAN/đối soát, không upload bản sao.
4. Job đã chọn cloud luôn tra/PUT lại cùng resource ID+SHA256 kể cả khi LAN trở lại.
   Receipt được ghi trước request. Upload mất response không tạo job mới.
5. Giữ receipt completed gần nhất để backend/CUPS retry sau completion không tự
   quay sang in LAN. Job mới khác spool ID vẫn ưu tiên LAN. Hàng đợi serialize;
   không tuyên bố exactly-once giấy vật lý qua mọi dạng mất điện/mất ổ đĩa.
6. HTTP transient có tối đa2retry ngoài request đầu, cùng ID/body; lỗi auth/conflict/
   validation dừng và giữ receipt/capture, không in lại mù. Mac CUPS có thể cần
   Resume sau lỗi mạng kéo dài; Windows bridge restart tiếp tục capture cloud còn giữ.

CUPS job-ID/options và hash artifact tạo client-ID ổn định. Windows dùng tên
capture duy nhất và hash. Receipt cũ/khác job chưa giải quyết chặn job mới.
Không dùng điểm tiến trình client để cập nhật trạng thái ESP/server.

## Cấu hình/quyền client

Server UI sau đăng nhập: nút “Cấp cấu hình máy tính” cho đúng trạm và user có quyền.
Xuất JSON (<=4096byte), tên `<station_id>.cloud.json`:

```json
{"schema":1,"station_id":"esp32-1051db78a8fc","token":"CLIENT_SCOPED_SECRET_FROM_SERVER"}
```

Token ngẫu nhiên base64url32..256ký tự, ít nhất256bit entropy; lưu hash server,
thu hồi được, ràng user/client/station và kiểm tra quyền hiện tại ở mỗi request.
**Không dùng token ESP, không cho token client heartbeat/claim/OTA/config trạm.**
Scope: gửi prepared job, đọc job do client này gửi, đọc trạng thái ESP của trạm.
Không ghi token vào log/access URL, không commit JSON có token vào Git.
Cấu hình không nhận tùy ý origin: HTTPS cố định quanly.lophocbinhminh.com.
Không redirect mang Authorization sang host khác; xác minh TLS bình thường.
Thu hồi token phải trả401/403, không tự chuyển LAN nếu job đã chọn cloud.

- EXE Windows: nút “Cấu hình in qua web…” chọn JSON, rồi Cài đặt/Cập nhật.
  PS1: thêm `-CloudConfig C:\path\station.cloud.json`, hoặc đặt file đúng tên
  cạnh PS1. File nằm ProgramData\ESP32Printer\Stations\<id>\cloud.json,
  kế thừa ACL SYSTEM/Administrators hiện có; không đưa token cho status UI.
- Mac: `sudo sh install_mac.sh IP "Tên máy" /path/station.cloud.json`;
  hoặc file đúng tên cạnh installer. Lưu root-only0600 ở cache/<station_id>.
- Ubuntu: `sudo python3 install_ubuntu.py IP "Tên máy" --cloud-config FILE`.
- Máy đã cài backend mới: `sudo python3 configure_cloud.py FILE` (Windows chạy
  bằng Python với quyền quản trị). Không ghi đè token khi có receipt chưa đối soát.
  Không đổi máy in, driver, Wi-Fi Mac hoặc cấu hình ESP khi kích hoạt fallback.

## API server cần triển khai

Base `https://quanly.lophocbinhminh.com/api/print/v1/client/stations/{station_id}`.
Bearer client, không session cookie. JSON response<=64KiB, Cache-Control:no-store.
Status export không bao giờ chứa token/user credentials/support commands.

### PUT /jobs/{client_job_id}

`client_job_id` khớp `client-[a-f0-9]{48}`. Header:

```
Content-Type: application/vnd.esp32.espjob
X-Content-SHA256: <64 lowercase hex>
X-Print-Title-Base64: <UTF-8 title, <=384 bytes before base64>
X-Print-Client-OS: windows | macos | linux
Authorization: Bearer <client token>
```

Body chính là bytes ESPJOB1, không multipart/base64/PDF/HTML/raw arbitrary CAPT.
Đọc stream tối đa4194304byte +1, kiểm tra content length nếu có, SHA256 và cấu
trúc. Dùng **printerValidateArtifact() sẵn có ở app/printer_repository.php**;
không viết validator khác. Chỉ cho normalized cassette1 (parameter byte40=1).
Không tin total/copies/options header từ client; lấy từ binary đã validate.
Giới hạn quota/job/user và hàng đợi như web, trả429 khi đầy; không bắt buộc
station phải online/ready để nhận job vào queue khi có quyền gửi.

Transaction/atomic storage:
- Unique(station_id,client_job_id), gắn owner client credential+user; không namespace
  lại theo token mới để tránh reissue token tạo bản in thứ hai cùng logical ID.
- Cùng identity/hash trả lại receipt/job hiện có kể cả completed/cancelled/failed;
  không đổi queued, không gọi render, không cấp lease lại chỉ vì PUT trùng.
- Khác hash/owner/station trên ID đã có trả409, không chạm artifact cũ.
- Lưu tempfile0600 ngoài webroot trong printerStorage(), fsync+rename, rồi publish
  queued chỉ sau artifact hợp lệ và metadata commit. Dọn orphan file khi rollback.
  Concurrent PUT cùng ID chỉ thắng một transaction; không overwrite file của job
  đang được claim. Không “upsert status=queued” trên duplicate.
- Lưu mapping/tombstone id/hash/outcome ngay cả khi xóa artifact để PUT cũ không
  in lại. Nếu mapping đã expired thì410, không tái tạo ID đó. Không xóa dedup
  tombstone khi người dùng chỉ xóa lịch sử hiển thị.

Đưa trực tiếp vào luồng **queued + artifact_bytes/artifact_sha256**, document_type
`client_prepared`, không document nội bộ và không HTML. Bổ sung schema nếu cần.
Reuse printerArtifact()/printerClaim()/printerEvent(), lease/download chuẩn hiện có,
printerPublish() để cập nhật UI. **Không gọi printerCreateJob()** vì hàm đó bắt đầu
ở rendering, đọc exercise/answers/outline, giới hạn layout theo renderer cũ và
kiểm tra printerRendererReady(). Không áp các hạn chế N-up/landscape của renderer
web lên raster client đã render hợp lệ. Không gửi tham số layout để ESP xoay lần2.

Trả201 lần đầu;200 khi duplicate. Response luôn có đủ identity/hash, ví dụ:

```json
{"schema":1,"station_id":"esp32-1051db78a8fc","client_job_id":"client-<48hex>","sha256":"<64hex>","state":"queued","progress_known":false,"total":7,"completed":0,"error":null}
```

`state`: queued,claimed,downloading,printing,waiting_printer,paused,uncertain,
completed,failed,cancelled. Map leased nội bộ sang claimed. resolve không xác minh
đủ giấy phải uncertain/failed, không map sang completed. Chỉ completed sau ESP
xác nhận kết thúc; upload xong/download xong không phải đã in.

### GET /jobs/{client_job_id}

Cùng response, kiểm tra token owner và station. Không cho client xem tài liệu/job
của user khác.404 không tự tạo lại GET hoặc đổi ID. HTTP401/403 auth,409 hash/
identity conflict,410 expired tombstone,413 oversized,415 format,422 invalid binary,
429 quota,5xx temporary. API chưa có404 nghĩa là chưa sẵn sàng, giữ capture client.
Không trả HTML đăng nhập hoặc redirect302 cho API.

### GET /status

Response tương thích `/printer/status` của ESP, thêm `station_id`, trạng thái từ
heartbeat ESP mới nhất. Field connected là **USB máy in**, không phải Wi-Fi:

```json
{"schema":1,"station_id":"esp32-1051db78a8fc","connected":true,"responsive":true,"capt":{"fresh":true,"paper_required":false,"paper_jam":false,"cover_open":false,"service_error":false,"service_code":"","toner_warning":false},"job_control":{"id":"job_x","state":"printing","active":true,"total":7,"completed":2,"progress_known":true},"active":true,"total":7,"completed":2}
```

Không có heartbeat mới/station offline: connected=false,responsive=false,
capt.fresh=false, progress chưa biết giữ null/false theo contract nguồn trạng thái;
không suy ra Ready từ queue rỗng. Không thêm reporter từ PC: PC chỉ GET trạng thái.
Mac/Windows root bridge lấy LAN trước, server khi LAN thất bại; status UI chỉ đọc
cache không bí mật, hết hạn10giây. Client job GET mỗi2giây; không hứa realtime
subsecond ở tuyến fallback này. Server UI tiếp tục SSE chuẩn đang có.

## Hủy/giữ lệnh và giới hạn bản đầu

Lệnh đang ở server được quản lý bằng UI web và command ESP sẵn có. Xóa/giữ lệnh
trên hàng đợi OS không bảo đảm hủy file đã upload; không được báo “đã hủy vật lý”
chỉ vì đóng spool/kill bridge. Có thể còn tờ đã kéo. Chưa thêm dịch vụ RPC điều
khiển cloud từ status UI; không mở quyền token ra desktop để làm tắt đường bảo mật.
Sau server báo failed/cancelled/uncertain, receipt/capture giữ lại để đối soát.
Chỉ xóa/archived local receipt sau khi xác nhận outcome server; không tự chuyển
lại sang LAN. Không triển khai remote claim từ PC, không yêu cầu PC khác bật.

## Nghiệm thu dev server

1. Tạo token client đúng trạm; user không quyền trạm khác phải403.
2. Tắt riêng đường LAN từ máy gửi (không tắt ESP Internet), in Word/PDF. Nhận đúng
   ESPJOB1 đã render tại client, job queued, không tiến trình renderer server chạy.
3. So SHA256 artifact client/server/claim; duplex long/short,N-up,landscape,copies
   giữ đúng; ESP và giấy thật hoàn tất mới đánh dấu completed.
4. Ngắt upload response sau transaction, PUT lại cùngID/hash: đúng một job, không
   thêm giấy; cùngID khác hash409; concurrent PUT một job.
5. Chạy lại backend sau cloud completed nhưng OS chưa ACK: tra cùng cloudID,
   không in lại LAN khi mạng vừa phục hồi. Job mới vẫn ưu tiên LAN.
6. Ngắt LAN giữa truyền raster: client không upload bản sao cloud.
7. Test revoked token, HTML302, file quá4MiB, malformed bands, stale heartbeat,
   cancelled/uncertain, server restart, client restart và token rotation.
8. Xác nhận bộ cài Windows EXE/PS1, Mac, Ubuntu trên thiết bị thật trước nghiệm thu
   toàn tuyến. Test hiện tại chỉ production-code fake transport/serialization,
   C++ Prepared::parse, Mono bridge self-test; chưa thay thế ca in vật lý.
