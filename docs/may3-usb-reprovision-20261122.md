# Nạp lại Máy 3 qua USB native — 22/11/2026

## Nhận diện và dữ liệu cũ

Cổng được Mac nhận: `/dev/cu.usbmodem1101`, USB JTAG/serial debug unit. esptool xác nhận ESP32-S3, MAC `10:51:DB:78:6A:34`, flash 16 MB, PSRAM embedded 8 MB. Không mặc định tên cổng cho lần sau.

Bảng phân vùng trên bo là factory-only (NVS 0x9000/0x6000, phy_init 0xF000, factory 0x10000/0x1F0000), không có OTA metadata/slots và không thấy namespace/key cấu hình printer. Đây không phải layout trạm in đang phát hành. Không chỉ ghi app vào slot OTA giả định.

Đã sao lưu riêng toàn bộ flash 16 MB trước thay layout; bản sao/NVS/token nằm trong thư mục tạm riêng, không commit. Người dùng xác nhận cho cấp lại riêng khóa server Máy 3 để khôi phục cả LAN/web. Dùng helper quản trị hiện có, giữ tên/trạng thái enabled/quyền người dùng, không đổi khóa Máy 1–2 hay client credential của máy tính.

## Nạp

Build bootloader/partition table với cấu hình ESP32-S3 N16R8/16 MB/OPI PSRAM và layout app3M_fat9M_16MB. Nạp public app `firmware/station.bin` bản 2026112101, hash đối chiếu manifest. Provision NVS riêng cho Máy 3 (password không đưa vào firmware public, Wi-Fi TVT, IP tĩnh .201, AP ESP32-Printer-May3, cloud enabled với credential đã cấp lại). Không ghi credential hoặc NVS image lên Git/log.

Các vùng được nạp: bootloader 0x0, partition table 0x8000, NVS 0x9000/0x5000, OTA metadata khởi tạo 0xE000/0x2000, app0 0x10000. esptool xác minh hash thành công.

## Khởi động qua USB native

Sau `--after hard-reset`, bo vẫn vào/download lại khi mở native serial dù DTR/RTS false. Mẫu ROM: USB_UART_CHIP_RESET, DOWNLOAD(USB/UART0). Đóng serial monitor, chạy esptool `--before no-reset --after watchdog-reset read-mac` để thoát download và khởi động app. Không tiếp tục mở lại serial khi USB native đã dùng cho Host; COM/UART0 vẫn là cổng ưu tiên cho debug trong vận hành.

## Xác minh

GET .201/setup/profile.json xác nhận đúng station ID esp32-1051db786a34, tên ESP32-LBP3300-Máy 3, AP ESP32-Printer-May3, IP 192.168.1.201. GET /status: healthy=true, FW 2026112101, Wi-Fi TVT, static_ip=true, cloud enabled/idle, heartbeat_http=200. Server ghi nhận heartbeat mới cùng FW/IP. USB connected=false phù hợp vì cổng native đang nối Mac, chưa cắm máy in.

Đây là xác nhận bo/firmware/network/server đã chạy; chưa phải kiểm tra nhận máy in hoặc xác nhận giấy sau lần nạp này.
