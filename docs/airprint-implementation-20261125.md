# AirPrint/IPP LBP3300 — bản thử 2026112501

**Cập nhật tiếp cùng ngày:** người dùng yêu cầu OTA cho các bo. Bản 2026112502 bật IPP trên public OTA; job AirPrint báo source `lan` để tương thích điều khiển server. Chưa nghiệm thu hardware. Những đoạn mô tả chỉ-build/OTA chưa bật bên dưới ghi lại giai đoạn 2026112501. Xem [release OTA](airprint-ota-release-20261125.md).

## Phạm vi theo người dùng

Người dùng yêu cầu triển khai driverless LAN; sau đó chọn **chưa có thiết bị, chỉ build bản thử**. Chưa nạp lên Máy 1/2/3, chưa thay stable OTA, chưa xác nhận iPhone/Android phát hiện hoặc giấy thật. Bản thử riêng, không gọi là hỗ trợ AirPrint đã nghiệm thu.

## Đã triển khai

- `station/ipp_protocol.h`: parser IPP 1.1/2.0 bounded, response encoder. Các thao tác Print-Job, Validate-Job, Get-Printer-Attributes, Get-Job-Attributes, Get-Jobs, Cancel-Job. Validate bản in A4/600dpi, copies 1–99, simplex/duplex dài/ngắn, từ chối tùy chọn được nhận diện là chưa hỗ trợ thay vì lặng lẽ in sai.
- `station/ipp_server.h`: HTTP port 631, endpoint `/ipp/print`, native IPP client LAN-only, từ chối Origin. HTTP Content-Length hoặc chunked, không chấp nhận cả hai/duplicate framing. Hạn mức body 64MiB, header 8KiB, attribute 16KiB, thời gian nhận 120s, idle 15s. Một job nén tối đa 4MiB trong PSRAM, tổng 40 trang logic. Chỉ nhận khi CAPT đang đọc mới/không lỗi và giành được gate bảo trì + USB arbiter.
- mDNS `_ipp._tcp` với subtype `_universal`, URF/PWG, độ phân giải 600dpi, mono, duplex; tên hiển thị trạm. Chưa hỗ trợ TLS IPPS hoặc print từ Internet qua cổng 631. Không mở router.
- `station/raster_job.h`: decoder URF/Apple Raster và PWG Raster compressed (`RaS2`); modified PackBits + lặp dòng. Hỗ trợ mono1 black/sgray, sgray8, sRGB24; A4 full-page khoảng 4960x7014 ở 600dpi. Crop vùng in theo encoder 3300 hiện có (120px lề trái/trên, 4736x6774 pixel); ordered dither 4x4; giữ một row và band 70 dòng, không giữ trang RGB trong RAM.
- Hi-SCoA upstream captdriver commit đã pin, giấy phép/source kèm firmware, cùng sửa alignment đã dùng ở renderer Ubuntu. Input band 41440 byte; scratch output 82944 byte; row lớn nhất 14886 byte. Các buffer chính lấy PSRAM. Không nhúng driver Canon độc quyền hoặc code từ firmware MrOld.
- CAPT nén được đóng ESPJOB1 và validate lại bằng parser hiện có. Dùng lại engine in CAPT/pipeline và checkpoint hiện có dưới owner độc quyền; tiến trình lấy counter thật của ESP. Source job `ipp`, thể hiện như LAN trong heartbeat. Bộ cài/đường CAPT cũ vẫn tồn tại.
- Chặn cloud claim/LAN print/OTA khi đang nhận hoặc in IPP; dùng gate đã có, không chạy hai engine song song. Pause/cancel dùng job control hiện có. Không tự chuyển LAN/cloud hoặc gửi lại khi kết quả không rõ.
- Diagnostic USB reset thêm `repair_step` (0 host halt/flush; 1 printer-class reset; 2 clear OUT; 3 clear IN) và `repair_transfer_status` (raw USB_TRANSFER_STATUS, -1 trước callback). Không còn chỉ ESP_FAIL tổng hợp khi thử trên bo.

## Giới hạn bản thử cần kiểm chứng/cải thiện

- Nhận/encode/validate cả tài liệu trước khi in: giữ được kiểm tra toàn bộ đầu vào, nhưng chưa tối ưu tờ đầu bằng receive-print đồng thời. Sau khi có giấy thật mới tách pipeline hai buffer/streaming; không hy sinh validation/idempotency cho tốc độ.
- IPP listener bản đầu xử lý một request tại một thời điểm; IPP query có thể phải chờ upload hiện tại xong. Web/SSE trạng thái vẫn ở task khác. Cần tách worker nhận/upload khỏi control IPP nếu phép thử client cho thấy timeout.
- Chỉ một job, không spool durable hoặc giữ nhiều tài liệu trong flash. IPP Get-Jobs giữ job gần nhất trong RAM. Mất nguồn mất spool/ID; không tự reprint sau reboot.
- Tình huống bulk USB ngắt giữa IPP job: job chuyển uncertain/aborted theo tiến trình hiện có; chưa có vòng replay IPP giữ spool đến phục hồi. Không coi đó là khôi phục hoàn chỉnh mọi lỗi. Các lỗi giấy/nắp đọc được vẫn đi qua wait engine hiện có.
- Chưa hỗ trợ PDF/JPEG qua IPP, 300dpi, khổ khác A4, media-col collection, page-ranges, finishing ngoài none, layout N-up trên ESP, uncollated copies. Không quảng bá PDF renderer. Điện thoại làm layout/raster trước khi gửi; một số client có thể yêu cầu media-col hoặc khác profile nên cần bắt capture và bổ sung chính xác sau.
- Chỉ build + kiểm thử host. Chưa chứng nhận AirPrint/Mopria/IPP Everywhere, chưa khẳng định Android/iOS dùng được ngay, duplex giấy đúng chiều hoặc tốc độ tương đương dây USB.
- Các thay đổi arbiter/telemetry có trong source nhưng không được phát hành vào stable image ở lượt này; `firmware/latest.json` vẫn 2026112101.

