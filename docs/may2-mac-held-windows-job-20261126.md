# Máy 2 chặn lệnh Mac do phiên Windows cũ — 26/11/2026

Máy 2 đúng ID esp32-1051db78a8fc, IP 192.168.1.200, FW 2026112509. Mac queue ESP32_esp321051db78a8fc bị disabled với `TCP closed; result uncertain`, giữ job 333. ESP giữ job Windows 20261124-120609-9702227, waiting_printer, inactive, 0/60 mặt; USB/CAPT fresh, không cờ service/jam/cover/paper. Native USB đang nối máy in, Mac không thấy COM.

Trong F005 của capt_tcp.h, một job waiting_printer/uncertain/active/needsCleanup khác ID sẽ chặn job mới bằng đóng TCP. Backend Mac nhận lỗi chung; không có bằng chứng job Mac đã gửi trang tới USB ở lượt bị chặn này. Không bỏ guard vì phiên cũ có thể còn giấy/buffer.

Người dùng xác nhận bỏ lệnh Windows cũ, cho Mac in. POST /jobs/control delete được nhận nhưng cancel chuyển unverified, CAPT counters 1/0/0/0; không tự coi là cleared. Đã khởi động lại riêng ESP bằng lưu nguyên AP SSID, giữ cấu hình LAN và nguồn máy in. Phiên cũ được phép bỏ theo xác nhận người dùng; không có job active lúc restart.

POST /printer/discard được 202 nhưng không xuất hiện job discard-orphan sau thời gian theo dõi. Source có nhánh exchange(false) trước kiểm tra cloudClaiming; yêu cầu có thể bị tiêu thụ mà không chạy khi claim đang diễn ra. Chưa có trace xác minh điều kiện chính xác của lượt này; chưa sửa firmware trong đợt xử lý.

Đã dùng CAPT HTTP authenticated, lấy reservation mới A3A2/A2A0, GoOffline E0A6, ClearMisprint E0A3, ClearError E0A2, DiscardData E0A4, GoOnline page 0 E0A5 rồi offline, E1A1 stage 4, E0A9 release. Tất cả ACK; đọc A0A8 xác nhận counters 0/0/0/0. Không gửi raster/C0A4 hoặc thêm trang thử. Đây là dọn phiên đã được người dùng cho bỏ, không phải cách tự xóa mọi job chưa rõ kết quả.

Sau khi verified empty, cupsenable đúng queue Máy 2 chạy lại chính job Mac 333, không tạo job mới. ESP nhận mac-1795688635-8587, tổng 100 mặt; cấu hình Mac 25 bản sao, duplex và number-up=2. Có giai đoạn waiting_printer/counter 1/0/0/0, sau đó đã quan sát printing và completed tăng 12/100. Không đủ trace kết luận nguyên nhân/phương thức kết thúc giai đoạn chờ đó. Chưa thay bộ cài, firmware hay cấu hình mạng Mac; không thao tác Máy 1/3.

Chờ xác nhận giấy người dùng; counter tăng không thay xác nhận giấy thực tế.

## Tái phát giữa lệnh và lỗi đã bắt trực tiếp

Người dùng xác nhận ra giấy, sau đó dừng sau hơn 10 tờ. CUPS job333 đã chuyển completed/processing-to-stop-point và backend PID8587 thoát, trong khi ESP waiting_printer 18/100, CAPT22/21/21/20. Không coi job CUPS completed là đủ giấy. Tài liệu gốc còn trong spool, đã sao chép riêng có quyền0600; không commit tài liệu/pixel. Dựng lại bằng Canon cupsfilter đúng A4/duplex BindEdgeTop/number-up2 cho 4 mặt/bản;25 bản=100mặt. Lưu checkpoint20 mặt đã hoàn tất, ranh giới tờ đúng; dọn hai mặt chưa hoàn tất và chỉ gửi80 mặt còn lại. Mốc18 ESP là snapshot trước sender thoát; counter CAPT completed20 được đọc ổn định trước cleanup.

Backend monitor cũ gửi CUPS-Add-Modify-Printer qua /admin để cập nhật message/reasons liên tục. Không đủ trace khẳng định đó là nguyên nhân duy nhất của CUPS333 kết thúc; đã bỏ mọi mutation CUPS trong monitor, chỉ đọc/cache trạng thái ESP. Cùng job dùng ID ổn định từ station/CUPSjob-uuid (fallback jobID+hash nội dung), pending receipt phải khớp đúng ID, không tự bỏ pending của lệnh khác. Cài tại chỗ qua xác thực quản trị macOS, giữ queue/driver. Có backup mac_backend.pl.before-20261126. Test check_mac_monitor.py chạy production monitor với fakes, cache giữ nguyên lỗi/progress, không gọi CUPS mutation; ID không đổi qua hai tiến trình, receipt khác bị từ chối.

