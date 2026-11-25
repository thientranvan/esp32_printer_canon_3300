#include <string>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cassert>
using std::min;
static uint32_t tick=0;
static uint32_t millis(){return tick;}
#define pdMS_TO_TICKS(x) (x)
static void vTaskDelay(unsigned n){tick+=n;}
struct String:std::string {
 using std::string::string;
 size_t length()const{return size();}
 bool endsWith(const char *s)const{return size()>=strlen(s)&&compare(size()-strlen(s),strlen(s),s)==0;}
 void remove(size_t n){erase(n);}
 String &operator=(const char*s){std::string::operator=(s);return *this;}
};
struct WiFiClient {
 std::string bytes;size_t at=0;bool stall=false;unsigned fragment=1,reads=0;
 int available(){return stall?0:int(std::min<size_t>(fragment,bytes.size()-at));}
 int read(uint8_t*p,size_t n){++reads;n=std::min(n,bytes.size()-at);memcpy(p,bytes.data()+at,n);at+=n;return n;}
 bool connected(){return stall||at<bytes.size();}
};
static const size_t MaxBody=64*1024*1024;
