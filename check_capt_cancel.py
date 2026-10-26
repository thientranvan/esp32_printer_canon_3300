"""Compile production CAPT cancellation against a fake printer; no hardware needed."""
from pathlib import Path
import subprocess
import tempfile
source=Path('station/cloud_print.h').read_text()
engine=source[source.index('static bool cancelCapt('):source.index('static PrintRecovery::Checkpoint cloudRecovery;')]
harness=r'''
#include "station/job_control.h"
#include "station/prepared_job.h"
#include <atomic>
#include <cassert>
#include <vector>
#include <cstring>
#include <cstdio>
static bool cancelCapt(uint32_t owner,bool report=true);
static PrintControl::Job currentJob;
static int jobLock=0;
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
#define pdMS_TO_TICKS(x) (x)
static std::atomic<unsigned> printerEpoch{1};
static PrintControl::Job jobSnapshot(){return currentJob;}
static bool claimCapt(unsigned owner){assert(owner==42);return true;}
static struct USB{bool connected=true;} usb;
static USB readUSB(){return usb;}
static void vTaskDelay(unsigned){}
static void cloudPut16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void cloudPut32(uint8_t *p,unsigned n){cloudPut16(p,n);cloudPut16(p+2,n>>16);}
static unsigned failOp=0;
static std::vector<unsigned> ops;
static bool captExchange(uint8_t *out,size_t cap,unsigned owner,unsigned op,const uint8_t *p=nullptr,size_t n=0){
 assert(cap==80&&owner==42);ops.push_back(op);memset(out,0,cap);cloudPut16(out+2,op==0xA0A8?60:6);
 if(op==0xE0A6)assert(n==2&&p[0]==1&&p[1]==0);
 if(op==0xE0A5)assert(n==16&&Prepared::u32(p)==0xADEADBEE&&Prepared::u16(p+4)==0);
 if(op==0xE1A1)assert(n==72&&p[16]==4&&Prepared::u16(p+18)==7);
 if(op==0xE0A9)assert(n==2&&Prepared::u16(p)==7);
 return op!=failOp;
}
'''
main=r'''
static void begin(){currentJob.begin("job","cloud",true);currentJob.printerSession=7;currentJob.sessionEpoch=1;currentJob.completed=2;ops.clear();}
int main(){
 begin();assert(cancelCapt(42));assert(!strcmp(currentJob.printerCancel,"cleared")&&currentJob.completed==2);
 assert((ops==std::vector<unsigned>{0xE0A6,0xE0A3,0xE0A2,0xE0A4,0xE0A5,0xE0A6,0xE1A1,0xE0A9,0xA0A8}));
 for(unsigned op:ops){begin();failOp=op;assert(!cancelCapt(42));assert(!strcmp(currentJob.printerCancel,"unverified"));assert(currentJob.completed==2);assert(std::find(ops.begin(),ops.end(),0xE0A9)!=ops.end());}
 failOp=0;begin();printerEpoch=2;assert(!cancelCapt(42)&&ops.empty());printerEpoch=1;
 begin();usb.connected=false;assert(!cancelCapt(42)&&ops.empty());usb.connected=true;
 begin();currentJob.printerSession=0;assert(cancelCapt(42)&&ops.empty()&&!strcmp(currentJob.printerCancel,"not_started"));
 currentJob.finish("uncertain");assert(!cancelCapt(42)&&ops.empty()&&!strcmp(currentJob.printerCancel,"unverified"));
 puts("PASS: production Canon cancel/discard/release, empty-session reset, ACK failures, epoch/disconnect, no replay or progress inflation");
}
'''
# Iterate a copy: each test replaces the captured command trace.
main=main.replace('for(unsigned op:ops){','auto expected=ops;for(unsigned op:expected){')
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder)/'cancel.cpp';p.write_text(harness.replace('#include <vector>','#include <vector>\n#include <algorithm>')+engine+main)
 out=Path(folder)/'cancel'
 subprocess.run(['/Library/Developer/CommandLineTools/usr/bin/clang++','-std=c++11','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk','-I',str(Path.cwd()),str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
