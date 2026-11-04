# Máy 1 nháy đỏ khi rảnh — 03/11/2026

Người dùng báo RGB nháy đỏ sau khi bật khoảng 24 phút, chưa gửi lệnh.
Kiểm tra chỉ đọc, không reset ESP, không mở COM hoặc gửi dữ liệu in.

- Firmware 2026110201, reset_reason=1, uptime tăng liên tục.
- LAN 192.168.1.199, Wi-Fi đã nối, RSSI khoảng −45 đến −56 dBm.
- USB và CAPT mới, không service/jam/cover/paper fault được báo;
  Cassette 1 có giấy, transport failures=0, không có job active.
- Khi quan sát: heartbeat HTTP 200, claim 204 (không có job), cloud idle.
  Server DB cũng nhận printer.state=idle, CAPT fresh; không có lỗi in đang chạy.
- Nginx trong khoảng 10:32–10:57 UTC ghi 1740 heartbeat HTTP 200,
  10 heartbeat HTTP 400 với response bytes=0, 752 claim HTTP 204.
  Các mốc 400: 10:36:04, 10:37:16, 10:39:17, 10:41:32, 10:44:45,
  10:45:41, 10:49:32, 10:52:45, 10:53:29, 10:56:23 UTC.
- Firmware bật NetworkError (đỏ 250 ms sáng/250 ms tắt) khi Wi-Fi mất,
  cloudOnline=false hoặc cloud.state=server_error. Một heartbeat không thành
  công đặt cloudOnline=false ngay; heartbeat 200/204 sau đó khôi phục true.
  Vì vậy có thể chớp đỏ ngay cả khi không in. Không đồng nhất đèn đỏ với lỗi USB.

Chưa bắt được trạng thái đỏ kéo dài trong lúc đọc; 5 mẫu cách 4 giây đều bình thường.
HTTP 400/0 không đủ để kết luận ứng dụng từ chối JSON hay client đóng kết nối
sớm. Heartbeat dùng timeout 1500 ms và reusable HTTPS; cần chẩn đoán transport
đúng thời điểm để phân biệt. Không sửa server, không tăng timeout/che cảnh báo
chỉ từ suy đoán. Đã hỏi người dùng hiện còn đỏ liên tục hay đã xanh chớp 3 giây.
