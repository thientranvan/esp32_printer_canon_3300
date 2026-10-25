#pragma once
#include <cstring>
#include <cstdint>

namespace PrintControl {
// One executing job. Durable waiting queues remain on their source (OS/VPS).
struct Job {
 char id[65]={}, source[8]={}, state[24]={}, request[24]={};
 char title[129]={}, client[97]={}, os[9]={};
 bool waiting() const {return !std::strcmp(state,"waiting_printer");}
 bool unresolved() const {return !std::strcmp(state,"uncertain");}
 bool stopping() const {return !std::strcmp(request,"cancel_requested");}
 bool canCancel() const {return id[0]&&controllable&&!stopping()&&(active||waiting()||unresolved());}
 bool canPause() const {return canCancel()&&!unresolved()&&std::strcmp(state,"paused")&&std::strcmp(request,"pause_requested");}
 bool canResume() const {return canCancel()&&!unresolved()&&(waiting()||!std::strcmp(state,"paused")||!std::strcmp(request,"pause_requested"));}
 bool canDelete() const {return id[0]&&!stopping()&&(!active||controllable);}
 void metadata(const char *name,const char *host,const char *platform){std::strncpy(title,name,128);title[128]=0;std::strncpy(client,host,96);client[96]=0;std::strncpy(os,platform,8);os[8]=0;}
 unsigned total=0, completed=0, completedSheets=0;bool automaticRecovery=false, deleteAfterStop=false; bool active=false, controllable=false;
 bool matches(const char *target) const {return id[0] && !std::strcmp(id,target);}
 void begin(const char *target,const char *origin,bool control=false) {
  *this=Job();std::strncpy(id,target,64);std::strncpy(source,origin,7);
  std::strcpy(state,"printing");active=true;controllable=control;
 }
 bool rejectBeforeStart(const char *target,const char *origin,unsigned count,bool control=false) {
  if(active || !std::strcmp(state,"uncertain") || !std::strcmp(state,"waiting_printer"))return false;
  if(!matches(target)){begin(target,origin,control);total=count;finish("failed");}
  return true;
 }
 bool command(const char *target,const char *action) {
  if(!matches(target))return false;
  if(!std::strcmp(action,"delete")) {
   if(!canDelete())return false;
   if(active||waiting()||unresolved()){deleteAfterStop=true;std::strcpy(request,"cancel_requested");return true;}
   *this=Job();return true;
  }
  if(!controllable || !std::strcmp(request,"cancel_requested"))return false;
  if(!active&&!waiting()&&!unresolved())return false;
  if(unresolved()&&std::strcmp(action,"cancel"))return false;
  if(!std::strcmp(action,"pause"))std::strcpy(request,"pause_requested");
  else if(!std::strcmp(action,"resume"))std::strcpy(request,"resume_requested");
  else if(!std::strcmp(action,"cancel"))std::strcpy(request,"cancel_requested");
  else return false;
  return true;
 }
 void finish(const char *result) {if(!std::strcmp(result,"uncertain")||!std::strcmp(result,"failed")||!std::strcmp(result,"resolved")||!std::strcmp(result,"cancelled"))automaticRecovery=false;active=false;std::strncpy(state,result,23);state[23]=0;request[0]=0;}
};
}
