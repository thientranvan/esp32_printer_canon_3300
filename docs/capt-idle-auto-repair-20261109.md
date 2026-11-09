# Tự khôi phục CAPT lúc rảnh/preflight — firmware 2026110901

## Nguyên nhân đã xác định trong source

Firmware 2026110801 chỉ yêu cầu printer-class SOFT_RESET khi lệnh cloud đang in bị ngắt, không phải preflight. `captStatusTask` lúc rảnh chỉ tiếp tục hỏi A0A8/A0A1. Máy 1 có USB control/GET_DEVICE_ID còn đáp ứng nhưng bulk endpoint 01 timeout, CAPT cũ khoảng 92 phút; watchdog hiện có không được gọi. Không phải mất Wi-Fi, enrollment hay upload.

## Thay đổi

- Status task tự yêu cầu repair khi CAPT không mới trong 3 giây, lỗi transport hiện tại là ESP_ERR_TIMEOUT, USB control còn đáp ứng và giành được CAPT owner. Áp dụng lúc rảnh, LAN gián đoạn và cloud preflight/recovery; không giành transfer đang chạy hoặc OTA.
- Nhánh trạng thái không gửi thêm A0A1 khi A0A8 vừa lỗi, tránh thêm một timeout trước khi repair.
- Dùng chuỗi bất đồng bộ có sẵn: halt/flush bulk, printer-class SOFT_RESET, CLEAR_FEATURE cả hai endpoint, clear host endpoint. Không tắt nguồn máy in/ESP, không cưỡng bức root-port power cycle (đã từng gây abort SDK), không giải phóng transfer trước callback.
- Nhánh status tối đa ba lần trong một đợt stale: lần đầu khi đủ điều kiện, lần hai cách ít nhất 15 giây, lần ba cách ít nhất 60 giây. CAPT đọc lại thành công hoặc thiết bị ngắt mới mở đợt mới. Lỗi/hết giấy/kẹt giấy/hở nắp đã đọc được không phải điều kiện reset.
- Xóa độ mới telemetry cũ sau repair. ACK reset chưa phải ready. Chỉ khi đọc một frame CAPT mới, không lỗi, không còn processing/printing và bốn counter đều 0 thì tăng session generation đúng một lần; checkpoint LAN/cloud hiện có được phép phục hồi từ tờ đã xác nhận. Không tăng tiến trình hoặc tự coi trang đã in chỉ vì reset ACK.
- Nếu reset không được hỗ trợ hoặc bulk vẫn timeout sau các lần thử, giữ trạng thái lỗi thật. Bản sửa không bảo đảm mọi lỗi điện/phần cứng của máy in đều có thể giải quyết bằng USB.

Ngưỡng stale 3 giây không có nghĩa mọi lỗi được xử lý trong đúng 3 giây: transfer đang chạy có timeout riêng (thông thường 5 giây); phải đợi callback thu hồi an toàn trước maintenance.

## Kiểm thử

`python3 check_usb_printer_repair.py`: chạy production repair/arbiter/verification với fake callback; giới hạn retry, wraparound, không reset vì lỗi giấy, không lấy USB đang bận, callback lifetime, NAK/STALL, lỗi submit, ngắt USB, ACK không cho replay nếu còn counter/printing; chỉ frame rỗng mới tăng generation một lần.

Giữ các kiểm thử cloud session recovery, paper wait/cancel và OTA. Cần nghiệm thu class reset thực trên Máy 1 đang lỗi; build/test không thay thế xác nhận giấy.

## Nạp Máy 1

Người dùng xác nhận phiên LAN cũ 4/8 mặt đã xong/bỏ, cho phép nạp bản sửa. Mac hiện không nhận COM và không truy cập LAN Máy 1; trạm còn heartbeat qua server. Phiên cũ đã chuyển cancelled nhưng printer_cancel=unverified nên firmware cũ vẫn khóa auto OTA. Có thể cần khởi động lại riêng ESP một lần để bỏ phiên RAM đã được người dùng cho phép bỏ, sau đó auto OTA lấy bản phát hành mới. Không yêu cầu tắt/bật máy in để nghiệm thu sửa bulk này. Không ghi nhận đã nạp trước khi heartbeat báo đúng version.
