# Bộ cài Windows EXE

Tải `installers/ESP32PrinterSetup.exe`, mở bằng chuột, chấp nhận quyền quản trị.
Chọn hoặc nhập IP, bấm **Đọc trạm**, đặt tên rồi **Cài đặt / Cập nhật**. Giao diện
hiện log và báo kết quả; không cần lưu script hoặc gõ lệnh PowerShell. Cần Windows
10/11 x64, .NET Framework có sẵn trong Windows; chưa hỗ trợ ARM64. EXE chưa ký số,
Windows có thể hiển thị cảnh báo nhà phát hành.

EXE nhúng nguyên bản `esp32-printer-setup-v3.ps1`, không tải script từ mạng khi
chạy; driver Canon còn thiếu vẫn được script tải từ nguồn chính thức và kiểm tra
như trước. Nhận tên/ID/IP từ `/setup/profile.json`; đổi tên, cập nhật theo ID,
kiểm tra job và giữ khóa pending theo bộ cài hiện có. Không mở SSH, không đổi
execution policy toàn máy. PowerShell chỉ dùng Bypass cho tiến trình cài này.

File tạm nằm trong thư mục GUID riêng ở ProgramData, ACL chỉ SYSTEM/Administrators;
tên/IP đưa vào runner bằng UTF-8/base64, không ghép dữ liệu vào câu lệnh shell.
Sau khi tiến trình kết thúc, dọn riêng thư mục tạm đó. Không cho đóng cửa sổ giữa
lượt cài để tránh vô tình ngắt cài driver. Không cần cập nhật bộ cài Mac/Ubuntu.

## Build

Source: `installers/windows/Setup.cs`, `setup.manifest`, `build.py`.
Compiler: Mono `mcs` hoặc .NET Framework `csc` (tham số MCS/CSC). Build tạo EXE
x64 GUI, resource icon và manifest yêu cầu admin, nhúng PS1 và xuất SHA-256:

```sh
python3 installers/windows/build.py
python3 check_windows_exe.py
```

Runtime self-check, không cài driver/queue:

```text
ESP32PrinterSetup.exe --self-test
```

Đã build và chạy self-check bằng Mono trong thư mục tạm trên Ubuntu, không cài
Mono hoặc đổi dịch vụ production. Kiểm tra PE x64/GUI, resource icon/UAC, bytes
script nhúng trùng script gốc, SHA-256 và link trên ESP đạt. Máy Windows thử nghiệm
`192.168.1.105:22222` không truy cập được; chưa nghiệm thu mở GUI/UAC và cài lại trên
Windows thật. Giữ script PS1 làm lựa chọn hỗ trợ kỹ thuật trong mục thu gọn của ESP.

Triển khai 25/10/2026: EXE và SHA-256 đã lên GitHub, tải lại từ raw GitHub có hash
khớp. Máy 1 đã nạp giao diện mới qua COM, đúng MAC `98:C3:77:A1:36:B0`, hash
verified; boot `STATION: PASS`, Wi-Fi status 3, AP `ESP32-Printer-May1`.
Người dùng yêu cầu Máy 2/3 để nạp sau; chưa cập nhật giao diện EXE trên hai bo đó.

Cả hai cách cài được giữ trên ESP: EXE là nút chính; mục thu gọn **Cài bằng
PowerShell (cách cũ)** có link PS1, hướng dẫn mở PowerShell quản trị, lệnh chọn
file đã tải với IP của trạm và nút sao chép. Không cần dùng EXE nếu chọn cách cũ.

Bản khôi phục lỗi giấy 25/10/2026 nhúng bridge giữ phiên khi hết giấy/kẹt/nắp; hỗ trợ checkpoint ESP khi USB ngắt. Cần firmware mới trước khi chạy lại bộ cài. Chi tiết: [paper-jam-recovery.md](paper-jam-recovery.md).
