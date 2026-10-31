"""Run production same-USB recovery against a fake printer."""
from pathlib import Path
import subprocess,tempfile
s=Path('station/cloud_print.h').read_text()
block=s[s.index('static bool cloudResetInterruptedSession('):s.index('static bool cloudPrint(')]
prefix=r'''
#include "station/prepared_job.h"
#include "station/capt_status.h"
#include "station/print_recovery.h"
#include "station/job_control.h"
#include <atomic>
#include <cassert>
#include <cstdio>
static PrintControl::Job j;
static PrintRecovery::Checkpoint cloudRecovery;
static std::atomic<unsigned> printerEpoch{7};
static struct {bool connected=true,responsive=true,recovering=false;} usb;
static uint8_t cloudBuffer[80];
static const unsigned CloudOwner=42;
static unsigned queries=0,failOp=0,mask=0,count=2,tail=0,processing=0,tailProcessing=0;
static bool cleaned=false,rejectCleanup=false,paperLoaded=true;
static bool cloudPollPaper(bool){return true;}
static int captPaperPresent(unsigned source){assert(source==1);return paperLoaded;}
static std::vector<unsigned> ops;
static void put(uint8_t*p,unsigned n){p[0]=n;p[1]=n>>8;}
static PrintControl::Job jobSnapshot(){return j;}
static decltype(usb) readUSB(){return usb;}
static void jobProgress(unsigned,unsigned n){j.completed=n;}
static void jobRecoveryProgress(const PrintRecovery::Checkpoint&){}
static void jobPrinterSession(unsigned n){j.printerSession=n;j.sessionEpoch=printerEpoch;}
static bool cancelCapt(unsigned owner,bool report){assert(owner==42&&!report);cleaned=true;return !rejectCleanup;}
static bool cloudCommand(unsigned op,const uint8_t*p=nullptr,size_t n=0){
 ops.push_back(op);if(op==failOp)return false;
 memset(cloudBuffer,0,sizeof(cloudBuffer));
 if(op==0xA0A8){++queries;put(cloudBuffer+2,60);put(cloudBuffer+16,mask);put(cloudBuffer+12,queries==1?processing:tailProcessing);put(cloudBuffer+24,queries==1?count:tail);}
 else if(op==0xA2A0){assert(n==8&&p[2]==30);put(cloudBuffer+2,8);put(cloudBuffer+6,9);}
 else assert(op==0xA3A2);
 return true;
}
static void begin(){j.begin("job","cloud",true);j.total=4;j.printerSession=3;j.sessionEpoch=7;
 cloudRecovery.begin("job",{0,4,2,0},7);cloudRecovery.started=true;
 queries=failOp=mask=tail=processing=tailProcessing=0;count=2;cleaned=rejectCleanup=false;ops.clear();}
'''
main=r'''
int main(){
 begin();assert(cloudResetInterruptedSession());assert(cleaned&&j.completed==1);
 assert((ops==std::vector<unsigned>{0xA0A8,0xA3A2,0xA2A0,0xA0A8}));
 assert(!cloudRecovery.restart(7));assert(cloudRecovery.restart(7,true)&&cloudRecovery.base==1);
 // Captured interrupted session: engine flags 0x84 may remain set with no
 // physical fault. Cleanup must be reachable; post-cleanup still proves idle.
 begin();processing=0x84;assert(cloudResetInterruptedSession()&&cleaned);
 for(unsigned fault:{0x100u,0x4000u}){begin();mask=fault;assert(!cloudResetInterruptedSession()&&!cleaned&&ops.size()==1);}
 for(unsigned op:{0xA0A8u,0xA3A2u,0xA2A0u}){begin();failOp=op;assert(!cloudResetInterruptedSession()&&!cleaned);}
 begin();rejectCleanup=true;assert(!cloudResetInterruptedSession());
 begin();tailProcessing=0x84;assert(!cloudResetInterruptedSession()&&cleaned);
 begin();tail=1;assert(!cloudResetInterruptedSession()); // ACK is not proof of an empty buffer.
 begin();cloudRecovery.observed=3;assert(!cloudResetInterruptedSession()&&!cleaned);
 begin();count=0;cloudRecovery.observed=3;cloudRecovery.confirmed=1;
 assert(!cloudResetInterruptedSession()&&!cleaned);
 queries=0;assert(cloudResetInterruptedSession(true)&&cleaned&&cloudRecovery.confirmed==1);
 begin();processing=0x84;count=0;cloudRecovery.observed=3;assert(!cloudResetInterruptedSession(true)&&!cleaned);
 begin();j.sessionEpoch=6;assert(!cloudResetInterruptedSession()&&ops.empty());
 begin();j.command("job","cancel");assert(!cloudResetInterruptedSession()&&ops.empty());
 begin();paperLoaded=false;assert(!cloudResetInterruptedSession()&&!cleaned&&ops.empty());paperLoaded=true;
 puts("PASS: production same-USB recovery requires Ready, valid counters, reservation and verified empty buffer; restarts only from complete sheets");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'check.cpp';p.write_text(prefix+block+main);out=Path(tmp)/'check'
 subprocess.run(['/Library/Developer/CommandLineTools/usr/bin/clang++','-std=c++11','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk','-I',str(Path.cwd()),str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
