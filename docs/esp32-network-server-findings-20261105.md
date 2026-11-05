# Bắt lỗi HTTP trực tiếp và cô lập FPM máy in

Ngày 05/11/2026, giờ log UTC (giờ Việt Nam cộng 7).

## Bằng chứng

- 11:10:05: cả hai heartbeat bị client đóng sau khoảng 1,5 giây, nginx 499. Request nhận đủ body nhưng upstream chưa trả header, thời gian chờ upstream khoảng 1,45 giây. Pool www dùng chung có tối đa 5 worker. Chưa chứng minh SQL nào chậm hoặc mọi lần chờ đều do hết worker.
- 11:10:15: Máy 2 `esp32-1051db78a8fc` HTTP400 sau 0,068 giây. Content-Length 2052 nhưng request_length chỉ 285 byte (header); request thành công cùng máy dài 2337 byte. Các trường upstream rỗng. Nginx info ghi `client prematurely closed connection`. Lỗi này xảy ra trước PHP, do client đóng request trước khi gửi đủ body; không phải JSON bị PHP từ chối. Chưa xác định vì sao ESP đóng sớm.
- Firmware cần kiểm tra write đủ byte, TLS/reuse/reconnect và task đồng thời. Chưa có source `station/cloud_print.h` trong workspace; chưa sửa hoặc flash firmware.

## Thay đổi server đã áp dụng

- Log HTTP503 dạng JSON có SQLSTATE/driver code nếu là PDOException, duration_ms, station/operation và request ID. Không ghi exception message, SQL, token, payload, query string. Thời gian PHP không gồm chờ FPM hoặc TLS. Response có X-Request-ID và JSON503 có request_id.
- Log nginx riêng cho tuyến máy in có số byte, content length, request/upstream timing và connection ID; giữ access log cũ. Error log info riêng location để bắt client ngắt giữa request. Logrotate nginx hiện hữu bao phủ các file này.
- Tách `/api/print/v1/` sang FPM pool `lop1-printer-api`: ondemand, tối đa 3 worker, 96M/worker; không render/SSE trong pool này. Đây là cô lập tải web để giảm ảnh hưởng worker web bị bận, không phải khẳng định đã hết lỗi firmware.
- Giữ HTTPS, quyền thiết bị, job, ngưỡng stale/offline và kiểm tra CAPT. Không gửi lệnh in, không reset ESP, không giữ Ready giả.

## Triển khai lại

`deploy/printer-api-pool.conf` đặt vào pool.d của PHP8.4; `deploy/nginx-printer-diagnostics.conf` nạp trong http context trước sites-enabled; snippet `deploy/nginx-printers.conf` trong HTTPS server. Kiểm tra và reload PHP trước nginx để socket tồn tại. Các thay đổi đã kiểm tra bằng `php-fpm8.4 -t`, `nginx -t`, reload mềm.

Kiểm tra logic: `php scripts/check_printer_api_diagnostics.php`, `php scripts/check_printers.php`. Log `/var/log/nginx/lop1-printer-diagnostic.log` và `/var/log/nginx/lop1-printer-error.log`. Cần tiếp tục quan sát lỗi 400 trước khi kết luận hết chập chờn; 400 thiếu body không thể sửa bằng thay validate JSON trên webserver.

## Quan sát sau cô lập pool

11:11:10 UTC Máy 2 vẫn có 400, request_length 285 / content_length 2052, upstream rỗng: xác nhận tách pool không sửa lỗi đóng trước body. 11:12:09 có một 502 trùng lần reload FPM; không quy lỗi này cho firmware. 11:12:25 Máy 1 còn 499 khoảng 1,5 giây dù đã có pool riêng, nên chưa thể khẳng định tải pool chung là nguyên nhân gốc hoặc đã sửa dứt điểm. Tổng mẫu 11:10:55 tới khoảng 11:12:25: 302 HTTP200, 108 HTTP204, một 400, một 502, một 499. Không tăng timeout mù hoặc đánh ready giả. Cần source/diagnostic firmware để hoàn tất sửa transport. Test PHP: 167 kiểm tra đạt, không in vật lý.
