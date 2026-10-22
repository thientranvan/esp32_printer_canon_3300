#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_mac.h>
#include "device_secret.h"
#include "usb_monitor.h"
#include "capt_tcp.h"
#include "usb_trace.h"

WebServer server(80);
Preferences prefs;
String csrf;
String stationID, stationName = "Máy 1";
IPAddress stationIP(192,168,1,199), stationGateway(192,168,1,1), stationMask(255,255,255,0), stationDNS(192,168,1,1);
struct StationSettings {
  uint32_t version;
  char name[193], ip[16], gateway[16], mask[16], dns[16], cloudToken[97];
  bool cloudEnabled;
};
StationSettings stationSettings = {};

bool healthy = false;
bool uploadOK = false;
uint32_t rebootAt = 0;
uint32_t connectionAt = 0;
bool connecting = false;
String pendingSSID, pendingPassword;
String windowsReport;
String supportCommand = "# Hiện không cần chạy thêm lệnh trên Windows.";
struct WiFiConfig { char ssid[33]; char password[64]; };

String jsonString(const String &value) {
  String result = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    uint8_t c = value[i];
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c < 32) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      result += escaped;
    } else result += char(c);
  }
  return result + "\"";
}

bool validStationName(const String &name) {
  if (!name.length() || name.length() > 192) return false;
  for (uint8_t c : name) if (c < 32 || c == 127 || c == '\\' || c == '/' || c == '"' || c == ';') return false;
  return true;
}
bool validNetwork(const String &ipText, const String &gatewayText, const String &maskText, const String &dnsText) {
  IPAddress ip, gateway, mask, dns;
  if (!ip.fromString(ipText) || !gateway.fromString(gatewayText) || !mask.fromString(maskText) || !dns.fromString(dnsText)) return false;
  if(ip.toString()!=ipText || gateway.toString()!=gatewayText || mask.toString()!=maskText || dns.toString()!=dnsText)return false;
  uint32_t m = 0, a = 0, g = 0;
  for (int i=0; i<4; ++i) { m=(m<<8)|mask[i]; a=(a<<8)|ip[i]; g=(g<<8)|gateway[i]; }
  uint32_t host = ~m;
  return host >= 3 && (host & (host+1)) == 0 && m && ip[0] > 0 && ip[0] < 224 &&
    gateway[0] > 0 && gateway[0] < 224 && dns[0] > 0 && dns[0] < 224 &&
    (a&m)==(g&m) && (a&host) && (a&host)!=host && (g&host) && (g&host)!=host && a != g;
}
String profileJSON() {
  return "{\"schema\":1,\"model\":\"LBP3300\",\"id\":" + jsonString(stationID) +
    ",\"name\":" + jsonString(stationName) + ",\"ip\":" + jsonString(WiFi.localIP().toString()) + "}";
}

#include "cloud_print.h"

void connectSaved() {
  WiFiConfig config = {};
  if (prefs.getBytes("wifi", &config, sizeof(config)) != sizeof(config)) return;
  if (!memchr(config.ssid, 0, sizeof(config.ssid)) ||
      !memchr(config.password, 0, sizeof(config.password))) return;
  if (config.ssid[0]) WiFi.begin(config.ssid, config.password);
}

bool authorized() {
  if (server.authenticate("admin", DEVICE_PASSWORD)) return true;
  server.requestAuthentication(DIGEST_AUTH, "ESP32 printer");
  return false;
}

bool validPost() {
  return server.method() == HTTP_POST && server.arg("token") == csrf;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

uint32_t captSession() {
  String value = server.arg("session");
  if (!value.length() || value.length() > 10) return 0;
  uint64_t result = 0;
  for (char c : value) {
    if (c < '0' || c > '9') return 0;
    result = result * 10 + c - '0';
  }
  return result >= 2 && result <= UINT32_MAX-2 ? uint32_t(result) : 0;
}

void captResult() {
  if (!authorized()) return;
  uint32_t session = captSession();
  if (!session) { server.send(400, "text/plain", "Invalid session"); return; }
  if (!claimCapt(session)) { server.send(409, "text/plain", "Printer session busy"); return; }
  int state = captCommand.state.load();
  String result = "{\"state\":" + String(state);
  if (state == 3) {
    result += ",\"error\":" + String(captCommand.error) + ",\"data\":\"";
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < captCommand.received; ++i) {
      result += hex[captCommand.data[i] >> 4];
      result += hex[captCommand.data[i] & 15];
    }
    result += "\"";
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", result + "}");
}

