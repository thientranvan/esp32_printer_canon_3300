# Hủy phiên CAPT trên LBP3300

Bản sửa 25/10/2026: Hủy/Xóa trên ESP hoặc command từ server dừng bộ gửi rồi
thực hiện cleanup qua USB cho đúng reservation của job. Không cần tắt/bật máy in
khi cleanup được xác nhận. Tờ đã được kéo vào đường giấy vẫn có thể ra.

## Giao thức đã đối chiếu

Đối chiếu các hàm `GoOffline`, `ClearMisprint`, `ClearError`, `DiscardData`,
`GoOnline`, `SetJobInfo`, `EngineReleasePrinter` trong bản `captmoncnab5`
đã cài trên Mac. Không phân phối binary/disassembly của Canon trong repo.

Chuỗi khi reservation còn thuộc job, USB chưa đổi epoch:

1. `E0A6`, payload `01 00`: GoOffline.
2. `E0A3`, `E0A2`, `E0A4`: ClearMisprint, ClearError, DiscardData.
3. `E0A5`, payload 16 byte, LE32 `ADEADBEE`, page=0, còn lại 0: phiên rỗng.
4. `E0A6`, payload `01 00`: dừng phiên rỗng.
5. `E1A1`, metadata stage=4 và reservation ID đúng.
6. `E0A9`, ID đúng: release.
7. Đọc `A0A8`, xác nhận không còn cờ processing/printing (`STATUS1 & 0x84`).

Phải nhận đủ reply đúng opcode/length và không bị reject ở mọi bước.
Vẫn thử release nếu bước trước lỗi; không báo thành công khi có bước thất bại.
Không gửi raster, không FIRE `E0A7`, không cộng completed khi cleanup.
Không dùng `E0A1`: lệnh ResetEngine bị LBP3300 này reject `80 00` ngay cả khi reserved.

Quan sát trực tiếp: DiscardData ACK làm cờ processing dữ liệu tắt nhưng counter
vẫn `3/2/2/2`. GoOnline(page 0) sau discard đưa counter về `0/0/0/0` mà không
ngắt nguồn. Counter lệch đơn lẻ không đủ kết luận máy còn dữ liệu: preflight cloud
chấp nhận buffer không processing/printing, sau đó khởi tạo phiên mới như bình thường.

## Trạng thái gửi server

`job.printer_cancel` trong heartbeat; `job_control.printer_cancel` trong status/SSE:

| Giá trị | Ý nghĩa |
| --- | --- |
| `""` | Chưa thực hiện cleanup |
| `cleared` | Chuỗi cleanup được ACK, status không còn processing/printing |
| `not_started` | Job chưa nhận reservation; không xóa phiên của job khác |
| `unverified` | Thiếu ID từ phiên mất, USB đổi epoch/ngắt hoặc cleanup bị lỗi/reject |

Chỉ hiển thị “Đã hủy — máy in xác nhận bỏ dữ liệu đệm” khi `cleared`.
Với `unverified`, hiển thị “Đã dừng gửi — chưa xác minh xóa dữ liệu đệm”.
Không biến số mặt đã hoàn tất thành tổng; không requeue/replay khi người dùng hủy.
`command_ack=accepted` vẫn chỉ là nhận intent; đợi kết quả cleanup/terminal.
Cloud events v1 vẫn dùng `resolved`, error nêu kết quả cleanup, không dùng `completed`.
Xóa job cloud vẫn chờ lưu outcome và ACK server.

LAN và cloud dùng chung hàm cleanup trên ESP, cùng USB arbiter. Lưu reservation ID
và USB epoch trong job; không dùng ID cũ sau USB reconnect. Hủy LAN đang chờ mà
không còn TCP sender cũng được task ESP xử lý; không phụ thuộc agent máy tính.
Xóa riêng OS spool không phải bằng chứng ESP đã nhận yêu cầu Hủy; contract này áp
dụng khi ESP nhận đúng action cho job. Không thêm telemetry từ máy tính lên VPS.

`python3 check_capt_cancel.py` chạy production cleanup với fake printer: đúng thứ tự,
ACK thất bại ở từng bước, mất USB/epoch, chưa mở phiên, mất ID và không tăng tiến độ.

## Kiểm thử phần cứng 25/10/2026

Máy 1, MAC `98:C3:77:A1:36:B0`, firmware nạp qua COM. Đưa raster trang thử
vào máy in nhưng chưa FIRE: counter `1/0/0/0`. Hủy qua ESP nhận
`printer_cancel=cleared`, completed vẫn 0, counter về `0/0/0/0`.
Lệnh LAN mới một mặt đạt counter `1/1/1/1`, job completed, không tắt/bật
máy in hoặc ESP giữa hai lệnh. COM tiếp tục báo STATION PASS.
Đây là xác nhận giao thức và counter; giấy thực tế chờ người dùng xác nhận.
Cloud dùng cùng hàm cleanup, đã kiểm thử logic; lượt phần cứng này là LAN.
