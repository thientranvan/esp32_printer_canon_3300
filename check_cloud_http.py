"""Compile production cloudPost against deterministic HTTP fakes. python3 check_cloud_http.py"""
from pathlib import Path
import tempfile, subprocess, sys, shutil
source=Path('station/cloud_print.h').read_text()
production=source[source.index('struct CloudResponse'):source.index('static String cloudBoot;')]
harness=r'''
#include <string>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
struct String:std::string {using std::string::string;bool concat(const char*p,int n){append(p,n);return true;}};
using esp_err_t=int;
#define ESP_OK 0
#define ESP_FAIL -1
#define HTTP_METHOD_POST 1
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
enum {HTTP_EVENT_ON_CONNECTED,HTTP_EVENT_HEADERS_SENT,HTTP_EVENT_ON_HEADER,HTTP_EVENT_ON_DATA,HTTP_EVENT_ON_FINISH};
struct esp_http_client_event_t {int event_id;void *user_data;void *data;int data_len;};
using Callback=esp_err_t(*)(esp_http_client_event_t*);
struct Fake {int code=204,performs=0,closes=0;bool fail=false,truncated=false,setupFail=false;
 std::string body,posted;const char *post=nullptr;int postLen=0;void *user=nullptr;Callback callback=nullptr;};
using esp_http_client_handle_t=Fake*;
static Fake foreground;
static bool allocationFail=false;
static auto cloudClient(const String&,const String&)->Fake* {return allocationFail?nullptr:&foreground;}
static int esp_http_client_get_errno(Fake*){return 11;}
static struct {unsigned getFreeHeap(){return 123456;}} ESP;
static uint32_t tick=0;
static uint32_t millis(){return ++tick;}
static int cloudLock;
static struct {int http=0,heartbeatHttp=0,claimHttp=0,eventHttp=0;unsigned heartbeatDurationMs=0,heartbeatFailures=0;char heartbeatDiagnostic[128]={};} cloudView;
static int esp_http_client_set_method(Fake*h,int){return h->setupFail?-1:0;}
static int esp_http_client_set_header(Fake*,const char*,const char*){return 0;}
static int esp_http_client_set_event_handler(Fake*h,Callback c){h->callback=c;return 0;}
static int esp_http_client_set_user_data(Fake*h,void*p){h->user=p;return 0;}
static int esp_http_client_set_post_field(Fake*h,const char*p,int n){h->post=p;h->postLen=n;return 0;}
static int esp_http_client_get_status_code(Fake*h){return h->code;}
static bool esp_http_client_is_complete_data_received(Fake*h){return !h->truncated;}
static int esp_http_client_perform(Fake*h){
 ++h->performs;assert(h->callback&&h->user&&h->post);
 h->posted.assign(h->post,h->postLen);
 auto event=[&](int id,void*p=nullptr,int n=0){esp_http_client_event_t e{id,h->user,p,n};return h->callback(&e);};
 event(HTTP_EVENT_HEADERS_SENT);if(h->fail)return -7;
 event(HTTP_EVENT_ON_HEADER);
 if(h->code!=204)for(size_t at=0;at<h->body.size();at+=17)
  if(event(HTTP_EVENT_ON_DATA,&h->body[at],std::min(size_t(17),h->body.size()-at))!=0)return -1;
 if(!h->truncated)event(HTTP_EVENT_ON_FINISH);
 return 0;
}
static void esp_http_client_close(Fake*h){++h->closes;}
static void esp_http_client_cleanup(Fake*){}
'''
main=r'''
int main(){
 String out="stale";
 assert(cloudPost("/jobs/claim","{}",out)==204&&out.empty());
 assert(foreground.posted=="{}"&&foreground.performs==1&&!foreground.user&&!foreground.callback&&!foreground.post);
 assert(cloudView.claimHttp==204&&cloudView.http==204);
 Fake heartbeat;heartbeat.code=200;heartbeat.body="{}";
 String payload(2052,'x');
 assert(cloudPost("/heartbeat",payload,out,"",&heartbeat)==200&&out=="{}");
 assert(heartbeat.posted==payload&&heartbeat.closes==0&&cloudView.heartbeatFailures==0);
 assert(!heartbeat.user&&!heartbeat.post&&!heartbeat.callback);
 foreground=Fake();foreground.code=503;foreground.body="busy";
 assert(cloudPost("/jobs/claim","{}",out)==503&&cloudView.claimHttp==503);
 heartbeat.fail=true;String detail;
 assert(cloudPost("/heartbeat",payload,out,"",&heartbeat,&detail)==-1&&out.empty());
 assert(heartbeat.performs==2&&heartbeat.closes==1&&cloudView.heartbeatFailures==1);
 assert(detail.find("phase=headers_sent")!=String::npos&&strlen(cloudView.heartbeatDiagnostic)>0);
 assert(cloudView.claimHttp==503&&cloudView.http==503);
 heartbeat.fail=false;assert(cloudPost("/heartbeat",payload,out,"",&heartbeat)==200&&out=="{}");
 assert(cloudView.heartbeatFailures==0&&heartbeat.performs==3);
 foreground=Fake();foreground.code=200;foreground.body=std::string(8192,'x');
 assert(cloudPost("/jobs/claim","{}",out)==200&&out.size()==8192);
 foreground.body+='x';
 assert(cloudPost("/jobs/claim","{}",out)==-1&&out.empty());
 foreground=Fake();foreground.code=200;foreground.body="partial";foreground.truncated=true;
 assert(cloudPost("/jobs/claim","{}",out)==-1&&out.empty());
 foreground=Fake();foreground.setupFail=true;
 assert(cloudPost("/jobs/claim","{}",out)==-1&&foreground.performs==0);
 allocationFail=true;assert(cloudPost("/jobs/claim","{}",out)==-1&&cloudView.claimHttp==-1);allocationFail=false;
 foreground=Fake();assert(cloudPost("/jobs/test/events","{}",out)==204&&cloudView.eventHttp==204);
 puts("PASS: native perform, 2052-byte POST, fragmented response, 204/503, complete framing, bounded body, failed-stream close, no POST replay, diagnostics and independent endpoint codes");
}
'''
assert 'else cloudState("idle");' not in source
assert 'else if(code==204)cloudState("idle");' in source
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.cpp';p.write_text(harness+production+main)
 compiler=shutil.which('clang++') or shutil.which('g++');flags=['-std=c++11']
 if sys.platform=='darwin':
  compiler='/Library/Developer/CommandLineTools/usr/bin/clang++';flags+=['-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk']
 subprocess.run([compiler,*flags,str(p),'-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
