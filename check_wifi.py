"""Run saved-Wi-Fi retry policy against fake radio: python3 check_wifi.py."""
from pathlib import Path
import subprocess
import tempfile

source = Path('station/station.ino').read_text()
policy = source.split('  if (!connecting) {', 1)[1].split('  updateStatusLED();', 1)[0]
program = r'''
#include <cassert>
#include <cstdint>
constexpr int WL_CONNECTED=3;
uint32_t now=0, savedWiFiAttemptAt=0, savedWiFiRetryAt=0;
bool savedWiFiAttempt=false, wifiScanRunning=false,trialBusy=false;
uint32_t wifiTrialUntil=0;
struct {unsigned load(){return 0;}} captTCPState;
bool cloudBusy(){return trialBusy;}
struct Job {bool active=false;};Job jobSnapshot(){return {};}
int retries=0;
uint32_t millis(){return now;}
struct Radio {
 int state=0,clients=0,stops=0;
 int status(){return state;}
 int softAPgetStationNum(){return clients;}
 void disconnect(){++stops;}
} WiFi;
struct Log {void println(const char*){}} Serial;
void connectSaved(){++retries;savedWiFiRetryAt=now;}
void tick(){
''' + policy + r'''
int main(){
 now=30000;WiFi.clients=1;tick();assert(retries==0);
 WiFi.clients=0;wifiScanRunning=true;tick();assert(retries==0);
 wifiScanRunning=false;tick();assert(retries==1);
 savedWiFiAttempt=true;savedWiFiAttemptAt=now;now+=14999;tick();assert(WiFi.stops==0);
 now++;tick();assert(WiFi.stops==1&&!savedWiFiAttempt);
 now+=29999;tick();assert(retries==1);now++;tick();assert(retries==2);
 savedWiFiAttempt=true;savedWiFiAttemptAt=now;WiFi.clients=1;tick();assert(WiFi.stops==2);
 savedWiFiAttempt=true;WiFi.state=WL_CONNECTED;tick();assert(!savedWiFiAttempt&&WiFi.stops==2);
 WiFi.state=0;WiFi.clients=0;savedWiFiRetryAt=UINT32_MAX-10000;now=20000;tick();assert(retries==3);
 WiFi.state=WL_CONNECTED;wifiTrialUntil=30000;now=29999;tick();assert(WiFi.stops==2);
 now=30000;trialBusy=true;tick();assert(WiFi.stops==2&&wifiTrialUntil);
 trialBusy=false;tick();assert(WiFi.stops==3&&retries==4&&!wifiTrialUntil);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'wifi.cpp';cpp.write_text(program)
    binary=Path(tmp)/'check'
    subprocess.run(['/Library/Developer/CommandLineTools/usr/bin/clang++', '-std=c++11', '-isysroot', '/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Wi-Fi retry policy PASS')

# Run actual saved-record loader and native IP selection, including old NVS records.
loader=source[source.index('static bool configureStationIP('):source.index('bool authorized()')]
config_decl=source[source.index('struct WiFiConfig {'):source.index('struct WiFiConfig {')+source[source.index('struct WiFiConfig {'):].index('; };')+4]
harness=r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string>
struct IPAddress {unsigned value=0;};
IPAddress stationIP{200},stationGateway{1},stationMask{24},stationDNS{1};
bool wifiDHCP=false,savedWiFiAttempt=false;
unsigned savedWiFiRetryAt=0,savedWiFiAttemptAt=0;
unsigned millis(){return 123;}
struct Radio {int clients=0,starts=0;bool reject=false;unsigned ip=999;
 int softAPgetStationNum(){return clients;}
 bool config(IPAddress local,IPAddress,IPAddress,IPAddress={}){ip=local.value;return !reject;}
 void begin(const char *ssid,const char *password){assert(std::string(ssid)=="test"&&std::string(password)=="testpass");++starts;}
} WiFi;
struct Store {std::vector<uint8_t> bytes;
 size_t getBytesLength(const char*){return bytes.size();}
 size_t getBytes(const char*,void*out,size_t n){assert(n<=bytes.size());memcpy(out,bytes.data(),n);return n;}
} prefs;
'''
main=r'''
int main(){
 static_assert(sizeof(WiFiConfig)==98,"NVS layout changed");
 WiFiConfig c={};strcpy(c.ssid,"test");strcpy(c.password,"testpass");
 auto save=[&](size_t n){prefs.bytes.assign((uint8_t*)&c,(uint8_t*)&c+n);};
 save(97);connectSaved();assert(WiFi.starts==1&&WiFi.ip==200&&!wifiDHCP);
 c.dhcp=1;save(98);connectSaved();assert(WiFi.starts==2&&WiFi.ip==0&&wifiDHCP);
 // Failed new-network attempt restores saved static settings through connectSaved.
 c.dhcp=0;save(98);connectSaved();assert(WiFi.starts==3&&WiFi.ip==200&&!wifiDHCP);
 prefs.bytes.resize(99);connectSaved();assert(WiFi.starts==3);
 c.dhcp=2;save(98);connectSaved();assert(WiFi.starts==3);
 c.dhcp=0;memset(c.ssid,'x',sizeof(c.ssid));save(98);connectSaved();assert(WiFi.starts==3);
 strcpy(c.ssid,"test");save(98);WiFi.reject=true;connectSaved();assert(WiFi.starts==3);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'dhcp.cpp';cpp.write_text(harness+config_decl+loader+main)
    binary=Path(tmp)/'dhcp'
    subprocess.run(['/Library/Developer/CommandLineTools/usr/bin/clang++','-std=c++14','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: production static/DHCP selection, legacy NVS migration, rollback, malformed records')
