#include "nanoleaf_canvas.h"
#include "canvas_web_page.h"

#include "esphome/core/log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

#include <cstring>
#include <cstdio>
#include <sstream>

namespace esphome {
namespace nanoleaf_canvas {

static const char *const TAG = "nanoleaf_canvas";

#define NANOLEAF_UART_NUM UART_NUM_1
#define BAUD_RATE 1000000

// Helper to convert HSV to RGB
static void hsv2rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (s == 0) {
    r = g = b = v;
    return;
  }
  uint8_t region = h / 60;
  uint8_t remainder = (h - (region * 60)) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
  switch (region) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

class NanoleafWebHandler : public AsyncWebHandler {
 public:
  explicit NanoleafWebHandler(NanoleafCanvas *parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    std::string url = std::string(request->url_to(url_buf));
    return (url == "/" || url == "/canvas" || url == "/api/tiles" ||
            url == "/api/set" || url == "/api/all" || url == "/api/effect" ||
            url == "/api/debug" || url == "/api/re-enumerate" || url == "/api/reboot");
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    std::string url = std::string(request->url_to(url_buf));

    if (url == "/" || url == "/canvas") {
      request->send(200, "text/html", CANVAS_HTML);
      return;
    }

    if (url == "/api/debug") {
      std::string debug = this->parent_->get_debug_json();
      request->send(200, "application/json", debug.c_str());
      return;
    }

    if (url == "/api/re-enumerate") {
      bool ok = this->parent_->enumerate_cluster();
      request->send(200, "application/json", ok ? "{\"status\":\"ok\"}" : "{\"status\":\"failed\"}");
      return;
    }

    if (url == "/api/reboot") {
      request->send(200, "application/json", "{\"status\":\"rebooting\"}");
      delay(100);
      esp_restart();
      return;
    }

    if (url == "/api/tiles") {
      std::string json = this->parent_->get_tiles_json();
      request->send(200, "application/json", json.c_str());
      return;
    }

    if (url == "/api/set") {
      if (request->hasParam("id") && request->hasParam("r") && request->hasParam("g") && request->hasParam("b")) {
        int id = std::stoi(request->getParam("id")->value());
        int r = std::stoi(request->getParam("r")->value());
        int g = std::stoi(request->getParam("g")->value());
        int b = std::stoi(request->getParam("b")->value());
        this->parent_->set_wave_animation(false);
        this->parent_->set_node_color((uint16_t)id, (uint8_t)r, (uint8_t)g, (uint8_t)b, 0, 1);
        this->parent_->sync_to_light_state(true, this->parent_->get_brightness() > 0 ? this->parent_->get_brightness() : 255, r, g, b);
        request->send(200, "application/json", "{\"status\":\"ok\"}");
      } else {
        request->send(400, "application/json", "{\"error\":\"missing parameters\"}");
      }
      return;
    }

    if (url == "/api/all") {
      if (request->hasParam("bri")) {
        int bri = std::stoi(request->getParam("bri")->value());
        this->parent_->set_global_brightness((uint8_t)bri);
      }
      if (request->hasParam("r") && request->hasParam("g") && request->hasParam("b")) {
        int r = std::stoi(request->getParam("r")->value());
        int g = std::stoi(request->getParam("g")->value());
        int b = std::stoi(request->getParam("b")->value());
        this->parent_->set_wave_animation(false);
        this->parent_->set_global_color((uint8_t)r, (uint8_t)g, (uint8_t)b, 0, 2);
      }
      this->parent_->sync_to_light_state(this->parent_->get_brightness() > 0,
                                         this->parent_->get_brightness(),
                                         this->parent_->get_cur_r(),
                                         this->parent_->get_cur_g(),
                                         this->parent_->get_cur_b());
      request->send(200, "application/json", "{\"status\":\"ok\"}");
      return;
    }

    if (url == "/api/effect") {
      if (request->hasParam("name")) {
        std::string name = request->getParam("name")->value();
        if (name == "wave") {
          this->parent_->set_wave_animation(true);
          this->parent_->sync_to_light_state(true, this->parent_->get_brightness() > 0 ? this->parent_->get_brightness() : 255, 255, 255, 255);
        } else if (name == "off") {
          this->parent_->set_wave_animation(false);
          this->parent_->set_global_brightness(0);
          this->parent_->sync_to_light_state(false, 0, 0, 0, 0);
        } else if (name == "static_rainbow") {
          this->parent_->set_wave_animation(false);
          const uint8_t colors[12][3] = {
            {255, 0, 0}, {255, 60, 0}, {255, 180, 0}, {200, 255, 0},
            {0, 255, 0}, {0, 255, 120}, {0, 255, 255}, {0, 120, 255},
            {0, 0, 255}, {130, 0, 255}, {255, 0, 255}, {255, 0, 80}
          };
          for (size_t i = 0; i < this->parent_->get_tiles().size(); i++) {
            this->parent_->set_node_color(i, colors[i % 12][0], colors[i % 12][1], colors[i % 12][2], 0, 2);
          }
          this->parent_->sync_to_light_state(true, this->parent_->get_brightness() > 0 ? this->parent_->get_brightness() : 255, 255, 200, 100);
        }
        request->send(200, "application/json", "{\"status\":\"ok\"}");
      } else {
        request->send(400, "application/json", "{\"error\":\"missing effect name\"}");
      }
      return;
    }

    request->send(404, "text/plain", "Not Found");
  }

