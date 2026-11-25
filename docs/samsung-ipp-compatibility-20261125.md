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
