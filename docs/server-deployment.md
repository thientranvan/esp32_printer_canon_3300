> Bản kéo từ `/var/www/lop1/docs/printers.md` ngày 22/10/2026.
> Ghi chú phía ESP sau đồng bộ: source đã sửa origin đúng và build đạt; người dùng
> yêu cầu nạp sau. Các mốc “firmware gốc” bên dưới mô tả trạng thái tại lúc dev server bàn giao.

# Quản lý máy in ESP32

## Trạng thái triển khai

Đã có quản lý máy, quyền theo máy, heartbeat, hàng đợi, claim nguyên tử, tải artifact,
event idempotent, menu in đề/đáp án/đề cương, worker và timer tự chạy sau reboot.
Đã nối `scripts/render_printer_job.mjs` với bộ PDF → CAPT tại `tools/esp32-renderer`
được người dùng xác nhận kiểm chứng in thật ngày 22/10/2026. Đã mở cờ renderer cho
PHP-FPM và worker. Chỉ nhận A4 dọc, một mặt, xếp theo bộ, 1–42 trang và 1–99 bản,
tối đa 4 MiB. Duplex/N-up/ngang/tắt collate bị từ chối ở API và ẩn ở giao diện;
không âm thầm đổi tùy chọn.

Luồng HTML → PDF → CAPT → hàng đợi/claim đã kiểm tra trên server bằng tài khoản
`www-data`, gồm cấu hình systemd có NoNewPrivileges/PrivateTmp/MemoryMax 768 MiB.
**Chưa nghiệm thu cloud đầu-cuối với ba bo:** chưa đăng ký ID/token của ESP thật;
firmware gốc còn ghi nhầm tên miền. Test tích hợp không gửi giấy ra máy thật.

Trang: `index.php?page=printers`. Quyền mới `printers` trong nhóm Hệ thống.
Admin/người có quyền quản lý được cấu hình máy; giáo viên cần quyền `exercises`
và được cấp đúng máy. Người có quyền quản lý máy và quyền Bài tập dùng mọi máy.

## Cấu hình ESP32

1. Lấy ID thật `esp32-<12 ký tự MAC lowercase>` từ mỗi bo. Thêm máy trên web,
   đặt tên Máy 1/2/3, cấp giáo viên. Không dùng IP làm ID.
2. Sao chép token chỉ hiển thị một lần vào ESP; server chỉ giữ SHA-256.
   Không gửi token qua URL, log, Git hoặc mã JavaScript. Khi cấp lại token phải cập nhật ESP;
   không cấp lại khi máy còn giữ job chưa đối soát.
3. Origin đúng của hệ thống là `https://quanly.lophocbinhminh.com`.
   Contract gốc viết `lophochinhminh.com`: phải sửa cấu hình/firmware nếu đang cố định tên đó.
   TLS cần CA/NTP hợp lệ. Bật cloud trên từng ESP. Không mở port router.
4. Dùng bốn endpoint trong [contract](server-contract.md).
   Include `deploy/nginx-printers.conf` trong HTTPS server block; kiểm tra nginx trước reload.

Online: request hợp lệ trong 90 giây theo giờ server. Poll giao diện 5 giây.
Online khác với máy in sẵn sàng. USB idle không chứng minh đủ giấy/mực hoặc hết kẹt;
`ready_verified=false` là giá trị firmware hiện tại, không tự coi đó là lỗi.
Lỗi chưa giải mã hiển thị mã/raw CAPT, không đoán là hết giấy.
Máy offline, USB lỗi, đang bận hoặc giữ job chưa rõ kết quả không được chọn.

## Renderer bắt buộc

Worker gọi executable cố định, không chạy shell hoặc nhận lệnh từ trình duyệt:

```text
/var/www/lop1/scripts/render_printer_job.mjs /var/lib/lop1-printers/job_<id>.render.json
```

JSON đầu vào:

```json
{
  "schema": 1,
  "input_html": "/private/job.safe.html",
  "document_type": "exercise",
  "output_espjob": "/private/job.espjob",
  "options": {"copies":1,"duplex":1,"collate":true,"nup":1,"orientation":"portrait"}
}
```

Worker loại script/event handler khỏi snapshot, nhúng logo/ảnh trước render.
Ảnh ngoài dùng helper kiểm tra DNS/IP công khai, pin IP, HTTPS xác thực, không redirect,
giới hạn byte/kích thước/timeout. Ảnh mất làm job lỗi, không in thiếu.
Công thức dùng Temml cục bộ, giữ header/footer/watermark và dàn trang đề hiện có.
Đề cương không chạy script dàn câu hỏi để tránh bỏ nội dung.

