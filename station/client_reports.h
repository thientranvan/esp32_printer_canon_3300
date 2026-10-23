#pragma once
#include <cJSON.h>
// ponytail: four LAN reporters, four visible jobs each; durable queues stay on OS.
// Reports describe spoolers, never confirm physical output or authorize controls.
struct ClientReport { char id[65]={}, ip[16]={}, json[2049]={}; uint32_t at=0; };
static ClientReport clientReports[4];
static portMUX_TYPE clientReportsLock=portMUX_INITIALIZER_UNLOCKED;
static bool reportText(cJSON *j,const char *key,size_t maximum,bool required=false) {
 auto *v=cJSON_GetObjectItemCaseSensitive(j,key);
 if(!v)return !required;
 if(!cJSON_IsString(v)||strlen(v->valuestring)>maximum)return false;
 for(const unsigned char *p=(const unsigned char *)v->valuestring;*p;++p)if(*p<32||*p==127)return false;
 return !required||v->valuestring[0];
}
static bool storeClientReport(const String &body,const String &ip) {
 if(body.length()>4096)return false;
 auto *j=cJSON_Parse(body.c_str());
 bool valid=cJSON_IsObject(j)&&reportText(j,"id",64,true)&&reportText(j,"name",96,true)&&reportText(j,"os",8,true)&&reportText(j,"queue",96,true);
 String id=valid?cJSON_GetObjectItem(j,"id")->valuestring:"";
 for(char c:id)if(!isalnum(uint8_t(c))&&c!='-'&&c!='_')valid=false;
 String os=valid?cJSON_GetObjectItem(j,"os")->valuestring:"";
 valid=valid&&(os=="windows"||os=="macos");
 auto *jobs=cJSON_GetObjectItemCaseSensitive(j,"jobs"),*truncated=cJSON_GetObjectItemCaseSensitive(j,"truncated");
 valid=valid&&cJSON_IsArray(jobs)&&cJSON_GetArraySize(jobs)<=4&&cJSON_IsBool(truncated);
 auto *safe=cJSON_CreateObject();
 if(valid){
  for(auto key:{"id","name","os","queue"})cJSON_AddStringToObject(safe,key,cJSON_GetObjectItem(j,key)->valuestring);
  cJSON_AddBoolToObject(safe,"truncated",cJSON_IsTrue(truncated));
  auto *captured=cJSON_GetObjectItemCaseSensitive(j,"captured_jobs_count"),*pending=cJSON_GetObjectItemCaseSensitive(j,"unresolved_submission");
  if(captured&&!cJSON_IsNull(captured)&&(!cJSON_IsNumber(captured)||captured->valuedouble<0||captured->valuedouble>1000000||captured->valuedouble!=captured->valueint))valid=false;
  if(pending&&!cJSON_IsNull(pending)&&!cJSON_IsBool(pending))valid=false;
  if(cJSON_IsNumber(captured))cJSON_AddNumberToObject(safe,"captured_jobs_count",captured->valueint);else cJSON_AddNullToObject(safe,"captured_jobs_count");
  if(cJSON_IsBool(pending))cJSON_AddBoolToObject(safe,"unresolved_submission",cJSON_IsTrue(pending));else cJSON_AddNullToObject(safe,"unresolved_submission");
  auto *list=cJSON_AddArrayToObject(safe,"jobs");
  for(int i=0;i<cJSON_GetArraySize(jobs);++i){
   auto *in=cJSON_GetArrayItem(jobs,i);
   if(!reportText(in,"id",64,true)||!reportText(in,"title",128)||!reportText(in,"state",64,true)){valid=false;break;}
   auto *out=cJSON_CreateObject();
   for(auto key:{"id","title","state"}){auto *v=cJSON_GetObjectItemCaseSensitive(in,key);cJSON_AddStringToObject(out,key,cJSON_IsString(v)?v->valuestring:"");}
   for(auto key:{"total","completed"}){auto *v=cJSON_GetObjectItemCaseSensitive(in,key);if(v&&!cJSON_IsNull(v)&&(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>1000000||v->valuedouble!=v->valueint))valid=false;if(cJSON_IsNumber(v))cJSON_AddNumberToObject(out,key,v->valueint);else cJSON_AddNullToObject(out,key);}
   auto *stage=cJSON_GetObjectItemCaseSensitive(in,"stage");bool bridge=cJSON_IsString(stage)&&!strcmp(stage->valuestring,"bridge");
   if(stage&&(!cJSON_IsString(stage)||(strcmp(stage->valuestring,"spooler")&&strcmp(stage->valuestring,"bridge"))))valid=false;
   cJSON_AddStringToObject(out,"stage",bridge?"bridge":"spooler");cJSON_AddStringToObject(out,"progress_basis",bridge?"bridge_capture":"os_spooler");cJSON_AddBoolToObject(out,"remote_controllable",false);cJSON_AddItemToArray(list,out);
  }
 }
 char *text=valid?cJSON_PrintUnformatted(safe):nullptr;bool stored=false;
 if(text&&strlen(text)<=2048){
  portENTER_CRITICAL(&clientReportsLock);int slot=-1;
  bool conflict=false;for(int i=0;i<4;++i)if(id==clientReports[i].id){if(ip==clientReports[i].ip||uint32_t(millis()-clientReports[i].at)>15000)slot=i;else conflict=true;break;}
  if(slot<0&&!conflict)for(int i=0;i<4;++i)if(!clientReports[i].at||uint32_t(millis()-clientReports[i].at)>15000){slot=i;break;}
  if(slot>=0){auto &r=clientReports[slot];strlcpy(r.id,id.c_str(),sizeof(r.id));strlcpy(r.ip,ip.c_str(),sizeof(r.ip));strlcpy(r.json,text,sizeof(r.json));r.at=millis();stored=true;}
  portEXIT_CRITICAL(&clientReportsLock);
 }
 free(text);cJSON_Delete(safe);cJSON_Delete(j);return stored;
}
static String clientReportsJSON() {
 String result="{\"data_source\":\"lan_client_report\",\"scope\":\"reporting_clients_only\",\"ttl_ms\":15000,\"items\":[";bool comma=false;
 for(int i=0;i<4;++i){char text[2049];uint32_t at;
  portENTER_CRITICAL(&clientReportsLock);at=clientReports[i].at;memcpy(text,clientReports[i].json,sizeof(text));portEXIT_CRITICAL(&clientReportsLock);
  if(!at)continue;if(comma)result+=",";comma=true;
  String item(text);item.remove(item.length()-1);result+=item+",\"last_report_uptime_ms\":"+String(at)+",\"stale\":"+(uint32_t(millis()-at)>15000?"true":"false")+"}";
 }
 return result+"]}";
}
