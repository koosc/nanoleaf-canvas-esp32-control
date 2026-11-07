#include "nanoleaf_canvas.h"
#include "canvas_web_page.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include "hal/gpio_ll.h"
#include "hal/uart_ll.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace esphome {
namespace nanoleaf_canvas {

static const char *const TAG = "nanoleaf_canvas";

#define NANOLEAF_UART_NUM UART_NUM_1
#define BAUD_RATE 1000000

static constexpr size_t MAX_TILES = 64;
static constexpr uint32_t POLL_INTERVAL_MS = 50;
// Minimum spacing between color/brightness frames; bursts in between coalesce.
static constexpr uint32_t FLUSH_INTERVAL_MS = 20;
// The bus has no acknowledgement for SET frames, so a frame lost to noise or a
// collision leaves a panel stale. Re-send the full state once things are idle.
static constexpr uint32_t REFRESH_IDLE_MS = 3000;
static constexpr uint32_t REFRESH_INTERVAL_MS = 5000;

static void hsv2rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
  h %= 360;
  uint8_t region = h / 60;
  uint16_t f = (uint16_t) (h % 60) * 255 / 60;
  uint8_t p = (v * (255 - s)) / 255;
  uint8_t q = (v * (255 - (s * f) / 255)) / 255;
  uint8_t t = (v * (255 - (s * (255 - f)) / 255)) / 255;
  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

static void kelvin_to_rgbw(float kelvin, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &w) {
  // Calibrated against reverse-engineered stock Nanoleaf Canvas (NL29) controller
  static const struct {
    float k;
    uint8_t r, g, b, w;
  } CALIB[] = {
    {1200.0f, 255, 110, 0,   0},
    {1500.0f, 255, 122, 0,   0},
    {2000.0f, 255, 147, 0,  16},
    {2500.0f, 255, 142, 0,  89},
    {2700.0f, 255, 137, 0, 113},
    {3000.0f, 255, 137, 0, 143},
    {3500.0f, 255, 132, 0, 183},
    {4000.0f, 255, 127, 0, 216},
    {4500.0f, 255, 127, 0, 243},
    {5000.0f, 221, 119, 0, 255},
    {5500.0f, 143,  74, 0, 255},
    {6000.0f,  78,  47, 0, 255},
    {6500.0f,  23,  19, 0, 255},
  };
  constexpr size_t N = sizeof(CALIB) / sizeof(CALIB[0]);
  if (kelvin <= CALIB[0].k) {
    r = CALIB[0].r; g = CALIB[0].g; b = CALIB[0].b; w = CALIB[0].w;
    return;
  }
  if (kelvin >= CALIB[N - 1].k) {
    r = CALIB[N - 1].r; g = CALIB[N - 1].g; b = CALIB[N - 1].b; w = CALIB[N - 1].w;
    return;
  }
  for (size_t i = 0; i < N - 1; i++) {
    if (kelvin >= CALIB[i].k && kelvin <= CALIB[i + 1].k) {
      float t = (kelvin - CALIB[i].k) / (CALIB[i + 1].k - CALIB[i].k);
      r = (uint8_t) roundf(CALIB[i].r + t * (CALIB[i + 1].r - CALIB[i].r));
      g = (uint8_t) roundf(CALIB[i].g + t * (CALIB[i + 1].g - CALIB[i].g));
      b = (uint8_t) roundf(CALIB[i].b + t * (CALIB[i + 1].b - CALIB[i].b));
      w = (uint8_t) roundf(CALIB[i].w + t * (CALIB[i + 1].w - CALIB[i].w));
      return;
    }
  }
}

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

// Parses an integer query parameter; returns false if missing or malformed.
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
// Web handler. Runs in the HTTP server task, so it never touches the bus or
// the light state directly; everything is marshalled onto the main loop.
// ---------------------------------------------------------------------------
class NanoleafWebHandler : public AsyncWebHandler {
 public:
  explicit NanoleafWebHandler(NanoleafCanvas *parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest *request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    std::string url = std::string(request->url_to(url_buf));
    return (url == "/" || url == "/canvas" || url.rfind("/api/", 0) == 0);
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    std::string url = std::string(request->url_to(url_buf));

    if (url == "/" || url == "/canvas") {
      request->send(200, "text/html", CANVAS_HTML);
      return;
    }

    std::function<std::string()> job;
    int r = 0, g = 0, b = 0, w = 0, bri = 0, id = 0;
    bool has_w = get_int_param(request, "w", 0, 255, w);
    bool has_rgb = get_int_param(request, "r", 0, 255, r) && get_int_param(request, "g", 0, 255, g) &&
                   get_int_param(request, "b", 0, 255, b);
    NanoleafCanvas *c = this->parent_;

    if (url == "/api/reboot") {
      job = [c]() {
        c->request_reboot();
        return std::string("{\"status\":\"rebooting\"}");
      };
    } else if (url == "/api/tiles") {
      job = [c]() { return c->get_tiles_json(); };
    } else if (url == "/api/debug") {
      job = [c]() { return c->get_debug_json(); };
    } else if (url == "/api/re-enumerate") {
      job = [c]() { return std::string(c->enumerate_cluster() ? "{\"status\":\"ok\"}" : "{\"status\":\"failed\"}"); };
    } else if (url == "/api/set") {
      if ((!has_rgb && !has_w) || !get_int_param(request, "id", 0, MAX_TILES - 1, id)) {
        request->send(400, "application/json", "{\"error\":\"missing or invalid parameters\"}");
        return;
      }
      job = [c, id, r, g, b, w]() {
        if ((size_t) id >= c->get_panel_count())
          return std::string("{\"error\":\"no such tile\"}");
        auto *ls = c->get_light_state();
        if (ls != nullptr) {
          auto call = ls->make_call();
          bool need = false;
          if (!ls->remote_values.is_on()) {
            call.set_state(true);
            need = true;
          }
          if (ls->get_current_effect_index() != 0) {
            call.set_effect((uint32_t) 0);
            need = true;
          }
          if (need)
            call.perform();
          // Keep the per-tile colors instead of repainting the solid color.
          c->consume_color_repaint();
        }
        c->set_tile_color(id, r, g, b, w);
        return std::string("{\"status\":\"ok\"}");
      };
    } else if (url == "/api/all") {
      int k = 0;
      bool has_k = get_int_param(request, "k", 1200, 6500, k) || get_int_param(request, "kelvin", 1200, 6500, k);
      bool has_bri = get_int_param(request, "bri", 0, 255, bri);
      if (!has_rgb && !has_w && !has_bri && !has_k) {
        request->send(400, "application/json", "{\"error\":\"missing or invalid parameters\"}");
        return;
      }
      job = [c, has_rgb, has_w, has_bri, has_k, r, g, b, w, bri, k]() {
        auto *ls = c->get_light_state();
        if (ls == nullptr) {
          if (has_k) {
            uint8_t kr, kg, kb, kw;
            kelvin_to_rgbw((float) k, kr, kg, kb, kw);
            c->set_all_colors(kr, kg, kb, kw);
          } else if (has_rgb || has_w) {
            c->set_all_colors(r, g, b, w);
          }
          if (has_bri)
            c->set_brightness(bri);
          return std::string("{\"status\":\"ok\"}");
        }
        auto call = ls->make_call();
        if (has_k) {
          call.set_effect((uint32_t) 0);
          call.set_color_mode(light::ColorMode::COLOR_TEMPERATURE);
          call.set_color_temperature(1000000.0f / k);
          call.set_state(true);
        } else if ((has_rgb || has_w) && r == 0 && g == 0 && b == 0 && w == 0) {
          call.set_state(false);
        } else if (has_rgb || has_w) {
          call.set_effect((uint32_t) 0);
          call.set_color_mode(light::ColorMode::RGB);
          call.set_rgb(r / 255.0f, g / 255.0f, b / 255.0f);
          call.set_state(true);
          // An identical color would otherwise not repaint tiles set individually.
          c->request_color_repaint();
        }
        if (has_bri) {
          if (bri == 0) {
            call.set_state(false);
          } else {
            call.set_state(true);
            call.set_brightness(bri / 255.0f);
          }
        }
        call.set_transition_length(0);
        call.perform();
        return std::string("{\"status\":\"ok\"}");
      };
    } else if (url == "/api/effect") {
      auto *p = request->getParam("name");
      if (p == nullptr) {
        request->send(400, "application/json", "{\"error\":\"missing effect name\"}");
        return;
      }
      std::string name = p->value();
      job = [c, name]() {
        auto *ls = c->get_light_state();
        if (ls == nullptr)
          return std::string("{\"error\":\"no light\"}");
        auto call = ls->make_call();
        if (name == "off") {
          call.set_state(false);
        } else if (name == "none") {
          call.set_effect((uint32_t) 0);
        } else if (name == "wave") {
          call.set_state(true).set_effect("Rainbow Wave");
        } else if (name == "static_rainbow") {
          call.set_state(true).set_effect("Static Rainbow");
        } else {
          return std::string("{\"error\":\"unknown effect\"}");
        }
        call.perform();
        return std::string("{\"status\":\"ok\"}");
      };
    } else if (url == "/api/raw") {
      // Debug: send an arbitrary frame and return the reply.
      // /api/raw?hex=FC0480&expect=0&timeout=100
      auto *p = request->getParam("hex");
      int expect = 0, timeout = 100;
      get_int_param(request, "expect", -1, 1024, expect);
      get_int_param(request, "timeout", 1, 2000, timeout);
      // Frames separated by '-' are sent one after another; the reply of the last is returned.
      std::vector<std::vector<uint8_t>> frames(1);
      if (p != nullptr) {
        const std::string &hex = p->value();
        for (size_t i = 0; i < hex.size();) {
          if (hex[i] == '-') {
            frames.emplace_back();
            i++;
            continue;
          }
          if (i + 1 >= hex.size())
            break;
          frames.back().push_back((uint8_t) strtol(hex.substr(i, 2).c_str(), nullptr, 16));
          i += 2;
        }
      }
      for (auto &f : frames) {
        if (f.empty() || f.size() > 512) {
          request->send(400, "application/json", "{\"error\":\"bad hex\"}");
          return;
        }
      }
      job = [c, frames, expect, timeout]() {
        for (size_t i = 0; i + 1 < frames.size(); i++)
          c->raw_exchange(frames[i], 0, 10);
        return c->raw_exchange(frames.back(), expect, timeout);
      };
    } else if (url == "/api/buscfg") {
      int pp = -1, gap = -1, np = -1, ae = -1;
      get_int_param(request, "autoenum", 0, 1, ae);
      get_int_param(request, "pp", 0, 1, pp);
      get_int_param(request, "gap", 0, 100000, gap);
      get_int_param(request, "nopoll", 0, 1, np);
      job = [c, pp, gap, np, ae]() { return c->apply_bus_config(pp, gap, np, ae); };
    } else {
      request->send(404, "text/plain", "Not Found");
      return;
    }

    std::string result;
    if (!this->parent_->run_on_loop(std::move(job), result, 5000)) {
      request->send(503, "application/json", "{\"error\":\"timeout\"}");
      return;
    }
    request->send(200, "application/json", result.c_str());
  }