2601 canary thêm LAN same-USB recovery: sender nối lại đúng ID/plan, tác vụ USB đã rời, Ready thật/khay có giấy, thu counter hoàn tất và quy về whole-sheet checkpoint; reservation mới, cleanup CAPT phải ACK và counter0 rồi mới restart cùng generation. Không tiếp tục khi giấy/nắp/service còn lỗi, counter giảm không được xác minh, session epoch khác hoặc có job đang chạy. Lệnh mới vẫn không được ghi đè checkpoint của job khác. Cũng giữ yêu cầu discard pending cho tới khi điều kiện maintenance thực sự đạt, tránh exchange(false) làm mất yêu cầu khi cloudClaiming.

Trên2601 lượt gửi phần còn lại bắt trực tiếp tcp.error=257 ESP_ERR_NO_MEM, last_header=0x1000c0a0 (gói raster4096byte). USB transport không timeout/không cờ giấy. Chưa có mặt bổ sung nào hoàn tất. Đây là lỗi cấp usb_host_transfer_alloc trong serviceCapt khi gửi raster; không gọi là kẹt giấy hoặc kết nối mạng. Chưa xác định mọi nguồn gây phân mảnh heap.

2602 cấp sẵn một usb_transfer_t/buffer DMA4096 trước các network worker và dùng lại cho serialized CAPT. captCommand.transfer chỉ trỏ buffer trong lúc queued/inflight, vềnull sau callback hoặc submit thất bại; không free DMA trong mỗi packet, không tái dùng trước callback timeout. Buffer giữ suốt firmware, context device/endpoint/num_bytes/callback gán lại mỗi lần. Allocation thất bại vẫn báo NO_MEM, không che lỗi. Test production check_capt_transfer_buffer.py:1000transfer, fail allocation/submit, timeout phải đợi callback, disconnect, không free/đụng buffer còn inflight.

Đã nạp riêng Máy2 2602 lúc không có phiên gửi active, giữ nguồn máy in, CAPT counters0/fresh. Khôi phục80 mặt bằng sender ID mac333-remainder-from20-usbbufferfix, giữ bản dựng+checkpoint+pending riêng ngoài repo. Đây là xử lý lệnh cũ đã bị CUPS bỏ; không phải tạo lại100mặt. User gửi thêm job334; đang giữ job334 (hold) để không in trùng, chưa xóa. Chưa phát hành2602 OTA chung trước theo dõi.

## Panic trong nhánh khôi phục và bản2603

2602 đã vượt lỗi allocation, gửi tiếp được dữ liệu; một lần HTTP control_snapshot timeout làm sender nối lại. Sau đó ESP panic/reset reason4. /crash: task capt-tcp, cause65 (stack canary), PC0x40056f5d; backtrace 0x42003777 readUSB →0x420046ca cloudTransfer →0x42005d05 captExchange →0x42005e55/0x42007596 cancelCapt →0x420132ea LAN reset gọi trong captTCPTask. Đối chiếu ELF2602 rebuild đúng source, không coi đây là lỗi nguồn/Wi-Fi. Đường khôi phục thêm snapshot USBStatus chứa details1536 byte vào nhiều frame lồng nhau; stack8192 không đủ trong nhánh này.2603 tăng capt-tcp stack16384; giữ buffer DMA tái dùng.

Đã dừng sender sau panic; USB Host bị guard tạm dừng như thiết kế. Bật lại host để đọc trạng thái thật: CAPT11/10/10/10, tức phần khôi phục đã hoàn tất10mặt (sau mốc20 cũ), tổng30/100mặt. Lưu checkpoint30, dọn phần còn lại sau kiểm tra, không tự giữ mốc28 trước khi đèn/máy in hoàn tất mặt lẻ. Sau cleanup counter0 và không còn backend gửi, OTA riêng Máy2 lên2603. Sender khôi phục mới chỉ gửi70mặt còn lại, IDmac333-remainder-from30-stackfix; tài liệu local và receipt giữ ngoài repo. Job334 tiếp tục hold.

