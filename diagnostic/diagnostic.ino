#include <Arduino.h>
#include <WiFi.h>

// ponytail: RGB pin assumed GPIO48; confirm board schematic if LED stays unchanged.
constexpr uint8_t LED_PIN = 48;
bool healthy = false;

void setup() {
  Serial.begin(115200);
  rgbLedWrite(LED_PIN, 0, 0, 0);
  delay(1500);
  Serial.println("\nESP32 printer diagnostic v1");
  Serial.printf("Chip: %s rev %u, CPU: %u MHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
  Serial.printf("Flash: %u bytes, PSRAM: %u bytes\n", ESP.getFlashChipSize(), ESP.getPsramSize());
  bool memoryOK = false;
  auto memory = static_cast<volatile uint8_t *>(ps_malloc(65536));
  if (memory) {
    memoryOK = true;
    for (size_t i = 0; i < 65536; ++i) memory[i] = uint8_t(i ^ (i >> 8));
    for (size_t i = 0; i < 65536; ++i) {
      if (memory[i] != uint8_t(i ^ (i >> 8))) { memoryOK = false; break; }
    }
    free(const_cast<uint8_t *>(memory));
  }
  Serial.printf("PSRAM 64 KiB write/read: %s\n", memoryOK ? "PASS" : "FAIL");
  const bool wifiOK = WiFi.mode(WIFI_STA);
  Serial.printf("Wi-Fi init: %s, MAC: %s\n", wifiOK ? "PASS" : "FAIL", WiFi.macAddress().c_str());
  Serial.println("Scanning Wi-Fi (no connection, no credentials stored)...");
  const int networks = wifiOK ? WiFi.scanNetworks() : -1;
  Serial.printf("Wi-Fi scan: %d networks (negative means error)\n", networks);
  WiFi.scanDelete();
  healthy = ESP.getFlashChipSize() == 16 * 1024 * 1024 &&
            ESP.getPsramSize() == 8 * 1024 * 1024 && memoryOK && wifiOK && networks >= 0;
  Serial.printf("DIAGNOSTIC: %s\n", healthy ? "PASS" : "FAIL");
}

void loop() {
  // Low brightness heartbeat: green = PASS, red = FAIL.
  rgbLedWrite(LED_PIN, healthy ? 0 : 2, healthy ? 2 : 0, 0);
  delay(100);
  rgbLedWrite(LED_PIN, 0, 0, 0);
  delay(2900);
  Serial.printf("Heartbeat: %s, uptime=%lu s, free heap=%u\n",
                healthy ? "PASS" : "FAIL", millis() / 1000, ESP.getFreeHeap());
}
