# Tự khôi phục sau kẹt giấy / tắt bật máy in

Firmware và bộ gửi LAN mới giữ điểm hoàn tất theo **tờ**, do ESP xác định từ phản hồi CAPT. Không dùng số trang đã gửi, số trang Word/spool hoặc dữ liệu từ agent máy tính làm xác nhận đã in.

- Một mặt: bắt đầu lại từ mặt đầu chưa được xác nhận hoàn tất.
- Hai mặt: chỉ chốt sau cả cặp mặt; nếu dở một mặt, gửi lại cả hai mặt của tờ đó. Giữ nguyên thứ tự mặt sau/mặt trước của CAPT, số bản, collate và tờ lẻ cuối.
- Lỗi nhưng USB vẫn nối: giữ nguyên phiên CAPT và dữ liệu trong máy, chuyển `waiting_printer`, tiếp tục đọc trạng thái. Mở nắp lấy giấy/đóng nắp, nạp giấy đúng khay là đủ nếu CAPT hết lỗi. Tự tiếp tục **cùng phiên**, không gửi trùng raster, không bắt tắt nguồn. Thời gian xử lý lỗi không tính vào timeout kéo/đẩy giấy.
- USB thực sự ngắt: giữ job/checkpoint; khi nhận lại LBP3300 và CAPT mới hết lỗi, mở phiên mới, gửi lại từ tờ chưa chốt. Giữ ESP có nguồn. Chỉ nhánh này cần chờ bằng chứng USB ngắt trước khi gửi lại dữ liệu.
- Timeout hoặc mất TCP đơn thuần không chứng minh bộ nhớ máy in đã sạch: vẫn chờ một lần USB ngắt/kết nối lại. Mã service, nắp mở, thiếu giấy và kẹt giấy tiếp tục chặn khôi phục.
- Dừng/Tiếp tục/Hủy/Xóa vẫn dùng được trong lúc chờ theo bảng thao tác bên dưới. Dừng không thu hồi mặt đã đệm; Tiếp tục không tự xóa lỗi cảm biến. Hủy/Xóa ngừng gửi thêm, không thu hồi giấy/dữ liệu còn trong bộ nhớ máy in.

## Phạm vi

Cloud giữ artifact đã kiểm SHA-256 trong PSRAM, không tải lại/claim lại/requeue job. LAN giữ bản render ở chính tiến trình đang gửi của Windows/Mac/Ubuntu và nối lại cùng ID; **checkpoint do ESP trả**, máy tính không tự quyết định trang đã in. Không có agent báo trạng thái nền lên server.

Bộ gửi cũ không có giao thức checkpoint vẫn giữ cách khóa `uncertain`. Cần cập nhật firmware **và** cài lại bộ gửi LAN để có phục hồi LAN. Bộ EXE và PowerShell chứa cùng bridge mới; Mac và Ubuntu dùng backend Perl mới.

Checkpoint RAM tồn tại qua **máy in** tắt/bật, chưa tồn tại qua ESP mất nguồn/reset hoặc bộ gửi LAN bị đóng. Những trường hợp đó vẫn `uncertain`; cần spool/journal bền vững để mở rộng. Không tự chuyển job `uncertain` từ firmware cũ sang chế độ tự phục hồi vì thiếu bằng chứng phiên USB và kế hoạch tờ đã lưu.

CAPT counter là bằng chứng giao thức, không phải xác nhận giấy vật lý. Nếu tờ đã ra nhưng phản hồi hoàn tất mất trước khi ESP nhận, tờ đó có thể được in lại; không thể hứa chính xác tuyệt đối sau mất phản hồi. Không tự cộng tiến trình khi chưa có dữ liệu.

ESP nhớ 16 ID LAN hoàn tất gần nhất trong RAM để mất ACK cuối không khiến job bị in lại. Không dùng cache này thay cho idempotency/spool bền vững.

## Giao thức LAN bổ sung

