#include "station/raster_job.h"
#include "station/prepared_job.h"
extern "C" {
#include "hiscoa-decompress.h"
}
#include <cstdio>
#include <vector>
#include <cassert>
#include <cstdlib>
int main(int argc,char **argv){unsigned pages=0;std::vector<uint8_t> bytes;
 bool ok=RasterJob::convert([](uint8_t*p,size_t n){return fread(p,1,n,stdin)==n;},[&](const uint8_t*p,size_t n){if(bytes.size()+n>Prepared::Limit)return false;bytes.insert(bytes.end(),p,p+n);return true;},[]{int c=getchar();if(c==EOF)return true;ungetc(c,stdin);return false;},1,argc>1?atoi(argv[1]):1,pages);
 if(!ok)return 2;bytes[8]=pages;bytes[9]=pages>>8;Prepared::Job j;assert(Prepared::parse(bytes.data(),bytes.size(),j));
 for(auto page:j.pages){unsigned y=120;for(auto band:page.bands){
  const void *p=band.data;size_t remaining=band.size;uint8_t decoded[592*70];size_t length=sizeof(decoded);
  hiscoa_decompress_band(&p,&remaining,decoded,&length,592,&hiscoa_default_params);assert(!remaining&&length&&length%592==0);
  for(size_t i=0;i<length;++i){unsigned row=y+i/592,col=(i%592)*8+120;uint8_t expected=0;
   for(unsigned bit=0;bit<8;++bit)if(row>=200&&row<240&&col+bit>=320&&col+bit<560)expected|=128>>bit;
   assert(decoded[i]==expected);
  }
  y+=length/592;
 }assert(y==120+6774);}
 fwrite(bytes.data(),1,bytes.size(),stdout);
}
