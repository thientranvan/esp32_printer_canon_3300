"""Exercise production CAPT transfer buffer lifetime and callback completion."""
from pathlib import Path
import subprocess,tempfile
s=Path('station/capt_transport.h').read_text()
block=s[s.index('// One DMA-capable transfer'):]
prefix=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "station/capt_status.h"
using esp_err_t=int;
const int ESP_OK=0,ESP_FAIL=1,ESP_ERR_NO_MEM=257,ESP_ERR_TIMEOUT=263,ESP_ERR_INVALID_STATE=259,ESP_ERR_INVALID_SIZE=260,USB_TRANSFER_STATUS_COMPLETED=0;
struct usb_transfer_t {uint8_t data_buffer[4096];void *device_handle=nullptr;unsigned bEndpointAddress=0,num_bytes=0,actual_num_bytes=0;int status=0;void(*callback)(usb_transfer_t*)=nullptr;};
struct USBContext {bool gone=false,claimed=true;void *device=(void*)1;usb_transfer_t *probe=nullptr;unsigned probeAt=0;};
struct Command {std::atomic<int> state{0};bool input=false,done=false,cancelled=false;unsigned size=0,received=0,started=0;int error=0;usb_transfer_t *transfer=nullptr;uint8_t data[4096]={};};
static Command captCommand;
static unsigned tick=0,allocations=0,frees=0;static bool failAlloc=false,failSubmit=false;
static unsigned millis(){return tick;}
static struct {void reset(){}} captFrames;
static int captTelemetryLock=0;
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
static unsigned captRawSize=0,captRawAt=0,captServiceAt=0,captServiceCode=0,captServiceSub=0,captPaperAt=0,captPaperSources=0;static int captPaperSource=-1;
static std::atomic<unsigned> captLastOpcode{0};
static void captObserve(const uint8_t*,unsigned){}
static void captTransferFailed(int,unsigned){}
static int usb_host_transfer_alloc(unsigned n,int,usb_transfer_t **p){assert(n==4096);++allocations;if(failAlloc)return ESP_ERR_NO_MEM;*p=new usb_transfer_t;return ESP_OK;}
static int usb_host_transfer_submit(usb_transfer_t *p){assert(p==captCommand.transfer);return failSubmit?ESP_FAIL:ESP_OK;}
static int usb_host_endpoint_clear(void*,unsigned){return ESP_OK;}
static int usb_host_endpoint_halt(void*,unsigned){return ESP_OK;}
static int usb_host_endpoint_flush(void*,unsigned){return ESP_OK;}
static void usb_host_transfer_free(usb_transfer_t*){++frees;}
'''
main=r'''
int main(){
 failAlloc=true;assert(!prepareCaptTransfer());failAlloc=false;assert(prepareCaptTransfer());auto *reserved=captTransferBuffer;
 USBContext u;
 for(unsigned i=0;i<1000;++i){
  auto &c=captCommand;c.state=1;c.input=i%2;c.size=c.input?64:4096;memset(c.data,0x43,c.size);serviceCapt(u);
  assert(c.state==2&&c.transfer==reserved&&c.transfer->num_bytes==c.size);
  c.transfer->status=0;c.transfer->actual_num_bytes=c.size;memset(c.transfer->data_buffer,0x35,c.size);c.transfer->callback(c.transfer);serviceCapt(u);
  assert(c.state==3&&!c.transfer&&c.error==ESP_OK);if(c.input)assert(c.received==64&&c.data[0]==0x35);
 }
 assert(allocations==2&&!frees);
 failSubmit=true;captCommand.state=1;serviceCapt(u);assert(captCommand.state==3&&!captCommand.transfer&&!frees);failSubmit=false;
 captCommand.state=1;captCommand.input=false;captCommand.size=4096;serviceCapt(u);tick+=31000;serviceCapt(u);
 assert(captCommand.state==4&&captCommand.transfer==reserved&&!frees);
 captCommand.transfer->callback(captCommand.transfer);serviceCapt(u);assert(captCommand.state==3&&!captCommand.transfer&&!frees);
 u.gone=true;captCommand.state=1;serviceCapt(u);assert(captCommand.state==3&&!captCommand.transfer&&!frees);
 delete reserved;puts("PASS: one reserved CAPT USB buffer reused 1000 transfers; allocation/submit failures, timeout callback and disconnect retain lifetime");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(prefix+block+main);exe=Path(tmp)/'test'
 subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-I',str(Path.cwd()),str(p),'-o',str(exe)],check=True)
 import os
 env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1'
 subprocess.run([str(exe)],check=True,env=env)