`/setup/profile.json` có `lan_recovery_version: 1`.

Sau `F002` (ID), `F004` (metadata), trước `F001` (tổng mặt), gửi `F005` với payload một byte/mặt theo kế hoạch **đã mở rộng copies/collate**: `0` một mặt/tờ lẻ, cặp `4,2` cho hai mặt. 1–9900 mặt; cặp sai bị từ chối. Gửi lại cùng ID và cùng kế hoạch.

Phản hồi `F005` dài 10 byte: header 4 byte, ACK `00 01`, unsigned LE32 checkpoint:

| Giá trị | Nghĩa |
| --- | --- |
| `0..total` | Bỏ qua chừng này mặt đã chốt, mở phiên CAPT mới cho phần còn lại |
| `0xFFFFFFFF` | Chờ máy in khởi động lại / CAPT mới hết lỗi |
| `0xFFFFFFFE` | Người dùng đã hủy; dừng, giữ bảo vệ kết quả chưa rõ |
| `0xFFFFFFFD` | Cùng ID đã hoàn tất; không gửi lại dữ liệu |

`F001` vẫn gửi tổng **ban đầu**. Counter của phiên CAPT mới tính từ 0; ESP cộng checkpoint để báo tiến trình tổng, không cộng trùng. Job khác không được chiếm checkpoint đang chờ.

## Web/server

Heartbeat và SSE ESP bổ sung trong `job` / `job_control`:

```json
{
  "state": "waiting_printer",
  "total": 30,
  "completed": 8,
  "recovery": {"automatic": true, "confirmed_sheets": 4, "next_sheet": 5}
}
```

`completed` là mặt đã chốt theo tờ khi có recovery. Trường sheet là `null` nếu không có checkpoint. `next_sheet` là `null` khi job hoàn tất.

Dev server: hiển thị lỗi vật lý hiện tại làm trạng thái máy; đồng thời hiển thị job “Chờ xử lý lỗi — tự tiếp tục khi máy sẵn sàng; nếu USB ngắt, in lại từ tờ 5” cho ví dụ trên. Không đổi thành “Sẵn sàng” chỉ vì USB trở lại, không đổi `waiting_printer` thành `uncertain`/lỗi chưa xác định, không xóa job hoặc tự requeue/claim lại. Giữ lease cũ; ESP gửi terminal event khi thực sự xong hoặc dừng. Dùng heartbeat/SSE hiện có, không thêm polling chậm hay agent máy tính.

Đây là khôi phục **trong cùng lần thực thi**, không thay quy tắc không tự replay job mất kết quả sau ESP reset. Không sửa code/DB server trong đợt này; tài liệu cho dev server triển khai hiển thị.

## Kiểm thử

- `python3 check_cloud.py`: production CAPT engine bỏ qua phần đã hoàn tất, kế hoạch duplex, parser và checkpoint C++.
- `python3 check_lan_recovery.py`: production backend Perl bị ngắt ở tờ hai; nối lại cùng ID, không in lại tờ một, gửi lại đủ cặp mặt tờ hai, dọn pending khi xong.
- Bridge Windows được compile và chạy self-test bằng Mono; chưa thay thế thử thực tế driver/Windows/kẹt giấy vật lý.
- Cần người dùng kiểm chứng giấy thực tế trước khi coi phục hồi kẹt giấy đã xác minh trên LBP3300.

## Nút điều khiển bắt buộc trên web server / ESP

Dùng `job.actions` từ heartbeat (ESP dùng `job_control.actions`), không tự khóa toàn bộ vì job có lỗi vật lý. Chỉ cho bấm khi dữ liệu trạm còn mới và ID/boot/sequence đúng. Không gửi hành động cho job khác chỉ vì cùng tên tài liệu.

