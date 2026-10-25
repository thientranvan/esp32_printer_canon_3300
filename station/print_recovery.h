#pragma once
#include <vector>
#include <string>
#include <cstdint>

// ponytail: checkpoint survives printer power cycles while ESP stays powered.
// Add durable spool + journal before recovering across ESP/process restarts.
namespace PrintRecovery {
struct Checkpoint {
 std::string id;
 std::vector<uint8_t> flags;
 unsigned base=0, confirmed=0, observed=0;
 uint32_t epoch=0;
 bool started=false;
 static bool valid(const std::vector<uint8_t> &plan) {
  if(plan.empty()||plan.size()>9900)return false;
  for(size_t i=0;i<plan.size();++i){
   if(plan[i]==4){if(++i==plan.size()||plan[i]!=2)return false;}
   else if(plan[i]!=0)return false;
  }
  return true;
 }
 bool matches(const char *job,const std::vector<uint8_t> &plan) const {return id==job&&flags==plan;}
 void begin(const char *job,const std::vector<uint8_t> &plan,uint32_t generation){
  id=job;flags=plan;base=confirmed=observed=0;epoch=generation;started=false;
 }
 bool observe(unsigned count) {
  if(!started||count<observed||count>flags.size()-base)return false;
  observed=count;unsigned safe=base;
  for(unsigned i=base;i<base+count;++i)if(flags[i]!=4)safe=i+1;
  if(safe>confirmed)confirmed=safe;
  return true;
 }
 unsigned sheets() const {unsigned n=0;for(unsigned i=0;i<confirmed;++i)if(flags[i]!=4)++n;return n;}
 bool restart(uint32_t generation) {
  if(started&&generation==epoch)return false;
  base=confirmed;observed=0;epoch=generation;started=false;return true;
 }
};
}
