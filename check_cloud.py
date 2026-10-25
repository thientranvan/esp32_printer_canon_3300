"""Run host-side CAPT engine simulation: python3 check_cloud.py (requires C++11 compiler)."""
from pathlib import Path
import subprocess
import tempfile
import shutil
import sys
from prepare_cloud_job import self_test

self_test()
source = Path('station/cloud_print.h').read_text()
assert 'static const char *cloudOrigin="https://quanly.lophocbinhminh.com";' in source, 'Wrong production cloud origin'
assert 'quanly.lophochinhminh.com' not in Path('station/station.ino').read_text(), 'Stale cloud domain in device UI'
# Compile the production engine unchanged, replacing USB/network calls with a fake printer.
engine = source[source.index('static bool cloudPrint('):source.index('static void cloudTask(')]
harness = r'''
#include "station/prepared_job.h"
#include "station/capt_status.h"
#include "station/print_recovery.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
using std::min;
static unsigned decoded=0, started=0, submitted=0, completed=0, reservations=0, releases=0;
static bool rejectPaper=false;
static bool cloudTracking=false;static PrintRecovery::Checkpoint cloudRecovery;
static uint8_t cloudBuffer[4096];
static int cloudLock=0;
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
static struct { unsigned submitted; } cloudView;
static std::vector<int> flags;
static void cloudHeartbeat(){}
static bool cloudCheckpoint(){return true;}
static void cloudPut16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void cloudPut32(uint8_t *p,uint32_t n){cloudPut16(p,n);cloudPut16(p+2,n>>16);}
static bool cloudCounters(unsigned *c){c[0]=decoded;c[1]=started;c[2]=completed;c[3]=completed;return true;}
static bool cloudWait(unsigned index,unsigned target,unsigned,bool=false){
 if(index==1){assert(target<=submitted);started=target;return true;}
 if(index==0){assert(decoded==target);return true;}
 assert(index==3 && target==submitted);completed=target;return true;
}
static bool cloudInfo(unsigned,unsigned,unsigned){return true;}
static bool cloudCommand(unsigned op,const uint8_t *p=nullptr,size_t n=0,bool reply=true){
 memset(cloudBuffer,0,sizeof(cloudBuffer));cloudPut16(cloudBuffer+2,6);cloudBuffer[5]=1;
 if(op==0xA1A1){cloudPut16(cloudBuffer+2,8);cloudPut16(cloudBuffer+6,0x2A94);}
 if(op==0xA2A0){++reservations;cloudPut16(cloudBuffer+2,8);cloudPut16(cloudBuffer+6,7);}
 if(op==0xE1A9){assert(n==8 && p[0]==2 && p[1]==1);if(rejectPaper)return false;}
 if(op==0xD0A9){assert(n==64 && !reply && decoded-started<2);flags.push_back(p[9]&6);}
 if(op==0xC0A0)assert(n && !reply);
 if(op==0xC0A4)++decoded;
 if(op==0xE0A7){++submitted;assert(Prepared::u16(p)==submitted);}
 if(op==0xE0A9){assert(completed==submitted);++releases;}
 return true;
}
'''
main = r'''
int main(){
 uint8_t parameters[64]={};std::vector<uint8_t> band(10000,0x33);
 Prepared::Job j;j.pages={ {parameters,{{band.data(),band.size()}}}, {parameters,{{band.data(),1}}}, {parameters,{{band.data(),1}}} };
 j.plan={{1,4},{0,2},{2,0}};
 assert(cloudPrint(j));assert(reservations==1 && releases==1 && decoded==3 && completed==3);
 assert((flags==std::vector<int>{4,2,0}));assert(cloudView.submitted==3);
 decoded=started=submitted=completed=reservations=releases=0;rejectPaper=true;
 assert(!cloudPrint(j));assert(reservations==1 && releases==0 && decoded==0);
 decoded=started=submitted=completed=reservations=releases=0;rejectPaper=false;flags.clear();
 assert(cloudPrint(j,2));assert(decoded==1 && completed==1 && reservations==1 && releases==1);
 assert((flags==std::vector<int>{0}));assert(cloudView.submitted==3);
 puts("PASS: production cloud engine, duplex order, pipeline, one reservation/release, no retry on failure");
}
'''
compiler = shutil.which('clang++') or shutil.which('g++')
assert compiler, 'C++11 compiler required'
flags = ['-std=c++11', '-I', str(Path.cwd())]
if sys.platform == 'darwin':
    compiler = '/Library/Developer/CommandLineTools/usr/bin/clang++'
    flags += ['-isysroot', '/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk']
with tempfile.TemporaryDirectory() as folder:
    cpp = Path(folder, 'engine.cpp')
    cpp.write_text(harness + engine + main)
    binary = str(Path(folder, 'engine'))
    subprocess.run([compiler, *flags, str(cpp), '-o', binary], check=True)
    subprocess.run([binary], check=True)
    control = str(Path(folder, 'control'))
    subprocess.run([compiler, *flags, 'check_job_control.cpp', '-o', control], check=True)
    subprocess.run([control], check=True)
    recovery = str(Path(folder, 'recovery'))
    subprocess.run([compiler, *flags, 'check_print_recovery.cpp', '-o', recovery], check=True)
    subprocess.run([recovery], check=True)
    binary = str(Path(folder, 'parser'))
    subprocess.run([compiler, *flags, 'check_prepared.cpp', '-o', binary], check=True)
    subprocess.run([binary], check=True)