void captSubmit() {
  if (!authorized()) return;
  if (!validPost()) { server.send(403, "text/plain", "Invalid token"); return; }
  if (readUSB().recovering) { server.send(409, "text/plain", "USB recovering; no command accepted"); return; }
  String data = server.arg("data"), readSize = server.arg("read");
  bool input = !readSize.isEmpty();
  int size = 0;
  if (input) {
    if (readSize.length() > 4 || !data.isEmpty()) { server.send(400, "text/plain", "Invalid read"); return; }
    for (size_t i = 0; i < readSize.length(); ++i) {
      if (readSize[i] < '0' || readSize[i] > '9') { server.send(400, "text/plain", "Invalid read"); return; }
      size = size * 10 + readSize[i] - '0';
    }
    if (size < 64 || size > 4096 || size % 64) { server.send(400, "text/plain", "Read must be 64..4096, multiple of 64"); return; }
  } else {
    if (data.length() < 8 || data.length() > 8192 || data.length() % 2) { server.send(400, "text/plain", "Invalid CAPT packet size"); return; }
    for (size_t i = 0; i < data.length(); ++i)
      if (hexDigit(data[i]) < 0) { server.send(400, "text/plain", "Invalid hex"); return; }
    size = data.length() / 2;
    int declared = (hexDigit(data[4]) * 16 + hexDigit(data[5])) |
                   ((hexDigit(data[6]) * 16 + hexDigit(data[7])) << 8);
    if (declared != size) { server.send(400, "text/plain", "CAPT length mismatch"); return; }
  }
  uint32_t session = captSession();
  if (!session) { server.send(400, "text/plain", "Invalid session"); return; }
  if (!claimCapt(session)) { server.send(409, "text/plain", "Printer session busy"); return; }
  int state = captCommand.state.load();
  if ((state != 0 && state != 3) || !captCommand.state.compare_exchange_strong(state, 5)) {
    server.send(409, "text/plain", "CAPT operation busy"); return;
  }
  captCommand.size = size;
  captCommand.received = 0;
  captCommand.error = 0;
  captCommand.input = input;
  if (!input) for (int i = 0; i < size; ++i)
    captCommand.data[i] = hexDigit(data[i*2]) * 16 + hexDigit(data[i*2+1]);
  captCommand.state.store(1);
  server.send(202, "application/json", "{\"queued\":true}");
}

void home() {
  if (!authorized()) return;
  server.sendHeader("Cache-Control", "no-store");
  String page = R"HTML(<!doctype html><html lang="vi"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Trạm in ESP32</title>
<style>body{font:18px system-ui;max-width:640px;margin:32px auto;padding:0 16px}label{display:block;margin:16px 0}input,button{font:inherit;max-width:100%;box-sizing:border-box}button{padding:8px}small{display:block}</style>
<h1>Trạm in ESP32</h1><p>Canon LBP3300 · A4, cassette 1 · Đã xác nhận in thử qua LAN.</p><p id="status" aria-live="polite">Đang đọc trạng thái...</p>
<h2>Lệnh cần chạy hiện tại</h2><p>Mở PowerShell quản trị và dán lệnh. Nội dung tự cập nhật mỗi 3 giây; không tự chạy.</p>
<textarea id="live-command" aria-label="Lệnh PowerShell cần chạy hiện tại" readonly rows="8" style="width:100%;box-sizing:border-box"></textarea>
<button id="live-copy" type="button" disabled onclick="copyLiveCommand()">Sao chép lệnh hiện tại</button><p id="live-result" role="status">Đang tải lệnh...</p>
<script>
async function refreshCommand(){try{const r=await fetch('/support/command',{cache:'no-store'});if(!r.ok)throw Error();const s=await r.json();const field=document.getElementById('live-command');if(field.value!==s.command){field.value=s.command;document.getElementById('live-result').textContent='Đã nhận lệnh mới.';}document.getElementById('live-copy').disabled=!s.command;}catch{document.getElementById('live-copy').disabled=true;document.getElementById('live-result').textContent='Chưa tải được lệnh hiện tại. Đang thử lại...';}}
function copyLiveCommand(){const field=document.getElementById('live-command');field.focus();field.select();let copied=false;try{copied=document.execCommand('copy');}catch{}document.getElementById('live-result').textContent=copied?'Đã sao chép. Dán vào PowerShell quản trị.':'Đã chọn lệnh. Nhấn Ctrl+C để sao chép.';}
refreshCommand();setInterval(refreshCommand,3000);
</script>
<h2>Cài máy in trên Windows</h2><p>Đã xác nhận in qua hàng đợi Windows. Chạy bộ cài này trên từng máy Windows 10/11 x64, kể cả máy chưa từng cài máy in. Bộ cài tự tải CAPT chính thức từ Canon (12 MB), kiểm tra SHA-256 và chữ ký rồi cài driver cùng cầu nối LAN. Máy đã có driver sẽ bỏ qua bước tải; không cần cắm USB máy in vào máy tính. Bộ cài lấy tên và IP từ trạm này; chọn tên máy in tương ứng trong danh sách máy in của Word, Chrome hoặc trình đọc PDF.</p>
<p><a href="https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/esp32-printer-setup-v3.ps1" download="esp32-printer-setup-v3.ps1">Tải bộ cài Windows v3</a>. Mở PowerShell quản trị, dán lệnh rồi chọn file bộ cài vừa tải:</p>
<textarea id="setup-command" aria-label="Lệnh chạy bộ cài Windows" readonly rows="4" style="width:100%;box-sizing:border-box">Add-Type -AssemblyName System.Windows.Forms; $f = New-Object System.Windows.Forms.OpenFileDialog; $f.Filter = 'PowerShell (*.ps1)|*.ps1'; $f.Title = 'Chon esp32-printer-setup-v3.ps1'; if ($f.ShowDialog() -eq 'OK') { powershell -NoProfile -ExecutionPolicy Bypass -File $f.FileName -StationIP __STATION_IP__ }</textarea>
<button type="button" onclick="copyRemote('setup-command')">Sao chép lệnh cài Windows</button><p id="setup-copy-result" role="status"></p>
<p>Hỗ trợ A4, khay cassette 1: một mặt, hai mặt cạnh dài/cạnh ngắn, ngang/dọc và nhiều trang trên một mặt. Cầu nối tự chạy cùng Windows; mỗi máy tự gửi tới ESP, không cần máy trung gian bật thường trực.</p><p>Windows: mở Thuộc tính máy in / Printing Preferences của driver Canon để chọn kiểu in và cạnh lật; chọn số trang trên một mặt trong Word, Chrome hoặc mục Page Layout của driver. Mac: chọn Print Style (1-sided/2-sided), Binding Location (Long Edge/Short Edge) trong mục Canon; chọn Pages per Sheet và Orientation trong hộp thoại in. Với khổ giấy hoặc nguồn giấy khác, chưa xác minh đầy đủ.</p><p id="tcp" aria-live="polite"></p>
<h2>Cài máy in trên Mac</h2><p><a href="https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/install_mac.sh" download="install_mac.sh">Tải bộ cài macOS</a>. Không cần Python, không cần máy in USB có sẵn. Máy chưa có CAPT sẽ tự tải gói Canon 10.0.10 (43 MB), kiểm tra SHA-256 và chữ ký. Cần mạng Internet và mật khẩu quản trị. Cài driver mới tự động dành cho macOS 11.7.11–27 theo tương thích Canon.</p>
<p>Mở Terminal, dán lệnh và chọn file install_mac.sh vừa tải:</p>
<textarea id="mac-command" aria-label="Lệnh cài máy in trên Mac" readonly rows="3" style="width:100%;box-sizing:border-box">capt_installer=$(osascript -e 'POSIX path of (choose file with prompt "Chon install_mac.sh")') &amp;&amp; sudo /bin/sh "$capt_installer" __STATION_IP__</textarea>
<button type="button" onclick="copyRemote('mac-command')">Sao chép lệnh cài Mac</button><p id="mac-copy-result" role="status"></p>
<p><a href="https://github.com/thientranvan/esp32_printer_canon_3300">Mã nguồn và hướng dẫn</a>. Bộ cài nằm trên GitHub, driver tải từ Canon; không nhúng vào ESP.</p><details><summary>Chẩn đoán và điều khiển Windows</summary><p>Token gửi báo cáo (chỉ dùng cho phiên ESP hiện tại): <code>)HTML";
  page += csrf;
  page += R"HTML(</code></p><p><a href="https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/esp32-printer-diagnostics.ps1" download="esp32-printer-diagnostics.ps1">Tải chẩn đoán Windows</a>: chỉ đọc thông tin driver/cổng in và gửi về ESP.</p>
