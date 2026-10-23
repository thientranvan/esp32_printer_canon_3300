"""Compile production cloudPost against deterministic HTTP fakes. python3 check_cloud_http.py"""
from pathlib import Path
import tempfile, subprocess, sys, shutil
source=Path('station/cloud_print.h').read_text()
production=source[source.index('static int cloudPost('):source.index('static String cloudBoot;')]
harness=r'''
#include <string>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
struct String:std::string {using std::string::string;void concat(const char*p,int n){append(p,n);}};
struct Fake {int code=204,reads=0;bool fail=false,truncated=false;std::string body;size_t at=0;};
using esp_http_client_handle_t=Fake*;
static Fake foreground;
static bool allocationFail=false;
static auto cloudClient(const String&,const String&)->Fake* {return allocationFail?nullptr:&foreground;}
static int esp_http_client_get_errno(Fake*){return 0;}
static struct {unsigned getFreeHeap(){return 123456;}} ESP;
static int cloudLock;
static struct {int http=0,heartbeatHttp=0,claimHttp=0,eventHttp=0;} cloudView;
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
#define HTTP_METHOD_POST 1
#define ESP_OK 0
static void esp_http_client_set_method(Fake*,int){}
static void esp_http_client_set_header(Fake*,const char*,const char*){}
static int esp_http_client_open(Fake*h,size_t){return h->fail?-1:0;}
static int esp_http_client_write(Fake*,const char*,size_t n){return n;}
static int esp_http_client_fetch_headers(Fake*){return 0;}
static int esp_http_client_get_status_code(Fake*h){return h->code;}
static int esp_http_client_read(Fake*h,char*p,size_t size){
 ++h->reads;if(h->code==204)return -1; // Reading nonexistent body reproduces old failure.
 size_t n=std::min(size,h->body.size()-h->at);memcpy(p,h->body.data()+h->at,n);h->at+=n;
 return n?int(n):h->truncated?-1:0;
}
static void esp_http_client_close(Fake*){}
static void esp_http_client_cleanup(Fake*){}
'''
main=r'''
int main(){
 String out="stale";
 assert(cloudPost("/jobs/claim","{}",out)==204 && out.empty() && foreground.reads==0);
 assert(cloudView.claimHttp==204 && cloudView.http==204);
 Fake heartbeat;heartbeat.code=200;heartbeat.body="{}";
 assert(cloudPost("/heartbeat","{}",out,"",&heartbeat)==200 && out=="{}");
 assert(cloudView.heartbeatHttp==200 && cloudView.claimHttp==204 && cloudView.http==204);
 foreground=Fake();foreground.code=503;foreground.body="busy";
 assert(cloudPost("/jobs/claim","{}",out)==503 && cloudView.claimHttp==503);
 heartbeat.at=0;
 assert(cloudPost("/heartbeat","{}",out,"",&heartbeat)==200);
 assert(cloudView.claimHttp==503 && cloudView.http==503);
 foreground=Fake();foreground.code=200;foreground.body="{\"id\":\"test\"}";
 assert(cloudPost("/jobs/claim","{}",out)==200 && out==foreground.body.c_str());
 foreground=Fake();foreground.code=200;foreground.body=std::string(8193,'x');
 assert(cloudPost("/jobs/claim","{}",out)==-1);
 foreground=Fake();foreground.code=200;foreground.body="partial";foreground.truncated=true;
 assert(cloudPost("/jobs/claim","{}",out)==-1);
 allocationFail=true;assert(cloudPost("/jobs/claim","{}",out)==-1 && cloudView.claimHttp==-1 && out.empty());allocationFail=false;
 foreground=Fake();foreground.fail=true;String detail;assert(cloudPost("/jobs/claim","{}",out,"",nullptr,&detail)==-1 && detail.find("open=")!=String::npos);
 foreground=Fake();assert(cloudPost("/jobs/test/events","{}",out)==204 && cloudView.eventHttp==204 && foreground.reads==0);
 puts("PASS: no-body 204, independent endpoint codes, 200/503, bounded/truncated bodies and allocation failure");
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
