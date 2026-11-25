# Nghiên cứu fw.mrold.xyz: driverless LAN trên ESP32-S3 — 25/11/2026

## Nguồn và cách kiểm tra

- Trang: https://fw.mrold.xyz/
- Catalog public: https://fw.mrold.xyz/firmwares/catalog.json
- JavaScript trang nạp tại thời điểm xem: https://fw.mrold.xyz/assets/index-y27q5I2_.js
- Firmware public: https://fw.mrold.xyz/firmwares/Universal_AirPrint.enc và https://fw.mrold.xyz/firmwares/full_firmware_lbp.enc

Đã tải catalog và ảnh public, dùng cùng định dạng giải mã có sẵn trong trang nạp để kiểm tra chuỗi tĩnh. Không chạy code firmware, không nạp lên ESP người dùng, không sửa bo/server. Không commit firmware hoặc khóa đóng gói của bên thứ ba. Phân tích chuỗi không thay thế source code, kiểm thử protocol hoặc phép đo trên thiết bị.

## Bằng chứng trực tiếp

Catalog có Universal AirPrint v1.0.0 cho ESP32-S3 N16R8. Có bản riêng Canon LBP AirPrint v1.2.0, liệt kê LBP2900, LBP6030/6040/6018L; không liệt kê LBP3300. Bản debug hướng dẫn gửi log từ IP:8080/log để tác giả thêm model.

Trong cả ảnh Universal và Canon thấy các chuỗi:

- `ipp/print`, `/ipp/print`, `ipp-versions-supported`, `ipp-features-supported`, `document-format-supported`.
- Danh sách PDL `application/octet-stream,image/urf,image/pwg-raster`, các thuộc tính `urf-supported`, `pwg-raster-document-type-supported`, `pwg-raster-document-resolution-supported`.
- `not a valid URF / PWG raster document` và log thời gian waiting for data / printer output / rendering / decoding.
- Module mDNS và tên AirPrint. Universal có log convert / dither / compress+emit.

Ảnh Canon còn có `canon-lbp2900`, `canon-lbp2900b`, `canon-lbp6030`, log gửi/nén từng band và khởi tạo engine CAPT. Đặc biệt có log: `unknown CAPT printer: asking it for its status only (no print), for its log`. Không tìm thấy chuỗi LBP3300 trong hai ảnh. Không thấy MIME application/pdf trong phần chuỗi kiểm tra; không suy diễn rằng ESP đang có một PDF/Word renderer đầy đủ.

Kết luận có cơ sở: firmware triển khai IPP/AirPrint nhận raster tiêu chuẩn và có phần chuyển/encode theo driver máy in chạy trên ESP. Catalog/binary chưa chứng minh LBP3300 được hỗ trợ, tốc độ cụ thể hoặc xử lý mọi lỗi giấy/USB đúng. Không được gọi bản Canon đó là bản dùng chắc chắn cho LBP3300.

## Vì sao điện thoại in được mà không cài driver Canon

Hệ thống in sẵn trên iOS/macOS raster hóa tài liệu thành URF; các client IPP/Mopria tương thích có thể gửi PWG Raster. ESP được công bố qua mDNS/DNS-SD và phục vụ IPP, báo các định dạng/khổ giấy/độ phân giải/tùy chọn thực sự hỗ trợ. ESP giải mã raster, đổi màu/halftone và nén sang ngôn ngữ máy in, rồi giao tiếp USB. Driver thiết bị nằm trong firmware; renderer trang nằm trên máy gửi. Không cần một máy tính trung gian luôn bật hoặc VPS render cho đường LAN này.

“Driverless” là không phải cài driver hãng trên máy gửi, không có nghĩa không tồn tại raster hóa hay phần phụ thuộc model. Android còn phụ thuộc dịch vụ in hệ thống, khả năng IPP/PWG và discovery của bản Android; không khẳng định chỉ quảng bá AirPrint là mọi điện thoại đều dùng được.

## Đối chiếu repo hiện tại

