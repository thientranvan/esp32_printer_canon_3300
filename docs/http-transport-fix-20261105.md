# HTTP/TLS firmware 2026110501 — 05/11/2026

## Bằng chứng và giới hạn kết luận

Đọc docs/esp32-network-server-findings-20261105.md từ /var/www/lop1.
Nginx xác nhận Máy 2 khai Content-Length 2052 nhưng đóng khi chỉ nhận
285 byte header (400 trước PHP). Máy 1 có 499 sau ~1.5 giây khi upstream
chưa trả header. Tách PHP pool không giải quyết tất cả các lỗi này.
Không kết luận mọi 400 là cùng lỗi TLS hoặc mọi 499 do server chậm.

## Thay đổi

station/cloud_print.h dùng API native ESP-IDF esp_http_client_perform thay
vòng open/write/fetch_headers/read thủ công. POST field giữ nguyên bộ nhớ
đến khi synchronous perform kết thúc; ESP-IDF sở hữu gửi TLS/body và framing.
Callback gom response tối đa 8192 byte, hỗ trợ nhiều fragment; chỉ chấp nhận
khi perform=ESP_OK và is_complete_data_received=true. 204 không tự đọc thêm.
Lỗi setup/transport, thiếu body hoặc quá giới hạn trả -1; không xử lý JSON dở.

Mỗi handle vẫn thuộc một task. Callback context, userdata và POST pointer
được gỡ trước khi return. Broken connection bị close trước lần heartbeat kế.
Không thêm vòng retry POST claim/events trong ứng dụng: kết quả sau lỗi có
thể chưa rõ; heartbeat sau là snapshot mới có telemetry_seq mới. Giữ TLS CA,
không redirect, không giảm bảo mật hoặc tự reset USB/ESP khi mạng lỗi.

Heartbeat timeout 1500 -> 5000ms, dựa trên log valid body nhưng upstream
chưa trả sau 1.45s. Task telemetry độc lập nên không chặn USB/local controls.
Đây là timeout I/O SDK, không hứa deadline tổng DNS/TLS tuyệt đối 5 giây.
Không giữ Ready giả: một request lỗi vẫn cập nhật cloudOnline=false.
Không thay đổi ngưỡng stale/offline server trong bản này.

## Diagnostic bổ sung trong /status.cloud và heartbeat.cloud

- heartbeat_duration_ms: thời gian request gần nhất (millis monotonic).
- heartbeat_failures: số heartbeat không thành công liên tiếp; 200/204 đặt 0.
- heartbeat_diagnostic: lỗi gần nhất, không xóa khi hồi phục. Gồm HTTP code,
  phase (open/connected/headers_sent/response_headers/read/finish), setup,
  perform (esp_err_t), errno, response bytes, overflow và free heap.
  Với HTTP status thật, diagnostic có thể chỉ có HTTP code.

Không có body/token/password/URL ký trong diagnostic. `headers_sent` chỉ
là mốc event cuối, không chứng minh body đã gửi hoặc chưa gửi; dùng log byte
nginx để đối chiếu. duration loại khỏi key so sánh telemetry để không tự tạo
vòng gửi nhanh chỉ vì mỗi request có latency khác nhau.
Dev giữ các trường mới khi lưu heartbeat, có thể hiển thị lỗi gần nhất cùng
số lần lỗi liên tiếp; không dùng lỗi lịch sử làm trạng thái lỗi hiện tại.

## Kiểm thử

check_cloud_http.py chạy production transport với HTTP giả: POST 2052 byte,
fragmented response, 204/503, giới hạn 8192, truncated body, setup/perform lỗi,
đóng stream lỗi, gỡ pointer trên reused handle, không POST replay và mã HTTP
heartbeat/claim/events độc lập. check_cloud.py, check_cloud_recovery.py,
check_paper_wait.py, check_firmware_update.py đạt. Build public loại secret.

Kiểm thử mô phỏng không tái tạo RF/TLS thực. Cần ghi version, uptime và log
nginx sau nạp; theo dõi ít nhất 15–30 phút và khi tải web/render. Nếu tái diễn,
đọc diagnostic mới và request_id/timing nginx; không tuyên bố hết lỗi chỉ vì
vài phút HTTP200. Không phát lệnh in trong phép thử transport.

## Nạp thực tế và quan sát ban đầu

