# OTA tự động từ bản build sẵn trên GitHub

ESP không build mã nguồn. Sau mỗi lần khởi động, khi có Wi-Fi và đồng hồ TLS
hợp lệ, ESP kiểm tra `firmware/latest.json` trên nhánh `main`. Nếu có phiên bản
mới hơn cho `esp32-s3-n16r8`, tải `firmware/station.bin`, ghi phân vùng OTA,
xác minh SHA-256 rồi kích hoạt và khởi động lại. Một image dùng chung Máy 1/2/3.

Tên máy, IP, AP SSID, Wi-Fi, mật khẩu quản lý/AP và token cloud nằm trong NVS;
OTA application không xóa NVS. Image công khai không chứa mật khẩu/token riêng.

## Chuyển tiếp lần đầu

Các firmware cũ còn biên dịch mật khẩu từ `device_secret.h`. Nạp một lần bản
chuyển tiếp riêng để lưu mật khẩu hiện tại vào NVS key `printer/password`.
Không đưa bản chuyển tiếp lên Git. Sau đó mọi bản OTA công khai dùng mật khẩu
đã lưu. Image công khai nạp vào bo trắng không có mật khẩu sẽ không mở AP/trang
quản lý với mật khẩu mặc định; phải provisioning riêng lần đầu.

## Phát hành

1. Tăng `ESP32_FIRMWARE_VERSION` trong `station/firmware_update.h`; không dùng lại số cũ.
2. Chạy `python3 build_firmware_release.py` trên máy đã cài Arduino ESP32 và toolchain của repo.
3. Chạy `python3 check_firmware_update.py` và các kiểm thử liên quan thay đổi.
4. Commit/push mã nguồn, `firmware/station.bin` và `firmware/latest.json` cùng một commit.

Script build bật `ESP32_PUBLIC_OTA_BUILD`, loại include secret riêng, kiểm tra
image và kiểm tra không chứa mật khẩu riêng hiện tại trước khi chép vào repo.
Chỉ binary OTA application được công khai; không thêm flash dump, NVS, ELF,
binary provisioning hoặc toàn bộ thư mục build. Git lưu lịch sử binary; nếu
số bản phát hành lớn, chuyển hai URL cố định sang release assets kèm quy tắc
redirect/host tin cậy trước khi đổi cách phát hành.

## Hoạt động và giới hạn

- Kiểm tra một lần mỗi boot; chờ Wi-Fi/đồng hồ tối đa 120 giây. Lỗi kiểm tra/tải: giữ firmware hiện tại, thử lại lần khởi động sau.
- Trong giai đoạn kiểm tra boot, chưa nhận lệnh CAPT qua LAN và chưa claim job cloud. Kết nối LAN đến sớm chờ tối đa 60 giây như cũ; client có thể cần thử lại nếu mạng GitHub quá chậm.
- Manifest tối đa 2 KiB, schema/board/version/bytes/hash được kiểm tra. URL cố định trên raw.githubusercontent.com, HTTPS kiểm chứng CA, không theo redirect. SHA-256 nằm trong manifest được tải qua HTTPS; đây không phải chữ ký firmware độc lập.
- Chỉ image lớn hơn phiên bản hiện tại, vừa phân vùng OTA, tải đủ byte và khớp SHA-256 mới được `Update.end`. Tải thiếu, lỗi HTTP, sai hash hoặc lỗi ghi flash: abort, không kích hoạt image dở.
- Không hủy job đang in để update. Cơ chế này chạy trước khi nhận job sau boot; không polling update giữa phiên in.
- RGB tím nháy nhanh 100 ms sáng / 100 ms tắt trong lúc tải/xác minh/nạp. Khác tím chớp đôi báo đối soát. Trạng thái/phiên bản hiển thị trên trang chủ ESP, trường `/status.firmware`.
- Chưa bổ sung rollback tự động khi image hợp lệ nhưng có lỗi runtime. Cần giữ cách nạp COM và OTA thủ công để phục hồi.

`check_firmware_update.py` chạy production validator và downloader với HTTP/
flash/SHA giả lập để kiểm tra nhánh lỗi; SHA-256 thật do mbedTLS thực hiện trên ESP.
Kiểm tra tải thật, reboot và giữ cấu hình cần ghi nhận riêng theo từng bo.

## Kiểm chứng thực tế 30/10/2026

Máy 2 (`esp32-1051db78a8fc`, `.200`): nạp riêng bootstrap version
2026103003; quan sát ESP tự tải firmware công khai từ GitHub, báo xác minh/nạp
thành công rồi reboot sang 2026103004. Sau boot, check kết thúc, self-test đạt,
CAPT fresh và heartbeat HTTP 200. IP/tên/AP SSID giữ theo cấu hình, đăng nhập
vẫn dùng mật khẩu cũ từ NVS. Không gửi trang in thử trong lượt kiểm chứng OTA.
Máy 1: đợi counter job kết thúc 44/44 rồi mới nạp bootstrap. ESP tự lên
2026103004, giữ `.199`, tên/AP SSID và đăng nhập; heartbeat 200. Sau boot chưa
nhận được USB dù người dùng xác nhận máy in vẫn bật/cáp giữ nguyên. Nạp lại
chính public image 2026103004 để reboot riêng ESP thêm một lần (không job),
USB nhận đúng Canon LBP3300, CAPT fresh, cloud idle. Không tắt nguồn máy in.
Đây là một lần enumeration chưa hoàn tất sau boot; chưa xác định nguyên nhân,
không gọi đây là kiểm chứng in giấy hoặc khẳng định mọi lần OTA đều tái nhận
USB thành công ngay. Không thêm reboot lặp vô hạn để che lỗi. Máy 3 chưa
truy cập được qua LAN.