Chrome qua `scripts/printer_chromium.sh`: bubblewrap cách ly mạng/process, chỉ thấy
thư viện/font/profile tạm, không thấy file ứng dụng hay credential. CSP cấm script
từ tài liệu; DevTools chỉ chạy Temml và script dàn trang cố định của hệ thống.
Không chạy root, không fallback bỏ sandbox. Poppler dùng sandbox của bộ renderer.
Timeout tổng 720 giây, service 750 giây. Quá giới hạn báo lỗi, không tự chia job.

PHP kiểm tra envelope `espjob-v1`, copies/duplex/collate, kích thước và SHA-256
trước queued; compressor giải nén so byte từng band. Validator giữ trần firmware
100 ảnh/4096 bands; renderer hiện chỉ nhận 42 trang.

`APP_PRINT_RENDERER_VERIFIED=1` trong PHP-FPM pool và `/etc/lop1-printers.env`.
Đường dẫn mặc định là adapter ở trên; dùng `APP_PRINT_RENDERER` nếu thay adapter
đã kiểm chứng tương thích. Hai script adapter/Chromium wrapper cần mode 0755.
Giữ binary `tools/esp32-renderer/capt-compress` đã build và source/license đi kèm.

## Vòng đời và chống in trùng

`rendering` → `queued` → `leased` → `completed` / `failed` / `uncertain`.
`uncertain` chỉ sang `resolved` khi người vận hành kiểm tra giấy rồi bỏ khóa tại ESP.
Không tự hết hạn lease, không tự requeue, không tự in lại job đã claim.
Mất response claim vẫn giữ khóa server: cần đối soát, không dùng token mới để né khóa.
Mất ACK event được gửi lại, không ghi đè kết quả cuối hoặc reset tiến độ.
`completed` phải có counter hoàn tất đủ số mặt in; tải xong/gửi USB xong không phải in xong.
Không cam kết exactly-once về giấy vật lý khi mất điện.

Trình duyệt giữ idempotency key trong sessionStorage sau lỗi mạng. Gửi lại cùng payload
trả cùng job; cùng key nhưng payload khác bị từ chối. Hủy chỉ trước claim.
Render lỗi kết thúc `failed`, người dùng tự tạo lệnh mới sau khi sửa lỗi.
Artifact và HTML ngoài webroot, quyền 0600, thư mục 0700, mặc định
`/var/lib/lop1-printers`, owner `www-data`. Xóa file terminal sau 7 ngày;
job đang giữ/uncertain giữ nguyên để đối soát. Metadata/audit không xóa.

Worker: `deploy/lop1-printers.service` và `.timer`. Timer chạy lại 5 giây sau mỗi lần
kết thúc, khởi động sau reboot. `flock` ngăn hai renderer trùng nhau.
Render đang dở được làm lại trước claim; job leased không bao giờ làm lại tự động.

## API web

`printer_api.php?op=list|jobs` GET: session staff; danh sách giới hạn theo quyền.
`op=print|cancel` POST JSON: same-origin, header `X-CSRF-Token` lấy từ `list`.
`print` nhận `station_id`, `document_type` (`exercise`, `answers`, `outline`),
`document_id`, `idempotency_key`, các tùy chọn như trên. Trả 202 job, không báo đã in xong.
`cancel` nhận `id`. Token thiết bị không có quyền dùng API web.
Quản lý máy dùng POST form CSRF, token mới chỉ hiện trong HTML sau lưu, không đưa vào JS.
Audit có tạo/sửa máy, cấp lại token (không ghi giá trị), tạo/hủy lệnh in.
Chưa tính lệnh nhận vào hàng đợi là lượt in vật lý trong hiệu quả giáo viên.

## Kiểm tra

```sh
php scripts/check_migrations.php
php scripts/check_printers.php
node scripts/check_printers_web.cjs
node scripts/check_exercise_page_browser.cjs
runuser -u www-data -- php scripts/check_printer_render.php
```

Test repository/HTTP dùng fixture giả chỉ kiểm tra parser/giao vận. Test render dùng
CAPT thật, DB/spool riêng, không gửi tới ESP thật.
Kết quả 22/10: 46 kiểm tra repository, migration, HTTP/UI máy in và UI bài tập qua.
Mẫu một trang khoảng 6–8 giây; tạo job đề 4 trang qua worker đến queued/claim
khoảng 12–34 giây trong các lượt thử, không phải cam kết tốc độ mọi đề.
`php scripts/check_web_http.php` còn dừng tại kiểm tra lịch sử điểm danh
“Buổi điểm danh gần đây vẫn hiển thị theo lớp”, trước phần kiểm tra in; chưa xác định
nguyên nhân trong phạm vi triển khai máy in.
Còn phải nghiệm thu cloud vật lý: ba bo đồng thời; LAN tranh USB; mất Wi-Fi/ACK;
reboot giữa trang; USB lỗi/khay sai. Duplex/N-up/ngang chỉ mở sau khi bổ sung và kiểm chứng.
Chỉ nghiệm thu đầu-cuối sau lượt in từ mạng ngoài vào máy thật.
