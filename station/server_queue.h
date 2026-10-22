#pragma once
// VPS owns durable queued documents; ESP caches only bounded metadata.
static portMUX_TYPE serverQueueLock=portMUX_INITIALIZER_UNLOCKED;
static char serverQueueJSON[4097]="{\"supported\":false,\"items\":[]}";
static char queueRequestId[65]={},queueJob[65]={},queueAction[8]={},queueRevision[65]={},queueResult[24]={};
static uint32_t serverQueueAt=0;
static String serverQueueSnapshot(){char copy[4097];portENTER_CRITICAL(&serverQueueLock);memcpy(copy,serverQueueJSON,sizeof(copy));uint32_t at=serverQueueAt;portEXIT_CRITICAL(&serverQueueLock);String s(copy);s.remove(s.length()-1);return s+",\"age_ms\":"+(at?String(uint32_t(millis()-at)):"null")+"}";}
static String queueCommandJSON(){char id[65],job[65],action[8],revision[65],result[24];portENTER_CRITICAL(&serverQueueLock);memcpy(id,queueRequestId,sizeof(id));memcpy(job,queueJob,sizeof(job));memcpy(action,queueAction,sizeof(action));memcpy(revision,queueRevision,sizeof(revision));memcpy(result,queueResult,sizeof(result));portEXIT_CRITICAL(&serverQueueLock);return "{\"id\":"+jsonString(id)+",\"job_id\":"+jsonString(job)+",\"action\":"+jsonString(action)+",\"revision\":"+jsonString(revision)+",\"result\":"+jsonString(result)+"}";}
static bool submitQueueCommand(const String &id,const String &action){
 if(!cloudIdentifier(id)||(action!="pause"&&action!="resume"&&action!="cancel"&&action!="delete"))return false;
 String snapshot=serverQueueSnapshot();auto *q=cJSON_Parse(snapshot.c_str());auto *items=cJSON_GetObjectItem(q,"items");bool found=false;
 for(int i=0;i<cJSON_GetArraySize(items);++i)if(cloudText(cJSON_GetArrayItem(items,i),"id")==id)found=true;
 String revision=cloudText(q,"revision");cJSON_Delete(q);
 if(!found || !revision.length())return false;
 char request[65];snprintf(request,sizeof(request),"queue-%08lx-%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
 portENTER_CRITICAL(&serverQueueLock);
 bool ok=(!queueResult[0]||!strcmp(queueResult,"accepted")||!strcmp(queueResult,"rejected")) && serverQueueAt && uint32_t(millis()-serverQueueAt)<5000;
 if(ok){strlcpy(queueRequestId,request,sizeof(queueRequestId));strlcpy(queueJob,id.c_str(),sizeof(queueJob));strlcpy(queueAction,action.c_str(),sizeof(queueAction));strlcpy(queueRevision,revision.c_str(),sizeof(queueRevision));strcpy(queueResult,"preparing");}
 portEXIT_CRITICAL(&serverQueueLock);
 if(ok){String saved=queueCommandJSON();Preferences journal;bool stored=journal.begin("print-queue",false)&&journal.putString("request",saved)==saved.length();journal.end();
  portENTER_CRITICAL(&serverQueueLock);strcpy(queueResult,stored?"pending":"rejected");portEXIT_CRITICAL(&serverQueueLock);return stored;}
 return false;
}
static void updateServerQueue(cJSON *response){
 auto *ack=cJSON_GetObjectItemCaseSensitive(response,"queue_ack");String id=cloudText(ack,"id"),result=cloudText(ack,"result");
 if(result=="accepted"||result=="rejected"){
  portENTER_CRITICAL(&serverQueueLock);bool matched=id==queueRequestId&&!strcmp(queueResult,"pending");if(matched)strcpy(queueResult,"acknowledging");portEXIT_CRITICAL(&serverQueueLock);
  if(matched){Preferences journal;bool removed=journal.begin("print-queue",false)&&journal.remove("request");journal.end();portENTER_CRITICAL(&serverQueueLock);strlcpy(queueResult,removed?result.c_str():"pending",sizeof(queueResult));portEXIT_CRITICAL(&serverQueueLock);}
 }
 auto *q=cJSON_GetObjectItemCaseSensitive(response,"queue");if(!cJSON_IsObject(q))return;
 auto *items=cJSON_GetObjectItemCaseSensitive(q,"items");String revision=cloudText(q,"revision");
 if(!cloudIdentifier(revision)||!cJSON_IsArray(items)||cJSON_GetArraySize(items)>16)return;
 auto *safe=cJSON_CreateObject();cJSON_AddBoolToObject(safe,"supported",true);cJSON_AddStringToObject(safe,"revision",revision.c_str());
 cJSON_AddBoolToObject(safe,"truncated",cJSON_IsTrue(cJSON_GetObjectItem(q,"truncated")));auto *list=cJSON_AddArrayToObject(safe,"items");
 bool valid=true;for(int i=0;i<cJSON_GetArraySize(items);++i){auto *in=cJSON_GetArrayItem(items,i);String job=cloudText(in,"id"),state=cloudText(in,"state"),title=cloudText(in,"title");
  if(!cloudIdentifier(job)||!cloudIdentifier(state,1,24)||title.length()>128){valid=false;break;}
  auto *out=cJSON_CreateObject();cJSON_AddStringToObject(out,"id",job.c_str());cJSON_AddStringToObject(out,"state",state.c_str());cJSON_AddStringToObject(out,"title",title.c_str());cJSON_AddItemToArray(list,out);
 }
 char *text=valid?cJSON_PrintUnformatted(safe):nullptr;
 if(text && strlen(text)<=4096){portENTER_CRITICAL(&serverQueueLock);strlcpy(serverQueueJSON,text,sizeof(serverQueueJSON));serverQueueAt=millis();portEXIT_CRITICAL(&serverQueueLock);}
 free(text);cJSON_Delete(safe);
}

static void restoreQueueCommand(){
 Preferences journal;if(!journal.begin("print-queue",false))return;String saved=journal.getString("request","");journal.end();if(!saved.length())return;
 auto *j=cJSON_Parse(saved.c_str());String id=cloudText(j,"id"),job=cloudText(j,"job_id"),action=cloudText(j,"action"),revision=cloudText(j,"revision");cJSON_Delete(j);
 if(!cloudIdentifier(id)||!cloudIdentifier(job)||!cloudIdentifier(revision)||(action!="pause"&&action!="resume"&&action!="cancel"&&action!="delete"))return;
 strlcpy(queueRequestId,id.c_str(),sizeof(queueRequestId));strlcpy(queueJob,job.c_str(),sizeof(queueJob));strlcpy(queueAction,action.c_str(),sizeof(queueAction));strlcpy(queueRevision,revision.c_str(),sizeof(queueRevision));strcpy(queueResult,"pending");
}
