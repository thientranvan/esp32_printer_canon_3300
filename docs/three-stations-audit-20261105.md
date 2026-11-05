# Kiểm tra ba trạm sau phát hành 2026110505

Quan sát khoảng13:59–14:01UTC ngày05/11/2026 (20:59–21:01Việt Nam).
Cả ba trạm đọc trực tiếp qua LAN đều chạy2026110505. Không gửi lệnh reset,
OTA, Wi-Fi hoặc điều khiển máy in trong lượt kiểm tra.

- Máy1: uptime1873->1960s, reset_reason3 (software của boot hiện tại),
  Wi-Fi disconnects0, heartbeat200/failures0, USB responsive/CAPT fresh,
  không cờ giấy/kẹt/nắp/service; job gần nhất completed4/4.
- Máy2: uptime2356->2442s, reset_reason3, disconnects0, heartbeat200/
  failures0, USB responsive/CAPT fresh, không lỗi máy in, không job.
- Máy3: uptime34->3->33s, reset_reason1 POWERON. Server lúc13:59:44UTC
  ghi boot_id fe8cefd49d1bb6e, uptime35s. Uptime tụt sau đó xác nhận boot
  mới, không phải chỉ mất HTTP. Người dùng xác nhận không thao tác nguồn/
  Reset. Chưa nhận USB máy in; CAPT không fresh nên không suy ra máy in
  không lỗi từ các bool false. Sau boot heartbeat200 nhưng claim0 vì chưa
  nhận máy in. Không có cổng UART ngoài Bluetooth/debug-console trên Mac.

Nginx từ13:20UTC đến thời điểm kiểm tra (~40phút), lọc theo stationID:

| Trạm | Request | Kết quả |
|---|---:|---|
| Máy1 |7793|4992×200,2796×204,5×503|
| Máy2 |8510|4860×200,3650×204,khôngHTTP>=400|
| Máy3 |2251|2247×200,4×408|

503 Máy1 ở13:22:21,13:30:03,13:32:02,13:36:16,13:48:30UTC;
hiện đã phục hồi, diagnostic503 là lịch sử. Chưa truy nguồn503 trong lượt
kiểm tra này. Máy3 có408 sau60s ở13:35:18,13:39:44,13:39:48,13:52:22;
không thể khẳng định mọi408 đều do reset nếu thiếu dữ liệu đồng bộ.
Không400/499 trong cửa sổ log trên. Không gọi toàn hệ thống đã ổn.

Kết luận: Máy1/2 không reset thêm trong lượt quan sát; uptime phù hợp boot
sau nạp OTA/software, không có mất association trong boot hiện tại. Chưa
có lịch sử boot đầy đủ để loại trừ mọi reset trước đó. Máy3 xác nhận reset
ngoài ý muốn, cần bắt UART liên tục115200 và kiểm tra đường nguồn/EN trước
khi kết luận nguyên nhân. POWERON không phải bằng chứng watchdog/panic,
cũng chưa đủ kết luận cáp/nguồn hỏng. Không cố mở COM hoặc reset để đọc log.
