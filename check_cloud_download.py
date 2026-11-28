"""Compile production artifact GET receiver; no network, printer or firmware changes."""
from pathlib import Path
import subprocess,tempfile,os
s=Path('station/cloud_print.h').read_text();block=s[s.index('struct DownloadBuffer {'):s.index('// Cloud uses a reserved owner')]
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
using std::min;
struct String:std::string{using std::string::string;String(const std::string&s):std::string(s){}};
using esp_err_t=int;
const int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=257,ESP_ERR_TIMEOUT=263,ESP_ERR_HTTP_EAGAIN=0x7007,HTTP_METHOD_GET=0;
const int HTTP_EVENT_ON_DATA=1,HTTP_EVENT_HEADERS_SENT=2;
#define pdMS_TO_TICKS(x) (x)
static uint32_t tick=0;static unsigned millis(){return tick++;}static void vTaskDelay(unsigned n){tick+=n;}
struct DownloadView {unsigned attempt=0,expected=0,received=0,duration=0;int http=0,error=0;char phase[32]="idle";};
static DownloadView view;static void downloadProgress(const DownloadView &v){view=v;}
struct Fake;
struct esp_http_client_event_t{int event_id;void *user_data;Fake *client;void *data;int data_len;};
using Callback=esp_err_t(*)(esp_http_client_event_t*);
struct Scenario{int code=200;size_t bytes=1101276;bool complete=true,chunked=false,badLength=false;unsigned again=0;bool corrupt=false;};
struct Fake{Scenario scenario;Callback callback=nullptr;void *user=nullptr;};
using esp_http_client_handle_t=Fake*;
static std::vector<Scenario> scenarios;static unsigned requests=0,closes=0,cleanups=0,hashes=0;static bool allocationFail=false,hashFail=false;
static std::vector<uint8_t> reference;
static Fake *cloudClient(const String &path,const String &lease,bool asynchronous){assert(asynchronous);assert(path=="/jobs/test/data"&&lease=="private-test-lease");if(allocationFail)return nullptr;assert(requests<scenarios.size());return new Fake{scenarios[requests++]};}
static int esp_http_client_set_method(Fake*,int method){assert(method==HTTP_METHOD_GET);return 0;}
static int esp_http_client_set_event_handler(Fake*h,Callback cb){h->callback=cb;return 0;}
static int esp_http_client_set_user_data(Fake*h,void*p){h->user=p;return 0;}
static int esp_http_client_set_timeout_ms(Fake*,unsigned n){assert(n>0&&n<=15000);return 0;}
static int esp_http_client_get_status_code(Fake*h){return h->scenario.code;}
static bool esp_http_client_is_complete_data_received(Fake*h){return h->scenario.complete;}
static bool esp_http_client_is_chunked_response(Fake*h){return h->scenario.chunked;}
static int64_t esp_http_client_get_content_length(Fake*h){return h->scenario.badLength?reference.size()+1:reference.size();}
static int esp_http_client_perform(Fake*h){
 assert(h->callback&&h->user);if(h->scenario.again){--h->scenario.again;tick+=3000;return ESP_ERR_HTTP_EAGAIN;}
 esp_http_client_event_t e{HTTP_EVENT_HEADERS_SENT,h->user,h,nullptr,0};h->callback(&e);
 std::vector<uint8_t> data(h->scenario.bytes);for(size_t i=0;i<data.size();++i)data[i]=reference[i%reference.size()];if(h->scenario.corrupt&&!data.empty())data.back()^=1;
 for(size_t at=0;at<data.size();at+=997){e={HTTP_EVENT_ON_DATA,h->user,h,data.data()+at,int(std::min(size_t(997),data.size()-at))};if(h->callback(&e)!=0)return ESP_FAIL;}
 return h->scenario.complete?ESP_OK:ESP_FAIL;
}
static void esp_http_client_close(Fake*){++closes;}
static void esp_http_client_cleanup(Fake*h){++cleanups;delete h;}
static int mbedtls_sha256(const uint8_t*p,size_t n,uint8_t*out,int mode){assert(mode==0&&n==reference.size());++hashes;if(hashFail)return -1;memset(out,!memcmp(p,reference.data(),n)?0xaa:0xbb,32);return 0;}
'''
main=r'''
static void begin(std::vector<Scenario> s){scenarios=s;requests=closes=cleanups=hashes=tick=0;allocationFail=hashFail=false;}
int main(){
 reference.resize(1101276);for(size_t i=0;i<reference.size();++i)reference[i]=(i*37+i/257)&255;
 std::vector<uint8_t> buffer(reference.size()+32,0x5a);String hash(64,'a');
 auto run=[&](){bool ok=cloudDownload("test","private-test-lease",buffer.data(),reference.size(),hash);for(size_t i=reference.size();i<buffer.size();++i)assert(buffer[i]==0x5a);assert(closes==requests&&cleanups==requests);return ok;};
 begin({Scenario{}});assert(run()&&requests==1&&hashes==1&&view.received==1101276&&!strcmp(view.phase,"verified"));
 Scenario partial;partial.bytes=331488;partial.complete=false;
 begin({partial,Scenario{}});assert(run()&&requests==2&&hashes==1&&view.attempt==2);
 begin({partial,partial,partial});assert(!run()&&requests==3&&!hashes&&view.received==331488&&!strcmp(view.phase,"incomplete_body"));
 Scenario framing;framing.complete=false;begin({framing,framing,framing});assert(!run()&&requests==3&&!hashes);
 Scenario chunked;chunked.chunked=true;begin({chunked});assert(run());
 Scenario waiting;waiting.again=2;begin({waiting});assert(run()&&requests==1);
 Scenario stalled;stalled.again=1000;begin({stalled});assert(!run()&&requests==1&&!hashes&&view.error==ESP_ERR_TIMEOUT);
 Scenario denied;denied.code=403;denied.bytes=25;begin({denied});assert(!run()&&requests==1&&!hashes&&!strcmp(view.phase,"http_error"));
 Scenario unavailable;unavailable.code=503;unavailable.bytes=25;begin({unavailable,Scenario{}});assert(run()&&requests==2);
 Scenario extra;extra.bytes=1101277;begin({extra});assert(!run()&&requests==1&&!hashes&&!strcmp(view.phase,"body_overflow"));
 Scenario wrongLength;wrongLength.badLength=true;begin({wrongLength});assert(!run()&&requests==1&&!hashes&&!strcmp(view.phase,"length_mismatch"));
 Scenario corrupt;corrupt.corrupt=true;begin({corrupt});assert(!run()&&requests==1&&hashes==1&&!strcmp(view.phase,"checksum_mismatch"));
 begin({Scenario{}});hashFail=true;assert(!run()&&!strcmp(view.phase,"hash_error"));
 begin({});allocationFail=true;assert(!run()&&requests==0&&view.error==ESP_ERR_NO_MEM);
 puts("PASS: production GET streams 1101276 bytes; truncated 331488-byte retry, chunked/EAGAIN, 403/503, bounds/hash/framing and cleanup; no USB operations");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'check.cpp';p.write_text(prefix+block+main);exe=Path(tmp)/'check'
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I',str(Path.cwd()),str(p),'-o',str(exe)],check=True)
 env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1';subprocess.run([str(exe)],check=True,env=env)
