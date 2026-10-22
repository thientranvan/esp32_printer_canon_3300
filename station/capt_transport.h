#pragma once

// ponytail: one diagnostic operation at a time, 4096 bytes; add job ownership
// and a spool before accepting concurrent print clients.
struct CaptCommand {
  std::atomic<int> state{0}; // 0 idle, 1 queued, 2 active, 3 result, 4 cancelling, 5 filling, 6 maintenance
  uint8_t data[4096];
  int size = 0;
  int received = 0;
  int error = 0;
  bool input = false;
  bool done = false; // USB client task only
  bool cancelled = false;
  uint32_t started = 0;
  usb_transfer_t *transfer = nullptr;
};
static CaptCommand captCommand;
static portMUX_TYPE captOwnerLock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t captOwner = 0; // 1 = TCP session; >=2 = authenticated HTTP client
static uint32_t captOwnerAt = 0;

static bool claimCapt(uint32_t owner) {
  portENTER_CRITICAL(&captOwnerLock);
  int state = captCommand.state.load();
  if (captOwner >= 2 && uint32_t(millis() - captOwnerAt) >= 30000 && (state == 0 || state == 3))
    captOwner = 0;
  bool acquired = owner && (!captOwner || captOwner == owner);
  if (acquired) { captOwner = owner; captOwnerAt = millis(); }
  portEXIT_CRITICAL(&captOwnerLock);
  return acquired;
}

static void releaseCapt(uint32_t owner) {
  portENTER_CRITICAL(&captOwnerLock);
  if (captOwner == owner) captOwner = 0;
  portEXIT_CRITICAL(&captOwnerLock);
}

static void captTransferDone(usb_transfer_t *) { captCommand.done = true; }

static void serviceCapt(USBContext &context) {
  auto &command = captCommand;
  int state = command.state.load();
  if (state == 1) {
    if (context.probe && !context.gone) {
      if (uint32_t(millis() - context.probeAt) >= 5000) {
        command.error = ESP_ERR_TIMEOUT;
        command.state.store(3);
      }
      return;
    }
    if (!context.device || context.gone || !context.claimed || context.probe) {
      command.error = ESP_ERR_INVALID_STATE;
      command.state.store(3);
      return;
    }
    command.done = false;
    command.cancelled = false;
    command.received = 0;
    command.error = usb_host_transfer_alloc(command.size, 0, &command.transfer);
    if (command.error != ESP_OK) { command.state.store(3); return; }
    auto *transfer = command.transfer;
    transfer->device_handle = context.device;
    transfer->bEndpointAddress = command.input ? 0x82 : 0x01;
    transfer->num_bytes = command.size;
    transfer->callback = captTransferDone;
    if (!command.input) memcpy(transfer->data_buffer, command.data, command.size);
    command.error = usb_host_transfer_submit(transfer);
    if (command.error != ESP_OK) {
      usb_host_transfer_free(transfer);
      command.transfer = nullptr;
      command.state.store(3);
      return;
    }
    command.started = millis();
    command.state.store(2);
    state = 2;
  }
  if ((state == 2 || state == 4) && command.done) {
    auto *transfer = command.transfer;
    if (!command.cancelled) {
      command.error = transfer->status == USB_TRANSFER_STATUS_COMPLETED ? ESP_OK : ESP_FAIL;
      if (command.error == ESP_OK) {
        if (command.input) {
          command.received = transfer->actual_num_bytes;
          memcpy(command.data, transfer->data_buffer, command.received);
        } else if (transfer->actual_num_bytes != command.size) command.error = ESP_ERR_INVALID_SIZE;
      }
    }
    if (!context.gone && (command.cancelled || command.error != ESP_OK)) {
      esp_err_t cleared = usb_host_endpoint_clear(context.device, transfer->bEndpointAddress);
      if (cleared != ESP_OK && command.error == ESP_OK) command.error = cleared;
    }
    usb_host_transfer_free(transfer);
    command.transfer = nullptr;
    command.state.store(3);
  } else if (state == 2 && uint32_t(millis() - command.started) >= 5000) {
    command.cancelled = true;
    command.error = ESP_ERR_TIMEOUT;
    command.state.store(4);
    // timeout_ms is unsupported by this SDK. Halt/flush, then wait for callback;
    // never free an in-flight transfer, even if cancellation itself fails.
    esp_err_t error = usb_host_endpoint_halt(context.device, command.transfer->bEndpointAddress);
    if (error == ESP_OK) error = usb_host_endpoint_flush(context.device, command.transfer->bEndpointAddress);
    if (error != ESP_OK) command.error = error;
  }
}