Mac control_snapshot thử lại tối đa3lần khi HTTP/JSON lỗi tạm thời, vẫn dừng nếu IDtrạm/job đổi hoặc cả3lần không đọc được; không tự bỏ kiểm tra cancel/điều khiển. Đã cài backend mới bằng xác thực quản trị, monitor chỉread/cache. Chưa nghiệm thu toàn lệnh70mặt hoặc mọi tình huống lỗi trên2603 tại thời điểm ghi. Không phát hành stable OTA trong lúc thử.

2603 đang in phần70mặt. Đã có recovery thật: HTTP control_snapshot lỗi cả3lần, sender nối lại đúng ID; F005 ban đầu trả RECOVERY_WAIT, sau đó ESP cleanup đã xác minh và trả offset14. Sender tiếp tục phần56mặt còn lại, không cần tắt/bật máy in hoặc reset ESP. Job-control giữ14/70 trong khi counter phiên CAPT mới về0; không nhảy job completed giả. Nhánh stack mở rộng đã đi qua recovery thật mà chưa panic tại thời điểm mẫu. Tổng gốc lúc này44/100 mặt. Chưa tính là hoàn tất cả tài liệu.

Bổ sung backend: trước reconnect, kiểm tra ESP vẫn giữ đúng ID. Nếu ESP đã reboot mất checkpoint, không tạo lại ID đó từ đầu và phát lại mù. Cần đối soát khi mất toàn bộ RAM checkpoint; không đảm bảo tự tiếp tục qua ESP mất nguồn nếu chưa có journal bền vững. Checkpoint cùng ESP/USB sửa lỗi giấy vẫn tự hồi phục như trên.

Sau recovery thứ hai, ESP giữ checkpoint20/70 rồi tiếp tục phiên mới; tiếp tục tăng qua42/70 và52/70, USB transport last_error0. Không can thiệp nguồn/cáp giữa những lần này. Job334 của người dùng vẫn held để tránh trùng tài liệu. Các test production Mac monitor/receipt/control HTTP retry, LAN reconnect, LAN same-USB recovery, USB reserved buffer, cancel, printer repair, cloud engine và OTA đều PASS. Bộ kiểm thử Python độc lập không thay nghiệm thu giấy thực tế.

## Nghiệm thu giấy và phát hành2603

Phần70mặt đã hoàn tất đúng job ID, job_control completed70/70 và CAPT cuối2/2/2/2 sau lần phục hồi gần cuối (checkpoint68, gửi2mặt cuối). Sender kết thúc thành công, xóa receipt phần còn lại, log `VERIFIED COMPLETED: remaining 70 sides`. Tổng30mặt đã xác nhận trước +70mặt còn lại=100/100. Người dùng xác nhận ĐỦ GIẤY và yêu cầu bỏ lệnh gửi lại334; đã cancel đúng334, không gửi nó tới máy in. Biên nhận pending.txt legacy của333 được lưu thành pending.txt.resolved-20261126 sau xác nhận đủ giấy, không xóa mù marker trước đó.

Bản backend cuối cài trên Mac này: monitor read-only/cache, ID ổn định, receipt cô lập, retry HTTP3lần và guard không phát lại nếu ESP mất ID sau reboot. Không đổi Wi-Fi Mac, driver hoặc queue. Firmware2603 giữ máy in nguồn, tự nối lại qua các lần HTTP tạm lỗi, không panic lại trong phần70mặt. Những lỗi HTTP tạm vẫn có thể gây khoảng nghỉ; không khẳng định đã xử lý mọi nguyên nhân mạng hay mọi lỗi phần cứng. Các lỗi giấy/nắp chưa hết phải chờ Ready thật; mất toàn bộ RAM checkpoint do ESP mất nguồn vẫn cần đối soát, không in lại mù.

Promote đúng image2603 đã thử lên firmware/station.bin + latest.json SHA-256 cùng commit; các trạm tự lấy khi online/rảnh. Máy2 đã xác nhận chạy2603, không khẳng định Máy1/3 đã nạp trong đợt này. Giữ airprint-preview/hardware_validatedfalse cho profile rộng vì chưa chứng nhận mọi client/tính năng. mac_backend.pl mới theo bộ cài Mac/Ubuntu tải từ main; máy khác đang cài bản cũ cần chạy lại bộ cài để nhận sửa backend. Không thay EXE Windows vì lỗi monitor/identity này thuộc backend Mac/Ubuntu, USB/recovery là sửa firmware chung.
