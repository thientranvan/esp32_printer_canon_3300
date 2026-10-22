#pragma once
#include <usb/usb_host.h>
#include <soc/usb_struct.h>
#include <atomic>

struct USBStatus {
  bool installed;
  bool connected;
  bool responsive;
  bool recovering;
  uint32_t responseAt;
  uint32_t recoveries;
  int heartbeatError;
  int error;
  bool hostMode;
  bool physicalLink;
  bool powerCommand;
  uint32_t portRegister;
  int devices;
  uint32_t descriptorID;
  int descriptorClass;
  int descriptorCount;
  char details[1536];
};
static USBStatus usbStatus = {};
static bool usbTaskStarted = false;
static portMUX_TYPE usbLock = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<int> usbDaemonError{0};
static std::atomic<uint32_t> usbDescriptorID{0};
static std::atomic<int> usbDescriptorClass{0};
static std::atomic<int> usbDescriptorCount{0};

static bool usbEnumFilter(const usb_device_desc_t *descriptor, uint8_t *configuration) {
  usbDescriptorID.store((uint32_t(descriptor->idVendor) << 16) | descriptor->idProduct);
  usbDescriptorClass.store(descriptor->bDeviceClass);
  usbDescriptorCount.fetch_add(1);
  *configuration = 1;
  return true;
}

static void publishUSB(const USBStatus &status) {
  portENTER_CRITICAL(&usbLock);
  usbStatus = status;
  portEXIT_CRITICAL(&usbLock);
}

static USBStatus readUSB() {
  portENTER_CRITICAL(&usbLock);
  USBStatus status = usbStatus;
  portEXIT_CRITICAL(&usbLock);
  if (status.responseAt == 0 || uint32_t(millis() - status.responseAt) > 8000)
    status.responsive = false;
  if (status.installed) {
    // Read-only: port power bit is a controller command, not a VBUS voltage measurement.
    status.portRegister = USB0.hprt;
    status.hostMode = USB0.gintsts & 1;
    status.physicalLink = status.portRegister & 1;
    status.powerCommand = status.portRegister & (1 << 12);
    usb_host_lib_info_t info = {};
    if (usb_host_lib_info(&info) == ESP_OK) status.devices = info.num_devices;
    if (usbDaemonError.load()) status.error = usbDaemonError.load();
    status.descriptorID = usbDescriptorID.load();
    status.descriptorClass = usbDescriptorClass.load();
    status.descriptorCount = usbDescriptorCount.load();
  }
  return status;
}

