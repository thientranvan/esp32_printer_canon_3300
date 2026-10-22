#pragma once
#include <cstring>
#include <cstdint>

namespace PrintControl {
// One executing job. Durable waiting queues remain on their source (OS/VPS).
struct Job {
 char id[65]={}, source[8]={}, state[24]={}, request[24]={};
 unsigned total=0, completed=0; bool active=false, controllable=false;
 bool matches(const char *target) const {return id[0] && !std::strcmp(id,target);}
 void begin(const char *target,const char *origin,bool control=false) {
  *this=Job();std::strncpy(id,target,64);std::strncpy(source,origin,7);
  std::strcpy(state,"printing");active=true;controllable=control;
 }
 bool command(const char *target,const char *action) {
  if(!matches(target))return false;
  if(!std::strcmp(action,"delete")) {if(active || !std::strcmp(state,"uncertain"))return false;*this=Job();return true;}
  if(!controllable || !active || !std::strcmp(request,"cancel_requested"))return false;
  if(!std::strcmp(action,"pause"))std::strcpy(request,"pause_requested");
  else if(!std::strcmp(action,"resume"))std::strcpy(request,"resume_requested");
  else if(!std::strcmp(action,"cancel"))std::strcpy(request,"cancel_requested");
  else return false;
  return true;
 }
 void finish(const char *result) {active=false;std::strncpy(state,result,23);state[23]=0;request[0]=0;}
};
}
