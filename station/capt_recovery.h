#pragma once
#include <cstdint>
namespace CaptRecovery {
// ponytail: three printer-class resets per stale episode; no root-port power cycle.
// Unsupported class resets remain an explicit error, not an endless reset loop.
struct Retry {
 unsigned attempts=0;uint32_t last=0;
 bool due(uint32_t now,bool fresh,bool connected,bool bulkTimeout,bool fault,bool maintenance){
  if(fresh||!connected){attempts=0;return false;}
  if(!bulkTimeout||fault||maintenance||attempts>=3)return false;
  uint32_t delay=attempts==1?15000:60000;
  return !attempts||uint32_t(now-last)>=delay;
 }
 void queued(uint32_t now){last=now;++attempts;}
};
}