static void usbDaemon(void *) {
  while (true) {
    uint32_t flags = 0;
    esp_err_t error = usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (error != ESP_OK) {
      usbDaemonError.store(error);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

struct USBContext {
  uint8_t address;
  bool gone;
  usb_device_handle_t device;
  usb_transfer_t *probe;
  bool probeDone;
  bool claimed;
  bool heartbeat;
  uint32_t probeAt;
};

static void usbEvent(const usb_host_client_event_msg_t *event, void *arg) {
  auto &context = *static_cast<USBContext *>(arg);
  // ponytail: one LBP3300; add per-printer state for multiple printers.
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV && !context.device)
    context.address = event->new_dev.address;
  if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
      event->dev_gone.dev_hdl == context.device) context.gone = true;
}

// Completion runs on the USB client task; never free an in-flight transfer.
static void printerProbeDone(usb_transfer_t *transfer) {
  static_cast<USBContext *>(transfer->context)->probeDone = true;
}

static esp_err_t startPrinterProbe(usb_host_client_handle_t client, USBContext &context) {
  esp_err_t error = ESP_OK;
  if (!context.claimed) {
    error = usb_host_interface_claim(client, context.device, 0, 0);
    if (error != ESP_OK) return error;
    context.claimed = true;
  }
  error = usb_host_transfer_alloc(1032, 0, &context.probe);
  if (error != ESP_OK) return error;
  auto *transfer = context.probe;
  // USB Printer Class GET_DEVICE_ID, configuration index 0, interface/alt 0.
  const uint8_t setup[] = {0xA1, 0, 0, 0, 0, 0, 0, 4};
  memcpy(transfer->data_buffer, setup, sizeof(setup));
  transfer->num_bytes = 1032;
  transfer->device_handle = context.device;
  transfer->bEndpointAddress = 0;
  transfer->callback = printerProbeDone;
  transfer->context = &context;
  error = usb_host_transfer_submit_control(client, transfer);
  context.probeAt = millis();
  if (error != ESP_OK) {
    usb_host_transfer_free(context.probe);
    context.probe = nullptr;
  }
  return error;
}

static esp_err_t startHeartbeat(usb_host_client_handle_t client, USBContext &context) {
  // LBP3300 supports GET_DEVICE_ID; GET_PORT_STATUS failed on real hardware.
  // Control endpoint polling cannot consume a CAPT bulk reply.
  context.heartbeat = true;
  return startPrinterProbe(client, context);
}

#include "capt_transport.h"

static void usbTask(void *) {
  USBStatus status = {};
  usb_host_config_t config = {};
  config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  config.peripheral_map = 1;
  config.enum_filter_cb = usbEnumFilter;
  esp_err_t error = usb_host_install(&config);
  if (error != ESP_OK) {
    status.error = error;
    snprintf(status.details, sizeof(status.details), "USB Host install: %s", esp_err_to_name(error));
    publishUSB(status);
    vTaskDelete(nullptr);
    return;
  }
  USBContext context = {};
  usb_host_client_handle_t client = nullptr;
  usb_host_client_config_t clientConfig = {};
  clientConfig.max_num_event_msg = 5;
  clientConfig.async.client_event_callback = usbEvent;
  clientConfig.async.callback_arg = &context;
  error = usb_host_client_register(&clientConfig, &client);
  if (error != ESP_OK) {
    status.error = error;
    snprintf(status.details, sizeof(status.details), "USB client: %s", esp_err_to_name(error));
    publishUSB(status);
    usb_host_uninstall();
    vTaskDelete(nullptr);
    return;
  }
  if (xTaskCreate(usbDaemon, "usb-daemon", 8192, nullptr, 2, nullptr) != pdPASS) {
    status.error = ESP_ERR_NO_MEM;
    snprintf(status.details, sizeof(status.details), "Cannot start USB daemon");
    publishUSB(status);
    usb_host_client_deregister(client);
    usb_host_uninstall();
    vTaskDelete(nullptr);
    return;
  }
  status.installed = true;
  snprintf(status.details, sizeof(status.details), "USB Host sẵn sàng. Chưa phát hiện thiết bị. Kiểm tra máy in bật nguồn, cáp OTG và VBUS 5 V.");
  publishUSB(status);
  uint32_t lastScan = 0;
  unsigned scanIndex = 0;
  uint32_t lastHeartbeat = 0;
  uint32_t lastRecovery = millis();
  uint32_t recoveryAt = 0;
  while (true) {
    error = usb_host_client_handle_events(client, pdMS_TO_TICKS(10));
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
      status.error = error;
      publishUSB(status);
    }
    if (context.probe && context.probeDone) {
      auto *transfer = context.probe;
      if (context.heartbeat) {
        status.responsive = false;
        if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes >= 10) {
          const uint8_t *data = transfer->data_buffer + 8;
          int length = (int(data[0]) << 8) | data[1];
          status.responsive = length > 2 && length <= transfer->actual_num_bytes - 8;
        }
        status.heartbeatError = status.responsive ? ESP_OK : ESP_FAIL;
        if (status.responsive) status.responseAt = millis();
      } else {
        String details = status.details;
        details += "\nGET_DEVICE_ID: ";
        if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes >= 10) {
          const uint8_t *data = transfer->data_buffer + 8;
          int count = transfer->actual_num_bytes - 8;
          int length = (int(data[0]) << 8) | data[1];
          if (length >= 2 && length <= count) {
            for (int i = 2; i < length; ++i)
              details += (data[i] >= 32 && data[i] <= 126) ? char(data[i]) : '?';
          } else details += "invalid IEEE 1284 length";
        } else details += "transfer status=" + String(int(transfer->status)) + " bytes=" + String(transfer->actual_num_bytes);
        details.toCharArray(status.details, sizeof(status.details));
      }
      usb_host_transfer_free(transfer);
      context.probe = nullptr;
      context.probeDone = false;
      context.heartbeat = false;
      publishUSB(status);
    }
    serviceCapt(context);
    if (context.gone && status.connected) {
      status.connected = false;
      status.responsive = false;
      snprintf(status.details, sizeof(status.details), "Thiết bị USB đã ngắt kết nối. Đang thu hồi giao dịch USB.");
      publishUSB(status);
    }
    // Native root-port reset keeps Wi-Fi/HTTP alive and does not resend job data.
    // Power command changes controller state; it is not a physical VBUS switch.
    bool stalledProbe = context.probe && uint32_t(millis() - context.probeAt) >= 5000;
    bool missing = !context.device || context.gone || (status.heartbeatError != ESP_OK);
    int idleState = captCommand.state.load();
    if (!status.recovering && !captCommand.transfer && (idleState == 0 || idleState == 3) &&
        uint32_t(millis() - lastRecovery) >= 30000 && (missing || stalledProbe) &&
        captCommand.state.compare_exchange_strong(idleState, 6)) {
      esp_err_t reset = usb_host_lib_set_root_port_power(false);
      lastRecovery = millis();
      status.heartbeatError = reset;
      if (reset == ESP_OK) {
        status.recovering = true;
        status.connected = false;
        status.responsive = false;
        ++status.recoveries;
        recoveryAt = millis();
      }
      publishUSB(status);
      captCommand.state.store(idleState);
    }
    if (status.recovering && uint32_t(millis() - recoveryAt) >= 1000) {
      esp_err_t reset = usb_host_lib_set_root_port_power(true);
      status.heartbeatError = reset;
      if (reset == ESP_OK) status.recovering = false;
      publishUSB(status);
    }
    if (!status.recovering && context.device && !context.gone && !context.probe &&
        !captCommand.transfer && uint32_t(millis() - lastHeartbeat) >= 3000) {
      lastHeartbeat = millis();
      status.heartbeatError = startHeartbeat(client, context);
      if (status.heartbeatError != ESP_OK) status.responsive = false;
      publishUSB(status);
    }
    if (context.gone && !context.probe && !captCommand.transfer) {
      if (context.claimed) {
        usb_host_interface_release(client, context.device, 0);
        context.claimed = false;
      }
      error = usb_host_device_close(client, context.device);
      context.device = nullptr;
      context.gone = false;
      context.address = 0;
      status.connected = false;
      status.error = error;
      snprintf(status.details, sizeof(status.details), "Thiết bị USB đã ngắt kết nối.");
      publishUSB(status);
    }
    // NEW_DEV hints can overwrite each other while a hub enumerates children.
    // Revisit the authoritative address list so discovery does not depend on event order.
    if (!status.recovering && !context.device && uint32_t(millis() - lastScan) >= 500) {
      lastScan = millis();
      uint8_t addresses[127];
      int count = 0;
      if (usb_host_device_addr_list_fill(sizeof(addresses), addresses, &count) == ESP_OK && count > 0)
        context.address = addresses[scanIndex++ % unsigned(count)];
    }
    if (!status.recovering && context.address && !context.device) {
      const uint8_t address = context.address;
      context.address = 0;
      error = usb_host_device_open(client, address, &context.device);
      const usb_device_desc_t *device = nullptr;
      const usb_config_desc_t *configuration = nullptr;
      if (error == ESP_OK) error = usb_host_get_device_descriptor(context.device, &device);
      if (error == ESP_OK) error = usb_host_get_active_config_descriptor(context.device, &configuration);
      if (error == ESP_OK && (device->idVendor != 0x04A9 || device->idProduct != 0x267E)) {
        usb_host_device_close(client, context.device);
        context.device = nullptr;
        continue;
      }
      status.error = error;
      status.connected = error == ESP_OK;
      if (error == ESP_OK) {
        char line[128];
        snprintf(line, sizeof(line), "VID=%04X PID=%04X USB=%04X class=%02X address=%u\n", device->idVendor, device->idProduct, device->bcdUSB, device->bDeviceClass, address);
        String details = line;
        const auto bytes = reinterpret_cast<const uint8_t *>(configuration);
        size_t offset = 0;
        while (offset + 2 <= configuration->wTotalLength) {
          uint8_t length = bytes[offset], type = bytes[offset + 1];
          if (length < 2 || offset + length > configuration->wTotalLength) {
            status.error = ESP_ERR_INVALID_SIZE;
            details += "Malformed configuration descriptor\n";
            break;
          }
          const uint8_t *d = bytes + offset;
          if (type == 4 && length >= 9) {
            snprintf(line, sizeof(line), "Interface %u alt=%u class=%02X subclass=%02X protocol=%02X endpoints=%u\n", d[2], d[3], d[5], d[6], d[7], d[4]);
            details += line;
          } else if (type == 5 && length >= 7) {
            snprintf(line, sizeof(line), "Endpoint 0x%02X %s type=%u max_packet=%u\n", d[2], d[2] & 0x80 ? "IN" : "OUT", d[3] & 3, d[4] | (d[5] << 8));
            details += line;
          }
          offset += length;
          if (details.length() > sizeof(status.details) - 160) { details += "[truncated]\n"; break; }
        }
        details.toCharArray(status.details, sizeof(status.details));
        status.error = startPrinterProbe(client, context);
      } else {
        snprintf(status.details, sizeof(status.details), "USB descriptor error: %s", esp_err_to_name(error));
        if (context.device) {
          usb_host_device_close(client, context.device);
          context.device = nullptr;
        }
      }
      publishUSB(status);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void startUSB() {
  if (usbTaskStarted) return;
  usbTaskStarted = true;
  if (xTaskCreate(usbTask, "usb-monitor", 6144, nullptr, 3, nullptr) != pdPASS) {
    usbTaskStarted = false;
    USBStatus status = {};
    status.error = ESP_ERR_NO_MEM;
    snprintf(status.details, sizeof(status.details), "Cannot start USB task");
    publishUSB(status);
  }
}
