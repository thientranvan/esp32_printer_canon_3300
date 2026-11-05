# Máy 1/2 chập chờn — 05/11/2026

Đọc LAN đồng thời 17:51–17:53 giờ Việt Nam; không reset/nạp/đổi Wi-Fi.
Máy 1 uptime 1815 -> 1929, Máy 2 1284 -> 1398: không thấy reset.
Trong các mẫu đều Wi-Fi connected, USB connected và CAPT fresh, không job active.
Máy 1 RSSI -43..-51; Máy 2 -63..-73 dBm. Máy 3 không truy cập được,
server last_seen vẫn 04/11 14:17:41 UTC, chưa biết đang tắt hay mất kết nối.
Không suy diễn POWERON của Máy 3 hôm trước thành nguyên nhân của Máy 1/2.

Bắt được Máy 1 heartbeat=-1 lúc 17:51:35, hồi phục 200 lúc 17:51:38;
17:52:11 cloud.state=server_error, heartbeat=-1 trong khi uptime tăng và LAN
vẫn trả lời; 17:52:15 phục hồi idle/200. Máy 2 có server_error lúc 17:52:05,
chưa bắt được transport phase/reason tại đúng lúc đó.

Nginx trong giờ trước kiểm tra:
- Máy 1: 4018 response 200, 1 response 503, 2 response 499;
  khoảng cách heartbeat lớn nhất 3 giây trong tập đọc.
- Máy 2: 2010 response 200, 6 response 400 với body 0 byte;
  có khoảng trống 33–96 giây, ngoài khoảng trống dài đầu phiên.
  Chưa chứng minh khoảng trống đó là Wi-Fi disconnect hay HTTP/TLS bị kẹt.
- 10:41:32 UTC Máy 1 HTTP503: nginx ghi `printer_api PDOException`.
  Chưa có SQLSTATE nên không kết luận DB lock hay lỗi SQL cụ thể.
- 10:51:35 UTC HTTP499 trùng mẫu heartbeat=-1; 499 nghĩa client đóng trước
  response hoàn tất, không chứng minh server quá tải hoặc Wi-Fi mất.

Firmware cloudTelemetryTask đặt HTTP timeout 1500ms và reusable HTTPS.
Một lần heartbeat không thành công lập tức đặt cloudOnline=false; state và
RGB vì vậy báo lỗi dịch vụ ngay dù Wi-Fi/USB vẫn còn. Đây là hành vi đang có,
không phải bằng chứng reset. Server đánh stale sau >3s, offline sau >6s đối
với telemetry mới: một vài request trễ làm giao diện đổi trạng thái.

Bước sửa cần làm, không che lỗi bằng giữ Ready:
1. Firmware bổ sung diagnostic heartbeat: phase open/write/headers/read,
   esp_err/errno, duration, last_success_age, số lỗi liên tiếp; thêm Wi-Fi
   disconnect reason/count và thời gian. Không log credential/body/token.
   Hiện claim có diagnostic nhưng heartbeat không truyền tham số này.
2. Server ghi SQLSTATE/error class và duration cho 503, không credential hoặc
   payload; điều tra transaction/lock bằng đúng thời điểm lỗi. HTTP400/0
   cần log timing/bytes request hoặc nginx info có chọn lọc, chưa đổ lỗi JSON.
3. Tách local Wi-Fi, cloud transport, server HTTP và telemetry stale/offline
   trong UI; phản ánh tuổi dữ liệu và lỗi thật. Đánh giá timeout bằng latency
   đo được; heartbeat độc lập USB nên không cần ép 1.5s làm deadline in.
   Không tăng timeout đơn thuần hoặc bật giữ Ready để che transport failure.

Chưa sửa mã/config server hoặc phát hành firmware trong lượt này.

## Kiểm tra lại 18:00 ngày 05/11

Server log đến 11:00:28 UTC: Máy 2 còn HTTP400/0 lúc 10:54:20,
10:58:07, 10:59:28 UTC, max gap trong cửa sổ 15 phút là 106 giây.
Máy 1 không thêm HTTP lỗi sau 10:51:35 UTC trong tập đọc này, max gap 3 giây.
LAN lúc 18:00 cả hai online/heartbeat200, uptime nối tiếp từ lần trước,
không reset. Chưa kết luận hết lỗi Máy 1 từ vài phút không tái hiện.