# Exercise actual command reassembly, including the real 6+54-byte error reply.
exchange = source[source.index('static bool captExchange('):source.index('static bool cloudCommand(')]
exchange_harness = r'''
#include "station/prepared_job.h"
#include "station/capt_status.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
static std::vector<uint8_t> reply;
static size_t offset=0, fragmentSize=6;
static void captRecordFrame(const uint8_t *,size_t){}
static void cloudPut16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static bool cloudTransfer(uint8_t *p,int capacity,bool input,int &received,uint32_t owner){
 assert(owner==123);
 if(!input){offset=0;received=0;return true;}
 size_t n=offset==0?fragmentSize:reply.size()-offset;
 if(!n||n>size_t(capacity))return false;
 memcpy(p,reply.data()+offset,n);offset+=n;received=n;return true;
}
'''
exchange_main = r'''
int main(){
 uint8_t out[80];reply.assign(60,0);reply[0]=0xa8;reply[1]=0xa0;reply[2]=60;reply[6]=8;
 for(fragmentSize=1;fragmentSize<60;++fragmentSize){assert(captExchange(out,sizeof(out),123,0xA0A8));assert(offset==60&&out[6]==8);}
 reply[2]=59;fragmentSize=6;assert(!captExchange(out,sizeof(out),123,0xA0A8));
 reply[2]=60;reply[0]=0xa1;assert(!captExchange(out,sizeof(out),123,0xA0A8));
 reply[0]=0xa8;reply[4]=128;assert(!captExchange(out,sizeof(out),123,0xA0A8));
 puts("PASS: production CAPT reassembly, all splits, mismatched size/opcode, rejected reply");
}
'''
with tempfile.TemporaryDirectory() as folder:
    cpp = Path(folder, 'exchange.cpp')
    cpp.write_text(exchange_harness + exchange + exchange_main)
    binary = str(Path(folder, 'exchange'))
    subprocess.run([compiler, *flags, str(cpp), '-o', binary], check=True)
    subprocess.run([binary], check=True)
    binary = str(Path(folder, 'status'))
    subprocess.run([compiler, *flags, 'check_capt_status.cpp', '-o', binary], check=True)
    subprocess.run([binary], check=True)

# Production fault wait keeps the CAPT session alive; no reserve/replay on ready.
fault_engine=source[source.index('static bool cloudCounters('):source.index('static bool cloudWait(')]
fault_harness=r'''
#include "station/prepared_job.h"
#include "station/capt_status.h"
#include "station/print_recovery.h"
#include "station/job_control.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static uint8_t cloudBuffer[4096];static int cloudLock=0,jobLock=0;
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
#define pdMS_TO_TICKS(x) (x)
static struct {unsigned completed;} cloudView;
static PrintRecovery::Checkpoint cloudRecovery;static bool cloudTracking=true;
static uint32_t cloudFaultTime=0,tick=0;
static uint32_t millis(){return tick;}
static PrintControl::Job currentJob;
static PrintControl::Job jobSnapshot(){return currentJob;}
static void jobProgress(unsigned,unsigned n){currentJob.completed=n;}
static void jobRecoveryProgress(const PrintRecovery::Checkpoint &p){currentJob.completed=p.confirmed;}
static void cloudState(const char *,const char * = ""){}
static void cloudHeartbeat(){}
static bool cancelOnWait=false;static unsigned queries=0,serviceQueries=0;
static void vTaskDelay(unsigned n){tick+=n;if(cancelOnWait)strcpy(currentJob.request,"cancel_requested");}
static void put16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
static bool cloudCommand(unsigned op){
 if(op==0xA2A2){++serviceQueries;return true;}
 assert(op==0xA0A8);memset(cloudBuffer,0,sizeof(cloudBuffer));put16(cloudBuffer+2,60);
 ++queries;
 if(queries==1)cloudBuffer[6]=8; // Service fault also waits, retaining session.
 if(queries==2)put16(cloudBuffer+16,0x100);
 if(queries==3)put16(cloudBuffer+16,0x4000);
 if(queries==4)put16(cloudBuffer+12,0x4000);
 for(int i=0;i<4;++i)put16(cloudBuffer+18+i*2,2);
 return true;
}
'''
fault_main=r'''
int main(){
 cloudRecovery.begin("job",{4,2,0},1);cloudRecovery.started=true;currentJob.begin("job","cloud",true);currentJob.total=3;
 unsigned c[4];assert(cloudCounters(c));assert(queries==5&&serviceQueries==1&&tick==800&&cloudFaultTime==800);
 assert(currentJob.completed==2&&cloudRecovery.confirmed==2&&!strcmp(currentJob.state,"printing"));
 queries=serviceQueries=0;cancelOnWait=true;assert(!cloudCounters(c));assert(queries==1&&currentJob.stopping());
 puts("PASS: production cloud service/jam/cover/paper wait, ready in same session, preserved progress, cancellation during fault");
}
'''
with tempfile.TemporaryDirectory() as folder:
    cpp=Path(folder,'fault.cpp');cpp.write_text(fault_harness+fault_engine+fault_main)
    binary=str(Path(folder,'fault'))
    subprocess.run([compiler,*flags,str(cpp),'-o',binary],check=True)
    subprocess.run([binary],check=True)
