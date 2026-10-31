# Hết giấy giữa lệnh web — 31/10/2026

## Ca thật trên Máy 1, firmware 2026103004

Người dùng tạo lệnh web 4 mặt, cho khay Cassette 1 hết giấy giữa lệnh.
ESP dừng ở 2/4 mặt nhưng A0A8 vẫn `paper_required=false`; có lúc báo hết giấy
rồi mất cảnh báo dù chưa nạp giấy, RGB trở lại xanh dương. USB vẫn nối,
`recoveries=0`, `capt_transport.failures=0`, uptime liên tục. Không có bằng chứng
USB reset trong ca này.

Frame A0A8 khi đang đứng (full frame):

```
a8a03c00148e00000f000000000000000000010000000000000000005600000006000000550702000000000001000000000000000000000000000100
```

Trong lúc khay vẫn trống, counter CAPT về 0 rồi lại decoded=1. Phù hợp với
nhánh timeout/dọn phiên hiện tại, không phải bằng chứng người dùng tắt máy in.
Người dùng xác nhận nạp giấy, đóng khay thì lệnh tự in tiếp, không tắt/bật máy;
ESP sau đó ghi completed 4/4. Chưa coi bộ đếm là xác nhận riêng đủ tờ giấy.

A0A1 lúc Máy 1 rảnh, khay đã lấy hết giấy và đóng, payload:

```
003080100000000000000100000008000000a201010200000000fa000000000000002302020200000000fa00000000000000
```

Nguồn 1 có byte cờ `a2`. Mẫu Máy 2 có giấy có byte `e2`.
Đã đối chiếu trên chính Máy 1: sau khi người dùng nạp giấy và giữ giấy trong khay,
record nguồn 1 đổi từ `a2` sang `e2`, trường cuối record từ `0000` sang `fd00`.
Chỉ dùng bit `0x40` cho có giấy; không suy đoán ý nghĩa trường còn lại.

## Bản sửa 2026103101

- Đọc A0A1 mỗi 500 ms khi giữ owner cloud hoặc task trạng thái lấy được owner.
  Không giành USB owner của client LAN; vẫn quan sát thụ động A0A1/E1A9 của client.
- Frame A0A1: header 6 byte, tối đa 8 record × 16 byte. Kiểm tra chiều dài,
  source index, record trùng và frame lỗi. Bit `0x80` là nguồn được lắp,
  `0x40` là có giấy; byte record 2 là source index. Không suy đoán bit khác.
- Ghi nguồn được chọn từ ACK E1A9; cloud chọn rõ Cassette 1 (source 1).
- `paper_required` hợp nhất A0A8 với nguồn đã chọn đang trống và dữ liệu A0A1
  còn mới. Dữ liệu cũ/nguồn không lắp/không biết không được coi là có giấy.
- Khi cloud chờ giấy: giữ phiên và checkpoint, loại thời gian chờ khỏi timeout
  trang, vẫn nhận hủy và gửi heartbeat. Không ClearError/ClearMisprint khi
  cảm biến khay chưa xác nhận có giấy. Không dọn phiên/replay trong lúc khay trống.
- Nhánh khôi phục phiên gián đoạn cũng phải đọc A0A1 mới và xác nhận có giấy
  trước khi reservation/DiscardData; không dùng riêng A0A8 Ready để quyết định.
- Nạp giấy chỉ cho phép tiếp tục chờ counter của cùng phiên; không tự cộng số mặt.

## Contract cho dev server

Nguồn duy nhất vẫn là ESP. Không thêm agent máy tính. Heartbeat thêm:

```json
{"printer":{"capt":{"paper_required":true,"paper_sources":{"fresh":true,"selected":1,"cassette1_present":false}}},"cloud":{"state":"waiting_printer","wait_reason":"paper_required"}}
```

- Chỉ dùng `paper_sources` khi station online, USB nối, CAPT fresh và
  `paper_sources.fresh=true`. `cassette1_present` có ba giá trị true/false/null.
- Fresh + false: hiện **Cassette 1 hết giấy — nạp giấy rồi đóng khay**.
- `cloud.wait_reason=paper_required`: giữ lệnh/tiến trình, không đổi thành failed,
  không yêu cầu tắt máy in, không tự gửi lại lệnh hoặc tăng completed.
- `cloud.wait_reason=paper_status_unknown`: **Chưa đọc được cảm biến Cassette 1**;
  không khẳng định Ready hoặc hết giấy. Trường thiếu ở firmware cũ cũng là chưa biết.
- Còn giấy nhưng A0A8 yêu cầu giấy: giữ thông báo kiểm tra khổ/nguồn giấy;
  không đồng nhất mọi paper_required với khay trống.
- Nạp giấy xong cập nhật theo heartbeat mới; giữ nguyên job ID, quyền điều khiển
  lấy từ `job.actions`. Không cần endpoint server hoặc reporter mới.

## Kiểm thử

`python3 check_paper_wait.py`: chạy nguyên hàm cloudCounters với máy in giả,
khay trống hơn 65 giây rồi nạp giấy, không clear/replay, vẫn hủy được.
`check_capt_status.cpp`: record thật, khay trống/có giấy, nguồn không lắp,
frame thiếu/trùng và frame 134 byte chia ở mọi vị trí.
`check_capt_session_recovery.py`: chặn cleanup khi nguồn giấy trống.

Nghiệm thu trên bản mới cần tạo lại hết giấy giữa lệnh web, giữ trạng thái
hơn 60 giây, xác nhận báo hết giấy ổn định và nạp giấy tự tiếp tục không tắt máy.
Chưa khẳng định đã sửa các ca phần cứng khác hoặc đã nghiệm thu firmware mới.

## Triển khai

Máy 1 đã nạp OTA 2026103101 khi rảnh, xác minh version đang chạy, CAPT fresh,
`paper_sources.fresh=true`, `cassette1_present=true`, heartbeat HTTP 200 và claim 204.
Máy 2/3 chưa được nạp thủ công trong lượt này. Nghiệm thu hết giấy giữa lệnh
trên firmware mới vẫn cần người dùng tạo lại ca thử; chưa tuyên bố đã đạt.
