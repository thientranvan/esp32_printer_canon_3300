#pragma once
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform.h>
#include <esp_heap_caps.h>
#include <time.h>
#include "prepared_job.h"

// ponytail: one prepared job in PSRAM, <=4 MiB; VPS retains durable queue/files.
// No PDF renderer on ESP. Restart only after USB disconnect, from confirmed sheets.
static const char *cloudOrigin="https://quanly.lophocbinhminh.com";
static std::atomic<bool> cloudWorking{false}, cloudResolve{false}, cloudNeedsInspection{false};
static portMUX_TYPE cloudLock=portMUX_INITIALIZER_UNLOCKED;
struct CloudView { char state[24], error[128], job[65]; unsigned total, completed, submitted; int http, heartbeatHttp, claimHttp, eventHttp; bool progressKnown; };
static CloudView cloudView={"disabled","","",0,0,0,0,0,0,0,false};
static bool cloudBusy() { return cloudWorking.load(); }
static CloudView cloudSnapshot() { portENTER_CRITICAL(&cloudLock); auto v=cloudView; portEXIT_CRITICAL(&cloudLock); return v; }
static void cloudState(const char *state,const char *error="") {
  portENTER_CRITICAL(&cloudLock); strlcpy(cloudView.state,state,sizeof(cloudView.state)); strlcpy(cloudView.error,error,sizeof(cloudView.error)); portEXIT_CRITICAL(&cloudLock);
}
static String cloudStatusJSON() {
  auto v=cloudSnapshot();
  return String("{\"enabled\":")+(stationSettings.cloudEnabled?"true":"false")+",\"state\":"+jsonString(v.state)+
    ",\"error\":"+jsonString(v.error)+",\"http\":"+v.http+",\"heartbeat_http\":"+v.heartbeatHttp+",\"claim_http\":"+v.claimHttp+",\"event_http\":"+v.eventHttp+",\"job\":"+jsonString(v.job)+
    ",\"progress_known\":"+(v.progressKnown?"true":"false")+",\"total\":"+v.total+",\"submitted\":"+v.submitted+",\"completed\":"+v.completed+"}";
}
static String cloudText(cJSON *object,const char *key) {
  auto *v=cJSON_GetObjectItemCaseSensitive(object,key); return cJSON_IsString(v)?String(v->valuestring):String("");
}
static bool cloudIdentifier(const String &s,size_t minimum=1,size_t maximum=64) {
  if(s.length()<minimum || s.length()>maximum) return false;
  for(char c:s) if(!isalnum(uint8_t(c)) && c!='-' && c!='_') return false;
  return true;
}
#include "server_queue.h"
static String cloudPath(const String &suffix) { return "/api/print/v1/stations/"+stationID+suffix; }
static esp_http_client_handle_t cloudClient(const String &path,const String &lease="") {
  String url=String(cloudOrigin)+cloudPath(path);
  esp_http_client_config_t cfg={}; cfg.url=url.c_str(); cfg.crt_bundle_attach=esp_crt_bundle_attach;
  cfg.keep_alive_enable=true; cfg.timeout_ms=10000; cfg.disable_auto_redirect=true; cfg.buffer_size=2048;
  auto h=esp_http_client_init(&cfg);
  if(h) {
    String auth="Bearer "+String(stationSettings.cloudToken);
    esp_http_client_set_header(h,"Authorization",auth.c_str());
    esp_http_client_set_header(h,"Accept-Encoding","identity");
    if(lease.length())esp_http_client_set_header(h,"X-Print-Lease",lease.c_str());
  }
  return h;
}
// Bounded bodies, no redirects: a server response never chooses another host/path.
static int cloudPost(const String &path,const String &body,String &response,const String &lease="",esp_http_client_handle_t reusable=nullptr,String *diagnostic=nullptr) {
  response="";int status=-1;auto h=reusable?reusable:cloudClient(path,lease);
  auto note=[&](const char *stage,int result){if(diagnostic){char text[112];snprintf(text,sizeof(text),"%s=%d errno=%d heap=%u",stage,result,h?esp_http_client_get_errno(h):0,(unsigned)ESP.getFreeHeap());*diagnostic=text;}};
  if(diagnostic)*diagnostic="";
  if(!h)note("init",-1);
  if(h){
  esp_http_client_set_method(h,HTTP_METHOD_POST); esp_http_client_set_header(h,"Content-Type","application/json");
  int opened=esp_http_client_open(h,body.length());if(opened!=ESP_OK)note("open",opened);
  if(opened==ESP_OK) {
    size_t offset=0;
    while(offset<body.length()) { int n=esp_http_client_write(h,body.c_str()+offset,body.length()-offset); if(n<=0){note("write",n);break;} offset+=n; }
    int64_t headers=offset==body.length()?esp_http_client_fetch_headers(h):-1;
    if(headers<0 && offset==body.length())note("headers",int(headers));
    if(offset==body.length() && headers>=0) {
      status=esp_http_client_get_status_code(h); response=""; char b[512];
      // HTTP 204 has no body. Reading it may wait for nonexistent data and fail.
      while(status!=204 && response.length()<=8192) { int n=esp_http_client_read(h,b,sizeof(b)); if(n<0){note("read",n);status=-1;break;} if(!n)break; response.concat(b,n); }
      if(response.length()>8192)status=-1;
    }
  }
  if(!reusable){esp_http_client_close(h);esp_http_client_cleanup(h);}else if(status<0)esp_http_client_close(h);
  }
  portENTER_CRITICAL(&cloudLock);
  if(path=="/heartbeat")cloudView.heartbeatHttp=status;
  else {cloudView.http=status;if(path=="/jobs/claim")cloudView.claimHttp=status;else cloudView.eventHttp=status;}
  portEXIT_CRITICAL(&cloudLock);
  return status;
}
static String cloudBoot;
static std::atomic<bool> cloudOnline{false};
static std::atomic<uint32_t> cloudCommandSeq{0};
static std::atomic<int> cloudCommandResult{0};
static bool cloudHeartbeat(bool force=false) {return cloudOnline.load();}
static String jobStatusJSON(const PrintControl::Job &j) {
  return String("{\"recovery\":{\"automatic\":")+(j.automaticRecovery?"true":"false")+",\"confirmed_sheets\":"+(j.automaticRecovery?String(j.completedSheets):String("null"))+",\"next_sheet\":"+(j.automaticRecovery&&strcmp(j.state,"completed")?String(j.completedSheets+1):String("null"))+"},\"title\":"+jsonString(j.title)+",\"client_name\":"+jsonString(j.client)+",\"client_os\":"+jsonString(j.os)+",\"progress_known\":"+(j.total?"true":"false")+",\"progress_unit\":\"impressions\",\"actions\":{\"pause\":"+(j.canPause()?"true":"false")+",\"resume\":"+(j.canResume()?"true":"false")+",\"cancel\":"+(j.canCancel()?"true":"false")+",\"delete\":"+(j.canDelete()?"true":"false")+"},\"id\":"+jsonString(j.id)+",\"source\":"+jsonString(j.source)+",\"state\":"+jsonString(j.state)+
    ",\"request\":"+jsonString(j.request)+",\"active\":"+(j.active?"true":"false")+",\"controllable\":"+(j.controllable?"true":"false")+
    ",\"total\":"+j.total+",\"completed\":"+j.completed+"}";
}
static String jobStatusJSON() {return jobStatusJSON(jobSnapshot());}
static String cloudHeartbeatBody() {
  auto u=readUSB();auto j=jobSnapshot();bool lanJob=j.id[0] && !strcmp(j.source,"lan");
  String printer=u.recovering?"recovering":(u.error||captFault())?"error":!u.connected?"disconnected":(!u.responsive||(!captFresh()&&!captTCPState.load()&&!cloudWorking.load()))?"unresponsive":(lanJob&&j.active)?"busy_lan":cloudWorking.load()?"busy_cloud":"idle";
  String body="{\"schema\":1,\"status_source\":\"esp32\",\"boot_id\":"+jsonString(cloudBoot)+",\"name\":"+jsonString(stationName)+
    ",\"ip\":"+jsonString(WiFi.localIP().toString())+",\"uptime_s\":"+millis()/1000+
    ",\"printer\":{\"state\":"+jsonString(printer)+",\"ready_verified\":false,\"usb_connected\":"+(u.connected?"true":"false")+
    ",\"usb_responsive\":"+(u.responsive?"true":"false")+",\"error_code\":"+u.error+",\"capt\":"+captTelemetryJSON()+"},\"lan\":{\"active\":"+(lanJob&&j.active?"true":"false")+
    ",\"total\":"+String(lanJob?j.total:0)+",\"completed\":"+String(lanJob?j.completed:0)+"},\"cloud\":"+cloudStatusJSON()+",\"job\":"+jobStatusJSON(j)+",\"queue_command\":"+queueCommandJSON()+"}";
  return body;
}
// Dedicated network task: HTTPS cannot block USB engine/status or local controls.
static void cloudTelemetryTask(void *) {
  String previous;uint32_t sent=0,sequence=0;auto h=cloudClient("/heartbeat");
  if(!h){vTaskDelete(nullptr);return;}
  esp_http_client_set_timeout_ms(h,1500);
  while(true) {
    if(stationSettings.cloudEnabled && WiFi.isConnected() && time(nullptr)>1700000000) {
      String body=cloudHeartbeatBody();
      auto *j=cJSON_Parse(body.c_str());
      if(j){cJSON_DeleteItemFromObject(j,"uptime_s");auto *printer=cJSON_GetObjectItem(j,"printer");auto *capt=cJSON_GetObjectItem(printer,"capt");
        cJSON_DeleteItemFromObject(capt,"age_ms");auto *cloud=cJSON_GetObjectItem(j,"cloud");cJSON_DeleteItemFromObject(cloud,"http");}
      char *encoded=j?cJSON_PrintUnformatted(j):nullptr;String key=encoded?String(encoded):String("");free(encoded);cJSON_Delete(j);
      if(key!=previous || uint32_t(millis()-sent)>=1000) {
        body.remove(body.length()-1);body+=",\"telemetry_seq\":"+String(++sequence)+",\"command_ack\":{\"seq\":"+String(cloudCommandSeq.load())+",\"result\":"+jsonString(cloudCommandResult.load()==1?"accepted":cloudCommandResult.load()==2?"rejected":"none")+"}}";
        sent=millis();String response;int code=cloudPost("/heartbeat",body,response,"",h);cloudOnline.store(code==200||code==204);
        if(code==200){auto *r=cJSON_Parse(response.c_str());updateServerQueue(r);auto *cmd=cJSON_GetObjectItemCaseSensitive(r,"command");
          String target=cloudText(cmd,"job_id"),action=cloudText(cmd,"action");
          auto *n=cJSON_GetObjectItemCaseSensitive(cmd,"seq");double seq=cJSON_IsNumber(n)?n->valuedouble:0;
          if(cloudText(cmd,"boot_id")==cloudBoot && seq>=1 && seq<=2147483647 && seq==uint32_t(seq) && uint32_t(seq)>cloudCommandSeq.load()){
            bool ok=cloudIdentifier(target)&&jobCommand(target,action);cloudCommandResult.store(ok?1:2);cloudCommandSeq.store(uint32_t(seq));
          }cJSON_Delete(r);}
        if(code==200||code==204)previous=key;
      }
    }else cloudOnline.store(false);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
static bool cloudDownload(const String &id,const String &lease,uint8_t *data,size_t size,const String &expected) {
  auto h=cloudClient("/jobs/"+id+"/data",lease);if(!h)return false;
  bool ok=false;
  if(esp_http_client_open(h,0)==ESP_OK && esp_http_client_fetch_headers(h)==int64_t(size) && esp_http_client_get_status_code(h)==200) {
    size_t offset=0;uint32_t began=millis();
    while(offset<size && uint32_t(millis()-began)<120000) {
      int n=esp_http_client_read(h,reinterpret_cast<char *>(data+offset),min(size-offset,size_t(8192)));
      if(n<=0)break;offset+=n;cloudHeartbeat();
    }
    if(offset==size) {
      uint8_t hash[32];char text[65];
      if(mbedtls_sha256(data,size,hash,0)!=0){esp_http_client_close(h);esp_http_client_cleanup(h);return false;}
      for(int i=0;i<32;++i)snprintf(text+i*2,3,"%02x",hash[i]);
      ok=expected==text;
    }
  }
  esp_http_client_close(h);esp_http_client_cleanup(h);return ok;
}
// Cloud uses a reserved owner, renewed for every transfer; shares USB arbiter with LAN.
static constexpr uint32_t CloudOwner=CaptCloudOwner;
static bool cloudTransfer(uint8_t *data,int size,bool input,int &received,uint32_t owner=CloudOwner) {
  if(!claimCapt(owner) || readUSB().recovering)return false;
  int state=captCommand.state.load();
  if((state!=0 && state!=3)||!captCommand.state.compare_exchange_strong(state,5))return false;
  captCommand.size=size;captCommand.input=input;captCommand.received=0;captCommand.error=0;
  if(!input)memcpy(captCommand.data,data,size);captCommand.state.store(1);
  uint32_t start=millis();
  while(captCommand.state.load()!=3) { if(uint32_t(millis()-start)>8000)return false;vTaskDelay(pdMS_TO_TICKS(2)); }
  if(captCommand.error!=ESP_OK)return false;
  received=captCommand.received;if(input)memcpy(data,captCommand.data,received);return true;
}
static uint8_t cloudBuffer[4096];
static void cloudPut16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void cloudPut32(uint8_t *p,uint32_t n){cloudPut16(p,n);cloudPut16(p+2,n>>16);}
static bool captExchange(uint8_t *buffer,size_t capacity,uint32_t owner,unsigned op,const uint8_t *payload=nullptr,size_t size=0,bool reply=true) {
  if(size+4>capacity)return false;
  cloudPut16(buffer,op);cloudPut16(buffer+2,size+4);
  if(size)memcpy(buffer+4,payload,size);
  int n=0;if(!cloudTransfer(buffer,size+4,false,n,owner))return false;
  if(!reply)return true;
  if(!CaptStatus::reply(buffer,capacity,op,[owner](uint8_t *p,int size,int &n){return cloudTransfer(p,size,true,n,owner);}))return false;
  captRecordFrame(buffer,Prepared::u16(buffer+2));
  if(Prepared::u16(buffer+2)<6 || (buffer[4]&128))return false;
  return true;
}
static bool cloudCommand(unsigned op,const uint8_t *payload=nullptr,size_t size=0,bool reply=true) {
  return captExchange(cloudBuffer,sizeof(cloudBuffer),CloudOwner,op,payload,size,reply);
}
static PrintRecovery::Checkpoint cloudRecovery;
static bool cloudTracking=false;static uint32_t cloudFaultTime=0;
static bool cloudCounters(unsigned *c) {
  uint32_t waitStart=millis();bool waited=false;
  while(true){
    if(!strcmp(jobSnapshot().request,"cancel_requested"))return false;
    if(!cloudCommand(0xA0A8)||Prepared::u16(cloudBuffer+2)<40)return false;
    for(int i=0;i<4;++i)c[i]=Prepared::u16(cloudBuffer+18+i*2);
    if(cloudTracking&&!cloudRecovery.observe(c[3]))return false;
    unsigned done=cloudRecovery.confirmed;
    portENTER_CRITICAL(&cloudLock);cloudView.completed=done;portEXIT_CRITICAL(&cloudLock);
    auto j=jobSnapshot();if(j.active&&!strcmp(j.source,"cloud")){jobProgress(j.total,done);jobRecoveryProgress(cloudRecovery);}
    auto flags=CaptStatus::decode(cloudBuffer);bool blocked=flags.service||flags.jam||flags.cover||flags.paper;
    if(flags.service)cloudCommand(0xA2A2);
    if(!blocked){
      if(waited)cloudFaultTime+=uint32_t(millis()-waitStart);
      if(!strcmp(j.state,"waiting_printer")){
        portENTER_CRITICAL(&jobLock);strcpy(currentJob.state,"printing");portEXIT_CRITICAL(&jobLock);cloudState("printing");
      }
      return true;
    }
    portENTER_CRITICAL(&jobLock);strcpy(currentJob.state,"waiting_printer");portEXIT_CRITICAL(&jobLock);
    waited=true;cloudState("waiting_printer","Fix printer fault; printing continues automatically when ready");
    cloudHeartbeat();vTaskDelay(pdMS_TO_TICKS(200));
  }
}
static bool cloudWait(unsigned counter,unsigned value,uint32_t limit,bool exact=false) {
  uint32_t start=millis();unsigned c[4];
  do { uint32_t before=cloudFaultTime;if(!cloudCounters(c))return false;start+=uint32_t(cloudFaultTime-before);if(c[counter]>=value)return !exact || c[counter]==value;
    cloudHeartbeat();vTaskDelay(pdMS_TO_TICKS(200));
  }while(uint32_t(millis()-start)<limit);
  return false;
}
static bool cloudInfo(unsigned id,unsigned stage,unsigned page) {
  uint8_t b[72]={};cloudPut32(b+4,page);b[16]=stage;b[17]=1;cloudPut16(b+18,id);cloudPut16(b+20,65116);cloudPut16(b+22,65116);
  time_t now=time(nullptr);struct tm t;localtime_r(&now,&t);cloudPut16(b+24,t.tm_year+1900);
  b[26]=t.tm_mon;b[27]=t.tm_mday;b[28]=t.tm_hour;b[29]=t.tm_min;b[30]=t.tm_sec;b[31]=1;
  return cloudCommand(0xE1A1,b,sizeof(b));
}
// Pause only before the next page. Already buffered pages can still exit.
static bool cloudCheckpoint() {
  while(true){
    auto j=jobSnapshot();
    if(!strcmp(j.request,"cancel_requested"))return false;
    if(!strcmp(j.request,"resume_requested")) {
      portENTER_CRITICAL(&jobLock);if(currentJob.matches(j.id)){currentJob.request[0]=0;strcpy(currentJob.state,"printing");}portEXIT_CRITICAL(&jobLock);
      cloudState("printing");return true;
    }
    if(strcmp(j.request,"pause_requested"))return true;
    portENTER_CRITICAL(&jobLock);if(currentJob.matches(j.id))strcpy(currentJob.state,"paused");portEXIT_CRITICAL(&jobLock);
    cloudState("paused");unsigned c[4];if(!cloudCounters(c))return false;
    jobProgress(j.total,cloudRecovery.confirmed);vTaskDelay(pdMS_TO_TICKS(250));
  }
}
static bool cloudPrint(const Prepared::Job &job,unsigned base=0) {
  cloudTracking=false;
  if(!cloudCommand(0xA1A1) || Prepared::u16(cloudBuffer+2)<8 || Prepared::u16(cloudBuffer+6)!=0x2A94)return false;
  unsigned c[4];if(!cloudCounters(c)||c[0]!=c[1]||c[0]!=c[2]||c[0]!=c[3])return false;
  uint8_t reservation[8]={0,0,30};
  if(!cloudCommand(0xA3A2)||!cloudCommand(0xA2A0,reservation,8)||Prepared::u16(cloudBuffer+2)!=8)return false;
  unsigned id=Prepared::u16(cloudBuffer+6);
  uint8_t init[16]={};cloudPut32(init,0xADEADBEE);
  if(!cloudInfo(id,1,0)||!cloudCommand(0xE0A3)||!cloudCommand(0xE0A2)||!cloudCommand(0xE0A4)||!cloudCommand(0xE0A5,init,16))return false;
  cloudRecovery.started=true;cloudTracking=true;
  unsigned number=0;
  for(size_t index=base;index<job.plan.size();++index) {
    auto side=job.plan[index];
    if(!cloudCheckpoint())return false;
    ++number;if(number>2 && !cloudWait(1,number-2,60000))return false;
    if(!cloudCheckpoint())return false;
    const auto &page=job.pages[side.page];uint8_t params[64];memcpy(params,page.parameters,64);
    params[8]=2;params[9]=(params[9]&~6)|side.flag;params[10]=1;params[40]=1;
    uint8_t source[8]={2,1,0xB0,9,0xB3,13,1,uint8_t(params[9]&8)};
    if(!cloudCommand(0xE1A9,source,8)||Prepared::u16(cloudBuffer+2)!=6||Prepared::u16(cloudBuffer+4)!=0x100||
       !cloudCommand(0xD0A9,params,64,false))return false;
    for(auto band:page.bands) for(size_t p=0;p<band.size;p+=4092)
      if(!cloudCommand(0xC0A0,band.data+p,min(size_t(4092),band.size-p),false))return false;
    if(!cloudCommand(0xC0A4,nullptr,0,false)||!cloudWait(0,number,20000,true)||!cloudInfo(id,2,number))return false;
    if(!cloudCheckpoint())return false;
    uint8_t n[2];cloudPut16(n,number);
    if(!cloudCommand(0xE0A7,n,2)||!cloudInfo(id,6,number))return false;
    portENTER_CRITICAL(&cloudLock);cloudView.submitted=base+number;portEXIT_CRITICAL(&cloudLock);cloudHeartbeat();
  }
  if(!cloudWait(3,number,60000)||!cloudInfo(id,4,number))return false;
  uint8_t release[2];cloudPut16(release,id);return cloudCommand(0xE0A9,release,2);
}
static void cloudTask(void *) {
  Preferences journal;
  if(!journal.begin("print-cloud",false)){cloudState("blocked","Cannot open durable journal");vTaskDelete(nullptr);return;}
  String id,lease,state,error;bool deleteAfterAck=false;
  auto persist=[&]() {
    auto v=cloudSnapshot();String value="{\"id\":"+jsonString(id)+",\"lease\":"+jsonString(lease)+",\"state\":"+jsonString(state)+
      ",\"error\":"+jsonString(error)+",\"total\":"+v.total+",\"completed\":"+v.completed+"}";
    return journal.putString("job",value)==value.length();
  };
  String saved=journal.getString("job","");
  if(saved.length()) {
    auto *j=cJSON_Parse(saved.c_str());id=cloudText(j,"id");lease=cloudText(j,"lease");state=cloudText(j,"state");error=cloudText(j,"error");
    if(!cloudIdentifier(id)||!cloudIdentifier(lease,16,128)||(state!="completed"&&state!="failed"&&state!="uncertain"&&state!="resolved")) {
      cJSON_Delete(j);cloudState("blocked","Invalid journal; manual recovery required");vTaskDelete(nullptr);return;
    }
    auto total=cJSON_GetObjectItem(j,"total"),completed=cJSON_GetObjectItem(j,"completed");
    portENTER_CRITICAL(&cloudLock);strlcpy(cloudView.job,id.c_str(),sizeof(cloudView.job));
    cloudView.progressKnown=state=="completed";cloudView.total=cJSON_IsNumber(total)?total->valueint:0;cloudView.completed=cJSON_IsNumber(completed)?completed->valueint:0;portEXIT_CRITICAL(&cloudLock);
    cloudNeedsInspection.store(state=="uncertain");jobBegin(id.c_str(),"cloud",true);jobProgress(cloudSnapshot().total,cloudSnapshot().completed);jobFinish(state.c_str());cJSON_Delete(j);
  }

  while(true) {
      if(state=="uncertain" && (cloudResolve.exchange(false)||jobSnapshot().stopping())) {
        auto old=jobSnapshot();deleteAfterAck=old.deleteAfterStop;
        cloudNeedsInspection.store(false);jobFinish(old.stopping()?"cancelled":"resolved");state="resolved";error=old.stopping()?"Operator discarded interrupted job; no data resent; buffered paper may still print":"Operator inspected printer; no automatic reprint";
        if(!persist()){cloudState("blocked","Cannot persist resolution");vTaskDelete(nullptr);return;}
      }
    if(!stationSettings.cloudEnabled){vTaskDelay(pdMS_TO_TICKS(1500));continue;}
    if(WiFi.status()!=WL_CONNECTED || time(nullptr)<1700000000){cloudState("connecting","Waiting for Wi-Fi / trusted clock");vTaskDelay(pdMS_TO_TICKS(1000));continue;}
    if(id.length())cloudState(state.c_str(),error.c_str());
    bool online=cloudHeartbeat(true);
    if(!online){auto code=cloudSnapshot().heartbeatHttp;String message=String("Heartbeat failed (HTTP ")+code+")";cloudState(code==0?"connecting":"server_error",code==0?"Waiting for initial heartbeat":message.c_str());vTaskDelay(pdMS_TO_TICKS(250));continue;}
    if(!id.length()) {
      auto recovered=cloudSnapshot();
      if(!strcmp(recovered.state,"connecting") ||
         (!strcmp(recovered.state,"server_error") && !strncmp(recovered.error,"Heartbeat failed (HTTP ",22)))
        cloudState("idle");
    }
    if(id.length()) {
      auto v=cloudSnapshot();String response;
      String event="{\"schema\":1,\"state\":"+jsonString(state)+",\"error\":"+jsonString(error)+",\"progress_known\":"+(v.progressKnown?"true":"false")+",\"total\":"+v.total+",\"completed\":"+v.completed+"}";
      int code=cloudPost("/jobs/"+id+"/events",event,response,lease);
      if(code==204 && state!="uncertain") {
        if(!journal.remove("job")){cloudState("blocked","Cannot clear acknowledged journal");vTaskDelete(nullptr);return;}
        if(deleteAfterAck){jobCommand(id,"delete");deleteAfterAck=false;}
        id="";lease="";state="";error="";
        portENTER_CRITICAL(&cloudLock);cloudView.job[0]=0;cloudView.progressKnown=false;cloudView.total=cloudView.completed=cloudView.submitted=0;portEXIT_CRITICAL(&cloudLock);
      }
    }else if(captFresh() && !captFault() && readUSB().connected && readUSB().responsive && !readUSB().recovering && !captTCPState.load() && strcmp(jobSnapshot().state,"uncertain") && strcmp(jobSnapshot().state,"waiting_printer")) {
      String response,detail;int code=cloudPost("/jobs/claim","{\"schema\":1,\"max_bytes\":4194304,\"format\":\"espjob-v1\"}",response,"",nullptr,&detail);
      if(code==200) {
        auto *j=cJSON_Parse(response.c_str());id=cloudText(j,"id");lease=cloudText(j,"lease");String hash=cloudText(j,"sha256"),title=cloudText(j,"title");if(title.length()>128)title="";
        auto *n=cJSON_GetObjectItem(j,"bytes");double bytes=cJSON_IsNumber(n)?n->valuedouble:0;
        bool valid=cloudIdentifier(id)&&cloudIdentifier(lease,16,128)&&cloudText(j,"format")=="espjob-v1"&&hash.length()==64&&bytes>=16&&bytes<=Prepared::Limit&&bytes==size_t(bytes);
        for(char c:hash)if(!isxdigit(uint8_t(c))||isupper(uint8_t(c)))valid=false;
        cJSON_Delete(j);
        if(!valid){id="";lease="";cloudState("server_error","Invalid claim response");vTaskDelay(pdMS_TO_TICKS(5000));continue;}
        portENTER_CRITICAL(&cloudLock);strlcpy(cloudView.job,id.c_str(),sizeof(cloudView.job));cloudView.progressKnown=false;cloudView.total=cloudView.completed=cloudView.submitted=0;portEXIT_CRITICAL(&cloudLock);
        state="failed";error="Interrupted before USB; no pages sent";
        if(!persist()){cloudState("blocked","Cannot persist claimed job");vTaskDelete(nullptr);return;}
        cloudWorking.store(true);cloudState("downloading");
        auto *data=static_cast<uint8_t *>(ps_malloc(size_t(bytes)));Prepared::Job job;
        bool validData=data && cloudDownload(id,lease,data,size_t(bytes),hash) && Prepared::parse(data,size_t(bytes),job);
        bool acquired=false;
        if(validData) {
          cloudState("waiting_printer");
          while(!(acquired=claimCapt(CloudOwner))) {cloudHeartbeat();vTaskDelay(pdMS_TO_TICKS(200));}
        }
        if(validData && acquired) {
          portENTER_CRITICAL(&cloudLock);cloudView.total=job.plan.size();cloudView.progressKnown=true;portEXIT_CRITICAL(&cloudLock);
          cloudNeedsInspection.store(true);state="uncertain";error="USB submission may have started; inspect before reprinting";
          if(!persist()){free(data);releaseCapt(CloudOwner);cloudState("blocked","Cannot persist before USB");vTaskDelete(nullptr);return;}
          jobBegin(id.c_str(),"cloud",true);jobMetadata(title.c_str(),"Web server","web");jobProgress(job.plan.size(),0);
          std::vector<uint8_t> flags;for(auto side:job.plan)flags.push_back(side.flag);
          cloudRecovery.begin(id.c_str(),flags,printerEpoch.load());jobRecoveryProgress(cloudRecovery);
          cloudState("printing");bool done=cloudPrint(job);
          while(!done){
            cloudTracking=false;releaseCapt(CloudOwner);
            auto control=jobSnapshot();if(!strcmp(control.request,"cancel_requested"))break;
            portENTER_CRITICAL(&jobLock);strcpy(currentJob.state,"waiting_printer");portEXIT_CRITICAL(&jobLock);
            cloudState("waiting_printer","Printer interrupted; waiting for restart to reprint incomplete sheet");
            bool ready=false;
            while(!ready){
              control=jobSnapshot();if(!strcmp(control.request,"cancel_requested"))break;
              auto usb=readUSB();
              ready=printerEpoch.load()!=cloudRecovery.epoch&&usb.connected&&usb.responsive&&!usb.recovering&&captFresh()&&!captFault();
              if(!ready){cloudHeartbeat();vTaskDelay(pdMS_TO_TICKS(200));}
            }
            if(!ready)break;
            while(!claimCapt(CloudOwner)){cloudHeartbeat();vTaskDelay(pdMS_TO_TICKS(50));}
            if(!cloudRecovery.restart(printerEpoch.load()))break;
            portENTER_CRITICAL(&jobLock);strcpy(currentJob.state,"printing");portEXIT_CRITICAL(&jobLock);
            cloudState("printing");done=cloudPrint(job,cloudRecovery.base);
          }
          cloudTracking=false;
          if(done){cloudNeedsInspection.store(false);state="completed";error="";}
          auto stopped=jobSnapshot();
          if(!done&&stopped.stopping()){
            state="resolved";error="Operator cancelled submission; buffered paper may still print";cloudNeedsInspection.store(false);deleteAfterAck=stopped.deleteAfterStop;
          }
          jobFinish(done?"completed":stopped.stopping()?"cancelled":"uncertain");
          releaseCapt(CloudOwner);
        }else {state="failed";error="Download, checksum, memory or format validation failed; no pages sent";}
        free(data);
        if(!persist()){cloudState("blocked","Cannot persist outcome; journal retained");vTaskDelete(nullptr);return;}
        cloudWorking.store(false);
      }else if(code==204)cloudState("idle");
      else {String message=String("Claim failed (HTTP ")+code+"; "+detail+")";cloudState("server_error",message.c_str());}
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
static constexpr uint32_t CaptStatusOwner=0xFFFFFFFD;
static void captStatusTask(void *) {
  uint8_t buffer[80];unsigned previous=~0U;
  while(true){
    if(readUSB().responsive && claimCapt(CaptStatusOwner)){
      if(captExchange(buffer,sizeof(buffer),CaptStatusOwner,0xA0A8) && CaptStatus::word(buffer+2)>=40){
        auto f=CaptStatus::decode(buffer);unsigned code=0,sub=0;char service[24]="unknown";
        if(f.service && captExchange(buffer,sizeof(buffer),CaptStatusOwner,0xA2A2) && CaptStatus::word(buffer+2)>=12 && buffer[5]){
          code=CaptStatus::word(buffer+8);sub=CaptStatus::word(buffer+10);snprintf(service,sizeof(service),"E%03X %04X",code,sub);
        }
        unsigned key=f.service|f.jam<<1|f.cover<<2|f.paper<<3|f.toner<<4|code<<8;
        if(key!=previous){Serial.printf("CAPT status service=%u code=%s jam=%u cover=%u paper=%u toner=%u completed=%u\n",f.service,service,f.jam,f.cover,f.paper,f.toner,f.completed);previous=key;}
      }
      releaseCapt(CaptStatusOwner);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
static void startCloud() {
  restoreQueueCommand();
  if(healthy && xTaskCreate(captStatusTask,"printer-status",8192,nullptr,1,nullptr)!=pdPASS)Serial.println("CAPT status task failed");
  if(!stationSettings.cloudEnabled)return;
  if(!healthy){cloudState("blocked","Station self-test failed");return;}
  // TLS contexts overlap for realtime telemetry and job downloads. Keep their
  // buffers in PSRAM; install allocator before either HTTPS task starts.
  if(!psramFound() || mbedtls_platform_set_calloc_free(
      [](size_t count,size_t size)->void * {return heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);},
      heap_caps_free)!=0){cloudState("blocked","Cannot initialize TLS PSRAM allocator");return;}
  cloudBoot=String(esp_random(),HEX)+String(esp_random(),HEX);configTime(0,0,"pool.ntp.org","time.google.com");
  if(xTaskCreate(cloudTelemetryTask,"print-telemetry",12288,nullptr,1,nullptr)!=pdPASS){cloudState("blocked","Cannot start telemetry");return;}
  if(xTaskCreate(cloudTask,"print-cloud",16384,nullptr,1,nullptr)!=pdPASS)cloudState("blocked","Cannot start cloud task");
}
