# Máy 1 không xóa được lệnh cũ — 26/11/2026

Người dùng yêu cầu kiểm tra/xóa lệnh trên ESP. Đúng Máy1 esp32-98c377a136b0, IP192.168.1.199, FW2026112509, uptime77874s. Không có COM trên Mac, dùng LAN. Job Windows20261124-120609-9702227 cancelled/inactive, total4/completed0, printer_cancelunverified; không có queue Mac chờ Máy1. Tờ đã in không suy từ counter0.

Delete của Job giữ record nếu needsCleanup. cancelCapt cũ chỉ thao tác khi printerSession/sessionEpoch còn khớp, dùng reservation cũ để GoOffline/Discard/Release. Nếu reservation cũ hết hạn hoặc máy in đã reconnect thì cleanup có thể unverified mãi; UI delete tiếp tục yêu cầu cancel, không xóa ID giả. Đây là lý do gate OTA và lệnh mới vẫn bị khóa; chưa có mã lỗi reservation trực tiếp để quy mọi ACK thất bại cho hết hạn.

Đã dọn phiên người dùng yêu cầu xóa bằng reservation mới A3A2/A2A0 và chuỗi CAPT cancel/discard/page0/release, toàn ACK, A0A8 xác nhận0/0/0/0. Không tắt máy in, không gửi raster/trang thử. Delete trên FW2509 vẫn giữ record unverified; sau xác minh empty/không có job active/cloud, restart riêng ESP với nguyên APSSID để bỏ record legacy và cho OTA. Chỉ làm điều này sau khi dữ liệu máy in được dọn/xác minh, không dùng reboot làm chứng cứ completed.

FW2026112604: cancel được người dùng yêu cầu (report=true và stopping) lấy reservation mới dưới cùng exclusive owner trước chuỗi hủy; cập nhật sessionID/epoch mới. Cho phép yêu cầu cancel rõ ràng sau printer epoch thay đổi; không mở đường auto replay, recovery/report=false vẫn phải giữ epoch hợp lệ. Reservation thất bại giữ unverified và không gửi DiscardData. Xóa record vẫn chỉ sau ACK+release+frame sạch+counter0; không biến ACKHTTP202 thành cleared.

Test check_capt_cancel.py thêm refresh ID mới, cancel sau epoch đổi, thất bại A3A2/A2A0 không discard; giữ kiểm tra counters còn giấy, ACK lỗi, không replay hoặc tăng progress giả. Các sửa buffer USB, stack LAN, recovery của2603 được giữ. Chờ nạp riêng Máy1 và kiểm tra UI/delete/USB thật; chưa phát hành image mới trước xác minh.

## Kết quả canary và sửa thứ tự reservation

2604 nạp Máy1 thành công nhưng self-check không in giấy (F002/F005/F001/A3A2/A2A0 rồi đóng sender, không C0A0/C0A4) tạo waiting_printer, delete vẫn cancelled/unverified. Không nghiệm thu hoặc phát hành2604. Các lệnh CAPT cleanup từ clientHTTP riêng khi có reservation mới đềuACK và counters0.

2605 bổ sung job_control.cancel_failed_opcode/cancel_reply/printer_session/session_epoch/printer_epoch để không chỉ có chữ unverified. Live self-check ghi failed_opcode41632 (0xA2A0), reply135 (0x87), session5, epoch0=printer_epoch0. Như vậy refresh vô điều kiện gây lỗi khi reservation cũ còn hoạt động; đây không phải lỗi giấy hoặc epoch lệch của chính lượt test.2605 cũng chưa phát hành chung.

2606 sửa thứ tự: nếu epoch khớp thì cleanup bằng reservation hiện có trước. Chỉ nếu cleanup thất bại và là yêu cầu cancel/delete rõ ràng thì handshake reservation mới rồi cleanup lại. Nếu epoch đã thay đổi thì bỏ thao tác bằng reservation cũ, chỉ thử reservation mới khi user yêu cầu hủy. Cleanup nội bộ report=false không tự vượt epoch. Không thêm vòng vô hạn, không replay raster; lỗi cuối vẫn giữunverified, success xóa diagnostic lỗi cũ. Các bước kết thúc/release và frame/counter0 vẫn phải xác minh.

check_capt_cancel.py thêm: reservation còn sống không gọi A2A0 lần hai; stale session làm cleanup đầu lỗi rồi refreshID9 thành công; epoch đổi với explicitcancel mới được lấy ID mới; handshake lỗi không DiscardData; first failed opcode được ghi đúng. Chờ build/nạp2606 và livecheck trước phát hành. Phiên chẩn đoán zero-counter đã được bỏ bằng restart riêngESP; máy in giữ nguồn, không có job người dùng active/cloud.

## Nghiệm thu Máy1 2606

Nạp riêng Máy1, /status xác nhận FW2026112606, IDđúng, không còn jobWindows cũ, CAPT fresh/không lỗi. Hai live self-check chỉ tạo reservation và metadata, không gửi C0A0/C0A4/raster:

- Tạo phiên LANwaiting_printer rồi POSTdelete ngay khi reservation còn hiệu lực: record được xóa, counters0/0/0/0.
- Tạo phiên khác với thời hạn reservation30s, chờ35s rồi POSTdelete: record cũng được xóa, counters0/0/0/0.

Cả hai không tắt nguồn máy in hoặc resetESP để hoàn tấtdelete. Không dựa vào riêngHTTP202; poll thấyjobIDrỗng và frameCAPT mới/counter0. Không khẳng định tracecho biết nhánh refresh đã chạy trong mọi caACK; bằng chứng thực tế là kết quảxóa sau khoảng chờ và fakeprinter test kiểm tra nhánhstale/refresh.

Các test cancel/refresh+diagnostic, cloud engine, LAN/cloud session recovery, reservedUSBbuffer, OTA và homepage đềuPASS; build2606 đạt. Promote đúngbinaryđã nạp sang firmware/station.bin+latest.json, các bo khác tự lấy khi rảnh theo cơ chếOTA. Chưa xác nhận versionthực chạy trênMáy2/3 trong lượt này. Bộ cài máy tính không thay trong sửa2606.

## Trường chẩn đoán gửi webserver

job_control (và heartbeatjob cùngserializer) thêm cancel_failed_opcode, cancel_reply, printer_session, session_epoch, printer_epoch. Không có token/password/pixel/tài liệu. Opcode0 nghĩa chưa ghi lỗicommand hoặc đãcleared; replylàwordđầuphản hồi lưu khi lệnh thất bại, đối chiếu capt_transport để phân biệt lỗiUSB và ACKâm. Ví dụ live2605: opcode41632=0xA2A0/reply135=0x87. Server có thể hiện kèm unverified thay vì lỗi chưa xác định; giữ ID/cleanup gate, không tự đổi cleared theo suy đoán. Các trường là diagnosticoptional, không thay schemajob/control hoặc yêu cầu serverreset máy in.
