#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include <vector>
#include <string>

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
  uint8_t w;
  uint8_t status;
};

class NanoleafCanvas : public Component {
 public:
  NanoleafCanvas();
  void set_data_pin(int pin) { data_pin_ = pin; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  bool enumerate_cluster();
  void set_global_brightness(uint8_t bri);
  void set_global_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0, uint8_t trans = 1);
  void set_node_color(uint16_t id, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0, uint8_t trans = 1);
  void set_bulk_push_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0, uint8_t trans = 1);
  void send_bulk_push_tiles(uint8_t trans = 1);
  bool test_bus_echo();
  std::string get_debug_json();

  void set_wave_animation(bool enable) { anim_wave_ = enable; }
  bool is_wave_animation() const { return anim_wave_; }

  void set_light_state(light::LightState *state) { this->light_state_ = state; }
  bool is_in_internal_sync() const { return in_internal_sync_; }
  void sync_to_light_state(bool is_on, uint8_t bri, uint8_t r, uint8_t g, uint8_t b);

  const std::vector<CanvasTile>& get_tiles() const { return tiles_; }
  uint8_t get_brightness() const { return cur_brightness_; }
  uint8_t get_cur_r() const { return cur_r_; }
  uint8_t get_cur_g() const { return cur_g_; }
  uint8_t get_cur_b() const { return cur_b_; }

  std::string get_tiles_json();

 protected:
  void configure_bus();
  int xact(const uint8_t *frame, size_t len, uint8_t *reply_buf, size_t max_reply, int expect = 0, uint32_t timeout_ms = 80);
  void poll_bulk();
  void parse_layout_tree(const uint8_t *layout, size_t len);

  int data_pin_{4};
  bool enumerated_{false};
  uint8_t n_panels_{1};
  uint8_t cur_brightness_{255};
  uint8_t cur_r_{255};
  uint8_t cur_g_{220};
  uint8_t cur_b_{180};
  uint8_t cur_w_{0};
  bool has_color_{false};
  uint32_t last_poll_ms_{0};
  uint32_t last_anim_ms_{0};
  uint32_t last_re_enum_ms_{0};
  uint16_t anim_hue_{0};
  bool anim_wave_{false};

  uint32_t poll_count_{0};
  uint32_t poll_success_{0};
  int last_poll_rlen_{0};
  uint8_t last_poll_reply_[64]{0};
  int last_layout_rlen_{0};
  uint8_t last_layout_reply_[64]{0};
  uint32_t tx_count_{0};

  light::LightState *light_state_{nullptr};
  bool in_internal_sync_{false};

  std::vector<CanvasTile> tiles_;
};

class NanoleafCanvasLight : public light::LightOutput {
 public:
  explicit NanoleafCanvasLight(NanoleafCanvas *parent) : parent_(parent) {}

  void setup_state(light::LightState *state) override {
    this->parent_->set_light_state(state);
  }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::RGB});
    return traits;
  }

  void write_state(light::LightState *state) override {
    if (this->parent_->is_in_internal_sync()) {
      return;
    }
    float r = 0.0f, g = 0.0f, b = 0.0f;
    state->current_values_as_rgb(&r, &g, &b);
    float brightness = state->current_values.get_brightness();
    bool is_on = state->current_values.is_on();

    if (!is_on || brightness <= 0.001f) {
      this->parent_->set_wave_animation(false);
      this->parent_->set_global_brightness(0);
    } else {
      uint8_t bri = (uint8_t)(brightness * 255.0f);
      uint8_t red = (uint8_t)(r * 255.0f);
      uint8_t green = (uint8_t)(g * 255.0f);
      uint8_t blue = (uint8_t)(b * 255.0f);
      this->parent_->set_wave_animation(false);
      this->parent_->set_global_brightness(bri);
      this->parent_->set_global_color(red, green, blue, 0, 2);
    }
  }

 protected:
  NanoleafCanvas *parent_;
};

}  // namespace nanoleaf_canvas
}  // namespace esphome
