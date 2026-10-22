#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

// ponytail: normalized LBP3300/A4 CAPT, <=4 MiB and 100 logical pages.
// Add a durable streaming spool before increasing beyond available PSRAM.
namespace Prepared {
constexpr size_t Limit = 4 * 1024 * 1024;
inline uint16_t u16(const uint8_t *p) { return p[0] | (uint16_t(p[1])<<8); }
inline uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t(u16(p+2))<<16); }
struct Band { const uint8_t *data; size_t size; };
struct Page { const uint8_t *parameters; std::vector<Band> bands; };
struct Side { unsigned page; uint8_t flag; };
struct Job { unsigned copies=0, duplex=0; bool collate=false; std::vector<Page> pages; std::vector<Side> plan; };
inline bool parse(const uint8_t *data, size_t size, Job &out) {
  out = Job{};
  if (size<16 || size>Limit || memcmp(data,"ESPJOB1\n",8)) return false;
  unsigned count=u16(data+8); out.copies=u16(data+10); out.duplex=data[12]; out.collate=data[13];
  if (!count || count>100 || !out.copies || out.copies>99 || out.duplex<1 || out.duplex>3 || data[13]>1 || u16(data+14)) return false;
  out.pages.reserve(count);out.plan.reserve(count*out.copies);
  size_t offset=16;unsigned totalBands=0;
  for (unsigned i=0;i<count;++i) {
    if (size-offset<68) return false;
    const uint8_t *p=data+offset;
    if (u16(p)!=0xD0A0 || u16(p+2)!=44 || u16(p+6)!=0x2A94 ||
        (p[8]!=0 && p[8]!=2) || u16(p+44)!=0xD0A4 || u16(p+46)!=12 ||
        u16(p+56)!=0xD0A1 || u16(p+58)!=4 || u16(p+60)!=0xD0A2 || u16(p+62)!=4) return false;
    unsigned shift=p[17];
    if ((u16(p+34)>>(shift&15))!=2480 || (u16(p+36)>>(shift>>4))!=3507) return false;
    unsigned bands=u32(p+64); if (!bands || bands>4096-totalBands) return false; totalBands+=bands;
    Page page{p,{}}; page.bands.reserve(bands); offset+=68;
    for (unsigned b=0;b<bands;++b) {
      if (size-offset<4) return false;
      size_t n=u32(data+offset); offset+=4;
      if (!n || n>size-offset) return false;
      page.bands.push_back({data+offset,n}); offset+=n;
    }
    out.pages.push_back(page);
  }
  if (offset!=size) return false;
  unsigned step=out.duplex==1?1:2;
  for (unsigned group=0;group<(out.collate?out.copies:1);++group)
    for (unsigned i=0;i<count;i+=step)
      for (unsigned copy=0;copy<(out.collate?1:out.copies);++copy) {
        bool paired=step==2 && i+1<count;
        if (paired) out.plan.push_back({i+1,4});
        out.plan.push_back({i,uint8_t(paired?2:0)});
      }
  return true;
}
}
