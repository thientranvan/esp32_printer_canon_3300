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
#include <cassert>
#include <cstdio>
#include <algorithm>
using std::min;
static unsigned decoded=0, started=0, submitted=0, completed=0, reservations=0, releases=0;
static bool rejectPaper=false;
static uint8_t cloudBuffer[4096];
static int cloudLock=0;
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
static struct { unsigned submitted; } cloudView;
static std::vector<int> flags;
static void cloudHeartbeat(){}
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
    binary = str(Path(folder, 'parser'))
    subprocess.run([compiler, *flags, 'check_prepared.cpp', '-o', binary], check=True)
    subprocess.run([binary], check=True)
