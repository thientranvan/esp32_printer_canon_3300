# Máy2 RGB đỏ, sóng Wi-Fi tốt — 26/11/2026

Máy2 esp32-1051db78a8fc tại192.168.1.200, trước sửaFW2603, uptime1898→1900s, resetreason1 từ lần usercấpnguồntrước. Wi-Fi connected/channel12, RSSI-59..-63dBm, disconnects0. CAPT fresh, USB phản hồi, không job/giấy/serviceerror. Cloud ngay lúc đọc đổi idle/heartbeat200 sang server_error/HTTP-1; diagnostic phaseheaders_sent, perform-1, tls1, heap10144..15180byte. Server log cùng thời điểm có200/204 xen400. Không coi sóng Wi-Fi mạnh là bảo đảm TCP/TLS; không dùng resetUSB hoặc tắt cảnh báo để che cloud lỗi. Diagnostic hiện tại không đủ khẳng định từng HTTP400 đều do thiếu RAM.

Sau tính năngIPP, hai workerHTTP cố định mỗi stack16384byte dùng RAMinternal, ngoài các taskUSB/cloud/status/SSE/OTA. SDK đang build (esp32s3-libs3.3.11/qio_opi) bật CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM và có xTaskCreateWithCaps/vTaskDeleteWithCaps.2608 đổi đúng hai ipp-http stack sang MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT, giữ cố định2worker/queuebound2, không tạo/xóa task mỗi request. Thu hồi32KB internal cho Wi-Fi/TCP/USB. StackUSB/LAN/OTA và bufferUSB DMA vẫninternal; không giảm stack hoặc nới kiểm traTLS.

Startup thất bại phải vTaskDeleteWithCaps đúng các worker tạo bằng caps; các tasknormal giữ vTaskDelete. Check_ipp_task_memory.py biên dịch startup production vớifakeAPI: caps/sizeđúng, phân bổ32768bytePSRAM, cleanup khi fail từng1..4task. FirmwareJSON/heartbeatthêm memory.internal_free/internal_largest/internal_minimum (byte). Loại memory khỏi change-key của heartbeat/SSE, không gây push4Hz chỉ do heap biến động; dữ liệu vẫn có trong snapshot định kỳ.

2607 chỉbuild, không nạp thành công.2608 nạp riêng Máy2 khi không jobactive/cloud; reboot chủđộng doOTA. /statusxácnhậnversion2608/resetreason3, uptime15s, internal_free58000/largest31732/minimum38792; uptime17s free55556/largest31732/minimum22360 trong lúc chạy kiểm traIPP. Heartbeat200, claim204, failures0, diagnosticrỗng. Wi-Fi-55..-56dBm/disconnects0, CAPTfresh. Có tăng headroom thực tế; chưa kết luận loại mọi lỗi mạng chỉ từ mẫu ngắn.

Live `python3 check_ipp_live.py 192.168.1.200 --incoming-job` PASS trên stackPSRAM: một requestchậm không chặn querykhác; nhận incoming/my-jobs, cancel header-only, không gửi raster/in giấy. Chưa thử lại giấySamsung hoặc mọi thao tácflash/cache qua stackPSRAM trong đợt này; giữ release_stageairprint-preview và hardware_validatedfalse. Tiếp tục theo dõi qua chu kỳOTA, không phát hành chung trước kết quả.

## Chưa nghiệm thu: lỗi connect còn tái diễn dù RAM đã tăng

Đợt quan sát đầu có 18 snapshot thành công, uptime15→49s, internal_free47504..60484 byte, cloud idle/heartbeat200/claim204/failures0; hai lần đọc HTTP LAN lỗi. Đợt sau có15lần đọcLAN lỗi trong20lượt. Khi truy cập lại được, uptime130/145s vẫn liên tục, resetreason3 giữ nguyên từOTA, Wi-Fi disconnects0. Không có bằng chứng bo tự reboot trong khoảng này.

Đặc biệt diagnostic ghi HTTP-1 phaseopen/perform28674, duration5010ms, heap58632byte; lỗi connect vẫn xảy ra khi RAMinternal đã dư hơn trước. Sau đó cloud tự vềidle/200/204. Server còn ghi400 cho heartbeat/claim cùng timestamp11:48:37UTC. Như vậy chuyển stack đã cải thiện headroom nhưng CHƯA giải quyết hết chập chờn, không kết luận nguyên nhân duy nhất là thiếu RAM. Cần đối chiếu TCP/TLS/Wi-Fi log lúc hiện tượng xảy ra; không sửa server hoặc tắt xác thựcTLS.

Đã đề nghị cắm COM Máy2 vào Mac để bắt UART115200 theo AGENTS, giữ nativeUSB cho máy in và không đổi Wi-Fi Mac. Hiện Mac không có COM. Giữ2608 riêng Máy2 dưới dạngcanary; firmware/latest.json+station.bin vẫn là2606. Chưa phát hành2608 OTAchung hoặc khẳng định ổn định. Các trường firmware.memory có thể giúp devserver hiển thị headroom cùng firmware version nhưng không thay trạng thái printer/CAPT.