 protected:
  NanoleafCanvas *parent_;
};

bool NanoleafCanvas::run_on_loop(std::function<std::string()> fn, std::string &out, uint32_t timeout_ms) {
  LoopJob job;
  job.fn = std::move(fn);
  uint32_t start = millis();
  // Claim the single job slot.
  while (true) {
    {
      LockGuard lock(this->job_mutex_);
      if (this->pending_job_ == nullptr) {
        this->pending_job_ = &job;
        break;
      }
    }
    if (millis() - start > timeout_ms)
      return false;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  App.wake_loop_threadsafe();
  while (!job.done.load()) {
    if (millis() - start > timeout_ms) {
      LockGuard lock(this->job_mutex_);
      if (this->pending_job_ == &job) {
        // Not picked up yet; withdraw it.
        this->pending_job_ = nullptr;
        return false;
      }
      // The loop holds the mutex while it runs a job, so once we get the lock
      // here the job has completed.
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  out = std::move(job.result);
  return true;
}

// ---------------------------------------------------------------------------
// Bus
// ---------------------------------------------------------------------------
void NanoleafCanvas::configure_bus() {
  if (uart_is_driver_installed(NANOLEAF_UART_NUM)) {
    uart_driver_delete(NANOLEAF_UART_NUM);
  }

  uart_config_t uart_config = {
      .baud_rate = BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 122,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(NANOLEAF_UART_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_driver_install(NANOLEAF_UART_NUM, 2048, 0, 0, NULL, 0));

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << this->data_pin_),
      .mode = GPIO_MODE_INPUT_OUTPUT_OD,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);

  // Only matters while we actively drive the line (push_pull_); limits the
  // current if a panel ever talks over us.
  gpio_set_drive_capability((gpio_num_t) this->data_pin_, GPIO_DRIVE_CAP_1);

  esp_rom_gpio_connect_out_signal((gpio_num_t) this->data_pin_, U1TXD_OUT_IDX, false, false);
  esp_rom_gpio_connect_in_signal((gpio_num_t) this->data_pin_, U1RXD_IN_IDX, false);

  ESP_LOGI(TAG, "UART1 at 1 Mbaud on GPIO%d (open-drain, %s TX)", this->data_pin_,
           this->push_pull_ ? "push-pull" : "open-drain");
}

// Sends a frame and collects the reply that follows our own echo.
//  expect > 0 : stop once `expect` reply bytes arrived and the line went quiet
//  expect = -1: stop once a 0x40 terminator arrived and the line went quiet
//  any reply  : stop after `gap_ms` of silence following the last reply byte
//  expect = 0 : no reply; only wait for the echo
// Blocking reads yield to other tasks instead of busy-waiting.
int NanoleafCanvas::xact(const uint8_t *frame, size_t len, uint8_t *reply, size_t max_reply, int expect,
                         uint32_t timeout_ms, uint32_t gap_ms, bool *echo_ok) {
  static uint8_t rx[1024 + 600];
  // Panels relay frames hop by hop and drop a frame that follows the previous
  // one without an idle gap, so every frame gets its own write.
  if (this->gap_us_ > 0) {
    uint32_t idle = micros() - this->last_bus_us_;
    if (idle < this->gap_us_)
      delayMicroseconds(this->gap_us_ - idle);
  }
  uart_flush_input(NANOLEAF_UART_NUM);
  // With open drain only, the weak pull-ups make rising edges so slow that
  // panels misread roughly one frame in five. Drive the line actively while
  // transmitting (as the stock linker's buffer does). Only for frames without
  // a reply: panels answer a poll almost immediately, before we could release.
  bool drive = this->push_pull_ && expect == 0;
  if (drive) {
    gpio_ll_od_disable(&GPIO, this->data_pin_);
    uart_write_bytes(NANOLEAF_UART_NUM, (const char *) frame, len);
    // Release the line as soon as the stop bit is out.
    uart_dev_t *hw = UART_LL_GET_HW(NANOLEAF_UART_NUM);
    uint32_t spin_start = micros();
    while (!uart_ll_is_tx_idle(hw) && micros() - spin_start < 20000) {
    }
    gpio_ll_od_enable(&GPIO, this->data_pin_);
  } else {
    uart_write_bytes(NANOLEAF_UART_NUM, (const char *) frame, len);
  }
  uart_wait_tx_done(NANOLEAF_UART_NUM, pdMS_TO_TICKS(50));
  this->tx_frames_++;

  size_t want_echo = len < sizeof(rx) ? len : sizeof(rx);
  size_t total = 0;
  uint32_t start = millis();
  uint32_t last_rx = start;
  bool term_seen = false;

  while (total < sizeof(rx)) {
    uint32_t now = millis();
    if (now - start >= timeout_ms)
      break;
    bool have_reply = total > want_echo;
    if (expect == 0 && total >= want_echo)
      break;
    if (have_reply && now - last_rx >= gap_ms)
      break;
    if (expect > 0 && total >= want_echo + (size_t) expect && now - last_rx >= 1)
      break;
    if (term_seen && now - last_rx >= 1)
      break;

    int n = uart_read_bytes(NANOLEAF_UART_NUM, rx + total, sizeof(rx) - total, pdMS_TO_TICKS(1));
    if (n > 0) {
      if (expect == -1) {
        for (int i = 0; i < n; i++) {
          if (total + i >= want_echo && rx[total + i] == 0x40)
            term_seen = true;
        }
      }
      total += n;
      last_rx = millis();
    }
  }

  this->last_bus_us_ = micros();
  bool ok = total >= want_echo && memcmp(rx, frame, want_echo) == 0;
  if (!ok)
    this->echo_errors_++;
  if (echo_ok != nullptr)
    *echo_ok = ok;

  size_t reply_len = 0;
  if (total > want_echo) {
    reply_len = total - want_echo;
    if (reply_len > max_reply)
      reply_len = max_reply;
    if (reply != nullptr)
      memcpy(reply, rx + want_echo, reply_len);
  }
  return reply_len;
}

std::string NanoleafCanvas::apply_bus_config(int push_pull, int gap_us, int no_poll, int auto_enum) {
  if (auto_enum >= 0)
    this->auto_enum_ = auto_enum != 0;

  if (push_pull >= 0)
    this->push_pull_ = push_pull != 0;
  if (gap_us >= 0)
    this->gap_us_ = gap_us;
  if (no_poll >= 0)
    this->no_poll_ = no_poll != 0;
  std::stringstream ss;
  ss << "{\"push_pull\":" << (this->push_pull_ ? 1 : 0)
     << ",\"gap_us\":" << this->gap_us_ << ",\"no_poll\":" << (this->no_poll_ ? 1 : 0) << "}";
  return ss.str();
}

std::string NanoleafCanvas::raw_exchange(const std::vector<uint8_t> &frame, int expect, uint32_t timeout_ms) {
  // Hold off our own color/brightness frames for a while so they do not
  // overwrite what is being tested by hand.
  this->manual_until_ms_ = millis() + 60000;
  uint8_t reply[1024];
  bool echo_ok = false;
  uint32_t t0 = micros();
  // Long gap so we see everything that arrives within the window.
  int n = xact(frame.data(), frame.size(), reply, sizeof(reply), expect, timeout_ms, expect == 0 ? 1 : timeout_ms,
               &echo_ok);
  uint32_t dt = micros() - t0;
  std::stringstream ss;
  ss << "{\"echo_ok\":" << (echo_ok ? "true" : "false") << ",\"len\":" << n << ",\"us\":" << dt << ",\"reply\":\""
     << to_hex(reply, n) << "\"}";
  return ss.str();
}

void NanoleafCanvas::parse_layout_tree(const uint8_t *layout, size_t len) {
  std::vector<CanvasTile> prev_tiles = this->tiles_;
  this->tiles_.clear();

  size_t idx = 0;
  uint16_t pid = 0;
  const int DIR[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

  auto parse_node = [&](auto &self, int parent_id, int p_edge, int depth) -> void {
    if (idx >= len || layout[idx] == 0x40 || depth > (int) MAX_TILES)
      return;
    uint8_t b = layout[idx++];
    uint8_t code = (b >> 4) & 7;
    uint8_t root = b & 3;
    if (code == 1) {  // PSU node
      return;
    }
    if (this->tiles_.size() >= MAX_TILES)
      return;

    uint16_t my_id = pid++;
    int16_t x = 0, y = 0;
    uint8_t rot = 0;

    if (parent_id >= 0) {
      const auto &parent = this->tiles_[parent_id];
      int abs_p_edge = (parent.rot + p_edge) % 4;
      rot = (abs_p_edge + 2 - root + 4) % 4;
      x = parent.x + DIR[abs_p_edge][0];
      y = parent.y + DIR[abs_p_edge][1];
    }

    CanvasTile tile = {.id = my_id,
                       .x = x,
                       .y = y,
                       .rot = rot,
                       .r = this->def_r_,
                       .g = this->def_g_,
                       .b = this->def_b_,
                       .w = this->def_w_,
                       .status = 0,
                       .sent_r = 0,
                       .sent_g = 0,
                       .sent_b = 0,
                       .sent_w = 0};
    // Keep the color of a tile we already knew.
    if (my_id < prev_tiles.size()) {
      tile.r = prev_tiles[my_id].r;
      tile.g = prev_tiles[my_id].g;
      tile.b = prev_tiles[my_id].b;
      tile.w = prev_tiles[my_id].w;
    }
    this->tiles_.push_back(tile);

    uint8_t curr_e = (root + 1) % 4;
    while (idx < len && layout[idx] != 0x40) {
      uint8_t next_val = layout[idx];
      if (next_val & 0x80) {
        size_t before = idx;
        self(self, (int) my_id, (int) curr_e, depth + 1);
        if (idx == before)
          return;  // no progress; malformed string
      } else {
        uint8_t next_e = (curr_e + 1) % 4;
        if (next_e == root)
          break;
        idx++;
        curr_e = next_e;
      }
    }
  };

  parse_node(parse_node, -1, -1, 0);
  ESP_LOGI(TAG, "Parsed layout: %u Canvas panels", (unsigned) this->tiles_.size());
  for (const auto &t : this->tiles_) {
    ESP_LOGD(TAG, "  Tile #%d: X=%d, Y=%d, rot=%d", t.id, t.x, t.y, t.rot);
  }
}

bool NanoleafCanvas::enumerate_cluster() {
  ESP_LOGI(TAG, "Enumerating cluster...");
  this->enum_count_++;
  uint8_t best[256];
  int best_len = 0;
  bool confirmed = false;

  // PROTOCOL.md §8: a second 00 + 80 clears a pending hot-plug marker.
  // §9.2: read the layout several times and accept one that repeats.
  for (int attempt = 0; attempt < 6 && !confirmed; attempt++) {
    const uint8_t root_cmd = 0x00;
    xact(&root_cmd, 1, nullptr, 0, 0, 10);
    delay(30);

    uint8_t reply[256];
    const uint8_t layout_cmd = 0x80;
    int n = xact(&layout_cmd, 1, reply, sizeof(reply), -1, 800, 30);
    if (n > 0 && reply[n - 1] == 0x40) {
      if (n == best_len && memcmp(reply, best, n) == 0 && attempt >= 1) {
        confirmed = true;
      } else {
        memcpy(best, reply, n);
        best_len = n;
      }
    } else {
      ESP_LOGD(TAG, "Layout attempt %d: %d bytes, no terminator", attempt, n);
    }
    delay(20);
  }

  if (best_len <= 0) {
    ESP_LOGW(TAG, "Layout detect failed: no valid reply");
    return false;
  }
  if (!confirmed)
    ESP_LOGW(TAG, "Layout string did not repeat; using last good read");

  this->last_layout_rlen_ = best_len;
  memcpy(this->last_layout_reply_, best, std::min<int>(best_len, sizeof(this->last_layout_reply_)));

  parse_layout_tree(best, best_len);
  this->enumerated_ = !this->tiles_.empty();

  // Touch reporting
  const uint8_t touch_cmd[] = {0xFC, 0x07, 0x01};
  xact(touch_cmd, sizeof(touch_cmd), nullptr, 0, 0, 10);

  // Newly enumerated panels have no state; resend everything.
  this->bri_dirty_ = true;
  this->colors_dirty_ = true;
  this->sent_valid_ = false;
  this->last_poll_ms_ = 0;
  return this->enumerated_;
}

void NanoleafCanvas::set_brightness(uint8_t bri) {
  if (bri != this->brightness_) {
    this->brightness_ = bri;
    this->bri_dirty_ = true;
    this->last_change_ms_ = millis();
  }
}

void NanoleafCanvas::set_all_colors(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  this->def_r_ = r;
  this->def_g_ = g;
  this->def_b_ = b;
  this->def_w_ = w;
  for (auto &t : this->tiles_) {
    if (t.r != r || t.g != g || t.b != b || t.w != w) {
      t.r = r;
      t.g = g;
      t.b = b;
      t.w = w;
      this->colors_dirty_ = true;
    }
  }
  if (this->colors_dirty_)
    this->last_change_ms_ = millis();
}

void NanoleafCanvas::set_tile_color(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  if (index >= this->tiles_.size())
    return;
  auto &t = this->tiles_[index];
  if (t.r != r || t.g != g || t.b != b || t.w != w) {
    t.r = r;
    t.g = g;
    t.b = b;
    t.w = w;
    this->colors_dirty_ = true;
    this->last_change_ms_ = millis();
  }
}

void NanoleafCanvas::send_brightness() {
  const uint8_t frame[] = {0xFC, 0x04, this->brightness_};
  xact(frame, sizeof(frame), nullptr, 0, 0, 10);
}

// Canvas panels take native color through FC 06 (global broadcast) and
// F8 <id> 06 (one panel). The W byte drives the physical dedicated phosphor
// cool-white LEDs.
void NanoleafCanvas::send_colors(bool force) {
  if (this->tiles_.empty())
    return;
  const auto &first = this->tiles_[0];
  bool uniform = true;
  for (const auto &t : this->tiles_) {
    if (t.r != first.r || t.g != first.g || t.b != first.b || t.w != first.w) {
      uniform = false;
      break;
    }
  }
  if (uniform) {
    // FC 06 <R> <G> <B> <W> <Transition>
    const uint8_t frame[] = {0xFC, 0x06, first.r, first.g, first.b, first.w, this->transition_};
    xact(frame, sizeof(frame), nullptr, 0, 0, 10);
    for (auto &t : this->tiles_) {
      t.sent_r = t.r;
      t.sent_g = t.g;
      t.sent_b = t.b;
      t.sent_w = t.w;
    }
  } else {
    for (auto &t : this->tiles_) {
      if (!force && this->sent_valid_ && t.sent_r == t.r && t.sent_g == t.g && t.sent_b == t.b && t.sent_w == t.w)
        continue;
      const uint8_t frame[] = {0xF8, (uint8_t) (t.id & 0xFF), (uint8_t) (t.id >> 8), 0x06, t.r, t.g, t.b, t.w,
                               this->transition_};
      xact(frame, sizeof(frame), nullptr, 0, 0, 10);
      t.sent_r = t.r;
      t.sent_g = t.g;
      t.sent_b = t.b;
      t.sent_w = t.w;
    }
  }
  this->sent_valid_ = true;
}

void NanoleafCanvas::flush_frames(uint32_t now) {
  if (!this->enumerated_ || (int32_t) (this->manual_until_ms_ - now) > 0)
    return;
  if ((this->bri_dirty_ || this->colors_dirty_) && now - this->last_flush_ms_ >= FLUSH_INTERVAL_MS) {
    // Colors first so that turning on does not flash the previous color.
    if (this->colors_dirty_) {
      this->colors_dirty_ = false;
      send_colors(false);
    }
    if (this->bri_dirty_) {
      this->bri_dirty_ = false;
      send_brightness();
    }
    this->last_flush_ms_ = now;
    return;
  }
  if (now - this->last_change_ms_ >= REFRESH_IDLE_MS && now - this->last_refresh_ms_ >= REFRESH_INTERVAL_MS &&
      now - this->last_flush_ms_ >= FLUSH_INTERVAL_MS) {
    this->last_refresh_ms_ = now;
    send_colors(true);
    send_brightness();
    this->last_flush_ms_ = now;
  }
}

void NanoleafCanvas::poll_bulk() {
  this->poll_count_++;
  const uint8_t cmd = 0xC0;
  uint8_t reply[256];
  size_t n_panels = this->tiles_.size();
  uint32_t t0 = micros();
  int n = xact(&cmd, 1, reply, sizeof(reply), n_panels * 2, 40 + 4 * n_panels, 3);
  this->last_poll_us_ = micros() - t0;
  this->last_poll_rlen_ = n;
  memcpy(this->last_poll_reply_, reply, std::min<int>(n, sizeof(this->last_poll_reply_)));
  if (n < (int) n_panels * 2) {
    this->poll_short_++;
    this->last_short_rlen_ = n;
    memcpy(this->last_short_reply_, reply, std::min<int>(n, sizeof(this->last_short_reply_)));
  }
  // Panels that lost their enumeration (power blip, long bus pause) stop
  // answering polls; enumerate again rather than talking into the void.
  if (n == 0) {
    if (++this->polls_silent_ >= 20) {
      this->polls_silent_ = 0;
      ESP_LOGW(TAG, "Panels stopped answering polls; re-enumerating");
      this->silent_events_++;
      this->enumerate_requested_ = true;
    }
  } else {
    this->polls_silent_ = 0;
  }

  for (size_t p = 0; p < n_panels && (p * 2 + 1) < (size_t) n; p++) {
    uint8_t status = reply[p * 2];
    if (status == 0xCC)
      break;
    this->tiles_[p].status = status;
    if ((status & 0x10) && (status & 0x0F)) {
      ESP_LOGD(TAG, "Panel %u touched, gesture 0x%02X", (unsigned) p, status);
    }
  }

  // §8: a trailing CC marks a topology change; re-enumerate.
  bool has_cc = false;
  for (int i = 0; i < n; i++) {
    if (reply[i] == 0xCC) {
      has_cc = true;
      break;
    }
  }
  if (has_cc && !this->in_cc_) {
    this->cc_times_[this->cc_events_ % 16] = millis();
    this->cc_events_++;
  }
  this->in_cc_ = has_cc;
  if (has_cc && this->auto_enum_) {
    uint32_t now = millis();
    // Panels ignore color frames until they are enumerated again, so do not
    // wait long; a fresh 00 + 80 also clears a marker left from our own restart.
    if (now - this->last_re_enum_ms_ >= 250) {
      this->last_re_enum_ms_ = now;
      ESP_LOGD(TAG, "Hot-plug marker in poll reply; re-enumerating");
      this->enumerate_requested_ = true;
    }
  }
}

std::string NanoleafCanvas::get_debug_json() {
  std::stringstream ss;
  ss << "{"
     << "\"enumerated\":" << (this->enumerated_ ? "true" : "false") << ",\"n_panels\":" << this->tiles_.size()
     << ",\"enum_count\":" << this->enum_count_ << ",\"poll_count\":" << this->poll_count_
     << ",\"poll_short\":" << this->poll_short_ << ",\"cc_events\":" << this->cc_events_
     << ",\"silent_events\":" << this->silent_events_ << ",\"uptime_ms\":" << millis() << ",\"cc_times\":\"" << [this]() {
       std::string o;
       for (int i = 0; i < 16; i++) o += std::to_string(this->cc_times_[i]) + " ";
       return o;
     }() << "\"" << ",\"last_short_hex\":\""
     << to_hex(this->last_short_reply_, std::min<int>(this->last_short_rlen_, sizeof(this->last_short_reply_))) << "\""
     << ",\"push_pull\":" << (this->push_pull_ ? "true" : "false") << ",\"echo_errors\":" << this->echo_errors_
     << ",\"tx_frames\":" << this->tx_frames_ << ",\"last_poll_us\":" << this->last_poll_us_
     << ",\"last_poll_rlen\":" << this->last_poll_rlen_ << ",\"brightness\":" << (int) this->brightness_
     << ",\"transition\":" << (int) this->transition_ << ",\"last_layout_hex\":\""
     << to_hex(this->last_layout_reply_, std::min<int>(this->last_layout_rlen_, sizeof(this->last_layout_reply_)))
     << "\",\"last_poll_hex\":\""
     << to_hex(this->last_poll_reply_, std::min<int>(this->last_poll_rlen_, sizeof(this->last_poll_reply_))) << "\"}";
  return ss.str();
}

std::string NanoleafCanvas::get_tiles_json() {
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < this->tiles_.size(); i++) {
    const auto &t = this->tiles_[i];
    if (i > 0)
      ss << ",";
    ss << "{\"id\":" << t.id << ",\"x\":" << t.x << ",\"y\":" << t.y << ",\"rot\":" << (int) t.rot
       << ",\"r\":" << (int) t.r << ",\"g\":" << (int) t.g << ",\"b\":" << (int) t.b
       << ",\"w\":" << (int) t.w << "}";
  }
  ss << "]";
  return ss.str();
}

void NanoleafCanvas::setup() {
  ESP_LOGI(TAG, "Initializing Nanoleaf Canvas controller...");
  configure_bus();
  delay(100);

  enumerate_cluster();

  if (web_server_base::global_web_server_base != nullptr) {
    web_server_base::global_web_server_base->add_handler(new NanoleafWebHandler(this));
  } else {
    ESP_LOGW(TAG, "web_server_base is null; web UI not attached");
  }
}

void NanoleafCanvas::loop() {
  // Web requests queued from the HTTP task.
  {
    LockGuard lock(this->job_mutex_);
    if (this->pending_job_ != nullptr) {
      LoopJob *job = this->pending_job_;
      this->pending_job_ = nullptr;
      job->result = job->fn();
      job->done.store(true);
    }
  }

  uint32_t now = millis();
  if (this->enumerate_requested_ || (!this->enumerated_ && now - this->last_re_enum_ms_ >= 3000)) {
    this->enumerate_requested_ = false;
    this->last_re_enum_ms_ = now;
    enumerate_cluster();
    now = millis();
  }

  if (this->enumerated_ && !this->no_poll_ && now - this->last_poll_ms_ >= POLL_INTERVAL_MS) {
    this->last_poll_ms_ = now;
    poll_bulk();
    now = millis();
  }

  flush_frames(now);
}

void NanoleafCanvas::dump_config() {
  ESP_LOGCONFIG(TAG, "Nanoleaf Canvas:");
  ESP_LOGCONFIG(TAG, "  Data Pin: GPIO%d", this->data_pin_);
  ESP_LOGCONFIG(TAG, "  Panels: %u", (unsigned) this->tiles_.size());
  ESP_LOGCONFIG(TAG, "  Enumerated: %s", YESNO(this->enumerated_));
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------
void CanvasRainbowWaveEffect::apply() {
  uint32_t now = millis();
  if (this->last_ms_ != 0 && now - this->last_ms_ < 50)
    return;
  this->last_ms_ = now;
  this->hue_ = (this->hue_ + 4) % 360;
  const auto &tiles = this->parent_->get_tiles();
  for (size_t i = 0; i < tiles.size(); i++) {
    int offset = (tiles[i].x * 40 + tiles[i].y * 25) % 360;
    if (offset < 0)
      offset += 360;
    uint8_t r, g, b;
    hsv2rgb(this->hue_ + offset, 255, 255, r, g, b);
    this->parent_->set_tile_color(i, r, g, b);
  }
}

void CanvasStaticRainbowEffect::apply() {
  if (this->painted_)
    return;
  this->painted_ = true;
  const auto &tiles = this->parent_->get_tiles();
  size_t n = tiles.size();
  for (size_t i = 0; i < n; i++) {
    uint8_t r, g, b;
    hsv2rgb(i * 360 / (n ? n : 1), 255, 255, r, g, b);
    this->parent_->set_tile_color(i, r, g, b);
  }
}

// ---------------------------------------------------------------------------
// Light output
// ---------------------------------------------------------------------------
void NanoleafCanvasLight::setup_state(light::LightState *state) {
  this->parent_->set_light_state(state);
  if (state->get_effect_count() == 0) {
    state->add_effects({new CanvasRainbowWaveEffect(this->parent_), new CanvasStaticRainbowEffect(this->parent_)});
  }
}

light::LightTraits NanoleafCanvasLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(153.8f);  // 6500K
  traits.set_max_mireds(833.3f);  // 1200K
  return traits;
}

void NanoleafCanvasLight::write_state(light::LightState *state) {
  const auto &v = state->current_values;

  // Brightness goes out once, through the panels' own brightness control (FC 04).
  // The RGBW chunk carries only the color at full scale, so dimming never
  // quantizes the color channels (which shifted hue at low brightness).
  float bri = v.get_state() * v.get_brightness();
  uint8_t bri8 = (uint8_t) roundf(state->gamma_correct_lut(bri) * 255.0f);
  if (bri > 0.0f && bri8 == 0)
    bri8 = 1;

  uint8_t r = 0, g = 0, b = 0, w = 0;

  if (v.get_color_mode() == light::ColorMode::COLOR_TEMPERATURE) {
    float kelvin = v.get_color_temperature_kelvin();
    kelvin_to_rgbw(kelvin, r, g, b, w);
  } else {
    float cb = v.get_color_brightness();
    float rf = state->gamma_correct_lut(v.get_red() * cb);
    float gf = state->gamma_correct_lut(v.get_green() * cb);
    float bf = state->gamma_correct_lut(v.get_blue() * cb);

    // Desaturation white extraction: extract common white component to dedicated W LED
    float min_c = std::min(rf, std::min(gf, bf));
    float wf = min_c;
    rf -= min_c;
    gf -= min_c;
    bf -= min_c;

    r = (uint8_t) roundf(rf * 255.0f);
    g = (uint8_t) roundf(gf * 255.0f);
    b = (uint8_t) roundf(bf * 255.0f);
    w = (uint8_t) roundf(wf * 255.0f);
  }

  bool repaint = this->parent_->consume_color_repaint();
  bool effect_active = state->get_current_effect_index() != 0;
  bool changed = !this->have_last_ || r != this->last_r_ || g != this->last_g_ || b != this->last_b_ || w != this->last_w_;
  // Only repaint on a color change, so per-tile colors survive dimming.
  if (!effect_active && (changed || repaint)) {
    this->parent_->set_all_colors(r, g, b, w);
  }
  this->have_last_ = true;
  this->last_r_ = r;
  this->last_g_ = g;
  this->last_b_ = b;
  this->last_w_ = w;

  this->parent_->set_brightness(bri8);
}

}  // namespace nanoleaf_canvas
}  // namespace esphome
