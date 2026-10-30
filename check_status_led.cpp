// Run: clang++ -std=c++11 check_status_led.cpp -o /tmp/check_status_led && /tmp/check_status_led
#include <cassert>
#include <initializer_list>
#include "station/status_led.h"
int main(){
 using namespace StatusLed;
 assert(color(Updating,99)==0x020003&&color(Updating,100)==0&&color(Updating,200)==0x020003);
 assert(color(Idle,0)==0x000200&&color(Idle,59)==0x000200&&color(Idle,60)==0&&color(Idle,3000)==0x000200);
 assert(color(NetworkError,249)==0x030000&&color(NetworkError,250)==0&&color(NetworkError,500)==0x030000);
 assert(color(Busy,124)==3&&color(Busy,125)==0&&color(Busy,250)==3);
 for(auto m:{PrinterError,Uncertain}){assert(color(m,119)&&!color(m,120)&&color(m,280)&&!color(m,400)&&color(m,1600));}
 assert(color(Disconnected,499)&&!color(Disconnected,500)&&color(Disconnected,2000));
 assert(color(Unverified,119)&&!color(Unverified,120));
 assert(color(Paused,499)&&!color(Paused,500));
 assert(color(HardwareError,99)&&!color(HardwareError,100));
 for(int m=Updating;m<=Idle;++m)for(unsigned t=0;t<6000;++t){auto rgb=color(Mode(m),t);assert((rgb>>16)<=3&&((rgb>>8)&255)<=3&&(rgb&255)<=3);}
}
