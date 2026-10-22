# Renderer Ubuntu → ESP32 → Canon LBP3300

Đã chạy trên Ubuntu 22.04 x86_64 của `binhminh`: PDF → Poppler PBM →
Hi-SCoA → `espjob-v1`. Trang thử được kiểm tra bằng parser firmware, gửi qua LAN,
và người dùng xác nhận có giấy/chữ đầy đủ ngày 2026-10-22.
Chưa kiểm thử giao việc qua HTTPS cloud; API và worker ứng dụng chưa được triển khai.

## Phạm vi bản đầu

- PDF xuất sẵn bố cục A4 dọc, 600 dpi, giấy thường, cassette 1, một mặt.
- 1–42 trang/file, 1–99 bản, collate; PDF tối đa 20 MiB; kết quả tối đa 4 MiB.
- Vùng không in khoảng 5 mm quanh mép; nội dung sát mép có thể bị cắt.
- Chưa hỗ trợ tham số duplex/N-up/ngang của renderer cloud. Không mặc định biến
  tùy chọn không hỗ trợ thành một mặt: ứng dụng phải từ chối ngay từ giao diện/API.
  Các tùy chọn driver LAN Windows/Mac hiện có là luồng riêng, vẫn hoạt động.
- Word/HTML/ảnh phải được ứng dụng xuất sang PDF trước. Font nên nhúng vào PDF.
- Nếu vượt giới hạn, báo lỗi render; không giao file lỗi hoặc tự chia job sau khi in.

## Build và chạy

Cần `python3`, `cc`, `curl`, `sha256sum`, `tar`, `poppler-utils`, `bubblewrap`.
VPS hiện tại đã có đủ; không cần cài CUPS, Canon daemon hay thay dịch vụ hệ thống.

```sh
cd /var/www/lop1/tools/esp32-renderer
sh build.sh
python3 check.py
python3 render.py /path/input.pdf /path/new-output.espjob --copies 1
```

Chạy bằng tài khoản worker không phải root. Nếu quản trị viên thử bằng root,
dùng `runuser -u nobody -- ...` với thư mục đầu ra riêng thuộc `nobody`.
Không dùng thư mục public của web làm nơi lưu tài liệu/job.
`build.sh` tải commit cố định, xác minh SHA-256, giữ nguyên source và giấy phép GPL
cạnh binary; không cài package hoặc service. Không đưa binary/source tải về vào git.

`render.py` trả JSON sau khi ghi thành công:

```json
{"format":"espjob-v1","pages":1,"copies":1,"bytes":28968,"sha256":"..."}
```

File đích phải chưa tồn tại. Đọc/xử lý hoàn tất trước khi tạo đích; lỗi ghi sẽ xóa
file dở. Worker chỉ chuyển job sang `queued` khi tiến trình trả 0 và metadata đã lưu.
Worker giữ file đến khi job kết thúc theo `docs/esp32-printer-contract.md`.

## Tích hợp và cách ly

Ứng dụng gọi bằng argv riêng, không ghép chuỗi shell từ tên file/tùy chọn người dùng.
Worker cần hàng đợi có giới hạn đồng thời (khởi đầu 1), thời hạn toàn job và thư mục
riêng cho từng job. Không chạy render trong request PHP. Giới hạn upload trước khi
lưu, xác thực quyền chọn máy, xác thực tùy chọn và không cho người dùng chỉ định
đường dẫn trên server. Không tự gửi lại job đã bắt đầu in khi mất kết nối.

Poppler chạy trong bubblewrap: namespace mạng/process riêng, bỏ capabilities,
chỉ thấy thư viện/font hệ thống và thư mục tạm của job; giới hạn CPU, bộ nhớ, file,
FD và timeout mỗi tiến trình. Nếu sandbox không chạy được thì render thất bại,
không có đường chạy không sandbox. Dùng gói Poppler được cập nhật bản vá của Ubuntu.
Không sửa cấu hình kernel để bỏ sandbox.

`compress.c` giải nén và so byte cho mọi band trước khi xuất. Tích hợp production
nên chạy thêm parser `Prepared::parse` từ repo ESP trước khi đưa file vào hàng đợi.

## Giấy phép

`compress.c`: GPL-3.0-or-later. Liên kết bộ nén Hi-SCoA của
[mounaiban/captdriver](https://github.com/mounaiban/captdriver), commit
`62719249ac34633338be54bc74beddd0e7003d38` (Alexey Galakhov và cộng tác viên).
Giữ source tải về và `COPYING`; nếu phân phối binary phải cung cấp corresponding
source và giấy phép theo GPL. Không nhúng binary Canon độc quyền.
