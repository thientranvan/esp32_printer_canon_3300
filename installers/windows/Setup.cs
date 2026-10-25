using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Net;
using System.Reflection;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

[DataContract] sealed class StationProfile {
    [DataMember] public int schema;
    [DataMember] public string model;
    [DataMember] public string id;
    [DataMember] public string name;
    [DataMember] public string ip;
}
static class Program {
    internal static bool ValidIP(string value) {
        IPAddress ip;
        return System.Text.RegularExpressions.Regex.IsMatch(value, @"\A(?:[0-9]{1,3}\.){3}[0-9]{1,3}\z") &&
            IPAddress.TryParse(value, out ip) && ip.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork && ip.ToString() == value;
    }
    internal static bool ValidName(string value) {
        return !String.IsNullOrWhiteSpace(value) && value.Length <= 64 &&
            !System.Text.RegularExpressions.Regex.IsMatch(value, "[\\x00-\\x1f\\x7f\\\\/\";]");
    }
    internal static StationProfile ParseProfile(byte[] data, string ip) {
        if (data.Length > 4096) throw new Exception("Thông tin trạm quá lớn.");
        StationProfile p;
        using (var stream = new MemoryStream(data)) p = (StationProfile)new DataContractJsonSerializer(typeof(StationProfile)).ReadObject(stream);
        if (p == null || p.schema != 1 || p.model != "LBP3300" || p.ip != ip || p.id == null ||
            !System.Text.RegularExpressions.Regex.IsMatch(p.id, @"\Aesp32-[0-9a-f]{12}\z") || p.id == "esp32-000000000000" || !ValidName(p.name))
            throw new Exception("Địa chỉ này không phải trạm ESP32 LBP3300 hợp lệ.");
        return p;
    }
    internal static StationProfile ReadProfile(string ip) {
        if (!ValidIP(ip)) throw new Exception("Địa chỉ IPv4 không hợp lệ.");
        var request = (HttpWebRequest)WebRequest.Create("http://" + ip + "/setup/profile.json");
        request.Proxy = null; request.Timeout = 5000; request.ReadWriteTimeout = 5000; request.AllowAutoRedirect = false;
        using (var response = (HttpWebResponse)request.GetResponse()) {
            if (response.StatusCode != HttpStatusCode.OK) throw new Exception("Trạm không trả thông tin cài đặt.");
            using (var input = response.GetResponseStream()) using (var data = new MemoryStream()) {
                byte[] buffer = new byte[1024]; int n;
                while ((n = input.Read(buffer, 0, buffer.Length)) > 0) {
                    data.Write(buffer, 0, n); if (data.Length > 4096) throw new Exception("Thông tin trạm quá lớn.");
                }
                return ParseProfile(data.ToArray(), ip);
            }
        }
    }
    internal static string Runner(string ip, string name) {
        if (!ValidIP(ip) || !ValidName(name)) throw new Exception("Tên hoặc IP không hợp lệ.");
        Func<string, string> encode = value => Convert.ToBase64String(Encoding.UTF8.GetBytes(value));
        return "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\r\n$ErrorActionPreference='Stop'\r\ntry {\r\n" +
            "& (Join-Path $PSScriptRoot 'setup.ps1') -StationIP ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + encode(ip) +
            "'))) -PrinterName ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" + encode(name) +
            "')))\r\nexit 0\r\n} catch { [Console]::Error.WriteLine($_.ToString()); exit 1 }\r\n";
    }
    [STAThread] static int Main(string[] args) {
        if (args.Length == 1 && args[0] == "--self-test") {
            if (!ValidIP("192.168.1.200") || ValidIP("127.1") || ValidIP("256.1.1.1") || ValidIP("192.168.1.2;whoami") ||
                !ValidName("Máy 2") || ValidName("bad\nname") || ValidName("bad\"name")) throw new Exception("Validation failed");
            var profile = ParseProfile(Encoding.UTF8.GetBytes("{\"schema\":1,\"model\":\"LBP3300\",\"id\":\"esp32-1051db78a8fc\",\"name\":\"Máy 2\",\"ip\":\"192.168.1.200\"}"), "192.168.1.200");
            if (profile.name != "Máy 2" || Runner("192.168.1.200", "Máy 2").Contains("-PrinterName 'Máy")) throw new Exception("Profile/runner failed");
            using (var resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("setup.ps1")) {
                if (resource == null || resource.Length < 10000) throw new Exception("Installer resource missing");
            }
            Console.WriteLine("PASS: IPv4/name validation, station profile, safe runner and embedded installer"); return 0;
        }
        if (Environment.OSVersion.Platform != PlatformID.Win32NT || !Environment.Is64BitProcess || Environment.GetEnvironmentVariable("PROCESSOR_ARCHITECTURE") != "AMD64") {
            MessageBox.Show("Cần Windows 10/11 x64. Chưa hỗ trợ Windows ARM64.", "Trạm in ESP32"); return 1;
        }
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new SetupWindow()); return 0;
    }
}
sealed class SetupWindow : Form {
    readonly ComboBox ip = new ComboBox(); readonly TextBox name = new TextBox();
    readonly Button read = new Button(); readonly Button install = new Button();
    readonly Label station = new Label(); readonly TextBox log = new TextBox();
    StationProfile selected; bool busy; Icon ownedIcon;
    public SetupWindow() {
        Text = "Cài máy in ESP32 — Canon LBP3300"; ClientSize = new Size(640, 490);
        MinimumSize = new Size(580, 480); StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 10); AutoScaleMode = AutoScaleMode.Dpi;
        ownedIcon = Icon.ExtractAssociatedIcon(Assembly.GetExecutingAssembly().Location); Icon = ownedIcon;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(18), ColumnCount = 2, RowCount = 7 };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 135)); layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 46)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 44)); layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 30)); Controls.Add(layout);
        var heading = new Label { Text = "Chọn trạm trong LAN, đặt tên rồi bấm Cài đặt.", AutoSize = true, Dock = DockStyle.Fill };
        layout.Controls.Add(heading, 0, 0); layout.SetColumnSpan(heading, 2);
        layout.Controls.Add(new Label { Text = "IP trạm in", AutoSize = true }, 0, 1);
        ip.Items.AddRange(new object[] { "192.168.1.199", "192.168.1.200", "192.168.1.201" }); ip.Dock = DockStyle.Fill; ip.Text = "192.168.1.199";
        layout.Controls.Add(ip, 1, 1); layout.Controls.Add(new Label { Text = "Tên máy in", AutoSize = true }, 0, 2);
        name.Dock = DockStyle.Fill; name.MaxLength = 64; layout.Controls.Add(name, 1, 2);
        station.Dock = DockStyle.Fill; station.Text = "Bấm Đọc trạm để nhận đúng tên và ID."; station.AutoEllipsis = true;
        layout.Controls.Add(station, 0, 3); layout.SetColumnSpan(station, 2);
        var actions = new FlowLayoutPanel { Dock = DockStyle.Fill };
        read.Text = "Đọc trạm"; read.AutoSize = true; install.Text = "Cài đặt / Cập nhật"; install.AutoSize = true; install.Enabled = false;
        actions.Controls.Add(read); actions.Controls.Add(install); layout.Controls.Add(actions, 0, 4); layout.SetColumnSpan(actions, 2);
        log.Multiline = true; log.ReadOnly = true; log.ScrollBars = ScrollBars.Vertical; log.Dock = DockStyle.Fill;
        layout.Controls.Add(log, 0, 5); layout.SetColumnSpan(log, 2);
        var note = new Label { Text = "Driver Canon tự tải nếu thiếu. Cài lại theo ID, không tạo máy trùng.", AutoSize = true, Dock = DockStyle.Fill };
        layout.Controls.Add(note, 0, 6); layout.SetColumnSpan(note, 2);
        ip.TextChanged += (s, e) => { selected = null; install.Enabled = false; station.Text = "Bấm Đọc trạm để kiểm tra IP vừa chọn."; };
        read.Click += async (s, e) => await LoadStation();
        install.Click += async (s, e) => await Install();
        Shown += async (s, e) => await LoadStation();
        FormClosing += (s, e) => { if (busy) { e.Cancel = true; MessageBox.Show(this, "Đang xử lý. Chờ hoàn tất để tránh ngắt cài đặt.", Text); } };
    }
    void SetBusy(bool value) { busy = value; read.Enabled = ip.Enabled = name.Enabled = !value; install.Enabled = !value && selected != null; }
    void Append(string value) { if (value != null && !IsDisposed) BeginInvoke((Action)(() => { log.AppendText(value + Environment.NewLine); })); }
    async Task LoadStation() {
        string host = ip.Text.Trim(); SetBusy(true); selected = null;
        try { selected = await Task.Run(() => Program.ReadProfile(host)); name.Text = selected.name; station.Text = selected.name + " · " + selected.id + " · " + host; }
        catch (Exception error) { station.Text = "Không đọc được trạm. Kiểm tra IP, nguồn ESP và mạng LAN."; MessageBox.Show(this, error.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning); }
        finally { SetBusy(false); }
    }
    async Task Install() {
        if (selected == null || selected.ip != ip.Text.Trim() || !Program.ValidName(name.Text.Trim())) { MessageBox.Show(this, "Kiểm tra IP và tên máy in.", Text); return; }
        string host = selected.ip, printerName = name.Text.Trim(), folder = null; SetBusy(true); log.Clear();
        try {
            var security = new DirectorySecurity(); security.SetAccessRuleProtection(true, false);
            foreach (var sid in new[] { WellKnownSidType.LocalSystemSid, WellKnownSidType.BuiltinAdministratorsSid })
                security.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(sid, null), FileSystemRights.FullControl, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
            folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "ESP32PrinterSetup-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(folder, security);
            using (var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("setup.ps1")) using (var output = File.Create(Path.Combine(folder, "setup.ps1"))) input.CopyTo(output);
            File.WriteAllText(Path.Combine(folder, "runner.ps1"), Program.Runner(host, printerName), new UTF8Encoding(true));
            var start = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), @"WindowsPowerShell\v1.0\powershell.exe"),
                "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + Path.Combine(folder, "runner.ps1") + "\"") {
                UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
            };
            int code = await Task.Run(() => {
                using (var process = new Process { StartInfo = start }) {
                    process.OutputDataReceived += (s, e) => Append(e.Data); process.ErrorDataReceived += (s, e) => Append(e.Data);
                    process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine(); process.WaitForExit(); return process.ExitCode;
                }
            });
            if (code != 0) throw new Exception("Cài đặt chưa hoàn tất. Xem lỗi trong khung phía trên; không tự gửi lại lệnh in.");
            station.Text = "Đã cài / cập nhật: " + printerName + " · " + host;
            MessageBox.Show(this, "Đã cài máy in. Mở ứng dụng và chọn “" + printerName + "”.", Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
        } catch (Exception error) { Append(error.Message); MessageBox.Show(this, error.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error); }
        finally {
            if (folder != null) try { Directory.Delete(folder, true); } catch (IOException) { Append("File tạm còn tại " + folder); }
            SetBusy(false);
        }
    }
    protected override void Dispose(bool disposing) { if (disposing && ownedIcon != null) ownedIcon.Dispose(); base.Dispose(disposing); }
}