 protected:
  NanoleafCanvas *parent_;
};

NanoleafCanvas::NanoleafCanvas() {}

void NanoleafCanvas::configure_bus() {
  if (uart_is_driver_installed(NANOLEAF_UART_NUM)) {
    uart_driver_delete(NANOLEAF_UART_NUM);
  }

  uart_config_t uart_config = {
    .baud_rate = BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
    .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(NANOLEAF_UART_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_driver_install(NANOLEAF_UART_NUM, 2048, 1024, 0, NULL, 0));

  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << this->data_pin_),
    .mode = GPIO_MODE_INPUT_OUTPUT_OD,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);

  esp_rom_gpio_connect_out_signal((gpio_num_t)this->data_pin_, U1TXD_OUT_IDX, false, false);
  esp_rom_gpio_connect_in_signal((gpio_num_t)this->data_pin_, U1RXD_IN_IDX, false);

  ESP_LOGI(TAG, "Hardware UART1 running at 1 Mbaud on GPIO%d (Open-Drain)", this->data_pin_);
}

int NanoleafCanvas::xact(const uint8_t *frame, size_t len, uint8_t *reply_buf, size_t max_reply, int expect, uint32_t timeout_ms) {
  uart_flush_input(NANOLEAF_UART_NUM);
  uart_write_bytes(NANOLEAF_UART_NUM, (const char *)frame, len);
  uart_wait_tx_done(NANOLEAF_UART_NUM, pdMS_TO_TICKS(50));
  if (expect == 0) {
    return 0;
  }

  uint32_t start = millis();
  uint8_t rx_raw[1024];
  size_t total_rx = 0;
  uint32_t last_rx_micros = micros();

  while (millis() - start < timeout_ms) {
    size_t available = 0;
    uart_get_buffered_data_len(NANOLEAF_UART_NUM, &available);
    if (available > 0) {
      int r = uart_read_bytes(NANOLEAF_UART_NUM, rx_raw + total_rx,
                              sizeof(rx_raw) - total_rx, 0);
      if (r > 0) {
        total_rx += r;
        last_rx_micros = micros();
        if (expect == -1 && total_rx > len) {
          bool found_term = false;
          for (size_t j = len; j < total_rx; j++) {
            if (rx_raw[j] == 0x40) {
              found_term = true;
              break;
            }
          }
          if (found_term) break;
        }
      }
    }
    if (expect > 0 && (int)total_rx >= (int)len + expect && (micros() - last_rx_micros > 1500)) {
      break;
    }
    esp_rom_delay_us(100);
  }

  size_t reply_len = 0;
  if (total_rx > len) {
    reply_len = total_rx - len;
    if (reply_len > max_reply) reply_len = max_reply;
    if (reply_buf != nullptr) {
      memcpy(reply_buf, rx_raw + len, reply_len);
    }
  }
  return reply_len;
}


void NanoleafCanvas::parse_layout_tree(const uint8_t *layout, size_t len) {
  std::vector<CanvasTile> prev_tiles = this->tiles_;
  this->tiles_.clear();

  size_t idx = 0;
  uint16_t pid = 0;
  const int DIR[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

  auto parse_node = [&](auto &self, int parent_id, int p_edge) -> void {
    if (idx >= len || layout[idx] == 0x40) return;
    uint8_t b = layout[idx++];
    uint8_t code = (b >> 4) & 7;
    uint8_t root = b & 7;
    if (code == 1) { // PSU node
      return;
    }

    uint16_t my_id = pid++;
    int16_t x = 0, y = 0;
    uint8_t rot = 0;

    if (parent_id < 0) {
      x = 0; y = 0; rot = 0;
    } else {
      auto &parent = this->tiles_[parent_id];
      int abs_p_edge = (parent.rot + p_edge) % 4;
      rot = (abs_p_edge + 2 - root + 4) % 4;
      x = parent.x + DIR[abs_p_edge][0];
      y = parent.y + DIR[abs_p_edge][1];
    }

    // Preserve existing color if previously known, else default to current global color
    uint8_t r = this->cur_r_;
    uint8_t g = this->cur_g_;
    uint8_t b_col = this->cur_b_;
    uint8_t w = this->cur_w_;
    if (my_id < prev_tiles.size()) {
      r = prev_tiles[my_id].r;
      g = prev_tiles[my_id].g;
      b_col = prev_tiles[my_id].b;
      w = prev_tiles[my_id].w;
    }

    CanvasTile tile = {
      .id = my_id,
      .x = x,
      .y = y,
      .rot = rot,
      .r = r,
      .g = g,
      .b = b_col,
      .w = w,
      .status = 0
    };
    this->tiles_.push_back(tile);

    uint8_t curr_e = (root + 1) % 4;
    while (idx < len && layout[idx] != 0x40) {
      uint8_t next_val = layout[idx];
      if (next_val & 0x80) {
        self(self, (int)my_id, (int)curr_e);
      } else {
        uint8_t next_e = (curr_e + 1) % 4;
        if (next_e == root) {
          break;
        } else {
          idx++;
          curr_e = next_e;
        }
      }
    }
  };

  parse_node(parse_node, -1, -1);
  this->n_panels_ = this->tiles_.size();
  ESP_LOGI(TAG, "Parsed 2D layout: %d Canvas panels", this->n_panels_);
  for (const auto &t : this->tiles_) {
    ESP_LOGI(TAG, "  Tile #%d: X=%d, Y=%d, rot=%d", t.id, t.x, t.y, t.rot);
  }
}

bool NanoleafCanvas::enumerate_cluster() {
  ESP_LOGI(TAG, "Starting cluster enumeration...");
  uint8_t best_reply[256];
  int best_rlen = 0;
  int confirmed_count = 0;

  // Run up to 6 iterations of (0x00 root_detect + 0x80 layout_detect).
  // Section 8: "A second 00 + 80 clears it [the hotplug CC marker]."
  // Section 9.2: "Read the string up to six times and use one that repeats."
  for (int attempt = 0; attempt < 6; attempt++) {
    const uint8_t root_cmd = 0x00;
    xact(&root_cmd, 1, nullptr, 0, 0, 30);
    delay(25);

    uint8_t reply[256];
    const uint8_t layout_cmd = 0x80;
    int rlen = xact(&layout_cmd, 1, reply, sizeof(reply), -1, 600);

    if (rlen > 0 && reply[rlen - 1] == 0x40) {
      if (best_rlen > 0 && rlen == best_rlen && memcmp(reply, best_reply, rlen) == 0) {
        confirmed_count++;
        if (confirmed_count >= 1 && attempt >= 1) {
          // Confirmed identical layout and at least 2 cycles run to clear CC
          break;
        }
      } else if (rlen >= best_rlen) {
        memcpy(best_reply, reply, rlen);
        best_rlen = rlen;
        confirmed_count = 0;
      }
    }
    delay(25);
  }

  if (best_rlen <= 0) {
    ESP_LOGW(TAG, "Layout detect 0x80 failed: No valid reply");
    return false;
  }

  this->last_layout_rlen_ = best_rlen;
  memcpy(this->last_layout_reply_, best_reply, (best_rlen < 64) ? best_rlen : 64);

  parse_layout_tree(best_reply, best_rlen);
  this->enumerated_ = true;

  // Set brightness
  set_global_brightness(this->cur_brightness_);
  delay(15);

  // Enable touch reporting
  const uint8_t touch_cmd[] = {0xFC, 0x07, 0x01};
  xact(touch_cmd, sizeof(touch_cmd), nullptr, 0, 0, 30);
  delay(15);

  // Restore active color without forcing teal
  if (this->has_color_) {
    set_global_color(this->cur_r_, this->cur_g_, this->cur_b_, this->cur_w_, 1);
  }

  return true;
}

void NanoleafCanvas::sync_to_light_state(bool is_on, uint8_t bri, uint8_t r, uint8_t g, uint8_t b) {
  if (this->light_state_ == nullptr) return;
  this->light_state_->current_values.set_state(is_on);
  if (is_on) {
    this->light_state_->current_values.set_brightness((float)bri / 255.0f);
    this->light_state_->current_values.set_color_mode(light::ColorMode::RGB);
    this->light_state_->current_values.set_red((float)r / 255.0f);
    this->light_state_->current_values.set_green((float)g / 255.0f);
    this->light_state_->current_values.set_blue((float)b / 255.0f);
  }
  this->light_state_->remote_values = this->light_state_->current_values;
  this->light_state_->publish_state();
}

void NanoleafCanvas::set_global_brightness(uint8_t bri) {
  this->cur_brightness_ = bri;
  this->tx_count_++;
  const uint8_t bri_cmd[] = {0xFC, 0x04, this->cur_brightness_};
  xact(bri_cmd, sizeof(bri_cmd), NULL, 0, 0, 30);
}

void NanoleafCanvas::set_bulk_push_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t trans) {
  if (this->n_panels_ == 0) return;
  this->tx_count_++;
  // E0 03 followed by n_panels chunks of [05 trans R G B W]
  uint8_t frame[2 + 256 * 6];
  frame[0] = 0xE0;
  frame[1] = 0x03;
  size_t idx = 2;
  for (size_t i = 0; i < this->n_panels_; i++) {
    frame[idx++] = 0x05;
    frame[idx++] = trans;
    frame[idx++] = r;
    frame[idx++] = g;
    frame[idx++] = b;
    frame[idx++] = w;
  }
  xact(frame, idx, NULL, 0, 0, 50);
}

void NanoleafCanvas::send_bulk_push_tiles(uint8_t trans) {
  if (this->tiles_.empty()) return;
  this->tx_count_++;
  uint8_t frame[2 + 256 * 6];
  frame[0] = 0xE0;
  frame[1] = 0x03;
  size_t idx = 2;
  // PROTOCOL.md §5.3: chunk order is reverse of layout string
  for (int p = (int)this->tiles_.size() - 1; p >= 0; p--) {
    const auto &t = this->tiles_[p];
    frame[idx++] = 0x05;
    frame[idx++] = trans;
    frame[idx++] = t.r;
    frame[idx++] = t.g;
    frame[idx++] = t.b;
    frame[idx++] = t.w;
  }
  xact(frame, idx, NULL, 0, 0, 50);
}

void NanoleafCanvas::set_global_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t trans) {
  this->cur_r_ = r;
  this->cur_g_ = g;
  this->cur_b_ = b;
  this->cur_w_ = w;
  this->has_color_ = true;
  this->tx_count_++;

  // Send FC 06 global color
  const uint8_t frame[] = {0xFC, 0x06, r, g, b, w, trans};
  xact(frame, sizeof(frame), NULL, 0, 0, 30);

  // Also send E0 03 bulk push
  set_bulk_push_color(r, g, b, w, trans);

  for (auto &t : this->tiles_) {
    t.r = r; t.g = g; t.b = b; t.w = w;
  }
}

