#include <Arduino.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

#define NANOLEAF_UART_NUM UART_NUM_1
#define DATA_PIN GPIO_NUM_4
#define BAUD_RATE 1000000

static bool enumerated = false;
static uint8_t n_panels = 1;
static uint32_t last_poll_ms = 0;
static bool auto_poll = true;
static uint8_t cur_brightness = 255;
static bool anim_wave = false;
static uint32_t last_anim_ms = 0;
static uint16_t anim_hue = 0;

int xact(const uint8_t *frame, size_t len, uint8_t *reply_buf, size_t max_reply, int expect = 0, uint32_t timeout_ms = 80);

void set_node_color(uint16_t node_id, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0, uint8_t trans = 1) {
    uint8_t frame[] = {0xF8, (uint8_t)(node_id & 0xFF), (uint8_t)(node_id >> 8), 0x06, r, g, b, w, trans};
    xact(frame, sizeof(frame), NULL, 0, 0, 30);
}

void configure_bus() {
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
        .pin_bit_mask = (1ULL << DATA_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    esp_rom_gpio_connect_out_signal(DATA_PIN, U1TXD_OUT_IDX, false, false);
    esp_rom_gpio_connect_in_signal(DATA_PIN, U1RXD_IN_IDX, false);

    Serial.printf("[INIT] Hardware UART1 running at 1 Mbaud on GPIO%d (Open-Drain)\n", DATA_PIN);
}

int xact(const uint8_t *frame, size_t len, uint8_t *reply_buf, size_t max_reply, int expect, uint32_t timeout_ms) {
    uart_flush_input(NANOLEAF_UART_NUM);

    uart_write_bytes(NANOLEAF_UART_NUM, (const char *)frame, len);
    uart_wait_tx_done(NANOLEAF_UART_NUM, pdMS_TO_TICKS(50));

    uint32_t start = millis();
    uint8_t rx_raw[1024];
    size_t total_rx = 0;

    while (millis() - start < timeout_ms) {
        size_t available = 0;
        uart_get_buffered_data_len(NANOLEAF_UART_NUM, &available);
        if (available > 0) {
            int r = uart_read_bytes(NANOLEAF_UART_NUM, rx_raw + total_rx,
                                    sizeof(rx_raw) - total_rx, pdMS_TO_TICKS(10));
            if (r > 0) {
                total_rx += r;
                if (expect > 0 && (int)total_rx >= (int)len + expect) break;
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
        delay(2);
    }

    size_t reply_len = 0;
    if (total_rx > len) {
        reply_len = total_rx - len;
        if (reply_len > max_reply) reply_len = max_reply;
        memcpy(reply_buf, rx_raw + len, reply_len);
    }
    return reply_len;
}

bool test_bus_echo() {
    const uint8_t pattern[] = {0x55, 0xAA, 0x55, 0xAA, 0x0F, 0xF0};
    int pass = 0;
    for (int t = 0; t < 10; t++) {
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
    Serial.printf("[ECHO] Bus signal test: %d/10 echo packets verified\n", pass);
    return pass >= 9;
}

void set_global_brightness(uint8_t bri) {
    cur_brightness = bri;
    const uint8_t bri_cmd[] = {0xFC, 0x04, cur_brightness};
    xact(bri_cmd, sizeof(bri_cmd), NULL, 0, 0, 30);
    Serial.printf("[CMD] Global Brightness set to %d\n", cur_brightness);
}

void set_global_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0, uint8_t trans = 1) {
    const uint8_t frame[] = {0xFC, 0x06, r, g, b, w, trans};
    xact(frame, sizeof(frame), NULL, 0, 0, 30);
    Serial.printf("[CMD] Global Color set: R=%d G=%d B=%d W=%d T=%d\n", r, g, b, w, trans);
}

void set_bulk_push_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0, uint8_t trans = 1) {
    // E0 03 followed by n_panels chunks of [05 trans R G B W]
    uint8_t frame[2 + 256 * 6];
    frame[0] = 0xE0;
    frame[1] = 0x03;
    size_t idx = 2;
    for (int i = 0; i < n_panels; i++) {
        frame[idx++] = 0x05;
        frame[idx++] = trans;
        frame[idx++] = r;
        frame[idx++] = g;
        frame[idx++] = b;
        frame[idx++] = w;
    }
    xact(frame, idx, NULL, 0, 0, 50);
    Serial.printf("[CMD] Bulk push color sent for %d panels\n", n_panels);
}

bool enumerate_panels() {
    Serial.println("\n[ENUM] Starting enumeration...");
    uint8_t reply[256];

    // Step 1: Root side detect
    const uint8_t root_cmd = 0x00;
    xact(&root_cmd, 1, reply, sizeof(reply), 0, 30);
    delay(50);

    // Step 2: Layout detect (allow up to 1000ms for multi-panel tree propagation)
    const uint8_t layout_cmd = 0x80;
    int rlen = xact(&layout_cmd, 1, reply, sizeof(reply), -1, 1000);

    if (rlen <= 0) {
        Serial.println("[ENUM] Failed: No response to 0x80");
        return false;
    }

    Serial.printf("[ENUM] Layout string (%d bytes): ", rlen);
    int panels = 0;
    int psus = 0;
    for (int i = 0; i < rlen; i++) {
        Serial.printf("%02X ", reply[i]);
        if (reply[i] & 0x80) {
            uint8_t code = (reply[i] >> 4) & 7;
            if (code == 1) {
                psus++;
            } else {
                panels++;
            }
        }
    }
    Serial.println();

    if (panels == 0) panels = 1;
    n_panels = panels;
    enumerated = true;
    Serial.printf("[ENUM] SUCCESS! Detected %d Canvas panel(s) and %d PSU node(s)\n", n_panels, psus);

    // Step 3: Set brightness
    set_global_brightness(cur_brightness);
    delay(20);

    // Step 4: Enable touch
    const uint8_t touch_cmd[] = {0xFC, 0x07, 0x01};
    xact(touch_cmd, sizeof(touch_cmd), reply, sizeof(reply), 0, 30);
    delay(20);

    // Step 5: Light up in bright green to verify immediate operation!
    set_global_color(0, 255, 0, 0, 2);

    return true;
}

void poll_bulk() {
    const uint8_t cmd = 0xC0;
    uint8_t reply[256];
    int rlen = xact(&cmd, 1, reply, sizeof(reply), n_panels * 2, 40 + n_panels * 15);

    if (rlen > 0) {
        for (int i = 0; i < rlen; i += 2) {
            uint8_t status = reply[i];
            if (status == 0xCC) {
                Serial.println("[EVENT] Hot-plug detected (0xCC)! Re-enumerating...");
                enumerate_panels();
                break;
            } else if ((status & 0x10) && (status & 0x0F)) {
                Serial.printf("[TOUCH] Panel %d touched! Code=0x%02X\n", i/2, status);
            }
        }
    }
}

// Convert HSV (H: 0-359, S: 0-255, V: 0-255) to RGB
void hsv2rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
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

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=======================================================");
    Serial.println("  ESP32-C3 Nanoleaf Canvas Cluster Controller");
    Serial.println("=======================================================");

    configure_bus();
    delay(100);

    test_bus_echo();
    delay(50);

    enumerate_panels();

    Serial.println("\nCommands:");
    Serial.println("  c <R> <G> <B> [W]     : Set global color (FC 06)");
    Serial.println("  node <id> <R> <G> <B> : Set specific panel color (F8 <id> 06)");
    Serial.println("  b <0-255>             : Set brightness");
    Serial.println("  wave                  : Toggle animated flowing rainbow wave");
    Serial.println("  enum                  : Re-enumerate cluster");
    Serial.println("  hex <data>            : Send raw hex bytes");
    Serial.println("=======================================================\n");
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.startsWith("c ")) {
            anim_wave = false;
            int r = 0, g = 0, b = 0, w = 0;
            sscanf(cmd.c_str() + 2, "%d %d %d %d", &r, &g, &b, &w);
            set_global_color(r, g, b, w, 2);
        } else if (cmd.startsWith("node ")) {
            anim_wave = false;
            int id = 0, r = 0, g = 0, b = 0, w = 0;
            sscanf(cmd.c_str() + 5, "%d %d %d %d %d", &id, &r, &g, &b, &w);
            set_node_color(id, r, g, b, w, 2);
            Serial.printf("[CMD] Set Node %d color: R=%d G=%d B=%d W=%d\n", id, r, g, b, w);
        } else if (cmd.startsWith("b ")) {
            int v = cmd.substring(2).toInt();
            set_global_brightness(constrain(v, 0, 255));
        } else if (cmd.equalsIgnoreCase("enum")) {
            enumerate_panels();
        } else if (cmd.equalsIgnoreCase("wave")) {
            anim_wave = !anim_wave;
            Serial.printf("Wave animation: %s\n", anim_wave ? "ON" : "OFF");
        } else if (cmd.startsWith("hex ")) {
            String hex_str = cmd.substring(4);
            hex_str.trim();
            uint8_t buf[256];
            size_t count = 0;
            int idx = 0;
            while (idx < hex_str.length() && count < sizeof(buf)) {
                while (idx < hex_str.length() && hex_str[idx] == ' ') idx++;
                if (idx >= hex_str.length()) break;
                int next_space = hex_str.indexOf(' ', idx);
                if (next_space == -1) next_space = hex_str.length();
                String token = hex_str.substring(idx, next_space);
                buf[count++] = (uint8_t)strtol(token.c_str(), NULL, 16);
                idx = next_space + 1;
            }
            if (count > 0) {
                uint8_t reply[256];
                int rlen = xact(buf, count, reply, sizeof(reply), 0, 80);
                Serial.printf("[HEX REPLY (%d bytes)]: ", rlen);
                for (int i = 0; i < rlen; i++) Serial.printf("%02X ", reply[i]);
                Serial.println();
            }
        }
    }

    uint32_t now = millis();

    // Flowing rainbow wave animation across the panels
    if (anim_wave && (now - last_anim_ms >= 80)) {
        last_anim_ms = now;
        anim_hue = (anim_hue + 5) % 360;
        for (uint16_t i = 0; i < n_panels; i++) {
            uint16_t panel_hue = (anim_hue + (i * 360 / n_panels)) % 360;
            uint8_t r, g, b;
            hsv2rgb(panel_hue, 255, 255, r, g, b);
            set_node_color(i, r, g, b, 0, 1);
            delay(2);
        }
    }

    // 50ms periodic bulk poll
    if (auto_poll && enumerated && (now - last_poll_ms >= 50)) {
        last_poll_ms = now;
        poll_bulk();
    }

    delay(2);
}

