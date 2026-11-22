# Nơi lưu dữ liệu bền vững của bộ cài — 22/11/2026

| OS | Thư mục state theo trạm | Thay đổi |
|---|---|---|
| Windows | `%ProgramData%\ESP32Printer\Stations\<station_id>` | Đã là dữ liệu bền vững, ACL SYSTEM/Administrators. Không cần đổi PS/EXE cho bản sửa vị trí lưu state. |
| Mac | `/Library/Application Support/ESP32Printer/<station_id>` | Thay cache cũ, đã cài trên Mac hiện tại. |
| Ubuntu | `/var/lib/esp32printer/<station_id>` | Thay `/var/cache/cups/esp32printer/<station_id>`, cập nhật cả backend generator và configure_cloud.py. |

Identity, client credential, pending marker và receipt phải nằm trong state, không phải cache có thể dọn. Khóa/identity trên Mac/Linux 0600, thư mục 0700. Không đưa dữ liệu này lên Git hay web download.

Migration Mac/Ubuntu chỉ chạy khi không có lệnh CUPS hoặc pending/cloud receipt chưa giải quyết. Copy legacy sang staging riêng, giữ quyền/bytes và publish bằng rename sau khi copy thành công. Không overwrite identity đích đã tồn tại. Copy lỗi không publish dữ liệu nửa chừng; cài lại có thể thử lại. Không tự xóa lịch sử lệnh hoặc gửi lại dữ liệu in.

Windows EXE nguồn không đổi trong bản sửa vị trí state; kiểm tra PE/UAC/embedded resource/SHA-256 đạt. Mac: migration/permissions/enrollment thật đã kiểm tra. Ubuntu: manifest/backend/parser và migration thật trong thư mục tạm (gồm lỗi copy, dữ liệu đích có sẵn, receipt chưa giải quyết) đạt; chưa thay thế nghiệm thu cài/in trên Ubuntu thật. Windows runtime trên máy thật vẫn cần nghiệm thu riêng.

Bộ cài cả ba OS vẫn cài Máy 1–2–3 trong một lần và không yêu cầu LAN/ticket/account. Không cần cập nhật firmware cho thay đổi đường dẫn state; máy tính đã cài bản cũ phải chạy lại bộ cài mới lúc hàng đợi rảnh.