void NanoleafCanvas::set_node_color(uint16_t id, uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t trans) {
  this->tx_count_++;
  uint8_t frame[] = {0xF8, (uint8_t)(id & 0xFF), (uint8_t)(id >> 8), 0x06, r, g, b, w, trans};
  xact(frame, sizeof(frame), NULL, 0, 0, 30);
  if (id < this->tiles_.size()) {
    this->tiles_[id].r = r;
    this->tiles_[id].g = g;
    this->tiles_[id].b = b;
    this->tiles_[id].w = w;
  }
  send_bulk_push_tiles(trans);
}

void NanoleafCanvas::poll_bulk() {
  this->poll_count_++;
  const uint8_t cmd = 0xC0;
  uint8_t reply[256];
  // Expect 2 bytes per panel (2 * n_panels), allow up to 250ms window
  int rlen = xact(&cmd, 1, reply, sizeof(reply), this->n_panels_ * 2, 250);
  this->last_poll_rlen_ = rlen;

  if (rlen > 0) {
    this->poll_success_++;
    memcpy(this->last_poll_reply_, reply, (rlen < 64) ? rlen : 64);

    // Process touch status on panel pairs
    for (size_t p = 0; p < this->n_panels_ && (p * 2 + 1) < (size_t)rlen; p++) {
      uint8_t status = reply[p * 2];
      if ((status & 0x10) && (status & 0x0F)) {
        ESP_LOGD(TAG, "Panel %d touched! Gesture=0x%02X", p, status);
        if (p < this->tiles_.size()) {
          this->tiles_[p].status = status;
        }
      }
    }

    // Check for trailing or lone 0xCC (hot-swap / topology change)
    bool has_cc = false;
    for (int i = 0; i < rlen; i++) {
      if (reply[i] == 0xCC) {
        has_cc = true;
        break;
      }
    }
    if (has_cc) {
      uint32_t now = millis();
      if (now - this->last_re_enum_ms_ >= 5000) {
        this->last_re_enum_ms_ = now;
        ESP_LOGI(TAG, "Hot-plug detected (0xCC in reply)! Re-enumerating...");
        enumerate_cluster();
      }
    }
  }
}

