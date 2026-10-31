#pragma once
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>
#ifndef ESP32_FIRMWARE_VERSION
#define ESP32_FIRMWARE_VERSION 2026103101
#endif
static constexpr unsigned FirmwareVersion=ESP32_FIRMWARE_VERSION;
static constexpr const char *FirmwareBoard="esp32-s3-n16r8";
static constexpr const char *FirmwareOrigin="https://raw.githubusercontent.com/thientranvan/esp32_printer_canon_3300/main/firmware/";
static std::atomic<bool> firmwareDownloading{false},firmwareChecking{false};
static portMUX_TYPE firmwareLock=portMUX_INITIALIZER_UNLOCKED;
static char firmwareState[96]="Chờ mạng để kiểm tra cập nhật";
static void firmwareNote(const char *s){portENTER_CRITICAL(&firmwareLock);strlcpy(firmwareState,s,sizeof(firmwareState));portEXIT_CRITICAL(&firmwareLock);}
static String firmwareJSON(){char state[96];portENTER_CRITICAL(&firmwareLock);strlcpy(state,firmwareState,sizeof(state));portEXIT_CRITICAL(&firmwareLock);return String("{\"version\":")+FirmwareVersion+",\"check_interval_s\":60,\"checking\":"+(firmwareChecking.load()?"true":"false")+",\"downloading\":"+(firmwareDownloading.load()?"true":"false")+",\"state\":"+jsonString(state)+"}";}
static bool validFirmwareManifest(unsigned schema,unsigned version,const char *board,unsigned bytes,const char *hash,unsigned limit){
  if(schema!=1||version<=FirmwareVersion||!board||strcmp(board,FirmwareBoard)||bytes<288||bytes>limit||!hash||strlen(hash)!=64)return false;
  for(unsigned i=0;i<64;++i)if(!((hash[i]>='0'&&hash[i]<='9')||(hash[i]>='a'&&hash[i]<='f')))return false;
  return true;
}
static esp_http_client_handle_t firmwareClient(const char *name){
  String url=String(FirmwareOrigin)+name;
  esp_http_client_config_t cfg={};cfg.url=url.c_str();cfg.crt_bundle_attach=esp_crt_bundle_attach;
  cfg.disable_auto_redirect=true;cfg.timeout_ms=5000;cfg.buffer_size=2048;
  auto h=esp_http_client_init(&cfg);if(h)esp_http_client_set_header(h,"Accept-Encoding","identity");return h;
}
static bool firmwareFetchManifest(String &body){
  auto h=firmwareClient("latest.json");if(!h)return false;
  bool ok=esp_http_client_open(h,0)==ESP_OK;
  int64_t length=ok?esp_http_client_fetch_headers(h):-1;
  ok=ok&&length>0&&length<=2048&&esp_http_client_get_status_code(h)==200;
  char b[256];uint32_t start=millis();
  while(ok&&body.length()<size_t(length)){
    int n=esp_http_client_read(h,b,min(sizeof(b),size_t(length)-body.length()));
    if(n<=0||uint32_t(millis()-start)>15000){ok=false;break;}body.concat(b,n);
  }
  esp_http_client_close(h);esp_http_client_cleanup(h);return ok;
}
static bool firmwareDownload(unsigned bytes,const char *expected){
  auto h=firmwareClient("station.bin");if(!h)return false;
  bool ok=esp_http_client_open(h,0)==ESP_OK;
  int64_t length=ok?esp_http_client_fetch_headers(h):-1;
  ok=ok&&length==bytes&&esp_http_client_get_status_code(h)==200;
  bool started=ok&&Update.begin(bytes,U_FLASH);ok=ok&&started;
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
  if(ok)ok=mbedtls_sha256_starts(&sha,0)==0;
  uint8_t b[4096],digest[32];size_t received=0;uint32_t start=millis();
  while(ok&&received<bytes){
    int n=esp_http_client_read(h,reinterpret_cast<char*>(b),min(sizeof(b),size_t(bytes)-received));
    if(n<=0||uint32_t(millis()-start)>120000){ok=false;break;}
    ok=mbedtls_sha256_update(&sha,b,n)==0&&Update.write(b,n)==size_t(n);received+=n;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if(ok)ok=mbedtls_sha256_finish(&sha,digest)==0;
  char hex[65]={};if(ok)for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);
  if(ok)ok=!strcmp(hex,expected);
  if(ok)ok=Update.end(true);if(started&&!ok)Update.abort();
  mbedtls_sha256_free(&sha);esp_http_client_close(h);esp_http_client_cleanup(h);return ok;
}
static bool firmwareIdle(){
  auto j=jobSnapshot();auto c=cloudSnapshot();
  return !cloudBusy()&&!cloudClaiming.load()&&!captTCPState.load()&&!cloudNeedsInspection.load()&&
    !j.active&&strcmp(j.state,"uncertain")&&strcmp(j.state,"waiting_printer")&&strcmp(j.state,"paused")&&
    strcmp(j.printerCancel,"unverified")&&!c.job[0];
}
static bool firmwareReserve(){
  if(firmwareCheckPending.exchange(true))return false;
  if(!claimCapt(CaptFirmwareOwner)){firmwareCheckPending.store(false);return false;}
  int state=captCommand.state.load();
  if(!firmwareIdle()||(state!=0&&state!=3)){
    releaseCapt(CaptFirmwareOwner);firmwareCheckPending.store(false);return false;
  }
  return true; // Keep owner through flash/reboot; no diagnostic or print transfer can enter.
}
static void firmwareRelease(){releaseCapt(CaptFirmwareOwner);firmwareCheckPending.store(false);}
static void firmwareUpdateTask(void*){
  uint32_t checkedAt=millis()-60000;bool clockRequested=false;
  while(true){
    if(WiFi.isConnected()&&!clockRequested){configTime(0,0,"pool.ntp.org","time.google.com");clockRequested=true;}
    if(healthy&&WiFi.isConnected()&&time(nullptr)>1700000000&&!firmwareCheckPending.load()&&
       firmwareIdle()&&uint32_t(millis()-checkedAt)>=60000){
      bool reboot=false;firmwareChecking.store(true);
      firmwareNote("Đang kiểm tra bản cập nhật");String body;
      if(!firmwareFetchManifest(body))firmwareNote("Không kiểm tra được; thử lại sau 60 giây");
      else {
        auto *j=cJSON_Parse(body.c_str());
        auto number=[&](const char *key)->unsigned{auto *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsNumber(v)&&v->valuedouble>=0&&v->valuedouble<=UINT32_MAX&&v->valuedouble==unsigned(v->valuedouble)?unsigned(v->valuedouble):0;};
        unsigned schema=number("schema"),version=number("version"),bytes=number("bytes");
        auto *board=cJSON_GetObjectItemCaseSensitive(j,"board"),*hash=cJSON_GetObjectItemCaseSensitive(j,"sha256");
        auto *partition=esp_ota_get_next_update_partition(nullptr);
        if(validFirmwareManifest(schema,version,cJSON_IsString(board)?board->valuestring:nullptr,bytes,cJSON_IsString(hash)?hash->valuestring:nullptr,partition?partition->size:0)){
          if(firmwareReserve()){
            firmwareDownloading.store(true);firmwareNote("Đang tải và xác minh firmware");
            reboot=firmwareDownload(bytes,hash->valuestring);
            firmwareNote(reboot?"Cập nhật thành công; đang khởi động lại":"Cập nhật thất bại; giữ firmware hiện tại");
            if(!reboot)firmwareRelease();
          }else firmwareNote("Có bản mới; hoãn vì trạm đang bận");
        }else firmwareNote("Không có bản mới hơn hợp lệ cho bo này");
        cJSON_Delete(j);
      }
      if(reboot){vTaskDelay(pdMS_TO_TICKS(1500));ESP.restart();}
      firmwareDownloading.store(false);firmwareChecking.store(false);checkedAt=millis();
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
static void startFirmwareUpdate(){
  // ponytail: poll manifest every 60s while idle; one image, no background staging during jobs.
  if(xTaskCreate(firmwareUpdateTask,"firmware-update",12288,nullptr,1,nullptr)!=pdPASS)firmwareNote("Không khởi động được kiểm tra cập nhật");
}
