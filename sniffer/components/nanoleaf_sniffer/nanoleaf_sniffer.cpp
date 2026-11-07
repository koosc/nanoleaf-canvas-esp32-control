#include "nanoleaf_sniffer.h"
#include "sniffer_web_page.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include "hal/gpio_ll.h"
#include "hal/uart_ll.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace esphome {
namespace nanoleaf_sniffer {

static const char *const TAG = "nanoleaf_sniffer";

#define NANOLEAF_UART_NUM UART_NUM_1
#define BAUD_RATE 1000000

static std::string to_hex(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(len * 2);
  char h[3];
  for (size_t i = 0; i < len; i++) {
    snprintf(h, sizeof(h), "%02X", data[i]);
    out += h;
  }
  return out;
}

static bool get_int_param(AsyncWebServerRequest *request, const char *name, int lo, int hi, int &out) {
  auto *p = request->getParam(name);
  if (p == nullptr)
    return false;
  const std::string &s = p->value();
  char *end = nullptr;
  long v = strtol(s.c_str(), &end, 10);
  if (s.empty() || end == nullptr || *end != '\0' || v < lo || v > hi)
    return false;
  out = (int) v;
  return true;
}

// ---------------------------------------------------------------------------
// Web Handler
// ---------------------------------------------------------------------------
class SnifferWebHandler : public AsyncWebHandler {
 public:
  explicit SnifferWebHandler(NanoleafSniffer *parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    std::string url = std::string(request->url_to(url_buf));
    return (url == "/" || url == "/sniffer" || url.rfind("/api/", 0) == 0);
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    std::string url = std::string(request->url_to(url_buf));

    if (url == "/" || url == "/sniffer") {
      request->send(200, "text/html", SNIFFER_HTML);
      return;
    }

    if (url == "/api/packets") {
      int since = 0, limit = 100;
      get_int_param(request, "since", 0, 2000000000, since);
      get_int_param(request, "limit", 1, 1000, limit);
      std::string json = this->parent_->get_packets_json((uint32_t) since, (size_t) limit);
      request->send(200, "application/json", json.c_str());
      return;
    }

    if (url == "/api/clear") {
      this->parent_->clear_buffer();
      request->send(200, "application/json", "{\"status\":\"cleared\"}");
      return;
    }

    if (url == "/api/status") {
      std::string json = this->parent_->get_status_json();
      request->send(200, "application/json", json.c_str());
      return;
    }

    if (url == "/api/filter") {
      int polls = 1;
      get_int_param(request, "polls", 0, 1, polls);
      this->parent_->set_filter_polls_runtime(polls != 0);
      std::string resp = "{\"filter_polls\":" + std::string(polls != 0 ? "true" : "false") + "}";
      request->send(200, "application/json", resp.c_str());
      return;
    }

    if (url == "/api/mode") {
      auto *p = request->getParam("mode");
      if (p != nullptr) {
        bool emu = (p->value() == "emulator");
        this->parent_->set_emulator_mode_runtime(emu);
      }
      std::string resp = "{\"emulator_mode\":" + std::string(this->parent_->get_emulator_mode() ? "true" : "false") + "}";
      request->send(200, "application/json", resp.c_str());
      return;
    }

    if (url == "/api/pin") {
      int pin = -1;
      if (get_int_param(request, "pin", 0, 21, pin)) {
        this->parent_->set_data_pin_runtime(pin);
      }
      std::string resp = "{\"data_pin\":" + std::to_string(this->parent_->get_data_pin()) + "}";
      request->send(200, "application/json", resp.c_str());
      return;
    }

    if (url == "/api/send") {
      auto *p = request->getParam("hex");
      if (p != nullptr) {
        const std::string &hex = p->value();
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i + 1 < hex.size(); i += 2) {
          bytes.push_back((uint8_t) strtol(hex.substr(i, 2).c_str(), nullptr, 16));
        }
        if (!bytes.empty()) {
          this->parent_->send_raw_bytes(bytes.data(), bytes.size());
          request->send(200, "application/json", "{\"status\":\"sent\"}");
          return;
        }
      }
      request->send(400, "application/json", "{\"error\":\"bad hex\"}");
      return;
    }

    if (url == "/api/hotplug") {
      this->parent_->trigger_hotplug();
      request->send(200, "application/json", "{\"status\":\"hotplug_triggered\"}");
      return;
    }

