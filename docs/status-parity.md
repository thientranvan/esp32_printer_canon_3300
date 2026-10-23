# Trạng thái độc lập từ ESP — 23/10/2026

Origin: `https://quanly.lophocbinhminh.com`. Giữ `schema=1`, `espjob-v1`.

ESP là nguồn trạng thái duy nhất gửi server. Không cần Mac/Windows bật hoặc chạy
agent báo hàng đợi. Firmware bỏ `clients`, `client_reports_version` và endpoint
`/printer/client-report`. Server không dùng lại dữ liệu clients từ heartbeat cũ.
Không sửa ứng dụng, DB hoặc service server trong đợt cập nhật contract này.

| Trường | Ý nghĩa |
| --- | --- |
| `status_source` | Luôn `esp32` |
| `printer.capt` | Trạng thái USB/CAPT do ESP đọc trực tiếp |
| `printer.capt.capabilities` | Khả năng giải mã; không khẳng định mọi lỗi đã kiểm chứng |
| `job` | Lệnh ESP thực sự đã nhận, không phải hàng đợi OS chưa gửi |
| `job.title/client_name/client_os` | Metadata tùy chọn đi kèm chính lệnh in |
| `job.progress_known/progress_unit` | Tổng có biết hay không; `impressions` là mặt in |
| `job.actions` | Quyền pause/resume/cancel/delete của đúng lệnh trên ESP |
| `queue_command` | Xác nhận điều khiển hàng đợi web theo contract hiện có |

Không biết tên tài liệu thì để trống; không biết tổng hoặc phần trăm mực thì giữ
null/chưa xác định. `toner_percent=null`, capability false. Các cờ CAPT chỉ được
coi là hiện tại khi `fresh=true`; khi mất USB/dữ liệu cũ phải hiện rõ. Hiện nguyên
service_code như `E100 0000` cùng các cờ paper_required, jam, cover_open,
toner_warning đã giải mã. `all_faults_verified=false` không phải lỗi cảm biến.

Máy LAN chỉ render/gửi tài liệu khi chính người dùng in. Metadata TCP F004 tùy
chọn gửi sau F002 trước F001, tối đa 1024 byte UTF-8 JSON
`{title,client_name,client_os}`; không phải báo cáo nền. Không suy đoán tên/ID OS
bằng thứ tự capture. Cloud render trên Ubuntu rồi giao ESP, không cần máy LAN.

Server dùng boot_id + telemetry_seq để loại snapshot trễ, xét tuổi heartbeat để
báo offline. Job inactive là lịch sử; không hiện đang in. Không lấy counter CAPT
tích lũy làm tiến trình job mới. Job uncertain cần đối soát, không tự in lại.
Hủy chỉ ngừng gửi thêm, không đảm bảo thu hồi trang đã nằm trong máy in.

UI server hiển thị trạng thái mới qua SSE/WebSocket ngay sau heartbeat. Không
poll browser 5–10 giây. ESP kiểm tra CAPT idle khoảng 500 ms, telemetry 250 ms;
độ trễ phụ thuộc mạng, không cam kết hard realtime. Hàng đợi web do server giữ;
ESP điều khiển qua contract realtime-control, không quản lý job chưa gửi trên OS.

Kiểm thử: `python3 check_telemetry.py` kiểm tra nguồn ESP, endpoint cũ 404,
capability unknown và đọc status liên tiếp không reset; không gửi lệnh in.
Buffer snapshot queue 4097 byte giữ trên heap để tránh tràn stack WebServer.

## Sự cố Mac 23/10, lệnh 28 mặt

ESP ghi nhận 9/28 mặt, lỗi TCP 263 (ESP_ERR_TIMEOUT) khi ghi C0A0; USB control
vẫn phản hồi nhưng CAPT bulk không còn trạng thái mới. Không suy diễn là hết giấy
hoặc máy đã rảnh. Không tự phát lại phần còn lại vì máy có thể còn mặt đang đệm.

CUPS job 241 lưu CNDuplex boolean true, BindEdge Top, sides one-sided. Backend
phải nhận cả tùy chọn bare CNDuplex và CNDuplex=True, ưu tiên tùy chọn Canon.
Đã bổ sung kiểm thử hai dạng boolean; cửa sổ giữ số mặt cuối và tự mở khi uncertain.
Lỗi timeout bulk của lượt này chưa xác định nguyên nhân vật lý/giao thức.
