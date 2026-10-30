"""Compile production OTA validation/download against fake HTTP, SHA and flash APIs."""
from pathlib import Path
import subprocess,tempfile
s=Path('station/firmware_update.h').read_text()
policy=s[s.index('static bool validFirmwareManifest('):s.index('static esp_http_client_handle_t firmwareClient(')]
download=s[s.index('static bool firmwareDownload('):s.index('static void firmwareUpdateTask(')]
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using std::min;
static const unsigned FirmwareVersion=2026103004;
static const char *FirmwareBoard="esp32-s3-n16r8";
using esp_http_client_handle_t=void*;
const int ESP_OK=0,U_FLASH=0;
static std::vector<uint8_t> data(5000,3);
static unsigned pos=0,tick=0;
static int code=200;static int64_t contentLength=5000;
static bool failRead=false,failWrite=false,failBegin=false,failEnd=false;
static struct {
 bool ended=false,aborted=false;size_t written=0;
 bool begin(unsigned,int){written=0;ended=aborted=false;return !failBegin;}
 size_t write(uint8_t*,size_t n){written+=n;return failWrite?0:n;}
 bool end(bool){ended=true;return !failEnd;}
 void abort(){aborted=true;}
} Update;
static void *firmwareClient(const char *name){assert(!strcmp(name,"station.bin"));pos=0;return (void*)1;}
static int esp_http_client_open(void*,int){return 0;}
static int64_t esp_http_client_fetch_headers(void*){return contentLength;}
static int esp_http_client_get_status_code(void*){return code;}
static int esp_http_client_read(void*,char *b,size_t n){if(failRead)return -1;n=min(n,data.size()-pos);memcpy(b,data.data()+pos,n);pos+=n;return n;}
static void esp_http_client_close(void*){}
static void esp_http_client_cleanup(void*){}
static uint32_t millis(){return tick;}
#define pdMS_TO_TICKS(x) (x)
static void vTaskDelay(unsigned n){tick+=n;}
struct mbedtls_sha256_context{uint8_t sum=0;};
static void mbedtls_sha256_init(mbedtls_sha256_context *s){s->sum=0;}
static int mbedtls_sha256_starts(mbedtls_sha256_context*,int){return 0;}
static int mbedtls_sha256_update(mbedtls_sha256_context *s,const uint8_t*p,size_t n){while(n--)s->sum+=*p++;return 0;}
static int mbedtls_sha256_finish(mbedtls_sha256_context *s,uint8_t*p){memset(p,s->sum,32);return 0;}
static void mbedtls_sha256_free(mbedtls_sha256_context*){}
'''
main=r'''
int main(){
 std::string hash(64,'a');
 assert(validFirmwareManifest(1,FirmwareVersion+1,FirmwareBoard,5000,hash.c_str(),3145728));
 for(unsigned version:{0u,FirmwareVersion-1,FirmwareVersion})assert(!validFirmwareManifest(1,version,FirmwareBoard,5000,hash.c_str(),3145728));
 assert(!validFirmwareManifest(2,FirmwareVersion+1,FirmwareBoard,5000,hash.c_str(),3145728));
 assert(!validFirmwareManifest(1,FirmwareVersion+1,"other",5000,hash.c_str(),3145728));
 for(unsigned size:{0u,287u,3145729u})assert(!validFirmwareManifest(1,FirmwareVersion+1,FirmwareBoard,size,hash.c_str(),3145728));
 hash[0]='A';assert(!validFirmwareManifest(1,FirmwareVersion+1,FirmwareBoard,5000,hash.c_str(),3145728));
 uint8_t sum=uint8_t(5000*3);char expected[65]={};for(unsigned i=0;i<32;++i)snprintf(expected+i*2,3,"%02x",sum);
 assert(firmwareDownload(5000,expected)&&Update.ended&&!Update.aborted&&Update.written==5000);
 assert(!firmwareDownload(5000,std::string(64,'0').c_str())&&!Update.ended&&Update.aborted);
 failWrite=true;assert(!firmwareDownload(5000,expected)&&Update.aborted&&!Update.ended);failWrite=false;
 failRead=true;assert(!firmwareDownload(5000,expected)&&Update.aborted&&!Update.ended);failRead=false;
 data.resize(4000);assert(!firmwareDownload(5000,expected)&&Update.aborted&&!Update.ended);data.resize(5000,3);
 failBegin=true;assert(!firmwareDownload(5000,expected)&&!Update.ended);failBegin=false;
 failEnd=true;assert(!firmwareDownload(5000,expected));failEnd=false;
 contentLength=4999;assert(!firmwareDownload(5000,expected));contentLength=5000;
 code=302;assert(!firmwareDownload(5000,expected));
 puts("PASS: production OTA board/version/size/hash validation; streamed flash, hash mismatch, truncation, HTTP and flash failures");
}
'''
assert 'cfg.crt_bundle_attach=esp_crt_bundle_attach' in s and 'cfg.disable_auto_redirect=true' in s
assert 'firmwareCheckPending.load() && captFresh()' in Path('station/cloud_print.h').read_text()
assert '!firmwareCheckPending.load()&&claimCapt(1)' in Path('station/capt_tcp.h').read_text()
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'check.cpp';p.write_text(prefix+policy+download+main);out=Path(tmp)/'check'
 subprocess.run(['/Library/Developer/CommandLineTools/usr/bin/clang++','-std=c++11','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk',str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
