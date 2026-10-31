"""Run production cloud wait through >60 seconds empty, refill, and cancellation."""
from pathlib import Path
import subprocess,tempfile
s=Path('station/cloud_print.h').read_text()
production=s[s.index('static bool cloudCounters('):s.index('static bool cloudWait(')]
harness=r'''
#include "station/capt_status.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static unsigned now=1,cloudFaultTime=0,clears=0,waits=0;
static bool cloudTracking=true,cancel=false;static int paper=0;
static uint8_t cloudBuffer[64];static int cloudLock=0,jobLock=0;
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
#define pdMS_TO_TICKS(x) (x)
static unsigned millis(){return now;}
static struct Recovery {unsigned confirmed=2;bool observe(unsigned){return true;}} cloudRecovery;
static struct {unsigned completed;} cloudView;
static struct Job {char request[24]="",state[24]="printing";const char *source="cloud";bool active=true;unsigned total=4,printerSession=1,sessionEpoch=1;} currentJob;
static Job jobSnapshot(){if(cancel)strcpy(currentJob.request,"cancel_requested");return currentJob;}
static struct {unsigned load(){return 1;}} printerEpoch;
static void jobProgress(unsigned,unsigned){}
static void jobRecoveryProgress(const Recovery&){}
static bool cloudPollPaper(){return true;}
static int captPaperPresent(unsigned n){assert(n==1);return paper;}
namespace Prepared {static unsigned u16(const uint8_t*p){return CaptStatus::word(p);}}
static bool cloudCommand(unsigned op){
 if(op==0xA0A8){memset(cloudBuffer,0,sizeof(cloudBuffer));cloudBuffer[2]=60;cloudBuffer[24]=2;}
 else {++clears;assert(paper==1);}
 return true;
}
static void cloudState(const char*,const char* ="",const char* =""){}
static void cloudHeartbeat(){}
static void vTaskDelay(unsigned ms){now+=ms;++waits;if(now>65000)paper=1;}
'''
main=r'''
int main(){unsigned counters[4];
 assert(cloudCounters(counters));assert(waits>300&&cloudFaultTime>=65000&&clears==0&&counters[3]==2);
 assert(!strcmp(currentJob.state,"printing"));
 cancel=true;paper=0;assert(!cloudCounters(counters)&&clears==0);
 puts("PASS: empty cassette waits beyond page timeout without clearing/replay; refill resumes same session; cancel remains available");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(harness+production+main);out=Path(tmp)/'test'
 subprocess.run(['/Library/Developer/CommandLineTools/usr/bin/clang++','-std=c++11','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk','-I',str(Path.cwd()),str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
