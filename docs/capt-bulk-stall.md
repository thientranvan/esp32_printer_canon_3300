# CAPT bulk OUT bị đứng — Máy 1, 02/11/2026

## Quan sát thực tế trước can thiệp

Firmware 2026103101, lệnh web `job_8f62ea64d3aeb7f766644739d93899f2`,
68 mặt, submitted=4, confirmed=2 (1 tờ hoàn chỉnh theo checkpoint).
Người dùng xác nhận mới ra khoảng 2–3 tờ, khay có giấy, Ready nháy đều,
không in tiếp. Không lấy số tờ ước lượng làm checkpoint.

USB còn enumerated và GET_DEVICE_ID phản hồi, nhưng A0A8 đã cũ hơn 687 giây.
Counter CAPT cuối: decoded/started/exiting/completed = 4/3/2/2.
Lỗi đầu tiên: ESP_ERR_TIMEOUT (263), opcode C0A0 (dữ liệu ảnh), endpoint OUT 01.
129 lỗi truyền tại lần đọc đầu, chủ yếu các query A0A1 tiếp tục timeout.
Printer-class repair thử một lần, failed (-1); không có ACK khôi phục.
Không có COM, chỉ các cổng Bluetooth/debug-console, nên đọc qua LAN.
Server nhận đúng cùng lỗi transport, nhưng printer.state vẫn busy_cloud.

Không được suy ra hết giấy/kẹt giấy/nắp đã ổn từ các cờ false của frame cũ.
Chưa chứng minh nguyên nhân ban đầu: nghẽn bộ đệm, backpressure, cáp hay máy in.
Đèn Ready nháy và USB enumerated không chứng minh CAPT còn làm việc.

## Bản giảm nguy cơ 2026110201

- Cloud giữ tối đa hai mặt chưa hoàn tất, tính cả mặt đang gửi: chờ completed,
  thay vì started, trước khi gửi mặt tiếp theo. Một mặt đã started chưa có
  nghĩa đã giải phóng bộ đệm, đặc biệt khi in hai mặt.
- Cho bulk dữ liệu ảnh C0A0 chờ tối đa 30 giây thay vì 5 giây, để tránh cắt
  một lần truyền do backpressure ngắn. Các query khác giữ 5 giây. Caller LAN
  và cloud cùng dùng timeout tương ứng + 3 giây chờ hủy/callback.
- Không retry C0A0 đã timeout, không cộng completed từ byte truyền hoặc thời gian.
  Đây là giới hạn hữu hạn, không phải lời hứa mọi máy sẽ hồi phục sau 30 giây.
- Heartbeat ưu tiên unresponsive khi CAPT không fresh, kể cả có job cloud/LAN.
  Không để busy_cloud che việc mất phản hồi CAPT. GET_DEVICE_ID vẫn là trường
  usb_responsive riêng, không dùng nó thay cho CAPT freshness.

Mô phỏng engine kiểm tra không gửi raster thứ ba khi hai mặt trước chưa
completed; duplex order, checkpoint, cancel, wait giấy và USB repair callback
vẫn qua kiểm thử. Chưa nghiệm thu vật lý bản mới hoặc chứng minh hết tái diễn.

## Hủy và cập nhật

Người dùng đã cho hủy lệnh hiện tại, sửa và nạp lại. Lệnh được hủy trên ESP,
server xác nhận event 204; CAPT cleanup không được ACK nên printer_cancel=unverified.
Giữ cảnh báo này; không ghi giả đã xóa bộ đệm. Firmware chặn OTA khi chưa
xác minh; cần xử lí kết nối vật lý trước khi cập nhật, không bypass khóa bằng
xóa lịch sử rồi tự in lại. Không gửi lại lệnh 68 mặt trong lượt bảo trì.

Người dùng đã rút cáp USB máy in. Đã xác nhận connected=false, devices=0,
physical_link=false, không active/cloud job/TCP. Lưu snapshot hủy vào file
chẩn đoán riêng rồi xóa bản ghi cancelled trên ESP để nạp khi máy in tách rời.
Đây chỉ là xóa lịch sử bảo trì, không chứng minh bộ đệm máy in đã sạch.
Phải kiểm tra CAPT sau khi cắm lại; không tự gửi lại tài liệu.

Máy 1 đã nạp OTA 2026110201 khi cáp máy in đang rút; đọc lại version,
self-test đạt, heartbeat HTTP 200, không còn job active. Chưa xác nhận
CAPT sau khi cắm lại hoặc thử lại tải in dài trên bản mới.
