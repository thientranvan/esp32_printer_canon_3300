#pragma once
#ifdef ESP32_AIRPRINT_EXPERIMENTAL
#include <mdns.h>
#include "ipp_protocol.h"
#include "raster_job.h"
// Experimental, opt-in build: never advertise unsupported PDF/JPEG or accept Internet clients.
// ponytail: one <=4MiB prepared job, 40 A4/600dpi pages; later add durable streaming spool.
namespace AirPrint {
static WiFiServer listener(631);
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static uint8_t *spool=nullptr;static size_t spoolSize=0;
static unsigned jobId=0,nextId=1,ippState=3,jobPages=0,copies=1,duplex=1;
static char title[129]="AirPrint";static bool pending=false;

struct Diagnostic {unsigned requests=0,operation=0,http=0,status=0,width=0,height=0,dpi=0,bits=0,space=0;size_t received=0;uint32_t at=0;char attribute[129]={},format[64]={},phase[32]="idle";};
static Diagnostic diagnostic,lastFailure;
static void beginRequest(){portENTER_CRITICAL(&lock);++diagnostic.requests;diagnostic.operation=diagnostic.http=diagnostic.status=0;diagnostic.received=0;diagnostic.width=diagnostic.height=diagnostic.dpi=diagnostic.bits=diagnostic.space=0;diagnostic.format[0]=0;diagnostic.at=millis();diagnostic.attribute[0]=0;strcpy(diagnostic.phase,"http");portEXIT_CRITICAL(&lock);}
static void recordRequest(const Ipp::Request&r,size_t received,const char*phase){portENTER_CRITICAL(&lock);diagnostic.operation=r.operation;strlcpy(diagnostic.format,r.format.c_str(),sizeof(diagnostic.format));diagnostic.received=received;strlcpy(diagnostic.attribute,r.attribute.c_str(),sizeof(diagnostic.attribute));strlcpy(diagnostic.phase,phase,sizeof(diagnostic.phase));portEXIT_CRITICAL(&lock);}
static String diagnosticsJSON(){portENTER_CRITICAL(&lock);auto d=diagnostic;auto e=lastFailure;portEXIT_CRITICAL(&lock);return String("{\"enabled\":true,\"requests\":")+d.requests+",\"operation\":"+d.operation+",\"format\":"+jsonString(d.format)+",\"width\":"+d.width+",\"height\":"+d.height+",\"dpi\":"+d.dpi+",\"bits\":"+d.bits+",\"space\":"+d.space+",\"http\":"+d.http+",\"ipp_status\":"+d.status+",\"received\":"+String(uint32_t(d.received))+",\"age_ms\":"+(d.at?String(uint32_t(millis()-d.at)):String("null"))+",\"attribute\":"+jsonString(d.attribute)+",\"phase\":"+jsonString(d.phase)+",\"last_error_operation\":"+e.operation+",\"last_error_http\":"+e.http+",\"last_error_status\":"+e.status+",\"last_error_phase\":"+jsonString(e.phase)+",\"last_error_attribute\":"+jsonString(e.attribute)+",\"last_error_format\":"+jsonString(e.format)+",\"last_error_width\":"+e.width+",\"last_error_height\":"+e.height+",\"last_error_dpi\":"+e.dpi+",\"last_error_bits\":"+e.bits+",\"last_error_space\":"+e.space+",\"last_error_age_ms\":"+(e.at?String(uint32_t(millis()-e.at)):String("null"))+"}";}
static const size_t MaxBody=64*1024*1024;
struct Body {
 explicit Body(WiFiClient &c):client(c){}
 WiFiClient &client;size_t remaining=0,chunk=0,consumed=0;bool chunked=false,finished=false,failed=false;uint32_t started=millis();
 bool raw(uint8_t *p,size_t n){uint32_t progress=millis();while(n){if(uint32_t(millis()-started)>120000||uint32_t(millis()-progress)>15000)return false;int a=client.available();if(a){int got=client.read(p,min(n,size_t(a)));if(got<=0)return false;p+=got;n-=got;progress=millis();}else{if(!client.connected())return false;vTaskDelay(pdMS_TO_TICKS(2));}}return true;}
 bool line(String &s,size_t limit){s="";uint8_t c;while(s.length()<limit){if(!raw(&c,1))return false;if(c=='\n'){if(!s.endsWith("\r"))return false;s.remove(s.length()-1);return true;}s+=char(c);}return false;}
 bool next(){
  if(!chunked)return remaining>0;
  if(chunk)return true;if(finished)return false;String s;if(!line(s,128)||!s.length()){failed=true;return false;}
  size_t n=0;for(size_t i=0;i<s.length();++i){char c=s[i];unsigned v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;if(v>15||n>(MaxBody-v)/16){failed=true;return false;}n=n*16+v;}
  if(!n){if(!line(s,128)||s.length()){failed=true;return false;}finished=true;return false;}chunk=n;return true;
 }
 bool read(uint8_t *p,size_t n){while(n){if(!next()){failed=true;return false;}size_t part=min(n,chunked?chunk:remaining);if(part>MaxBody-consumed||!raw(p,part)){failed=true;return false;}consumed+=part;p+=part;n-=part;if(chunked){chunk-=part;if(!chunk){uint8_t crlf[2];if(!raw(crlf,2)||memcmp(crlf,"\r\n",2)){failed=true;return false;}}}else remaining-=part;}return true;}
 bool end(){bool more=next();return !more&&!failed;}
};
static void reply(WiFiClient &client,Ipp::Response &r){portENTER_CRITICAL(&lock);diagnostic.http=200;diagnostic.status=Ipp::u16(r.bytes.data()+2);if(diagnostic.status>=0x0400)lastFailure=diagnostic;portEXIT_CRITICAL(&lock);r.end();client.printf("HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n\r\n",unsigned(r.bytes.size()));size_t at=0;while(at<r.bytes.size()){size_t n=client.write(r.bytes.data()+at,r.bytes.size()-at);if(!n)break;at+=n;}}
static void httpError(WiFiClient &c,unsigned code){portENTER_CRITICAL(&lock);diagnostic.http=code;strcpy(diagnostic.phase,"http-error");lastFailure=diagnostic;portEXIT_CRITICAL(&lock);c.printf("HTTP/1.1 %u Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",code);}
static std::string uri(){return std::string("ipp://")+WiFi.localIP().toString().c_str()+":631/ipp/print";}
static void jobAttributes(Ipp::Response &r){
 portENTER_CRITICAL(&lock);unsigned id=jobId,state=ippState,pages=jobPages;portEXIT_CRITICAL(&lock);
 auto j=jobSnapshot();r.group(2);r.text(0x45,"job-printer-uri",uri());r.text(0x42,"job-name",title);r.text(0x42,"job-originating-user-name","LAN client");r.number(0x21,"job-id",id);r.text(0x45,"job-uri",uri()+"/"+std::to_string(id));r.number(0x23,"job-state",state);
 r.text(0x44,"job-state-reasons",state==9?"job-completed-successfully":state==7?"job-canceled-by-user":state==8?"aborted-by-system":state==5?"job-printing":"job-incoming");
 r.number(0x21,"job-impressions",pages);r.number(0x21,"job-impressions-completed",j.matches((std::string("ipp-")+std::to_string(id)).c_str())?j.completed:0);
}
static void mediaCollection(Ipp::Response &r,const char *name){
 r.attr(0x34,name,nullptr,0);r.text(0x4a,"","media-size");r.attr(0x34,"",nullptr,0);
 r.text(0x4a,"","x-dimension");r.number(0x21,"",21000);r.text(0x4a,"","y-dimension");r.number(0x21,"",29700);r.attr(0x37,"",nullptr,0);
 for(const char *member:{"media-top-margin","media-bottom-margin","media-left-margin","media-right-margin"}){r.text(0x4a,"",member);r.number(0x21,"",508);}
 r.attr(0x37,"",nullptr,0);
}
static void printerAttributes(Ipp::Response &r){
 auto u=readUSB();bool busy=ippBusy.load()||cloudBusy()||captTCPState.load()||jobSnapshot().active;
 r.group(4);r.text(0x45,"printer-uri-supported",uri());r.text(0x44,"uri-authentication-supported","none");r.text(0x44,"uri-security-supported","none");
 r.text(0x45,"printer-uuid",std::string("urn:uuid:00000000-0000-4000-8000-")+stationID.substring(6).c_str());
 r.text(0x42,"printer-name",stationName.c_str());r.text(0x41,"printer-info",stationName.c_str());r.text(0x41,"printer-make-and-model","Canon LBP3300 ESP32 AirPrint experimental");
 r.number(0x23,"printer-state",busy?4:(!u.connected||!captFresh()||captFault()?5:3));r.text(0x44,"printer-state-reasons",busy?"none":!u.connected?"offline":!captFresh()?"connecting-to-device":captFault()?"other":"none");
 r.boolean("printer-is-accepting-jobs",u.connected&&u.responsive&&captFresh()&&!captFault()&&!busy&&!firmwareCheckPending.load()&&!jobSnapshot().needsCleanup());r.number(0x21,"queued-job-count",ippBusy.load()?1:0);
 r.text(0x44,"ipp-versions-supported","1.1");r.text(0x44,"","2.0");
 bool first=true;for(unsigned op:{2u,4u,8u,9u,10u,11u}){r.number(0x23,first?"operations-supported":"",op);first=false;}
 r.text(0x47,"charset-configured","utf-8");r.text(0x47,"charset-supported","utf-8");r.text(0x48,"natural-language-configured","en");r.text(0x48,"generated-natural-language-supported","en");
 r.text(0x49,"document-format-default","image/urf");r.text(0x49,"document-format-supported","image/urf");r.text(0x49,"","image/pwg-raster");
 r.text(0x44,"compression-supported","none");r.text(0x44,"media-default","iso_a4_210x297mm");r.text(0x44,"media-supported","iso_a4_210x297mm");r.text(0x44,"media-ready","iso_a4_210x297mm");
 r.text(0x44,"media-source-supported","main");r.text(0x44,"media-type-supported","stationery");
 bool mediaFirst=true;for(const char *member:{"media-size","media-top-margin","media-bottom-margin","media-left-margin","media-right-margin","media-source","media-type"}){r.text(0x44,mediaFirst?"media-col-supported":"",member);mediaFirst=false;}
 mediaCollection(r,"media-col-default");mediaCollection(r,"media-col-ready");mediaCollection(r,"media-col-database");
 r.attr(0x34,"media-size-supported",nullptr,0);r.text(0x4a,"","x-dimension");r.number(0x21,"",21000);r.text(0x4a,"","y-dimension");r.number(0x21,"",29700);r.attr(0x37,"",nullptr,0);
 for(const char *side:{"media-top-margin-supported","media-bottom-margin-supported","media-left-margin-supported","media-right-margin-supported"})r.number(0x21,side,508);
 r.text(0x44,"sides-default","one-sided");r.text(0x44,"sides-supported","one-sided");r.text(0x44,"","two-sided-long-edge");r.text(0x44,"","two-sided-short-edge");
 r.boolean("color-supported",false);r.text(0x44,"print-color-mode-default","monochrome");r.text(0x44,"print-color-mode-supported","monochrome");
 r.resolution("printer-resolution-default");r.resolution("printer-resolution-supported");r.resolution("pwg-raster-document-resolution-supported");
 r.text(0x44,"pwg-raster-document-type-supported","black_1");r.text(0x44,"","sgray_1");r.text(0x44,"","sgray_8");r.text(0x44,"","srgb_8");
 r.text(0x44,"pwg-raster-document-sheet-back","normal");
 first=true;for(const char *v:{"V1.4","W8","SRGB24","RS600","DM1"}){r.text(0x44,first?"urf-supported":"",v);first=false;}
 r.range("copies-supported",1,99);r.number(0x21,"copies-default",1);r.number(0x21,"number-up-default",1);r.number(0x21,"number-up-supported",1);
 r.text(0x44,"multiple-document-handling-default","separate-documents-collated-copies");r.text(0x44,"multiple-document-handling-supported","separate-documents-collated-copies");
 r.boolean("multiple-document-jobs-supported",false);r.number(0x23,"orientation-requested-default",3);r.number(0x23,"orientation-requested-supported",3);
 r.number(0x21,"printer-up-time",millis()/1000);r.number(0x23,"print-quality-default",4);r.number(0x23,"print-quality-supported",4);
}
static void handle(WiFiClient &client){
 beginRequest();Body body{client};String line;if(!body.line(line,256)||!line.startsWith("POST /ipp/print")||(!line.endsWith(" HTTP/1.1")&&!line.endsWith(" HTTP/1.0"))){httpError(client,404);return;}
 String target=line.substring(5,line.lastIndexOf(' '));unsigned pathJob=0;
 if(target!="/ipp/print"){
  if(!target.startsWith("/ipp/print/")||target.length()<=11){httpError(client,404);return;}
  for(size_t i=11;i<target.length();++i){char c=target[i];if(c<'0'||c>'9'||pathJob>(0x7fffffff-unsigned(c-'0'))/10){httpError(client,404);return;}pathJob=pathJob*10+c-'0';}if(!pathJob){httpError(client,404);return;}
 }
 bool lengthSeen=false,transferSeen=false,type=false,expect=false;unsigned headers=0;
 while(body.line(line,1024)&&line.length()){
  if((headers+=line.length())>8192){httpError(client,431);return;}int colon=line.indexOf(':');if(colon<=0){httpError(client,400);return;}
  String key=line.substring(0,colon),value=line.substring(colon+1);key.toLowerCase();value.trim();
  if(key=="content-length"){if(lengthSeen||!value.length()){httpError(client,400);return;}lengthSeen=true;for(char c:value){if(c<'0'||c>'9'||body.remaining>(MaxBody-unsigned(c-'0'))/10){httpError(client,413);return;}body.remaining=body.remaining*10+c-'0';}}
  else if(key=="transfer-encoding"){if(transferSeen||value!="chunked"){httpError(client,400);return;}transferSeen=true;body.chunked=true;}
  else if(key=="content-type")type=value=="application/ipp";
  else if(key=="expect"){if(value!="100-continue"){httpError(client,417);return;}expect=true;}
  else if(key=="origin"){httpError(client,403);return;}
 }
 if(line.length()||!type||lengthSeen==transferSeen){httpError(client,400);return;}
 if(expect)client.print("HTTP/1.1 100 Continue\r\n\r\n");
 Ipp::Request request;auto read=[&](uint8_t*p,size_t n){return body.read(p,n);};
 if(!Ipp::parse(read,request)){recordRequest(request,body.consumed,"attributes-rejected");Ipp::Response r(request,0x0400);reply(client,r);return;}
 recordRequest(request,body.consumed,"attributes-ok");
 std::string host=std::string("ipp://esp32-")+stationID.substring(12).c_str()+".local";
 std::string address=std::string("ipp://")+WiFi.localIP().toString().c_str();
 if(!request.uri.empty()&&request.uri!=address+":631/ipp/print"&&request.uri!=address+"/ipp/print"&&request.uri!=host+":631/ipp/print"&&request.uri!=host+"/ipp/print"){Ipp::Response r(request,0x0406);reply(client,r);return;}
 if(pathJob){if(request.job&&request.job!=pathJob){Ipp::Response r(request,0x0406);reply(client,r);return;}request.job=pathJob;}
 Ipp::Response response(request);
 if(request.operation==11)printerAttributes(response);
 else if(request.operation==8){
  if(request.job!=jobId||!ippBusy.load()){Ipp::Response r(request,0x0406);reply(client,r);return;}
  String id=String("ipp-")+jobId;if(!jobCommand(id,"cancel")){Ipp::Response r(request,0x0404);reply(client,r);return;}
 }else if(request.operation==9){
  if(request.operation==9&&(!jobId||request.job!=jobId)){Ipp::Response r(request,0x0406);reply(client,r);return;}
  if(jobId)jobAttributes(response);
 }else if(request.operation==10){if(jobId)jobAttributes(response);}
 else if(request.operation==4){if(request.format!="image/urf"&&request.format!="image/pwg-raster"){Ipp::Response r(request,0x040a);reply(client,r);return;}}
 else if(request.operation==2){
  if(request.format!="image/urf"&&request.format!="image/pwg-raster"){Ipp::Response r(request,0x040a);reply(client,r);return;}
  auto usb=readUSB();if(!usb.connected||!usb.responsive||!captFresh()||captFault()){Ipp::Response r(request,0x0506);reply(client,r);return;}
  if(ippBusy.exchange(true)){Ipp::Response r(request,0x0507);reply(client,r);return;}
  if(!firmwareReserve()){ippBusy=false;Ipp::Response r(request,0x0507);reply(client,r);return;}
  spool=static_cast<uint8_t*>(ps_malloc(Prepared::Limit));spoolSize=0;unsigned pages=0;
  RasterJob::Format detected;bool firstRasterRead=true;
  auto rasterRead=[&](uint8_t*p,size_t n){if(!read(p,n))return false;if(firstRasterRead){firstRasterRead=false;return n==4&&!memcmp(p,request.format=="image/urf"?"UNIR":"RaS2",4);}return true;};
  bool valid=spool&&RasterJob::convert(rasterRead,[](const uint8_t*p,size_t n){if(n>Prepared::Limit-spoolSize)return false;memcpy(spool+spoolSize,p,n);spoolSize+=n;vTaskDelay(pdMS_TO_TICKS(1));return true;},[&]{return body.end();},request.copies,request.duplex,pages,ps_malloc,&detected);
  portENTER_CRITICAL(&lock);diagnostic.width=detected.width;diagnostic.height=detected.height;diagnostic.dpi=detected.dpi;diagnostic.bits=detected.bits;diagnostic.space=detected.space;portEXIT_CRITICAL(&lock);
  if(valid){RasterJob::le16(spool+8,pages);Prepared::Job verify;valid=Prepared::parse(spool,spoolSize,verify);}
  if(!valid){recordRequest(request,body.consumed,"raster-rejected");free(spool);spool=nullptr;firmwareRelease();ippBusy=false;Ipp::Response r(request,0x0411);reply(client,r);return;}
  recordRequest(request,body.consumed,"accepted");
  portENTER_CRITICAL(&lock);jobId=nextId++;ippState=3;jobPages=pages*request.copies;strlcpy(title,request.title.c_str(),sizeof(title));pending=true;portEXIT_CRITICAL(&lock);
  jobAttributes(response);
 }else{Ipp::Response r(request,0x0501);reply(client,r);return;}
 reply(client,response);
}
static void serverTask(void*){
 listener.begin();while(true){WiFiClient client=listener.accept();if(!client){vTaskDelay(pdMS_TO_TICKS(10));continue;}
  auto mask=uint32_t(WiFi.subnetMask());if(!WiFi.isConnected()||!mask||(uint32_t(client.remoteIP())&mask)!=(uint32_t(WiFi.localIP())&mask)){client.stop();continue;}
  handle(client);client.stop();
 }
}
static void printTask(void*){
 while(true){
  portENTER_CRITICAL(&lock);bool ready=pending;pending=false;unsigned id=jobId;portEXIT_CRITICAL(&lock);
  if(!ready){vTaskDelay(pdMS_TO_TICKS(20));continue;}
  // Keep the firmware admission gate while handing the reserved USB owner to the engine.
  releaseCapt(CaptFirmwareOwner);while(!claimCapt(CloudOwner))vTaskDelay(pdMS_TO_TICKS(10));
  Prepared::Job job;bool valid=Prepared::parse(spool,spoolSize,job);String name=String("ipp-")+id;
  jobBegin(name.c_str(),"lan",true);jobMetadata(title,"AirPrint","ipp");jobProgress(jobPages,0);
  std::vector<uint8_t> flags;for(auto side:job.plan)flags.push_back(side.flag);cloudRecovery.begin(name.c_str(),flags,printerEpoch.load());jobRecoveryProgress(cloudRecovery);
  portENTER_CRITICAL(&lock);ippState=5;portEXIT_CRITICAL(&lock);
  bool done=valid&&cloudPrint(job);cloudTracking=false;
  if(!done&&jobSnapshot().stopping())cancelCapt(CloudOwner);
  bool cancelled=jobSnapshot().stopping();
  jobFinish(done?"completed":cancelled?"cancelled":cloudPreflight?"failed":"uncertain");
  portENTER_CRITICAL(&lock);ippState=done?9:cancelled?7:8;portEXIT_CRITICAL(&lock);
  releaseCapt(CloudOwner);free(spool);spool=nullptr;spoolSize=0;firmwareCheckPending=false;ippBusy=false;
 }
}
static bool start(){
 nextId=(esp_random()&0x3fffffff)+1;
 TaskHandle_t worker=nullptr;if(xTaskCreate(printTask,"ipp-print",12288,nullptr,1,&worker)!=pdPASS)return false;
 if(xTaskCreate(serverTask,"ipp-server",16384,nullptr,1,nullptr)!=pdPASS){vTaskDelete(worker);return false;}
 MDNS.addService("ipp","tcp",631);MDNS.addServiceTxt("ipp","tcp","txtvers","1");MDNS.addServiceTxt("ipp","tcp","qtotal","1");MDNS.addServiceTxt("ipp","tcp","rp","ipp/print");
 MDNS.addServiceTxt("ipp","tcp","ty","Canon LBP3300");MDNS.addServiceTxt("ipp","tcp","note",stationName.c_str());MDNS.addServiceTxt("ipp","tcp","pdl","image/urf,image/pwg-raster");
 MDNS.addServiceTxt("ipp","tcp","UUID",(String("00000000-0000-4000-8000-")+stationID.substring(6)).c_str());
 MDNS.addServiceTxt("ipp","tcp","priority","0");MDNS.addServiceTxt("ipp","tcp","usb_MFG","Canon");MDNS.addServiceTxt("ipp","tcp","usb_MDL","LBP3300");
 MDNS.addServiceTxt("ipp","tcp","URF","V1.4,CP1,W8,SRGB24,RS600,DM1");MDNS.addServiceTxt("ipp","tcp","Color","F");MDNS.addServiceTxt("ipp","tcp","Duplex","T");
 mdns_service_instance_name_set("_ipp","_tcp",stationName.c_str());
 mdns_service_subtype_add_for_host(nullptr,"_ipp","_tcp",nullptr,"_universal");return true;
}
}
static String ippDiagnosticsJSON(){return AirPrint::diagnosticsJSON();}
#else
static String ippDiagnosticsJSON(){return "{\"enabled\":false}";}
#endif
