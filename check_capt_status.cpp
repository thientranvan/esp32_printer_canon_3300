// Run: clang++ -std=c++11 -I. check_capt_status.cpp -o /tmp/check-capt-status && /tmp/check-capt-status
#include "station/capt_status.h"
#include <cassert>
#include <cstdio>
int main(){
 uint8_t fault[60]={0xa8,0xa0,60,0,0x33,0x8c,8};
 fault[16]=2;fault[24]=3;
 for(unsigned split=1;split<60;++split){
  CaptStatus::Frames frames;unsigned calls=0;
  auto check=[&](const uint8_t *p,size_t n){++calls;assert(n==60);auto f=CaptStatus::decode(p);assert(f.service&&!f.jam&&!f.cover&&!f.paper&&f.completed==3);};
  frames.feed(fault,split,check);assert(!calls);frames.feed(fault+split,60-split,check);assert(calls==1);
 }
 CaptStatus::Frames frames;unsigned calls=0;
 for(auto b:fault)frames.feed(&b,1,[&](const uint8_t*,size_t){++calls;});assert(calls==1);
 uint8_t flags[60]={0xa8,0xa0,60,0};flags[12]=0;flags[13]=0x40;flags[17]=0x61;
 auto f=CaptStatus::decode(flags);assert(f.jam&&f.cover&&f.toner&&f.paper&&!f.service);
 uint8_t bad[]={0,0,0xff,0xff};frames.feed(bad,4,[&](const uint8_t*,size_t){assert(false);});assert(frames.used==0);
 uint8_t output[80];unsigned step=0;
 assert(CaptStatus::reply(output,sizeof(output),0xA0A8,[&](uint8_t *p,int,int &n){
   if(step==0){memcpy(p,fault+6,54);n=54;}
   else if(step==1){memcpy(p,fault,6);n=6;}
   else if(step==2){memcpy(p,fault+6,54);n=54;}
   else return false;
   ++step;return true;
 }));assert(step==3&&CaptStatus::decode(output).service);
 puts("CAPT fragmented status, stale tail recovery and flags: OK");
}
