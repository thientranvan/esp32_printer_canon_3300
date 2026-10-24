# Sửa hiển thị trạng thái server — 23/10/2026

Các mục ngày 23/10 bên dưới là hướng dẫn lịch sử cho dev server.

## Cập nhật 24/10: USB ngắt phải ưu tiên hơn CAPT cũ

Theo yêu cầu sửa trực tiếp lần này, đã sửa riêng `printerStatus()` trong
`/var/www/lop1/app/printer_repository.php`, giữ nguyên thay đổi khác đang có.
Sau enabled/online/stale, kiểm tra USB recovering, disconnected, USB error và
unresponsive trước faults/CAPT freshness. Không xóa trường CAPT hoặc giả lập
fresh/ready trên ESP để né lỗi hiển thị server. ESP đã gửi đúng
`printer.state=disconnected`, `usb_connected=false`, `usb_responsive=false`.

Kiểm tra production bằng dữ liệu DB chỉ đọc: cả ba trạm online trả reason
**Máy in chưa kết nối USB**, printer_state `disconnected`.
Không thay đổi DB, hàng đợi, service hoặc JS/CSS. PHP lint đạt.
Kiểm tra hồi quy trên server: `php scripts/check_printer_usb_status.php`;
bản test trong repo firmware là `check_server_printer_status.php` (nhận đường
dẫn file PHP server), kiểm tra USB ngắt, recovering, lỗi USB, không phản hồi,
CAPT cũ khi USB còn nối và ESP offline. Tất cả đều chặn ready.

Firmware phục hồi heartbeat đã nạp COM Máy 1, OTA Máy 2/3. Không cần nạp lại
Máy 1 cùng binary lần nữa; lỗi nhãn CAPT/USB được sửa phía server.

## 1. Không lấy lịch sử làm tiến trình đang in

Đã đọc `/var/www/lop1/assets/scripts/app-printers.js`, `renderStations()`,
khoảng dòng 88–94. Code hiện tạo panel/progress cho mọi `heartbeat.job.id`,
không kiểm tra `job.active`. Vì thế lịch sử failed/uncertain 0/2 vẫn có thanh in.

- Nguồn duy nhất của lệnh trạm: `heartbeat.job` (ID, source, state, active,
  total, completed). Không dùng TCP socket/counter hoặc ghép tổng của job khác.
- Chỉ hiển thị thanh **tiến trình đang in** khi `s.online && !s.stale &&
  live.active === true`. Dừng/tiếp tục/hủy cũng cần active, controllable và
  dữ liệu còn mới. Không tăng phần trăm từ thời gian hay spooler.
- Khi inactive, phần chính ghi “Trạm hiện không có lệnh đang chạy”. Bản ghi
  cũ đưa vào mục thu gọn **Lịch sử lệnh gần nhất**; failed không phải printing.
- `state=uncertain`: luôn hiển thị cảnh báo **Lệnh cần đối soát — chưa rõ kết quả
  in**, kể cả active=false. Không đánh dấu hoàn tất, không tự xóa/replay hay gửi lại.
- Với dữ liệu offline/stale, bỏ thanh tiến trình trực tiếp, giữ dấu thời gian
  và ghi “Trạng thái cũ”. Không kết luận đã hoàn tất chỉ từ total==completed.
- Hủy/xóa lệnh local Mac không xóa capture của Windows hoặc lịch sử tại ESP.
  Thông tin từng hàng đợi phải giữ đúng ID và nguồn.

Trong `/var/www/lop1/app/printer_repository.php`, `printerStatus()`, thêm điều
kiện chặn ready khi `heartbeat.job.state === 'uncertain'`, trước fallback idle.
Thông báo: “Có lệnh chưa rõ kết quả; cần đối soát trước khi in tiếp”.
Hiện code kiểm tra cloud.state và active_job_id nhưng bỏ sót LAN job uncertain.
Không thay đổi DB/job status chỉ để dọn hiển thị.

## 2. Bỏ thông báo cảm biến mặc định gây hiểu nhầm

`app/printer_repository.php`, `printerList()`, khoảng dòng 80 đang hard-code:

```php
$ready ? 'Có thể nhận lệnh; cảm biến giấy/mực chưa được xác minh' : 'Chưa có renderer CAPT được kiểm chứng'
```

Nhánh này không dùng cờ CAPT mới nhất, nên luôn hiện câu cũ ngay cả khi đã có CAPT.
Thay bằng thông báo phụ thuộc `printerStatus()`/capt_fresh:

- Dữ liệu mới, không lỗi và không uncertain: “CAPT vừa cập nhật; chưa ghi nhận
  cảnh báo giấy, nắp, kẹt giấy hoặc hộp mực”. Chỉ nói “Có thể nhận lệnh” khi
  renderer và các điều kiện ready còn lại đều đạt.
- CAPT cũ/mất kết nối: “Chưa có trạng thái mới”, không suy ra giấy/mực tốt.
- Có fault: hiện các lỗi cụ thể từ `faults` và service_code; không che bằng
  thông báo chung. Ví dụ `Lỗi Canon: E100 0000`.
- `paper_required`: “Kiểm tra giấy / khổ / nguồn giấy”; không khẳng định khay trống.
- `toner_warning`: “Kiểm tra hộp mực”; không suy ra hết mực hay phần trăm.
- `toner_percent=null`: “Không có dữ liệu phần trăm mực”, không hiện 0% hay 100%.

`printer.ready_verified=false` trong heartbeat là giới hạn xác nhận tổng thể,
không có nghĩa firmware chưa đọc CAPT. Không đổi thành true để làm đẹp giao diện.
Các cờ đã ánh xạ: service_error/service_code, paper_jam, cover_open,
paper_required, toner_warning; chưa nghiệm thu vật lý mọi trường hợp. Không tuyên
bố giải mã mọi cảm biến hoặc giải thích được mọi mã service Canon.

## 3. Quan sát trực tiếp trong lần kiểm tra này

ESP báo job LAN `20261023-014536-5756511`, inactive, state uncertain, tổng 2,
đã ghi nhận cho job 0. CAPT mới tại thời điểm lấy mẫu không còn E100/cờ cảnh báo,
counter máy in completed=2. Không được gán counter vật lý này vào job 0/2 để
đánh dấu hoàn tất: job chưa được xác nhận kết thúc. Giữ đối soát cho người dùng.

## 4. Ca kiểm tra cần có trên server

1. failed 0/2 inactive: không có thanh đang in; chỉ hiện lịch sử lỗi.
2. uncertain 0/2 inactive: cảnh báo đối soát, can_print=false, không tự retry.
3. printing 1/3 active: thanh 1/3, điều khiển đúng ID.
4. CAPT fresh E100: mã E100 nổi bật; không có câu “cảm biến chưa xác minh” chung.
5. CAPT fresh không fault: thông báo CAPT mới; toner=null không biến thành %.
6. stale/offline: không giữ thanh tiến trình như dữ liệu trực tiếp.
7. Xóa Mac queue: không xóa/làm hoàn tất job Windows hay job web khác.