Đã OTA Máy 1 và Máy 2 lúc rảnh; cả hai đọc lại 2026110501, reset_reason=3
(do OTA), Wi-Fi/USB nối lại, CAPT fresh và heartbeat200. Server DB giữ đúng
version cùng heartbeat_diagnostic/duration/failures mới. Không gửi lệnh in thử.
Một số lần upload bị khóa admission khi trạm chưa lấy được quyền USB/claim;
server trả cập nhật thất bại, firmware cũ giữ nguyên; không bypass khóa.
Sau khi xác nhận lệnh người dùng đã in xong, nạp thành công.
Log nginx sau 11:22:35 UTC: mẫu đầu 97 heartbeat Máy 2 và 93 Máy 1 đều200,
request_time tối đa lần lượt0.150s/0.246s; chưa có400/499 trong mẫu này.
Đây chỉ là quan sát ngắn ban đầu, không phải nghiệm thu15–30phút.
Máy 3 không truy cập được trong LAN; chưa nạp trực tiếp bo này.

## Tái hiện bản 2026110501 và sửa tiếp 2026110502

Server đã xác nhận 0501 chưa đạt: 499 lúc 11:28:16–24 UTC, Máy 2 400
thiếu body lúc11:28:53; gián đoạn heartbeat40s, boot ID không đổi.
Đối chiếu esp_http_client.h cài trong Arduino ESP32 3.3.11 / IDF5.5.5:
28679 (0x7007) = ESP_ERR_HTTP_EAGAIN, 28674 (0x7002) = ESP_ERR_HTTP_CONNECT.
Đọc source Espressif tag v5.5.5, esp_http_client_perform trả EAGAIN khi
fetch_headers hết thời gian chờ và giữ state REQ_COMPLETE_DATA.

0502 tiếp tục perform trên CÙNG handle/POST/response context khi EAGAIN,
ngân sách10s từ lúc bắt đầu request; mỗi lần tiếp tục dùng timeout không lớn
hơn phần ngân sách còn lại, nhường task10ms. Không gọi set_post_field,
connect hoặc tạo request mới trong vòng tiếp tục. Không retry lỗi connect,
write, framing hoặc HTTP status thật. Đây là tiếp tục state SDK, không replay
POST. Deadline không bao gồm giới hạn DNS/TLS tuyệt đối của mọi đường SDK.

Sau transport failure, telemetry cleanup toàn bộ handle và tạo handle mới
ở snapshot kế tiếp, thay vì chỉ close rồi tái dùng cấu trúc cũ. Init thất bại
không xóa task telemetry vĩnh viễn. Bổ sung tls error/verify flags, số lần resume
và elapsed ms vào diagnostic. Chưa kết luận việc cleanup sửa được400 thiếu
body: phải đối chiếu thực nghiệm; không tăng timeout để tuyên bố hết lỗi.

Unit test thêm EAGAIN2lần rồi thành công: perform3lần nhưng POST chỉ gửi1lần;
EAGAIN kéo dài: dừng hữu hạn, close, không replay, TLS diagnostic giữ lại.

## Bản thử 2026110503 — chưa phát hành OTA chung

0502 vẫn có lỗi CONNECT 5s và EAGAIN đến hết ngân sách10s. Không nghiệm thu.
0503 giữ sửa continuation/cleanup, bổ sung:
- Claim poll dùng một handle riêng do cloudTask sở hữu; giữ khi204, cleanup
  khi nhận job hoặc có lỗi. Tránh tạo TLS handshake mới mỗi~0.5s lúc idle.
  Unit test xác nhận hai204 chỉ init1handle;200/lỗi giải phóng trước download.
- /status.wifi_diagnostics và heartbeat.wifi_diagnostics: connected, rssi,
  channel, bssid, disconnects, last_reason, last_disconnect_ms (uptime millis).
  Không chứa SSID password/token. Server đã lưu được các trường mới.

Nạp0503 Máy 2 chưa thành công qua LAN; đọc lại vẫn0502. Không ghi giả đã nạp.
Nạp0503 Máy 1 thành công; USB/CAPT/heartbeat nối lại. Mẫu Máy 1 lúcuptime121s:
Wi-Fi connected, disconnects=0, RSSI-49, channel12, BSSID24:0B:2B:0C:40:0C,
nhưng có lỗi phase=headers_sent, perform28679, resumes1, elapsed10011ms,
TLS0/flags0. Vẫn CHƯA đạt; không thể quy mọi lỗi thành Wi-Fi disassociation.