`station/station.ino` chỉ quảng bá http + pdl-datastream TCP 9100. `station/capt_tcp.h` nhận CAPT đã được chuẩn bị từ client; `station/prepared_job.h` nhận ESPJOB1. Chưa có IPP server, IPP discovery hoặc decoder URF/PWG, nên điện thoại không thể dùng luồng in hệ thống chỉ nhờ biết IP trạm.

CAPT 3300 hiện có: giao tiếp USB, status/counter, checkpoint mặt/tờ, duplex, pipeline và bộ nén Hi-SCoA đã chạy trên Ubuntu. `server-renderer/compress.c` sử dụng captdriver (GPL-3.0-or-later), nén theo 70 dòng, đầu vào mỗi band 592*70 = 41440 byte, output buffer dự phòng 82944 byte; là điểm tái sử dụng khi port encoder sang ESP. Cần giữ đúng giấy phép khi phân phối firmware dẫn xuất; không lấy code không rõ giấy phép từ binary bên thứ ba.

ESP S3 N16R8 có thể xử lý raster theo dòng/band thay vì giữ toàn bộ trang. A4 600 dpi 4960*7014: mono 1-bit khoảng 4.35 MB, gray8 khoảng 34.8 MB, RGB24 khoảng 104.4 MB. Không thể giữ nguyên cả trang màu trong 8 MB PSRAM. Phải giới hạn input, giải mã tuần tự, cắt lề, dither và nén theo band, điều tiết ngược HTTP/USB. Không nhận thêm job raster đồng thời với cloud đang chiếm PSRAM/USB.

Nhận định cũ “ESP bắt buộc phải có server/máy tính thứ ba render” không đúng cho mọi đường in: điện thoại có thể là renderer, ESP chỉ encode CAPT. Nhận định chính xác hơn: firmware hiện tại chưa triển khai đường IPP raster đó; ESP không cần trở thành PDF/Word renderer tổng quát.

## Hướng triển khai phù hợp LBP3300

1. Thêm IPP + quảng bá AirPrint/DNS-SD và thuộc tính PWG thực sự hỗ trợ. Giữ TCP/CAPT cũ và cloud cho tương thích, dùng chung arbiter/hàng đợi/trạng thái ESP.
2. Nhận URF và PWG Raster từ client; validate header, kích thước, DPI, pixel format, số dòng, độ dài, job limits tại biên mạng. Decode tuần tự, dither mono và encode Hi-SCoA theo band trên ESP.
3. Đưa raster vào đúng giao thức/pipeline LBP3300 đang có. Không coi driver LBP2900 hoặc 6030 là tương đương LBP3300. Kiểm thử A4 một mặt trước, rồi duplex, copies, nhiều trang, cancel, hết giấy, resume/checkpoint và USB timeout.
4. Thử iPhone/iPad và Android thật; xác nhận thiết bị nhìn thấy đúng ba trạm, tùy chọn khớp khả năng, có giấy thật và không in trùng. Sau đó mới dùng driverless cho Mac/Windows nếu client tương thích.

## Hiệu quả và giới hạn

LAN từ điện thoại bỏ được giai đoạn render/chờ queue renderer VPS vì điện thoại làm raster. Không phát sinh dịch vụ render thứ ba. Chưa có benchmark nên không đưa hệ số tăng tốc hay hứa nhanh bằng cắm dây. URF/PWG có thể lớn hơn CAPT đã nén; CPU ESP/USB và flow control vẫn có thể là nút thắt. Thêm AirPrint không tự sửa lỗi CAPT bulk/engine sẵn có.

Nút “In” trên web có hai đường khác nhau: nếu mở tài liệu và gọi hộp thoại in của điện thoại khi cùng LAN, OS có thể gửi raster thẳng tới ESP; nếu VPS tự đẩy job khi người dùng bấm lệnh từ xa, phải có nơi dựng tài liệu (server hoặc chính client) và gửi qua kênh cloud. AirPrint discovery không hoạt động xuyên Internet tự nhiên và không tự làm nhanh renderer VPS hiện tại.

Chưa nạp firmware bên thứ ba, chưa thêm IPP/URF/PWG vào firmware repo trong lượt nghiên cứu này. Chưa có phép đo tốc độ thực tế hoặc xác nhận giấy LBP3300 với firmware tham khảo.