bool NanoleafCanvas::test_bus_echo() {
  const uint8_t pattern[] = {0x55, 0xAA, 0x55, 0xAA, 0x0F, 0xF0};
  int pass = 0;
  for (int t = 0; t < 5; t++) {
    uart_flush_input(NANOLEAF_UART_NUM);
    uart_write_bytes(NANOLEAF_UART_NUM, (const char *)pattern, sizeof(pattern));
    uart_wait_tx_done(NANOLEAF_UART_NUM, pdMS_TO_TICKS(20));

    uint8_t rx[16] = {0};
    int r = uart_read_bytes(NANOLEAF_UART_NUM, rx, sizeof(pattern), pdMS_TO_TICKS(30));
    if (r == sizeof(pattern) && memcmp(rx, pattern, sizeof(pattern)) == 0) {
      pass++;
    }
    delay(5);
  }
  return pass >= 4;
}

std::string NanoleafCanvas::get_debug_json() {
  bool echo_ok = test_bus_echo();
  std::stringstream ss;
  ss << "{"
     << "\"bus_echo\":" << (echo_ok ? "true" : "false")
     << ",\"enumerated\":" << (this->enumerated_ ? "true" : "false")
     << ",\"n_panels\":" << (int)this->n_panels_
     << ",\"poll_count\":" << this->poll_count_
     << ",\"poll_success\":" << this->poll_success_
     << ",\"last_poll_rlen\":" << this->last_poll_rlen_
     << ",\"tx_count\":" << this->tx_count_
     << ",\"anim_wave\":" << (this->anim_wave_ ? "true" : "false")
     << ",\"cur_brightness\":" << (int)this->cur_brightness_
     << ",\"cur_r\":" << (int)this->cur_r_
     << ",\"cur_g\":" << (int)this->cur_g_
     << ",\"cur_b\":" << (int)this->cur_b_
     << ",\"last_layout_rlen\":" << this->last_layout_rlen_
     << ",\"last_layout_hex\":\"";
  for (int i = 0; i < this->last_layout_rlen_ && i < 32; i++) {
    char h[4];
    snprintf(h, sizeof(h), "%02X", this->last_layout_reply_[i]);
    ss << h;
  }
  ss << "\",\"last_poll_hex\":\"";
  for (int i = 0; i < this->last_poll_rlen_ && i < 32; i++) {
    char h[4];
    snprintf(h, sizeof(h), "%02X", this->last_poll_reply_[i]);
    ss << h;
  }
  ss << "\"}";
  return ss.str();
}


