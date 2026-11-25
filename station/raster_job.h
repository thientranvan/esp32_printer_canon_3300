#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>
#include <new>
#include <cstdlib>
extern "C" {
#include "src/hiscoa/hiscoa-compress.h"
}
// Stream decoding never retains an uncompressed page. Output is bounded by the caller.
// ponytail: A4 600 dpi, gray8/mono1/sRGB24, <=40 pages; add other media after hardware checks.
namespace RasterJob {
inline uint32_t be32(const uint8_t *p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
inline void le32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=n>>(8*i);}
inline void le16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
struct Format {unsigned width=0,height=0,bits=0,space=0,dpi=0;bool urf=false;const char *error="raster-read";};
inline bool format(const uint8_t *h,bool urf,Format &f){
 f.urf=urf;f.width=be32(h+(urf?12:372));f.height=be32(h+(urf?16:376));f.bits=urf?h[0]:be32(h+388);
 unsigned dpi=be32(h+(urf?20:276));f.dpi=dpi;f.space=urf?(h[1]==0||h[1]==4?18:h[1]==1||h[1]==5?19:999):be32(h+400);
 if(dpi!=600||(!urf&&be32(h+280)!=600)||f.width<4958||f.width>4962||f.height<7012||f.height>7016){f.error="raster-size-dpi";return false;}
 if(!((f.bits==1&&(f.space==18||f.space==3))||(f.bits==8&&f.space==18)||(f.bits==24&&f.space==19))){f.error="raster-color";return false;}
 if(!urf){if(be32(h+396)!=0){f.error="raster-order";return false;}if(be32(h+392)!=(f.width*f.bits+7)/8){f.error="raster-stride";return false;}if(be32(h+384)!=(f.bits==1?1:8)){f.error="raster-bits-color";return false;}if(be32(h+420)!=(f.bits==24?3:1)){f.error="raster-colors";return false;}}
 return true;
}
// Read: bool(uint8_t*,size_t). Output: bool(const uint8_t*,size_t). AtEnd: bool().
template<class Read,class Output,class AtEnd> bool convert(Read read,Output out,AtEnd end,unsigned copies,unsigned duplex,unsigned &pages,void *(*allocate)(size_t)=std::malloc,Format *observed=nullptr){
 pages=0;if(!copies||copies>99||duplex<1||duplex>3)return false;
 uint8_t magic[4];if(!read(magic,4))return false;bool urf=!memcmp(magic,"UNIR",4);
 unsigned declared=0;
 if(urf){uint8_t rest[8];if(!read(rest,8)||memcmp(rest,"AST\0",4))return false;declared=be32(rest+4);if(declared>40&&declared!=0xffffffff)return false;}
 else if(memcmp(magic,"RaS2",4))return false;
 std::unique_ptr<uint8_t,decltype(&std::free)> row(static_cast<uint8_t*>(std::malloc(4962*3)),std::free),band(static_cast<uint8_t*>(allocate(592*70)),std::free),encoded(static_cast<uint8_t*>(allocate(592*70*2+64)),std::free);
 if(!row||!band||!encoded)return false;
 uint8_t job[16]={'E','S','P','J','O','B','1','\n'};le16(job+10,copies);job[12]=duplex;job[13]=1;
 if(!out(job,16))return false;
 static const uint8_t parameters[44]={0xa0,0xd0,0x2c,0x0,0x0,0x0,0x94,0x2a,0x2,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x11,0x4,0x0,0x1,0x1,0x2,0x0,0x0,0x0,0x78,0x0,0x78,0x0,0x50,0x2,0x76,0x1a,0x60,0x13,0x66,0x1b,0x0,0x0,0x1,0x0,0x0,0x0};
 static const uint8_t threshold[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
 while(!end()){
  if(pages==40)return false;
  uint8_t header[1796]={};if(!read(header,urf?32:1796))return false;Format f;bool validFormat=format(header,urf,f);if(observed)*observed=f;if(!validFormat)return false;
  // Do not silently convert document-level duplex differently from the IPP job.
  if(urf){unsigned mode=header[2];if(mode>3||(mode&&mode!=(duplex==1?1:duplex==2?3:2))){if(observed)observed->error="raster-duplex";return false;}}
  else if(bool(be32(header+272))!=(duplex!=1)||(duplex!=1&&bool(be32(header+368))!=(duplex==3))){if(observed)observed->error="raster-duplex";return false;}
  uint8_t params[68]={};memcpy(params,parameters,44);le16(params+44,0xd0a4);le16(params+46,12);
  if(hiscoa_format_params(params+48,8,&hiscoa_default_params)!=8)return false;
  le16(params+56,0xd0a1);le16(params+58,4);le16(params+60,0xd0a2);le16(params+62,4);le32(params+64,(6774+69)/70);
  if(!out(params,sizeof(params)))return false;
  unsigned y=0,rows=0;size_t rowBytes=(f.width*f.bits+7)/8,pixel=f.bits==24?3:1;
  while(y<f.height){
   uint8_t repeat;if(!read(&repeat,1)||unsigned(repeat)+1>f.height-y)return false;
   size_t x=0;
   while(x<rowBytes){
    uint8_t run;if(!read(&run,1))return false;
    if(run==128){memset(row.get()+x,f.space==3?0:255,rowBytes-x);x=rowBytes;}
    else if(run>128){size_t n=size_t(257-run)*pixel;if(n>rowBytes-x){if(observed)observed->error="raster-literal";return false;}if(!read(row.get()+x,n))return false;x+=n;}
    else{size_t n=size_t(run+1)*pixel;if(n>rowBytes-x){if(observed)observed->error="raster-repeat";return false;}if(!read(row.get()+x,pixel))return false;for(size_t j=pixel;j<n;j+=pixel)memcpy(row.get()+x+j,row.get()+x,pixel);x+=n;}
   }
   for(unsigned r=0;r<=repeat;++r,++y){
    if(y<120||y>=120+6774)continue;
    uint8_t mono[592]={};uint8_t *dst=mono;
    for(unsigned col=0;col<4736;++col){unsigned sx=col+120,value;
     if(f.bits==1){bool bit=row.get()[sx/8]&(128>>(sx%8));value=f.space==3?(bit?0:255):(bit?255:0);}
     else if(f.bits==8)value=row.get()[sx];
     else value=(77*row.get()[sx*3]+150*row.get()[sx*3+1]+29*row.get()[sx*3+2]+128)>>8;
     if(value<unsigned(threshold[((y-120)&3)*4+(col&3)])*16+8)dst[col/8]|=128>>(col%8);
    }
    memcpy(band.get()+rows*592,mono,592);
    if(++rows==70||y==120+6774-1){
     size_t n=hiscoa_compress_band(encoded.get(),592*70*2+64,band.get(),592,rows,y==120+6774-1?HISCOA_EOB_LAST:HISCOA_EOB_NORMAL,&hiscoa_default_params);
     if(!n||n>=592*70*2+64)return false;uint8_t length[4];le32(length,n);
     if(!out(length,4)||!out(encoded.get(),n))return false;rows=0;
    }
   }
  }
  ++pages;
 }
 return pages&&(!urf||declared==0||declared==0xffffffff||declared==pages);
}
}
