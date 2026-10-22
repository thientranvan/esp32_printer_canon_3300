// Run: clang++ -std=c++11 check_prepared.cpp -o /tmp/check-prepared && /tmp/check-prepared
#include "station/prepared_job.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
static void put16(std::vector<uint8_t>& b,size_t p,unsigned n) { b[p]=n; b[p+1]=n>>8; }
int main(int argc,char **argv) {
  if(argc==2) {
    std::ifstream input(argv[1],std::ios::binary);assert(input.good());
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    Prepared::Job job;assert(Prepared::parse(file.data(),file.size(),job));
    printf("PASS: prepared artifact %zu pages, %zu planned images\n",job.pages.size(),job.plan.size());return 0;
  }
  std::vector<uint8_t> b(16); memcpy(b.data(),"ESPJOB1\n",8); b[8]=3;b[10]=2;b[12]=2;b[13]=1;
  for(int i=0;i<3;++i) {
    size_t p=b.size();b.resize(p+73);put16(b,p,0xD0A0);put16(b,p+2,44);put16(b,p+6,0x2A94);
    put16(b,p+34,2480);put16(b,p+36,3507);put16(b,p+44,0xD0A4);put16(b,p+46,12);
    put16(b,p+56,0xD0A1);put16(b,p+58,4);put16(b,p+60,0xD0A2);put16(b,p+62,4);
    b[p+64]=1;b[p+68]=1;b[p+72]=i;
  }
  Prepared::Job j;assert(Prepared::parse(b.data(),b.size(),j));assert(j.plan.size()==6);
  assert(j.plan[0].page==1 && j.plan[0].flag==4 && j.plan[1].page==0 && j.plan[1].flag==2 && j.plan[2].flag==0);
  b[13]=0;assert(Prepared::parse(b.data(),b.size(),j));assert(j.plan[2].page==1 && j.plan[4].page==2);
  for(size_t n=0;n<b.size();++n)assert(!Prepared::parse(b.data(),n,j));
  b.push_back(0);assert(!Prepared::parse(b.data(),b.size(),j));b.pop_back();
  b[12]=4;assert(!Prepared::parse(b.data(),b.size(),j));b[12]=1;b[16+6]=0;assert(!Prepared::parse(b.data(),b.size(),j));
  puts("PASS: prepared job validation, truncation, geometry, duplex/copies ordering");
}