| Job | Dừng | Tiếp tục | Hủy | Xóa |
| --- | --- | --- | --- | --- |
| Đang in, hỗ trợ điều khiển | Có | Khi đã yêu cầu dừng | Có | Có, yêu cầu ngừng gửi rồi xóa |
| Đang dừng | Không lặp | Có | Có | Có |
| Đang chờ hết giấy/kẹt/nắp hoặc USB nối lại | Có | Có; không bỏ qua lỗi vật lý | Có | Có |
| `uncertain`, mất phiên/checkpoint | Không | Không, không còn dữ liệu phiên để tiếp tục an toàn | Có, bỏ lệnh dở | Có, bỏ lệnh dở và xóa bản ghi sau ACK |
| Hoàn tất/đã hủy/đã đối soát | Không | Không | Không | Có |

Nhãn: **Dừng**, **Tiếp tục**, **Hủy lệnh**, **Xóa lệnh**. Khi Hủy/Xóa, xác nhận rõ: “Dừng gửi lệnh này; trang đã vào bộ nhớ máy in vẫn có thể được in. Không xóa bộ nhớ máy in.” Không mô tả là đã purge máy in.

`actions.delete=true` không có nghĩa xóa ngay. Với job đang chạy/chờ/uncertain, `delete` đặt `request=cancel_requested`, `deleteAfterStop` nội bộ; dừng bộ gửi trước, cloud lưu outcome bền vững và gửi terminal event, sau server ACK mới bỏ bản ghi hiện tại. Với lịch sử terminal, xóa bản ghi ESP trực tiếp. Không tự xóa file spool của thiết bị LAN khác.

Hủy đổi trạng thái local thành `cancelled`. Với API events v1 hiện có, cloud gửi `state=resolved`, `error="Operator cancelled submission; buffered paper may still print"` (job cũ bị bỏ: `Operator discarded interrupted job; no data resent; buffered paper may still print`), **không gửi completed**. Server cần hiển thị “Đã hủy gửi” / “Đã bỏ lệnh dở”, không đổi 8/30 thành 30/30 và không requeue. Bổ sung `cancelled` vào nhãn trạng thái heartbeat; server chưa cần nhận enum event mới.

Cloud bị tắt/mất mạng: Hủy/đối soát lệnh `uncertain` vẫn được ESP lưu trước khi đợi server. Xóa cloud chờ ACK server, không giả báo đã xóa trên VPS khi offline. `command_ack.result=accepted` chỉ là nhận yêu cầu, UI chờ state/request/terminal mới xác nhận hoàn tất thao tác.

Dev server cần nối nút của **job đã claim** qua cơ chế command heartbeat hiện có (job_id/action/boot_id/seq), không gọi lại API hàng đợi `queued` rồi bị từ chối vì `leased`/`uncertain`. Khi job đúng đang nằm trên ESP, dùng `job.actions`; chỉ job chưa giao cho trạm mới do queue API xử lý. Giữ idempotency và quyền theo trạm.

Job hiện tại `job_59c25b9ad2d857de6724192fac29b8b4` (8/30 mặt) đã terminal `uncertain` và artifact RAM đã giải phóng ở firmware cũ. Không thể khôi phục phiên cũ bằng đóng nắp hoặc bấm Tiếp tục. Firmware mới cho Hủy/Xóa job này; không tự in lại hoặc tự xóa thay người dùng. Không lấy số 8 đơn lẻ làm bằng chứng đủ để phát lại tài liệu cũ. Các job mới còn phiên sẽ chờ lỗi hết, không bị chuyển `uncertain` chỉ vì hết giấy/kẹt giấy/mở nắp.

Bảng việc dev server: render nút theo actions; phân biệt physical ready / job blocked; nhãn lỗi cụ thể từ CAPT; trạng thái chờ và số mặt/tờ đã chốt; command đang chờ/đã nhận/đã thực hiện; nhận hủy/xóa job đã giao; phát SSE/WebSocket ngay khi heartbeat đổi. Không chỉ hiển thị chuỗi tiếng Anh `USB submission may have started` hoặc “lỗi chưa xác định”.
