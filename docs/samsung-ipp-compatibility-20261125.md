# Samsung Default Print Service / Máy 1 — 25/11/2026

## Bằng chứng trước sửa

Người dùng chọn Máy 1, Dịch vụ in mặc định Samsung, báo Print error và Restart không thành công. Máy 1 thực tế chạy FW 2026112502; USB/CAPT fresh, không lỗi giấy/nắp/service/transport; job ESP cuối là job Mac đã completed 4/4. Chưa có job IPP được ESP chấp nhận trong lúc đọc mẫu.

Hai phép kiểm tra không in giấy trên bo:

- `ipptool` mặc định IPP 1.1 Get-Printer-Attributes nhận response IPP 2.0 và FAIL: `Bad version 2.0 in response - expected 1.1 (RFC 2911 section 3.1.8)`.
- Validate-Job IPP 2.0 với media-col A4 (nested media-size + margins/type/source) trả client-error-bad-request.

Đây là lỗi tương thích đã tái hiện, chưa phải capture chính request Samsung. Không suy lỗi giấy hoặc ép reset máy in để xử lý lỗi protocol.

## Sửa FW 2026112503

- Request ghi version; response dùng cùng version thay vì cố định 2.0.
- Parser collection bounded tối đa hai lớp cho media-col/media-size A4. Validate dimensions/margins/source/type; không chấp nhận giấy khác hoặc collection bất kỳ; tránh chỉ bỏ qua cấu hình giấy của điện thoại.
- Quảng bá media-size-supported, media-col-supported/default/ready/database, margins, source và type đúng profile A4 hiện có. Null/empty IPP collection value không làm pointer arithmetic trên nullptr.
- Nhận job-uri và đường HTTP `/ipp/print/<job-id>` cho truy vấn/cancel theo URI, không chỉ URI printer root; nhận HTTP/1.0 và 1.1. Giữ kiểm tra ID/path, subnet và framing.
- `/status.ipp` thêm diagnostic metadata: operation/status/phase/attribute, MIME, byte nhận, raster width/height/DPI/bits/space; giữ lỗi gần nhất ngay cả khi Get-Printer-Attributes ghi đè request cuối. Không log tài liệu, pixel, title, username, token/password. Last-error có age_ms để phân biệt lỗi cũ.
- Raster convert cho phép lấy metadata header khi từ chối để chẩn đoán giới hạn thực tế client; không tự scale/coerce giấy/DPI chưa hỗ trợ.

## Kiểm thử

check_airprint.py thêm test IPP response 1.1/2.0, media-col nested/margins/source/type, reject collection bị cắt/khổ khác và job-uri; giữ libcups URF/PWG/raster/duplex/giải nén đối chiếu pixel/HTTP/ASan/UBSan. Fixture read-only chạy trên bo trong `tests/airprint/ipp/`:

```sh
ipptool -tv -T 10 ipp://192.168.1.199:631/ipp/print tests/airprint/ipp/get-printer.test
ipptool -V 2.0 -tv -T 10 ipp://192.168.1.199:631/ipp/print tests/airprint/ipp/android-a4-validate.test
```

## Nghiệm thu canary

Đã nạp OTA trực tiếp qua LAN riêng Máy 1 khi không có job active, giữ cấu hình/NVS. GET /status xác nhận FW 2026112503. Hai fixture live ở trên đều PASS, CAPT vẫn fresh/Ready. Không in trang thử từ agent, không gửi lại tài liệu cũ. Đã yêu cầu Samsung hủy các job lỗi cũ và gửi đúng một trang mới; chờ kết quả thật trước khi khẳng định Samsung in được.

Chưa đẩy manifest 2026112503 lên kênh OTA chung trong thời gian canary. Bộ cài máy tính không liên quan hai lỗi IPP này.

Artifact canary: firmware/experimental/airprint-2026112503.bin + JSON SHA-256/size. Build lại bằng `python3 build_airprint_experimental.py --version 2026112503`. `firmware/latest.json` và station.bin vẫn giữ 2026112502 để không tự cập nhật Máy 2/3 khi Samsung chưa thử xong.

## Lượt Samsung thật và canary 2026112504

Lượt một trang người dùng gửi bằng Dịch vụ in mặc định đã tới Print-Job, MIME image/pwg-raster. Header nhận được 4960 × 7015, 600 dpi, gray8/color space 18. FW 2026112503 trả 0x0411 ở bước chuyển raster; chưa tạo job IPP, chưa có giấy xác nhận. USB/CAPT fresh và các job cloud sau đó vẫn hoàn tất. Đây không phải bằng chứng Samsung đã in được.

2026112504 thử riêng Máy 1:

- Body HTTP dùng buffer 4096 byte, giảm socket read từng byte khi giải mã RLE; giữ framing, giới hạn dung lượng, timeout và kiểm tra đầu vào.
- Chờ tối đa 2 giây cho USB owner chẩn đoán thoáng qua; không chiếm phiên in/OTA/cloud thực sự đang bận.
- Giữ lỗi raster trước các lỗi busy/HTTP404 phụ; phân biệt lỗi size/DPI/color/order/stride/bits-per-color/channel-count/duplex/RLE/read. Cập nhật số byte khi xuất band và phase receiving-raster.
- Test socket đọc nhỏ vượt biên buffer: 10.000 byte đọc từng byte chỉ cần ba lần đọc socket, đồng thời giữ test input phân mảnh/chunked/timeout và đối chiếu pixel libcups.

Kênh OTA chung vẫn 2026112502. Canary 2026112504 chưa được nghiệm thu Samsung, không tự phát hành cho Máy 2/3.

