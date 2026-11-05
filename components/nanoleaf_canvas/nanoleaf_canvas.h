#pragma once

#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace esphome {
namespace nanoleaf_canvas {

struct CanvasTile {
  uint16_t id;
  int16_t x;
  int16_t y;
  uint8_t rot;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t status;
  // Last color actually sent to the panel
  uint8_t sent_r;
  uint8_t sent_g;
  uint8_t sent_b;
};

// A unit of work handed from the HTTP server task to the main loop. All bus I/O
// must happen on the main loop; the web server runs in its own FreeRTOS task.
struct LoopJob {
  std::function<std::string()> fn;
  std::string result;
  std::atomic<bool> done{false};
};

class NanoleafCanvas : public Component {
 public:
  void set_data_pin(int pin) { data_pin_ = pin; }
  void set_push_pull_tx(bool pp) { push_pull_ = pp; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // --- Main-loop-only API. These only update the desired state; the frames are
  // sent from loop() so that bursts of changes coalesce into one bus frame.
  bool enumerate_cluster();
  void set_brightness(uint8_t bri);
  void set_all_colors(uint8_t r, uint8_t g, uint8_t b);
  void set_tile_color(size_t index, uint8_t r, uint8_t g, uint8_t b);
  void set_transition(uint8_t trans) { transition_ = trans; }
  void request_reboot() {
    this->set_timeout(250, []() { App.safe_reboot(); });
  }

  const std::vector<CanvasTile> &get_tiles() const { return tiles_; }
  uint8_t get_brightness() const { return brightness_; }
  size_t get_panel_count() const { return tiles_.size(); }

  std::string get_tiles_json();
  std::string get_debug_json();
  std::string apply_bus_config(int push_pull, int gap_us, int no_poll, int auto_enum);
  std::string raw_exchange(const std::vector<uint8_t> &frame, int expect, uint32_t timeout_ms);

  // --- Light integration.
  void set_light_state(light::LightState *state) { light_state_ = state; }
  light::LightState *get_light_state() const { return light_state_; }
  // Set by effects when they stop, so the next light write repaints the solid color.
  void request_color_repaint() { repaint_on_next_write_ = true; }
  bool consume_color_repaint() {
    bool r = repaint_on_next_write_;
    repaint_on_next_write_ = false;
    return r;
  }

  // --- Thread-safe: may be called from the web server task.
  // Runs fn on the main loop and returns its result, or "" on timeout.
  bool run_on_loop(std::function<std::string()> fn, std::string &out, uint32_t timeout_ms = 3000);

 protected:
  void configure_bus();
  int xact(const uint8_t *frame, size_t len, uint8_t *reply, size_t max_reply, int expect, uint32_t timeout_ms,
           uint32_t gap_ms = 3, bool *echo_ok = nullptr);
  void poll_bulk();
  void flush_frames(uint32_t now);
  void send_brightness();
  void send_colors(bool force);
  void parse_layout_tree(const uint8_t *layout, size_t len);

  int data_pin_{4};
  bool enumerated_{false};
  bool enumerate_requested_{false};

  // Desired output state
  uint8_t brightness_{0};
  uint8_t transition_{1};
  uint8_t def_r_{255}, def_g_{255}, def_b_{255};
  bool sent_valid_{false};
  bool bri_dirty_{false};
  bool colors_dirty_{false};
  bool repaint_on_next_write_{false};
  uint32_t last_change_ms_{0};
  uint32_t last_flush_ms_{0};
  uint32_t last_refresh_ms_{0};
  uint32_t manual_until_ms_{0};

  // Bus tuning
  bool push_pull_{true};
  uint32_t gap_us_{100};
  bool no_poll_{false};
  uint32_t last_bus_us_{0};

  uint32_t last_poll_ms_{0};
  uint32_t last_re_enum_ms_{0};

  // Diagnostics
  uint32_t poll_count_{0};
  uint32_t poll_short_{0};
  uint32_t polls_silent_{0};
  uint32_t cc_events_{0};
  uint32_t cc_times_[16]{0};
  bool in_cc_{false};
  bool auto_enum_{true};
  uint32_t silent_events_{0};
  int last_short_rlen_{0};
  uint8_t last_short_reply_[64]{0};
  uint32_t echo_errors_{0};
  uint32_t tx_frames_{0};
  uint32_t enum_count_{0};
  uint32_t last_poll_us_{0};
  int last_poll_rlen_{0};
  uint8_t last_poll_reply_[64]{0};
  int last_layout_rlen_{0};
  uint8_t last_layout_reply_[128]{0};

  light::LightState *light_state_{nullptr};

  Mutex job_mutex_;
  LoopJob *pending_job_{nullptr};

  std::vector<CanvasTile> tiles_;
};

class CanvasRainbowWaveEffect : public light::LightEffect {
 public:
  explicit CanvasRainbowWaveEffect(NanoleafCanvas *parent) : LightEffect("Rainbow Wave"), parent_(parent) {}
  void start() override { last_ms_ = 0; }
  void stop() override { parent_->request_color_repaint(); }
  void apply() override;

 protected:
  NanoleafCanvas *parent_;
  uint32_t last_ms_{0};
  uint16_t hue_{0};
};

class CanvasStaticRainbowEffect : public light::LightEffect {
 public:
  explicit CanvasStaticRainbowEffect(NanoleafCanvas *parent) : LightEffect("Static Rainbow"), parent_(parent) {}
  void start() override { painted_ = false; }
  void stop() override { parent_->request_color_repaint(); }
  void apply() override;

 protected:
  NanoleafCanvas *parent_;
  bool painted_{false};
};

class NanoleafCanvasLight : public light::LightOutput {
 public:
  explicit NanoleafCanvasLight(NanoleafCanvas *parent) : parent_(parent) {}

  void setup_state(light::LightState *state) override;
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

 protected:
  NanoleafCanvas *parent_;
  bool have_last_{false};
  uint8_t last_r_{0}, last_g_{0}, last_b_{0};
};

}  // namespace nanoleaf_canvas
}  // namespace esphome