Nginx 12:23:11 UTC Máy 1 heartbeat HTTP200 request_time9.981s trong khi
upstream_header_time0.032s, connect_time0.000s. Phần lớn độ trễ của mẫu này
ở trước/kết hợp nhận request, không phải9.981s xử lýPHP. Chưa phân biệt
TLS scheduling/ghi client, TCP loss, AP hoặc WAN. Không lấy số0.032s để
kết luận mọi lần server đều nhanh. CóHTTP422 Máy 1 12:21:41–42; cần dev
đối chiếu mã validate (không phải400 thiếubody), chưa tự sửa server.

Capture TCP header-only30s tại server, lọc nhóm packet không TCP options:
498 packet có payload,3 seq-range bị lặp theo hướngserver về một kết nối
client. Không lưu/hiển thị payload, token hoặc dữ liệu tài liệu. Capture ngắn
này chưa ánh xạ chắc chắn kết nối đó sang Máy1/2 và chưa bắt trọn ca timeout;
không dùng làm bằng chứng kết luận Wi-Fi/router là thủ phạm.

Trạng thái triển khai: Máy1=0503 thử nghiệm, Máy2=0502 thử nghiệm;
main/public manifest vẫn0501. Không merge/phát hành chung từ bằng chứng
chỉ vài phút200. Cần đối chiếu riêng một bo trên mạng2.4GHz khác, giữ Mac
và bo còn lại làm đối chứng; người dùng xác nhận có mạng khác, đang chờSSID.

## Thử mạng điện thoại — Máy 2, 2026110504

Người dùng chọn thử Máy2 trên hotspot robotpi; không đổi Wi-Fi Mac/Máy1.
Máy2 giữ lệnh LAN14mặt uncertain, job mac-1793881185-54946; đèn tím là
đối soát, không phải OTA. Người dùng cho hủy riêng Máy2. Đã hủy CUPS
ESP32_LBP3300_May2-279 và cancel trên ESP; CAPT cleanup unverified.
Người dùng tắt riêng máy in; xác nhận USB disconnected, lưu bằng chứng hủy
trong file chẩn đoán riêng, rồi xóa record cancelled để bảo trì OTA.
Không đổi unverified thành cleared và không gửi lại dữ liệu in.

0504 thêm DHCP vào màn hình đổi Wi-Fi; record NVS wifi mở rộng97->98byte,
đuôi uint8_t dhcp=0/1. Loader chấp nhận record97byte cũ (static), kiểm tra
kích thước, terminator và flag. Không thay layout station/token/ID/IP tĩnh
đã lưu. DHCP dùng WiFi.config(0,0,0); kết nối thất bại khôi phục config cũ.
Home/profile hiển thị IP đang dùng, /status.static_ip phản ánh mode hiện tại;
/status.network vẫn là cấu hình IP tĩnh được giữ để trở về LAN.

Tham số POST /wifi trial_seconds tùy chọn: chỉ số nguyên60..1800, không có
thì lưu như thường sau khi kết nối thành công. Khi thử, không ghi đè NVS Wi-Fi;
hết hạn và không có job/USB TCP đang chạy thì kết nối lại Wi-Fi đã lưu.
Nếu ESP reboot trong trial thì cũng trở về cấu hình cũ, không phụ thuộc Mac.
Ở lượt này dùng600s. Nếu job còn active thì chờ rảnh rồi mới quay lại.
Không log password hoặc nội dung CSRF/Bearer.

Đã nạp Máy2 0504 qua OTA sau khi máy in tắt. Bắt đầu hotspot:
12:42:39 UTC server nhận firmware0504, uptime77s, IP10.53.50.88,
BSSID5E:15:B6:F0:C9:B7, RSSI-73, channel12, heartbeat200/failures0.
disconnects1/reason8 là thao tác đổi Wi-Fi chủ động, không gọi là reset/rớt tự phát.
Diagnostic HTTP_WRITE_DATA/errno113 lúc đổi route được giữ làm lịch sử,
không dùng lỗi lịch sử đó để báo lỗi hiện tại khi failures=0.
Người dùng đã đưa điện thoại lại gần và bật lại máy in. Đang đo10phút,
chưa kết luận hotspot đã khắc phục nguyên nhân của TVT.

