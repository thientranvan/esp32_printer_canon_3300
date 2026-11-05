# Xóa lệnh phải giữ kết quả dọn phiên CAPT

Bản thử2026110505, chưa phát hành OTA chung trước khi kiểm chứng phần cứng.

Máy1 sau OTA0504: không còn job trên ESP/CUPS nhưng CAPT vẫn trả paper_required,
cassette1_present=true, counters1/0/0/0. Đây là phiên máy in còn dở; không đủ
bằng chứng gọi là lỗi giấy thật hay báo sai cảm biến. Không đổi trạng thái thành
Ready để che cờ máy in. Người dùng xác nhận đã bấm xóa lệnh lỗi trước đó.

Lỗ hổng source: delete ở trạng thái failed/cancelled/resolved có thể xóa Job
ngay cả khi printerSession còn và cleanup chưa cleared. Một số nhánh hủy/xóa
cũng bỏ record sau khi cancelCapt trả false. Kiểm tra cleanup trước đây chỉ
xem processing bits, không kiểm tra cờ giấy/kẹt/nắp/service và counters.

Bản sửa:
- Giữ ID/session của lệnh kết thúc nhưng cleanup chưa xác minh. Delete chuyển
  sang yêu cầu cancel, không xóa ngay. Cleanup thất bại giữ cancelled/unverified.
- Không tự lặp cleanup liên tục. Người dùng có thể yêu cầu xóa để thử lại;
  không nhận job cloud/LAN mới ghi đè phiên chưa dọn.
- Chỉ xác nhận cleared khi chuỗi ACK, release, trạng thái không lỗi/processing
  và bốn counters đều0. Không tăng tiến trình hoặc gửi lại raster.
- POST /printer/discard có Basic auth+CSRF, chỉ dùng khi không còn job/history,
  TCP/cloud/OTA đang chạy. Yêu cầu tường minh để dọn phiên orphan đã mất ID ở
  firmware cũ; status worker xin reservation mới rồi chạy cancel/discard.
  Theo dõi job_control.printer_cancel, HTTP202 chưa phải xác nhận cleared.
  Tờ đã kéo có thể vẫn ra giấy. Không tự gọi endpoint chỉ vì báo hết giấy.

Kiểm thử production cancel: ACK thất bại, epoch/USB thay đổi, còn counters,
còn paper_required không được cleared; delete giữ record cho failed/session
và cancelled/unverified, xóa được khi cleared. Cloud/LAN recovery và OTA đạt.
Build public không chứa device secret.

Chưa nạp Máy1: xuất hiện lệnh LAN Windows VANSINH-280502,12mặt,8mặt được
xác nhận, waiting_printer; chờ người dùng giữ hay hủy trước OTA. Lúc đọc này
paper_required=false. Không tự hủy hoặc làm reset phiên đang giữ.

## Kết quả Máy2

Người dùng đổi yêu cầu sang Máy2; không hủy, không OTA, không gửi lệnh điều
khiển Máy1. Máy2 được kiểm tra đúng ID esp32-1051db78a8fc, rảnh trước OTA;
nạp0505 thành công. Sau reboot USB/CAPT phản hồi, heartbeat200, không lỗi,
không job; counters lịch sử6/6/6/6.

Thử POST /printer/discard đúng một lần lúc rảnh: reservation/cancel được
ACK, job discard-orphan cancelled/cleared, bốn counters về0/0/0/0,
paper_required/jam/cover/service=false. Không reboot hoặc tắt/bật máy in.
Sau đó delete record cleared thành công, ID job rỗng. Không gửi trang in.
Đây là xác minh dọn phiên trên máy rảnh; chưa tái tạo case sai nguồn giấy
đang dở hoặc mất điện giữa chuỗi cleanup.0505 giữ ở nhánh thử, chưa OTA chung.

## Phát hành OTA chính thức

Người dùng yêu cầu phát hành0505 sau thử Máy2. Đưa đúng binary đã kiểm tra
lên main/public OTA cùng manifest SHA-256, không build lại hoặc đổi cấu hình
trạm. Bo có mạng tự kiểm tra khi rảnh; không ép OTA vào phiên đang chạy hay
đang cần đối soát. Publish không đồng nghĩa mọi bo đã nạp. Giới hạn xác minh
phần cứng ở trên vẫn áp dụng; tiếp tục theo dõi ca hủy giữa lúc lỗi giấy.