## Build

```sh
python3 check_airprint.py
python3 check_usb_printer_repair.py
python3 check_paper_wait.py
python3 check_capt_session_recovery.py
python3 build_airprint_experimental.py
```

Artifact: `firmware/experimental/airprint-2026112501.bin` và JSON SHA-256/size/channel. Build chỉ app, private provisioning bắt buộc có sẵn; không thay bảng phân vùng/NVS. Script từ chối nếu tìm mật khẩu device_secret trong public image.

Host check dùng **libcups** tạo PWG và Apple URF thật ở mono/gray/RGB, nhiều trang và ba chế độ duplex; converter production nén, parser production kiểm tra, decoder Hi-SCoA độc lập giải nén từng band đối chiếu mọi pixel. ASan/UBSan, header sai/kích thước/DPI/truncation/trailing bytes và IPP options. Test production HTTP Body với fragmented chunked/content-length, chunk lỗi, kích thước quá mức, timeout. Các phép đo host này không cho biết tốc độ CPU ESP hoặc RAM thấp nhất khi TLS đang chạy.

## Nghiệm thu tiếp khi có thiết bị

1. Chọn Máy 3 rảnh, xác nhận không có phiên dở; nạp thủ công app thử qua COM hoặc OTA đã được phép. Giữ nguồn và backup cấu hình; không nạp lên cả ba.
2. Điện thoại cùng Wi-Fi 2.4/LAN hỗ trợ mDNS, tắt AP isolation. Kiểm tra hiển thị đúng trạm trong hộp thoại in iOS; Android cần print service hỗ trợ IPP/PWG và discovery tương thích.
3. In một trang A4 chữ/ảnh/cỡ nhỏ, so bản CAPT cũ ở cùng quality. Xác nhận giấy, lề, chữ và pixel. Đo upload/decode/encode, tờ đầu và khoảng cách 10 trang; theo dõi minimum heap/PSRAM và watchdog.
4. Thử duplex 3 trang cả cạnh dài/ngắn, copies, cancel/pause/resume, hết giấy và mở/đóng nắp. Chỉ nâng profile sau khi được xác nhận giấy đúng.
5. Giữ nguyên UART monitor xuyên phiên thử để bắt USB timeout/reset step/status; không reopen COM gây reboot. Sau đó mới quyết định escalated recovery, không cưỡng bức power-cycle root port đã từng làm SDK abort.
6. Kiểm tra các đường cũ Windows CAPT/LAN/cloud và OTA vẫn đúng; mới phát hành bản chính thức riêng.

## Server/web

Bản thử LAN không cần endpoint server mới hay renderer VPS. Server cần nhận source `ipp` như lệnh LAN do ESP báo, hiển thị trạng thái/tiến trình hiện có, không tự tạo job cloud hoặc renderer cho job này. Thêm hai field repair diagnostic dạng optional, không suy raw enum ra lỗi giấy. Chưa sửa ứng dụng/database server.

Nút “In” web: muốn bỏ render VPS khi điện thoại cùng LAN thì mở tài liệu trên client và hộp thoại in native; khi in từ xa thì giữ đường server render/cache hoặc client render đã có. AirPrint LAN không tự làm printer discovery xuyên Internet.

## Kết quả build/test trong lượt này

Build ESP32-S3 N16R8 experimental: khoảng 1.41 MB/3 MB, globals khoảng 66.9 KB (buffer raster/spool cấp runtime trong PSRAM). Build không bật flag AirPrint cũng đạt. Không cập nhật manifest/image stable 2026112101. Không đo runtime minimum heap hoặc tốc độ ESP bằng số kích thước build này.

Các test host đã đạt: check_airprint.py (libcups + decoder đối chiếu toàn bộ pixels, ba pixel format, nhiều trang/duplex, malformed input, HTTP framing/timeouts, ASan/UBSan); check_cloud.py; check_usb_printer_repair.py; check_capt_session_recovery.py; check_paper_wait.py; check_cloud_http.py; check_firmware_update.py; check_home.py; check_cloud_recovery.py; check_lan_recovery.py. Phép đọc hardware check_telemetry.py không thực hiện được vì .199 không truy cập được; không tính là đạt. Người dùng đã chọn chỉ build, nên không tiếp tục yêu cầu nạp hoặc in thử.
