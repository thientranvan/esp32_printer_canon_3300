#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace CaptStatus {
inline unsigned word(const uint8_t *p){return p[0]|unsigned(p[1])<<8;}
struct Flags {
  bool service, jam, cover, toner, paper;
  unsigned decoded, started, exiting, completed;
};
inline Flags decode(const uint8_t *frame) {
  const uint8_t *p=frame+4;unsigned flags=word(p+12);
  return {bool(p[2]&8),bool(flags&0x100),bool(flags&0x4000),bool(flags&0x2000),
    bool(word(p+8)&0x4000),word(p+14),word(p+16),word(p+18),word(p+20)};
}
// CAPT replies may arrive as several short USB transfers. Retain only telemetry
// prefix, consume entire declared frame, never interpret a tail as a new header.
struct Frames {
  uint8_t data[80]={};size_t used=0, length=0;
  void reset(){used=length=0;}
  template<class F> void feed(const uint8_t *p,size_t n,F complete){
    while(n--){uint8_t b=*p++;if(used<sizeof(data))data[used]=b;++used;
      if(used==4){length=word(data+2);if(length<4||length>4096){reset();continue;}}
      if(length&&used==length){complete(data,length<sizeof(data)?length:sizeof(data));reset();}
    }
  }
};
// Read-only queries can drain replies left by an interrupted client. Never
// re-send a command to recover framing; bounded reads only, no print retries.
template<class Read> bool reply(uint8_t *out,size_t capacity,unsigned op,Read read) {
  bool query=op==0xA0A8||op==0xA2A2||op==0xA1A1||op==0xA0A1;
  for(unsigned attempt=0;attempt<(query?8U:1U);++attempt){
    size_t got=0,wanted=4;bool invalid=false;
    while(got<wanted){
      uint8_t part[64];int n=0;
      if(!read(part,64,n)||n<=0||n>64)return false;
      if(got+size_t(n)>capacity){invalid=true;break;}
      memcpy(out+got,part,n);got+=n;
      if(got>=4){wanted=word(out+2);if(wanted<4||wanted>capacity||got>wanted){invalid=true;break;}}
    }
    if(!invalid && word(out)==op)return true;
  }
  return false;
}

}
