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
  return "{\"schema\":1,\"lan_metadata_version\":1,\"model\":\"LBP3300\",\"id\":" + jsonString(stationID) +
    ",\"name\":" + jsonString(stationName) + ",\"ip\":" + jsonString(WiFi.localIP().toString()) + "}";
}

#include "cloud_print.h"
#include "realtime.h"

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
  String page = R"HTML(<!doctype html>
<html lang="vi"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Trạm in ESP32</title>
<style>
:root{color-scheme:light;--ink:#182b3b;--muted:#526777;--line:#dce5eb;--accent:#086b60}*{box-sizing:border-box}body{margin:0;background:#f2f6f8;color:var(--ink);font:14px/1.45 system-ui,sans-serif}main{max-width:1120px;margin:auto;padding:12px 16px}header{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-bottom:12px}.eyebrow{color:var(--accent);font-size:12px;font-weight:750;letter-spacing:.12em}h1{font-size:24px;line-height:1.2;margin:6px 0}h2{font-size:17px;margin:0 0 8px}p{margin:8px 0;color:var(--muted)}a{color:var(--accent);text-underline-offset:3px}button,.button{display:inline-block;padding:7px 11px;border:1px solid var(--line);border-radius:9px;background:white;color:var(--ink);font:inherit;font-weight:650;cursor:pointer;text-decoration:none}button:hover,.button:hover{background:#eaf2f3}button:disabled{opacity:.55;cursor:wait}.primary{background:var(--accent);border-color:var(--accent);color:white}.primary:hover{background:#07574e}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.card,details{background:white;border:1px solid var(--line);border-radius:10px;padding:14px}.grid>*,.fields>*{min-width:0}.wide{grid-column:1/-1}.badge{display:inline-block;padding:4px 10px;border-radius:20px;font-size:13px;background:#edf1f4;color:var(--muted)}.badge[data-tone=ok]{background:#dff4eb;color:#15563e}.badge[data-tone=warn]{background:#fff1d6;color:#735015}.badge[data-tone=error]{background:#fbe7e7;color:#922c2c}.big{font-size:19px;font-weight:700;line-height:1.4;margin:6px 0;color:var(--ink)}.printer-status{display:flex;align-items:center;gap:10px}.printer-status[data-tone=ok]{color:#166534}.printer-status[data-tone=busy]{color:#1754a1}.printer-status[data-tone=warn]{color:#855400}.printer-status[data-tone=error]{color:#b42318}.printer-status[data-tone=offline]{color:#596579}.printer-status .big{color:inherit}.status-icon{display:flex;align-items:center;justify-content:center;flex:0 0 56px;width:56px;height:56px;font-size:44px;font-weight:700;line-height:1}.meta{font-size:13px;color:var(--muted)}.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.row input{flex:1;min-width:180px}.stack{display:grid;gap:8px}label{display:block;font-size:14px;font-weight:600}input,textarea{display:block;width:100%;margin-top:6px;padding:7px 9px;border:1px solid #b9cbd5;border-radius:8px;background:white;color:var(--ink);font:inherit}input[readonly],textarea{background:#f7f9fb;font:13px/1.6 ui-monospace,monospace}input[type=checkbox]{display:inline;width:auto;margin:0 8px 0 0}input[type=file]{font-size:13px}textarea{resize:vertical;min-height:58px}summary{font-weight:700;cursor:pointer}details{margin-top:10px}details[open]>summary{margin-bottom:10px}.fields{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.fields .full{grid-column:1/-1}.notice{padding:12px;border-radius:8px;background:#fff5df;color:#725018}.error{color:#922c2c}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:12px;background:#f7f9fb;padding:12px;border-radius:8px}footer{margin-top:12px;font-size:12px;color:var(--muted)}:focus-visible{outline:3px solid #168779;outline-offset:3px}progress{width:100%;height:9px;accent-color:var(--accent)}[hidden]{display:none!important}@media(max-width:620px){main{padding:14px 10px}header{align-items:flex-start;flex-direction:column}.grid,.fields{grid-template-columns:minmax(0,1fr)}.card,details{padding:12px}h1{font-size:26px}}
.support-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.support-grid textarea{font-size:12px;line-height:1.4;min-height:68px}#feedback:empty{display:none}@media(max-width:620px){.support-grid{grid-template-columns:1fr}}
</style></head><body><main>
<header><div><span class="eyebrow">TRẠM IN WI-FI</span><h1 id="name">Canon LBP3300</h1><span class="meta">Canon LBP3300 · A4 · Khay cassette 1</span></div><div class="row"><a class="button" href="#install" onclick="document.getElementById('install').open=true">Cài máy in</a><span id="connection" class="badge" role="status">Đang kết nối trạm…</span></div></header>
<div class="grid">
<section class="card" aria-labelledby="printer-title"><h2 id="printer-title">Máy in</h2><div id="printer-status" class="printer-status" data-tone="offline" role="status" aria-live="polite" aria-atomic="true"><span id="printer-icon" class="status-icon" aria-hidden="true">⊘</span><div id="printer" class="big">Đang đọc trạng thái…</div></div><p id="printer-note">Cập nhật trực tiếp theo thay đổi.</p><div id="job" hidden><p id="job-progress"></p><progress id="progress" aria-label="Tiến độ mặt in"></progress></div></section>
<section class="card" aria-labelledby="network-title"><h2 id="network-title">Kết nối</h2><p class="meta">Wi-Fi</p><div id="wifi" class="big">Đang đọc…</div><p id="network"></p><p id="cloud">Web: đang đọc…</p></section>
<section class="card wide" aria-labelledby="jobs-title"><h2 id="jobs-title">Lệnh in trên trạm</h2><p id="control-job">Chưa có lệnh in.</p><p id="control-state" role="status"></p><div class="row"><button id="job-pause" onclick="controlJob('pause')" disabled>Dừng sau mặt đang gửi</button><button id="job-resume" onclick="controlJob('resume')" disabled>Tiếp tục gửi</button><button id="job-cancel" onclick="controlJob('cancel')" disabled>Hủy gửi tiếp</button><button id="job-resolve" onclick="resolveJob()" disabled>Đối soát kết quả</button><button id="job-delete" onclick="controlJob('delete')" disabled>Xóa lịch sử trên trạm</button></div><p class="meta">Xóa lệnh ở máy tính không xóa lịch sử trên trạm hoặc lệnh của máy khác. Trang đã gửi vẫn có thể được in.</p></section>
<section class="card wide"><h2>Hàng đợi web</h2><p id="queue-note">Chờ dữ liệu server.</p><div id="server-jobs" class="stack"></div></section>

<details class="wide"><summary>Đăng ký máy in trên web · ID ESP</summary><label for="station-id">ID ESP</label><div class="row"><input id="station-id" value="__STATION_ID__" readonly><button type="button" onclick="copyField('station-id')">Sao chép ID</button></div><p class="meta">Thêm ID này trên <a href="https://quanly.lophocbinhminh.com/index.php?page=printers" target="_blank" rel="noopener">trang quản lý máy in</a>, lấy token rồi dán vào <a href="#settings">cấu hình trạm</a>.</p></details>
</div>
<details id="support-panel"><summary>Hỗ trợ chẩn đoán từ Mac</summary>
<p class="meta">Chạy trên Windows bằng PowerShell quản trị. Chỉ bật khi cần hỗ trợ; dùng SSH đã cài, không ảnh hưởng bộ cài máy in.</p>
<div class="support-grid"><div><label for="support-start">Bật kết nối hỗ trợ</label><textarea id="support-start" readonly rows="3">$ErrorActionPreference='Stop'; Enable-NetFirewallRule -Name ESP32PrinterSupport; Enable-ScheduledTask -TaskName ESP32PrinterSupport | Out-Null; Start-ScheduledTask -TaskName ESP32PrinterSupport; Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*'} | Select-Object IPAddress</textarea><button onclick="copyField('support-start')">Sao chép lệnh bật</button></div>
<div><label for="support-stop">Dừng kết nối hỗ trợ</label><textarea id="support-stop" readonly rows="3">Disable-NetFirewallRule -Name ESP32PrinterSupport; Stop-ScheduledTask -TaskName ESP32PrinterSupport; Disable-ScheduledTask -TaskName ESP32PrinterSupport | Out-Null; Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -eq "$env:ProgramData\ESP32PrinterSupport\OpenSSH-Win64\sshd.exe"} | ForEach-Object {Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue}</textarea><button onclick="copyField('support-stop')">Sao chép lệnh dừng</button></div></div>
<p class="meta">Chưa cài SSH hỗ trợ? <a href="https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/esp32-printer-remote-v3.ps1" download>Tải script hỗ trợ</a>. Cấu hình IP và khóa SSH của Mac khi cài lần đầu. Lệnh bật giữ nguyên giới hạn truy cập đã cấu hình.</p>
<details><summary>Lệnh kết nối trên Mac</summary><p class="meta">Terminal trên Mac đã có khóa SSH hỗ trợ: thay USER_WINDOWS và IP_WINDOWS bằng tài khoản và IP Windows vừa hiện ở lệnh bật.</p><textarea id="support-mac" readonly rows="2">ssh -i ~/.ssh/esp32_printer_windows -o IdentitiesOnly=yes -p 22222 USER_WINDOWS@IP_WINDOWS</textarea><button onclick="copyField('support-mac')">Sao chép lệnh Mac</button></details><p class="meta">Lệnh dừng ngắt cả phiên hỗ trợ đang kết nối. Máy in LAN vẫn hoạt động.</p><div id="support" hidden><label for="support-command">Bước chẩn đoán hiện tại</label><textarea id="support-command" readonly rows="2" aria-label="Lệnh hỗ trợ Windows"></textarea><button onclick="copyField('support-command')">Sao chép lệnh chẩn đoán</button></div></details>
<details id="install"><summary>Cài máy in Windows / Mac · bộ cài mới nhất trên GitHub</summary><p>Cài một lần trên từng máy. Bộ cài tự tải driver Canon nếu chưa có.</p>
<div class="grid"><section><h2>Windows 10 / 11</h2><a class="button" href="https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/installers/esp32-printer-setup-v3.ps1" download>Tải bộ cài Windows</a><p class="meta">Tải file, mở PowerShell quản trị, sao chép lệnh rồi chọn file vừa tải.</p><textarea id="windows-command" aria-label="Lệnh cài Windows" readonly rows="4">Add-Type -AssemblyName System.Windows.Forms; $f = New-Object System.Windows.Forms.OpenFileDialog; $f.Filter = 'PowerShell (*.ps1)|*.ps1'; if ($f.ShowDialog() -eq 'OK') { powershell -NoProfile -ExecutionPolicy Bypass -File $f.FileName -StationIP __STATION_IP__ }</textarea><button type="button" onclick="copyField('windows-command')">Sao chép lệnh Windows</button></section>
<section><h2>macOS</h2><a class="button" href="https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/install_mac.sh" download>Tải bộ cài Mac</a><p class="meta">Tải file, mở Terminal, sao chép lệnh rồi chọn file vừa tải.</p><textarea id="mac-command" aria-label="Lệnh cài Mac" readonly rows="4">capt_installer=$(osascript -e 'POSIX path of (choose file with prompt "Chon install_mac.sh")') &amp;&amp; sudo /bin/sh "$capt_installer" __STATION_IP__</textarea><button type="button" onclick="copyField('mac-command')">Sao chép lệnh Mac</button></section></div>
<p class="meta">Chọn máy in trong Word, Chrome hoặc trình đọc PDF. Tùy chọn in nằm trong hộp thoại in của ứng dụng/driver.</p></details>
<details id="settings"><summary>Cấu hình trạm &amp; kết nối web</summary><p>Đổi tên hoặc IP sẽ cần cập nhật hàng đợi trên máy tính. Lưu cấu hình sẽ khởi động lại ESP.</p>
<form action="/station" method="post" class="stack"><input name="token" type="hidden" value="__CSRF__"><div class="fields">
<label class="full">Tên máy in<input id="station-name" name="name" maxlength="64" required></label>
<label>IP tĩnh<input id="station-ip" name="ip" inputmode="decimal" required></label><label>Gateway<input id="station-gateway" name="gateway" inputmode="decimal" required></label>
<label>Subnet<input id="station-mask" name="mask" inputmode="decimal" required></label><label>DNS<input id="station-dns" name="dns" inputmode="decimal" required></label></div>
<label><input id="cloud-enabled" name="cloud_enabled" type="checkbox">Nhận lệnh in từ web</label><label>Token do server cấp<input name="cloud_token" type="password" maxlength="96" autocomplete="new-password" placeholder="Để trống để giữ token đã lưu"></label><p class="meta">quanly.lophocbinhminh.com · Token chỉ hiện một lần khi server cấp.</p><div><button class="primary" data-config disabled>Lưu &amp; khởi động lại</button></div></form></details>
<details><summary>Đổi Wi-Fi</summary><p>Dùng mạng 2,4 GHz. Chỉ lưu sau khi kết nối thành công.</p><form action="/wifi" method="post" class="stack"><input type="hidden" name="token" value="__CSRF__"><label>Tên mạng (SSID)<input name="ssid" maxlength="32" required autocomplete="off"></label><label>Mật khẩu Wi-Fi<input name="password" type="password" maxlength="63" autocomplete="new-password"></label><div><button>Kết nối Wi-Fi</button></div></form></details>
<details><summary>Bảo trì &amp; chẩn đoán</summary><p id="hardware" class="meta"></p><pre id="diagnostics">Đang đọc dữ liệu USB…</pre>
<form action="/usb/start" method="post" id="usb-start" hidden><input type="hidden" name="token" value="__CSRF__"><button>Bật USB Host</button></form>
<form action="/cloud/resolve" method="post" id="resolve" hidden onsubmit="return confirm('Chỉ bỏ khóa khi đã kiểm tra giấy đã in và xử lý công việc dở trên máy in. Không tự in lại. Tiếp tục?')"><input type="hidden" name="token" value="__CSRF__"><p class="notice">Công việc web chưa rõ kết quả. Kiểm tra giấy và xử lý lệnh dở trước khi bỏ khóa.</p><button>Bỏ khóa sau khi đã kiểm tra</button></form>
<form action="/update?token=__CSRF__" method="post" enctype="multipart/form-data" class="stack"><label>Cập nhật firmware<input name="firmware" type="file" accept=".bin" required></label><p class="meta">Chọn đúng firmware cho ESP32-S3. Giữ nguồn ổn định khi cập nhật.</p><div><button>Cập nhật firmware</button></div></form></details>
<p id="feedback" role="status"></p><footer>ESP32-S3 · <a href="https://github.com/thientranvan/esp32_printer_canon_3300">Hướng dẫn &amp; mã nguồn</a></footer></main>
<script>
const el=id=>document.getElementById(id);let loaded=false;
function copyField(id){const field=el(id);field.focus();field.select();let ok=false;try{ok=document.execCommand('copy');}catch{}el('feedback').textContent=ok?'Đã sao chép.':'Đã chọn nội dung. Nhấn Ctrl+C hoặc Command+C để sao chép.';}
const cloudNames={disabled:'Chưa bật',connecting:'Đang kết nối',idle:'Đã kết nối',server_error:'Lỗi kết nối server',downloading:'Đang tải lệnh in',waiting_printer:'Đang chờ máy in',printing:'Đang in',completed:'Đã hoàn tất',failed:'Lệnh in lỗi',uncertain:'Cần kiểm tra kết quả in',resolved:'Đã đối soát',blocked:'Đang bị khóa',paused:'Đã dừng gửi mặt tiếp theo'};
let cached=null,currentJobId='';
async function resolveJob(){
 if(!confirm('Chỉ xác nhận sau khi đã kiểm tra giấy và xử lý công việc dở trên máy in. Không gửi lại job cũ. Tiếp tục?'))return;
 try{const r=await fetch('/jobs/resolve',{method:'POST',body:new URLSearchParams({token:'__CSRF__',job_id:currentJobId})});el('feedback').textContent=await r.text();refresh();}catch{el('feedback').textContent='Chưa xác nhận đối soát.';}
}
async function controlQueuedJob(id,action){
 if((action==='delete'||action==='cancel')&&!confirm('Gửi yêu cầu '+action+' cho job '+id+'? Server sẽ kiểm tra trạng thái trước khi thực hiện.'))return;
 try{const r=await fetch('/jobs/queue/control',{method:'POST',body:new URLSearchParams({token:'__CSRF__',job_id:id,action})});el('feedback').textContent=await r.text();}catch{el('feedback').textContent='Chưa gửi được yêu cầu tới trạm.';}
}
let queueVersion='';
function renderQueue(q,cmd){
 if(!q)return;
 el('queue-note').textContent=!q.supported?'Server chưa cung cấp API hàng đợi trực tiếp.':q.age_ms>5000?'Dữ liệu hàng đợi đã cũ; không xác nhận trạng thái hiện tại.':(q.truncated?'Chỉ hiện 16 job gần nhất. ':'')+'Cập nhật từ server.';
 if(cmd&&cmd.id)el('queue-note').textContent+=' Yêu cầu '+cmd.id+': '+cmd.result;
 const version=JSON.stringify([q.revision,cmd]);if(version===queueVersion)return;queueVersion=version;
 const container=el('server-jobs');container.replaceChildren();
 for(const item of q.items||[]){const row=document.createElement('div'),label=document.createElement('p');label.textContent=(item.title||item.id)+' · '+item.id+' · '+item.state;row.append(label);
  for(const action of ['pause','resume','cancel','delete']){const button=document.createElement('button');button.textContent=({pause:'Dừng',resume:'Tiếp tục',cancel:'Hủy',delete:'Xóa'})[action];button.disabled=q.age_ms>5000||(cmd&&cmd.result==='pending');button.addEventListener('click',()=>controlQueuedJob(item.id,action));row.append(button);}container.append(row);
 }
}
async function controlJob(action){
 if(!currentJobId)return;
 if(action==='cancel'&&!confirm('Hủy gửi dữ liệu tiếp theo? Các trang đã vào máy in vẫn có thể ra giấy. Công việc sẽ cần kiểm tra kết quả, không tự in lại.'))return;
 try{const r=await fetch('/jobs/control',{method:'POST',body:new URLSearchParams({token:'__CSRF__',job_id:currentJobId,action})});el('feedback').textContent=await r.text();if(r.ok)refresh();}catch{el('feedback').textContent='Chưa gửi được yêu cầu; không xác nhận đã thực hiện.';}
}
async function refresh(patch=null){try{
 let s;if(patch&&cached){s={...cached,...patch,usb:{...cached.usb,...patch.usb},tcp:{...cached.tcp,...patch.tcp}};}else{const r=await fetch('/status',{cache:'no-store'});if(!r.ok)throw Error();s=await r.json();}cached=s;const u=s.usb,c=s.cloud,t=s.tcp;renderQueue(s.server_queue,s.queue_command);
 const j=s.job_control||{};currentJobId=j.id||'';
 el('jobs-title').textContent=j.active?'Lệnh đang chạy trên trạm':'Lịch sử lệnh gần nhất trên trạm';
 el('control-job').textContent=j.id?(j.source==='cloud'?'Web':'LAN')+' · '+(j.title?j.title+' · ':'')+(j.client_name?j.client_name+' · ':'')+j.id+' · '+j.completed+' / '+(j.total||'?')+' mặt in':'Chưa có lệnh in trên trạm.';
 const states={failed:'Chưa gửi dữ liệu in — lỗi trước khi in',printing:'Đang gửi/in',paused:'Đã dừng trước mặt tiếp theo',completed:'Đã hoàn tất',uncertain:'Chưa rõ kết quả — cần kiểm tra máy và giấy',resolved:'Đã đối soát'};
 const requests={pause_requested:'Đã yêu cầu dừng, chờ bộ gửi xác nhận',resume_requested:'Đã yêu cầu tiếp tục, chờ xác nhận',cancel_requested:'Đã yêu cầu hủy gửi tiếp, chờ xác nhận'};
 el('control-state').textContent=(states[j.state]||j.state||'')+(j.request?' · '+(requests[j.request]||j.request):'');
 for(const action of ['pause','resume','cancel'])el('job-'+action).disabled=j.actions?!j.actions[action]:!(j.active&&j.controllable)||j.request==='cancel_requested';
 el('job-resolve').disabled=!j.id||j.active||j.state!=='uncertain';
 el('job-delete').disabled=!j.id||j.active||j.state==='uncertain';
 el('support').hidden=!s.support_command||s.support_command.startsWith('#');el('support-command').value=s.support_command||'';
 el('name').textContent=s.station.name;el('connection').textContent=s.healthy?'Trạm hoạt động':'Trạm cần kiểm tra';el('connection').dataset.tone=s.healthy?'ok':'error';
 el('wifi').textContent=s.connected?s.ssid:s.connecting?'Đang kết nối…':'Chưa kết nối Wi-Fi';el('network').textContent=s.connected?'IP '+s.ip+' · '+s.rssi+' dBm':'Kết nối Wi-Fi để in qua LAN và web.';
 el('cloud').textContent='Web: '+(cloudNames[c.state]||c.state)+(c.error?' · '+c.error:'');
 let title='Chưa kết nối máy in',note='Kiểm tra nguồn máy in và dây USB–OTG.';
 if(u.recovering){title='Đang khôi phục USB';note='Trạm đang tự dò lại máy in.';}
 else if(u.error){title='Lỗi kết nối USB';note='Mã lỗi '+u.error+'. Xem chẩn đoán bên dưới.';}
 else if(u.connected&&!u.responsive){title='Máy in chưa phản hồi';note='Kiểm tra nguồn và kết nối USB.';}
 else if(u.connected&&u.responsive){title=j.active?(j.source==='cloud'?'Đang in từ web':'Đang in qua LAN'):'Máy in đã kết nối';note='USB có phản hồi; chưa xác minh cảm biến giấy/mực.';}
 const p=s.capt||{};
 if(u.connected&&u.responsive){
  if(!p.fresh){title='Đang đọc trạng thái máy in';note='Chưa có phản hồi CAPT mới; không xác nhận máy sẵn sàng.';}
  else {const faults=[];if(p.service_error)faults.push(p.service_code||'Lỗi service');if(p.paper_jam)faults.push('Kẹt giấy');if(p.cover_open)faults.push('Nắp đang mở');if(p.paper_required)faults.push('Cần giấy / kiểm tra khổ và nguồn giấy');if(p.toner_warning)faults.push('Kiểm tra hộp mực');
   if(faults.length){title=faults.join(' · ');note='Trạng thái đọc từ máy in. Không tự xóa lỗi hoặc in lại.';}
   else note='CAPT đã phản hồi · cập nhật '+Math.round(p.age_ms/1000)+' giây trước.';
  }
 }
 let tone='offline',icon='⊘';
 if(u.recovering){tone='warn';icon='↻';}
 else if(u.error){tone='error';icon='✕';}
 else if(u.connected){tone='warn';icon='⚠';
  if(u.responsive&&p.fresh){
   if(p.service_error||p.paper_jam){tone='error';icon='✕';}
   else if(p.cover_open||p.paper_required||p.toner_warning||j.state==='uncertain'||j.state==='paused'){tone='warn';icon='⚠';}
   else if(j.active){tone='busy';icon='↻';}
   else{tone='ok';icon='✓';}
  }
 }
 el('printer-status').dataset.tone=tone;el('printer-icon').textContent=icon;
 el('printer').textContent=title;el('printer-note').textContent=note;
 // Progress belongs to the persistent job, never a diagnostic TCP connection.
 const known=!!j.id&&j.total>0,total=known?j.total:0,done=known?j.completed:0;
 el('job').hidden=!j.active;
 const phase=states[j.state]||j.state||'Đang xử lý';
 el('job-progress').textContent=phase+' · '+(known?'Đã ghi nhận '+done+' / '+total+' mặt in':'Chưa có tổng số mặt in');
 el('progress').max=total||1;if(known)el('progress').value=done;else el('progress').removeAttribute('value');
 if(!loaded){el('station-name').value=s.station.name;for(const k of ['ip','gateway','mask','dns'])el('station-'+k).value=s.network[k];el('cloud-enabled').checked=c.enabled;loaded=true;document.querySelector('[data-config]').disabled=false;}
 el('hardware').textContent='Thời gian chạy '+Math.floor(s.uptime/60)+' phút · Flash '+Math.round(s.flash/1048576)+' MB · PSRAM '+Math.round(s.psram/1048576)+' MB';
 el('diagnostics').textContent=(u.details||'Chưa có thiết bị USB.')+'\nUSB error: '+u.error+' · LAN error: '+t.error;
 el('usb-start').hidden=u.installed;el('resolve').hidden=c.state!=='uncertain';
 }catch{el('printer-status').dataset.tone='offline';el('printer-icon').textContent='⊘';el('connection').textContent='Mất kết nối trạm';el('connection').dataset.tone='error';el('printer').textContent='Chưa có trạng thái mới';el('printer-note').textContent='Đang thử kết nối lại…';el('wifi').textContent='Chưa có trạng thái mới';el('network').textContent='';el('cloud').textContent='Web: chưa có trạng thái mới';el('job').hidden=true;el('resolve').hidden=true;}
 if(!patch)setTimeout(refresh,1000);
}refresh();
if(typeof EventSource!=='undefined'){
 const events=new EventSource('http://__STATION_IP__:81/events?token=__CSRF__');
 events.onmessage=e=>{try{refresh(JSON.parse(e.data));}catch{}};
 events.onerror=()=>{el('feedback').textContent='Luồng trực tiếp đang nối lại; tạm đọc trạng thái mỗi giây.';};
}
document.querySelector('a[href="#settings"]').addEventListener('click',()=>{el('settings').open=true;});
</script></body></html>)HTML";
  page.replace("__CSRF__", csrf);
  page.replace("__STATION_ID__", stationID);
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
  server.on("/jobs/resolve",HTTP_POST,[]{
    if(!authorized())return;if(!validPost()){server.send(403,"text/plain","Invalid token");return;}
    auto j=jobSnapshot();if(j.active||captInUse()||cloudBusy()||!j.matches(server.arg("job_id").c_str())||strcmp(j.state,"uncertain")){server.send(409,"text/plain","Job changed or still busy");return;}
    if(!strcmp(j.source,"cloud"))cloudResolve.store(true);else jobFinish("resolved");
    server.send(202,"text/plain; charset=utf-8","Đã nhận xác nhận đối soát; không gửi lại job. Hàng đợi máy gửi vẫn giữ các lệnh chưa xác nhận.");
  });
  server.on("/jobs/queue/control",HTTP_POST,[]{
    if(!authorized())return;if(!validPost()){server.send(403,"text/plain","Invalid token");return;}
    if(!submitQueueCommand(server.arg("job_id"),server.arg("action"))){server.send(409,"text/plain; charset=utf-8","Hàng đợi chưa hỗ trợ, dữ liệu đã cũ, ID không hợp lệ hoặc có yêu cầu đang chờ.");return;}
    server.send(202,"text/plain; charset=utf-8","Đã xếp yêu cầu gửi server; chưa xác nhận đã thực hiện.");
  });
  server.on("/jobs/control",HTTP_POST,[]{
    if(!authorized())return;if(!validPost()){server.send(403,"text/plain","Invalid token");return;}
    String id=server.arg("job_id"),action=server.arg("action");
    if(!cloudIdentifier(id)||!jobCommand(id,action)){server.send(409,"text/plain; charset=utf-8","Không thực hiện: ID đã đổi, lệnh đã kết thúc, cần đối soát hoặc bộ gửi chưa hỗ trợ điều khiển.");return;}
    server.send(202,"text/plain; charset=utf-8",action=="delete"?"Đã xóa lịch sử trên trạm; không xóa tài liệu ở máy gửi/server.":"Đã nhận yêu cầu; chờ bộ gửi xác nhận. Trang đã vào máy in vẫn có thể được in.");
  });
  server.on("/printer/status", HTTP_GET, [] {
    IPAddress remote=server.client().remoteIP(),local=WiFi.localIP(),mask=WiFi.subnetMask();
    if(!WiFi.isConnected() || (uint32_t(remote)&uint32_t(mask))!=(uint32_t(local)&uint32_t(mask))){server.send(403,"text/plain","LAN only");return;}
    auto u=readUSB();auto j=jobSnapshot();server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",String("{\"schema\":1,\"job_control\":")+jobStatusJSON(j)+",\"connected\":"+(u.connected?"true":"false")+
      ",\"responsive\":"+(u.responsive?"true":"false")+",\"capt\":"+captTelemetryJSON()+
      ",\"active\":"+(j.active?"true":"false")+",\"total\":"+j.total+",\"completed\":"+j.completed+"}");
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
    String json = String("{\"status_source\":\"esp32\",\"server_queue\":")+serverQueueSnapshot()+",\"queue_command\":"+queueCommandJSON()+",\"job_control\":"+jobStatusJSON()+",\"station\":" + profileJSON() + ",\"network\":{\"ip\":" + jsonString(stationIP.toString()) + ",\"gateway\":" + jsonString(stationGateway.toString()) + ",\"mask\":" + jsonString(stationMask.toString()) + ",\"dns\":" + jsonString(stationDNS.toString()) + "},\"cloud\":" + cloudStatusJSON() + ",\"healthy\":" + (healthy ? "true" : "false") +
      ",\"connected\":" + (WiFi.status() == WL_CONNECTED ? "true" : "false") +
      ",\"connecting\":" + (connecting ? "true" : "false") +
      ",\"ssid\":" + jsonString(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("")) +
      ",\"rssi\":" + WiFi.RSSI() + ",\"static_ip\":true" +
      ",\"reset_reason\":" + int(esp_reset_reason()) +
      ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"uptime\":" + millis()/1000 +
      ",\"support_command\":" + jsonString(supportCommand) + ",\"flash\":" + ESP.getFlashChipSize() + ",\"psram\":" + ESP.getPsramSize() +
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
  if(xTaskCreate(realtimeTask,"dashboard-events",12288,nullptr,1,nullptr)!=pdPASS)Serial.println("Dashboard SSE task failed");
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
  Serial.printf("STATION: %s\nAP: ESP32-Printer-36B0\nWeb: http://192.168.4.1\nLogin: admin\n", healthy ? "PASS" : "FAIL");
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
