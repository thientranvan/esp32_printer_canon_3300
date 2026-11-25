"""Compile production raster/IPP code; independent libcups encoder + Hi-SCoA decoder.
macOS: python3 check_airprint.py. Linux needs libcups2-dev.
No printer/network/firmware changes.
"""
from pathlib import Path
import subprocess,tempfile,struct,shutil,os,sys
ROOT=Path(__file__).resolve().parent
os.environ['UBSAN_OPTIONS']='halt_on_error=1'
os.environ['ASAN_OPTIONS']=('detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')+':halt_on_error=1'
with tempfile.TemporaryDirectory(prefix='esp-airprint-check-') as tmp:
 folder=Path(tmp);objects=[]
 for source in ['station/src/hiscoa/hiscoa-compress.c','station/src/hiscoa/hiscoa-common.c','tests/airprint/hiscoa-decompress.c']:
  obj=folder/(Path(source).stem+'.o')
  subprocess.run(['cc','-std=c99','-fsanitize=address,undefined','-g','-I'+str(ROOT/'station/src/hiscoa'),'-I'+str(ROOT/'tests/airprint'),'-c',str(ROOT/source),'-o',str(obj)],check=True)
  objects.append(str(obj))
 executable=folder/'convert'
 subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-g','-I'+str(ROOT),'-I'+str(ROOT/'station/src/hiscoa'),'-I'+str(ROOT/'tests/airprint'),str(ROOT/'tests/airprint/convert.cpp'),*objects,'-o',str(executable)],check=True)
 generator=folder/'cups-fixture';subprocess.run(['cc',str(ROOT/'tests/airprint/cups_fixture.c'),'-lcups','-o',str(generator)],check=True)
 results=[]
 for apple in [0,1]:
  for bits in [1,8,24]:
   raw=subprocess.check_output([str(generator),str(apple),str(bits)])
   r=subprocess.run([str(executable)],input=raw,capture_output=True);assert r.returncode==0,r.stderr.decode()[:3000]
   results.append(r.stdout)
   for bad in [raw[:-1],raw+b'x',raw[:10],b'X'+raw[1:]]:
    r=subprocess.run([str(executable)],input=bad,capture_output=True);assert r.returncode==2,('Bad raster accepted or crashed',r.returncode,r.stderr[:300])
   if not apple:
    for offset,value in [(276,300),(372,0xffffffff),(376,1),(384,16),(388,32),(392,0xffffffff),(396,2),(400,6),(420,99)]:
     broken=bytearray(raw);struct.pack_into('>I',broken,4+offset,value)
     r=subprocess.run([str(executable)],input=broken,capture_output=True);assert r.returncode==2,(offset,r.stderr[:300])
 assert len(set(results))==1,'URF/PWG/gray/mono/RGB produced different images'
 for apple in [0,1]:
  for duplex in [1,2,3]:
   raw=subprocess.check_output([str(generator),str(apple),'8','3',str(duplex)])
   r=subprocess.run([str(executable),str(duplex)],input=raw,capture_output=True);assert r.returncode==0,r.stderr.decode()[:1000]
   assert struct.unpack_from('<H',r.stdout,8)[0]==3 and r.stdout[12]==duplex
   if duplex!=1:assert subprocess.run([str(executable),'1'],input=raw,capture_output=True).returncode==2

 # Production raster packet code under a memory budget / admission failure.
 harness=folder/'ipp.cpp';harness.write_text(r'''
#include "station/ipp_protocol.h"
#include <cassert>
#include <vector>
#include <cstring>
using namespace Ipp;
std::vector<uint8_t> request(){Response r(42);r.bytes[2]=0;r.bytes[3]=2;r.text(0x45,"printer-uri","ipp://test/ipp/print");r.text(0x49,"document-format","image/pwg-raster");r.group(2);r.number(0x21,"copies",2);r.text(0x44,"sides","two-sided-short-edge");r.end();return r.bytes;}
bool parseBytes(std::vector<uint8_t> &b,Request &r){size_t at=0;bool ok=parse([&](uint8_t*p,size_t n){if(n>b.size()-at)return false;memcpy(p,b.data()+at,n);at+=n;return true;},r);if(ok)assert(at==b.size());return ok;}
int main(){auto b=request();Request r;assert(parseBytes(b,r));assert(r.operation==2&&r.id==42&&r.copies==2&&r.duplex==3&&r.format=="image/pwg-raster");
for(size_t n=0;n<b.size();++n){auto bad=b;bad.resize(n);Request rejected;assert(!parseBytes(bad,rejected));}
Response bad(1);bad.bytes[3]=2;bad.group(2);bad.number(0x21,"copies",0);bad.end();Request no;assert(!parseBytes(bad.bytes,no));
Response huge(1);huge.bytes[3]=2;huge.text(0x42,"job-name",std::string(129,'X'));huge.end();assert(!parseBytes(huge.bytes,no));
Response options(1);options.bytes[3]=2;options.group(2);options.text(0x44,"sides","not-supported");options.end();assert(!parseBytes(options.bytes,no));
// Replies must use the version requested by IPP clients (Android/CUPS probes 1.1).
for(unsigned major:{1u,2u}){auto v=request();v[0]=major;v[1]=major==1?1:0;Request q;assert(parseBytes(v,q));Response answer(q);assert(answer.bytes[0]==v[0]&&answer.bytes[1]==v[1]);}
// Android media-col, including nested media-size and margins, is not a flat keyword.
Response media(17);media.bytes[3]=4;media.group(2);media.attr(0x34,"media-col",nullptr,0);
media.text(0x4a,"","media-size");media.attr(0x34,"",nullptr,0);
media.text(0x4a,"","x-dimension");media.number(0x21,"",21000);media.text(0x4a,"","y-dimension");media.number(0x21,"",29700);media.attr(0x37,"",nullptr,0);
media.text(0x4a,"","media-type");media.text(0x44,"","stationery");media.text(0x4a,"","media-source");media.text(0x44,"","main");media.text(0x4a,"","media-top-margin");media.number(0x21,"",508);
media.attr(0x37,"",nullptr,0);media.end();Request a4;assert(parseBytes(media.bytes,a4));
for(size_t n=0;n<media.bytes.size();++n){auto truncated=media.bytes;truncated.resize(n);Request q;assert(!parseBytes(truncated,q));}
auto letter=media.bytes;for(size_t i=0;i+4<=letter.size();++i)if(u32(letter.data()+i)==21000){letter[i+2]=0xff;break;}Request q;assert(!parseBytes(letter,q));
Response target(12);target.bytes[3]=9;target.text(0x45,"job-uri","ipp://192.168.1.199:631/ipp/print/1234");target.end();Request poll;assert(parseBytes(target.bytes,poll)&&poll.job==1234);

}
''')
 check=folder/'ipp';subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined','-I'+str(ROOT),str(harness),'-o',str(check)],check=True);subprocess.run([str(check)],check=True)
 # Run production HTTP chunk/length body reader against fragmented input.
 server=(ROOT/'station/ipp_server.h').read_text()
 body=server[server.index('struct Body {'):server.index('static void reply(')]
 http=folder/'http.cpp'
 http.write_text((ROOT/'tests/airprint/http_body_prefix.cpp').read_text()+body+r'''
int main(){
 for(unsigned fragment:{1u,2u,7u,1024u}){
  WiFiClient c;c.fragment=fragment;c.bytes="3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n";Body b(c);b.chunked=true;uint8_t p[5];assert(b.read(p,5)&&!memcmp(p,"abcde",5)&&b.end());
  WiFiClient fixed;fixed.fragment=fragment;fixed.bytes="abcde";Body length(fixed);length.remaining=5;assert(length.read(p,5)&&length.end());
 }
 for(const char*raw:{"x\r\n","4000001\r\n","1\r\naXX","0\r\nx\r\n","1;ignored=yes\r\na\r\n0\r\n\r\n"}){
  WiFiClient c;c.bytes=raw;Body b(c);b.chunked=true;uint8_t p[5];bool ok=b.read(p,5);assert(!ok&&b.failed);
 }
 // Tiny decoder reads must reuse socket blocks, even across a buffer boundary.
 WiFiClient bulk;bulk.fragment=8192;bulk.bytes=std::string(10000,'z');Body buffered(bulk);buffered.remaining=bulk.bytes.size();
 for(unsigned i=0;i<10000;++i){uint8_t byte;assert(buffered.read(&byte,1)&&byte=='z');}
 assert(buffered.end()&&bulk.reads==3);
 WiFiClient c;c.stall=true;Body timeout(c);timeout.remaining=1;uint8_t p;assert(!timeout.read(&p,1)&&tick>=15000);
}
''')
 http_check=folder/'http';subprocess.run(['c++','-std=c++11','-fsanitize=address,undefined',str(http),'-o',str(http_check)],check=True);subprocess.run([str(http_check)],check=True)
print('PASS: libcups PWG/URF mono/gray/RGB at 600dpi; all bands independently decompressed, byte-exact pixels; malformed headers, truncation/trailing bytes, multi-page duplex, HTTP framing/timeouts, IPP attributes/options, ASan/UBSan')