    if (url == "/api/reboot") {
      request->send(200, "application/json", "{\"status\":\"rebooting\"}");
      this->parent_->request_reboot();
      return;
    }

    request->send(404, "text/plain", "Not Found");
  }

 protected:
  NanoleafSniffer *parent_;
};

// ---------------------------------------------------------------------------
// Component Implementation
// ---------------------------------------------------------------------------
void NanoleafSniffer::setup() {
  ESP_LOGI(TAG, "Initializing Nanoleaf Canvas Bus Sniffer on GPIO%d...", this->data_pin_);
  configure_uart();

  if (web_server_base::global_web_server_base != nullptr) {
    web_server_base::global_web_server_base->add_handler(new SnifferWebHandler(this));
    ESP_LOGI(TAG, "Web UI and API handler attached to port 80");
  } else {
    ESP_LOGW(TAG, "web_server_base is null; Web UI not attached");
  }
}

void NanoleafSniffer::configure_uart() {
  if (uart_is_driver_installed(NANOLEAF_UART_NUM)) {
    uart_driver_delete(NANOLEAF_UART_NUM);
  }

  uart_config_t uart_config = {
      .baud_rate = BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 120,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(NANOLEAF_UART_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_driver_install(NANOLEAF_UART_NUM, 4096, 1024, 0, NULL, 0));

  gpio_reset_pin((gpio_num_t) this->data_pin_);

  if (this->emulator_mode_) {
    // Emulator mode: Needs open-drain bidirectional TX + RX
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << this->data_pin_),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_drive_capability((gpio_num_t) this->data_pin_, GPIO_DRIVE_CAP_1);
    esp_rom_gpio_connect_out_signal((gpio_num_t) this->data_pin_, U1TXD_OUT_IDX, false, false);
    esp_rom_gpio_connect_in_signal((gpio_num_t) this->data_pin_, U1RXD_IN_IDX, false);
    ESP_LOGI(TAG, "UART1 configured in EMULATOR mode (Open-Drain TX/RX) on GPIO%d", this->data_pin_);
  } else {
    // Passive mode: STRICTLY input (High-Z) - TX is NOT connected to GPIO!
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << this->data_pin_),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    esp_rom_gpio_connect_in_signal((gpio_num_t) this->data_pin_, U1RXD_IN_IDX, false);
    ESP_LOGI(TAG, "UART1 configured in PASSIVE SNIFFER mode (High-Z RX only) on GPIO%d", this->data_pin_);
  }
}

void NanoleafSniffer::set_data_pin_runtime(int pin) {
  if (pin < 0 || pin > 21) return;
  this->data_pin_ = pin;
  configure_uart();
}

void NanoleafSniffer::set_emulator_mode_runtime(bool emu) {
  if (this->emulator_mode_ == emu) return;
  this->emulator_mode_ = emu;
  configure_uart();
}

void NanoleafSniffer::clear_buffer() {
  std::lock_guard<std::mutex> lock(this->buffer_mutex_);
  this->ring_head_ = 0;
  this->ring_count_ = 0;
}

void NanoleafSniffer::send_emulator_reply(const uint8_t *data, size_t len) {
  if (len == 0) return;
  // Ensure TX output is mapped
  esp_rom_gpio_connect_out_signal((gpio_num_t) this->data_pin_, U1TXD_OUT_IDX, false, false);
  // Drive push-pull for crisp 1 Mbaud edges, then release immediately
  gpio_ll_od_disable(&GPIO, (gpio_num_t) this->data_pin_);
  uart_write_bytes(NANOLEAF_UART_NUM, (const char *) data, len);
  uart_dev_t *hw = UART_LL_GET_HW(NANOLEAF_UART_NUM);
  uint32_t spin_start = micros();
  while (!uart_ll_is_tx_idle(hw) && micros() - spin_start < 20000) {
  }
  gpio_ll_od_enable(&GPIO, (gpio_num_t) this->data_pin_);
  // Flush our own echoed bytes from the RX FIFO and assembly buffer so we never parse what we transmitted
  uart_flush_input(NANOLEAF_UART_NUM);
  this->rx_asm_len_ = 0;
}

void NanoleafSniffer::send_raw_bytes(const uint8_t *data, size_t len) {
  send_emulator_reply(data, len);
}

