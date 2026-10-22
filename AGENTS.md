# Làm việc với bo ESP32 này

- Theo yêu cầu người dùng: ưu tiên nạp firmware, đọc log và debug trực tiếp qua cổng
  **COM/UART0**, 115200 baud. Kiểm tra tên cổng và MAC trước khi nạp; không mặc định
  tên `/dev/cu.*` luôn giữ nguyên. Không chiếm cổng bằng hai tiến trình cùng lúc.
- Giữ cổng **USB native** cho USB Host nối máy in qua OTG. Chỉ khi không thể cắm COM
  mới dùng cách thao tác/nạp qua mạng và bố trí USB/OTG như trước. Không tự reset khi
  chỉ cần đọc log; đóng serial monitor trước khi nạp.
- Người dùng xác nhận 22/10/2026: sau khi hàn hai mối USB–OTG, LBP3300 nối thẳng
  qua OTG đã nhận; không cần hub hoặc cấp nguồn riêng cho USB. ESP vẫn cần nguồn nuôi.
  Chưa biết chính xác hai điểm hàn, không tự suy đoán pin hoặc áp dụng cho bo chưa sửa.
- Cập nhật README/docs khi có kết quả phần cứng mới. Phân biệt thiết bị được nhận,
  counter hoàn tất và trang giấy được người dùng xác nhận. Các ghi chép hub cũ là lịch sử.
- Không in/lưu token, mật khẩu hoặc nội dung credential vào log debug; lọc dữ liệu
  nhạy cảm trước khi hiển thị log. Không commit `station/device_secret.h`, credential,
  bản sao flash hoặc firmware có bí mật.

- Quan sát 22/10/2026: mở lại COM bằng pyserial dù đặt DTR/RTS false trước open
  vẫn có lần làm bo boot lại (uptime về 3 giây). Không lặp cách này để đọc log,
  nhất là khi có job. Ưu tiên giữ monitor liên tục từ sau lần nạp, giải phóng
  cổng khi flash; kiểm tra uptime để phát hiện reset ngoài ý muốn.
