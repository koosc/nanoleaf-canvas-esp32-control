# Nanoleaf Canvas ESP32 Controller & ESPHome Integration

Custom ESP32 firmware and ESPHome component to control **Nanoleaf Canvas (NL29)** modular square light tiles directly over their 1 Mbaud single-wire UART panel bus — replacing the stock controller with complete local control, zero cloud dependencies, native Home Assistant integration, and an embedded interactive 2D placement web UI.

---

## Features

- **Direct Hardware Bus Control**: Replaces the stock Nanoleaf Canvas master controller using an ESP32-C3 via a single GPIO pin configured for 1 Mbaud open-drain UART.
- **Cluster Enumeration & 2D Layout Geometry**: Automatic tree discovery and 2D Cartesian coordinate decoding of all attached tiles directly from the bus layout string.
- **Native Home Assistant Integration**: Exposes an ESPHome Light entity with state synchronization, RGB color picker, brightness slider, and instant transitions.
- **Embedded Web UI & Placement Visualizer**:
  - Hosted directly on the ESP32 (port 80).
  - Dynamically renders an SVG floorplan of the physical tile arrangement.
  - Interactive per-tile color picker and state preview.
  - Global color presets, brightness control, and dynamic effects (e.g., flowing rainbow wave, static rainbow).
- **Dual-Stack Color Protocol**: Supports both legacy global/addressed color frames (`FC 06` / `F8 id 06`) and reverse-chunk bulk push frames (`E0 03`), ensuring compatibility across Canvas firmware revisions.
- **Robust Bus Health & Hot-Plug Recovery**: 50 ms bulk status polling (`0xC0`), touch reporting (`FC 07`), and automatic re-enumeration on hot-swap events (`0xCC`).
- **Live Diagnostics API**: Endpoints to inspect bus echo integrity, cluster topology, transaction counts, and raw poll replies.

---

## Hardware & Wiring

### Requirements
- **Nanoleaf Canvas Tiles (NL29)** with their standard 24V power supply.
- **ESP32-C3** (or standard ESP32).
- **Canvas Linker Board**: Used to connect the ESP32 to an edge connector on any Canvas tile.

### Pinout & Wiring

| ESP32-C3 Pin | Canvas Linker Pad | Function | Notes |
| :--- | :--- | :--- | :--- |
| **GPIO4** | **DATA** (Center pad) | 1 Mbaud Bidirectional Bus | Internal pull-up enabled, Open-Drain mode |
| **GND** | **GND** (Outer pad) | Ground Reference | Common ground with power supply |
| *Do not connect* | **V+ (24V)** | Tile Power Supply | **WARNING**: Canvas operates at 24V! Do NOT connect directly to ESP32 3.3V/5V! |

> [!WARNING]
> The Canvas power rail carries **24 V DC**. Only connect **DATA** and **GND** between the ESP32 and the tile linker. Power the ESP32 separately via USB (or via a step-down buck converter from the 24V rail).

---

## Quick Start (ESPHome)

### 1. Project Structure
```text
.
├── components/
│   └── nanoleaf_canvas/          # ESPHome custom external component
│       ├── __init__.py
│       ├── canvas_web_page.h     # Embedded responsive HTML/CSS/JS web interface
│       ├── light.py              # ESPHome Light platform registration
│       ├── nanoleaf_canvas.cpp   # Core UART driver, cluster parser & API handler
│       └── nanoleaf_canvas.h
├── nanoleaf-canvas-chris.yaml    # ESPHome configuration file
├── CANVAS_PROTOCOL.md            # Reverse-engineered Canvas protocol documentation
├── PROTOCOL.md                   # Complete protocol specification
└── README.md
```

### 2. Configuration (`nanoleaf-canvas-chris.yaml`)
```yaml
esphome:
  name: nanoleaf-canvas
  friendly_name: Nanoleaf Canvas

esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf

external_components:
  - source:
      type: local
      path: components

wifi:
  ssid: "YourSSID"
  password: "YourPassword"

api:
ota:
  platform: esphome

web_server:
  port: 80

nanoleaf_canvas:
  id: canvas_hub
  data_pin: 4

light:
  - platform: nanoleaf_canvas
    name: "Nanoleaf Canvas"
    id: canvas_light
    restore_mode: RESTORE_DEFAULT_ON
    default_transition_length: 0s
```

### 3. Compile & Flash
Compile using Docker or native ESPHome:
```bash
docker run --rm -v $(pwd):/config esphome/esphome compile /config/nanoleaf-canvas-chris.yaml
```

Flash over serial or OTA:
```bash
docker run --rm --net=host -v $(pwd):/config esphome/esphome upload --device 192.168.1.106 /config/nanoleaf-canvas-chris.yaml
```

---

## Embedded Web Interface & REST API

Navigate to `http://<device-ip>/` in any browser to access the interactive controller:

- **Placement Canvas**: Click any individual tile in the SVG diagram to pick and apply a custom color.
- **All Tiles**: Change master brightness or set a uniform color for all tiles.
- **Effects**: Trigger built-in dynamic animations like the Flowing Rainbow Wave.

### REST Endpoints

| Endpoint | Method | Parameters | Description |
| :--- | :--- | :--- | :--- |
| `/api/tiles` | `GET` | — | Returns JSON array of tiles with Cartesian coordinates (`x`, `y`), rotation, and current RGB color. |
| `/api/set` | `GET` | `id`, `r`, `g`, `b` | Sets color of a single tile by node ID. |
| `/api/all` | `GET` | `r`, `g`, `b`, `[bri]` | Sets global color and/or brightness for all tiles. |
| `/api/effect` | `GET` | `name=wave\|off\|static_rainbow` | Starts or stops a dynamic lighting effect. |
| `/api/debug` | `GET` | — | Returns hardware bus echo status, enumeration state, poll counts, and raw poll reply bytes. |
| `/api/re-enumerate` | `GET` | — | Forces a full cluster enumeration (`00` + `80` layout walk). |
| `/api/reboot` | `GET` | — | Restarts the ESP32. |

---

## Protocol Documentation

For in-depth documentation on frame structures, packet timings, enumeration grammar, and reverse-engineered command sets, refer to:
- [`CANVAS_PROTOCOL.md`](CANVAS_PROTOCOL.md): Nanoleaf Canvas (NL29) protocol notes and command reference.
- [`PROTOCOL.md`](PROTOCOL.md): Complete technical specification of the Nanoleaf modular panel serial protocol.

---

## Credits & Prior Work

This project builds upon the foundational reverse-engineering work and documentation shared by the community:

- **Christian Panton**: For the pioneering analysis and deep-dive blog post on reverse engineering the Nanoleaf panel communication bus:
  - [**Nanoleaf Shapes deepdive, part 1: Reverse engineering the protocol**](https://christian.panton.org/posts/nanoleaf-ctrl-pt1/)
- **[LeafBus by MyrikLD](https://github.com/MyrikLD/LeafBus)**: For the comprehensive [`PROTOCOL.md`](PROTOCOL.md) technical specification documenting the serial physical layer, packet framing, and layout tree grammar.
- **[Cptmeme's Nanoleaf-Shapes-controller](https://github.com/Cptmeme/Nanoleaf-Shapes-controller)**: For invaluable observations on layout chunk ordering, hot-plug markers, and bus timing.

---

## License

This project is licensed under the MIT License. Nanoleaf is a trademark of Nanoleaf / NanoGrid Limited. This independent project is not affiliated with or endorsed by Nanoleaf.