<p>Tải tệp chẩn đoán, mở PowerShell bằng quyền quản trị rồi dán lệnh. Trong hộp chọn file, chọn script chẩn đoán vừa tải:</p>
<textarea id="diagnostics-command" aria-label="Lệnh chạy chẩn đoán Windows" readonly rows="4" style="width:100%;box-sizing:border-box">Add-Type -AssemblyName System.Windows.Forms; $f = New-Object System.Windows.Forms.OpenFileDialog; $f.Filter = 'PowerShell (*.ps1)|*.ps1'; $f.Title = 'Chon esp32-printer-diagnostics.ps1'; if ($f.ShowDialog() -eq 'OK') { powershell -NoProfile -ExecutionPolicy Bypass -File $f.FileName -StationIP __STATION_IP__ }</textarea>
<button type="button" onclick="copyDiagnostics()">Sao chép lệnh</button><p id="copy-result" role="status"></p>
<script>function copyDiagnostics(){const field=document.getElementById('diagnostics-command');field.focus();field.select();let copied=false;try{copied=document.execCommand('copy');}catch{}document.getElementById('copy-result').textContent=copied?'Đã sao chép. Dán vào PowerShell quản trị.':'Đã chọn lệnh. Nhấn Ctrl+C để sao chép.';}</script>
<h3>Điều khiển Windows từ Mac để sửa máy in</h3>
<p>Bản portable v3 (sửa quyền khóa SSH): tải OpenSSH chính thức của Microsoft từ GitHub (khoảng 6 MB), kiểm tra SHA256 trước khi chạy. Không cần Windows Update và không thay đổi thiết lập chặn cập nhật. Nếu script cũ đang chờ cài Windows Update, nhấn Ctrl+C trước.</p>
<p>Bật SSH riêng trên cổng 22222, chỉ nhận khóa công khai và IP máy hỗ trợ do bạn nhập. Không dùng mật khẩu SSH, không mở cổng router. Quyền quản trị cho phép kiểm tra và sửa máy Windows; kết nối vẫn bật sau khi khởi động lại cho đến khi tắt.</p>
<p><a href="https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/esp32-printer-remote-v3.ps1" download="esp32-printer-remote-v3.ps1">Tải script bật điều khiển</a> . Mở PowerShell quản trị rồi chạy lệnh; hộp chọn file sẽ mở, chọn script bật điều khiển vừa tải:</p>
<textarea id="remote-command" aria-label="Lệnh bật điều khiển Windows" readonly rows="4" style="width:100%;box-sizing:border-box">Add-Type -AssemblyName System.Windows.Forms; $f = New-Object System.Windows.Forms.OpenFileDialog; $f.Filter = 'PowerShell (*.ps1)|*.ps1'; $f.Title = 'Chon esp32-printer-remote-v3.ps1'; if ($f.ShowDialog() -eq 'OK') { powershell -NoProfile -ExecutionPolicy Bypass -File $f.FileName -StationIP __STATION_IP__ }</textarea>
<button type="button" onclick="copyRemote('remote-command')">Sao chép lệnh bật</button>
<p>Tắt quyền điều khiển bất cứ lúc nào bằng PowerShell quản trị:</p>
<textarea id="remote-stop" aria-label="Lệnh tắt điều khiển Windows" readonly rows="4" style="width:100%;box-sizing:border-box">Stop-ScheduledTask -TaskName ESP32PrinterSupport; Disable-ScheduledTask -TaskName ESP32PrinterSupport; Remove-NetFirewallRule -Name ESP32PrinterSupport</textarea>
<button type="button" onclick="copyRemote('remote-stop')">Sao chép lệnh tắt</button><p id="remote-copy-result" role="status"></p>
<script>function copyRemote(id){const field=document.getElementById(id);field.focus();field.select();let copied=false;try{copied=document.execCommand('copy');}catch{}document.getElementById(id==='setup-command'?'setup-copy-result':id==='mac-command'?'mac-copy-result':'remote-copy-result').textContent=copied?(id==='mac-command'?'Đã sao chép. Dán vào Terminal.':'Đã sao chép. Dán vào PowerShell quản trị.'):(id==='mac-command'?'Đã chọn lệnh. Nhấn Command+C để sao chép.':'Đã chọn lệnh. Nhấn Ctrl+C để sao chép.');}</script>
</details><h2>Máy in USB</h2><p id="usb-link"></p><pre id="usb" aria-live="polite" style="white-space:pre-wrap;overflow-wrap:anywhere">Đang kiểm tra...</pre>
<form method="post" action="/usb/start"><input type="hidden" name="token" value=")HTML";
  page += csrf;
  page += R"HTML("><button>Bật USB Host</button><p>Nếu lỗi USB tái diễn, mạch có thể khởi động lại và vào chế độ phục hồi.</p></form>
