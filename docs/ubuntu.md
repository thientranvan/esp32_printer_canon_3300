# Ubuntu desktop: máy in LAN ESP32 / LBP3300

Ubuntu 22.04/24.04, CUPS 2.x; build bộ nén theo CPU máy cài, không cần driver Canon
độc quyền, không cần Windows/Mac hoặc VPS để in LAN. Ubuntu chỉ phải bật khi chính
nó gửi lệnh. CUPS và bộ lọc Ubuntu render PDF; ESP giữ USB, trạng thái và điều khiển
máy in. Không có agent gửi trạng thái hàng đợi máy tính lên webserver.

Trên trang ESP, mở **Cài máy in Windows / Mac / Ubuntu**, tải `install_ubuntu.py`,
mở Terminal tại thư mục tải và sao chép lệnh của đúng trạm:

```sh
sudo python3 ./install_ubuntu.py 192.168.1.199
sudo python3 ./install_ubuntu.py 192.168.1.200 'Máy 2'
sudo python3 ./install_ubuntu.py 192.168.1.201 'Máy 3'
```

Không truyền tên: hỏi tên khi Terminal tương tác, mặc định lấy từ ESP. Bộ cài đọc
`/setup/profile.json`, xác minh ID/model/IP; tên queue dựa ID, cài lại cùng bo cập
nhật tên/IP trên queue cũ. Dừng nếu queue đang có lệnh hoặc có pending chưa đối soát,
không xóa job để cài lại. Không nhận mật khẩu/token trạm.

Bộ cài dùng apt cài CUPS/cups-filters, Poppler, Pillow, bubblewrap và công cụ build;
kiểm SHA-256 các source tải từ GitHub và source bộ nén upstream cố định. Build bằng
user nobody; source/license GPL được giữ cạnh binary trong
`/usr/lib/cups/filter/esp32printer`. Chỉ dùng source đã kiểm hash, không tải binary
Canon hoặc binary build sẵn theo kiến trúc. Không nhúng bộ cài vào flash ESP.

Máy in xuất hiện trong LibreOffice, Chrome và hộp thoại in chuẩn của desktop.
Mặc định A4, cassette 1, 600 dpi. CUPS xử lý bố cục nhiều trang trên một mặt và
hướng giấy; backend xử lý copies/collate và hai mặt, cạnh dài/cạnh ngắn. Giới hạn
42 mặt sau bố cục, PDF đầu vào 20 MiB, spool CAPT 20 MiB; chưa hỗ trợ khay/khổ khác.
Backend kiểm toàn bộ spool trước khi gửi lệnh; dừng queue khi lỗi. Marker
`/var/cache/cups/esp32printer/<station-id>/pending.txt` ngăn tự in lại khi kết quả
chưa rõ. Không tự xóa marker/resume sau lỗi: đối soát giấy và job trên ESP trước.

Trong khi in, CUPS nhận tiến trình/lỗi từ backend. Xem trạng thái ESP trực tiếp tại
`http://<IP>`; Ubuntu chưa có cửa sổ trạng thái riêng như Windows/macOS, cũng chưa
có monitor nền đồng bộ CUPS lúc rảnh. Trạng thái mặc định "Idle" của CUPS không
thay thế trạng thái máy in qua USB trên ESP.

## Kiểm tra

```sh
python3 check_ubuntu.py
python3 check_ubuntu.py --render
```

Đã chạy trên Mac: hash manifest, đầu vào/tùy chọn lỗi, sinh backend Linux và tự
kiểm parser. Trên Ubuntu 22.04: Poppler → Pillow → bộ nén CAPT trong sandbox →
parser production đạt; thử native `pdftopdf` với 2-up/ngang/duplex và PPD lint đạt.
Các công cụ CUPS kiểm thử chỉ giải nén trong `/tmp` trên VPS, không cài/start CUPS
hoặc thay dịch vụ web production. Chưa chạy bộ cài đầy đủ trên desktop Ubuntu và
chưa xác nhận giấy in thực tế từ Ubuntu; đặc biệt cần nghiệm thu chiều hai mặt.

Bubblewrap phải chạy được với tài khoản CUPS; sandbox lỗi thì render thất bại,
không có fallback bỏ sandbox hoặc tự đổi cấu hình kernel/AppArmor.
