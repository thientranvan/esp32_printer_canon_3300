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
bool savedWiFiAttempt=false, wifiScanRunning=false;
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
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'wifi.cpp';cpp.write_text(program)
    binary=Path(tmp)/'check'
    subprocess.run(['/Library/Developer/CommandLineTools/usr/bin/clang++', '-std=c++11', '-isysroot', '/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Wi-Fi retry policy PASS')
