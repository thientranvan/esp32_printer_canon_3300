# Nghiệm thu Mac: một bộ cài, ba trạm, cloud fallback — 09/11/2026

## Đã quan sát

- Chạy `install_mac.sh` không tham số bằng quyền quản trị: thành công, cài/cập nhật ba queue theo station ID. Không hỏi IP, account, JSON hoặc ticket; không đổi Wi-Fi Mac.
- Máy 1 và 2 chưa có cloud.json: bộ cài gọi API public `/api/print/v1/client/install/enroll` và nhận credential. Máy 3 giữ credential từ lần cấp ticket trước.
- Gọi lại API remote cho từng identity/verifier đã lưu: cả ba trả đúng credential cũ; không ghi token/verifier vào log. Kiểm tra cloud.json của cả ba có mode 0600.
- Để kiểm tra relay thật, tạm đổi riêng URI từng queue sang IP không truy cập được `192.0.2.1`, giữ station ID; gửi một trang PDF qua CUPS/Canon trên Mac. Khôi phục URI .199/.200/.201 trong finally. Không đổi firmware hoặc kết nối Wi-Fi.

| Trạm | CUPS job | Kết quả quan sát |
|---|---|---|
| Máy 1 | ESP32_esp3298c377a136b0-294 | Upload thành công, queued. Server từ chối cấp vì trạng thái CAPT không còn mới, máy in không phản hồi. Chưa in. Hủy riêng trang thử khi còn queued/unclaimed, đối soát receipt cancelled, lưu trữ receipt và dọn job CUPS thử. Không hủy lệnh cũ của người dùng. |
| Máy 2 | ESP32_esp321051db78a8fc-295 | queued → claimed → printing → completed; receipt hoàn tất 1 mặt. |
| Máy 3 | ESP32_esp321051db786a34-296 | queued → printing → completed; receipt hoàn tất 1 mặt. |

Tên trên trang thử: `ESP32 MAY 2 CLOUD TEST` và `ESP32 MAY 3 CLOUD TEST`. Counter/receipt completed không thay cho người dùng xác nhận tờ giấy. Cần hỏi xác nhận hai tờ.

## Máy 1: vấn đề riêng của CAPT

Trong thời điểm thử, heartbeat/claim hoạt động (200/204), trạm online, USB vẫn connected/responsive nhưng CAPT fresh=false, age khoảng 5.5 triệu ms, trạng thái unresponsive. Transport ghi nhiều timeout (263). Server ready=false nên giữ job queued là phù hợp, không phải enrollment hoặc upload bị lỗi. Không tự reset máy in, xóa phiên CAPT cũ hoặc cấp lại lệnh để ép nghiệm thu.

## Phạm vi chưa nghiệm thu

- Cài Windows/Ubuntu trên OS thật và giấy thật. EXE/bridge compile, parser, converter, enrollment mock và test chống gửi nhầm LAN đã đạt trước phát hành; không thay thế runtime thực tế.
- Máy 1 cần khắc phục phản hồi CAPT rồi thử giấy riêng.
- Chưa dùng máy Mac sạch hoàn toàn: Canon CAPT đã được cài sẵn, nhánh tải/cài driver không chạy lần này.

Ba queue hiện tại đã về URI LAN gốc, vẫn giữ credential cloud, không tạo printer trùng trong lần cập nhật này. Không khẳng định toàn bộ ba máy/ba OS đã nghiệm thu.
