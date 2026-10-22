#pragma once
#include "capt_status.h"

// ponytail: one diagnostic operation at a time, 4096 bytes; add job ownership
// and a spool before accepting concurrent print clients.
struct CaptCommand {
  std::atomic<int> state{0}; // 0 idle, 1 queued, 2 active, 3 result, 4 cancelling, 5 filling, 6 maintenance
  uint8_t data[4096];
  int size = 0;
  int received = 0;
  int error = 0;
  bool input = false;
  bool done = false; // USB client task only
  bool cancelled = false;
  uint32_t started = 0;
  usb_transfer_t *transfer = nullptr;
};
static CaptCommand captCommand;
static portMUX_TYPE captTelemetryLock=portMUX_INITIALIZER_UNLOCKED;
static uint8_t captRawStatus[64]={};
static size_t captRawSize=0;
static uint32_t captRawAt=0, captServiceAt=0;
static unsigned captServiceCode=0,captServiceSub=0;
static CaptStatus::Frames captFrames;
static void captRecordFrame(const uint8_t *p,size_t n) {
    unsigned op=CaptStatus::word(p);
    portENTER_CRITICAL(&captTelemetryLock);
    if(op==0xA0A8 && n>=40){
      captRawSize=min(n,sizeof(captRawStatus));memcpy(captRawStatus,p,captRawSize);captRawAt=millis();
      if(!CaptStatus::decode(p).service){captServiceAt=0;captServiceCode=captServiceSub=0;}
    }else if(op==0xA2A2 && n>=12 && p[5] && p[5]<=16){
      captServiceCode=CaptStatus::word(p+8);captServiceSub=CaptStatus::word(p+10);captServiceAt=millis();
    }
    portEXIT_CRITICAL(&captTelemetryLock);
}
static void captObserve(const uint8_t *data,size_t size) {
  captFrames.feed(data,size,[](const uint8_t *p,size_t n){captRecordFrame(p,n);});
}
static bool captFresh() {
  portENTER_CRITICAL(&captTelemetryLock);bool fresh=captRawSize>=40 && uint32_t(millis()-captRawAt)<10000;portEXIT_CRITICAL(&captTelemetryLock);return fresh;
}
static bool captFault() {
  uint8_t data[64];size_t n;uint32_t at;
  portENTER_CRITICAL(&captTelemetryLock);n=captRawSize;at=captRawAt;memcpy(data,captRawStatus,n);portEXIT_CRITICAL(&captTelemetryLock);
  if(n<40||uint32_t(millis()-at)>10000)return false;
  auto f=CaptStatus::decode(data);return f.service||f.jam||f.cover||f.paper;
}
static String captTelemetryJSON() {
  uint8_t data[64];size_t size;uint32_t at,serviceAt;unsigned code,sub;
  portENTER_CRITICAL(&captTelemetryLock);size=captRawSize;at=captRawAt;memcpy(data,captRawStatus,size);serviceAt=captServiceAt;code=captServiceCode;sub=captServiceSub;portEXIT_CRITICAL(&captTelemetryLock);
  String hex;const char *digits="0123456789abcdef";
  for(size_t i=0;i<size;++i){hex+=digits[data[i]>>4];hex+=digits[data[i]&15];}
  bool fresh=size>=40 && uint32_t(millis()-at)<10000 && readUSB().connected;
  CaptStatus::Flags f={};if(size>=40)f=CaptStatus::decode(data);
  char service[24]="";if(fresh&&f.service&&serviceAt&&uint32_t(millis()-serviceAt)<10000)snprintf(service,sizeof(service),"E%03X %04X",code,sub);
  String decoded=String(",\"fresh\":")+(fresh?"true":"false")+",\"service_error\":"+(f.service?"true":"false")+
    ",\"service_code\":\""+service+"\",\"paper_jam\":"+(f.jam?"true":"false")+",\"cover_open\":"+(f.cover?"true":"false")+
    ",\"toner_warning\":"+(f.toner?"true":"false")+",\"paper_required\":"+(f.paper?"true":"false")+
    ",\"toner_percent\":null,\"decoded\":"+f.decoded+",\"started\":"+f.started+",\"exiting\":"+f.exiting+",\"completed\":"+f.completed;
  return "{\"opcode\":\"a0a8\",\"frame_hex\":\""+hex+"\",\"age_ms\":"+(at?String(uint32_t(millis()-at)):String("null"))+decoded+"}";
}

