#pragma once
#include <stdint.h>
namespace StatusLed {
enum Mode { HardwareError, NetworkError, PrinterError, Disconnected, Unverified, Paused, Uncertain, Busy, Idle };
// Dim RGB values: status indicator, not illumination. All timing is nonblocking.
inline uint32_t color(Mode mode,uint32_t now) {
  bool on=false;uint32_t rgb=0;
  switch(mode) {
    case HardwareError:on=now%200<100;rgb=0x030000;break;
    case NetworkError:on=now%500<250;rgb=0x030000;break;
    case PrinterError:on=now%1600<120||(now%1600>=280&&now%1600<400);rgb=0x030200;break;
    case Disconnected:on=now%2000<500;rgb=0x030200;break;
    case Unverified:on=now%1000<120;rgb=0x030200;break;
    case Paused:on=now%1600<500;rgb=0x000202;break;
    case Uncertain:on=now%1600<120||(now%1600>=280&&now%1600<400);rgb=0x020003;break;
    case Busy:on=now%250<125;rgb=0x000003;break;
    case Idle:on=now%3000<60;rgb=0x000200;break;
  }
  return on?rgb:0;
}
}