<h2>Kết nối Wi-Fi</h2><p>Dùng mạng 2,4 GHz. Chỉ lưu sau khi kết nối thành công.</p><p>Tên trạm và IP được cấu hình riêng bên dưới. Tránh trùng IP với thiết bị khác; nên dành địa chỉ trên router.</p>
<form method="post" action="/wifi"><input type="hidden" name="token" value=")HTML";
  page += csrf;
  page += R"HTML("><label>Tên mạng (SSID) <input name="ssid" maxlength="32" required autocomplete="off"></label><label>Mật khẩu Wi-Fi <input name="password" type="password" maxlength="63" autocomplete="new-password"></label><button>Kết nối</button></form>
<h2>Cấu hình trạm in</h2><p>Mỗi ESP có ID riêng. Ví dụ: Máy 1 / .199, Máy 2 / .200, Máy 3 / .201. Khi đổi IP, cài lại hàng đợi trên máy tính. Lưu sẽ khởi động lại trạm.</p>
<form method="post" action="/station"><input type="hidden" name="token" value="__CSRF__">
<label>Tên máy in <input id="station-name" name="name" maxlength="64" required></label>
<label>IP tĩnh <input id="station-ip" name="ip" required></label>
<label>Gateway <input id="station-gateway" name="gateway" required></label>
<label>Subnet <input id="station-mask" name="mask" required></label>
<label>DNS <input id="station-dns" name="dns" required></label>
<label><input type="checkbox" id="cloud-enabled" name="cloud_enabled"> Kết nối quanly.lophochinhminh.com</label>
<label>Token riêng của trạm do server cấp <input name="cloud_token" type="password" maxlength="96" autocomplete="new-password" placeholder="Để trống để giữ token đã lưu"></label>
<p id="cloud-status" role="status"></p><button>Lưu cấu hình và khởi động lại</button></form>
<form method="post" action="/cloud/resolve" onsubmit="return confirm('Chỉ bỏ khóa khi đã kiểm tra giấy đã in và xử lý công việc dở trên máy in. Không tự in lại. Tiếp tục?')"><input type="hidden" name="token" value="__CSRF__"><button>Đã kiểm tra máy in: bỏ khóa công việc web không rõ kết quả</button></form>
<h2>Cập nhật firmware</h2><p>Chọn tệp ứng dụng .bin dành cho bo này. Giữ nguồn ổn định khi cập nhật.</p><form method="post" enctype="multipart/form-data" action="/update?token=)HTML";
  page += csrf;
  page += R"HTML("><label>Firmware <input name="firmware" type="file" accept=".bin" required></label><button>Cập nhật và khởi động lại</button></form>