static portMUX_TYPE captOwnerLock = portMUX_INITIALIZER_UNLOCKED;
static constexpr uint32_t CaptCloudOwner=0xFFFFFFFE;
static uint32_t captOwner = 0; // 1 = TCP session; >=2 = authenticated HTTP client
static uint32_t captOwnerAt = 0;

static bool captInUse() {
  portENTER_CRITICAL(&captOwnerLock); bool used=captOwner!=0; portEXIT_CRITICAL(&captOwnerLock); return used;
}

static bool claimCapt(uint32_t owner) {
  portENTER_CRITICAL(&captOwnerLock);
  int state = captCommand.state.load();
  if (captOwner >= 2 && captOwner != CaptCloudOwner && uint32_t(millis() - captOwnerAt) >= 30000 && (state == 0 || state == 3))
    captOwner = 0;
  bool acquired = owner && (!captOwner || captOwner == owner);
  if (acquired) { captOwner = owner; captOwnerAt = millis(); }
  portEXIT_CRITICAL(&captOwnerLock);
  return acquired;
}

static void releaseCapt(uint32_t owner) {
  portENTER_CRITICAL(&captOwnerLock);
  if (captOwner == owner) captOwner = 0;
  portEXIT_CRITICAL(&captOwnerLock);
}

static void captTransferDone(usb_transfer_t *) { captCommand.done = true; }

static void serviceCapt(USBContext &context) {
  if(context.gone){
    captFrames.reset();portENTER_CRITICAL(&captTelemetryLock);
    captRawSize=0;captRawAt=captServiceAt=0;captServiceCode=captServiceSub=0;
    portEXIT_CRITICAL(&captTelemetryLock);
  }
  auto &command = captCommand;
  int state = command.state.load();
  if (state == 1) {
    if (context.probe && !context.gone) {
      if (uint32_t(millis() - context.probeAt) >= 5000) {
        command.error = ESP_ERR_TIMEOUT;
        command.state.store(3);
      }
      return;
    }
    if (!context.device || context.gone || !context.claimed || context.probe) {
      command.error = ESP_ERR_INVALID_STATE;
      command.state.store(3);
      return;
    }
    command.done = false;
    command.cancelled = false;
    command.received = 0;
    command.error = usb_host_transfer_alloc(command.size, 0, &command.transfer);
    if (command.error != ESP_OK) { command.state.store(3); return; }
    auto *transfer = command.transfer;
    transfer->device_handle = context.device;
    transfer->bEndpointAddress = command.input ? 0x82 : 0x01;
    transfer->num_bytes = command.size;
    transfer->callback = captTransferDone;
    if (!command.input) memcpy(transfer->data_buffer, command.data, command.size);
    command.error = usb_host_transfer_submit(transfer);
    if (command.error != ESP_OK) {
      usb_host_transfer_free(transfer);
      command.transfer = nullptr;
      command.state.store(3);
      return;
    }
    command.started = millis();
    command.state.store(2);
    state = 2;
  }
  if ((state == 2 || state == 4) && command.done) {
    auto *transfer = command.transfer;
    if (!command.cancelled) {
      command.error = transfer->status == USB_TRANSFER_STATUS_COMPLETED ? ESP_OK : ESP_FAIL;
      if (command.error == ESP_OK) {
        if (command.input) {
          command.received = transfer->actual_num_bytes;
          memcpy(command.data, transfer->data_buffer, command.received);
          captObserve(command.data,command.received);
        } else if (transfer->actual_num_bytes != command.size) command.error = ESP_ERR_INVALID_SIZE;
      }
    }
    if(command.cancelled || command.error!=ESP_OK)captFrames.reset();
    if (!context.gone && (command.cancelled || command.error != ESP_OK)) {
      esp_err_t cleared = usb_host_endpoint_clear(context.device, transfer->bEndpointAddress);
      if (cleared != ESP_OK && command.error == ESP_OK) command.error = cleared;
    }
    usb_host_transfer_free(transfer);
    command.transfer = nullptr;
    command.state.store(3);
  } else if (state == 2 && uint32_t(millis() - command.started) >= 5000) {
    command.cancelled = true;
    command.error = ESP_ERR_TIMEOUT;
    command.state.store(4);
    // timeout_ms is unsupported by this SDK. Halt/flush, then wait for callback;
    // never free an in-flight transfer, even if cancellation itself fails.
    esp_err_t error = usb_host_endpoint_halt(context.device, command.transfer->bEndpointAddress);
    if (error == ESP_OK) error = usb_host_endpoint_flush(context.device, command.transfer->bEndpointAddress);
    if (error != ESP_OK) command.error = error;
  }
}
