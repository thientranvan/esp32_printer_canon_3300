## Automatic USB printer-class repair, 30/10/2026

After an interrupted cloud session has no fresh CAPT telemetry for 15 seconds,
USB control still responds, and the bulk transport reports timeout, request
one USB printer-class repair per interruption. Physical faults with fresh
CAPT telemetry use the existing fault recovery, not this reset path.

The exclusive cloud owner first atomically reserves maintenance state 6.
The USB client waits for existing control probes and all bulk transfers to
finish, halts/flushes empty bulk pipes, then serializes:

1. Printer Class SOFT_RESET: `21 02 00 00 00 00 00 00` (interface 0).
2. CLEAR_FEATURE(ENDPOINT_HALT): bulk OUT `01`, then bulk IN `82`.
3. Clear host endpoint halts; resume read-only CAPT polling.

No ESP reboot, root port power switch, interface release, or USB epoch
fabrication. All control buffers remain allocated until completion callbacks.
Disconnect, unsupported requests and submit failures remain failures, never
proof of a repaired printer. Transport fields `repair_state` (0 idle,
1 queued, 2 active, 3 acknowledged, 4 failed) and `repair_error` are included
in `capt_transport` on ESP/LAN/cloud.

A reset ACK alone does not resume raster transmission. Require fresh CAPT
without physical faults, preserve complete-sheet progress, acquire reservation,
perform CAPT buffer cleanup, and verify idle plus four zero counters. If the
acknowledged USB reset already reset counters, accept a lower counter only
when all four are zero and the engine is idle; retain the prior confirmed
whole-sheet checkpoint. Unconfirmed buffered sheets can be printed again,
as with existing printer-power-cycle recovery; actual physical output beyond
the last confirmed counter is unknown.

`check_usb_printer_repair.py` executes production asynchronous control code
with fake USB callbacks: exact setup packets/order, maintenance ownership,
pending callback lifetime, control/submit failures, and disconnect. The CAPT
recovery regression verifies counter reset proof and rejects non-idle state.
Hardware acceptance on LBP3300 remains pending; class support is not assumed
from compilation or simulated ACKs.

## CAPT bulk timeout / Ready panel, 30/10/2026

Live Máy 1: active cloud job 60 impressions; confirmed complete sheets 4
(8 impressions), submitted 11. USB control GET_DEVICE_ID still responds;
A0A8 telemetry stopped updating for over 3 minutes. The last A0A8 had
11/10/9/9 counters, no service/jam/cover/paper flags, and engine bits 0x84.
Polling the already-owned status operation showed repeated error 263
(ESP_ERR_TIMEOUT). This establishes loss of CAPT bulk responses, not a
verified physical jam or a server outage. The exact first failed bulk
command/direction was unavailable in that firmware.

Recovery fix: no longer require engine processing/printing bits to clear
*before* GoOffline/cleanup. An interrupted session itself can retain them.
Still require a fresh A0A8 without physical faults and monotonic counters;
only restart after acknowledged cleanup, a second no-fault/idle A0A8 and
all four counters zero. No blind raster resend on USB timeout.

Added `capt_transport` in local `/status`, LAN `/printer/status`, and cloud
heartbeat `printer.capt_transport`: state, failures, last_error, opcode,
endpoint, error_age_ms, first_error, first_opcode, first_endpoint. The first
failed transfer since boot remains available after repeated status queries.
Numeric opcode/endpoint only; no document payload
or credentials. `cloud.wait_reason=capt_transport` means bulk timeout with
stale CAPT telemetry. Server must describe communication timeout / waiting
for response, not instruct power cycling or assert a physical printer fault
from this reason alone. Existing freshness requirements still apply.

This fixes the processing-flag recovery gate. It does not establish the
root cause of the observed bulk timeout. A firmware build/test is not
physical validation of automatic recovery; live diagnosis remains required.

## Same-USB interrupted-session recovery, 26/10/2026

Captured job `job_7edfa38c31df137ab82150462c112bf7`: 0/4 confirmed,
2 submitted; fresh A0A8 has no fault, USB is responsive, cloud remains
session_interrupted. Previous retry gate required a new USB epoch even after
Ready, so closing the cover could never resume this branch.

New branch: exclusive cloud owner, same epoch, active cloud job, no cancel intent;
query fresh A0A8, require no physical faults/processing/printing, observe latest
completed counter and retain only confirmed whole sheets. Obtain a reservation,
run acknowledged CAPT buffer cleanup, query again and require all four counters
zero plus no fault/processing. Only then permit Checkpoint.restart on the same
epoch and send from the confirmed-sheet boundary. Never mark job cancelled for
this cleanup, never increment completed, never replay on an unverified cleanup.
Attempts bounded to one per five seconds. No power-cycle required by this branch.

Tests: `check_capt_session_recovery.py`, `check_print_recovery.cpp`,
`check_capt_cancel.py`, `check_cloud.py`. Máy 1 OTA succeeded after authorized cancellation of the test job; manual
cleanup counters 0/0/0/0, post-boot idle, heartbeat 200, no CAPT faults.
Physical fault/recovery acceptance remains pending. This note supersedes earlier mandatory power-cycle wording
for session_interrupted. LAN recovery not changed by this branch.

## Correction: captured JAM latch, 26/10/2026

Live web job `job_e9c26a5522a8acb50a7fdca41f428700`, 4 impressions, submitted 2,
confirmed 0. User reported clearing paper and closing cover without power cycle.
Fresh A0A8: `a8a03c00128e04000f00000000404000800102000100010000000000d500000002000000550303000000000002000000000000000000000000000100`.
Service=false, cover=false, JAM=true, paper_required=true, misprint=true.
The earlier guard requiring JAM=false prevented ClearErrorMisprint entirely.
Removed that guard: acknowledge errors in existing session while JAM remains,
following Canon's command conditions. Service/cover still block acknowledgement.
Never infer Ready from ACK: poll real flags/counters; no DiscardData in resume.
Regression test now holds JAM+misprint+paper together until acknowledgement.

User authorized cancel and OTA. Manual cleanup with a new reservation ACKed,
paper_required cleared but JAM remained. Thus this is a confirmed firmware
deadlock fix, not physical acceptance of complete jam recovery. No automatic
replay was performed. Further physical open/close and live print verification
are pending. Earlier assertions that all physical jam flags must clear before
acknowledging errors are superseded.

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
báo Ready. Máy 1 OTA thành công, sau boot cloud idle, heartbeat 200, claim 204; CAPT fresh, không còn jam/cover/paper_required; phục hồi tự động sau kẹt thực tế cần nghiệm thu
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
