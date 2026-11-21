# Máy 1 Ready nhưng CAPT không phản hồi — 21/11/2026

## Bằng chứng

Máy 1 `esp32-98c377a136b0`, FW 2026110902, uptime khoảng 120232 giây, không reset trong lúc lấy mẫu. Máy in Ready theo người dùng. USB connected/responsive=true; CAPT không mới; bulk OUT endpoint 01 timeout (ESP_ERR_TIMEOUT=263), lần lỗi đầu opcode C0A0. Cloud heartbeat 200, claim 200; lệnh `job_b06cd97a764c0f51ad402c8edded333b` chờ CAPT, 22 mặt, submitted=1, completed=0. Chưa biết số tờ thực tế. USB repair đã thử bốn lần, repair_state=4/error=-1. Lỗi này không phải mất kết nối server và không có bằng chứng lỗi giấy.

## Lỗi source và sửa

Gói printer-class SOFT_RESET đang dùng bmRequestType 0x21 (CLASS/INTERFACE). Linux usblp dùng CLASS/OTHER/OUT, tức 0x23, bRequest=2, value=0, index=số interface (0 trên thiết bị đang dùng), length=0.

Nguồn đối chiếu: https://github.com/torvalds/linux/blob/master/drivers/usb/class/usblp.c — macro usblp_reset, USB_RECIP_OTHER.

FW 2026112101 sửa đúng một byte thành `23 02 00 00 00 00 00 00`, giữ nguyên arbiter, asynchronous callback lifetime, CLEAR_FEATURE hai bulk pipe và xác minh CAPT mới/empty trước cho checkpoint phục hồi. Kiểm thử fake USB cũng kiểm tra gói 0x23.

Firmware trước có lỗi gói reset; telemetry chỉ có ESP_FAIL tổng hợp, chưa phân biệt bước/callback STALL nào đã xảy ra. Vì vậy chưa khẳng định gói sai là nguyên nhân duy nhất của bulk timeout đầu tiên hoặc rằng sửa sẽ phục hồi mọi trường hợp. Cần thử trên máy thật.

## Trạng thái triển khai

Đã lưu mẫu read-only qua LAN, không chiếm COM và không chủ động reset/hủy lệnh. Bản sửa được build/kiểm thử trước khi nạp. OTA khởi động lại ESP làm mất phiên/checkpoint RAM, nên cần người dùng chọn xử lý lệnh đang dở và đối chiếu số tờ đã ra. Chưa tự gửi lại 22 mặt hoặc coi completed=0 là chắc chắn chưa có giấy.