<script>async function refresh(){try{let r=await fetch('/status');if(!r.ok)return;let s=await r.json();if(!window.stationLoaded){document.getElementById('station-name').value=s.station.name;for(const k of ['ip','gateway','mask','dns'])document.getElementById('station-'+k).value=s.network[k];document.getElementById('cloud-enabled').checked=s.cloud.enabled;window.stationLoaded=true;}document.getElementById('cloud-status').textContent='Web server: '+s.cloud.state+(s.cloud.error?' · '+s.cloud.error:'');document.querySelector('h1').textContent=s.station.name+' · Trạm in ESP32';document.getElementById('tcp').textContent='TCP 9100: '+(s.tcp.state===2?'Đang có phiên in':s.tcp.state===1?'Đang chờ lượt':'Đang chờ máy gửi')+' · Tiến độ ảnh in: '+s.tcp.completed+'/'+(s.tcp.total||'?')+' · Lệnh đã chuyển: '+s.tcp.commands+(s.tcp.error?' · Lỗi: '+s.tcp.error:'');document.getElementById('status').textContent=`Kiểm tra: ${s.healthy?'Đạt':'Lỗi'} | Thời gian chạy: ${s.uptime}s | Wi-Fi: ${s.connected?s.ssid+' · '+s.rssi+' dBm · IP tĩnh '+s.ip:s.connecting?'Đang kết nối':'Chưa kết nối'} | Flash: ${s.flash} | PSRAM: ${s.psram}`;document.getElementById('usb-link').textContent='Chế độ Host: '+(s.usb.host_mode?'Có':'Không')+' · Tín hiệu kết nối USB: '+(s.usb.physical_link?'Có':'Chưa có')+' · Thiết bị đã nhận: '+s.usb.devices;document.getElementById('usb').textContent=(s.usb.recovering?'Đang tự phục hồi kết nối USB':s.usb.responsive?'Máy in phản hồi USB; chưa xác nhận sẵn sàng in':s.usb.connected?'Đã nhận USB Canon nhưng máy in chưa phản hồi / mất liên lạc':s.usb.installed?'Chưa kết nối được máy in; tự dò và phục hồi':'USB Host chưa sẵn sàng')+'\n'+s.usb.details+(s.usb.error?'\nMã lỗi: '+s.usb.error:'');}catch{document.getElementById('status').textContent='Mất kết nối với ESP. Đang thử lại...';}}refresh();setInterval(refresh,3000);</script></html>)HTML";
  page.replace("__CSRF__", csrf);
  page.replace("__STATION_IP__", stationIP.toString());
  server.send(200, "text/html; charset=utf-8", page);
}

