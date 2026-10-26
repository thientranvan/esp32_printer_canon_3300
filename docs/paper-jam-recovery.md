## Bổ sung 26/10/2026: lỗi còn lưu sau khi đóng nắp

Máy 1 vẫn trả CAPT mới sau khi người dùng xử lý kẹt giấy: jam=false,
cover=false, service=false nhưng frame STATUS1 high byte `0x40` và
STATUS2 low byte `0x80`. Firmware cũ chỉ poll, không gửi ClearErrorMisprint.
Vì vậy web tiếp tục waiting_printer và RGB vàng hai nhịp.

Cloud hiện xác nhận lỗi còn lưu bằng `E0A2` (ClearError), `E0A3`
(ClearMisprint), theo bit trong reply, tối đa một lượt mỗi giây khi còn lỗi.
Chỉ gửi trong reservation/USB epoch hiện tại, không gửi khi còn jam, hở nắp
hoặc service error. Không DiscardData/GoOnline/FIRE trong nhánh tiếp tục.
Phải đọc lại A0A8; ACK không tự được coi là Ready. Command lỗi/reject không
được coi là đã phục hồi và không tự replay dữ liệu đã gửi.

Lệnh web 64 mặt đã hủy theo yêu cầu ở mốc xác nhận 16 mặt. Cleanup lần đầu
trả unverified; đã lấy reservation mới cho thao tác dọn lệnh đã hủy,
ClearError/ClearMisprint/DiscardData/phiên rỗng đều ACK, counter về 0.
Không tắt/bật máy in. Không tự gửi lại lệnh này.

Kiểm thử mô phỏng dùng cờ sau kẹt thực tế, giữ lỗi cho đến khi nhận clear,
kiểm tra không clear khi jam/cover/service, không tăng progress, reject không
báo Ready. Máy 1 cập nhật OTA; phục hồi tự động sau kẹt thực tế cần nghiệm thu
bằng lượt in tiếp theo. Máy 2/3 chưa nhận bổ sung ngày 26/10.

# Tự khôi phục sau kẹt giấy / tắt bật máy in

**Cập nhật Hủy:** xem [capt-cancel.md](capt-cancel.md). Bản mới có cleanup CAPT; các ghi nhận trước bản này bên dưới là lịch sử. Không bắt tắt/bật chỉ vì counter lệch hoặc vừa hủy.

Firmware và bộ gửi LAN mới giữ điểm hoàn tất theo **tờ**, do ESP xác định từ phản hồi CAPT. Không dùng số trang đã gửi, số trang Word/spool hoặc dữ liệu từ agent máy tính làm xác nhận đã in.

- Một mặt: bắt đầu lại từ mặt đầu chưa được xác nhận hoàn tất.
- Hai mặt: chỉ chốt sau cả cặp mặt; nếu dở một mặt, gửi lại cả hai mặt của tờ đó. Giữ nguyên thứ tự mặt sau/mặt trước của CAPT, số bản, collate và tờ lẻ cuối.
- Lỗi nhưng USB vẫn nối: giữ nguyên phiên CAPT và dữ liệu trong máy, chuyển `waiting_printer`, tiếp tục đọc trạng thái. Mở nắp lấy giấy/đóng nắp, nạp giấy đúng khay là đủ nếu CAPT hết lỗi. Tự tiếp tục **cùng phiên**, không gửi trùng raster, không bắt tắt nguồn. Thời gian xử lý lỗi không tính vào timeout kéo/đẩy giấy.
- USB thực sự ngắt: giữ job/checkpoint; khi nhận lại LBP3300 và CAPT mới hết lỗi, mở phiên mới, gửi lại từ tờ chưa chốt. Giữ ESP có nguồn. Chỉ nhánh này cần chờ bằng chứng USB ngắt trước khi gửi lại dữ liệu.
- Timeout hoặc mất TCP đơn thuần không chứng minh bộ nhớ máy in đã sạch: vẫn chờ một lần USB ngắt/kết nối lại. Mã service, nắp mở, thiếu giấy và kẹt giấy tiếp tục chặn khôi phục.
- Dừng/Tiếp tục/Hủy/Xóa vẫn dùng được trong lúc chờ theo bảng thao tác bên dưới. Dừng không thu hồi mặt đã đệm; Tiếp tục không tự xóa lỗi cảm biến. Hủy/Xóa ngừng gửi thêm và cleanup CAPT theo `printer_cancel`; không thu hồi tờ đã kéo vào đường giấy.

## Phiên CAPT trước chưa khép (25/10/2026)

Quan sát Máy 1: USB/CAPT mới có phản hồi, không cờ service/kẹt giấy/nắp/thiếu giấy,
nhưng counter `decoded/started/exiting/completed = 4/3/3/2`. Lệnh web mới 8 mặt
chưa gửi trang nào. Đây là phiên CAPT còn dở, không phải bằng chứng lỗi cảm biến.
Firmware cũ trả lỗi ngay rồi chờ USB epoch đổi; đóng nắp không giải phóng nhánh chờ.

Firmware sửa: trước khi mở phiên mới, tiếp tục đọc counter và nhận Dừng/Tiếp tục/Hủy.
Counter khép lại thì tự bắt đầu, không cần USB ngắt. Lỗi đọc USB trước khi mở phiên
cũng được thử lại khi phản hồi trở lại, không bắt buộc epoch đổi. Sau khi bắt đầu
phiên, mất kết quả vẫn cần bằng chứng USB ngắt trước khi phát lại raster.
Phản hồi cảm biến bình thường không tự xóa trạng thái chờ phiên cũ.

`cloud.wait_reason` trong `/status`, SSE và heartbeat cho server:

