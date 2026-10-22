#include "usb_trace.h"
#include <esp_attr.h>
#include <esp_idf_version.h>
#include <cstring>

// ponytail: read-only private enumeration layout for IDF 5.5.5; recheck offset on SDK upgrade.
static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(5, 5, 5), "Recheck USB enum tracing layout");
static void *enumDriver;
struct Trace { uint32_t magic; uint32_t count; int stage[32]; };
RTC_NOINIT_ATTR static Trace trace;

extern "C" esp_err_t __real_enum_install(void *, void **);
extern "C" esp_err_t __real_enum_process();
extern "C" esp_err_t __wrap_enum_install(void *config, void **client) {
  esp_err_t error = __real_enum_install(config, client);
  if (error == ESP_OK) {
    enumDriver = *client;
    trace.magic = 0x55425354;
    trace.count = 0;
  }
  return error;
}

extern "C" esp_err_t __wrap_enum_process() {
  if (enumDriver) {
    int stage;
    memcpy(&stage, static_cast<const uint8_t *>(enumDriver) + 16, sizeof(stage));
    trace.stage[trace.count % 32] = stage;
    ++trace.count;
    // IDLE has no pending enumeration work. A stale hub notification must not
    // enter IDF 5.5.5 enum_process(), whose default branch calls abort().
    if (stage == 0) return ESP_OK;
  }
  return __real_enum_process();
}

String usbTraceReport() {
  if (trace.magic != 0x55425354) return "No USB stage trace\n";
  String report = "USB enum stages:";
  uint32_t start = trace.count > 32 ? trace.count - 32 : 0;
  for (uint32_t i = start; i < trace.count; ++i) report += " " + String(trace.stage[i % 32]);
  return report + "\n";
}
