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

## Theo dõi firmware 2026110501: chưa đạt — 05/11, 18:30 Việt Nam

Đọc `esp32-http-transport-fix-20261105.md`, xác nhận DB cả hai máy báo 2026110501 và giữ boot ID trong các lần lấy mẫu. Lấy cửa sổ sau OTA từ 11:22:35 UTC; không gửi lệnh in hoặc gây tải giả. Trước 11:28 đã có request thành công chậm 3,432 s (Máy 1), 3,511 s (Máy 2), không thể coi mọi heartbeat đều dưới 250 ms.

| UTC | Máy | HTTP | Request / body khai báo | Thời gian | Upstream |
|---|---|---:|---|---:|---|
| 11:28:16 | 1 | 499 | 2366 / 2081 byte | 3,869 s | kết nối được, chưa có header sau 3,808 s |
| 11:28:17 | 2 | 499 | 2421 / 2136 byte | 3,894 s | kết nối được, chưa có header sau 3,864 s |
| 11:28:24 | 2 | 499 | 2507 / 2222 byte | 4,997 s | kết nối được, chưa có header sau 4,956 s |
| 11:28:53 | 2 | 400 | 285 / 2223 byte | 0,128 s | rỗng, PHP chưa xử lý |

Nginx info tiếp tục ghi `client prematurely closed connection`. Request_length 285 đúng header: không nhận đủ body trong ca 400, lỗi còn xảy ra với native HTTP transport mới. Không kết luận nguyên nhân sâu do Wi-Fi/TLS/SDK chỉ từ event `headers_sent`.

Diagnostic lưu lúc 11:29:07: cả hai có `HTTP=-1 phase=headers_sent setup=0 perform=28679 errno=0 bytes=0 overflow=0`, heap lần lượt 102000 / 101552. Sau đó Máy 2 giữ lỗi mới `HTTP=-1 phase=open setup=0 perform=28674 errno=0 bytes=0 overflow=0 heap=99072`. Hai mã tương ứng 0x7007 / 0x7002; cần đối chiếu enum của đúng SDK đang build, không tự suy ý nghĩa errno=0 là không có lỗi.

Máy 2 heartbeat cuối trước khoảng mất là 11:28:53 (400), đầu tiên phục hồi là 11:29:33 (200), khoảng 40 s; heartbeat thành công trước đó là 11:28:52, tới lần thành công sau là 41 s. Máy 1 khoảng cách heartbeat lớn nhất trong cửa sổ tới 11:29:58 là 5 s. Tại 11:29:58 cả hai đã về idle, heartbeat200, claim204 và failures=0; diagnostic vẫn giữ lỗi lịch sử đúng contract, không dùng nó để báo lỗi hiện tại.

Kết luận: chưa nghiệm thu ổn định, không cần chờ đủ 15–30 phút mới báo fail vì đã có bằng chứng tái hiện ngay trong phút thứ sáu sau mốc theo dõi. Bộ kiểm tra server diagnostics/167 printer/USB status/batches đạt; không chứng minh transport thực tế đã hết lỗi. Không thay firmware, ngưỡng stale hoặc trạng thái job trong lượt theo dõi này.
