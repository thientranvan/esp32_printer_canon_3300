"""Compile production IPP task startup; verify memory caps and failure cleanup."""
from pathlib import Path
import subprocess,tempfile
s=Path('station/ipp_server.h').read_text();block=s[s.index('static bool start(){'):s.index(' MDNS.addService(',s.index('static bool start(){'))]+' return true;\n}\n'
prefix=r'''
#include <cassert>
#include <cstddef>
#include <cstdio>
class WiFiClient;
using TaskHandle_t=void*;using QueueHandle_t=void*;
const int pdPASS=1;const unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
static unsigned nextId=0,calls=0,failAt=0,internalBytes=0,externalBytes=0,normalDeleted=0,externalDeleted=0,queues=0;
static QueueHandle_t connections=nullptr;
static void printTask(void*){}static void clientTask(void*){}static void serverTask(void*){}
static unsigned esp_random(){return 12;}
static QueueHandle_t xQueueCreate(unsigned n,size_t sz){assert(n==2&&sz==sizeof(void*));++queues;return (void*)1;}
static void vQueueDelete(QueueHandle_t p){assert(p);--queues;}
static int xTaskCreate(void(*fn)(void*),const char*,unsigned size,void*,unsigned,TaskHandle_t *out){
 ++calls;if(calls==failAt)return 0;assert(fn==printTask||fn==serverTask);assert(size==(fn==printTask?12288:4096));internalBytes+=size;if(out)*out=(void*)2;return pdPASS;
}
static int xTaskCreateWithCaps(void(*fn)(void*),const char*,unsigned size,void*,unsigned,TaskHandle_t *out,unsigned caps){
 ++calls;if(calls==failAt)return 0;assert(fn==clientTask&&size==16384&&caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));externalBytes+=size;*out=(void*)3;return pdPASS;
}
static void vTaskDelete(TaskHandle_t task){assert(task==(void*)2);++normalDeleted;}
static void vTaskDeleteWithCaps(TaskHandle_t task){assert(task==(void*)3);++externalDeleted;}
'''
main=r'''
int main(){
 assert(start());assert(internalBytes==16384&&externalBytes==32768&&calls==4&&queues==1);
 for(unsigned failed=1;failed<=4;++failed){
  failAt=failed;calls=internalBytes=externalBytes=normalDeleted=externalDeleted=queues=0;connections=nullptr;
  assert(!start()&&!queues&&!connections);
  assert(externalDeleted==externalBytes/16384);assert(normalDeleted==internalBytes/12288);
 }
 puts("PASS: two fixed IPP HTTP stacks use 32768 bytes PSRAM; startup failures free stacks with matching API");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'check.cpp';p.write_text(prefix+block+main);exe=Path(tmp)/'check';subprocess.run(['c++','-std=c++11',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