| Giá trị | Hiển thị / xử lý |
| --- | --- |
| `previous_session` | Phiên trước chưa khép; trạm theo dõi bộ đếm, chưa gửi trang mới |
| `preflight_unavailable` | Chưa đọc được USB/CAPT trước khi mở phiên; đang thử lại |
| `printer_fault` | Lỗi CAPT thật; hiển thị mã/cờ cảm biến cụ thể |
| `session_interrupted` | Phiên đã bắt đầu nhưng bị gián đoạn; chờ máy in khởi động lại |
| Chuỗi rỗng | Không có lý do chờ đang áp dụng |

Server không đổi tất cả `waiting_printer` thành “Chờ xử lý lỗi giấy”.
Đọc `cloud.error` cùng `cloud.wait_reason` và `printer.capt` để hiển thị đúng.
ESP dùng đèn vàng chớp khi chờ, không dùng xanh dương như đang gửi/in.

Nếu counter phiên trước đứng yên, tắt/bật **riêng máy in**, giữ nguồn ESP để giữ
artifact/checkpoint. Không tự gửi lệnh purge/abort CAPT chưa kiểm chứng. Mở/đóng nắp
chỉ đủ khi lỗi vật lý hết và phiên tiếp tục tiến triển; không bảo đảm xóa phiên dở.
Không nạp/reset ESP khi còn job trong PSRAM; chờ job kết thúc hoặc người dùng hủy rõ ràng.
Kiểm thử mô phỏng đã bao gồm phiên cũ khép lại không ngắt USB, hủy lúc chờ,
lỗi đọc preflight và retry. Chưa xác minh lại bằng giấy cho bản sửa này.

Đã nạp bản sửa qua COM/UART0 vào Máy 1 (`98:C3:77:A1:36:B0`), kiểm tra hash đạt.
Log `STATION: PASS`; USB/CAPT phản hồi và heartbeat server HTTP 200 sau khởi động.
Lệnh 8 mặt đã được người dùng hủy trước khi nạp, không tự gửi lại. Sau nạp,
counter máy in còn `3/2/2/2` dù không có cờ lỗi vật lý: hủy lệnh ESP không đồng
nghĩa xóa bộ nhớ máy in. Cần tắt/bật riêng máy in sau khi hủy để kiểm tra lại;
chưa xác nhận bộ nhớ sạch hoặc giấy ra. Máy 2/3 chưa nạp bản sửa này.

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

Nhãn: **Dừng**, **Tiếp tục**, **Hủy lệnh**, **Xóa lệnh**. Khi Hủy/Xóa, xác nhận rõ: “Trạm sẽ dừng gửi và yêu cầu bỏ dữ liệu đệm của phiên này. Tờ đã kéo vào máy vẫn có thể ra giấy.” Chỉ báo cleanup thành công khi `printer_cancel=cleared`; xem [capt-cancel.md](capt-cancel.md).

`actions.delete=true` không có nghĩa xóa ngay. Với job đang chạy/chờ/uncertain, `delete` đặt `request=cancel_requested`, `deleteAfterStop` nội bộ; dừng bộ gửi trước, cloud lưu outcome bền vững và gửi terminal event, sau server ACK mới bỏ bản ghi hiện tại. Với lịch sử terminal, xóa bản ghi ESP trực tiếp. Không tự xóa file spool của thiết bị LAN khác.

Hủy đổi trạng thái local thành `cancelled`. Với API events v1 hiện có, cloud gửi `state=resolved`, `error="Operator cancelled submission; buffered paper may still print"` (job cũ bị bỏ: `Operator discarded interrupted job; no data resent; buffered paper may still print`), **không gửi completed**. Server cần hiển thị “Đã hủy gửi” / “Đã bỏ lệnh dở”, không đổi 8/30 thành 30/30 và không requeue. Bổ sung `cancelled` vào nhãn trạng thái heartbeat; server chưa cần nhận enum event mới.

Cloud bị tắt/mất mạng: Hủy/đối soát lệnh `uncertain` vẫn được ESP lưu trước khi đợi server. Xóa cloud chờ ACK server, không giả báo đã xóa trên VPS khi offline. `command_ack.result=accepted` chỉ là nhận yêu cầu, UI chờ state/request/terminal mới xác nhận hoàn tất thao tác.

Dev server cần nối nút của **job đã claim** qua cơ chế command heartbeat hiện có (job_id/action/boot_id/seq), không gọi lại API hàng đợi `queued` rồi bị từ chối vì `leased`/`uncertain`. Khi job đúng đang nằm trên ESP, dùng `job.actions`; chỉ job chưa giao cho trạm mới do queue API xử lý. Giữ idempotency và quyền theo trạm.

Job hiện tại `job_59c25b9ad2d857de6724192fac29b8b4` (8/30 mặt) đã terminal `uncertain` và artifact RAM đã giải phóng ở firmware cũ. Không thể khôi phục phiên cũ bằng đóng nắp hoặc bấm Tiếp tục. Firmware mới cho Hủy/Xóa job này; không tự in lại hoặc tự xóa thay người dùng. Không lấy số 8 đơn lẻ làm bằng chứng đủ để phát lại tài liệu cũ. Các job mới còn phiên sẽ chờ lỗi hết, không bị chuyển `uncertain` chỉ vì hết giấy/kẹt giấy/mở nắp.

Bảng việc dev server: render nút theo actions; phân biệt physical ready / job blocked; nhãn lỗi cụ thể từ CAPT; trạng thái chờ và số mặt/tờ đã chốt; command đang chờ/đã nhận/đã thực hiện; nhận hủy/xóa job đã giao; phát SSE/WebSocket ngay khi heartbeat đổi. Không chỉ hiển thị chuỗi tiếng Anh `USB submission may have started` hoặc “lỗi chưa xác định”.
