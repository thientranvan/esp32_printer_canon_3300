#pragma once
#include <WiFi.h>
#include "job_runtime.h"
#include <cJSON.h>

// ponytail: direct CAPT TCP, one active connection, OS spoolers hold queued jobs.
// Add a durable ESP spool for independently submitted prepared jobs (VPS path).
static WiFiServer captServer(9100);
static std::atomic<unsigned> captTCPTotal{0}, captTCPCompleted{0};
static std::atomic<int> captTCPState{0}; // 0 idle, 1 waiting for USB owner, 2 active
static std::atomic<int> captTCPError{0};
static std::atomic<uint32_t> captTCPHeader{0};
static std::atomic<uint32_t> captTCPCommands{0};

static bool tcpRead(WiFiClient &client, uint8_t *data, size_t size) {
  uint32_t progress = millis();
  while (size) {
    int available = client.available();
    if (available > 0) {
      int count = client.read(data, min(size, size_t(available)));
      if (count <= 0) return false;
      data += count;
      size -= count;
      progress = millis();
    } else {
      if (!client.connected() || uint32_t(millis() - progress) >= 15000) return false;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  return true;
}

static bool tcpTransfer(uint8_t *data, int size, bool input, int &received) {
  if (!claimCapt(1) || readUSB().recovering) return false;
  int state = captCommand.state.load();
  if ((state != 0 && state != 3) || !captCommand.state.compare_exchange_strong(state, 5)) return false;
  captCommand.size = size;
  captCommand.received = 0;
  captCommand.error = 0;
  captCommand.input = input;
  if (!input) memcpy(captCommand.data, data, size);
  captCommand.state.store(1);
  uint32_t started = millis();
  while (captCommand.state.load() != 3) {
    if (uint32_t(millis() - started) >= 8000) { captTCPError.store(ESP_ERR_TIMEOUT); return false; }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  int error = captCommand.error;
  if (error != ESP_OK) { captTCPError.store(error); return false; }
  received = captCommand.received;
  if (input) memcpy(data, captCommand.data, received);
  return true;
}

static void captTCPTask(void *) {
  auto *buffer = static_cast<uint8_t *>(malloc(4096));
  if (!buffer) { captTCPError.store(ESP_ERR_NO_MEM); vTaskDelete(nullptr); return; }
  captServer.begin();
  while (true) {
    WiFiClient client = captServer.accept();
    if (!client) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    // Match ordinary LAN printer access; never accept from recovery AP/foreign subnets.
    uint32_t mask = uint32_t(WiFi.subnetMask());
    if (WiFi.status() != WL_CONNECTED ||
        (uint32_t(client.remoteIP()) & mask) != (uint32_t(WiFi.localIP()) & mask)) {
      client.stop(); continue;
    }
    client.setNoDelay(true);
    captTCPTotal.store(0); captTCPCompleted.store(0);
    bool printJob=false,released=false,cooperative=false,mayPrint=false;char jobId[65]={},jobTitle[129]={},jobClient[97]={},jobOS[9]={};
    snprintf(jobId,sizeof(jobId),"lan-%08lx-%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
    captTCPState.store(1);
    uint32_t waiting = millis();
    bool acquired = false;
    while (client.connected() && uint32_t(millis() - waiting) < 60000) {
      if (claimCapt(1)) { acquired = true; break; }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (acquired) {
      captTCPState.store(2);
      captTCPError.store(0);
      while (tcpRead(client, buffer, 4)) {
        uint16_t opcode = uint16_t(buffer[0]) | (uint16_t(buffer[1]) << 8);
        unsigned length = unsigned(buffer[2]) | (unsigned(buffer[3]) << 8);
        captTCPHeader.store(uint32_t(opcode) | (uint32_t(length) << 16));
        if(opcode==0xF004) {
          if(!cooperative||printJob||length<6||length>1028||!tcpRead(client,buffer+4,length-4))break;
          buffer[length]=0;auto *meta=cJSON_Parse((char *)buffer+4);bool valid=cJSON_IsObject(meta);
          struct Field {const char *key;char *out;size_t size;};
          for(auto f:{Field{"title",jobTitle,sizeof(jobTitle)},Field{"client_name",jobClient,sizeof(jobClient)},Field{"client_os",jobOS,sizeof(jobOS)}}){
            auto *v=cJSON_GetObjectItemCaseSensitive(meta,f.key);
            if(!cJSON_IsString(v)||strlen(v->valuestring)>=f.size){valid=false;break;}
            for(const unsigned char *p=(const unsigned char *)v->valuestring;*p;++p)if(*p<32||*p==127)valid=false;
            strlcpy(f.out,v->valuestring,f.size);
          }
          cJSON_Delete(meta);if(!valid){captTCPError.store(ESP_ERR_INVALID_ARG);break;}
          const uint8_t ack[6]={4,0xF0,6,0,0,1};if(client.write(ack,6)!=6)break;continue;
        }
        if(opcode==0xF003) {
          if(!printJob || !cooperative || length!=5 || !tcpRead(client,buffer+4,1))break;
          auto j=jobSnapshot();unsigned phase=buffer[4];
          if(phase==1 && !strcmp(j.request,"pause_requested")){
            portENTER_CRITICAL(&jobLock);strcpy(currentJob.state,"paused");portEXIT_CRITICAL(&jobLock);
          }else if(phase==2 && !strcmp(j.request,"resume_requested")){
            portENTER_CRITICAL(&jobLock);strcpy(currentJob.state,"printing");currentJob.request[0]=0;portEXIT_CRITICAL(&jobLock);
          }else break;
          const uint8_t ack[6]={3,0xF0,6,0,0,1};if(client.write(ack,6)!=6)break;continue;
        }
        if(opcode==0xF002) {
          if(length<5 || length>68 || !tcpRead(client,buffer+4,length-4)){captTCPError.store(ESP_ERR_INVALID_ARG);break;}
          bool valid=true;for(unsigned i=4;i<length;++i)if(!isalnum(buffer[i]) && buffer[i]!='-' && buffer[i]!='_')valid=false;
          if(!valid || printJob){captTCPError.store(ESP_ERR_INVALID_ARG);break;}
          cooperative=true;memcpy(jobId,buffer+4,length-4);jobId[length-4]=0;
          const uint8_t ack[6]={2,0xF0,6,0,0,1};if(client.write(ack,6)!=6)break;continue;
        }
        // Optional LAN progress metadata; consumed here, never forwarded to USB.
        if(opcode==0xF001) {
          if(length!=8 || !tcpRead(client,buffer+4,4)) { captTCPError.store(ESP_ERR_INVALID_ARG);break; }
          unsigned total=uint32_t(buffer[4])|(uint32_t(buffer[5])<<8)|(uint32_t(buffer[6])<<16)|(uint32_t(buffer[7])<<24);
          if(!total || total>9900){captTCPError.store(ESP_ERR_INVALID_ARG);break;}
          // Fault retries must not repeatedly turn the same failed job into printing.
          if(captFault()){jobRejectBeforeStart(jobId,total,cooperative);if(jobSnapshot().matches(jobId))jobMetadata(jobTitle,jobClient,jobOS);captTCPError.store(ESP_FAIL);break;}
          captTCPTotal.store(total);
          if(!printJob){if(!strcmp(jobSnapshot().state,"uncertain"))break;jobBegin(jobId,"lan",cooperative);jobMetadata(jobTitle,jobClient,jobOS);printJob=true;}
          jobProgress(total,0);
          const uint8_t ack[6]={1,0xF0,6,0,0,1};
          if(client.write(ack,6)!=6)break;
          continue;
        }
        // CAPT commands occupy A0/A1/A2/A3, C0/D0 and E0/E1 namespaces.
        unsigned group = opcode >> 8;
        if (length < 4 || !((group >= 0xA0 && group <= 0xA3) || group == 0xC0 ||
            group == 0xD0 || group == 0xE0 || group == 0xE1)) {
          captTCPError.store(ESP_ERR_INVALID_ARG); break;
        }
        if(printJob && (opcode==0xA3A2 || group==0xC0 || group==0xD0))mayPrint=true;
        unsigned remaining = length - 4;
        int first = min(remaining, 4092U);
        int received = 0;
        if (!tcpRead(client, buffer + 4, first) || !tcpTransfer(buffer, first + 4, false, received)) break;
        remaining -= first;
        bool ok = true;
        while (remaining) {
          unsigned count = min(remaining, 4096U);
          if (!tcpRead(client, buffer, count) || !tcpTransfer(buffer, count, false, received)) { ok = false; break; }
          remaining -= count;
        }
        if (!ok) break;
        captTCPCommands.fetch_add(1);
        if (group == 0xC0 || group == 0xD0) continue; // Data packets have no reply.
        if(!CaptStatus::reply(buffer,4096,opcode,[](uint8_t *p,int size,int &n){return tcpTransfer(p,size,true,n);})){
          captTCPError.store(ESP_ERR_INVALID_RESPONSE);break;
        }
        unsigned replyLength=CaptStatus::word(buffer+2);
        captRecordFrame(buffer,replyLength);
        if(opcode==0xA0A8 && replyLength>=40 && CaptStatus::decode(buffer).service){
          portENTER_CRITICAL(&captTelemetryLock);bool query=!captServiceAt||uint32_t(millis()-captServiceAt)>=1500;portEXIT_CRITICAL(&captTelemetryLock);
          if(query){
            uint8_t service[80]={0xA2,0xA2,4,0};int count=0;
            if(tcpTransfer(service,4,false,count) && CaptStatus::reply(service,sizeof(service),0xA2A2,
                [](uint8_t *p,int size,int &n){return tcpTransfer(p,size,true,n);}))
              captRecordFrame(service,CaptStatus::word(service+2));
          }
        }
        if(opcode==0xA0A8 && replyLength>=40){captTCPCompleted.store(CaptStatus::decode(buffer).completed);if(printJob)jobProgress(captTCPTotal.load(),captTCPCompleted.load());}
        if(opcode==0xE0A9 && replyLength>=6 && !(buffer[4]&128))released=true;
        if(client.write(buffer,replyLength)!=replyLength)break;
      }
      if(printJob)jobFinish(released && captTCPCompleted.load()>=captTCPTotal.load()?"completed":mayPrint?"uncertain":"failed");
      releaseCapt(1);
    }
    client.stop();
    captTCPState.store(0);
  }
}

static void startCaptTCP() {
  if (xTaskCreate(captTCPTask, "capt-tcp", 8192, nullptr, 1, nullptr) != pdPASS)
    captTCPError.store(ESP_ERR_NO_MEM);
}
