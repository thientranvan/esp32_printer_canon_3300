#pragma once
#include <cstring>
#include <cstdint>
namespace ClientEnrollment {
inline bool hex(const char *s,size_t n){if(!s||strlen(s)!=n)return false;for(size_t i=0;i<n;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
inline bool valid(const char *id,const char *challenge,const char *os,const char *name){
 if(!hex(id,32)||!hex(challenge,64)||!os||!name||!name[0]||strlen(name)>96)return false;
 if(strcmp(os,"windows")&&strcmp(os,"macos")&&strcmp(os,"linux"))return false;
 for(const unsigned char *p=(const unsigned char*)name;*p;++p)if(*p<32||*p==127)return false;
 return true;
}
inline bool ticket(const char *s){if(!s||strlen(s)<32||strlen(s)>128)return false;for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-'))return false;return true;}
}
#ifndef ENROLLMENT_VALIDATION_ONLY
// LAN carries only a challenge and a short-lived ticket; verifier/token stay on HTTPS.
// ponytail: one installer/20s, idle-only; add a bounded queue if concurrent installs matter.
static portMUX_TYPE enrollmentLock=portMUX_INITIALIZER_UNLOCKED;
struct Enrollment {char id[33]={},challenge[65]={},os[8]={},name[97]={},ticket[129]={};uint32_t peer=0,at=0;int code=0;bool busy=false;};
static Enrollment enrollment;
static void enrollmentTask(void*){
 portENTER_CRITICAL(&enrollmentLock);auto e=enrollment;portEXIT_CRITICAL(&enrollmentLock);
 String body=String("{\"schema\":1,\"client_id\":")+jsonString(e.id)+",\"challenge\":"+jsonString(e.challenge)+",\"client_os\":"+jsonString(e.os)+",\"name\":"+jsonString(e.name)+"}",response;
 int code=cloudPost("/clients/enroll",body,response);auto *j=cJSON_Parse(response.c_str());
 auto *t=cJSON_GetObjectItem(j,"ticket"),*schema=cJSON_GetObjectItem(j,"schema");
 bool ok=(code==200||code==201)&&cJSON_IsNumber(schema)&&schema->valuedouble==1&&cJSON_IsString(t)&&ClientEnrollment::ticket(t->valuestring);
 portENTER_CRITICAL(&enrollmentLock);enrollment.code=ok?200:(code==403?403:503);if(ok)strlcpy(enrollment.ticket,t->valuestring,sizeof(enrollment.ticket));enrollment.busy=false;portEXIT_CRITICAL(&enrollmentLock);
 cJSON_Delete(j);firmwareRelease();vTaskDelete(nullptr);
}
static void enrollClient(){
 server.sendHeader("Cache-Control","no-store");
 uint32_t mask=uint32_t(WiFi.subnetMask()),peer=uint32_t(server.client().remoteIP());
 if(!WiFi.isConnected()||!mask||(peer&mask)!=(uint32_t(WiFi.localIP())&mask)||server.hasHeader("Origin")||server.header("X-ESP32-Enrollment")!="1"){
  server.send(403,"text/plain","Native LAN installer only");return;
 }
 if(!stationSettings.cloudEnabled||!healthy||time(nullptr)<1700000000){server.send(503,"text/plain","Cloud not ready; LAN printing remains available");return;}
 String raw=server.arg("plain");if(raw.length()>512){server.send(413,"text/plain","Enrollment request too large");return;}
 auto *j=cJSON_Parse(raw.c_str());String id=cloudText(j,"client_id"),challenge=cloudText(j,"challenge"),os=cloudText(j,"client_os"),name=cloudText(j,"name");auto *schema=cJSON_GetObjectItem(j,"schema");
 bool valid=cJSON_IsNumber(schema)&&schema->valuedouble==1&&ClientEnrollment::valid(id.c_str(),challenge.c_str(),os.c_str(),name.c_str());cJSON_Delete(j);
 if(!valid){server.send(400,"text/plain","Invalid enrollment request");return;}
 portENTER_CRITICAL(&enrollmentLock);auto old=enrollment;portEXIT_CRITICAL(&enrollmentLock);
 bool same=old.peer==peer&&id==old.id&&challenge==old.challenge&&os==old.os&&name==old.name;
 if(old.id[0]&&uint32_t(millis()-old.at)<60000&&same){
  if(old.busy){server.send(202,"application/json","{\"schema\":1,\"state\":\"pending\"}");return;}
  server.send(old.code,"application/json",old.code==200?String("{\"schema\":1,\"station_id\":")+jsonString(stationID)+",\"client_id\":"+jsonString(id)+",\"challenge\":"+jsonString(challenge)+",\"ticket\":"+jsonString(old.ticket)+"}":"{\"error\":\"ENROLLMENT_UNAVAILABLE\"}");return;
 }
 if(old.busy||(old.id[0]&&uint32_t(millis()-old.at)<20000)){server.send(429,"text/plain","Enrollment busy; retry later");return;}
 if(!firmwareReserve()){server.send(503,"text/plain","Station busy; retry when idle");return;}
 Enrollment next;strlcpy(next.id,id.c_str(),sizeof(next.id));strlcpy(next.challenge,challenge.c_str(),sizeof(next.challenge));strlcpy(next.os,os.c_str(),sizeof(next.os));strlcpy(next.name,name.c_str(),sizeof(next.name));next.at=millis();next.peer=peer;next.busy=true;
 portENTER_CRITICAL(&enrollmentLock);enrollment=next;portEXIT_CRITICAL(&enrollmentLock);
 if(xTaskCreate(enrollmentTask,"client-enroll",12288,nullptr,1,nullptr)!=pdPASS){portENTER_CRITICAL(&enrollmentLock);enrollment.busy=false;enrollment.code=503;portEXIT_CRITICAL(&enrollmentLock);firmwareRelease();server.send(503,"text/plain","Cannot start enrollment");return;}
 server.send(202,"application/json","{\"schema\":1,\"state\":\"pending\"}");
}
#endif
