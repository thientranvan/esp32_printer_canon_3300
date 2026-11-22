# Mac dừng hàng đợi vì mất cấu hình fallback — 22/11/2026

## Quan sát

Hàng đợi Máy 1 bị disabled từ job 308, lỗi `Cannot connect to ESP; cloud fallback not configured`. Job 310 mới đang chờ, chưa gửi tới ESP. Tại thời điểm kiểm tra, Máy 1 FW 2026112101 có USB/CAPT fresh, không service/jam/cover/paper error và không transport failure. Đây là lỗi dừng hàng đợi Mac, không phải CAPT tiếp tục lỗi ở thời điểm đó.

Mở lại queue đã cho job 310 chạy; ESP báo completed 1/1 mặt. Job người dùng tiếp theo trên Máy 2 cũng completed 2/2. Counter không thay cho xác nhận giấy của người dùng.

Kiểm tra root thư mục state Máy 1 `/Library/Caches/ESP32Printer/esp32-98c377a136b0`: chỉ còn lock và status-monitor.log, không cloud.json/enrollment.json. Chưa xác định ai hoặc cơ chế nào xóa; không khẳng định macOS là tác nhân. Đặt khóa/checkpoint/receipt trong thư mục cache là sai vị trí cho dữ liệu cần giữ.

## Sửa

Mac backend, installer và configure_cloud.py dùng `/Library/Application Support/ESP32Printer/<station_id>` cho khóa, identity, marker và receipt. Quyền thư mục root-only, credential 0600. Cài lại tự đăng ký nếu credential đã mất; không hỏi JSON/ticket.

Installer chỉ sao chép legacy state khi đích chưa tồn tại, giữ bytes/quyền của credential/identity/receipt; không trộn hai identity. Legacy pending/cloud-job chưa giải quyết hoặc CUPS có job đang chờ sẽ chặn migration/cài. Không xóa marker hoặc gửi lại job chưa rõ kết quả để ép cài.

Bản sửa tiếp theo cùng ngày chuyển Ubuntu sang `/var/lib/esp32printer`, có migration từ cache; Windows giữ ProgramData. Xem installer-state-storage-20261122.md.

## Kiểm tra

`python3 check_mac_state_migration.py`: chạy shell migration thật với đường dẫn có khoảng trắng, credential/identity/completed receipt thật trong thư mục tạm; kiểm tra quyền, không merge đích đã có, chặn receipt unresolved. Enrollment/fallback/Ubuntu/packaging tests giữ nguyên.

Không có thay đổi firmware trong bản sửa vị trí lưu state này. Mac đang dùng cần chạy lại bộ cài mới sau khi hàng đợi rảnh; firmware cập nhật không tự thay backend trên Mac.

## Kết quả trên Mac này

Đã chạy một lần bộ cài mới cập nhật đủ ba trạm, không còn job CUPS đang chờ. Cả ba cloud.json/enrollment.json trong Application Support thuộc root, mode 0600; GET status bằng từng client credential đều được server chấp nhận. Máy 1 queue enabled/idle, Printer connected. Không gửi thêm trang thử hoặc gửi lại job 308/310 để nghiệm thu thay đổi đường dẫn.
