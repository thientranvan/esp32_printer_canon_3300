#pragma once
#include <WiFi.h>

// ponytail: direct CAPT TCP, one active connection, OS spoolers hold queued jobs.
// Add a durable ESP spool for independently submitted prepared jobs (VPS path).
static WiFiServer captServer(9100);
static std::atomic<unsigned> captTCPTotal{0}, captTCPCompleted{0};
static std::atomic<int> captTCPState{0}; // 0 idle, 1 waiting for USB owner, 2 active
static std::atomic<int> captTCPError{0};
static std::atomic<uint32_t> captTCPHeader{0};
static std::atomic<uint32_t> captTCPCommands{0};

static bool tcpRead(WiFiClient &client, uint8_t *data, size_t size) {
  uint32_t progress = millis();
  while (size) {
    int available = client.available();
    if (available > 0) {
      int count = client.read(data, min(size, size_t(available)));
      if (count <= 0) return false;
      data += count;
      size -= count;
      progress = millis();
    } else {
      if (!client.connected() || uint32_t(millis() - progress) >= 15000) return false;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  return true;
}

static bool tcpTransfer(uint8_t *data, int size, bool input, int &received) {
  if (!claimCapt(1) || readUSB().recovering) return false;
  int state = captCommand.state.load();
  if ((state != 0 && state != 3) || !captCommand.state.compare_exchange_strong(state, 5)) return false;
  captCommand.size = size;
  captCommand.received = 0;
  captCommand.error = 0;
  captCommand.input = input;
  if (!input) memcpy(captCommand.data, data, size);
  captCommand.state.store(1);
  uint32_t started = millis();
  while (captCommand.state.load() != 3) {
    if (uint32_t(millis() - started) >= 8000) { captTCPError.store(ESP_ERR_TIMEOUT); return false; }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  int error = captCommand.error;
  if (error != ESP_OK) { captTCPError.store(error); return false; }
  received = captCommand.received;
  if (input) memcpy(data, captCommand.data, received);
  return true;
}

static void captTCPTask(void *) {
  auto *buffer = static_cast<uint8_t *>(malloc(4096));
  if (!buffer) { captTCPError.store(ESP_ERR_NO_MEM); vTaskDelete(nullptr); return; }
  captServer.begin();
  while (true) {
    WiFiClient client = captServer.accept();
    if (!client) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
    // Match ordinary LAN printer access; never accept from recovery AP/foreign subnets.
    uint32_t mask = uint32_t(WiFi.subnetMask());
    if (WiFi.status() != WL_CONNECTED ||
        (uint32_t(client.remoteIP()) & mask) != (uint32_t(WiFi.localIP()) & mask)) {
      client.stop(); continue;
    }
    client.setNoDelay(true);
    captTCPTotal.store(0); captTCPCompleted.store(0);
    captTCPState.store(1);
    uint32_t waiting = millis();
    bool acquired = false;
    while (client.connected() && uint32_t(millis() - waiting) < 60000) {
      if (claimCapt(1)) { acquired = true; break; }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (acquired) {
      captTCPState.store(2);
      captTCPError.store(0);
      while (tcpRead(client, buffer, 4)) {
        uint16_t opcode = uint16_t(buffer[0]) | (uint16_t(buffer[1]) << 8);
        unsigned length = unsigned(buffer[2]) | (unsigned(buffer[3]) << 8);
        captTCPHeader.store(uint32_t(opcode) | (uint32_t(length) << 16));
        // Optional LAN progress metadata; consumed here, never forwarded to USB.
        if(opcode==0xF001) {
          if(length!=8 || !tcpRead(client,buffer+4,4)) { captTCPError.store(ESP_ERR_INVALID_ARG);break; }
          unsigned total=uint32_t(buffer[4])|(uint32_t(buffer[5])<<8)|(uint32_t(buffer[6])<<16)|(uint32_t(buffer[7])<<24);
          if(!total || total>9900){captTCPError.store(ESP_ERR_INVALID_ARG);break;}
          captTCPTotal.store(total);
          const uint8_t ack[6]={1,0xF0,6,0,0,1};
          if(client.write(ack,6)!=6)break;
          continue;
        }
        // CAPT commands occupy A0/A1/A2/A3, C0/D0 and E0/E1 namespaces.
        unsigned group = opcode >> 8;
        if (length < 4 || !((group >= 0xA0 && group <= 0xA3) || group == 0xC0 ||
            group == 0xD0 || group == 0xE0 || group == 0xE1)) {
          captTCPError.store(ESP_ERR_INVALID_ARG); break;
        }
        unsigned remaining = length - 4;
        int first = min(remaining, 4092U);
        int received = 0;
        if (!tcpRead(client, buffer + 4, first) || !tcpTransfer(buffer, first + 4, false, received)) break;
        remaining -= first;
        bool ok = true;
        while (remaining) {
          unsigned count = min(remaining, 4096U);
          if (!tcpRead(client, buffer, count) || !tcpTransfer(buffer, count, false, received)) { ok = false; break; }
          remaining -= count;
        }
        if (!ok) break;
        captTCPCommands.fetch_add(1);
        if (group == 0xC0 || group == 0xD0) continue; // Data packets have no reply.
        if (!tcpTransfer(buffer, 64, true, received) || received < 4) break;
        unsigned replyOpcode = unsigned(buffer[0]) | (unsigned(buffer[1]) << 8);
        unsigned replyLength = unsigned(buffer[2]) | (unsigned(buffer[3]) << 8);
        if (replyOpcode != opcode || replyLength < unsigned(received) || replyLength > 4096) {
          captTCPError.store(ESP_ERR_INVALID_RESPONSE); break;
        }
        if(opcode==0xA0A8 && received>=26)captTCPCompleted.store(unsigned(buffer[24])|(unsigned(buffer[25])<<8));
        if (client.write(buffer, received) != size_t(received)) break;
        unsigned sent = received;
        while (sent < replyLength) {
          if (!tcpTransfer(buffer, min(4096U, ((replyLength - sent + 63) / 64) * 64), true, received) ||
              !received || sent + received > replyLength || client.write(buffer, received) != size_t(received)) {
            ok = false; break;
          }
          sent += received;
        }
        if (!ok) break;
      }
      releaseCapt(1);
    }
    client.stop();
    captTCPState.store(0);
  }
}

static void startCaptTCP() {
  if (xTaskCreate(captTCPTask, "capt-tcp", 8192, nullptr, 1, nullptr) != pdPASS)
    captTCPError.store(ESP_ERR_NO_MEM);
}