void NanoleafSniffer::commit_packet(const uint8_t *data, size_t len, uint64_t now_us) {
  if (len == 0) return;
  this->stats_.total_packets++;
  this->stats_.last_packet_us = now_us;

  // Emulator Mode Handlers
  if (this->emulator_mode_) {
    // 1-byte C0 status poll from master
    if (len == 1 && data[0] == 0xC0) {
      if (this->trigger_hotplug_) {
        this->trigger_hotplug_ = false; // Send CC hotplug marker once
        this->emulator_enumerated_ = false;
        static const uint8_t cc_reply[1] = {0xCC};
        send_emulator_reply(cc_reply, sizeof(cc_reply));
      } else if (this->emulator_enumerated_) {
        // Only reply with 00 00 once successfully enumerated!
        static const uint8_t poll_reply[2] = {0x00, 0x00};
        send_emulator_reply(poll_reply, sizeof(poll_reply));
      }
      if (this->filter_polls_) {
        this->stats_.c0_polls_filtered++;
        return;
      }
    }
    // 1-byte 80 layout query from master
    else if (len == 1 && data[0] == 0x80) {
      this->trigger_hotplug_ = false;
      this->emulator_enumerated_ = true;
      // 1-panel leaf layout reply: Canvas node (0xC0) with 3 separators (0x00, 0x00, 0x00).
      // Note: Must NEVER send 0x40 terminator; only root master appends 0x40.
      static const uint8_t layout_reply[] = {0xC0, 0x00, 0x00, 0x00};
      send_emulator_reply(layout_reply, sizeof(layout_reply));
    }
    // Node addressing / handshake queries (F8)
    else if (len >= 4 && data[0] == 0xF8) {
      uint8_t sub = data[3];
      if (sub == 0x81) {
        // Version / type query -> reply 01 (ACK "me") + 2 version bytes
        static const uint8_t ver_reply[] = {0x01, 0x03, 0x02};
        send_emulator_reply(ver_reply, sizeof(ver_reply));
      } else if (sub == 0x82) {
        // UID query -> reply 01 (ACK "me") + 18 bytes UID
        static const uint8_t uid_reply[19] = {0x01, 0x4E, 0x4C, 0x32, 0x39, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
        send_emulator_reply(uid_reply, sizeof(uid_reply));
      }
    }
  }

  // Filter routine idle C0 polls if requested
  if (this->filter_polls_) {
    if (len == 1 && data[0] == 0xC0) {
      this->stats_.c0_polls_filtered++;
      return;
    }
    // Poll replies that are all 00 or 10 (idle / release)
    if (len >= 2 && (len % 2 == 0) && data[0] != 0xFC && data[0] != 0xF8 && data[0] != 0xE0) {
      bool is_idle_poll_reply = true;
      for (size_t i = 0; i < len; i += 2) {
        if (data[i] == 0xCC || (data[i] != 0x00 && data[i] != 0x10 && data[i] != 0x20 && data[i] != 0x30)) {
          is_idle_poll_reply = false;
          break;
        }
      }
      if (is_idle_poll_reply) {
        this->stats_.c0_polls_filtered++;
        return;
      }
    }
  }

  // Update categorical stats
  if (data[0] == 0xFC && len >= 2) {
    if (data[1] == 0x06) this->stats_.color_frames++;
    else if (data[1] == 0x04) this->stats_.brightness_frames++;
  } else if (data[0] == 0xF8 && len >= 4) {
    if (data[3] == 0x06) this->stats_.color_frames++;
    else if (data[3] == 0x04) this->stats_.brightness_frames++;
    this->stats_.node_frames++;
  } else if (data[0] == 0x80 || (data[len - 1] == 0x40 && (data[0] & 0x80))) {
    this->stats_.layout_frames++;
  } else if (data[0] == 0xE0 && len >= 2 && data[1] == 0x03) {
    this->stats_.color_frames++;
  } else if (data[0] != 0xC0 && data[0] != 0x00) {
    this->stats_.unknown_frames++;
  }

  // Commit to ring buffer
  std::lock_guard<std::mutex> lock(this->buffer_mutex_);
  RawPacket &rp = this->packet_ring_[this->ring_head_];
  rp.id = this->next_packet_id_++;
  rp.timestamp_us = now_us;
  rp.delta_us = this->last_packet_time_us_ == 0 ? 0 : (uint32_t) (now_us - this->last_packet_time_us_);
  this->last_packet_time_us_ = now_us;
  rp.len = (uint16_t) std::min<size_t>(len, sizeof(rp.data));
  memcpy(rp.data, data, rp.len);

  this->ring_head_ = (this->ring_head_ + 1) % MAX_PACKETS;
  if (this->ring_count_ < MAX_PACKETS) {
    this->ring_count_++;
  }
}

void NanoleafSniffer::decode_packet(const RawPacket &raw, DecodedPacket &out) {
  out.id = raw.id;
  out.time_s = (double) raw.timestamp_us / 1000000.0;
  out.delta_ms = (double) raw.delta_us / 1000.0;
  out.len = raw.len;
  out.hex = to_hex(raw.data, raw.len);
  out.type = "Unknown";
  out.desc = out.hex;

  if (raw.len == 0) return;

  const uint8_t *d = raw.data;
  size_t l = raw.len;

  if (d[0] == 0xFC && l >= 2) {
    switch (d[1]) {
      case 0x04: // Set Global Brightness
        if (l >= 3) {
          out.type = "FC 04 (Brightness)";
          out.bri = d[2];
          char buf[64];
          snprintf(buf, sizeof(buf), "Global Brightness: %u/255 (%u%%)", d[2], (unsigned) (d[2] * 100 / 255));
          out.desc = buf;
        }
        break;
      case 0x06: // Set Global Color
        if (l >= 7) {
          out.type = "FC 06 (Color)";
          out.r = d[2];
          out.g = d[3];
          out.b = d[4];
          out.w = d[5];
          out.trans = d[6];
          char buf[96];
          snprintf(buf, sizeof(buf), "Global Color: R=%u G=%u B=%u W=%u Trans=%u", d[2], d[3], d[4], d[5], d[6]);
          out.desc = buf;
        }
        break;
      case 0x07:
        out.type = "FC 07 (Touch Enable)";
        out.desc = (l >= 3 && d[2] == 0x01) ? "Touch Reporting: ENABLED" : "Touch Reporting: DISABLED";
        break;
      case 0x08:
        out.type = "FC 08 (Keep-Alive)";
        out.desc = "Keep-Alive Ping";
        break;
      default:
        out.type = "FC " + to_hex(d + 1, 1);
        out.desc = "Global Command 0x" + to_hex(d + 1, 1);
        break;
    }
  } else if (d[0] == 0xF8 && l >= 4) {
    out.node_id = d[1] | (d[2] << 8);
    switch (d[3]) {
      case 0x04: // Node Brightness
        if (l >= 5) {
          out.type = "F8 04 (Node Bri)";
          out.bri = d[4];
          char buf[64];
          snprintf(buf, sizeof(buf), "Node %d Brightness: %u/255", out.node_id, d[4]);
          out.desc = buf;
        }
        break;
      case 0x06: // Node Color
        if (l >= 8) {
          out.type = "F8 06 (Node Color)";
          out.r = d[4];
          out.g = d[5];
          out.b = d[6];
          out.w = d[7];
          out.trans = (l >= 9) ? d[8] : 1;
          char buf[96];
          snprintf(buf, sizeof(buf), "Node %d Color: R=%u G=%u B=%u W=%u Trans=%u", out.node_id, d[4], d[5], d[6], d[7], out.trans);
          out.desc = buf;
        }
        break;
      case 0x81:
        out.type = "F8 81 (Get Version)";
        out.desc = "Query Node " + std::to_string(out.node_id) + " Firmware Version";
        break;
      case 0x82:
        out.type = "F8 82 (Get UID)";
        out.desc = "Query Node " + std::to_string(out.node_id) + " Hardware UID";
        break;
      default:
        out.type = "F8 Cmd " + to_hex(d + 3, 1);
        out.desc = "Node " + std::to_string(out.node_id) + " Command 0x" + to_hex(d + 3, 1);
        break;
    }
  } else if (d[0] == 0x00 && l == 1) {
    out.type = "00 (Root Detect)";
    out.desc = "Root Side Detect Broadcast";
  } else if (d[0] == 0x80 && l == 1) {
    out.type = "80 (Layout Query)";
    out.desc = "Layout Detect Query Broadcast";
  } else if (d[l - 1] == 0x40 && (d[0] & 0x80)) {
    out.type = "Layout Reply";
    int panels = 0, psus = 0;
    for (size_t i = 0; i < l; i++) {
      if (d[i] & 0x80) {
        uint8_t code = (d[i] >> 4) & 7;
        if (code == 1) psus++;
        else panels++;
      }
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "Layout Response: %d Panels, %d PSU (%u bytes)", panels, psus, (unsigned) l);
    out.desc = buf;
  } else if (d[0] == 0xC0 && l == 1) {
    out.type = "C0 (Bulk Poll)";
    out.desc = "Bulk Status Poll Request";
  } else if (d[0] == 0xE0 && l >= 2 && d[1] == 0x03) {
    out.type = "E0 03 (Bulk Push)";
    char buf[64];
    snprintf(buf, sizeof(buf), "Shapes Bulk Push Frame (%u bytes)", (unsigned) l);
    out.desc = buf;
  } else if (d[0] == 0xCC) {
    out.type = "CC (Hotplug)";
    out.desc = "Hot-Plug / Topology Change Marker";
  } else if (l >= 2 && (l % 2 == 0) && (d[0] == 0x00 || d[0] == 0x10 || d[0] == 0x20 || d[0] == 0x30)) {
    out.type = "Poll Reply";
    char buf[64];
    snprintf(buf, sizeof(buf), "Poll Reply: %u Panels (%u bytes)", (unsigned) (l / 2), (unsigned) l);
    out.desc = buf;
  }
}

void NanoleafSniffer::loop() {
  // Read incoming bytes from 1 Mbaud UART buffer
  uint8_t chunk[256];
  int n = uart_read_bytes(NANOLEAF_UART_NUM, chunk, sizeof(chunk), 0);

  uint64_t now_us = esp_timer_get_time();

  if (n > 0) {
    this->stats_.total_rx_bytes += n;
    for (int i = 0; i < n; i++) {
      if (this->last_raw_count_ < sizeof(this->last_raw_bytes_)) {
        this->last_raw_bytes_[this->last_raw_count_++] = chunk[i];
      } else {
        memmove(this->last_raw_bytes_, this->last_raw_bytes_ + 1, sizeof(this->last_raw_bytes_) - 1);
        this->last_raw_bytes_[sizeof(this->last_raw_bytes_) - 1] = chunk[i];
      }
    }

    for (int i = 0; i < n; i++) {
      uint8_t b = chunk[i];

      // Delineate by idle gap timeout (> 150 µs of bus silence indicates end of packet)
      if (this->rx_asm_len_ > 0 && (now_us - this->rx_last_byte_us_ > 150)) {
        commit_packet(this->rx_asm_buf_, this->rx_asm_len_, this->rx_last_byte_us_);
        this->rx_asm_len_ = 0;
      }

      this->rx_asm_buf_[this->rx_asm_len_++] = b;
      this->rx_last_byte_us_ = now_us;

      // Protocol-aware early packet commitment for known packet boundaries
      bool commit_now = false;
      uint8_t op = this->rx_asm_buf_[0];
      size_t cur_len = this->rx_asm_len_;

      if (op == 0x80 && cur_len == 1) {
        commit_now = true;
      } else if (op == 0xC0 && cur_len == 1) {
        commit_now = true;
      } else if (op == 0xFC && cur_len >= 2) {
        uint8_t sub = this->rx_asm_buf_[1];
        if (sub == 0x04 && cur_len == 3) commit_now = true;      // Brightness
        else if (sub == 0x06 && cur_len == 7) commit_now = true; // Color
        else if (sub == 0x07 && cur_len == 3) commit_now = true; // Touch
        else if (sub == 0x08 && cur_len == 3) commit_now = true; // Keepalive
      } else if (op == 0xF8 && cur_len >= 4) {
        uint8_t sub = this->rx_asm_buf_[3];
        if (sub == 0x04 && cur_len == 5) commit_now = true;      // Node brightness
        else if (sub == 0x06 && cur_len == 8) commit_now = true; // Node color
        else if (sub == 0x81 && cur_len == 4) commit_now = true; // Version
        else if (sub == 0x82 && cur_len == 4) commit_now = true; // UID
      } else if (b == 0x40 && (op & 0x80) && cur_len >= 3) {
        commit_now = true; // Layout string terminator
      } else if (cur_len >= sizeof(this->rx_asm_buf_)) {
        commit_now = true; // Buffer full safeguard
      }

      if (commit_now) {
        commit_packet(this->rx_asm_buf_, this->rx_asm_len_, now_us);
        this->rx_asm_len_ = 0;
      }
    }
  } else {
    // If no bytes received, check if line has gone idle and pending assembled frame should be committed
    if (this->rx_asm_len_ > 0 && (now_us - this->rx_last_byte_us_ > 150)) {
      commit_packet(this->rx_asm_buf_, this->rx_asm_len_, this->rx_last_byte_us_);
      this->rx_asm_len_ = 0;
    }
  }
}

std::string NanoleafSniffer::get_packets_json(uint32_t since_id, size_t limit) {
  std::vector<RawPacket> matched;
  {
    std::lock_guard<std::mutex> lock(this->buffer_mutex_);
    matched.reserve(std::min<size_t>(limit, this->ring_count_));

    size_t start_idx = (this->ring_head_ + MAX_PACKETS - this->ring_count_) % MAX_PACKETS;
    for (size_t i = 0; i < this->ring_count_; i++) {
      size_t idx = (start_idx + i) % MAX_PACKETS;
      if (this->packet_ring_[idx].id > since_id) {
        matched.push_back(this->packet_ring_[idx]);
        if (matched.size() >= limit) break;
      }
    }
  }

  std::stringstream ss;
  ss << "{\"packets\":[";
  for (size_t i = 0; i < matched.size(); i++) {
    DecodedPacket dec;
    decode_packet(matched[i], dec);

    if (i > 0) ss << ",";
    ss << "{\"id\":" << dec.id
       << ",\"time_s\":" << dec.time_s
       << ",\"delta_ms\":" << dec.delta_ms
       << ",\"len\":" << dec.len
       << ",\"hex\":\"" << dec.hex << "\""
       << ",\"type\":\"" << dec.type << "\""
       << ",\"desc\":\"" << dec.desc << "\"";

    if (dec.r >= 0) ss << ",\"r\":" << dec.r << ",\"g\":" << dec.g << ",\"b\":" << dec.b << ",\"w\":" << dec.w << ",\"trans\":" << dec.trans;
    if (dec.bri >= 0) ss << ",\"bri\":" << dec.bri;
    if (dec.node_id >= 0) ss << ",\"node_id\":" << dec.node_id;

    ss << "}";
  }
  ss << "],\"buffer_count\":" << this->ring_count_
     << ",\"latest_id\":" << (this->next_packet_id_ - 1)
     << ",\"stats\":" << get_status_json() << "}";

  return ss.str();
}

std::string NanoleafSniffer::get_status_json() {
  int pin_level = gpio_get_level((gpio_num_t) this->data_pin_);
  std::stringstream ss;
  ss << "{\"total_packets\":" << this->stats_.total_packets
     << ",\"total_rx_bytes\":" << this->stats_.total_rx_bytes
     << ",\"c0_polls\":" << this->stats_.c0_polls_filtered
     << ",\"color_frames\":" << this->stats_.color_frames
     << ",\"brightness_frames\":" << this->stats_.brightness_frames
     << ",\"layout_frames\":" << this->stats_.layout_frames
     << ",\"node_frames\":" << this->stats_.node_frames
     << ",\"unknown_frames\":" << this->stats_.unknown_frames
     << ",\"filter_polls\":" << (this->filter_polls_ ? "true" : "false")
     << ",\"mode\":\"" << (this->emulator_mode_ ? "emulator" : "passive") << "\""
     << ",\"data_pin\":" << this->data_pin_
     << ",\"pin_level\":" << pin_level
     << ",\"raw_hex\":\"" << to_hex(this->last_raw_bytes_, this->last_raw_count_) << "\""
     << ",\"uptime_s\":" << (millis() / 1000)
     << "}";
  return ss.str();
}

void NanoleafSniffer::dump_config() {
  ESP_LOGCONFIG(TAG, "Nanoleaf Canvas Bus Sniffer:");
  ESP_LOGCONFIG(TAG, "  Data Pin: GPIO%d", this->data_pin_);
  ESP_LOGCONFIG(TAG, "  Baud Rate: %d", BAUD_RATE);
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->emulator_mode_ ? "Emulator" : "Passive Sniffer");
  ESP_LOGCONFIG(TAG, "  Filter C0 Polls: %s", YESNO(this->filter_polls_));
}

}  // namespace nanoleaf_sniffer
}  // namespace esphome
