"""Run production asynchronous printer-class repair with fake USB callbacks."""
from pathlib import Path
import subprocess, tempfile
source=Path('station/usb_monitor.h').read_text()
block=source[source.index('static esp_err_t startPrinterRepair('):source.index('static void usbTask(')]
prefix=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using esp_err_t=int;using usb_host_client_handle_t=void*;
const int ESP_OK=0,ESP_FAIL=1,ESP_ERR_INVALID_STATE=2,USB_TRANSFER_STATUS_COMPLETED=0;
struct usb_transfer_t {uint8_t data_buffer[8];void *device_handle,*context;unsigned bEndpointAddress,num_bytes;int status=0;void(*callback)(usb_transfer_t*);};
struct USBContext {bool gone=false,claimed=true,probeDone=false;void *device=(void*)1;usb_transfer_t *probe=nullptr;unsigned repairStep=0;};
struct USBStatus {bool recovering=false;unsigned recoveries=0;};
static struct {std::atomic<int> state{6};int error=0;void *transfer=nullptr;} captCommand;
static struct {void reset(){}} captFrames;
static std::atomic<int> printerRepair{1},printerRepairError{0};
static struct {template<class...T>void printf(const char*,T...){}} Serial;
static unsigned allocations=0,frees=0,failSubmit=0,submitted=0;
static std::vector<unsigned> ops;
static void publishUSB(const USBStatus&){}
static int usb_host_transfer_alloc(int n,int,usb_transfer_t **p){assert(n==8);*p=new usb_transfer_t; ++allocations;return 0;}
static void usb_host_transfer_free(usb_transfer_t *p){++frees;delete p;}
static int usb_host_endpoint_halt(void *d,unsigned ep){assert(d);ops.push_back(0x100|ep);return 0;}
static int usb_host_endpoint_flush(void *d,unsigned ep){assert(d);ops.push_back(0x200|ep);return 0;}
static int usb_host_endpoint_clear(void *d,unsigned ep){assert(d);ops.push_back(0x300|ep);return 0;}
static int usb_host_transfer_submit_control(void*,usb_transfer_t *t){
 ++submitted;assert(t->num_bytes==8&&t->bEndpointAddress==0);
 unsigned step=static_cast<USBContext*>(t->context)->repairStep;
 uint8_t reset[]={0x21,2,0,0,0,0,0,0},clear[]={2,1,0,0,uint8_t(step==2?1:0x82),0,0,0};
 assert(!memcmp(t->data_buffer,step==1?reset:clear,8));ops.push_back(step);
 return step==failSubmit?ESP_FAIL:ESP_OK;
}
static void printerProbeDone(usb_transfer_t *t){static_cast<USBContext*>(t->context)->probeDone=true;}
static void begin(){allocations=frees=submitted=failSubmit=0;ops.clear();printerRepair=1;captCommand.state=6;captCommand.transfer=nullptr;}
'''
request=Path('station/cloud_print.h').read_text()
request=request[request.index('static bool cloudRequestUSBRepair()'):request.index('static bool cloudResetInterruptedSession(')]
prefix+=r'''
static bool ownerAvailable=true;static unsigned released=0;
static const unsigned CloudOwner=42;
static bool claimCapt(unsigned owner){assert(owner==42);return ownerAvailable;}
static void releaseCapt(unsigned owner){assert(owner==42);++released;}
'''
main=r'''
int main(){
 begin();captCommand.state=2;assert(!cloudRequestUSBRepair()&&captCommand.state==2);
 captCommand.state=3;ownerAvailable=false;assert(!cloudRequestUSBRepair()&&captCommand.state==3);
 ownerAvailable=true;assert(cloudRequestUSBRepair()&&captCommand.state==6&&printerRepair==1);
 assert(!cloudRequestUSBRepair()&&captCommand.state==6);
 for(unsigned failure=0;failure<=3;++failure){
  begin();USBContext c;USBStatus s;
  servicePrinterRepair(nullptr,c,s);assert(c.repairStep==1&&s.recovering&&captCommand.state==6);
  // A pending callback must not be freed or treated as an ACK.
  auto *pending=c.probe;servicePrinterRepair(nullptr,c,s);assert(c.probe==pending&&frees==0);
  while(c.probe){unsigned step=c.repairStep;c.probe->status=step==failure?ESP_FAIL:0;c.probe->callback(c.probe);servicePrinterRepair(nullptr,c,s);}
  assert(allocations==frees&&!s.recovering&&captCommand.state==3);
  assert(printerRepair==(failure?4:3));assert(submitted==(failure?failure:3));
  if(!failure)assert((ops==std::vector<unsigned>{0x101,0x201,0x182,0x282,1,2,3,0x301,0x382}));
 }
 for(unsigned failure=1;failure<=3;++failure){
  begin();failSubmit=failure;USBContext c;USBStatus s;servicePrinterRepair(nullptr,c,s);
  while(c.probe){c.probe->callback(c.probe);servicePrinterRepair(nullptr,c,s);}
  assert(printerRepair==4&&allocations==frees&&captCommand.state==3);
 }
 begin();USBContext c;USBStatus s;captCommand.transfer=(void*)1;
 servicePrinterRepair(nullptr,c,s);assert(!allocations&&printerRepair==1);
 captCommand.transfer=nullptr;servicePrinterRepair(nullptr,c,s);c.gone=true;
 c.probe->callback(c.probe);servicePrinterRepair(nullptr,c,s);assert(printerRepair==4&&allocations==frees&&submitted==1);
 begin();USBContext absent;absent.device=nullptr;USBStatus a;
 servicePrinterRepair(nullptr,absent,a);assert(printerRepair==4&&allocations==0);
 puts("PASS: production USB printer reset/CLEAR_FEATURE sequence; exclusive maintenance, callback lifetime, NAK/STALL failure, submit failure and disconnect");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'check.cpp';p.write_text(prefix+block+request+main);out=Path(tmp)/'check'
 subprocess.run(['/Library/Developer/CommandLineTools/usr/bin/clang++','-std=c++11','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk',str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