void setup() {
  Serial.begin(115200);
  rgbLedWrite(48, 0, 0, 0);
  delay(1000);
  bool memoryOK = false;
  auto memory = static_cast<volatile uint8_t *>(ps_malloc(65536));
  if (memory) {
    memoryOK = true;
    for (size_t i = 0; i < 65536; ++i) memory[i] = uint8_t(i ^ (i >> 8));
    for (size_t i = 0; i < 65536; ++i) {
      if (memory[i] != uint8_t(i ^ (i >> 8))) { memoryOK = false; break; }
    }
    free(const_cast<uint8_t *>(memory));
  }
  bool storageOK = prefs.begin("printer", false);
  if (storageOK) supportCommand = prefs.getString("supportcmd", supportCommand);
  bool wifiOK = WiFi.mode(WIFI_AP_STA);
  uint8_t mac[6]={};bool idOK=esp_read_mac(mac,ESP_MAC_WIFI_STA)==ESP_OK;
  bool nonzero=false;for(uint8_t b:mac)nonzero|=b!=0;idOK=idOK&&nonzero&&!(mac[0]&1);
  char id[19];snprintf(id,sizeof(id),"esp32-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  stationID=idOK?String(id):String("");
  StationSettings saved = {};
  if (prefs.getBytes("station", &saved, sizeof(saved)) == sizeof(saved) && saved.version == 1 &&
      memchr(saved.name,0,sizeof(saved.name)) && memchr(saved.ip,0,sizeof(saved.ip)) &&
      memchr(saved.gateway,0,sizeof(saved.gateway)) && memchr(saved.mask,0,sizeof(saved.mask)) &&
      memchr(saved.dns,0,sizeof(saved.dns)) && memchr(saved.cloudToken,0,sizeof(saved.cloudToken)) &&
      validStationName(saved.name) && validNetwork(saved.ip,saved.gateway,saved.mask,saved.dns)) {
    stationSettings = saved; stationName = saved.name;
    stationIP.fromString(saved.ip); stationGateway.fromString(saved.gateway);
    stationMask.fromString(saved.mask); stationDNS.fromString(saved.dns);
  }
  bool staticOK = WiFi.config(stationIP, stationGateway, stationMask, stationDNS);
  String hostname = "esp32-" + stationID.substring(12);
  WiFi.setHostname(hostname.c_str());
  WiFi.setAutoReconnect(true);
  bool apOK = WiFi.softAP(("ESP32-Printer-" + stationID.substring(12)).c_str(), DEVICE_PASSWORD);
  healthy = idOK && storageOK && wifiOK && staticOK && apOK && memoryOK &&
            ESP.getFlashChipSize() == 16777216 && ESP.getPsramSize() == 8388608;
  csrf = String(esp_random(), HEX) + String(esp_random(), HEX) + String(esp_random(), HEX) + String(esp_random(), HEX);
  if (storageOK) connectSaved();
  MDNS.begin(hostname.c_str());
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("pdl-datastream", "tcp", 9100);
  MDNS.addServiceTxt("pdl-datastream", "tcp", "ty", "Canon LBP3300 CAPT");
  startCaptTCP();
  server.on("/", HTTP_GET, home);
  server.on("/setup/profile.json", HTTP_GET, [] {
    uint32_t mask=uint32_t(WiFi.subnetMask());
    if (WiFi.status()!=WL_CONNECTED || (uint32_t(server.client().remoteIP())&mask)!=(uint32_t(WiFi.localIP())&mask)) {
      server.send(403,"text/plain","LAN only"); return;
    }
    server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json; charset=utf-8",profileJSON());
  });
  server.on("/cloud/resolve", HTTP_POST, [] {
    if(!authorized())return;
    if(!validPost()){server.send(403,"text/plain","Invalid token");return;}
    if(cloudBusy() || !cloudNeedsInspection.load()){server.send(409,"text/plain","No uncertain idle cloud job");return;}
    cloudResolve.store(true);server.send(202,"text/plain; charset=utf-8","Đã yêu cầu bỏ khóa sau kiểm tra; không in lại công việc cũ.");
  });
  server.on("/station", HTTP_POST, [] {
    if (!authorized()) return;
    if (!validPost()) { server.send(403,"text/plain","Invalid token"); return; }
    if (captInUse() || captTCPState.load() || captCommand.state.load()==1 || captCommand.state.load()==2 || cloudBusy()) {
      server.send(409,"text/plain","Printer busy; settings unchanged"); return;
    }
    String name=server.arg("name"), ip=server.arg("ip"), gateway=server.arg("gateway"), mask=server.arg("mask"), dns=server.arg("dns");
    String token=server.arg("cloud_token");
    if (!token.length()) token=stationSettings.cloudToken;
    bool enabled=server.arg("cloud_enabled")=="on";
    bool validToken=token.length()>=32 && token.length()<=96;
    for (char c : token) if (!isalnum(uint8_t(c)) && c!='-' && c!='_') validToken=false;
    if (!validStationName(name) || !validNetwork(ip,gateway,mask,dns) || (enabled && !validToken)) {
      server.send(400,"text/plain","Invalid name/network/token; nothing saved"); return;
    }
    StationSettings next = {}; next.version=1; next.cloudEnabled=enabled;
    name.toCharArray(next.name,sizeof(next.name)); ip.toCharArray(next.ip,sizeof(next.ip));
    gateway.toCharArray(next.gateway,sizeof(next.gateway)); mask.toCharArray(next.mask,sizeof(next.mask)); dns.toCharArray(next.dns,sizeof(next.dns));
    token.toCharArray(next.cloudToken,sizeof(next.cloudToken));
    if (prefs.putBytes("station",&next,sizeof(next))!=sizeof(next)) { server.send(500,"text/plain","Save failed"); return; }
    server.send(200,"text/plain; charset=utf-8","Đã lưu. Trạm khởi động lại; mở http://"+ip+". Cài lại máy in nếu đổi IP."); rebootAt=millis()+1500;
  });
  server.on("/support/command", HTTP_GET, [] {
    if (!authorized()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json; charset=utf-8", "{\"command\":" + jsonString(supportCommand) + "}");
  });
  server.on("/support/command", HTTP_POST, [] {
    if (!authorized()) return;
    if (!validPost()) { server.send(403, "text/plain", "Invalid token"); return; }
    String command = server.arg("command");
    if (!command.length() || command.length() > 8192) { server.send(400, "text/plain", "Command must be 1..8192 bytes"); return; }
    if (!prefs.putString("supportcmd", command)) { server.send(500, "text/plain", "Cannot save command"); return; }
    supportCommand = command;
    server.send(200, "text/plain", "Saved");
  });
  server.on("/setup/windows.ps1", HTTP_GET, [] {
    if (!authorized()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Location", "https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/esp32-printer-setup-v3.ps1");
    server.send(302, "text/plain", "Download from GitHub");
  });
  server.on("/setup/windows-diagnostics.ps1", HTTP_GET, [] {
    if (!authorized()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Location", "https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/esp32-printer-diagnostics.ps1");
    server.send(302, "text/plain", "Download from GitHub");
  });
  server.on("/setup/windows-remote.ps1", HTTP_GET, [] {
    if (!authorized()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Location", "https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/esp32-printer-remote-v3.ps1");
    server.send(302, "text/plain", "Download from GitHub");
  });
  server.on("/support/windows", HTTP_POST, [] {
    // Downloaded admin script carries this boot's CSRF capability, not admin password.
    if (!validPost()) { server.send(403, "text/plain", "Invalid token"); return; }
    String data = server.arg("data");
    if (!data.length() || data.length() > 60000) { server.send(400, "text/plain", "Report must be 1..60000 bytes"); return; }
    windowsReport = data;
    server.send(200, "text/plain", "Saved in RAM");
  });
  server.on("/support/windows", HTTP_GET, [] {
    if (!authorized()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send(windowsReport.length() ? 200 : 404, "text/plain; charset=utf-8", windowsReport);
  });
  server.on("/printer/command", HTTP_GET, captResult);
  server.on("/printer/command", HTTP_POST, captSubmit);
  server.on("/printer/release", HTTP_POST, [] {
    if (!authorized()) return;
    if (!validPost()) { server.send(403, "text/plain", "Invalid token"); return; }
    uint32_t session = captSession();
    if (!session) { server.send(400, "text/plain", "Invalid session"); return; }
    int state = captCommand.state.load();
    if (state != 0 && state != 3) { server.send(409, "text/plain", "USB operation busy"); return; }
    releaseCapt(session);
    server.send(200, "application/json", "{\"released\":true}");
  });
  server.on("/usb/start", HTTP_POST, [] {
    if (!authorized()) return;
    if (!validPost()) { server.send(403, "text/plain", "Invalid token"); return; }
    if (usbTaskStarted) { server.sendHeader("Location", "/"); server.send(303); return; }
    server.sendHeader("Location", "/");
    server.send(303);
    startUSB();
  });
  server.on("/crash.bin", HTTP_GET, [] {
    if (!authorized()) return;
    size_t address = 0, size = 0;
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    esp_err_t error = esp_core_dump_image_check();
    if (error == ESP_OK) error = esp_core_dump_image_get(&address, &size);
    if (error != ESP_OK || !partition || address != partition->address || size > partition->size) {
      server.send(404, "text/plain", "No valid crash dump"); return;
    }
    server.setContentLength(size);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/octet-stream", "");
    char buffer[512];
    for (size_t offset = 0; offset < size; offset += sizeof(buffer)) {
      size_t count = min(sizeof(buffer), size - offset);
      if (esp_partition_read(partition, offset, buffer, count) != ESP_OK) { server.client().stop(); return; }
      server.sendContent(buffer, count);
    }
  });
  server.on("/crash", HTTP_GET, [] {
    if (!authorized()) return;
    esp_core_dump_summary_t summary = {};
    esp_err_t error = esp_core_dump_image_check();
    if (error == ESP_OK) error = esp_core_dump_get_summary(&summary);
    if (error != ESP_OK) { server.send(404, "text/plain", esp_err_to_name(error)); return; }
    String report = "Task: " + String(summary.exc_task) + "\nPC: 0x" + String(summary.exc_pc, HEX) +
      "\nCause: " + summary.ex_info.exc_cause + "\nAddress: 0x" + String(summary.ex_info.exc_vaddr, HEX) +
      "\nELF SHA256: " + String(reinterpret_cast<const char *>(summary.app_elf_sha256)) + "\nBacktrace:";
    for (size_t i = 0; i < summary.exc_bt_info.depth && i < 16; ++i)
      report += " 0x" + String(summary.exc_bt_info.bt[i], HEX);
    report += "\n";
    report += usbTraceReport();
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain", report);
  });
  server.on("/status", HTTP_GET, [] {
    if (!authorized()) return;
    USBStatus usb = readUSB();
    String json = String("{\"station\":") + profileJSON() + ",\"network\":{\"ip\":" + jsonString(stationIP.toString()) + ",\"gateway\":" + jsonString(stationGateway.toString()) + ",\"mask\":" + jsonString(stationMask.toString()) + ",\"dns\":" + jsonString(stationDNS.toString()) + "},\"cloud\":" + cloudStatusJSON() + ",\"healthy\":" + (healthy ? "true" : "false") +
      ",\"connected\":" + (WiFi.status() == WL_CONNECTED ? "true" : "false") +
      ",\"connecting\":" + (connecting ? "true" : "false") +
      ",\"ssid\":" + jsonString(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("")) +
      ",\"rssi\":" + WiFi.RSSI() + ",\"static_ip\":true" +
      ",\"reset_reason\":" + int(esp_reset_reason()) +
      ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"uptime\":" + millis()/1000 +
      ",\"flash\":" + ESP.getFlashChipSize() + ",\"psram\":" + ESP.getPsramSize() +
      ",\"usb\":{\"installed\":" + (usb.installed ? "true" : "false") +
      ",\"connected\":" + (usb.connected ? "true" : "false") +
      ",\"responsive\":" + (usb.responsive ? "true" : "false") +
      ",\"recovering\":" + (usb.recovering ? "true" : "false") +
      ",\"response_age_ms\":" + (usb.responseAt ? String(uint32_t(millis() - usb.responseAt)) : String("null")) +
      ",\"recoveries\":" + usb.recoveries +
      ",\"heartbeat_error\":" + usb.heartbeatError +
      ",\"host_mode\":" + (usb.hostMode ? "true" : "false") +
      ",\"physical_link\":" + (usb.physicalLink ? "true" : "false") +
      ",\"power_command\":" + (usb.powerCommand ? "true" : "false") +
      ",\"port_register\":" + usb.portRegister + ",\"devices\":" + usb.devices +
      ",\"descriptor_id\":" + usb.descriptorID + ",\"descriptor_class\":" + usb.descriptorClass + ",\"descriptor_count\":" + usb.descriptorCount +
      ",\"error\":" + usb.error + ",\"details\":" + jsonString(usb.details) + "}" +
      ",\"capt\":" + captTelemetryJSON() + ",\"tcp\":{\"port\":9100,\"state\":" + captTCPState.load() +
      ",\"error\":" + captTCPError.load() + ",\"last_header\":" + captTCPHeader.load() +
      ",\"commands\":" + captTCPCommands.load() + ",\"total\":" + captTCPTotal.load() + ",\"completed\":" + captTCPCompleted.load() + "}}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
  });
  server.on("/wifi", HTTP_POST, [] {
    if (!authorized()) return;
    if (!validPost()) { server.send(403, "text/plain", "Invalid token"); return; }
    String ssid = server.arg("ssid"), password = server.arg("password");
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
        (password.length() > 0 && password.length() < 8)) {
      server.send(400, "text/plain", "Invalid SSID or password length"); return;
    }
    if (connecting) { server.send(409, "text/plain", "Connection pending"); return; }
    pendingSSID = ssid;
    pendingPassword = password;
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
    connectionAt = millis();
    connecting = true;
    server.sendHeader("Location", "/");
    server.send(303);
  });
  server.on("/update", HTTP_POST, [] {
    if (!authorized()) return;
    if (!validPost()) { server.send(403, "text/plain", "Invalid token"); return; }
    bool ok = uploadOK && !Update.hasError();
    server.send(ok ? 200 : 400, "text/plain; charset=utf-8", ok ? "Cập nhật thành công. Đang khởi động lại." : "Cập nhật thất bại. Firmware hiện tại được giữ.");
    if (ok) rebootAt = millis() + 2000;
  }, [] {
    if (!server.authenticate("admin", DEVICE_PASSWORD) || !validPost()) return;
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      uploadOK = false;
      if (!upload.filename.endsWith(".bin")) return;
      uploadOK = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    } else if (upload.status == UPLOAD_FILE_WRITE && uploadOK) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        uploadOK = false;
        Update.abort();
      }
    } else if (upload.status == UPLOAD_FILE_END && uploadOK) {
      uploadOK = Update.end(true);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.abort();
      uploadOK = false;
    }
  });
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();
  startCloud();
  const esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT ||
      resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT) {
    USBStatus status = {};
    snprintf(status.details, sizeof(status.details), "USB Host tạm dừng để kiểm tra crash (reset %d). Web vẫn hoạt động để cập nhật firmware.", int(resetReason));
    publishUSB(status);
  } else {
    startUSB();
  }
  Serial.printf("STATION: %s\nAP: ESP32-Printer-36B0\nPassword: %s\nWeb: http://192.168.4.1\nLogin: admin\n", healthy ? "PASS" : "FAIL", DEVICE_PASSWORD);
}

void loop() {
  server.handleClient();
  if (connecting && WiFi.status() == WL_CONNECTED) {
    WiFiConfig config = {};
    pendingSSID.toCharArray(config.ssid, sizeof(config.ssid));
    pendingPassword.toCharArray(config.password, sizeof(config.password));
    bool saved = prefs.putBytes("wifi", &config, sizeof(config)) == sizeof(config);
    Serial.printf("Wi-Fi connected: %s; saved=%d\n", WiFi.localIP().toString().c_str(), saved);
    pendingPassword = "";
    connecting = false;
  } else if (connecting && millis() - connectionAt > 30000) {
    WiFi.disconnect();
    connectSaved();
    pendingPassword = "";
    connecting = false;
    Serial.println("Wi-Fi attempt failed; restored saved settings");
  }
  uint32_t phase = millis() % 3000;
  rgbLedWrite(48, !healthy && phase < 100 ? 2 : 0, healthy && phase < 100 ? 2 : 0, 0);
  if (rebootAt && int32_t(millis() - rebootAt) >= 0) ESP.restart();
  static uint32_t lastLog = 0;
  if (millis() - lastLog >= 3000) {
    lastLog = millis();
    Serial.printf("STATION: %s, uptime=%lu, AP=%s, LAN=%s, heap=%u\n", healthy ? "PASS" : "FAIL", millis()/1000, WiFi.softAPIP().toString().c_str(), WiFi.localIP().toString().c_str(), ESP.getFreeHeap());
  }
  delay(2);
}
