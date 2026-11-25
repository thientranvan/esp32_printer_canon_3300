# Phát hành OTA AirPrint preview 2026112502

Người dùng yêu cầu đưa firmware lên Git để các máy tự cập nhật sau khi đã được thông báo chưa có nghiệm thu điện thoại/giấy thật. Yêu cầu mới thay lựa chọn chỉ build trước đó. Không ghi nhận đã thử phần cứng hoặc chứng nhận AirPrint/Mopria.

## Kênh cập nhật

`firmware/latest.json` và `firmware/station.bin` phát hành đồng thời bản 2026112502; bật tính năng IPP/AirPrint trong public build. Manifest ghi `release_stage=airprint-preview`, `hardware_validated=false`; boot firmware cũ chỉ cần schema/version/board/bytes/sha256, các trường thêm không đổi cơ chế OTA. Version cao hơn cả stable 2026112101 và experimental 2026112501 để cùng nhận update.

Giữ firmwareIdle/reserve: không tự reboot khi cloud/LAN đang in, paused/waiting/uncertain, journal cần đối soát hoặc xóa bộ đệm chưa xác minh. Không hủy/đối soát lệnh để ép cập nhật. Bo offline chỉ kiểm tra sau khi có mạng; kiểm tra mỗi 60 giây khi rảnh. Source đã đẩy Git không có nghĩa cả ba bo đã nạp.

Build script public bật `ESP32_AIRPRINT_EXPERIMENTAL` để phù hợp code feature hiện có. Tên flag giữ ý nghĩa tính năng chưa qua nghiệm thu thật; endpoint IPP 631, mDNS và raster-to-CAPT nay được bật trong artifact OTA theo yêu cầu rõ ràng của người dùng.

## Tương thích server

Kiểm tra source server: nhánh điều khiển job không nằm DB chỉ chấp nhận source `lan`. Bản 2026112502 do đó báo job AirPrint là source `lan`, client_name AirPrint, client_os ipp, ID `ipp-<random-boot-sequence>`; giữ tiến trình counter ESP. Không đòi server nhận source mới để nút pause/resume/cancel hoạt động. Không có agent máy tính đẩy telemetry, không sửa app/database server.

CAPT/USB dùng chung engine và arbiter; cloud claim và native LAN bị chặn khi IPP đang nhận/in. Bộ cài CAPT cũ vẫn dùng được; cập nhật firmware không bắt buộc cài lại máy tính.

## Kiểm tra trước phát hành và giới hạn

Chạy check_airprint.py, check_usb_printer_repair.py, check_paper_wait.py, check_capt_session_recovery.py, check_firmware_update.py; build ESP32-S3 N16R8/OPI PSRAM/16 MB và xác minh size/hash/không chứa DEVICE_PASSWORD. Kiểm tra regression cloud engine/HTTP và native LAN ở lượt build thử trước đó.

Các giới hạn của docs/airprint-implementation-20261125.md vẫn áp dụng: A4 600dpi, URF/PWG mono/gray/RGB, 40 trang logic, spool CAPT 4MiB, nhận đủ job trước khi in, chưa media-col/PDF/JPEG/layout trên ESP, một listener IPP có thể đợi upload; ngắt bulk chưa có replay tự động hoàn chỉnh cho IPP. Không hứa điện thoại tìm thấy/in đúng hoặc tốc độ nhanh bằng cắm dây trước khi thử thật.

Khi có thiết bị: kiểm tra version, bật cùng LAN/mDNS rồi thử đúng một trang trên Máy 3 trước. Không gửi tài liệu dài đồng thời cả ba để thay nghiệm thu. Nếu cần khôi phục tính năng cũ phải phát hành image không AirPrint với version mới cao hơn (hoặc nạp app thủ công sau khi bảo vệ lệnh dở); không hạ số version manifest vì client không tự downgrade.