check_wifi.py kiểm thử production loader/config: legacy migration, DHCP/static,
record sai, rollback, trial hết hạn và hoãn về mạng cũ khi đang bận.
check_home.py/check_cloud_http.py/check_firmware_update.py đạt; build public
không chứa secret. Main OTA chung chưa phát hành các bản thử0502–0504.

### Kết quả lượt hotspot và tự quay về TVT

Ngày 05/11/2026, Máy2 tự trở về TVT lúc uptime648035ms (~12:52:10 UTC),
IP192.168.1.200. Đọc trực tiếp lúc12:52:34: uptime673s (không reboot),
firmware0504, RSSI-56, heartbeat200/failures0, claim204, USB connected,
CAPT fresh, không job active. disconnects2/reason8 gồm hai lần chuyển mạng
chủ động, không có lần mất association tự phát trong lượt thử.
Máy in đã bật lại, CAPT không báo kẹt/nắp/giấy; ready_verified=false nên
không diễn giải thành đã xác minh đầy đủ mọi điều kiện sẵn sàng.

Đối chiếu nginx theo stationID trong cửa sổ12:42:40–12:52:09 UTC
(loại thời điểm đổi route):

| Trạm | Mạng | Request | HTTP | Trung vị | p95 | Lâu nhất |
|---|---|---:|---|---:|---:|---:|
| Máy2 /0504 | robotpi |1456|779×200,677×204|0,100s|0,156s|4,072s|
| Máy1 /0503 | TVT |1156|1156×200|0,066s|0,112s|0,329s|

Không400/499/5xx trong cửa sổ trên ở cả hai trạm. Máy1 đang có lệnh LAN
chờ giấy nên không có claim204; không so sánh số request như cùng tải.
Máy2 RSSI khoảng-40..-42 sau khi đưa điện thoại gần, uptime tăng liên tục,
heartbeat failures0 trong mẫu mỗi30s. Diagnostic WRITE_DATA/errno113 giữ
lại từ thao tác đổi route, không phải lỗi mới trong cửa sổ thử.

Kết luận giới hạn: lượt hotspot10phút thành công và rollback TVT đúng;
TVT/Máy1 cũng không tái hiện lỗi cùng cửa sổ. Chưa chứng minh router gây lỗi,
chưa nghiệm thu sửa dứt điểm HTTP/TLS; Máy2 cần theo dõi tiếp0504 trênTVT.
Không phát hành0504 lên main/public OTA chỉ dựa trên lượt thử này.

### Phát hành chính thức 2026110504 — 05/11/2026

Sau lượt thử, Máy2 về TVT và in LAN job285 hoàn tất7/7mặt theo bộ đếm;
CUPS trống, USB/CAPT phản hồi. Lỗi ban đầu của job285 do pending.txt trên
Mac còn từ job14mặt đã được người dùng cho hủy trước bảo trì. Đã kiểm tra
mtime1793881186, lưu marker thành pending.cancelled-1793881185.txt rồi
cupsenable riêng hàng đợi Máy2. Không reset ESP/máy in hoặc replay job cũ.
Không tự xóa marker uncertain chưa đối soát ở các máy khác.

Log sau khi trở về TVT (12:52:40 đến khoảng13:04UTC): Máy2 có2334request,
1336HTTP200/998HTTP204, khôngHTTP>=400, p95=0,160s/max0,678s. Máy1 có
1403request200, khôngHTTP>=400, p95=0,145s/max1,732s. Uptime liên tục;
Máy2 disconnects2 chỉ gồm chuyển đi/về chủ động, Máy1 disconnects0.
Đây là cửa sổ quan sát hữu hạn, không chứng minh đã loại bỏ mọi lỗi dài hạn.

Người dùng yêu cầu phát hành nếu kiểm tra ổn. Bộ kiểm thử transport/Wi-Fi/
home/OTA/cloud/recovery/paper-wait/LAN recovery đạt. Phát hành đúng binary
0504 đã thử, SHA25667e75f319f23885a17fae3862b02e9b89197b3eb408ff389ada86cb613e4c953,
1375744byte lên main/public OTA. Không đổi mật khẩu/token/cấu hình trạm.
Các bo kiểm tra bản mới khi rảnh; không xác nhận từng bo đã nạp chỉ từ việc
publish manifest. Theo dõi tiếp400/499, gap heartbeat và disconnects.