std::string NanoleafCanvas::get_tiles_json() {
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < this->tiles_.size(); i++) {
    const auto &t = this->tiles_[i];
    if (i > 0) ss << ",";
    ss << "{\"id\":" << t.id
       << ",\"x\":" << t.x
       << ",\"y\":" << t.y
       << ",\"rot\":" << (int)t.rot
       << ",\"r\":" << (int)t.r
       << ",\"g\":" << (int)t.g
       << ",\"b\":" << (int)t.b
       << "}";
  }
  ss << "]";
  return ss.str();
}

void NanoleafCanvas::setup() {
  ESP_LOGI(TAG, "Initializing Nanoleaf Canvas Controller...");
  configure_bus();
  delay(100);

  enumerate_cluster();

  if (web_server_base::global_web_server_base != nullptr) {
    ESP_LOGI(TAG, "Attaching Canvas Web Server handler on port %d...", web_server_base::global_web_server_base->get_port());
    web_server_base::global_web_server_base->add_handler(new NanoleafWebHandler(this));
  } else {
    ESP_LOGW(TAG, "web_server_base is null; interactive web UI handler not attached.");
  }
}

void NanoleafCanvas::loop() {
  uint32_t now = millis();

  // Wave animation tick
  if (this->anim_wave_ && (now - this->last_anim_ms_ >= 80)) {
    this->last_anim_ms_ = now;
    this->anim_hue_ = (this->anim_hue_ + 5) % 360;
    for (size_t i = 0; i < this->tiles_.size(); i++) {
      uint16_t panel_hue = (this->anim_hue_ + (i * 360 / this->n_panels_)) % 360;
      uint8_t r, g, b;
      hsv2rgb(panel_hue, 255, 255, r, g, b);
      set_node_color(i, r, g, b, 0, 1);
    }
  }

  // 50ms periodic bulk poll
  if (this->enumerated_ && (now - this->last_poll_ms_ >= 50)) {
    poll_bulk();
    this->last_poll_ms_ = millis();
  }
}

void NanoleafCanvas::dump_config() {
  ESP_LOGCONFIG(TAG, "Nanoleaf Canvas:");
  ESP_LOGCONFIG(TAG, "  Data Pin: GPIO%d", this->data_pin_);
  ESP_LOGCONFIG(TAG, "  Total Panels: %d", this->n_panels_);
  ESP_LOGCONFIG(TAG, "  Enumerated: %s", YESNO(this->enumerated_));
}

}  // namespace nanoleaf_canvas
}  // namespace esphome
