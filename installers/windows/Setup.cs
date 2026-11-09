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
    internal static string RunnerAll() {
        return "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\r\n$ErrorActionPreference='Stop'\r\ntry { & (Join-Path $PSScriptRoot 'setup.ps1') -AllStations; exit 0 } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }\r\n";
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
    readonly Button install=new Button(); readonly TextBox log=new TextBox(); bool busy; Icon ownedIcon;
    public SetupWindow(){
        Text="Cài Máy 1 · Máy 2 · Máy 3";ClientSize=new Size(720,430);MinimumSize=new Size(600,360);
        StartPosition=FormStartPosition.CenterScreen;Font=new Font("Segoe UI",10);AutoScaleMode=AutoScaleMode.Dpi;
        ownedIcon=Icon.ExtractAssociatedIcon(Assembly.GetExecutingAssembly().Location);Icon=ownedIcon;
        var layout=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(18),ColumnCount=1,RowCount=4};
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute,40));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,58));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute,46));layout.RowStyles.Add(new RowStyle(SizeType.Percent,100));Controls.Add(layout);
        layout.Controls.Add(new Label{Text="Một lần cài, đủ ba máy in",AutoSize=true,Font=new Font(Font,FontStyle.Bold)},0,0);
        layout.Controls.Add(new Label{Text="Máy 1 · Máy 2 · Máy 3\nKhông cần cùng LAN. Driver tự cài nếu thiếu; server chuyển bản in đã render.",AutoSize=true},0,1);
        install.Text="Cài đặt / Cập nhật cả 3 máy";install.AutoSize=true;layout.Controls.Add(install,0,2);
        log.Multiline=true;log.ReadOnly=true;log.ScrollBars=ScrollBars.Vertical;log.Dock=DockStyle.Fill;layout.Controls.Add(log,0,3);
        install.Click+=async(s,e)=>await Install();
        FormClosing+=(s,e)=>{if(busy){e.Cancel=true;MessageBox.Show(this,"Chờ cài đặt hoàn tất.",Text);}};
    }
    void Append(string value){if(value!=null&&!IsDisposed)BeginInvoke((Action)(()=>log.AppendText(value+Environment.NewLine)));}
    async Task Install(){
        string folder=null;busy=true;install.Enabled=false;log.Clear();
        try{
            var security = new DirectorySecurity(); security.SetAccessRuleProtection(true, false);
            foreach (var sid in new[] { WellKnownSidType.LocalSystemSid, WellKnownSidType.BuiltinAdministratorsSid })
                security.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(sid, null), FileSystemRights.FullControl, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
            folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "ESP32PrinterSetup-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(folder, security);
            using (var input = Assembly.GetExecutingAssembly().GetManifestResourceStream("setup.ps1")) using (var output = File.Create(Path.Combine(folder, "setup.ps1"))) input.CopyTo(output);

            File.WriteAllText(Path.Combine(folder, "runner.ps1"), Program.RunnerAll(), new UTF8Encoding(true));
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
            if(code!=0)throw new Exception("Chưa cài đủ ba máy. Xem thông báo và chạy lại; không tự gửi lại lệnh in.");
            MessageBox.Show(this,"Đã cài Máy 1, Máy 2, Máy 3. Chọn máy trong Word/PDF để in.",Text);
        }catch(Exception error){Append(error.Message);MessageBox.Show(this,error.Message,Text,MessageBoxButtons.OK,MessageBoxIcon.Error);}
        finally{if(folder!=null)try{Directory.Delete(folder,true);}catch(IOException){}busy=false;install.Enabled=true;}
    }
    protected override void Dispose(bool disposing){if(disposing&&ownedIcon!=null)ownedIcon.Dispose();base.Dispose(disposing);}
}
