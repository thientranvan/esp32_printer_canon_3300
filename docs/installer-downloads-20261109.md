# Link bộ cài cho trang Download trên web quản lý — 09/11/2026

## Nội dung cần dev web triển khai

Trong web **quanly.lophocbinhminh.com**, thêm mục tải bộ cài máy in dùng các link dưới đây. Mỗi hệ điều hành một bộ cài, chạy một lần thêm/cập nhật đủ **Máy 1, Máy 2, Máy 3**. Không yêu cầu cùng LAN, đăng nhập/chọn tài khoản chủ trạm, nhập IP/token hoặc tải JSON. Giữ nút PowerShell cũ trong mục thu gọn cho Windows.

| Hệ điều hành | Link tải trực tiếp | Cách dùng |
|---|---|---|
| Windows 10/11 x64 | [ESP32PrinterSetup.exe](https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/ESP32PrinterSetup.exe) | Mở EXE, cấp quyền quản trị, bấm “Cài đặt / Cập nhật cả 3 máy”. |
| macOS | [install_mac.sh](https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/install_mac.sh) | Tải file; chạy `sudo sh install_mac.sh` trong thư mục tải. |
| Ubuntu 22.04/24.04 | [install_ubuntu.py](https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/install_ubuntu.py) | Tải file; chạy `sudo python3 install_ubuntu.py` trong thư mục tải. |
| Windows PowerShell — cách cũ | [esp32-printer-setup-v3.ps1](https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/esp32-printer-setup-v3.ps1) | PowerShell quản trị: `powershell -NoProfile -ExecutionPolicy Bypass -File .\esp32-printer-setup-v3.ps1 -AllStations`. |

Link checksum EXE: [ESP32PrinterSetup.exe.sha256](https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/ESP32PrinterSetup.exe.sha256).
Source: [GitHub](https://github.com/thientranvan/esp32_printer_canon_3300).

Mô tả ngắn cạnh các nút: **“Cài một lần đủ Máy 1–2–3. In từ Word/PDF/Chrome qua LAN hoặc Internet. Không nhập khóa, không cần cùng mạng.”**

Windows EXE chưa ký số; giữ ghi chú nhà phát hành chưa được Windows xác minh. macOS/Ubuntu vẫn là script theo lựa chọn người dùng, không quảng cáo thành app/EXE. Quyền quản trị là quyền cài driver/hàng đợi của OS, không phải tài khoản web.

## Cách đặt link trên web

- Dùng link HTTPS trực tiếp GitHub, không đưa bộ cài vào flash ESP hoặc lưu bản cũ trong PHP. Không thêm bearer, token hay tham số theo người dùng vào URL.
- Link `main` tải bản mới nhất. Nếu web lưu cache hoặc mirror, cần kiểm tra SHA-256 và cập nhật đồng bộ khi có release; không dùng checksum cố định của tài liệu này để chặn mọi bản sau.
- Để nút bấm tải ngay cả script Mac/Ubuntu: web nên proxy download cùng domain từ allowlist đúng bốn URL trong bảng, đặt `Content-Disposition: attachment` với tên file cố định, `Content-Type: application/octet-stream`, giới hạn tối đa 4 MiB và timeout. Không nhận URL/path tùy ý từ query. Không ghi khóa vào link. Chỉ redirect hoặc thêm thuộc tính `download` cho link khác domain không bảo đảm trình duyệt tải file thay vì mở mã nguồn. Không yêu cầu người dùng copy mã nguồn.
- Hiển thị lệnh theo OS như bảng. Không truyền StationIP hoặc bắt chọn từng trạm. Gọi script cũ có IP vẫn cài cả ba theo mặc định, nhưng giao diện mới dùng lệnh không IP.
- Không yêu cầu dev thay API upload/render. API public `/api/print/v1/client/install/enroll` đã được server triển khai; bộ cài dùng endpoint này và giữ credential riêng tự động.

## Routing và dữ liệu

Bộ cài dùng station ID cố định, ba queue theo ID, cài lại không tạo printer trùng. Khi in, kiểm tra đúng ESP trước khi gửi LAN; không truy cập LAN được thì gửi bản CAPT đã render tại client qua server. Server chỉ relay, không render lại. ESP là nguồn trạng thái duy nhất; việc in web không phụ thuộc máy tính nào làm trạm trung gian.

## Kiểm thử và giới hạn nghiệm thu

- Windows EXE đã build, kiểm tra PE/UAC/resource/checksum và self-test Mono đạt. Bridge/converter/idempotency/không gửi nhầm LAN có kiểm thử tự động. **Chưa chạy bộ cài mới trên Windows 10/11 thật.**
- Mac này: một lần cài/cập nhật đủ ba queue; API remote cấp client, đăng ký lại giữ đúng credential cả ba. Đã gửi qua server, ESP Máy 2/3 báo completed 1/1 mặt; cần phân biệt counter với người dùng xác nhận giấy. Máy 3 đã được người dùng xác nhận in qua server ở lượt trước. Nhánh tải Canon mới không chạy vì Mac đã có driver.
- Ubuntu: kiểm tra manifest, backend, converter/options/parser và dispatch đạt. **Chưa cài/in bản này trên Ubuntu thật.**
- Máy 1 có vấn đề CAPT riêng; không quy thành lỗi bộ cài hoặc coi là đã nghiệm thu in thành công Máy 1.

Trang ESP đã đổi nội dung/lệnh tải theo luồng ba máy trong firmware **2026110902**, có kèm sửa CAPT từ 2026110901. Bo chỉ hiện giao diện mới sau khi thực sự nhận OTA; không đồng nhất source đã sửa với bo đã nạp.

## SHA-256 snapshot bộ cài hiện tại

- `installers/ESP32PrinterSetup.exe`: `df45e3f6012839b96c6f60c57e53149a16fa88b63c9be0f7ebda5f779ea68574`
- `install_mac.sh`: `05ef1c1bb95bc1065806d4e359c5df0c146b45f2fb80e0ba3f4deaba5830e540`
- `install_ubuntu.py`: `c1b496b8bd2ca60f1c11d0137ad3a4e47d7521d95cffff4e1207eed01b4b97f0`
- `installers/esp32-printer-setup-v3.ps1`: `dbd7e377d05caa4d617d41ba52f456559b1faa3b414f60818598f7e80f5200fa`

Các docs liên quan: [remote install contract](https://github.com/thientranvan/esp32_printer_canon_3300/blob/main/docs/all-stations-remote-install-20261109.md), [Mac acceptance](https://github.com/thientranvan/esp32_printer_canon_3300/blob/main/docs/all-stations-mac-acceptance-20261109.md). Source bộ cài đã lên Git trước thay đổi trang ESP; cập nhật này không yêu cầu người dùng cài lại chỉ để đổi văn bản download.