Đã nạp 2026112504 riêng Máy 1 lúc rảnh; xác nhận version qua /status, CAPT fresh/không lỗi. Get-Printer-Attributes IPP1.1 và Android A4 media-col Validate-Job live đều PASS. check_airprint.py (ASan/UBSan, pixel và buffer/framing) và check_cloud_http.py PASS. Chờ lượt Samsung mới; chưa có xác nhận giấy trên 2504.

Lượt Samsung mới trên 2026112504: sau khoảng 29 giây diagnostic receiving-raster đã nhận 3.932.004 byte. Sau đó ESP tạo job ipp-326968377 và xác nhận completed 2/2 mặt, CAPT fresh, không cờ lỗi. Chưa có xác nhận giấy/nội dung từ người dùng tại thời điểm ghi; số mặt là số ESP báo, không tự quy thành số tờ thực tế. Không còn lỗi raster-rejected trong lượt này. Chưa xác định chắc lỗi cũ là timeout socket; buffering là thay đổi đã giúp lượt mới đi hết pipeline.

## Xác nhận giấy và lỗi còn lại — 2026112505

Người dùng xác nhận Samsung đã ra giấy trên 2504, nhưng tài liệu dọc chọn long-edge thì mặt sau ngược 180°; điện thoại vẫn báo lỗi thay vì hoàn tất. Không coi 2504 là nghiệm thu đủ.

Canary 2505 đổi PWG sheet-back từ normal sang rotated và URF DM1 sang DM3 (cả IPP và mDNS). Đây là thông tin renderer client dùng để xoay mặt sau; không sửa chiều raster CAPT của Windows/Mac/web. Chưa khẳng định đúng chiều giấy cho tới lượt thử thật.

HTTP IPP trước đây chỉ xử lý một kết nối: nhận/chuyển raster giữ listener nên truy vấn trạng thái đồng thời phải chờ. Tách listener và tối đa hai handler có giới hạn tài nguyên, vẫn chỉ nhận một job in; diagnostics riêng từng request tránh trộn lỗi giữa upload và polling. Không tăng queue hoặc phát lại tài liệu. Bộ đếm IPP hoàn tất giữ riêng khi job Mac/web khác thay snapshot. Ghi phase response-write-failed nếu gửi response không đủ; không tự in lại.

Raster chuyển hàng nguồn sang allocation stdlib nhỏ, dựng 592 byte mono trong RAM cục bộ rồi memcpy vào band PSRAM, thay vì sửa từng bit trực tiếp trên PSRAM. Giữ output đối chiếu pixel không đổi. Mục tiêu giảm thời gian giữ request; chưa có số đo tốc độ trên ESP ở phiên này.

Check live không in: `python3 check_ipp_live.py 192.168.1.199`; giữ một request Get-Printer-Attributes thiếu byte cuối và yêu cầu kết nối thứ hai vẫn trả lời dưới timeout ba giây, đồng thời kiểm tra rotated/DM3. Sau lượt in thật có thể thêm `--completed-job <id>` để kiểm tra Get-Job-Attributes trả completed qua job-uri.

Trước OTA 2505, chạy check_ipp_live.py trên 2504 đã tái hiện socket.timeout ở kết nối thứ hai trong 3 giây khi kết nối Get-Printer-Attributes đầu còn thiếu delimiter; không gửi Print-Job hoặc tác động USB. Đây là bằng chứng head-of-line blocking, chưa đủ kết luận duy nhất nguyên nhân thông báo lỗi của Samsung. Bộ test raster/pixel/ASan/UBSan và check_cloud.py PASS sau sửa.

Sau khi job web hoàn tất 6/6, OTA riêng Máy 1 lên 2026112505, xác nhận version qua /status và CAPT fresh. Cùng check_ipp_live.py trên bo mới PASS: kết nối thứ hai trả thành công khi request đầu còn thiếu byte, quảng bá rotated/DM3 đúng. Đã yêu cầu thử lại Samsung tài liệu dọc hai trang long-edge và kiểm tra cả giấy lẫn thông báo điện thoại. Canary chưa lên manifest OTA chung.

## Lượt chưa in trên 2505, chẩn đoán 2506

Samsung tiếp tục báo Print error. Mẫu đầu trùng lúc Máy 1 có job cloud 4 mặt; khi cloud hoàn tất 4/4 và user gửi mới, đã bắt Print-Job PWG thật: 4960×7015, 600dpi, gray8/space18, nhận 524160 byte rồi trả 0x0411 sau khoảng 4,6 giây. Chưa tạo job IPP, chưa gửi trang tới USB. Vì vậy busy/cloud không giải thích hết lỗi; chưa khẳng định điện thoại ngắt trước hay firmware từ chối trước.

2505 ghi đè phase raster bằng response-write-failed; thông tin này không đủ kết luận nguyên nhân. Canary 2506 giữ phase gốc và tách response_write_failed thành boolean, thêm last_error_received và raster_line, phân biệt EOF/timeout/read/framing, row-repeat overflow và spool/memory. Đã nạp riêng Máy 1 lúc rảnh, xác nhận version và CAPT fresh. Chờ lượt Samsung thật để xác định nhánh lỗi; không phát hành OTA chung.

Test bổ sung: libcups tạo raster gray không đều >524160 byte (URF/PWG), decoder Hi-SCoA độc lập đối chiếu pixel sau dithering; HTTP chunk 32760/65520 byte qua buffer 4096 và read từng byte, tổng 600000 byte. check_airprint.py PASS với ASan/UBSan; chưa xem đây là thay thế capture thực tế Samsung. Không lưu tài liệu/pixel/username/password/token từ điện thoại.
