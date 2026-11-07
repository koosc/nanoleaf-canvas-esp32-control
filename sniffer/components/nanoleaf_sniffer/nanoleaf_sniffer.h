#pragma once

#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace esphome {
namespace nanoleaf_sniffer {

struct RawPacket {
  uint32_t id;
  uint64_t timestamp_us;
  uint32_t delta_us;
  uint16_t len;
  uint8_t data[64];
};

struct DecodedPacket {
  uint32_t id;
  double time_s;
  double delta_ms;
  uint16_t len;
  std::string hex;
  std::string type;
  std::string desc;
  int r{-1};
  int g{-1};
  int b{-1};
  int w{-1};
  int bri{-1};
  int trans{-1};
  int node_id{-1};
};

struct SnifferStats {
  uint32_t total_packets{0};
  uint32_t total_rx_bytes{0};
  uint32_t c0_polls_filtered{0};
  uint32_t color_frames{0};
  uint32_t brightness_frames{0};
  uint32_t layout_frames{0};
  uint32_t node_frames{0};
  uint32_t unknown_frames{0};
  uint32_t dropped_overflow{0};
  uint64_t last_packet_us{0};
};

class NanoleafSniffer : public Component {
 public:
  void set_data_pin(int pin) { data_pin_ = pin; }
  void set_filter_polls(bool filter) { filter_polls_ = filter; }
  void set_emulator_mode(bool emu) { emulator_mode_ = emu; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Buffer management
  void clear_buffer();
  void request_reboot() {
    this->set_timeout(250, []() { App.safe_reboot(); });
  }
  void set_data_pin_runtime(int pin);
  int get_data_pin() const { return data_pin_; }
  void set_filter_polls_runtime(bool filter) { filter_polls_ = filter; }
  bool get_filter_polls() const { return filter_polls_; }
  void set_emulator_mode_runtime(bool emu);
  bool get_emulator_mode() const { return emulator_mode_; }
  void trigger_hotplug() { trigger_hotplug_ = true; }
  void send_raw_bytes(const uint8_t *data, size_t len);

  std::string get_packets_json(uint32_t since_id, size_t limit);
  std::string get_status_json();

 protected:
  void configure_uart();
  void rx_process_byte(uint8_t byte, uint64_t now_us);
  void commit_packet(const uint8_t *data, size_t len, uint64_t now_us);
  void decode_packet(const RawPacket &raw, DecodedPacket &out);
  void send_emulator_reply(const uint8_t *data, size_t len);

  int data_pin_{4};
  bool filter_polls_{true};
  bool emulator_mode_{false};
  bool emulator_enumerated_{false};
  bool trigger_hotplug_{false};

  // Ring buffer
  static constexpr size_t MAX_PACKETS = 1000;
  RawPacket packet_ring_[MAX_PACKETS];
  size_t ring_head_{0};
  size_t ring_count_{0};
  uint32_t next_packet_id_{1};
  uint64_t last_packet_time_us_{0};

  // Assembler buffer for incoming stream
  uint8_t rx_asm_buf_[256];
  size_t rx_asm_len_{0};
  uint64_t rx_last_byte_us_{0};
  uint8_t last_raw_bytes_[32]{0};
  size_t last_raw_count_{0};

  SnifferStats stats_{};
  std::mutex buffer_mutex_;
};

}  // namespace nanoleaf_sniffer
}  // namespace esphome
