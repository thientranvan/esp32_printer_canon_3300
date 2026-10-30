# OTA tự động từ bản build sẵn trên GitHub

ESP không build mã nguồn. Khi có Wi-Fi, đồng hồ TLS hợp lệ và trạm rảnh, ESP kiểm tra mỗi 60 giây
(không cần khởi động lại) qua `firmware/latest.json` trên nhánh `main`. Nếu có phiên bản
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

- Kiểm tra manifest mỗi 60 giây khi trạm rảnh; mất mạng/đồng hồ chưa hợp lệ thì đợi. Lỗi kiểm tra/tải: giữ firmware hiện tại, thử lại sau 60 giây khi rảnh.
- Kiểm tra manifest không khóa in. Nếu job đến trong lúc kiểm tra, hoãn tải/nạp. Trước `Update.begin`, khóa nhận job mới, giữ quyền USB riêng và kiểm tra lại không có job active/paused/waiting/uncertain, phiên LAN, claim cloud đang bay hoặc kết quả hủy chưa xác minh. OTA thủ công dùng cùng khóa; khóa không tự hết hạn giữa lúc ghi flash.
- Manifest tối đa 2 KiB, schema/board/version/bytes/hash được kiểm tra. URL cố định trên raw.githubusercontent.com, HTTPS kiểm chứng CA, không theo redirect. SHA-256 nằm trong manifest được tải qua HTTPS; đây không phải chữ ký firmware độc lập.
- Chỉ image lớn hơn phiên bản hiện tại, vừa phân vùng OTA, tải đủ byte và khớp SHA-256 mới được `Update.end`. Tải thiếu, lỗi HTTP, sai hash hoặc lỗi ghi flash: abort, không kích hoạt image dở.
- Không hủy job đang in để update. Không chạy kiểm tra/tải trong phiên in; job mới đến sau khi trạm đã khóa để nạp sẽ chờ hoặc retry kết nối. Thao tác claim cloud kiểm tra lại khóa sau khi đánh dấu đang claim để tránh race với OTA.
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

## Firmware metadata gửi server (từ 2026103005)

Mỗi heartbeat có trường gốc `firmware` cùng nội dung `/status.firmware`:

```json
{"version":2026103005,"check_interval_s":60,"checking":false,"downloading":false,"state":"Không có bản mới hơn hợp lệ cho bo này"}
```

`version` là phiên bản image **đang chạy**, không phải phiên bản manifest hoặc
image đang tải. Server lưu/hiển thị `heartbeat_json.firmware.version`; khi
`downloading=true`, có thể hiển thị “Đang cập nhật firmware” thay vì suy ra máy
in lỗi vì CAPT tạm ngừng poll. `checking=true` chỉ là tải manifest, vẫn nhận
job bình thường. ESP là nguồn metadata; không cần Mac/Windows báo phiên bản.

## Chuyển từ 2026103004 sang kiểm tra định kỳ

Bo 1/2 đang chạy 2026103004 chỉ check khi boot. Sau khi manifest/image
2026103005 đã được push lên main, khởi động lại riêng ESP một lần lúc hết
job để nó tự tải bản mới. Chỉ cần ESP có Internet, không cần cùng LAN với
Mac. Không khởi động lại khi còn phiên in/đối soát chưa giải quyết. Từ
2026103005, kiểm tra mỗi 60 giây khi rảnh, không cần reboot để phát hiện
release mới. Bo 3 chưa có hỗ trợ OTA này cần nạp chuyển tiếp một lần sau.

Ngày 30/10/2026: build 2026103005 và test host cho validator/downloader/khóa
admission đạt. Chưa test polling định kỳ thực tế trên bo: Mac khác LAN với
bo 1/2, bo 3 tắt. Không ghi nhận việc các bo đã nhận 2026103005 cho đến khi
đọc được heartbeat hoặc status xác nhận version này.

## Yêu cầu tích hợp web quản lý (chỉ contract, không tự sửa app server)

- Giữ nguyên object `firmware` khi nhận heartbeat, lưu trong `heartbeat_json`; trường này tương thích bổ sung với schema 1.
- Hiển thị phiên bản đang chạy tại từng máy từ `heartbeat_json.firmware.version` (số nguyên, ví dụ `2026103005`). Không lấy version manifest làm version của bo.
- Bo cũ không có trường này: hiển thị “Chưa có thông tin phiên bản”; không suy ra version 0 hay suy ra offline.
- Dùng `checking`, `downloading`, `state` để hiển thị trạng thái cập nhật. Khi `downloading=true`, ưu tiên “Đang cập nhật firmware”; CAPT có thể cũ vì OTA giữ quyền USB.
- Đẩy thay đổi phiên bản/trạng thái qua luồng realtime hiện tại của server; ESP đã gửi chúng trong mọi heartbeat, không cần PC agent.
- Heartbeat mới sau reboot xác nhận version thực tế; offline vẫn xét theo last_seen như cũ. Không xóa trạng thái lỗi máy in chỉ vì version thay đổi.
- Không tự gửi lệnh reboot/update trong lúc job active/paused/waiting/uncertain. Firmware tự quyết định idle và giữ khóa admission trước khi flash.
