# Tăng tốc in web — bàn giao dev server 04/11/2026

## Phạm vi và tình trạng

Đã sửa nguồn firmware, kiểm thử mô phỏng và build public 2026110401.
Theo yêu cầu người dùng đã OTA Máy 1 lúc rảnh: đọc lại version 2026110401,
self-test đạt, nhận LBP3300/CAPT fresh và heartbeat HTTP 200; cấu hình giữ nguyên.
Phát hành bin/manifest cùng nguồn lên Git để các bo tự cập nhật lúc rảnh.
Chưa nghiệm thu tốc độ/độ ổn định in thật; chưa thay đổi ứng dụng/config/service server.
Không phát lại lệnh cũ hoặc in thử khi triển khai tài liệu này.
ESP vẫn là nguồn trạng thái duy nhất; cloud không phụ thuộc Mac/Windows bật.

## Bằng chứng

- Firmware đang dùng 2026110201 chờ counter[3] (hoàn tất) trước mặt thứ ba.
  Hai mặt tương ứng một tờ duplex: có thể làm cạn pipeline.
- LAN Mac dùng counter[1] (đã bắt đầu), không phải completed.
- Job a67120ca994abcd222229825af5326ee: tạo 10:49:03 UTC,
  render xong 10:49:40, hoàn tất 10:50:13 ngày 04/11; 844984 byte, 2 mặt.
  37 giây đầu gồm chờ worker và render; chưa có timing tách từng stage.
- Job 243ab26168aa87f22eb7b34ba0b5710c: 68 giây tổng, 743132 byte, 2 mặt.
  Không dùng hai mẫu này để suy ra tốc độ ổn định hoặc ppm.
- Renderer production khác bản tham khảo server-renderer/ trong repo ESP.
  Chromium persistent đã tồn tại. Heartbeat chạy riêng, không phải HTTPS
  đồng bộ mỗi mặt. Không tối ưu lại hai phần này theo giả định cũ.

## Firmware đã sửa

station/cloud_print.h: trước mặt N, chờ started >= N-2 và completed >= N-4.
Tối đa hai mặt chưa started và bốn mặt chưa completed, tính cả mặt sắp gửi.
Đây là cửa sổ hữu hạn, không phải kết luận máy có dung lượng đệm bốn mặt.
Giữ nguyên kiểm tra decoded chính xác, ACK, timeout, lỗi/giấy, checkpoint
whole-sheet, cancel và cấm tự retry raster đã timeout. Chỉ counter hoàn tất
mới được ghi nhận tiến trình; không dùng started làm giấy đã in.

check_cloud.py mô phỏng chuỗi 8 mặt, chặn cả hai giới hạn; các kiểm thử
paper wait/recovery vẫn chạy. Mô phỏng không chứng minh hết treo bulk USB.
Bản này được phát hành theo yêu cầu người dùng; vẫn cần nghiệm thu vật lý.

## Server: thứ tự triển khai

### 1. Đo trước và sau

Thêm thời gian monotonic theo job_id: queue_wait_ms, html_pdf_ms,
raster_ms, compress_ms, artifact_ms, render_total_ms; ở event/telemetry
đo claim, tải xong, submitted đầu và completed đầu/cuối khi ESP có dữ liệu.
Không trừ trực tiếp hai đồng hồ khác máy nếu chưa đồng bộ; ghi receive-time
server và duration ESP riêng. Không log token, URL ký, nội dung đề hoặc credential.
Không thêm thời gian giả nếu firmware chưa cung cấp. Giữ API hiện hữu tương thích.

### 2. Tối ưu renderer thật

Files: scripts/render_printer_job.mjs, tools/esp32-renderer/render.py,
scripts/worker_printers.php, app/printer_batches.php.
Hiện Python khởi chạy bwrap/pdftoppm riêng từng trang nguồn ở 4960x7014 gray,
sau đó Pillow ghép n-up, dithering và capt-compress từng mặt.

- Raster một dải trang bằng một tiến trình pdftoppm trong sandbox hiện hữu;
  tiêu thụ/giải phóng ảnh từng mặt. Giới hạn RSS, thời gian và file tạm;
  không giữ toàn bộ ảnh gray của PDF trong RAM hoặc tăng worker vô hạn.
- Sau khi đo bản theo lô, thử raster đúng kích thước ô n-up hoặc imposition
  PDF trước raster. Giữ đầu ra 600 dpi, margins, chiều xoay, duplex binding,
  crop, font và chữ toán. Không giảm dpi để đổi lấy tốc độ.
- Giữ kiểm tra giải nén HISCOA và validation artifact.
- Batch parts phải thẳng ranh giới tờ hai mặt. Đo riêng khoảng nghỉ giữa part;
  không claim trước nhiều part vượt bộ nhớ 4 MiB hoặc mất quyền cancel.
- Không sửa theo bản server-renderer/ cũ hoặc ghi đè thay đổi dev đang làm.

### 3. Cache render an toàn

Cache artifact bất biến, không cache lease/job trạng thái. Key gồm hash snapshot
đã chuẩn hóa + toàn bộ options ảnh hưởng đầu ra + renderer/font/assets version
+ model CAPT. Hash nội dung asset, không chỉ URL. Tôn trọng quyền tài liệu trước
mỗi lần dùng cache; lưu private ngoài webroot, quota/TTL, atomic write và khóa
chống render trùng. Xác minh SHA256/schema/options khi đọc; lỗi thì render lại.
Với schema hiện tại chứa copies/collate, đưa chúng vào key; chưa tái sử dụng
artifact giữa các options khác nhau. Cache manifest và mọi part như một tập.

### 4. Nút in LAN trên web

Thêm lựa chọn “In qua máy in đã cài” bên cạnh in cloud. Dùng trang in hiện hữu
với cùng CSS/font/toán và window.print(); người dùng chọn queue ESP đã cài.
Không tạo đồng thời cloud job. Không báo hoàn tất dựa vào afterprint:
đóng hộp thoại không chứng minh đã in. Không cần PC agent nền.
Chỉ phù hợp thiết bị có driver và truy cập LAN; từ mạng ngoài vẫn dùng cloud.
Không dùng Web Serial 115200 làm đường tăng tốc (~11.5 KB/s tối đa), không
mở ESP ra Internet, không fetch HTTP LAN từ HTTPS như giải pháp mặc định.
Browser JS/WASM CAPT chưa thuộc đợt này: phức tạp và không sửa nghẽn USB.

## Nghiệm thu và dự đoán

Cùng PDF/options, so LAN và cloud: simplex, duplex cạnh dài/ngắn, n-up,
ít nhất 10 tờ và một job lớn chia part. Ghi thời gian tờ đầu, khoảng cách tờ,
tổng thời gian; xác nhận giấy thật, không chỉ counter. Thử hết giấy giữa job,
đóng nắp sau xử lý, pause/resume/cancel; không phát lại mặt đã hoàn tất.
Nếu bulk stall tái diễn, dừng phát hành và quay về cửa sổ completed=2;
không replay lệnh đang có kết quả chưa rõ.

Mục tiêu: giảm 30–60% thời gian raster bằng tối ưu theo lô/n-up (chưa đo),
cache hit bỏ phần lớn render nhưng vẫn phải tải/in; pipeline hướng tới nhịp
LAN, không cam kết ppm hoặc hệ số tăng tốc trước phép đo cùng tài liệu.
Các lần phát hành tiếp theo cần nghiệm thu; public bin phải build không bí mật, tăng
version và cập nhật bin/manifest nguyên tử theo quy trình repo hiện tại.
