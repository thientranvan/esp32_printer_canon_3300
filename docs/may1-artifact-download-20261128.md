# Máy 1 tải thiếu file in từ web — 28/11/2026

Phạm vi: chỉ Máy 1, ID `esp32-98c377a136b0`, IP `192.168.1.199`; không thao tác Máy 2. Mac chưa nhận cổng COM, chỉ có Bluetooth/SoundMax/debug-console nên đọc qua LAN và SSH server. Giữ nguyên Wi-Fi Mac. Máy 1 chạy FW 2026112606 theo heartbeat server, USB/CAPT fresh, không lỗi giấy/nắp/service và không giữ job đang in khi kiểm tra.

## Bằng chứng

Các GET artifact của Máy 1 trong Nginx trả HTTP 200 nhưng kết thúc trước số byte trong claim:

| Job | Byte phải có | Byte Nginx ghi đã gửi |
| --- | ---: | ---: |
| job_f2efb95e81e792f5138d82ee8f906ceb | 1889748 | 458696 |
| job_f59c30684c99806403091ba960e7cf5a | 1310332 | 245488 |
| job_2731559bd490631d261c1374fb592a48 | 1101276 | 372448 |
| job_3eafa997859a2cde82d7fae5187d817c | 1101276 | 331488 |

Cả bốn job failed, progress_completed=0, lỗi chung `Download, checksum, memory or format validation failed; no pages sent`. Không suy số giấy thực tế chỉ từ progress0; đường code xác nhận chưa gọi USB khi chuẩn bị file thất bại.

File job cuối trên đĩa server có đúng 1101276 byte, SHA-256 khớp DB; validator server đạt 2 trang/copies1/duplex3. Tải file bằng SSH chỉ để kiểm tra cấu trúc: `Prepared::parse` production trên Mac PASS, 2 trang/2 mặt. Không render/in tài liệu, không commit file hoặc nội dung tài liệu. Do đó file render cuối không hỏng theo các phép kiểm tra này. Chưa có UART/TCP capture đủ để kết luận nguyên nhân tầng thấp duy nhất của việc kết nối kết thúc.

## Sửa thử 2026112801

Thay vòng open/fetch_headers/read thủ công (read <=0 dừng ngay) bằng native asynchronous `esp_http_client_perform` GET, event DATA chép trực tiếp vào buffer PSRAM đã cấp. Tiếp tục cùng request khi EAGAIN, không set lại headers/body; timeout mỗi bước tối đa 15 giây, tổng vòng tải 120 giây. Xác nhận HTTP200, framing hoàn tất, số byte chính xác, Content-Length khớp hoặc chunked hợp lệ, SHA-256 và `Prepared::parse` trước USB.

Tối đa 3 GET mới khi lỗi truyền/truncated hoặc HTTP5xx/408/429, vì chưa gửi USB và GET không in giấy. Không retry 403/401/404, payload vượt giới hạn, Content-Length sai, checksum sai. Không replay lệnh in đã gửi USB. Đóng/cleanup handle mỗi attempt, không nối hai file tải dở vào nhau.

Lỗi tách riêng PSRAM allocation, download/framing/checksum và ESPJOB validation; thông báo download ghi phase, received/expected, HTTP và attempt, dưới giới hạn 100 ký tự cột error_code server. `cloud.download` thêm attempt/expected/received/duration_ms/http/error/phase, không chứa token/lease, pixel hoặc tiêu đề. Server có thể hiển thị tiến trình tải riêng, không gọi đó là số mặt đã in. Không sửa ứng dụng/DB/service server trong lượt này.

Bản thử giữ thay đổi stack IPP sang PSRAM của canary 2608; chưa phát hành OTA chung. Chưa nghiệm thu in thật qua web trên 2801 tại thời điểm ghi.

## Kiểm thử

`python3 check_cloud_download.py`: compile production GET receiver, fake TLS/HTTP DATA phân mảnh, đúng 1101276 byte; tái hiện truncated331488 byte rồi thử lại thành công; hết 3 lần vẫn failed; chunked/EAGAIN và deadline; HTTP403/503; overflow canary, độ dài sai, checksum sai, hash error, allocation và cleanup. ASan/UBSan; không có lệnh USB trong harness. Các test HTTP POST/cloud engine, OTA và homepage cần giữ đạt trước nạp.

`cloud.download` là chẩn đoán lần tải gần nhất, không phải hàng đợi hay tổng mặt in. Chỉ hiện đang tải khi cloud.state=downloading. Các event fail chỉ được gửi sau khi dừng tải/thử lại và chưa đụng USB; thông báo lỗi không vượt 100 ký tự. Không tự đưa các job failed cũ vào hàng đợi hoặc gửi lại tài liệu cũ.

## Nạp canary

Build cuối 1429392 byte, kiểm tra public-build không có mật khẩu riêng. Đã nạp OTA qua LAN riêng Máy 1 sau kiểm tra đúng ID/không job active hoặc cloud. /status xác nhận version2026112801, uptime37s, CAPT fresh, cloud idle/heartbeat200, download.phaseidle. Mac vẫn không thấy COM; không mở hoặc reset serial giả định. Dùng curl cho thao tác LAN vì Python urllib có lúc No route to host trong khi curl đọc đúng cùng IP/MAC; đây là quan sát từ Mac, không dùng làm bằng chứng rằng ESP vừa mất Wi-Fi.

Đã đề nghị người dùng gửi đúng một lệnh web mới để nghiệm thu, không tự replay jobfailed cũ. Giữ stable OTA2606 trong thời gian thử; chưa phát hành2801 cho Máy2/3. Chưa sửa server app/DB/service; chỉ đọc log, metadata, kiểm tra artifact và chuẩn bị docs.

## Lượt web thật sau sửa

Người dùng gửi đúng một lệnh mới tới Máy 1: job_75bae92c587e0d19289490b9ec1a2e03. ESP 2801 ghi cloud.download attempt1, expected1101276, received1101276, HTTP200, error0, phaseverified, duration10854ms. Sau đó job completed2/2, inactive, cloudidle/errorrỗng. Nginx xác nhận GETdata lúc08:53:57UTC trả200 và gửi đủ1101276byte, chỉ một request trong lượt này. Không cần retry để đạt lượt này, nhưng các ca retry/truncated vẫn được kiểm tra bằng harness.

Chờ người dùng xác nhận giấy/nội dung, không dùng counter2/2 thay xác nhận giấy. Stable manifest vẫn2606 trong lúc chờ; chưa khẳng định đã loại hết lỗi tầng mạng/Wi-Fi chỉ bằng một lượt thành công.

## Phát hành OTA chung theo yêu cầu người dùng

Người dùng yêu cầu phát hành sau lượt tải đủ và counter completed2/2. Promote đúng image2026112801 đã nạp Máy1, không build lại: 1429392byte, SHA-256 6e9aa812e9585e2428d710b576b1c72c7573a567fd863b15758d26fabfc91adb. firmware/station.bin và latest.json phát hành cùng commit. Các bo tự kiểm tra khi có mạng/rảnh theo gate hiện có; không ép reboot phiên in đang chạy hoặc bỏ khóa đối soát.

Giữ hardware_validated=false/release_stageairprint-preview cho profile rộng. Chưa có phản hồi xác nhận giấy/nội dung của lượt mới tại thời điểm phát hành; kết quả đã xác minh là tải đủ/hash/framing/format đạt, server ghi gửi đủ byte và ESP completed2/2. Không gọi phát hành là bằng chứng cả Máy2/3 đã nạp hoặc mọi lỗi mạng đã hết. Không cần đổi bộ cài máy tính cho sửa download firmware này.
