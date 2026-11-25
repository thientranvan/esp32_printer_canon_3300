#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace Ipp {
inline unsigned u16(const uint8_t *p){return unsigned(p[0])<<8|p[1];}
inline uint32_t u32(const uint8_t *p){return uint32_t(u16(p))<<16|u16(p+2);}
inline void w16(std::vector<uint8_t>&v,unsigned n){v.push_back(n>>8);v.push_back(n);}
inline void w32(std::vector<uint8_t>&v,uint32_t n){w16(v,n>>16);w16(v,n);}
struct Request {unsigned operation=0;uint32_t id=0;unsigned copies=1,duplex=1,job=0;std::string format="image/urf",title="AirPrint",uri;};
// Parses only bounded attributes, leaves raster unread. Unknown mandatory options are rejected.
template<class Read> bool parse(Read read,Request &r){
 uint8_t h[8];if(!read(h,8)||(h[0]!=1&&h[0]!=2)||h[1]>1)return false;r.operation=u16(h+2);r.id=u32(h+4);if(!r.id)return false;
 size_t total=8;unsigned group=0,count=0;std::string previous;
 while(total<16384&&count++<256){
  uint8_t tag;if(!read(&tag,1))return false;++total;if(tag==3)return group!=0;
  if(tag<0x10){if(tag!=1&&tag!=2&&tag!=4&&tag!=5)return false;group=tag;previous.clear();continue;}
  if(!group)return false;
  uint8_t n[2];if(!read(n,2))return false;unsigned a=u16(n);if(a>128)return false;
  std::string name(a,'\0');if(a&&!read(reinterpret_cast<uint8_t*>(&name[0]),a))return false;if(!a)name=previous;else previous=name;
  if(name.empty()||!read(n,2))return false;unsigned b=u16(n);if(b>2048||total+4+a+b>16384)return false;
  std::vector<uint8_t> value(b);if(b&&!read(value.data(),b))return false;total+=4+a+b;
  std::string text(value.begin(),value.end());
  if(name=="document-format"){if(tag!=0x49)return false;r.format=text;}
  else if(name=="job-name"){if(b>128)return false;for(auto c:value)if(c<32||c==127)return false;r.title=text;}
  else if(name=="printer-uri"){if(tag!=0x45||b>256)return false;r.uri=text;}
  else if(name=="copies"){if(tag!=0x21||b!=4||!u32(value.data())||u32(value.data())>99)return false;r.copies=u32(value.data());}
  else if(name=="job-id"){if(tag!=0x21||b!=4)return false;r.job=u32(value.data());}
  else if(name=="sides"){if(tag!=0x44)return false;if(text=="one-sided")r.duplex=1;else if(text=="two-sided-long-edge")r.duplex=2;else if(text=="two-sided-short-edge")r.duplex=3;else return false;}
  else if(name=="media"&&text!="iso_a4_210x297mm")return false;
  else if(name=="compression"&&text!="none")return false;
  else if(name=="number-up"&&(tag!=0x21||b!=4||u32(value.data())!=1))return false;
  else if(name=="printer-resolution"&&(tag!=0x32||b!=9||u32(value.data())!=600||u32(value.data()+4)!=600||value[8]!=3))return false;
  else if(name=="print-color-mode"&&text!="monochrome"&&text!="auto")return false;
  else if(name=="print-quality"&&(tag!=0x23||b!=4||u32(value.data())!=4))return false;
  else if(name=="finishings"&&(tag!=0x23||b!=4||u32(value.data())!=3))return false;
  else if(name=="orientation-requested"&&(tag!=0x23||b!=4||u32(value.data())!=3))return false;
  else if(name=="multiple-document-handling"&&text!="separate-documents-collated-copies")return false;
  else if(name=="media-type"&&text!="stationery")return false;
  else if(name=="page-ranges"||name=="media-col"||name=="job-password")return false;
 }
 return false;
}
struct Response {
 std::vector<uint8_t> bytes;
 Response(uint32_t id,unsigned status=0){bytes={2,0};w16(bytes,status);w32(bytes,id);bytes.push_back(1);text(0x47,"attributes-charset","utf-8");text(0x48,"attributes-natural-language","en");}
 void group(unsigned tag){bytes.push_back(tag);}
 void attr(unsigned tag,const std::string &name,const uint8_t *p,size_t n){bytes.push_back(tag);w16(bytes,name.size());bytes.insert(bytes.end(),name.begin(),name.end());w16(bytes,n);bytes.insert(bytes.end(),p,p+n);}
 void text(unsigned tag,const std::string &name,const std::string &v){attr(tag,name,reinterpret_cast<const uint8_t*>(v.data()),v.size());}
 void number(unsigned tag,const std::string &name,uint32_t v){uint8_t p[4]={uint8_t(v>>24),uint8_t(v>>16),uint8_t(v>>8),uint8_t(v)};attr(tag,name,p,4);}
 void boolean(const std::string &name,bool b){uint8_t p=b;attr(0x22,name,&p,1);}
 void range(const std::string &name,unsigned a,unsigned b){uint8_t p[8]={};for(int i=0;i<4;++i){p[i]=a>>(24-i*8);p[4+i]=b>>(24-i*8);}attr(0x33,name,p,8);}
 void resolution(const std::string &name){uint8_t p[9]={0,0,2,88,0,0,2,88,3};attr(0x32,name,p,9);}
 void end(){bytes.push_back(3);}
};
}
